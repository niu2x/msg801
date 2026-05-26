#!/usr/bin/env bash
set -euo pipefail

PORT="${1:?usage: $0 <socks5_port>}"
URL_SMALL="${2:-https://www.google.com}"
URL_LARGE="${3:-https://ash-speed.hetzner.com/100MB.bin}"

echo "=== port $PORT: google ==="
curl -x "socks5h://127.0.0.1:$PORT" -o /dev/null -w "time: %{time_total}s  speed: %{speed_download} B/s\n" "$URL_SMALL"

echo "=== port $PORT: 100MB ==="
curl -x "socks5h://127.0.0.1:$PORT" -o /dev/null -w "time: %{time_total}s  speed: %{speed_download} B/s\n" "$URL_LARGE"
