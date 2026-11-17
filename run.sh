#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

SOCK=/tmp/tinyos-net.sock
rm -f "$SOCK"

# Helper-real internet fetcher for TINY BROWSER (COM1 <-> UNIX socket).
python3 nethelper.py &
HELPER=$!

cleanup() {
    kill "$HELPER" 2>/dev/null || true
    wait "$HELPER" 2>/dev/null || true
    rm -f "$SOCK"
}
trap cleanup EXIT INT TERM

# Wait until the helper has bound its socket.
for _ in $(seq 1 100); do
    [ -S "$SOCK" ] && break
    sleep 0.05
done
if [ ! -S "$SOCK" ]; then
    echo "nethelper не запустился" >&2
    exit 1
fi

qemu-system-i386 \
    -drive format=raw,file=os.img \
    -m 64 \
    -k en-us \
    -chardev socket,id=net,path="$SOCK" \
    -serial chardev:net \
    "$@"
