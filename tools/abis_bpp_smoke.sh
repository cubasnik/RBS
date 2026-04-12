#!/usr/bin/env bash
set -euo pipefail

BASE="${1:-http://127.0.0.1:8080/api/v1}"
WAIT_SEC="${2:-2}"

api_get() {
    local path="$1"
    curl -fsS "$BASE$path"
}

api_post() {
    local path="$1"
    curl -fsS -X POST "$BASE$path" -H "Content-Type: application/json" -d "${2:-{}}" >/dev/null
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

echo "[B++] REST smoke started"
echo "BASE=$BASE"

health_before="$(api_get "/links/abis/health")"
ka_enabled="$(extract_health_field "$health_before" "keepaliveEnabled")"
ka_tx_before="$(extract_health_field "$health_before" "keepaliveTxCount")"
ka_fail_before="$(extract_health_field "$health_before" "keepaliveFailCount")"
status_before="$(extract_health_field "$health_before" "healthStatus")"

echo "before: status=$status_before keepaliveEnabled=$ka_enabled tx=$ka_tx_before fail=$ka_fail_before"

echo "step: disconnect"
api_post "/links/abis/disconnect"

sleep 1

echo "step: connect"
api_post "/links/abis/connect"

echo "step: wait ${WAIT_SEC}s"
sleep "$WAIT_SEC"

health_after="$(api_get "/links/abis/health")"
ka_tx_after="$(extract_health_field "$health_after" "keepaliveTxCount")"
ka_fail_after="$(extract_health_field "$health_after" "keepaliveFailCount")"
status_after="$(extract_health_field "$health_after" "healthStatus")"
last_ka_tx="$(extract_health_field "$health_after" "lastKeepaliveTxEpochMs")"

echo "after : status=$status_after tx=$ka_tx_after fail=$ka_fail_after lastKeepaliveTxEpochMs=$last_ka_tx"

python3 - "$ka_tx_before" "$ka_tx_after" "$ka_fail_before" "$ka_fail_after" <<'PY'
import sys

def to_int(v):
    try:
        return int(v)
    except Exception:
        return None

b_tx, a_tx, b_fail, a_fail = map(to_int, sys.argv[1:5])
if None in (b_tx, a_tx, b_fail, a_fail):
    print("result: WARN (health counters not available)")
    sys.exit(0)

d_tx = a_tx - b_tx
d_fail = a_fail - b_fail
print(f"delta : keepaliveTxCount={d_tx}, keepaliveFailCount={d_fail}")
if d_tx >= 0 and d_fail >= 0:
    print("result: OK (B++ counters are readable after reconnect cycle)")
else:
    print("result: WARN (unexpected counter delta)")
PY
