#!/usr/bin/env bash
set -euo pipefail

BASE="${1:-http://127.0.0.1:8080/api/v1}"
WAIT_SEC="${2:-1}"

api_get() {
    local path="$1"
    curl -fsS "$BASE$path"
}

api_post() {
    local path="$1"
    local body="${2:-{}}"
    curl -fsS -X POST "$BASE$path" -H "Content-Type: application/json" -d "$body" >/dev/null
}

extract_health_field() {
    local json="$1"
    local field="$2"
    python3 - "$field" <<'PY' <<<"$json"
import json
import sys

field = sys.argv[1]
obj = json.load(sys.stdin)
health = obj.get("health", {}) if isinstance(obj, dict) else {}
value = health.get(field)
if isinstance(value, bool):
    print("true" if value else "false")
elif value is None:
    print("")
else:
    print(value)
PY
}

echo "[C1] REST smoke started"
echo "BASE=$BASE"

health_before="$(api_get "/links/abis/health")"
rsl_tx_before="$(extract_health_field "$health_before" "rslTxFrames")"
rsl_rx_before="$(extract_health_field "$health_before" "rslRxFrames")"
oml_tx_before="$(extract_health_field "$health_before" "omlTxFrames")"
status_before="$(extract_health_field "$health_before" "healthStatus")"

echo "before: status=$status_before rslTx=$rsl_tx_before rslRx=$rsl_rx_before omlTx=$oml_tx_before"

echo "step: RSL CHANNEL_ACTIVATION (chan=3 payload=[1,0,7])"
api_post "/links/abis/inject" '{"procedure":"RSL:CHANNEL_ACTIVATION","chanNr":3,"entity":3,"payload":[1,0,7]}'

echo "step: RSL PAGING_CMD (chan=0 payload=[33,67])"
api_post "/links/abis/inject" '{"procedure":"RSL:PAGING_CMD","chanNr":0,"entity":0,"payload":[33,67]}'

echo "step: RSL CHANNEL_RELEASE (chan=3 payload=[])"
api_post "/links/abis/inject" '{"procedure":"RSL:CHANNEL_RELEASE","chanNr":3,"entity":3,"payload":[]}'

sleep "$WAIT_SEC"

health_after="$(api_get "/links/abis/health")"
rsl_tx_after="$(extract_health_field "$health_after" "rslTxFrames")"
rsl_rx_after="$(extract_health_field "$health_after" "rslRxFrames")"
oml_tx_after="$(extract_health_field "$health_after" "omlTxFrames")"
status_after="$(extract_health_field "$health_after" "healthStatus")"

echo "after : status=$status_after rslTx=$rsl_tx_after rslRx=$rsl_rx_after omlTx=$oml_tx_after"

python3 - "$rsl_tx_before" "$rsl_tx_after" "$rsl_rx_before" "$rsl_rx_after" <<'PY'
import sys

def to_int(v):
    try:
        return int(v)
    except Exception:
        return None

b_tx, a_tx, b_rx, a_rx = map(to_int, sys.argv[1:5])
if None in (b_tx, a_tx, b_rx, a_rx):
    print("result: WARN (RSL frame counters are not available)")
    sys.exit(0)

d_tx = a_tx - b_tx
d_rx = a_rx - b_rx
print(f"delta : rslTxFrames={d_tx}, rslRxFrames={d_rx}")
if d_tx >= 3:
    print("result: OK (Option C.1 parameterized RSL inject works)")
else:
    print("result: WARN (unexpectedly low rslTxFrames delta)")
PY
