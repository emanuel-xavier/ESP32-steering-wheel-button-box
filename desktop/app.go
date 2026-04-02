package main

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/wailsapp/wails/v2/pkg/runtime"
	"tinygo.org/x/bluetooth"
)

// ── BLE UUIDs ─────────────────────────────────────────────────────────────

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
	encCtrlUUID   = mustUUID("bb010006-feed-dead-beef-cafebabe0001")
	adapter       = bluetooth.DefaultAdapter
)

// ── BLE connection state ──────────────────────────────────────────────────

type bleConn struct {
	device        bluetooth.Device
	readChar      bluetooth.DeviceCharacteristic
	writeChar     bluetooth.DeviceCharacteristic
	rebootChar    bluetooth.DeviceCharacteristic
	otaChar       bluetooth.DeviceCharacteristic
	btnNotifyChar bluetooth.DeviceCharacteristic
	encCtrlChar   bluetooth.DeviceCharacteristic
	encodersOn    bool
	cfgChan       chan string
	cfgMu         sync.Mutex
	cfgJSON       string
}

var (
	active *bleConn
	connMu sync.Mutex
)

func getConn() *bleConn  { connMu.Lock(); defer connMu.Unlock(); return active }
func setConn(c *bleConn) { connMu.Lock(); defer connMu.Unlock(); active = c }

// ── App ────────────────────────────────────────────────────────────────────

type App struct {
	ctx context.Context
}

func NewApp() *App { return &App{} }

func (a *App) startup(ctx context.Context) {
	a.ctx = ctx
	if err := adapter.Enable(); err != nil {
		panic("Bluetooth not available: " + err.Error())
	}
}

// ── App methods (bound to JS via Wails) ───────────────────────────────────

func (a *App) Discover() map[string]any {
	fmt.Println("[discover] starting — disconnecting any stale connection")
	if old := getConn(); old != nil {
		fmt.Println("[discover] disconnecting previous device")
		old.device.Disconnect()
		setConn(nil)
	}
	fmt.Println("[discover] calling scanAndConnect…")
	c, err := a.scanAndConnect()
	if err != nil {
		fmt.Printf("[discover] failed: %v\n", err)
		return map[string]any{"found": false, "error": err.Error()}
	}
	setConn(c)
	fmt.Println("[discover] connected successfully")
	return map[string]any{"found": true}
}

func (a *App) GetStatus() map[string]any {
	return map[string]any{"connected": getConn() != nil}
}

func (a *App) GetConfig() (string, error) {
	c := getConn()
	if c == nil {
		return "", fmt.Errorf("not connected")
	}

	// Fast path: config already received via BLE NOTIFY since last connect.
	c.cfgMu.Lock()
	cached := c.cfgJSON
	c.cfgMu.Unlock()
	if cached != "" {
		fmt.Printf("[config] returning BLE-notified config (%d bytes)\n", len(cached))
		return cached, nil
	}

	// Wait briefly in case the notification is still in flight (~1 s delay on device).
	select {
	case j := <-c.cfgChan:
		fmt.Printf("[config] received via BLE notification (%d bytes)\n", len(j))
		return j, nil
	case <-time.After(2 * time.Second):
	}

	// Probe the BLE link before falling back to the local cache.
	buf := make([]byte, 4)
	if _, err := c.readChar.Read(buf); err != nil {
		fmt.Printf("[config] BLE link gone (%v) — disconnecting\n", err)
		setConn(nil)
		return "", fmt.Errorf("not connected")
	}

	// Fallback: local file written on the last successful save.
	if data, err := os.ReadFile(configFilePath()); err == nil && len(data) > 0 {
		fmt.Printf("[config] using locally cached config (%d bytes)\n", len(data))
		return string(data), nil
	}

	fmt.Println("[config] no cached config found — returning defaults")
	return "{}", nil
}

func (a *App) SaveConfig(payload string) error {
	c := getConn()
	if c == nil {
		return fmt.Errorf("not connected")
	}
	body := []byte(payload)
	if err := writeChunked(c.writeChar, body); err != nil {
		return err
	}
	c.rebootChar.WriteWithoutResponse([]byte{1})
	setConn(nil)
	if err := os.WriteFile(configFilePath(), body, 0644); err != nil {
		fmt.Printf("[config] warning: could not write local cache: %v\n", err)
	} else {
		fmt.Printf("[config] saved local cache (%d bytes)\n", len(body))
	}
	return nil
}

func (a *App) TriggerOTA() error {
	c := getConn()
	if c == nil {
		return fmt.Errorf("not connected")
	}
	c.otaChar.WriteWithoutResponse([]byte{1})
	setConn(nil)
	return nil
}

func (a *App) ClearCache() error {
	if err := os.Remove(configFilePath()); err != nil && !os.IsNotExist(err) {
		return err
	}
	fmt.Println("[cache] local config cache cleared")
	return nil
}

func (a *App) SetEncoders(enabled bool) error {
	c := getConn()
	if c == nil {
		return fmt.Errorf("not connected")
	}
	if c.encCtrlChar.UUID() != encCtrlUUID {
		return fmt.Errorf("firmware does not support runtime encoder toggle — please flash the latest firmware")
	}
	val := byte(0)
	if enabled {
		val = 1
	}
	if _, err := c.encCtrlChar.WriteWithoutResponse([]byte{val}); err != nil {
		return err
	}
	c.encodersOn = enabled
	return nil
}

// ── Local config file ─────────────────────────────────────────────────────

func configFilePath() string {
	home, _ := os.UserHomeDir()
	dir := filepath.Join(home, ".config", "buttonbox")
	os.MkdirAll(dir, 0755)
	return filepath.Join(dir, "config.json")
}

// ── Chunked BLE write ─────────────────────────────────────────────────────

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
			cmd = 0x03
		} else if offset == 0 {
			cmd = 0x01
		} else if end == total {
			cmd = 0x03
		} else {
			cmd = 0x02
		}
		pkt := append([]byte{cmd}, data[offset:end]...)
		if _, err := char.WriteWithoutResponse(pkt); err != nil {
			return err
		}
		offset = end
		if cmd != 0x03 {
			time.Sleep(20 * time.Millisecond)
		}
	}
	return nil
}

// ── BLE discovery & connection ────────────────────────────────────────────

func (a *App) scanAndConnect() (*bleConn, error) {
	var (
		foundAddr bluetooth.Address
		found     bool
	)

	fmt.Println("[scan] starting BLE scan (8 s timeout)…")
	time.AfterFunc(8*time.Second, func() {
		fmt.Println("[scan] timeout — stopping scan")
		adapter.StopScan()
	})

	err := adapter.Scan(func(_ *bluetooth.Adapter, result bluetooth.ScanResult) {
		fmt.Printf("[scan] device: addr=%s rssi=%d name=%q uuids=%v\n",
			result.Address.String(), result.RSSI, result.LocalName(), result.ServiceUUIDs())
		if found {
			return
		}
		for _, u := range result.ServiceUUIDs() {
			if u == serviceUUID {
				fmt.Printf("[scan] matched ButtonBox at %s\n", result.Address.String())
				found = true
				foundAddr = result.Address
				adapter.StopScan()
				return
			}
		}
	})
	if err != nil {
		fmt.Printf("[scan] Scan() error: %v\n", err)
		return nil, err
	}
	if !found {
		fmt.Println("[scan] no ButtonBox found after timeout")
		return nil, errNotFound
	}

	fmt.Printf("[scan] connecting to %s…\n", foundAddr.String())

	device, err := adapter.Connect(foundAddr, bluetooth.ConnectionParams{})
	if err != nil {
		fmt.Printf("[scan] Connect() error: %v\n", err)
		return nil, err
	}
	fmt.Println("[scan] connected — discovering services…")

	services, err := device.DiscoverServices([]bluetooth.UUID{serviceUUID})
	if err != nil {
		fmt.Printf("[scan] DiscoverServices() error: %v\n", err)
		device.Disconnect()
		return nil, errServiceNotFound
	}
	if len(services) == 0 {
		fmt.Println("[scan] DiscoverServices() returned 0 services — ButtonBox service not found")
		device.Disconnect()
		return nil, errServiceNotFound
	}
	fmt.Printf("[scan] found %d service(s) — discovering characteristics…\n", len(services))

	chars, err := services[0].DiscoverCharacteristics(nil)
	if err != nil {
		fmt.Printf("[scan] DiscoverCharacteristics() error: %v\n", err)
		device.Disconnect()
		return nil, err
	}

	c := &bleConn{
		device:     device,
		cfgChan:    make(chan string, 1),
		encodersOn: true,
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
		case encCtrlUUID:
			c.encCtrlChar = ch
		}
	}
	fmt.Printf("[scan] readChar assigned: %v\n", c.readChar.UUID().String())

	// Subscribe to button event notifications — emit to Wails event bus.
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
			runtime.EventsEmit(a.ctx, "btn-event", string(msg))
		}); err != nil {
			fmt.Printf("[btn notify] EnableNotifications failed: %v\n", err)
		}
	}

	// Subscribe to config read notifications (chunked 0x01/0x02/0x03 framing).
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
			if err := os.WriteFile(configFilePath(), []byte(result), 0644); err != nil {
				fmt.Printf("[config notify] warning: could not write local cache: %v\n", err)
			}
			select {
			case c.cfgChan <- result:
			default:
			}
		}
	}); err != nil {
		fmt.Printf("[config notify] EnableNotifications failed: %v\n", err)
	} else {
		fmt.Println("[config notify] subscribed OK — waiting for device to push config")
	}

	// Watchdog: poll readChar every 2 s to detect link loss.
	go func() {
		buf := make([]byte, 4)
		for {
			time.Sleep(2 * time.Second)
			if getConn() != c {
				return
			}
			_, err := c.readChar.Read(buf)
			if err != nil {
				fmt.Printf("[watchdog] BLE link lost (%v) — clearing connection\n", err)
				c.device.Disconnect()
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
