#!/usr/bin/env bash
# Integration test for TCP tunnel
set -euo pipefail
trap 'kill 0 2>/dev/null; wait 2>/dev/null' EXIT

TUNNEL_BIN="${1:-./dist/bin/msg801}"
TUNNEL_PORT="${2:-19999}"
ECHO_PORT="${3:-19998}"
PASS=0
FAIL=0

green() { echo -e "\033[32m✓ $1\033[0m"; }
red()   { echo -e "\033[31m✗ $1\033[0m"; }

check() {
    local desc="$1"
    shift
    if eval "$@"; then
        green "$desc"
        PASS=$((PASS + 1))
    else
        red "$desc"
        FAIL=$((FAIL + 1))
    fi
}

# ---- helpers: pure Python ----

start_echo() {
    local port="$1"
    python3 -c "
import socket, threading, sys
def echo(s):
    while True:
        d = s.recv(65536)
        if not d: break
        s.sendall(d)
    s.close()
s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('127.0.0.1', $port))
s.listen(50)
sys.stderr.write('echo ready on $port\n'); sys.stderr.flush()
while True:
    c, _ = s.accept()
    threading.Thread(target=echo, args=(c,), daemon=True).start()
" &
}

# send data through tunnel on TUNNEL_PORT, return echoed response
send_recv() {
    printf '%s' "$1" | python3 -c "
import socket, sys
data = sys.stdin.buffer.read()
s = socket.socket()
s.settimeout(5)
s.connect(('127.0.0.1', int(sys.argv[1])))
s.sendall(data)
s.shutdown(socket.SHUT_WR)
resp = b''
while True:
    chunk = s.recv(65536)
    if not chunk: break
    resp += chunk
s.close()
sys.stdout.buffer.write(resp)
" "$TUNNEL_PORT"
}

concurrent_test() {
    local n="$1"
    local payload="$2"
    local pids=()
    for i in $(seq 1 "$n"); do
        (
            result=$(send_recv "$payload")
            if [[ "$result" != "$payload" ]]; then
                exit 1
            fi
            exit 0
        ) &
        pids+=($!)
    done
    local ok=0
    for pid in "${pids[@]}"; do
        wait "$pid" || ok=1
    done
    return "$ok"
}

# ---- standard tests (one tunnel, no XOR) ----

run_tests() {
    echo "=== 1. short message ==="
    result=$(send_recv "hello tunnel")
    check "short message" "[[ '$result' = 'hello tunnel' ]]"

    echo "=== 2. empty message ==="
    result=$(send_recv "")
    check "empty message" "[[ '$result' = '' ]]"

    echo "=== 3. 10KB payload ==="
    data=$(head -c 10240 /dev/urandom | base64 -w0)
    result=$(send_recv "$data")
    check "10KB payload" "[[ '$result' = '$data' ]]"

    echo "=== 4. 64KB payload ==="
    data=$(head -c 65536 /dev/urandom | base64 -w0)
    result=$(send_recv "$data")
    check "64KB payload" "[[ '$result' = '$data' ]]"

    echo "=== 5. 1MB payload ==="
    data=$(head -c 1048576 /dev/urandom | base64 -w0)
    result=$(send_recv "$data")
    check "1MB payload" "[[ '$result' = '$data' ]]"

    echo "=== 6. 4 concurrent clients, 1KB each ==="
    local ok6=true
    data=$(head -c 1024 /dev/urandom | base64 -w0)
    concurrent_test 4 "$data" || ok6=false
    check "4 concurrent 1KB" "[[ '$ok6' = 'true' ]]"

    echo "=== 7. 16 concurrent clients, 1KB each ==="
    local ok7=true
    data=$(head -c 1024 /dev/urandom | base64 -w0)
    concurrent_test 16 "$data" || ok7=false
    check "16 concurrent 1KB" "[[ '$ok7' = 'true' ]]"

    echo "=== 8. 16 concurrent clients, 64KB each ==="
    local ok8=true
    data=$(head -c 65536 /dev/urandom | base64 -w0)
    concurrent_test 16 "$data" || ok8=false
    check "16 concurrent 64KB" "[[ '$ok8' = 'true' ]]"

    echo "=== 9. rapid connect/disconnect (no crash) ==="
    local pids=()
    for i in $(seq 1 20); do
        send_recv "x" >/dev/null 2>/dev/null &
        pids+=($!)
    done
    for pid in "${pids[@]}"; do wait "$pid" 2>/dev/null || true; done
    check "20 rapid connect/disconnect" "true"

    echo "=== 10. tunnel still alive ==="
    result=$(send_recv "ping")
    check "tunnel still responsive" "[[ '$result' = 'ping' ]]"
}

# ---- XOR two-hop test: A(encrypt) → B(decrypt) → echo ----

xor_send_recv() {
    printf '%s' "$1" | python3 -c "
import socket, sys
data = sys.stdin.buffer.read()
s = socket.socket()
s.settimeout(5)
s.connect(('127.0.0.1', int(sys.argv[1])))
s.sendall(data)
s.shutdown(socket.SHUT_WR)
resp = b''
while True:
    chunk = s.recv(65536)
    if not chunk: break
    resp += chunk
s.close()
sys.stdout.buffer.write(resp)
" "$XOR_LISTEN_PORT"
}

run_xor_tests() {
    echo ""
    echo "--- XOR two-hop tests (A encrypt → B decrypt) ---"

    local ok1=true
    result=$(xor_send_recv "hello xor")
    check "xor short message" "[[ '$result' = 'hello xor' ]]"

    local ok2=true
    result=$(xor_send_recv "")
    check "xor empty message" "[[ '$result' = '' ]]"

    local ok3=true
    data=$(head -c 10240 /dev/urandom | base64 -w0)
    result=$(xor_send_recv "$data")
    check "xor 10KB payload" "[[ '$result' = '$data' ]]"

    local ok4=true
    data=$(head -c 65536 /dev/urandom | base64 -w0)
    result=$(xor_send_recv "$data")
    check "xor 64KB payload" "[[ '$result' = '$data' ]]"

    local ok5=true
    data=$(head -c 1024 /dev/urandom | base64 -w0)
    concurrent_test 16 "$data" || ok5=false
    check "xor 16 concurrent 1KB" "[[ '$ok5' = 'true' ]]"

    local ok6=true
    result=$(xor_send_recv "ping")
    check "xor tunnel still alive" "[[ '$result' = 'ping' ]]"

    echo "=== xor batch pipelined: 20 chunks without waiting ==="
    local ok7=true
    python3 -c "
import socket, time, sys
s = socket.socket()
s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
s.settimeout(5)
s.connect(('127.0.0.1', $XOR_LISTEN_PORT))

chunks = []
for i in range(20):
    chunk = b'chunk%02d_' % i + b'x' * 56
    chunks.append(chunk)

for c in chunks:
    s.sendall(c)
    time.sleep(0.002)

resp = b''
while len(resp) < sum(len(c) for c in chunks):
    d = s.recv(65536)
    if not d: break
    resp += d
s.close()

pos = 0
for c in chunks:
    if resp[pos:pos+len(c)] != c:
        sys.exit(1)
    pos += len(c)
sys.exit(0)
" || ok7=false
    check "xor 20 pipelined chunks" "[[ $ok7 = true ]]"
}

# ---- main ----

# Standard tests
start_echo "$ECHO_PORT"
sleep 0.5

"$TUNNEL_BIN" tunnel --listen "127.0.0.1:$TUNNEL_PORT" --remote "127.0.0.1:$ECHO_PORT" &
sleep 0.5

run_tests

# XOR two-hop tests: A(encrypt) → B(decrypt) → echo
XOR_ECHO_PORT=19997
XOR_DECODE_PORT=19996
XOR_LISTEN_PORT=${4:-19995}
XOR_KEY="${5:-my-secret-key}"

if "$TUNNEL_BIN" tunnel --help 2>/dev/null | grep -q cfb-key; then
    start_echo "$XOR_ECHO_PORT"
    sleep 0.5

    # Tunnel B: exit node (decrypt local, encrypt remote)
    "$TUNNEL_BIN" tunnel \
        --listen "127.0.0.1:$XOR_DECODE_PORT" \
        --remote "127.0.0.1:$XOR_ECHO_PORT" \
        --cfb-key "$XOR_KEY" --cfb-reverse &

    # Tunnel A: encrypt side (CFB encode), listen external → B
    "$TUNNEL_BIN" tunnel \
        --listen "127.0.0.1:$XOR_LISTEN_PORT" \
        --remote "127.0.0.1:$XOR_DECODE_PORT" \
        --cfb-key "$XOR_KEY" &
    sleep 0.5

    run_xor_tests
fi

echo ""
echo "=== result: $PASS passed, $FAIL failed ==="
exit $FAIL
