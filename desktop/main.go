package main

import (
	_ "embed"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
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
	serviceUUID   = mustUUID("bb010000-feed-dead-beef-cafebabe0001")
	readUUID      = mustUUID("bb010001-feed-dead-beef-cafebabe0001")
	writeUUID     = mustUUID("bb010002-feed-dead-beef-cafebabe0001")
	rebootUUID    = mustUUID("bb010003-feed-dead-beef-cafebabe0001")
	otaUUID       = mustUUID("bb010004-feed-dead-beef-cafebabe0001")
	btnNotifyUUID = mustUUID("bb010005-feed-dead-beef-cafebabe0001")
	adapter       = bluetooth.DefaultAdapter
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
	// Config JSON received via NOTIFY from the device (chunked, same framing as writes).
	// The device pushes it 1 second after the client connects.
	cfgChan chan string // delivers complete JSON after all chunks arrive
	cfgMu   sync.Mutex
	cfgJSON string // cached — returned on repeated GET /config calls
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
	mux.HandleFunc("/clearcache", handleClearCache)
	mux.HandleFunc("/debug/events", handleDebugEvents)

	go http.ListenAndServe("127.0.0.1:18080", mux)

	w := webview.New(false)
	defer w.Destroy()
	w.SetTitle("ButtonBox Config")
	w.SetSize(540, 900, webview.HintNone)
	w.Navigate("http://127.0.0.1:18080")
	w.Run()
}

// ── Local config file ─────────────────────────────────────────────────────────

// configFilePath returns ~/.config/buttonbox/config.json — the local cache
// written on every successful save so the next session has something to show.
func configFilePath() string {
	home, _ := os.UserHomeDir()
	dir := filepath.Join(home, ".config", "buttonbox")
	os.MkdirAll(dir, 0755)
	return filepath.Join(dir, "config.json")
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
		// Fast path: config received via BLE NOTIFY since last connect.
		c.cfgMu.Lock()
		cached := c.cfgJSON
		c.cfgMu.Unlock()
		if cached != "" {
			fmt.Printf("[config] returning BLE-notified config (%d bytes)\n", len(cached))
			w.Write([]byte(cached))
			return
		}
		// Wait briefly in case the notification is still in flight (~1s delay on device).
		select {
		case j := <-c.cfgChan:
			fmt.Printf("[config] received via BLE notification (%d bytes)\n", len(j))
			w.Write([]byte(j))
			return
		case <-time.After(2 * time.Second):
		}
		// Verify the BLE link is still alive before returning cached data.
		// Read returns (0, nil) when connected, (0, err) when the device is gone.
		buf := make([]byte, 4)
		if _, err := c.readChar.Read(buf); err != nil {
			fmt.Printf("[config] BLE link gone (%v) — disconnecting\n", err)
			setConn(nil)
			w.WriteHeader(http.StatusServiceUnavailable)
			w.Write([]byte(`{"error":"not connected"}`))
			return
		}
		// Fallback: local file written on the last successful save.
		if data, err := os.ReadFile(configFilePath()); err == nil && len(data) > 0 {
			fmt.Printf("[config] using locally cached config (%d bytes)\n", len(data))
			w.Write(data)
			return
		}
		// No cached data at all — return empty object so the UI shows defaults.
		fmt.Println("[config] no cached config found — returning defaults")
		w.Write([]byte("{}"))

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
		// Cache locally so the next GET returns the saved config without BLE read.
		if err := os.WriteFile(configFilePath(), body, 0644); err != nil {
			fmt.Printf("[config] warning: could not write local cache: %v\n", err)
		} else {
			fmt.Printf("[config] saved local cache (%d bytes)\n", len(body))
		}
		json.NewEncoder(w).Encode(map[string]any{"ok": true})
	}
}

func handleClearCache(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	if err := os.Remove(configFilePath()); err != nil && !os.IsNotExist(err) {
		w.WriteHeader(http.StatusInternalServerError)
		json.NewEncoder(w).Encode(map[string]any{"error": err.Error()})
		return
	}
	fmt.Println("[cache] local config cache cleared")
	json.NewEncoder(w).Encode(map[string]any{"ok": true})
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

	c := &bleConn{
		device:  device,
		cfgChan: make(chan string, 1),
	}
	fmt.Printf("[scan] discovered %d characteristics:\n", len(chars))
	for _, ch := range chars {
		fmt.Printf("  uuid=%s\n", ch.UUID().String())
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
	fmt.Printf("[scan] readChar assigned: %v\n", c.readChar.UUID().String())

	// Subscribe to button event notifications and forward to the SSE broker.
	if c.btnNotifyChar.UUID() == btnNotifyUUID {
		if err := c.btnNotifyChar.EnableNotifications(func(buf []byte) {
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
		}); err != nil {
			fmt.Printf("[btn notify] EnableNotifications failed: %v\n", err)
		}
	}

	// Subscribe to config read notifications.
	// The device pushes the config JSON when the CCCD is written (onSubscribe callback).
	// Same 0x01/0x02/0x03 chunked framing as writes — reassembled here.
	var cfgBuf strings.Builder
	if err := c.readChar.EnableNotifications(func(buf []byte) {
		if len(buf) < 1 {
			return
		}
		cmd := buf[0]
		chunk := string(buf[1:])
		switch cmd {
		case 0x01:
			cfgBuf.Reset()
			cfgBuf.WriteString(chunk)
		case 0x02:
			cfgBuf.WriteString(chunk)
		case 0x03:
			cfgBuf.WriteString(chunk)
			result := cfgBuf.String()
			cfgBuf.Reset()
			fmt.Printf("[config notify] received complete config (%d bytes)\n", len(result))
			c.cfgMu.Lock()
			c.cfgJSON = result
			c.cfgMu.Unlock()
			// Persist to local file so reconnect gaps and Refresh always have data.
			if err := os.WriteFile(configFilePath(), []byte(result), 0644); err != nil {
				fmt.Printf("[config notify] warning: could not write local cache: %v\n", err)
			}
			select {
			case c.cfgChan <- result:
			default: // already buffered
			}
		}
	}); err != nil {
		fmt.Printf("[config notify] EnableNotifications failed: %v\n", err)
	} else {
		fmt.Println("[config notify] subscribed OK — waiting for device to push config")
	}

	// Disconnect watchdog: poll readChar every 2 s.
	// Read returns (0, nil) when connected, (0, err) when the link is gone.
	// Clears active conn so /status reflects reality.
	go func() {
		buf := make([]byte, 4)
		for {
			time.Sleep(2 * time.Second)
			if getConn() != c {
				return // connection was replaced or already cleared
			}
			_, err := c.readChar.Read(buf)
			if err != nil {
				fmt.Printf("[watchdog] BLE link lost (%v) — clearing connection\n", err)
				if getConn() == c {
					setConn(nil)
				}
				return
			}
		}
	}()

	return c, nil
}

// sentinel errors
type bleError string

func (e bleError) Error() string { return string(e) }

const (
	errNotFound        bleError = "ButtonBox not found — make sure the device is powered on and Bluetooth is enabled"
	errServiceNotFound bleError = "ButtonBox service not found on device"
)
