package main

import (
	_ "embed"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"sync"
	"time"

	"tinygo.org/x/bluetooth"
	webview "github.com/webview/webview_go"
)

//go:embed index.html
var indexHTML string

// ── BLE UUIDs ─────────────────────────────────────────────────────────────────

func mustUUID(s string) bluetooth.UUID {
	u, err := bluetooth.ParseUUID(s)
	if err != nil {
		panic(err)
	}
	return u
}

var (
	serviceUUID = mustUUID("bb010000-feed-dead-beef-cafebabe0001")
	readUUID    = mustUUID("bb010001-feed-dead-beef-cafebabe0001")
	writeUUID   = mustUUID("bb010002-feed-dead-beef-cafebabe0001")
	rebootUUID  = mustUUID("bb010003-feed-dead-beef-cafebabe0001")
	otaUUID     = mustUUID("bb010004-feed-dead-beef-cafebabe0001")
	btnNotifyUUID = mustUUID("bb010005-feed-dead-beef-cafebabe0001")
	adapter     = bluetooth.DefaultAdapter
)

// ── Debug event broker (SSE) ──────────────────────────────────────────────────

type debugBroker struct {
	mu      sync.Mutex
	clients map[chan string]struct{}
}

var evtBroker = &debugBroker{clients: make(map[chan string]struct{})}

func (b *debugBroker) subscribe() chan string {
	ch := make(chan string, 32)
	b.mu.Lock()
	b.clients[ch] = struct{}{}
	b.mu.Unlock()
	return ch
}

func (b *debugBroker) unsubscribe(ch chan string) {
	b.mu.Lock()
	delete(b.clients, ch)
	b.mu.Unlock()
}

func (b *debugBroker) publish(msg string) {
	b.mu.Lock()
	defer b.mu.Unlock()
	for ch := range b.clients {
		select {
		case ch <- msg:
		default: // drop if client is slow
		}
	}
}

// ── BLE connection state ──────────────────────────────────────────────────────

type bleConn struct {
	device        bluetooth.Device
	readChar      bluetooth.DeviceCharacteristic
	writeChar     bluetooth.DeviceCharacteristic
	rebootChar    bluetooth.DeviceCharacteristic
	otaChar       bluetooth.DeviceCharacteristic
	btnNotifyChar bluetooth.DeviceCharacteristic
}

var (
	active *bleConn
	connMu sync.Mutex
)

func getConn() *bleConn { connMu.Lock(); defer connMu.Unlock(); return active }
func setConn(c *bleConn) { connMu.Lock(); defer connMu.Unlock(); active = c }

// ── main ─────────────────────────────────────────────────────────────────────

func main() {
	if err := adapter.Enable(); err != nil {
		panic("Bluetooth not available: " + err.Error())
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.Write([]byte(indexHTML))
	})
	mux.HandleFunc("/discover", handleDiscover)
	mux.HandleFunc("/status", handleStatus)
	mux.HandleFunc("/config", handleConfig)
	mux.HandleFunc("/ota", handleOTA)
	mux.HandleFunc("/debug/events", handleDebugEvents)

	go http.ListenAndServe("127.0.0.1:18080", mux)

	w := webview.New(false)
	defer w.Destroy()
	w.SetTitle("ButtonBox Config")
	w.SetSize(540, 900, webview.HintNone)
	w.Navigate("http://127.0.0.1:18080")
	w.Run()
}

// ── HTTP handlers ─────────────────────────────────────────────────────────────

func handleStatus(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	// Only checks whether Go still holds a connection object.
	// Actual BLE link failures are detected when /config or /ota operations fail.
	json.NewEncoder(w).Encode(map[string]any{"connected": getConn() != nil})
}

func handleDiscover(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	c, err := scanAndConnect()
	if err != nil {
		json.NewEncoder(w).Encode(map[string]any{"found": false, "error": err.Error()})
		return
	}
	setConn(c)
	json.NewEncoder(w).Encode(map[string]any{"found": true})
}

func handleConfig(w http.ResponseWriter, r *http.Request) {
	c := getConn()
	if c == nil {
		w.WriteHeader(http.StatusServiceUnavailable)
		w.Write([]byte(`{"error":"not connected"}`))
		return
	}
	w.Header().Set("Content-Type", "application/json")

	switch r.Method {
	case http.MethodGet:
		buf := make([]byte, 2048)
		n, err := c.readChar.Read(buf)
		if err != nil {
			w.WriteHeader(http.StatusBadGateway)
			json.NewEncoder(w).Encode(map[string]any{"error": err.Error()})
			return
		}
		if n == 0 {
			w.WriteHeader(http.StatusBadGateway)
			json.NewEncoder(w).Encode(map[string]any{"error": "empty read from device"})
			return
		}
		w.Write(buf[:n])

	case http.MethodPost:
		body, err := io.ReadAll(r.Body)
		if err != nil {
			w.WriteHeader(http.StatusInternalServerError)
			return
		}
		if err := writeChunked(c.writeChar, body); err != nil {
			w.WriteHeader(http.StatusBadGateway)
			json.NewEncoder(w).Encode(map[string]any{"error": err.Error()})
			return
		}
		// Trigger reboot — device disconnects immediately after
		c.rebootChar.WriteWithoutResponse([]byte{1})
		setConn(nil)
		json.NewEncoder(w).Encode(map[string]any{"ok": true})
	}
}

func handleDebugEvents(w http.ResponseWriter, r *http.Request) {
	flusher, ok := w.(http.Flusher)
	if !ok {
		w.WriteHeader(http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")

	ch := evtBroker.subscribe()
	defer evtBroker.unsubscribe(ch)

	for {
		select {
		case msg := <-ch:
			fmt.Fprintf(w, "data: %s\n\n", msg)
			flusher.Flush()
		case <-r.Context().Done():
			return
		}
	}
}

func handleOTA(w http.ResponseWriter, r *http.Request) {
	c := getConn()
	if c == nil {
		w.WriteHeader(http.StatusServiceUnavailable)
		w.Write([]byte(`{"error":"not connected"}`))
		return
	}
	w.Header().Set("Content-Type", "application/json")
	// Trigger OTA mode — device will stop BLE and start WiFi soft-AP
	c.otaChar.WriteWithoutResponse([]byte{1})
	setConn(nil)
	json.NewEncoder(w).Encode(map[string]any{"ok": true})
}

// ── Chunked BLE write ─────────────────────────────────────────────────────────

// writeChunked splits data into 180-byte chunks and sends them with a 1-byte
// header each: 0x01 = start, 0x02 = continue, 0x03 = end (triggers NVS save).
// This works around the single-packet limit of WriteWithoutResponse.
func writeChunked(char bluetooth.DeviceCharacteristic, data []byte) error {
	const chunkSize = 180
	total := len(data)
	for offset := 0; offset < total; {
		end := offset + chunkSize
		if end > total {
			end = total
		}
		var cmd byte
		if offset == 0 && end == total {
			cmd = 0x03 // only chunk: start + end combined
		} else if offset == 0 {
			cmd = 0x01 // start
		} else if end == total {
			cmd = 0x03 // end
		} else {
			cmd = 0x02 // continue
		}
		pkt := append([]byte{cmd}, data[offset:end]...)
		if _, err := char.WriteWithoutResponse(pkt); err != nil {
			return err
		}
		offset = end
		if cmd != 0x03 {
			time.Sleep(20 * time.Millisecond) // let firmware process each chunk
		}
	}
	return nil
}

// ── BLE discovery & connection ────────────────────────────────────────────────

func scanAndConnect() (*bleConn, error) {
	var (
		foundAddr bluetooth.Address
		found     bool
	)

	// Stop scan after 8 seconds if nothing found
	time.AfterFunc(8*time.Second, func() { adapter.StopScan() })

	err := adapter.Scan(func(_ *bluetooth.Adapter, result bluetooth.ScanResult) {
		if found {
			return
		}
		for _, u := range result.ServiceUUIDs() {
			if u == serviceUUID {
				found = true
				foundAddr = result.Address
				adapter.StopScan()
				return
			}
		}
	})
	if err != nil {
		return nil, err
	}
	if !found {
		return nil, errNotFound
	}

	device, err := adapter.Connect(foundAddr, bluetooth.ConnectionParams{})
	if err != nil {
		return nil, err
	}

	services, err := device.DiscoverServices([]bluetooth.UUID{serviceUUID})
	if err != nil || len(services) == 0 {
		device.Disconnect()
		return nil, errServiceNotFound
	}

	chars, err := services[0].DiscoverCharacteristics(nil)
	if err != nil {
		device.Disconnect()
		return nil, err
	}

	c := &bleConn{device: device}
	for _, ch := range chars {
		switch ch.UUID() {
		case readUUID:
			c.readChar = ch
		case writeUUID:
			c.writeChar = ch
		case rebootUUID:
			c.rebootChar = ch
		case otaUUID:
			c.otaChar = ch
		case btnNotifyUUID:
			c.btnNotifyChar = ch
		}
	}

	// Subscribe to button event notifications and forward to the SSE broker.
	if c.btnNotifyChar.UUID() == btnNotifyUUID {
		c.btnNotifyChar.EnableNotifications(func(buf []byte) {
			if len(buf) < 2 {
				return
			}
			press := buf[0] == 1
			btn := int(buf[1])
			evt := "release"
			if press {
				evt = "press"
			}
			msg, _ := json.Marshal(map[string]any{"btn": btn, "evt": evt})
			evtBroker.publish(string(msg))
		})
	}

	return c, nil
}

// sentinel errors
type bleError string

func (e bleError) Error() string { return string(e) }

const (
	errNotFound        bleError = "ButtonBox not found — make sure the device is powered on and Bluetooth is enabled"
	errServiceNotFound bleError = "ButtonBox service not found on device"
)
