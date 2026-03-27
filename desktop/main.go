package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/app"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/dialog"
	"fyne.io/fyne/v2/widget"
	"tinygo.org/x/bluetooth"
)

// ── Config struct ─────────────────────────────────────────────────────────────

type DeviceConfig struct {
	BLEDeviceName          string  `json:"bleDeviceName"`
	OTAPassword            string  `json:"otaPassword"`
	UseDirectButtons       bool    `json:"useDirectButtons"`
	NumButtons             int     `json:"numButtons"`
	ButtonPins             []int   `json:"buttonPins"`
	ButtonInputModes       []int   `json:"buttonInputModes"`
	DebounceDelayMs        int     `json:"debounceDelayMs"`
	UseEncoders            bool    `json:"useEncoders"`
	EncoderDebounceUs      int     `json:"encoderDebounceUs"`
	EncoderPressDurationMs int     `json:"encoderPressDurationMs"`
	EncoderTaskDelayMs     int     `json:"encoderTaskDelayMs"`
	ButtonTaskDelayMs      int     `json:"buttonTaskDelayMs"`
	EncoderZonesMode       bool    `json:"encoderZonesMode"`
	EncoderZoneSteps       int     `json:"encoderZoneSteps"`
	EncoderZoneCount       int     `json:"encoderZoneCount"`
	EncoderZoneMaster      int     `json:"encoderZoneMaster"`
	EncoderZoneResetButtons []int  `json:"encoderZoneResetButtons"`
	EncoderPins            [][]int `json:"encoderPins"`
	UseMatrix              bool    `json:"useMatrix"` // always false, disabled
	RecoveryOccurred       bool    `json:"recoveryOccurred"`
}

var defaultBtnPins = []int{2, 5, 13, 14, 15, 17, 18, 19, 21, 22, 23, 25, 32, 33}

func defaultConfig() DeviceConfig {
	pins := make([]int, 14)
	modes := make([]int, 14)
	copy(pins, defaultBtnPins)
	return DeviceConfig{
		BLEDeviceName:          "ESP32-steering-wheel",
		UseDirectButtons:       true,
		NumButtons:             14,
		ButtonPins:             pins,
		ButtonInputModes:       modes,
		DebounceDelayMs:        5,
		UseEncoders:            false,
		EncoderDebounceUs:      1000,
		EncoderPressDurationMs: 100,
		EncoderTaskDelayMs:     5,
		ButtonTaskDelayMs:      5,
		EncoderZoneSteps:       20,
		EncoderZoneCount:       2,
		EncoderZoneMaster:      0,
		EncoderZoneResetButtons: []int{},
		EncoderPins:            [][]int{{26, 27}, {4, 5}},
	}
}

func configFilePath() string {
	home, _ := os.UserHomeDir()
	dir := filepath.Join(home, ".config", "buttonbox")
	os.MkdirAll(dir, 0755)
	return filepath.Join(dir, "config.json")
}

func loadCachedConfig() (DeviceConfig, bool) {
	data, err := os.ReadFile(configFilePath())
	if err != nil || len(data) == 0 {
		return DeviceConfig{}, false
	}
	var cfg DeviceConfig
	if err := json.Unmarshal(data, &cfg); err != nil {
		return DeviceConfig{}, false
	}
	return cfg, true
}

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

// ── BLE connection state ──────────────────────────────────────────────────────

type bleConn struct {
	device        bluetooth.Device
	readChar      bluetooth.DeviceCharacteristic
	writeChar     bluetooth.DeviceCharacteristic
	rebootChar    bluetooth.DeviceCharacteristic
	otaChar       bluetooth.DeviceCharacteristic
	btnNotifyChar bluetooth.DeviceCharacteristic
	cfgChan       chan DeviceConfig
	cfgMu         sync.Mutex
	cfgJSON       string
}

var (
	active *bleConn
	connMu sync.Mutex
)

func getConn() *bleConn { connMu.Lock(); defer connMu.Unlock(); return active }
func setConn(c *bleConn) { connMu.Lock(); defer connMu.Unlock(); active = c }

// sentinel errors
type bleError string

func (e bleError) Error() string { return string(e) }

const (
	errNotFound        bleError = "ButtonBox not found — make sure the device is powered on and Bluetooth is enabled"
	errServiceNotFound bleError = "ButtonBox service not found on device"
)

// ── Chunked BLE write ─────────────────────────────────────────────────────────

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

// ── BLE discovery & connection ────────────────────────────────────────────────

func scanAndConnect(onBtn func(btn int, press bool)) (*bleConn, error) {
	var (
		foundAddr bluetooth.Address
		found     bool
	)

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
		cfgChan: make(chan DeviceConfig, 1),
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

	// Subscribe to button event notifications.
	if c.btnNotifyChar.UUID() == btnNotifyUUID {
		if err := c.btnNotifyChar.EnableNotifications(func(buf []byte) {
			if len(buf) < 2 {
				return
			}
			press := buf[0] == 1
			btn := int(buf[1])
			if onBtn != nil {
				onBtn(btn, press)
			}
		}); err != nil {
			fmt.Printf("[btn notify] EnableNotifications failed: %v\n", err)
		}
	}

	// Subscribe to config read notifications.
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
			var cfg DeviceConfig
			if err := json.Unmarshal([]byte(result), &cfg); err == nil {
				select {
				case c.cfgChan <- cfg:
				default:
				}
			}
		}
	}); err != nil {
		fmt.Printf("[config notify] EnableNotifications failed: %v\n", err)
	} else {
		fmt.Println("[config notify] subscribed OK — waiting for device to push config")
	}

	// Disconnect watchdog.
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
				if getConn() == c {
					setConn(nil)
				}
				return
			}
		}
	}()

	return c, nil
}

// ── UI state machine ──────────────────────────────────────────────────────────

type connState int

const (
	stateScanning connState = iota
	stateFound
	stateNotFound
	stateReconnecting
)

// ── Pin cell widgets ──────────────────────────────────────────────────────────

type pinCell struct {
	pinEntry   *widget.Entry
	modeSelect *widget.Select
}

// ── App UI ────────────────────────────────────────────────────────────────────

type appUI struct {
	win fyne.Window

	// Connection card widgets
	scanProgress    *widget.ProgressBarInfinite
	scanLabel       *widget.Label
	foundLabel      *widget.Label
	rescanBtn       *widget.Button
	debugToggleBtn  *widget.Button
	notFoundLabel   *widget.Label
	retryBtn        *widget.Button
	reconnectProgress *widget.ProgressBarInfinite
	reconnectLabel  *widget.Label

	connStateBox *fyne.Container // holds all connection sub-rows

	scanRow       *fyne.Container
	foundRow      *fyne.Container
	notFoundRow   *fyne.Container
	reconnectRow  *fyne.Container

	// Recovery banner
	recoveryCard *widget.Card

	// Config section (entire container shown/hidden)
	configSection *fyne.Container

	// Device card fields
	bleNameEntry *widget.Entry
	otaPassEntry *widget.Entry

	// Pin layout
	useDirectCheck  *widget.Check
	numButtonsEntry *widget.Entry
	pinGridWrap     *fyne.Container
	pinCells        []pinCell
	directOptions   *fyne.Container

	// Encoders
	useEncoderCheck    *widget.Check
	encoderModeRadio   *widget.RadioGroup
	zoneMasterRadio    *widget.RadioGroup
	zoneStepsEntry     *widget.Entry
	zoneCountSelect    *widget.Select
	resetCheckboxes    []*widget.Check
	resetRow           *fyne.Container
	encPin0CLK         *widget.Entry
	encPin0DT          *widget.Entry
	encPin1CLK         *widget.Entry
	encPin1DT          *widget.Entry
	zoneFields         *fyne.Container
	encoderOptions     *fyne.Container

	// Timing
	debounceEntry        *widget.Entry
	btnTaskDelayEntry    *widget.Entry
	encDebounceEntry     *widget.Entry
	encPressDurEntry     *widget.Entry
	encTaskDelayEntry    *widget.Entry

	// Debug panel
	debugCard       *widget.Card
	debugVisible    bool
	pressedLabel    *widget.Label
	logBox          *fyne.Container
	logScroll       *container.Scroll
	pressedBtns     map[int]bool
	pressedMu       sync.Mutex

	// Scroll container for whole UI
	scroll *container.Scroll
}

func newUI(w fyne.Window) *appUI {
	ui := &appUI{
		win:         w,
		pressedBtns: make(map[int]bool),
	}
	ui.build()
	return ui
}

func (ui *appUI) build() {
	// ── Connection card ───────────────────────────────────────────────────────

	ui.scanProgress = widget.NewProgressBarInfinite()
	ui.scanLabel = widget.NewLabel("Scanning for ButtonBox...")
	ui.scanRow = container.NewHBox(ui.scanProgress, ui.scanLabel)

	ui.foundLabel = widget.NewLabelWithStyle("ButtonBox  ●", fyne.TextAlignLeading, fyne.TextStyle{Bold: true})
	ui.rescanBtn = widget.NewButton("Re-scan", nil)
	ui.debugToggleBtn = widget.NewButton("Debug", nil)
	ui.foundRow = container.NewHBox(ui.foundLabel, ui.rescanBtn, ui.debugToggleBtn)

	ui.notFoundLabel = widget.NewLabel("Device not found. Make sure the ESP32 is powered on and Bluetooth is enabled.")
	ui.notFoundLabel.Wrapping = fyne.TextWrapWord
	ui.retryBtn = widget.NewButton("Retry scan", nil)
	ui.notFoundRow = container.NewVBox(ui.notFoundLabel, ui.retryBtn)

	ui.reconnectProgress = widget.NewProgressBarInfinite()
	ui.reconnectLabel = widget.NewLabel("Connection lost. Reconnecting...")
	ui.reconnectRow = container.NewHBox(ui.reconnectProgress, ui.reconnectLabel)

	ui.connStateBox = container.NewVBox(
		ui.scanRow,
		ui.foundRow,
		ui.notFoundRow,
		ui.reconnectRow,
	)
	connCard := widget.NewCard("Connection", "", ui.connStateBox)

	// ── Recovery banner ───────────────────────────────────────────────────────

	recoveryText := widget.NewLabel("Configuration reset to defaults — device detected a crash-loop.")
	recoveryText.Wrapping = fyne.TextWrapWord
	ui.recoveryCard = widget.NewCard("", "⚠ Configuration reset to defaults", recoveryText)

	// ── Device card ───────────────────────────────────────────────────────────

	ui.bleNameEntry = widget.NewEntry()
	ui.bleNameEntry.SetPlaceHolder("ESP32-steering-wheel")
	ui.otaPassEntry = widget.NewEntry()
	ui.otaPassEntry.SetPlaceHolder("(leave empty for none)")

	deviceForm := widget.NewForm(
		widget.NewFormItem("BLE Device Name", ui.bleNameEntry),
		widget.NewFormItem("OTA Password", ui.otaPassEntry),
	)
	deviceCard := widget.NewCard("Device", "", deviceForm)

	// ── Pin layout card ───────────────────────────────────────────────────────

	ui.useDirectCheck = widget.NewCheck("Use Direct Buttons", nil)
	ui.useDirectCheck.SetChecked(true)
	ui.numButtonsEntry = widget.NewEntry()
	ui.numButtonsEntry.SetText("14")

	ui.pinGridWrap = container.NewGridWrap(fyne.NewSize(72, 90))
	ui.pinCells = nil

	numBtnsForm := widget.NewForm(
		widget.NewFormItem("Number of Buttons", ui.numButtonsEntry),
	)
	ui.directOptions = container.NewVBox(
		numBtnsForm,
		widget.NewLabel("Button Pins (GPIO pin + mode per button):"),
		ui.pinGridWrap,
	)

	ui.useDirectCheck.OnChanged = func(on bool) {
		if on {
			ui.directOptions.Show()
		} else {
			ui.directOptions.Hide()
		}
	}

	ui.numButtonsEntry.OnChanged = func(s string) {
		n, err := strconv.Atoi(strings.TrimSpace(s))
		if err != nil || n < 1 {
			n = 1
		}
		if n > 32 {
			n = 32
		}
		ui.rebuildPinGrid(n, nil, nil)
	}

	ui.rebuildPinGrid(14, nil, nil)

	pinCard := widget.NewCard("Pin Layout", "", container.NewVBox(
		ui.useDirectCheck,
		ui.directOptions,
	))

	// ── Encoders card ─────────────────────────────────────────────────────────

	ui.useEncoderCheck = widget.NewCheck("Use Encoders", nil)

	ui.encoderModeRadio = widget.NewRadioGroup([]string{"Normal", "Zones"}, nil)
	ui.encoderModeRadio.SetSelected("Normal")

	ui.zoneMasterRadio = widget.NewRadioGroup([]string{"Enc 1", "Enc 2"}, nil)
	ui.zoneMasterRadio.SetSelected("Enc 1")

	ui.zoneStepsEntry = widget.NewEntry()
	ui.zoneStepsEntry.SetText("20")

	ui.zoneCountSelect = widget.NewSelect([]string{"2"}, nil)
	ui.zoneCountSelect.SetSelected("2")

	ui.zoneStepsEntry.OnChanged = func(s string) {
		ui.rebuildZoneCountOptions()
	}

	resetLabel := widget.NewLabel("Reset Combo (hold buttons to reset zone to 0):")
	ui.resetRow = container.NewHBox()
	ui.rebuildResetCheckboxes(14)

	ui.zoneFields = container.NewVBox(
		widget.NewForm(
			widget.NewFormItem("Master Encoder", ui.zoneMasterRadio),
			widget.NewFormItem("Zone Steps", ui.zoneStepsEntry),
			widget.NewFormItem("Zone Count", ui.zoneCountSelect),
		),
		resetLabel,
		ui.resetRow,
	)

	ui.encPin0CLK = widget.NewEntry()
	ui.encPin0CLK.SetText("26")
	ui.encPin0DT = widget.NewEntry()
	ui.encPin0DT.SetText("27")
	ui.encPin1CLK = widget.NewEntry()
	ui.encPin1CLK.SetText("4")
	ui.encPin1DT = widget.NewEntry()
	ui.encPin1DT.SetText("5")

	enc0Row := container.NewGridWithColumns(4,
		widget.NewLabel("CLK"), ui.encPin0CLK,
		widget.NewLabel("DT"), ui.encPin0DT,
	)
	enc1Row := container.NewGridWithColumns(4,
		widget.NewLabel("CLK"), ui.encPin1CLK,
		widget.NewLabel("DT"), ui.encPin1DT,
	)

	ui.encoderOptions = container.NewVBox(
		widget.NewForm(
			widget.NewFormItem("Encoder Mode", ui.encoderModeRadio),
		),
		ui.zoneFields,
		widget.NewForm(
			widget.NewFormItem("Encoder 1 Pins", enc0Row),
			widget.NewFormItem("Encoder 2 Pins", enc1Row),
		),
	)

	ui.encoderModeRadio.OnChanged = func(s string) {
		if s == "Zones" {
			ui.zoneFields.Show()
		} else {
			ui.zoneFields.Hide()
		}
	}
	ui.zoneFields.Hide()

	ui.useEncoderCheck.OnChanged = func(on bool) {
		if on {
			ui.encoderOptions.Show()
		} else {
			ui.encoderOptions.Hide()
		}
	}
	ui.encoderOptions.Hide()

	encoderCard := widget.NewCard("Encoders", "", container.NewVBox(
		ui.useEncoderCheck,
		ui.encoderOptions,
	))

	// ── Timing card ───────────────────────────────────────────────────────────

	ui.debounceEntry = widget.NewEntry()
	ui.debounceEntry.SetText("5")
	ui.btnTaskDelayEntry = widget.NewEntry()
	ui.btnTaskDelayEntry.SetText("5")
	ui.encDebounceEntry = widget.NewEntry()
	ui.encDebounceEntry.SetText("1000")
	ui.encPressDurEntry = widget.NewEntry()
	ui.encPressDurEntry.SetText("100")
	ui.encTaskDelayEntry = widget.NewEntry()
	ui.encTaskDelayEntry.SetText("5")

	timingForm := widget.NewForm(
		widget.NewFormItem("Debounce (ms)", ui.debounceEntry),
		widget.NewFormItem("Button task delay (ms)", ui.btnTaskDelayEntry),
		widget.NewFormItem("Encoder debounce (µs)", ui.encDebounceEntry),
		widget.NewFormItem("Encoder press duration (ms)", ui.encPressDurEntry),
		widget.NewFormItem("Encoder task delay (ms)", ui.encTaskDelayEntry),
	)
	timingCard := widget.NewCard("Timing", "", timingForm)

	// ── Actions ───────────────────────────────────────────────────────────────

	saveBtn := widget.NewButton("Save & Reboot", func() { ui.doSave() })
	saveBtn.Importance = widget.HighImportance
	refreshBtn := widget.NewButton("Refresh", func() { ui.doRefresh() })
	otaBtn := widget.NewButton("OTA Mode", func() { ui.doOTA() })
	clearCacheBtn := widget.NewButton("Clear Cache", func() { ui.doClearCache() })
	actionsRow := container.NewHBox(saveBtn, refreshBtn, otaBtn, clearCacheBtn)

	// ── Debug card ────────────────────────────────────────────────────────────

	ui.pressedLabel = widget.NewLabel("— none —")
	ui.logBox = container.NewVBox()
	ui.logScroll = container.NewVScroll(ui.logBox)
	ui.logScroll.SetMinSize(fyne.NewSize(0, 220))

	debugContent := container.NewVBox(
		widget.NewLabel("Currently pressed:"),
		ui.pressedLabel,
		widget.NewSeparator(),
		widget.NewLabel("Event log:"),
		ui.logScroll,
	)
	ui.debugCard = widget.NewCard("Debug Monitor", "", debugContent)

	// ── Wire Debug toggle ─────────────────────────────────────────────────────

	ui.debugToggleBtn.OnTapped = func() {
		ui.debugVisible = !ui.debugVisible
		if ui.debugVisible {
			ui.debugCard.Show()
		} else {
			ui.debugCard.Hide()
		}
	}

	// ── Config section ────────────────────────────────────────────────────────

	ui.configSection = container.NewVBox(
		deviceCard,
		pinCard,
		encoderCard,
		timingCard,
		actionsRow,
		ui.debugCard,
	)

	// ── Wire Rescan / Retry ───────────────────────────────────────────────────

	ui.rescanBtn.OnTapped = func() {
		ui.setState(stateScanning)
		ui.configSection.Hide()
		go ui.runScan()
	}
	ui.retryBtn.OnTapped = func() {
		ui.setState(stateScanning)
		go ui.runScan()
	}

	// ── Full layout ───────────────────────────────────────────────────────────

	content := container.NewVBox(
		connCard,
		ui.recoveryCard,
		ui.configSection,
	)

	ui.scroll = container.NewVScroll(content)
	ui.win.SetContent(ui.scroll)

	// Initial state
	ui.recoveryCard.Hide()
	ui.configSection.Hide()
	ui.debugCard.Hide()
	ui.setState(stateScanning)
}

// ── Pin grid management ───────────────────────────────────────────────────────

func (ui *appUI) rebuildPinGrid(n int, pins []int, modes []int) {
	// Preserve current values
	saved := make([]string, len(ui.pinCells))
	savedModes := make([]string, len(ui.pinCells))
	for i, cell := range ui.pinCells {
		saved[i] = cell.pinEntry.Text
		savedModes[i] = cell.modeSelect.Selected
	}

	ui.pinCells = make([]pinCell, n)
	ui.pinGridWrap.Objects = nil

	modeOpts := []string{"PU", "PD", "IN"}

	for i := 0; i < n; i++ {
		pinVal := "0"
		modeVal := "PU"

		if pins != nil && i < len(pins) {
			pinVal = strconv.Itoa(pins[i])
		} else if i < len(saved) && saved[i] != "" {
			pinVal = saved[i]
		} else if i < len(defaultBtnPins) {
			pinVal = strconv.Itoa(defaultBtnPins[i])
		}

		if modes != nil && i < len(modes) {
			switch modes[i] {
			case 0:
				modeVal = "PU"
			case 1:
				modeVal = "PD"
			case 2:
				modeVal = "IN"
			}
		} else if i < len(savedModes) && savedModes[i] != "" {
			modeVal = savedModes[i]
		}

		pinEntry := widget.NewEntry()
		pinEntry.SetText(pinVal)
		pinEntry.SetPlaceHolder("0")

		modeSelect := widget.NewSelect(modeOpts, nil)
		modeSelect.SetSelected(modeVal)

		label := widget.NewLabel(fmt.Sprintf("B%d", i+1))
		cell := container.NewVBox(label, pinEntry, modeSelect)
		ui.pinCells[i] = pinCell{pinEntry: pinEntry, modeSelect: modeSelect}
		ui.pinGridWrap.Add(cell)
	}
	ui.pinGridWrap.Refresh()
}

func (ui *appUI) rebuildZoneCountOptions() {
	steps, err := strconv.Atoi(strings.TrimSpace(ui.zoneStepsEntry.Text))
	if err != nil || steps < 2 {
		steps = 20
	}
	var opts []string
	for i := 2; i <= steps; i++ {
		if steps%i == 0 {
			opts = append(opts, strconv.Itoa(i))
		}
	}
	if len(opts) == 0 {
		opts = []string{"2"}
	}
	prev := ui.zoneCountSelect.Selected
	ui.zoneCountSelect.Options = opts
	found := false
	for _, o := range opts {
		if o == prev {
			found = true
			break
		}
	}
	if found {
		ui.zoneCountSelect.SetSelected(prev)
	} else {
		ui.zoneCountSelect.SetSelected(opts[0])
	}
}

func (ui *appUI) rebuildResetCheckboxes(numBtns int) {
	ui.resetCheckboxes = make([]*widget.Check, numBtns)
	ui.resetRow.Objects = nil
	for i := 0; i < numBtns; i++ {
		idx := i
		cb := widget.NewCheck(strconv.Itoa(idx+1), nil)
		ui.resetCheckboxes[idx] = cb
		ui.resetRow.Add(cb)
	}
	ui.resetRow.Refresh()
}

// ── State transitions ─────────────────────────────────────────────────────────

func (ui *appUI) setState(s connState) {
	ui.scanRow.Hide()
	ui.foundRow.Hide()
	ui.notFoundRow.Hide()
	ui.reconnectRow.Hide()

	switch s {
	case stateScanning:
		ui.scanProgress.Start()
		ui.scanRow.Show()
	case stateFound:
		ui.scanProgress.Stop()
		ui.foundRow.Show()
	case stateNotFound:
		ui.scanProgress.Stop()
		ui.notFoundRow.Show()
	case stateReconnecting:
		ui.reconnectProgress.Start()
		ui.reconnectRow.Show()
	}
	ui.connStateBox.Refresh()
}

func (ui *appUI) setFound() {
	ui.setState(stateFound)
	ui.configSection.Show()
}

func (ui *appUI) setReconnecting() {
	ui.setState(stateReconnecting)
	ui.configSection.Hide()
}

func (ui *appUI) setNotFound(errMsg string) {
	if errMsg != "" {
		ui.notFoundLabel.SetText("Device not found: " + errMsg)
	} else {
		ui.notFoundLabel.SetText("Device not found. Make sure the ESP32 is powered on and Bluetooth is enabled.")
	}
	ui.setState(stateNotFound)
}

// ── Scan logic ────────────────────────────────────────────────────────────────

func (ui *appUI) runScan() {
	c, err := scanAndConnect(ui.onBtnEvent)
	if err != nil {
		ui.setNotFound(err.Error())
		return
	}
	setConn(c)

	var cfg DeviceConfig
	select {
	case cfg = <-c.cfgChan:
		fmt.Println("[ui] config received from device")
	case <-time.After(3 * time.Second):
		fmt.Println("[ui] config timeout — trying local cache")
		if cached, ok := loadCachedConfig(); ok {
			cfg = cached
		} else {
			cfg = defaultConfig()
		}
	}

	ui.populateForm(cfg)
	ui.setFound()
	go ui.watchDisconnect()
}

func (ui *appUI) watchDisconnect() {
	wasConnected := true
	for {
		time.Sleep(2 * time.Second)
		if getConn() == nil && wasConnected {
			wasConnected = false
			ui.setReconnecting()
			go ui.reconnectLoop()
			return
		}
		if getConn() != nil {
			wasConnected = true
		}
	}
}

func (ui *appUI) reconnectLoop() {
	for {
		time.Sleep(5 * time.Second)
		c, err := scanAndConnect(ui.onBtnEvent)
		if err != nil {
			fmt.Printf("[reconnect] attempt failed: %v\n", err)
			continue
		}
		setConn(c)

		var cfg DeviceConfig
		select {
		case cfg = <-c.cfgChan:
		case <-time.After(3 * time.Second):
			if cached, ok := loadCachedConfig(); ok {
				cfg = cached
			} else {
				cfg = defaultConfig()
			}
		}

		ui.populateForm(cfg)
		ui.setFound()
		go ui.watchDisconnect()
		return
	}
}

// ── Button event handler ──────────────────────────────────────────────────────

func (ui *appUI) onBtnEvent(btn int, press bool) {
	ui.pressedMu.Lock()
	if press {
		ui.pressedBtns[btn] = true
	} else {
		delete(ui.pressedBtns, btn)
	}
	pressed := make([]int, 0, len(ui.pressedBtns))
	for b := range ui.pressedBtns {
		pressed = append(pressed, b)
	}
	ui.pressedMu.Unlock()

	sort.Ints(pressed)

	// Update pressed label
	if len(pressed) == 0 {
		ui.pressedLabel.SetText("— none —")
	} else {
		parts := make([]string, len(pressed))
		for i, b := range pressed {
			parts[i] = strconv.Itoa(b)
		}
		ui.pressedLabel.SetText(strings.Join(parts, ", "))
	}

	// Append to log
	if ui.debugVisible {
		t := time.Now().Format("15:04:05")
		action := "released"
		if press {
			action = "pressed"
		}
		line := fmt.Sprintf("%s  btn %d %s", t, btn, action)
		logLabel := widget.NewLabel(line)
		logLabel.TextStyle = fyne.TextStyle{Monospace: true}
		ui.logBox.Add(logLabel)
		// Trim to 50 entries
		for len(ui.logBox.Objects) > 50 {
			ui.logBox.Objects = ui.logBox.Objects[1:]
		}
		ui.logBox.Refresh()
		ui.logScroll.ScrollToBottom()
	}
}

// ── Form population ───────────────────────────────────────────────────────────

func (ui *appUI) populateForm(cfg DeviceConfig) {
	// Recovery banner
	if cfg.RecoveryOccurred {
		ui.recoveryCard.Show()
	} else {
		ui.recoveryCard.Hide()
	}

	// Device
	ui.bleNameEntry.SetText(cfg.BLEDeviceName)
	ui.otaPassEntry.SetText(cfg.OTAPassword)

	// Pin layout
	ui.useDirectCheck.SetChecked(cfg.UseDirectButtons)
	if cfg.UseDirectButtons {
		ui.directOptions.Show()
	} else {
		ui.directOptions.Hide()
	}

	n := cfg.NumButtons
	if n < 1 {
		n = 14
	}
	ui.numButtonsEntry.SetText(strconv.Itoa(n))
	ui.rebuildPinGrid(n, cfg.ButtonPins, cfg.ButtonInputModes)
	ui.rebuildResetCheckboxes(n)

	// Encoders
	ui.useEncoderCheck.SetChecked(cfg.UseEncoders)
	if cfg.UseEncoders {
		ui.encoderOptions.Show()
	} else {
		ui.encoderOptions.Hide()
	}

	if cfg.EncoderZonesMode {
		ui.encoderModeRadio.SetSelected("Zones")
		ui.zoneFields.Show()
	} else {
		ui.encoderModeRadio.SetSelected("Normal")
		ui.zoneFields.Hide()
	}

	if cfg.EncoderZoneMaster == 1 {
		ui.zoneMasterRadio.SetSelected("Enc 2")
	} else {
		ui.zoneMasterRadio.SetSelected("Enc 1")
	}

	if cfg.EncoderZoneSteps > 0 {
		ui.zoneStepsEntry.SetText(strconv.Itoa(cfg.EncoderZoneSteps))
	}
	ui.rebuildZoneCountOptions()
	if cfg.EncoderZoneCount >= 2 {
		ui.zoneCountSelect.SetSelected(strconv.Itoa(cfg.EncoderZoneCount))
	}

	// Reset checkboxes
	resetSet := make(map[int]bool)
	for _, b := range cfg.EncoderZoneResetButtons {
		resetSet[b] = true
	}
	for i, cb := range ui.resetCheckboxes {
		cb.SetChecked(resetSet[i+1])
	}

	// Encoder pins
	if len(cfg.EncoderPins) >= 2 {
		if len(cfg.EncoderPins[0]) >= 2 {
			ui.encPin0CLK.SetText(strconv.Itoa(cfg.EncoderPins[0][0]))
			ui.encPin0DT.SetText(strconv.Itoa(cfg.EncoderPins[0][1]))
		}
		if len(cfg.EncoderPins[1]) >= 2 {
			ui.encPin1CLK.SetText(strconv.Itoa(cfg.EncoderPins[1][0]))
			ui.encPin1DT.SetText(strconv.Itoa(cfg.EncoderPins[1][1]))
		}
	}

	// Timing
	ui.debounceEntry.SetText(strconv.Itoa(cfg.DebounceDelayMs))
	ui.btnTaskDelayEntry.SetText(strconv.Itoa(cfg.ButtonTaskDelayMs))
	ui.encDebounceEntry.SetText(strconv.Itoa(cfg.EncoderDebounceUs))
	ui.encPressDurEntry.SetText(strconv.Itoa(cfg.EncoderPressDurationMs))
	ui.encTaskDelayEntry.SetText(strconv.Itoa(cfg.EncoderTaskDelayMs))
}

// ── Form collection ───────────────────────────────────────────────────────────

func intEntry(e *widget.Entry, def int) int {
	v, err := strconv.Atoi(strings.TrimSpace(e.Text))
	if err != nil {
		return def
	}
	return v
}

func (ui *appUI) collectForm() DeviceConfig {
	cfg := DeviceConfig{}

	cfg.BLEDeviceName = ui.bleNameEntry.Text
	cfg.OTAPassword = ui.otaPassEntry.Text

	cfg.UseDirectButtons = ui.useDirectCheck.Checked
	n := intEntry(ui.numButtonsEntry, 14)
	if n < 1 {
		n = 1
	}
	if n > 32 {
		n = 32
	}
	cfg.NumButtons = n

	cfg.ButtonPins = make([]int, n)
	cfg.ButtonInputModes = make([]int, n)
	for i := 0; i < n && i < len(ui.pinCells); i++ {
		cfg.ButtonPins[i] = intEntry(ui.pinCells[i].pinEntry, 0)
		switch ui.pinCells[i].modeSelect.Selected {
		case "PU":
			cfg.ButtonInputModes[i] = 0
		case "PD":
			cfg.ButtonInputModes[i] = 1
		case "IN":
			cfg.ButtonInputModes[i] = 2
		}
	}

	cfg.UseEncoders = ui.useEncoderCheck.Checked
	cfg.EncoderZonesMode = ui.encoderModeRadio.Selected == "Zones"
	if ui.zoneMasterRadio.Selected == "Enc 2" {
		cfg.EncoderZoneMaster = 1
	} else {
		cfg.EncoderZoneMaster = 0
	}
	cfg.EncoderZoneSteps = intEntry(ui.zoneStepsEntry, 20)
	cfg.EncoderZoneCount = intEntry(&widget.Entry{Text: ui.zoneCountSelect.Selected}, 2)

	cfg.EncoderZoneResetButtons = []int{}
	for i, cb := range ui.resetCheckboxes {
		if cb.Checked {
			cfg.EncoderZoneResetButtons = append(cfg.EncoderZoneResetButtons, i+1)
		}
	}

	cfg.EncoderPins = [][]int{
		{intEntry(ui.encPin0CLK, 26), intEntry(ui.encPin0DT, 27)},
		{intEntry(ui.encPin1CLK, 4), intEntry(ui.encPin1DT, 5)},
	}

	cfg.DebounceDelayMs = intEntry(ui.debounceEntry, 5)
	cfg.ButtonTaskDelayMs = intEntry(ui.btnTaskDelayEntry, 5)
	cfg.EncoderDebounceUs = intEntry(ui.encDebounceEntry, 1000)
	cfg.EncoderPressDurationMs = intEntry(ui.encPressDurEntry, 100)
	cfg.EncoderTaskDelayMs = intEntry(ui.encTaskDelayEntry, 5)

	cfg.UseMatrix = false

	return cfg
}

// ── Actions ───────────────────────────────────────────────────────────────────

func (ui *appUI) doSave() {
	c := getConn()
	if c == nil {
		dialog.ShowError(fmt.Errorf("not connected"), ui.win)
		return
	}
	cfg := ui.collectForm()
	data, err := json.Marshal(cfg)
	if err != nil {
		dialog.ShowError(err, ui.win)
		return
	}

	go func() {
		if err := writeChunked(c.writeChar, data); err != nil {
			dialog.ShowError(fmt.Errorf("write failed: %w", err), ui.win)
			return
		}
		c.rebootChar.WriteWithoutResponse([]byte{1})
		setConn(nil)

		if err := os.WriteFile(configFilePath(), data, 0644); err != nil {
			fmt.Printf("[save] warning: could not write local cache: %v\n", err)
		}

		ui.setReconnecting()
		go ui.reconnectLoop()
	}()
}

func (ui *appUI) doRefresh() {
	c := getConn()
	if c == nil {
		dialog.ShowInformation("Not connected", "Device is not connected.", ui.win)
		return
	}

	go func() {
		// Check if we have a cached config on the connection
		c.cfgMu.Lock()
		cached := c.cfgJSON
		c.cfgMu.Unlock()

		if cached != "" {
			var cfg DeviceConfig
			if err := json.Unmarshal([]byte(cached), &cfg); err == nil {
				ui.populateForm(cfg)
				return
			}
		}

		// Wait for a new notification
		select {
		case cfg := <-c.cfgChan:
			ui.populateForm(cfg)
		case <-time.After(3 * time.Second):
			// Fall back to local cache
			if cfg, ok := loadCachedConfig(); ok {
				ui.populateForm(cfg)
			} else {
				dialog.ShowInformation("Refresh", "No config available from device.", ui.win)
			}
		}
	}()
}

func (ui *appUI) doOTA() {
	c := getConn()
	if c == nil {
		dialog.ShowError(fmt.Errorf("not connected"), ui.win)
		return
	}
	c.otaChar.WriteWithoutResponse([]byte{1})
	setConn(nil)
	ui.configSection.Hide()
	ui.setState(stateNotFound)
	dialog.ShowInformation("OTA Mode Active",
		"1. Connect your PC to the ButtonBox-OTA WiFi network.\n"+
			"2. In Arduino IDE: Tools → Port → buttonbox.\n"+
			"3. Upload your sketch normally.",
		ui.win)
}

func (ui *appUI) doClearCache() {
	if err := os.Remove(configFilePath()); err != nil && !os.IsNotExist(err) {
		dialog.ShowError(err, ui.win)
		return
	}
	dialog.ShowInformation("Cache Cleared", "Local config cache removed. Next connect will show device defaults.", ui.win)
}

// ── main ─────────────────────────────────────────────────────────────────────

func main() {
	if err := adapter.Enable(); err != nil {
		panic("Bluetooth not available: " + err.Error())
	}

	a := app.New()
	w := a.NewWindow("ButtonBox Config")
	w.Resize(fyne.NewSize(560, 860))

	ui := newUI(w)

	// Auto-scan on startup
	go func() {
		time.Sleep(200 * time.Millisecond) // let the window render first
		ui.runScan()
	}()

	w.ShowAndRun()
}
