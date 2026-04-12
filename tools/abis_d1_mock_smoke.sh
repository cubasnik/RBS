#!/usr/bin/env bash
set -euo pipefail

BASE="${1:-http://127.0.0.1:8080/api/v1}"
MOCK_HOST="${2:-127.0.0.1}"
MOCK_PORT="${3:-3002}"
WAIT_SEC="${4:-1}"

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
MOCK_SCRIPT="$ROOT_DIR/tools/mock_bsc_ipa.py"
STATS_FILE="${TMPDIR:-/tmp}/mock_bsc_ipa_stats.$$.json"

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

cleanup() {
    if [[ -n "${MOCK_PID:-}" ]] && kill -0 "$MOCK_PID" 2>/dev/null; then
        kill "$MOCK_PID" 2>/dev/null || true
        wait "$MOCK_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

echo "[D1] Starting mock BSC IPA on $MOCK_HOST:$MOCK_PORT"
python3 "$MOCK_SCRIPT" --host "$MOCK_HOST" --port "$MOCK_PORT" --stats-file "$STATS_FILE" &
MOCK_PID=$!
sleep 1

echo "[D1] Checking REST availability"
api_get "/status" >/dev/null

health_before="$(api_get "/links/abis/health")"
oml_rx_before="$(extract_health_field "$health_before" "omlRxFrames")"
rsl_rx_before="$(extract_health_field "$health_before" "rslRxFrames")"
profile="$(extract_health_field "$health_before" "interopProfile")"

echo "before: interopProfile=$profile omlRx=$oml_rx_before rslRx=$rsl_rx_before"

echo "[D1] connect + inject OML/RSL"
api_post "/links/abis/connect"
api_post "/links/abis/inject" '{"procedure":"OML:OPSTART"}'
api_post "/links/abis/inject" '{"procedure":"RSL:CHANNEL_ACTIVATION","chanNr":3,"entity":3,"payload":[1,0,7]}'
api_post "/links/abis/inject" '{"procedure":"RSL:PAGING_CMD","chanNr":0,"entity":0,"payload":[33,67]}'
api_post "/links/abis/inject" '{"procedure":"RSL:CHANNEL_RELEASE","chanNr":3,"entity":3,"payload":[]}'

sleep "$WAIT_SEC"

health_after="$(api_get "/links/abis/health")"
oml_rx_after="$(extract_health_field "$health_after" "omlRxFrames")"
rsl_rx_after="$(extract_health_field "$health_after" "rslRxFrames")"
connected_after="$(python3 - <<'PY' <<<"$health_after"
import json, sys
obj = json.load(sys.stdin)
print("true" if obj.get("connected") else "false")
PY
)"

echo "after : connected=$connected_after omlRx=$oml_rx_after rslRx=$rsl_rx_after"

if [[ -n "${MOCK_PID:-}" ]] && kill -0 "$MOCK_PID" 2>/dev/null; then
    kill "$MOCK_PID" 2>/dev/null || true
    wait "$MOCK_PID" 2>/dev/null || true
fi

echo "[D1] mock stats file: $STATS_FILE"
cat "$STATS_FILE"

python3 - "$oml_rx_before" "$oml_rx_after" "$rsl_rx_before" "$rsl_rx_after" "$connected_after" <<'PY'
import sys

def to_i(v):
    try:
        return int(v)
    except Exception:
        return None

b_oml, a_oml, b_rsl, a_rsl = map(to_i, sys.argv[1:5])
connected = sys.argv[5].strip().lower() == "true"
if None in (b_oml, a_oml, b_rsl, a_rsl):
    print("result: WARN (health counters not available)")
    sys.exit(0)

d_oml = a_oml - b_oml
d_rsl = a_rsl - b_rsl
print(f"delta : omlRxFrames={d_oml}, rslRxFrames={d_rsl}")
if connected and d_oml >= 1 and d_rsl >= 1:
    print("result: OK (Option D1 mock interop baseline works)")
else:
    print("result: WARN (connectivity/counters did not increase as expected)")
PY
