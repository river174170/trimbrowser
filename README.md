# TrimBrowser

A web browser for **TrimUI Smart Pro** built with SDL2 + litehtml + libcurl.

Designed to run completely standalone — copy to SD card, done.

---

## Features

- Full HTML/CSS rendering via [litehtml](https://github.com/litehtml/litehtml)
- HTTPS support (libcurl + OpenSSL 1.1)
- Gamepad + D-Pad + touch support
- Navigation history (back/forward)
- Address bar (Y button)
- Page scrolling & zoom
- **Bookmarks** — save favorite pages, quick access
- **Media playback** — click any video/audio link to play directly
- Minimal dependencies — only uses libs already on TrimUI firmware

---

## Building

### Option A — GitHub Actions (Recommended, no Linux machine needed)

1. Fork this repo on GitHub
2. Go to **Actions** → **Build TrimBrowser for TrimUI Smart Pro**
3. Click **Run workflow**
4. After ~5 minutes, download `Browser-TrimUI-SmartPro.zip` from Artifacts

### Option B — Local cross-compile (Linux / Docker)

```bash
# Install cross-compiler
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu cmake ninja-build

# Download TrimUI SDK sysroot
mkdir -p /opt/trimui-sdk/sysroot
wget https://github.com/trimui/toolchain_sdk_smartpro/releases/download/20231018/SDK_usr_tg5040_a133p.tgz
tar xzf SDK_usr_tg5040_a133p.tgz -C /opt/trimui-sdk/sysroot --strip-components=1

# Clone with submodules
git clone --recursive https://github.com/YOUR_USERNAME/TrimBrowser
cd TrimBrowser

# Build
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-trimui.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

---

## Installation

1. Download `Browser-TrimUI-SmartPro.zip` from [Releases](../../releases)
2. Extract — you'll get a `Browser/` folder
3. Copy `Browser/` to your SD card's `Apps/` directory:
   ```
   SD card/
   └── Apps/
       └── Browser/
           ├── trimbrowser    ← main binary
           ├── launch.sh      ← launcher script
           ├── config.json    ← app metadata
           ├── icon.png       ← app icon
           └── fonts/
               └── NotoSans-Regular.ttf
   ```
4. Reboot your TrimUI Smart Pro or refresh the app list

---

## Controls

| Button | Action |
|--------|--------|
| **A** | Click focused link / Add bookmark (in bookmark list) |
| **B** | Go back / Close bookmark list / Quit (if no history) |
| **X** | Reload |
| **Y** | Show address bar |
| **SELECT** | Toggle bookmark list |
| **START** | Quit |
| **D-Pad** | Scroll (or navigate bookmark list) |
| **L / R** | Page up / Page down |
| **Right stick** | Smooth scroll |
| **Enter** | Open selected bookmark |

### Keyboard Shortcuts (for testing on PC)

| Key | Action |
|-----|--------|
| **Tab** | Toggle bookmark list |
| **Esc** | Close bookmark list / Cancel |
| **↑/↓** | Navigate bookmark list |
| **Enter** | Open selected bookmark |

---

## Architecture

```
main.cpp              — SDL2 window/event loop
browser.h             — Browser API (C-style interface)
browser_core.cpp      — Navigation, HTTP fetch (libcurl), litehtml doc management
litehtml_container.h  — litehtml document_container interface
litehtml_container.cpp — SDL2 + FreeType rendering backend for litehtml
```

### Dependencies

| Library | Version | Source |
|---------|---------|--------|
| SDL2 | 2.26.1 | TrimUI system |
| FreeType | 2.x | TrimUI system |
| libssl/crypto | 1.1.1 | TrimUI system |
| libcurl | 7.88.1 | Bundled (static) |
| litehtml | latest | Vendored submodule |

---

## Limitations

- Images are shown as placeholders (download is asynchronous, deferred)
- No JavaScript support (litehtml is HTML/CSS only)
- No tabs
- Address bar uses hardware keyboard input — gamepad text input needs on-screen keyboard (TODO)

---

## License

MIT
# trimbrowser
