APP     := buttonbox-config
OUTDIR  := build

# ── Linux ─────────────────────────────────────────────────────────────────────
# Requires: webkit2gtk
#   Arch:   sudo pacman -S webkit2gtk
#   Ubuntu: sudo apt install libwebkit2gtk-4.1-dev libgtk-3-dev

linux: $(OUTDIR)
	CGO_ENABLED=1 GOOS=linux GOARCH=amd64 \
		go build -o $(OUTDIR)/$(APP)-linux-amd64 .

# ── Windows (cross-compiled from Linux) ───────────────────────────────────────
# Requires: mingw-w64
#   Arch:   sudo pacman -S mingw-w64-gcc
#   Ubuntu: sudo apt install gcc-mingw-w64
#
# The resulting .exe requires WebView2, which is built into Windows 10/11.
# No extra DLLs needed on the target machine.

windows: $(OUTDIR)
	CGO_ENABLED=1 GOOS=windows GOARCH=amd64 CC=x86_64-w64-mingw32-gcc \
		go build -ldflags="-H windowsgui" -o $(OUTDIR)/$(APP)-windows-amd64.exe .

# ── Both ──────────────────────────────────────────────────────────────────────
# Windows build is skipped automatically when mingw-w64 is not installed.

all: linux
	@if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then \
		$(MAKE) windows; \
	else \
		echo "Skipping Windows build: x86_64-w64-mingw32-gcc not found."; \
		echo "  Arch:   sudo pacman -S mingw-w64-gcc"; \
		echo "  Ubuntu: sudo apt install gcc-mingw-w64"; \
	fi

# ── Helpers ───────────────────────────────────────────────────────────────────

$(OUTDIR):
	mkdir -p $(OUTDIR)

clean:
	rm -rf $(OUTDIR)

run:
	go run .

.PHONY: linux windows all clean run
