#!/usr/bin/env bash
# Integration test for TCP tunnel
set -euo pipefail
trap 'kill $(jobs -p) 2>/dev/null; wait 2>/dev/null' EXIT

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

# send data through tunnel on TUNNEL_PORT, return echoed response
send_recv() {
    send_recv_to "$TUNNEL_PORT" "$1"
}

# compare two files byte-by-byte (handles null bytes unlike bash [[ ]])
file_eq() {
    python3 -c "
import sys
a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read()
sys.exit(0 if a == b else 1)
" "$1" "$2"
}

# send data to port, write response to file, compare with expected file
send_recv_cmp() {
    local port="$1" expected="$2" outfile="$3"
    send_recv_to "$port" "$(cat "$expected")" > "$outfile"
    file_eq "$expected" "$outfile"
}

concurrent_test() {
    local n="$1"
    local port="$2"
    local payload="$3"
    local pids=()
    for i in $(seq 1 "$n"); do
        (
            result=$(send_recv_to "$port" "$payload")
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
    ok1=true; [[ "$result" = "hello tunnel" ]] || ok1=false
    check "short message" "[[ '$ok1' = 'true' ]]"

    echo "=== 2. empty message ==="
    result=$(send_recv "")
    ok2=true; [[ "$result" = "" ]] || ok2=false
    check "empty message" "[[ '$ok2' = 'true' ]]"

    echo "=== 3. 10KB payload ==="
    data=$(head -c 10240 /dev/urandom | base64 -w0)
    result=$(send_recv "$data")
    ok3=true; [[ "$result" = "$data" ]] || ok3=false
    check "10KB payload" "[[ '$ok3' = 'true' ]]"

    echo "=== 4. 64KB payload ==="
    data=$(head -c 65536 /dev/urandom | base64 -w0)
    result=$(send_recv "$data")
    ok4=true; [[ "$result" = "$data" ]] || ok4=false
    check "64KB payload" "[[ '$ok4' = 'true' ]]"

    echo "=== 5. 1MB payload ==="
    data=$(head -c 1048576 /dev/urandom | base64 -w0)
    result=$(send_recv "$data")
    ok5=true; [[ "$result" = "$data" ]] || ok5=false
    check "1MB payload" "[[ '$ok5' = 'true' ]]"

    echo "=== 6. 4 concurrent clients, 1KB each ==="
    local ok6=true
    data=$(head -c 1024 /dev/urandom | base64 -w0)
    concurrent_test 4 "$TUNNEL_PORT" "$data" || ok6=false
    check "4 concurrent 1KB" "[[ '$ok6' = 'true' ]]"

    echo "=== 7. 16 concurrent clients, 1KB each ==="
    local ok7=true
    data=$(head -c 1024 /dev/urandom | base64 -w0)
    concurrent_test 16 "$TUNNEL_PORT" "$data" || ok7=false
    check "16 concurrent 1KB" "[[ '$ok7' = 'true' ]]"

    echo "=== 8. 16 concurrent clients, 64KB each ==="
    local ok8=true
    data=$(head -c 65536 /dev/urandom | base64 -w0)
    concurrent_test 16 "$TUNNEL_PORT" "$data" || ok8=false
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
    ok10=true; [[ "$result" = "ping" ]] || ok10=false
    check "tunnel still responsive" "[[ '$ok10' = 'true' ]]"
}

# ---- XOR two-hop test: A(encrypt) → B(decrypt) → echo ----

run_xor_tests() {
    echo ""
    echo "--- XOR two-hop tests (A encrypt → B decrypt) ---"

    local ok1=true
    result=$(send_recv_to "$XOR_LISTEN_PORT" "hello xor")
    [[ "$result" = "hello xor" ]] || ok1=false
    check "xor short message" "[[ '$ok1' = 'true' ]]"

    local ok2=true
    result=$(send_recv_to "$XOR_LISTEN_PORT" "")
    [[ "$result" = "" ]] || ok2=false
    check "xor empty message" "[[ '$ok2' = 'true' ]]"

    local ok3=true
    data=$(head -c 10240 /dev/urandom | base64 -w0)
    result=$(send_recv_to "$XOR_LISTEN_PORT" "$data")
    [[ "$result" = "$data" ]] || ok3=false
    check "xor 10KB payload" "[[ '$ok3' = 'true' ]]"

    local ok4=true
    data=$(head -c 65536 /dev/urandom | base64 -w0)
    result=$(send_recv_to "$XOR_LISTEN_PORT" "$data")
    [[ "$result" = "$data" ]] || ok4=false
    check "xor 64KB payload" "[[ '$ok4' = 'true' ]]"

    local ok5=true
    data=$(head -c 1024 /dev/urandom | base64 -w0)
    concurrent_test 16 "$XOR_LISTEN_PORT" "$data" || ok5=false
    check "xor 16 concurrent 1KB" "[[ '$ok5' = 'true' ]]"

    local ok6=true
    result=$(send_recv_to "$XOR_LISTEN_PORT" "ping")
    [[ "$result" = "ping" ]] || ok6=false
    check "xor tunnel still alive" "[[ '$ok6' = 'true' ]]"

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

# ---- cfb_nonce two-hop test: A(encrypt) -> B(decrypt) -> echo ----

run_cfb_nonce_tests() {
    echo ""
    echo "--- cfb_nonce two-hop tests (A encrypt -> B decrypt) ---"

    local ok1=true
    result=$(send_recv_to "$CFB_NONCE_LISTEN_PORT" "hello cfb_nonce")
    [[ "$result" = "hello cfb_nonce" ]] || ok1=false
    check "cfb_nonce short message" "[[ '$ok1' = 'true' ]]"

    local ok2=true
    result=$(send_recv_to "$CFB_NONCE_LISTEN_PORT" "")
    [[ "$result" = "" ]] || ok2=false
    check "cfb_nonce empty message" "[[ '$ok2' = 'true' ]]"

    local ok3=true
    data=$(head -c 10240 /dev/urandom | base64 -w0)
    result=$(send_recv_to "$CFB_NONCE_LISTEN_PORT" "$data")
    [[ "$result" = "$data" ]] || ok3=false
    check "cfb_nonce 10KB payload" "[[ '$ok3' = 'true' ]]"

    local ok4=true
    data=$(head -c 65536 /dev/urandom | base64 -w0)
    result=$(send_recv_to "$CFB_NONCE_LISTEN_PORT" "$data")
    [[ "$result" = "$data" ]] || ok4=false
    check "cfb_nonce 64KB payload" "[[ '$ok4' = 'true' ]]"

    local ok5=true
    data=$(head -c 1024 /dev/urandom | base64 -w0)
    concurrent_test 16 "$CFB_NONCE_LISTEN_PORT" "$data" || ok5=false
    check "cfb_nonce 16 concurrent 1KB" "[[ '$ok5' = 'true' ]]"

    local ok6=true
    result=$(send_recv_to "$CFB_NONCE_LISTEN_PORT" "ping")
    [[ "$result" = "ping" ]] || ok6=false
    check "cfb_nonce tunnel still alive" "[[ '$ok6' = 'true' ]]"
}

# ---- padding two-hop test: A(pad encode) → B(pad decode) → echo ----

run_padding_tests() {
    echo ""
    echo "--- padding two-hop tests (A pad encode → B pad decode) ---"

    local ok1=true
    result=$(send_recv_to "$PAD_LISTEN_PORT" "hello pad")
    [[ "$result" = "hello pad" ]] || ok1=false
    check "pad short message" "[[ '$ok1' = 'true' ]]"

    local ok2=true
    result=$(send_recv_to "$PAD_LISTEN_PORT" "")
    [[ "$result" = "" ]] || ok2=false
    check "pad empty message" "[[ '$ok2' = 'true' ]]"

    local ok3=true
    data=$(head -c 10240 /dev/urandom | base64 -w0)
    result=$(send_recv_to "$PAD_LISTEN_PORT" "$data")
    [[ "$result" = "$data" ]] || ok3=false
    check "pad 10KB payload" "[[ '$ok3' = 'true' ]]"

    local ok4=true
    data=$(head -c 65536 /dev/urandom | base64 -w0)
    result=$(send_recv_to "$PAD_LISTEN_PORT" "$data")
    [[ "$result" = "$data" ]] || ok4=false
    check "pad 64KB payload" "[[ '$ok4' = 'true' ]]"

    local ok5=true
    data=$(head -c 1048576 /dev/urandom | base64 -w0)
    result=$(send_recv_to "$PAD_LISTEN_PORT" "$data")
    [[ "$result" = "$data" ]] || ok5=false
    check "pad 1MB payload" "[[ '$ok5' = 'true' ]]"

    local ok6=true
    data=$(head -c 1024 /dev/urandom | base64 -w0)
    concurrent_test 16 "$PAD_LISTEN_PORT" "$data" || ok6=false
    check "pad 16 concurrent 1KB" "[[ '$ok6' = 'true' ]]"

    local ok7=true
    data=$(head -c 65536 /dev/urandom | base64 -w0)
    concurrent_test 4 "$PAD_LISTEN_PORT" "$data" || ok7=false
    check "pad 4 concurrent 64KB" "[[ '$ok7' = 'true' ]]"

    local ok8=true
    result=$(send_recv_to "$PAD_LISTEN_PORT" "ping")
    [[ "$result" = "ping" ]] || ok8=false
    check "pad tunnel still alive" "[[ '$ok8' = 'true' ]]"
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

if "$TUNNEL_BIN" tunnel --help 2>/dev/null | grep -q processor; then
    start_echo "$XOR_ECHO_PORT"
    sleep 0.5

    # Tunnel B: exit node (decrypt local, encrypt remote)
    "$TUNNEL_BIN" tunnel \
        --listen "127.0.0.1:$XOR_DECODE_PORT" \
        --remote "127.0.0.1:$XOR_ECHO_PORT" \
        --processor "cfb:key=$XOR_KEY,reverse=1" &

    # Tunnel A: encrypt side (CFB encode), listen external → B
    "$TUNNEL_BIN" tunnel \
        --listen "127.0.0.1:$XOR_LISTEN_PORT" \
        --remote "127.0.0.1:$XOR_DECODE_PORT" \
        --processor "cfb:key=$XOR_KEY" &
    sleep 0.5

    run_xor_tests
fi

# cfb_nonce two-hop tests: A(encrypt) -> B(decrypt) -> echo
CFB_NONCE_ECHO_PORT=19988
CFB_NONCE_DECODE_PORT=19987
CFB_NONCE_LISTEN_PORT=${11:-19986}
CFB_NONCE_IV="${12:-nonce-seed-iv}"
CFB_NONCE_HMAC_KEY="${13:-nonce-shared-secret}"

if "$TUNNEL_BIN" tunnel --help 2>/dev/null | grep -q processor; then
    start_echo "$CFB_NONCE_ECHO_PORT"
    sleep 0.5

    # Tunnel B: exit node (decrypt local, encrypt remote)
    "$TUNNEL_BIN" tunnel \
        --listen "127.0.0.1:$CFB_NONCE_DECODE_PORT" \
        --remote "127.0.0.1:$CFB_NONCE_ECHO_PORT" \
        --processor "cfb_nonce:iv=$CFB_NONCE_IV,hmac_key=$CFB_NONCE_HMAC_KEY,reverse=1" &

    # Tunnel A: encrypt side (CFB_NONCE encode), listen external -> B
    "$TUNNEL_BIN" tunnel \
        --listen "127.0.0.1:$CFB_NONCE_LISTEN_PORT" \
        --remote "127.0.0.1:$CFB_NONCE_DECODE_PORT" \
        --processor "cfb_nonce:iv=$CFB_NONCE_IV,hmac_key=$CFB_NONCE_HMAC_KEY" &
    sleep 0.5

    run_cfb_nonce_tests
fi

# padding two-hop tests: A(pad encode) → B(pad decode) → echo
PAD_ECHO_PORT=19994
PAD_DECODE_PORT=19993
PAD_LISTEN_PORT=${6:-19992}
PAD_CHUNK=${7:-1024}
PAD_MAX=${8:-64}

if "$TUNNEL_BIN" tunnel --help 2>/dev/null | grep -q processor; then
    start_echo "$PAD_ECHO_PORT"
    sleep 0.5

    "$TUNNEL_BIN" tunnel \
        --listen "127.0.0.1:$PAD_DECODE_PORT" \
        --remote "127.0.0.1:$PAD_ECHO_PORT" \
        --processor "padding:chunk=$PAD_CHUNK,max=$PAD_MAX,reverse=1" &

    "$TUNNEL_BIN" tunnel \
        --listen "127.0.0.1:$PAD_LISTEN_PORT" \
        --remote "127.0.0.1:$PAD_DECODE_PORT" \
        --processor "padding:chunk=$PAD_CHUNK,max=$PAD_MAX" &
    sleep 0.5

    run_padding_tests
fi

# padding+cfb pipeline two-hop tests: A(cfb→pad) → B(unpad→decfb) → echo
PC_ECHO_PORT=19991
PC_DECODE_PORT=19990
PC_LISTEN_PORT=${9:-19989}
PC_KEY="${10:-pipeline-secret}"

run_padding_cfb_tests() {
    echo ""
    echo "--- padding+cfb pipeline two-hop tests ---"

    local ok1=true
    result=$(send_recv_to "$PC_LISTEN_PORT" "hello pipeline")
    [[ "$result" = "hello pipeline" ]] || ok1=false
    check "pad+cfb short message" "[[ '$ok1' = 'true' ]]"

    local ok2=true
    result=$(send_recv_to "$PC_LISTEN_PORT" "")
    [[ "$result" = "" ]] || ok2=false
    check "pad+cfb empty message" "[[ '$ok2' = 'true' ]]"

    local ok3=true
    data=$(head -c 10240 /dev/urandom | base64 -w0)
    result=$(send_recv_to "$PC_LISTEN_PORT" "$data")
    [[ "$result" = "$data" ]] || ok3=false
    check "pad+cfb 10KB payload" "[[ '$ok3' = 'true' ]]"

    local ok4=true
    data=$(head -c 65536 /dev/urandom | base64 -w0)
    result=$(send_recv_to "$PC_LISTEN_PORT" "$data")
    [[ "$result" = "$data" ]] || ok4=false
    check "pad+cfb 64KB payload" "[[ '$ok4' = 'true' ]]"

    local ok5=true
    data=$(head -c 1024 /dev/urandom | base64 -w0)
    concurrent_test 16 "$PC_LISTEN_PORT" "$data" || ok5=false
    check "pad+cfb 16 concurrent 1KB" "[[ '$ok5' = 'true' ]]"

    local ok6=true
    result=$(send_recv_to "$PC_LISTEN_PORT" "ping")
    [[ "$result" = "ping" ]] || ok6=false
    check "pad+cfb tunnel still alive" "[[ '$ok6' = 'true' ]]"
}

if "$TUNNEL_BIN" tunnel --help 2>/dev/null | grep -q processor; then
    start_echo "$PC_ECHO_PORT"
    sleep 0.5

    "$TUNNEL_BIN" tunnel \
        --listen "127.0.0.1:$PC_DECODE_PORT" \
        --remote "127.0.0.1:$PC_ECHO_PORT" \
        --processor "padding:chunk=1024,max=64,reverse=1" \
        --processor "cfb:key=$PC_KEY,reverse=1" &

    "$TUNNEL_BIN" tunnel \
        --listen "127.0.0.1:$PC_LISTEN_PORT" \
        --remote "127.0.0.1:$PC_DECODE_PORT" \
        --processor "cfb:key=$PC_KEY" \
        --processor "padding:chunk=1024,max=64" &
    sleep 0.5

    run_padding_cfb_tests
fi

# padding+cfb_nonce pipeline two-hop tests: A(cfb_nonce→pad) → B(unpad→decfb_nonce) → echo
PNC_ECHO_PORT=19985
PNC_DECODE_PORT=19984
PNC_LISTEN_PORT=${14:-19983}
PNC_IV="${15:-pipeline-nonce-iv}"
PNC_HMAC_KEY="${16:-pipeline-nonce-secret}"

run_padding_cfb_nonce_tests() {
    echo ""
    echo "--- padding+cfb_nonce pipeline two-hop tests ---"

    local ok1=true
    result=$(send_recv_to "$PNC_LISTEN_PORT" "hello pad+cfb_nonce")
    [[ "$result" = "hello pad+cfb_nonce" ]] || ok1=false
    check "pad+cfb_nonce short message" "[[ '$ok1' = 'true' ]]"

    local ok2=true
    result=$(send_recv_to "$PNC_LISTEN_PORT" "")
    [[ "$result" = "" ]] || ok2=false
    check "pad+cfb_nonce empty message" "[[ '$ok2' = 'true' ]]"

    local ok3=true
    data=$(head -c 10240 /dev/urandom | base64 -w0)
    result=$(send_recv_to "$PNC_LISTEN_PORT" "$data")
    [[ "$result" = "$data" ]] || ok3=false
    check "pad+cfb_nonce 10KB payload" "[[ '$ok3' = 'true' ]]"

    local ok4=true
    data=$(head -c 65536 /dev/urandom | base64 -w0)
    result=$(send_recv_to "$PNC_LISTEN_PORT" "$data")
    [[ "$result" = "$data" ]] || ok4=false
    check "pad+cfb_nonce 64KB payload" "[[ '$ok4' = 'true' ]]"

    local ok5=true
    data=$(head -c 1024 /dev/urandom | base64 -w0)
    concurrent_test 16 "$PNC_LISTEN_PORT" "$data" || ok5=false
    check "pad+cfb_nonce 16 concurrent 1KB" "[[ '$ok5' = 'true' ]]"

    local ok6=true
    result=$(send_recv_to "$PNC_LISTEN_PORT" "ping")
    [[ "$result" = "ping" ]] || ok6=false
    check "pad+cfb_nonce tunnel still alive" "[[ '$ok6' = 'true' ]]"
}

if "$TUNNEL_BIN" tunnel --help 2>/dev/null | grep -q processor; then
    start_echo "$PNC_ECHO_PORT"
    sleep 0.5

    "$TUNNEL_BIN" tunnel \
        --listen "127.0.0.1:$PNC_DECODE_PORT" \
        --remote "127.0.0.1:$PNC_ECHO_PORT" \
        --processor "padding:chunk=1024,max=64,reverse=1" \
        --processor "cfb_nonce:iv=$PNC_IV,hmac_key=$PNC_HMAC_KEY,reverse=1" &

    "$TUNNEL_BIN" tunnel \
        --listen "127.0.0.1:$PNC_LISTEN_PORT" \
        --remote "127.0.0.1:$PNC_DECODE_PORT" \
        --processor "cfb_nonce:iv=$PNC_IV,hmac_key=$PNC_HMAC_KEY" \
        --processor "padding:chunk=1024,max=64" &
    sleep 0.5

    run_padding_cfb_nonce_tests

    echo ""
    echo "--- padding+cfb_nonce ordering validation (pad before cfb/cfb_nonce must fail) ---"

    err=$(2>&1 "$TUNNEL_BIN" tunnel \
        --listen "127.0.0.1:19980" \
        --remote "127.0.0.1:19981" \
        --processor "padding:chunk=1024,max=64" \
        --processor "cfb:key=test" \
        || true)
    if echo "$err" | grep -q "must be placed before padding"; then
        check "pad before cfb rejected" "true"
    else
        check "pad before cfb rejected" "false"
    fi

    err=$(2>&1 "$TUNNEL_BIN" tunnel \
        --listen "127.0.0.1:19980" \
        --remote "127.0.0.1:19981" \
        --processor "padding:chunk=1024,max=64" \
        --processor "cfb_nonce:iv=test,hmac_key=test" \
        || true)
    if echo "$err" | grep -q "must be placed before padding"; then
        check "pad before cfb_nonce rejected" "true"
    else
        check "pad before cfb_nonce rejected" "false"
    fi
fi

echo ""
echo "=== result: $PASS passed, $FAIL failed ==="
exit $FAIL
