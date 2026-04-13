#!/bin/sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)"
BINARY="$PROJECT_DIR/build/env_control"
GUI_START_DELAY="${GUI_START_DELAY:-5}"
TIME_SYNC_WAIT_SEC="${TIME_SYNC_WAIT_SEC:-20}"
RESTART_DELAY_SEC="${RESTART_DELAY_SEC:-8}"
PAUSE_FLAG="${PAUSE_FLAG:-/tmp/env_control.pause}"

cd "$PROJECT_DIR"

if [ ! -x "$BINARY" ]; then
    echo "[bootloader] binary not found, building..."
    make
fi

if [ "$GUI_START_DELAY" -gt 0 ] 2>/dev/null; then
    echo "[bootloader] delaying GUI startup for ${GUI_START_DELAY}s..."
    sleep "$GUI_START_DELAY"
fi

if command -v timedatectl >/dev/null 2>&1 && [ "$TIME_SYNC_WAIT_SEC" -gt 0 ] 2>/dev/null; then
    i=0
    while [ "$i" -lt "$TIME_SYNC_WAIT_SEC" ]; do
        if [ "$(timedatectl show -p NTPSynchronized --value 2>/dev/null || echo no)" = "yes" ]; then
            echo "[bootloader] system clock synchronized."
            break
        fi
        i=$((i + 1))
        sleep 1
    done
fi

echo "[bootloader] starting $BINARY"

# Keep the app alive. If it exits unexpectedly, restart after a short delay.
while true; do
    if [ -f "$PAUSE_FLAG" ]; then
        echo "[bootloader] pause flag exists ($PAUSE_FLAG), waiting..."
        sleep 1
        continue
    fi

    # Enforce single instance.
    if command -v pgrep >/dev/null 2>&1 && pgrep -x env_control >/dev/null 2>&1; then
        echo "[bootloader] env_control already running, waiting..."
        sleep 1
        continue
    fi

    "$BINARY"
    exit_code=$?
    echo "[bootloader] app exited with code $exit_code, restarting in ${RESTART_DELAY_SEC}s..."
    sleep "$RESTART_DELAY_SEC"
done
