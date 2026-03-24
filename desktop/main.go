package main

import (
	_ "embed"
	"encoding/json"
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
	adapter     = bluetooth.DefaultAdapter
)

// ── BLE connection state ──────────────────────────────────────────────────────

type bleConn struct {
	device     bluetooth.Device
	readChar   bluetooth.DeviceCharacteristic
	writeChar  bluetooth.DeviceCharacteristic
	rebootChar bluetooth.DeviceCharacteristic
	otaChar    bluetooth.DeviceCharacteristic
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
	c := getConn()
	if c == nil {
		json.NewEncoder(w).Encode(map[string]any{"connected": false})
		return
	}
	// Ping the BLE link: a failed read means the device is gone.
	type pingResult struct{ ok bool }
	ch := make(chan pingResult, 1)
	go func() {
		buf := make([]byte, 32)
		_, err := c.readChar.Read(buf)
		ch <- pingResult{err == nil}
	}()
	select {
	case res := <-ch:
		if !res.ok {
			setConn(nil)
		}
		json.NewEncoder(w).Encode(map[string]any{"connected": res.ok})
	case <-time.After(3 * time.Second):
		setConn(nil)
		json.NewEncoder(w).Encode(map[string]any{"connected": false})
	}
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
		w.Write(buf[:n])

	case http.MethodPost:
		body, err := io.ReadAll(r.Body)
		if err != nil {
			w.WriteHeader(http.StatusInternalServerError)
			return
		}
		if _, err := c.writeChar.WriteWithoutResponse(body); err != nil {
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
		}
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
