#!/usr/bin/env bash
# Run in two disposable network namespaces; no physical NIC is modified.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

if [[ $(uname -s) != Linux || $EUID -ne 0 ]]; then
    echo "Run on Linux as root: sudo bash ./demo.sh" >&2
    exit 1
fi
for tool in ip ping python3 grep mktemp; do
    command -v "$tool" >/dev/null || { echo "Missing command: $tool" >&2; exit 1; }
done
[[ -x ./af_xdp_echo && -f ./xdp_kern.o ]] || { echo "Run make first" >&2; exit 1; }

server_ns="xsk-server-$$"
client_ns="xsk-client-$$"
server_created=0
client_created=0
server_pid=
log=$(mktemp)
cleanup() {
    local result=$?
    trap - EXIT
    if [[ -n $server_pid ]]; then
        kill -INT "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    cat "$log"
    (( client_created == 0 )) || ip netns del "$client_ns" || true
    (( server_created == 0 )) || ip netns del "$server_ns" || true
    rm -f "$log"
    exit "$result"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

ip netns add "$server_ns"
server_created=1
ip netns add "$client_ns"
client_created=1
ip -n "$server_ns" link add xsk0 numrxqueues 1 numtxqueues 1 type veth peer name peer0
ip -n "$server_ns" link set peer0 netns "$client_ns"
ip -n "$server_ns" addr add 192.0.2.1/24 dev xsk0
ip -n "$client_ns" addr add 192.0.2.2/24 dev peer0
ip -n "$server_ns" link set lo up
ip -n "$client_ns" link set lo up
ip -n "$server_ns" link set xsk0 up
ip -n "$client_ns" link set peer0 up

ip netns exec "$server_ns" ./af_xdp_echo -i xsk0 >"$log" 2>&1 &
server_pid=$!
for ((i = 0; i < 50; i++)); do
    if grep -q '^READY ' "$log"; then break; fi
    kill -0 "$server_pid" 2>/dev/null || { echo "Server failed to start" >&2; exit 1; }
    sleep 0.1
done
grep -q '^READY ' "$log" || { echo "Server startup timed out" >&2; exit 1; }

# ARP + ICMP must still pass through the ordinary kernel stack.
ip netns exec "$client_ns" ping -c 1 -W 2 192.0.2.1
# More than NUM_FRAMES requests catches missing COMPLETION -> FILL recycling.
ip netns exec "$client_ns" python3 - <<'PY'
import socket

with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
    sock.settimeout(2)
    sock.connect(("192.0.2.1", 9000))
    for i in range(3072):
        size = (0, 1, 64, 512, 1400)[i % 5]
        payload = bytes([i % 251]) * size
        sock.send(payload)
        reply = sock.recv(2048)
        if reply != payload:
            raise RuntimeError(f"echo mismatch at packet {i}")
print("PASS: 3072 UDP echoes, payload sizes 0/1/64/512/1400 bytes")
PY
sleep 0.1
kill -INT "$server_pid"
if wait "$server_pid"; then
    server_pid=
else
    server_pid=
    echo "Server exited with an error" >&2
    exit 1
fi
python3 - "$log" <<'PY'
import pathlib
import re
import sys

text = pathlib.Path(sys.argv[1]).read_text()
match = re.search(r"rx=(\d+) tx_submitted=(\d+) tx_completed=(\d+) dropped=(\d+)", text)
if not match or tuple(map(int, match.groups())) != (3072, 3072, 3072, 0):
    raise RuntimeError("unexpected RX/TX/completion counters; see server log")
print("PASS: all TX frames returned through COMPLETION, no drops")
PY
