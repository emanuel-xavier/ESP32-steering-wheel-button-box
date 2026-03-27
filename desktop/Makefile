APP     := buttonbox-config
OUTDIR  := build

# ── Linux ─────────────────────────────────────────────────────────────────────
# Requires: libgl, xorg headers (for Fyne)
#   Arch:   sudo pacman -S libgl mesa xorg-server-devel
#   Ubuntu: sudo apt install libgl1-mesa-dev xorg-dev

linux: $(OUTDIR)
	CGO_ENABLED=1 go build -o $(OUTDIR)/$(APP)-linux-amd64 .

# ── Windows (cross-compiled from Linux via mingw-w64) ─────────────────────────
# Requires: mingw-w64
#   Arch:   sudo pacman -S mingw-w64-gcc
#   Ubuntu: sudo apt install gcc-mingw-w64 g++-mingw-w64

windows: $(OUTDIR)
	CGO_ENABLED=1 GOOS=windows GOARCH=amd64 \
		CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ \
		go build -ldflags="-s -w -H windowsgui" \
		-o $(OUTDIR)/$(APP)-windows-amd64.exe .

# ── Both ──────────────────────────────────────────────────────────────────────

all: linux
	@if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then \
		$(MAKE) windows; \
	else \
		echo "Skipping Windows build: mingw-w64 not found (sudo pacman -S mingw-w64-gcc)"; \
	fi

# ── Helpers ───────────────────────────────────────────────────────────────────

$(OUTDIR):
	mkdir -p $(OUTDIR)

clean:
	rm -rf $(OUTDIR)

run:
	go run .

.PHONY: linux windows all clean run
