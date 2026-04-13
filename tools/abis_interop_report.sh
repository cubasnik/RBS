#!/usr/bin/env bash
# Abis interop artifact report.
#
# Wraps abis_osmocom_interop_smoke.sh, saves timestamped directory with:
#   health_before.json  — Abis health snapshot before reconnect
#   health_after.json   — Abis health snapshot after traffic exchange
#   trace.json          — Full /links/abis/trace dump (limit 200)
#   links_before.json   — /links snapshot at start
#   status.json         — /status at end
#   run.log             — Full smoke stdout+stderr
#   summary.json        — Machine-readable run metadata and pass/fail
#
# Usage:
#   ./tools/abis_interop_report.sh \
#     [BASE] [EXPECTED_BSC_IP] [EXPECTED_BSC_PORT] [CONNECT_TIMEOUT_SEC] [TRAFFIC_WAIT_SEC] [ARTIFACT_ROOT]
#
# Example:
#   ./tools/abis_interop_report.sh \
#     http://10.10.10.2:8080/api/v1 10.10.10.1 3002 12 2 artifacts

set -uo pipefail

BASE="${1:-http://127.0.0.1:8080/api/v1}"
EXPECTED_BSC_IP="${2:-10.10.10.1}"
EXPECTED_BSC_PORT="${3:-3002}"
CONNECT_TIMEOUT_SEC="${4:-12}"
TRAFFIC_WAIT_SEC="${5:-2}"
ARTIFACT_ROOT="${6:-artifacts}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SMOKE_SCRIPT="$SCRIPT_DIR/abis_osmocom_interop_smoke.sh"

if [[ ! -x "$SMOKE_SCRIPT" ]]; then
    echo "ERROR: smoke script not found or not executable: $SMOKE_SCRIPT"
    exit 1
fi

TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
ARTIFACT_DIR="$ARTIFACT_ROOT/abis_interop_$TIMESTAMP"
mkdir -p "$ARTIFACT_DIR"

api_get() {
    curl -fsS "$BASE$1"
}

echo "[Report] artifact dir : $ARTIFACT_DIR"
echo "[Report] BASE          : $BASE"
echo "[Report] BSC endpoint  : ${EXPECTED_BSC_IP}:${EXPECTED_BSC_PORT}"

# ── pre-run snapshots ─────────────────────────────────────────────────────────
echo "[Report] snapshot: pre-run state"
api_get "/links/abis/health" > "$ARTIFACT_DIR/health_before.json" 2>/dev/null \
    || echo '{}' > "$ARTIFACT_DIR/health_before.json"
api_get "/links"              > "$ARTIFACT_DIR/links_before.json"  2>/dev/null \
    || echo '[]' > "$ARTIFACT_DIR/links_before.json"

# ── run smoke, tee output to log ──────────────────────────────────────────────
echo "[Report] running smoke ..."
echo ""
set +e
"$SMOKE_SCRIPT" \
    "$BASE" "$EXPECTED_BSC_IP" "$EXPECTED_BSC_PORT" \
    "$CONNECT_TIMEOUT_SEC" "$TRAFFIC_WAIT_SEC" \
    2>&1 | tee "$ARTIFACT_DIR/run.log"
SMOKE_EXIT="${PIPESTATUS[0]}"
set -e
echo ""

# ── post-run snapshots ────────────────────────────────────────────────────────
echo "[Report] snapshot: post-run state"
api_get "/links/abis/health"        > "$ARTIFACT_DIR/health_after.json"  2>/dev/null \
    || echo '{}' > "$ARTIFACT_DIR/health_after.json"
api_get "/links/abis/trace?limit=200" > "$ARTIFACT_DIR/trace.json"       2>/dev/null \
    || echo '{"messages":[]}' > "$ARTIFACT_DIR/trace.json"
api_get "/status"                   > "$ARTIFACT_DIR/status.json"         2>/dev/null \
    || echo '{}' > "$ARTIFACT_DIR/status.json"

# ── produce summary.json ──────────────────────────────────────────────────────
python3 - "$ARTIFACT_DIR" "$BASE" "$EXPECTED_BSC_IP" "$EXPECTED_BSC_PORT" \
          "$SMOKE_EXIT" "$TIMESTAMP" <<'PY'
import json
import sys
import os

artifact_dir, base, bsc_ip, bsc_port, exit_code_str, ts = sys.argv[1:]
exit_code = int(exit_code_str)

exit_messages = {
    0: "OK",
    2: "FAIL: abis link not registered in /links",
    3: "FAIL: peer address mismatch",
    4: "FAIL: Abis mode is not ipa_tcp",
    5: "FAIL: connect timeout",
    6: "FAIL: non-integer counter in health/trace response",
    7: "FAIL: RBS did not transmit expected OML/RSL frames",
    8: "FAIL: no inbound RX frames from external peer",
    9: "FAIL: trace has no RX messages",
}


def load_json(name):
    path = os.path.join(artifact_dir, name)
    try:
        with open(path) as f:
            return json.load(f)
    except Exception:
        return {}


def health_field(obj, key):
    try:
        return obj["health"][key]
    except (KeyError, TypeError):
        return None


health_b = load_json("health_before.json")
health_a = load_json("health_after.json")
trace_obj = load_json("trace.json")

msgs = trace_obj.get("messages", []) if isinstance(trace_obj, dict) else []
trace_tx = sum(1 for m in msgs if isinstance(m, dict) and m.get("tx", False))
trace_rx = sum(1 for m in msgs if isinstance(m, dict) and not m.get("tx", False))

summary = {
    "timestamp": ts,
    "base": base,
    "bsc_endpoint": f"{bsc_ip}:{bsc_port}",
    "result": exit_messages.get(exit_code, f"FAIL (exit {exit_code})"),
    "exit_code": exit_code,
    "passed": exit_code == 0,
    "counters": {
        "before": {
            "omlTx": health_field(health_b, "omlTxFrames"),
            "omlRx": health_field(health_b, "omlRxFrames"),
            "rslTx": health_field(health_b, "rslTxFrames"),
            "rslRx": health_field(health_b, "rslRxFrames"),
        },
        "after": {
            "omlTx": health_field(health_a, "omlTxFrames"),
            "omlRx": health_field(health_a, "omlRxFrames"),
            "rslTx": health_field(health_a, "rslTxFrames"),
            "rslRx": health_field(health_a, "rslRxFrames"),
        },
    },
    "trace_summary": {
        "tx": trace_tx,
        "rx": trace_rx,
        "total": len(msgs),
    },
    "interopProfile": health_field(health_a, "interopProfile"),
    "healthStatus": health_field(health_a, "healthStatus"),
    "artifacts": [
        "health_before.json",
        "health_after.json",
        "trace.json",
        "links_before.json",
        "status.json",
        "run.log",
        "summary.json",
    ],
}

out_path = os.path.join(artifact_dir, "summary.json")
with open(out_path, "w") as f:
    json.dump(summary, f, indent=2)

print(json.dumps(summary, indent=2))
PY

echo ""
echo "[Report] artifacts saved → $ARTIFACT_DIR"
exit "$SMOKE_EXIT"
