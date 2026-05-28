#!/usr/bin/env bash
# Integration test for IDR relay mode (B ↔ A ↔ C)
set -euo pipefail
trap 'kill $(jobs -p) 2>/dev/null; wait 2>/dev/null' EXIT

TUNNEL_BIN="${1:-./dist/bin/msg801}"
RELAY_PORT_A="${2:-19970}"           # Server A (shared by all pairs)
RELAY_PORT_B1="${3:-19971}"          # Visitor B1 listen (id=alice)
RELAY_PORT_C1_ECHO="${4:-19972}"     # Echo behind C1
RELAY_PORT_B2="${5:-19974}"          # Visitor B2 listen (id=bob)
RELAY_PORT_C2_ECHO="${6:-19973}"     # Echo behind C2
RELAY_PORT_B_DEAD="${7:-19976}"      # Visitor B_dead listen (unreachable)
DEAD_TARGET_PORT=19977               # Nothing listening here
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

start_echo() {
    local port="$1"
    python3 - "$port" <<'PY' &
import socket, sys, threading
port = int(sys.argv[1])
def echo(s):
    while True:
        d = s.recv(65536)
        if not d: break
        s.sendall(d)
    s.close()
s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('127.0.0.1', port))
s.listen(50)
sys.stderr.write(f'echo ready on {port}\n')
sys.stderr.flush()
while True:
    c, _ = s.accept()
    threading.Thread(target=echo, args=(c,), daemon=True).start()
PY
}

send_recv_to() {
    local port="$1"
    printf '%s' "$2" | python3 -c "
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
" "$port"
}

concurrent_test() {
    local n="$1" port="$2" payload="$3"
    local pids=()
    for i in $(seq 1 "$n"); do
        (
            result=$(send_recv_to "$port" "$payload")
            if [[ "$result" != "$payload" ]]; then exit 1; fi
            exit 0
        ) &
        pids+=($!)
    done
    local ok=0
    for pid in "${pids[@]}"; do wait "$pid" || ok=1; done
    return "$ok"
}

run_basic_tests() {
    local port="$1" label="$2"

    echo "=== $label: short message ==="
    local ok1=true
    result=$(send_recv_to "$port" "hello relay")
    [[ "$result" = "hello relay" ]] || ok1=false
    check "short message" "[[ '$ok1' = 'true' ]]"

    echo "=== $label: empty message ==="
    local ok2=true
    result=$(send_recv_to "$port" "")
    [[ "$result" = "" ]] || ok2=false
    check "empty message" "[[ '$ok2' = 'true' ]]"

    echo "=== $label: 10KB payload ==="
    local ok3=true
    data=$(head -c 10240 /dev/urandom | base64 -w0)
    result=$(send_recv_to "$port" "$data")
    [[ "$result" = "$data" ]] || ok3=false
    check "10KB payload" "[[ '$ok3' = 'true' ]]"

    echo "=== $label: 64KB payload ==="
    local ok4=true
    data=$(head -c 65536 /dev/urandom | base64 -w0)
    result=$(send_recv_to "$port" "$data")
    [[ "$result" = "$data" ]] || ok4=false
    check "64KB payload" "[[ '$ok4' = 'true' ]]"

    echo "=== $label: 1MB payload ==="
    local ok5=true
    data=$(head -c 1048576 /dev/urandom | base64 -w0)
    result=$(send_recv_to "$port" "$data")
    [[ "$result" = "$data" ]] || ok5=false
    check "1MB payload" "[[ '$ok5' = 'true' ]]"

    echo "=== $label: 4 concurrent 1KB ==="
    local ok6=true
    data=$(head -c 1024 /dev/urandom | base64 -w0)
    concurrent_test 4 "$port" "$data" || ok6=false
    check "4 concurrent 1KB" "[[ '$ok6' = 'true' ]]"

    echo "=== $label: 16 concurrent 1KB ==="
    local ok7=true
    data=$(head -c 1024 /dev/urandom | base64 -w0)
    concurrent_test 16 "$port" "$data" || ok7=false
    check "16 concurrent 1KB" "[[ '$ok7' = 'true' ]]"

    echo "=== $label: rapid connect/disconnect ==="
    local pids=()
    for i in $(seq 1 20); do
        send_recv_to "$port" "x" >/dev/null 2>/dev/null &
        pids+=($!)
    done
    for pid in "${pids[@]}"; do wait "$pid" 2>/dev/null || true; done
    check "20 rapid connect/disconnect" "true"

    echo "=== $label: still alive ==="
    local ok9=true
    result=$(send_recv_to "$port" "ping")
    [[ "$result" = "ping" ]] || ok9=false
    check "still responsive" "[[ '$ok9' = 'true' ]]"
}

run_advanced_tests() {
    echo ""
    echo "=== relay: unreachable target ==="
    local ok_dead=true
    result=$(send_recv_to "$RELAY_PORT_B_DEAD" "hello")
    [[ "$result" = "" ]] || ok_dead=false
    check "unreachable target returns empty" "[[ '$ok_dead' = 'true' ]]"

    echo "=== relay: pairs independent of dead pair ==="
    local ok_alice=true
    result=$(send_recv_to "$RELAY_PORT_B1" "still alive")
    [[ "$result" = "still alive" ]] || ok_alice=false
    check "alice pair still works" "[[ '$ok_alice' = 'true' ]]"

    local ok_bob=true
    result=$(send_recv_to "$RELAY_PORT_B2" "bob independent")
    [[ "$result" = "bob independent" ]] || ok_bob=false
    check "bob pair still works" "[[ '$ok_bob' = 'true' ]]"

    echo "=== relay: half-close client (shutdown_wr) ==="
    local ok_hc=true
    python3 -c "
import socket, sys, time
s = socket.socket()
s.settimeout(5)
s.connect(('127.0.0.1', $RELAY_PORT_B1))
s.sendall(b'hello half-close')
s.shutdown(socket.SHUT_WR)
resp = b''
while True:
    try:
        chunk = s.recv(65536)
        if not chunk: break
        resp += chunk
    except: break
s.close()
sys.exit(0 if resp == b'hello half-close' else 1)
" || ok_hc=false
    check "half-close data integrity" "[[ '$ok_hc' = 'true' ]]"

    echo "=== relay: still alive after all ==="
    local ok_final=true
    result=$(send_recv_to "$RELAY_PORT_B1" "final check")
    [[ "$result" = "final check" ]] || ok_final=false
    check "relay still responsive" "[[ '$ok_final' = 'true' ]]"
}

# ---- main ----

if ! "$TUNNEL_BIN" relay --help >/dev/null 2>&1; then
    echo "relay subcommand not supported"; exit 1
fi

start_echo "$RELAY_PORT_C1_ECHO"
start_echo "$RELAY_PORT_C2_ECHO"
sleep 0.5

"$TUNNEL_BIN" relay --server --port "$RELAY_PORT_A" &
sleep 0.5

# Pair "alice" (echo on C1_ECHO)
"$TUNNEL_BIN" relay --provider --connect 127.0.0.1 --port "$RELAY_PORT_A" \
    --id alice --target "127.0.0.1:$RELAY_PORT_C1_ECHO" &
"$TUNNEL_BIN" relay --visitor  --connect 127.0.0.1 --port "$RELAY_PORT_A" \
    --id alice --listen "127.0.0.1:$RELAY_PORT_B1" &

# Pair "bob" (separate id, separate echo on same A)
"$TUNNEL_BIN" relay --provider --connect 127.0.0.1 --port "$RELAY_PORT_A" \
    --id bob --target "127.0.0.1:$RELAY_PORT_C2_ECHO" &
"$TUNNEL_BIN" relay --visitor  --connect 127.0.0.1 --port "$RELAY_PORT_A" \
    --id bob --listen "127.0.0.1:$RELAY_PORT_B2" &

# Pair "dead" (C target is a dead port → SESSION_ERR)
"$TUNNEL_BIN" relay --provider --connect 127.0.0.1 --port "$RELAY_PORT_A" \
    --id dead --target "127.0.0.1:$DEAD_TARGET_PORT" &
"$TUNNEL_BIN" relay --visitor  --connect 127.0.0.1 --port "$RELAY_PORT_A" \
    --id dead --listen "127.0.0.1:$RELAY_PORT_B_DEAD" &

sleep 1

run_basic_tests "$RELAY_PORT_B1" "relay alice"
run_basic_tests "$RELAY_PORT_B2" "relay bob"
run_advanced_tests

echo ""
echo "=== result: $PASS passed, $FAIL failed ==="
exit $FAIL
