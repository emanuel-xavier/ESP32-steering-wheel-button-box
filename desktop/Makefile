APP     := buttonbox-config
OUTDIR  := build

# ── Linux ─────────────────────────────────────────────────────────────────────
# Requires: libgl, xorg headers (for Fyne)
#   Arch:   sudo pacman -S libgl mesa xorg-server-devel
#   Ubuntu: sudo apt install libgl1-mesa-dev xorg-dev

linux: $(OUTDIR)
	CGO_ENABLED=1 go build -o $(OUTDIR)/$(APP)-linux-amd64 .

# ── Windows ───────────────────────────────────────────────────────────────────
# Cross-compile using the fyne CLI tool or build natively on Windows.
#
# Install fyne CLI:  go install fyne.io/fyne/v2/cmd/fyne@latest
# Cross-compile:     fyne-cross windows -arch amd64
#
# Or natively on Windows:
#   CGO_ENABLED=1 go build -ldflags="-H windowsgui" -o build\buttonbox-config-windows-amd64.exe .

windows: $(OUTDIR)
	@echo "Windows builds are best done natively or via fyne-cross."
	@echo "  Install: go install github.com/fyne-io/fyne-cross@latest"
	@echo "  Run:     fyne-cross windows -arch amd64"

# ── Both ──────────────────────────────────────────────────────────────────────

all: linux

# ── Helpers ───────────────────────────────────────────────────────────────────

$(OUTDIR):
	mkdir -p $(OUTDIR)

clean:
	rm -rf $(OUTDIR)

run:
	go run .

.PHONY: linux windows all clean run
