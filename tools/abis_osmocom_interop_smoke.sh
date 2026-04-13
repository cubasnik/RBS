#!/usr/bin/env bash
set -euo pipefail

# Real Abis IPA interop smoke against external Osmocom BSC.
#
# Usage:
#   ./tools/abis_osmocom_interop_smoke.sh \
#     [BASE] [EXPECTED_BSC_IP] [EXPECTED_BSC_PORT] [CONNECT_TIMEOUT_SEC] [TRAFFIC_WAIT_SEC]
#
# Example:
#   ./tools/abis_osmocom_interop_smoke.sh \
#     http://10.10.10.2:8080/api/v1 10.10.10.1 3002 12 2

BASE="${1:-http://127.0.0.1:8080/api/v1}"
EXPECTED_BSC_IP="${2:-10.10.10.1}"
EXPECTED_BSC_PORT="${3:-3002}"
CONNECT_TIMEOUT_SEC="${4:-12}"
TRAFFIC_WAIT_SEC="${5:-2}"

api_get() {
    local path="$1"
    curl -fsS "$BASE$path"
}

api_post() {
    local path="$1"
    local body="${2:-{}}"
    curl -fsS -X POST "$BASE$path" -H "Content-Type: application/json" -d "$body" >/dev/null
}

json_get() {
    local json="$1"
    local expr="$2"
    python3 - "$expr" <<'PY' <<<"$json"
import json
import sys

expr = sys.argv[1]
obj = json.load(sys.stdin)

cur = obj
for part in expr.split('.'):
    if isinstance(cur, dict) and part in cur:
        cur = cur[part]
    else:
        print("")
        sys.exit(0)

if isinstance(cur, bool):
    print("true" if cur else "false")
elif cur is None:
    print("")
else:
    print(cur)
PY
}

extract_abis_peer() {
    local links_json="$1"
    python3 - <<'PY' <<<"$links_json"
import json
import sys

arr = json.load(sys.stdin)
if not isinstance(arr, list):
    print("")
    sys.exit(0)

for item in arr:
    if isinstance(item, dict) and item.get("name") == "abis":
        peer = item.get("peer", "")
        print(peer)
        sys.exit(0)

print("")
PY
}

extract_trace_counts() {
    local trace_json="$1"
    python3 - <<'PY' <<<"$trace_json"
import json
import sys

obj = json.load(sys.stdin)
msgs = obj.get("messages", []) if isinstance(obj, dict) else []

tx = 0
rx = 0
rx_oml = 0
rx_rsl = 0
for m in msgs:
    if not isinstance(m, dict):
        continue
    t = str(m.get("type", ""))
    is_tx = bool(m.get("tx", False))
    if is_tx:
        tx += 1
    else:
        rx += 1
        if t.startswith("OML:"):
            rx_oml += 1
        if t.startswith("RSL:"):
            rx_rsl += 1

print(f"{tx} {rx} {rx_oml} {rx_rsl}")
PY
}

echo "[Osmocom] BASE=$BASE"
echo "[Osmocom] expected peer: ${EXPECTED_BSC_IP}:${EXPECTED_BSC_PORT}"

echo "[Osmocom] precheck REST"
api_get "/status" >/dev/null

links_json="$(api_get "/links")"
peer="$(extract_abis_peer "$links_json")"
if [[ -z "$peer" ]]; then
    echo "result: FAIL (abis link is not registered in /links)"
    exit 2
fi
echo "[Osmocom] configured Abis peer: $peer"

expected_peer="${EXPECTED_BSC_IP}:${EXPECTED_BSC_PORT}"
if [[ "$peer" != "$expected_peer" ]]; then
    echo "result: FAIL (peer mismatch: configured=$peer expected=$expected_peer)"
    exit 3
fi

health_before="$(api_get "/links/abis/health")"
mode="$(json_get "$health_before" "health.mode")"
profile="$(json_get "$health_before" "health.interopProfile")"
connected_before="$(json_get "$health_before" "connected")"

if [[ "$mode" != "ipa_tcp" ]]; then
    echo "result: FAIL (Abis mode must be ipa_tcp, got: ${mode:-<empty>})"
    exit 4
fi

if [[ "$profile" != "osmocom" ]]; then
    echo "[Osmocom] WARN: interopProfile is '${profile:-<empty>}' (expected 'osmocom')"
fi

echo "[Osmocom] before connect: connected=$connected_before mode=$mode interopProfile=$profile"

echo "[Osmocom] reconnect cycle"
api_post "/links/abis/disconnect"
api_post "/links/abis/connect"

deadline=$(( $(date +%s) + CONNECT_TIMEOUT_SEC ))
connected="false"
while [[ $(date +%s) -lt $deadline ]]; do
    h="$(api_get "/links/abis/health")"
    connected="$(json_get "$h" "connected")"
    if [[ "$connected" == "true" ]]; then
        break
    fi
    sleep 1
done

if [[ "$connected" != "true" ]]; then
    echo "result: FAIL (Abis did not connect within ${CONNECT_TIMEOUT_SEC}s)"
    exit 5
fi

health_start="$(api_get "/links/abis/health")"
oml_rx_before="$(json_get "$health_start" "health.omlRxFrames")"
rsl_rx_before="$(json_get "$health_start" "health.rslRxFrames")"
oml_tx_before="$(json_get "$health_start" "health.omlTxFrames")"
rsl_tx_before="$(json_get "$health_start" "health.rslTxFrames")"

echo "[Osmocom] counters before traffic: omlTx=$oml_tx_before omlRx=$oml_rx_before rslTx=$rsl_tx_before rslRx=$rsl_rx_before"

echo "[Osmocom] inject OML/RSL procedures"
api_post "/links/abis/inject" '{"procedure":"OML:OPSTART"}'
api_post "/links/abis/inject" '{"procedure":"RSL:CHANNEL_ACTIVATION","chanNr":3,"entity":3,"payload":[1,0,7]}'
api_post "/links/abis/inject" '{"procedure":"RSL:PAGING_CMD","chanNr":0,"entity":0,"payload":[33,67]}'
api_post "/links/abis/inject" '{"procedure":"RSL:CHANNEL_RELEASE","chanNr":3,"entity":3,"payload":[]}'

sleep "$TRAFFIC_WAIT_SEC"

health_after="$(api_get "/links/abis/health")"
oml_rx_after="$(json_get "$health_after" "health.omlRxFrames")"
rsl_rx_after="$(json_get "$health_after" "health.rslRxFrames")"
oml_tx_after="$(json_get "$health_after" "health.omlTxFrames")"
rsl_tx_after="$(json_get "$health_after" "health.rslTxFrames")"
status_after="$(json_get "$health_after" "health.healthStatus")"

trace_json="$(api_get "/links/abis/trace?limit=200")"
read -r trace_tx trace_rx trace_rx_oml trace_rx_rsl <<<"$(extract_trace_counts "$trace_json")"

echo "[Osmocom] counters after traffic : omlTx=$oml_tx_after omlRx=$oml_rx_after rslTx=$rsl_tx_after rslRx=$rsl_rx_after status=$status_after"

python3 - "$oml_rx_before" "$oml_rx_after" "$rsl_rx_before" "$rsl_rx_after" \
           "$oml_tx_before" "$oml_tx_after" "$rsl_tx_before" "$rsl_tx_after" \
           "$trace_tx" "$trace_rx" "$trace_rx_oml" "$trace_rx_rsl" <<'PY'
import sys

vals = []
for x in sys.argv[1:]:
    try:
        vals.append(int(x))
    except Exception:
        print("result: FAIL (non-integer counter in health/trace response)")
        sys.exit(6)

oml_rx_b, oml_rx_a, rsl_rx_b, rsl_rx_a, oml_tx_b, oml_tx_a, rsl_tx_b, rsl_tx_a, trace_tx, trace_rx, trace_rx_oml, trace_rx_rsl = vals

d_oml_rx = oml_rx_a - oml_rx_b
d_rsl_rx = rsl_rx_a - rsl_rx_b
d_oml_tx = oml_tx_a - oml_tx_b
d_rsl_tx = rsl_tx_a - rsl_tx_b

print(f"delta: omlTx={d_oml_tx} omlRx={d_oml_rx} rslTx={d_rsl_tx} rslRx={d_rsl_rx}")
print(f"trace: tx={trace_tx} rx={trace_rx} rx_oml={trace_rx_oml} rx_rsl={trace_rx_rsl}")

if d_oml_tx < 1 or d_rsl_tx < 2:
    print("result: FAIL (RBS did not transmit expected OML/RSL frames)")
    sys.exit(7)

if d_oml_rx < 1 and d_rsl_rx < 1:
    print("result: FAIL (no inbound Abis response frames observed from external peer)")
    sys.exit(8)

if trace_rx < 1:
    print("result: FAIL (trace has no RX messages, interop response not confirmed)")
    sys.exit(9)

print("result: OK (external Osmocom Abis interop smoke passed)")
PY
