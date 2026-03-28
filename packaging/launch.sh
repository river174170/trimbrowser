#!/bin/sh
# TrimBrowser launcher for TrimUI Smart Pro (Allwinner A133P / tg5040)

SELF="$(readlink -f "$0")"
APP_DIR="$(dirname "$SELF")"

# ---- SDL2 backend for TrimUI Smart Pro ----
# The device uses a Mali GPU with direct framebuffer / KMS
export SDL_VIDEODRIVER=mali
export SDL_FBDEV=/dev/fb0
export SDL_AUDIODRIVER=alsa
export MALI_NOCLEAR=1

# If mali driver isn't available, fall back to framebuffer
if [ ! -f /dev/mali0 ]; then
    export SDL_VIDEODRIVER=fbdev
fi

# ---- Font config ----
export FONTCONFIG_PATH="$APP_DIR/fonts"

# ---- Optional: increase process priority ----
# renice -n -5 $$ 2>/dev/null

cd "$APP_DIR"
exec ./trimbrowser "${1:-https://www.baidu.com}"
