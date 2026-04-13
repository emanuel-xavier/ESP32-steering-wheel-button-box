APP     := buttonbox-config
OUTDIR  := build

# ── Linux ─────────────────────────────────────────────────────────────────────
# Requires: webkit2gtk-4.1
#   Arch:   sudo pacman -S webkit2gtk-4.1
#   Ubuntu: sudo apt install libwebkit2gtk-4.1-dev libgtk-3-dev

linux: $(OUTDIR)
	CGO_ENABLED=1 GOOS=linux GOARCH=amd64 \
		go build -tags production -o $(OUTDIR)/$(APP)-linux-amd64 .

# ── Windows (cross-compiled from Linux) ───────────────────────────────────────
# Requires: mingw-w64
#   Arch:   sudo pacman -S mingw-w64-gcc
#   Ubuntu: sudo apt install gcc-mingw-w64
#
# The resulting .exe requires WebView2, built into Windows 10/11.

windows: $(OUTDIR)
	CGO_ENABLED=1 GOOS=windows GOARCH=amd64 CC=x86_64-w64-mingw32-gcc \
		go build -tags production -ldflags="-H windowsgui" -o $(OUTDIR)/$(APP)-windows-amd64.exe .

# ── Both ──────────────────────────────────────────────────────────────────────

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
	go run -tags production .

.PHONY: linux windows all clean run
