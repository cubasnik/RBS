#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "usage: ./tools/rbs_api.sh <url> [GET|POST|PUT|DELETE] [json-body]" >&2
    exit 1
fi

url="$1"
method="${2:-GET}"
body="${3:-}"

# Use curl directly — works in both native Linux and WSL2 mirrored-networking mode.
if [[ -n "$body" ]]; then
    raw_json="$(curl -s -X "$method" \
        -H "Content-Type: application/json" \
        -d "$body" \
        --connect-timeout 5 \
        "$url" 2>&1)" || true
else
    raw_json="$(curl -s -X "$method" \
        --connect-timeout 5 \
        "$url" 2>&1)" || true
fi

if [[ -z "$raw_json" ]]; then
    echo -e "\033[38;5;203mError: no response from $url — is rbs_node running?\033[0m" >&2
    exit 1
fi

python3 - "$url" "$method" "$raw_json" <<'PY'
import json, sys, re, datetime

url    = sys.argv[1]
method = sys.argv[2]
raw    = sys.argv[3].strip()

# ── palette ──────────────────────────────────────────────────────────────────
R      = "\033[0m"
BRACE  = "\033[38;5;244m"      # { } [ ]  — dark gray
COMMA  = "\033[38;5;240m"      # ,
COLON  = "\033[38;5;244m"      # :
KEY    = "\033[38;5;117m"      # keys      — sky blue
STR    = "\033[38;5;185m"      # strings   — warm yellow
NUM    = "\033[38;5;120m"      # numbers   — mint green
BOOL_T = "\033[38;5;83m"       # true      — bright green
BOOL_F = "\033[38;5;209m"      # false     — salmon
NIL    = "\033[38;5;245m"      # null      — mid gray
ERR    = "\033[38;5;203m"      # error     — red
INDENT = "  "

# method badge colours
METHOD_COLOUR = {
    "GET":    "\033[48;5;22m\033[97m",   # dark green bg
    "POST":   "\033[48;5;20m\033[97m",   # dark blue bg
    "PUT":    "\033[48;5;94m\033[97m",   # brown bg
    "DELETE": "\033[48;5;88m\033[97m",   # dark red bg
}

# special-value highlighter for strings
_IP_RE  = re.compile(r'^\d{1,3}(\.\d{1,3}){3}(:\d+)?$')
_VER_RE = re.compile(r'^\d+\.\d+(\.\d+)*$')
_RAT_RE = re.compile(r'^(GSM|UMTS|LTE|NR|ENDC)$')

RAT_COLOUR = {
    "GSM":  "\033[38;5;76m",
    "UMTS": "\033[38;5;44m",
    "LTE":  "\033[38;5;69m",
    "NR":   "\033[38;5;177m",
    "ENDC": "\033[38;5;213m",
}
IP_COL  = "\033[38;5;222m"   # pale gold  — IPs / addresses
VER_COL = "\033[38;5;156m"   # pale green — version strings

def str_colour(s):
    if _RAT_RE.match(s):
        return RAT_COLOUR.get(s, STR)
    if _IP_RE.match(s):
        return IP_COL
    if _VER_RE.match(s):
        return VER_COL
    return STR

# known keys whose value has semantic meaning
_STATE_COLOURS = {
    "UNLOCKED": "\033[38;5;83m",
    "LOCKED":   "\033[38;5;203m",
    "SHUTDOWN": "\033[38;5;209m",
}
_SEMANTIC_KEYS = {"nodeState", "state", "status"}

def val_colour(key, val):
    if isinstance(key, str) and key in _SEMANTIC_KEYS and isinstance(val, str):
        return _STATE_COLOURS.get(val, str_colour(val))
    return str_colour(val) if isinstance(val, str) else None

def dump(obj, indent=0, key=None):
    pad = INDENT * indent
    pad1 = INDENT * (indent + 1)

    if isinstance(obj, dict):
        if not obj:
            return f"{BRACE}{{}}{R}"
        lines = [f"{BRACE}{{{R}"]
        items = list(obj.items())
        for i, (k, v) in enumerate(items):
            sep = f"{COMMA},{R}" if i < len(items) - 1 else ""
            lines.append(
                f"{pad1}{KEY}\"{k}\"{R}{COLON}:{R} {dump(v, indent+1, key=k)}{sep}"
            )
        lines.append(f"{pad}{BRACE}}}{R}")
        return "\n".join(lines)

    if isinstance(obj, list):
        if not obj:
            return f"{BRACE}[]{R}"
        lines = [f"{BRACE}[{R}"]
        for i, v in enumerate(obj):
            sep = f"{COMMA},{R}" if i < len(obj) - 1 else ""
            lines.append(f"{pad1}{dump(v, indent+1)}{sep}")
        lines.append(f"{pad}{BRACE}]{R}")
        return "\n".join(lines)

    if isinstance(obj, bool):
        c = BOOL_T if obj else BOOL_F
        return f"{c}{str(obj).lower()}{R}"
    if obj is None:
        return f"{NIL}null{R}"
    if isinstance(obj, (int, float)):
        return f"{NUM}{obj}{R}"
    if isinstance(obj, str):
        c = val_colour(key, obj)
        return f"{c}\"{obj}\"{R}"
    return f"{STR}\"{obj}\"{R}"

# ── header line ──────────────────────────────────────────────────────────────
mc   = METHOD_COLOUR.get(method.upper(), "\033[7m")
host = re.sub(r'^https?://', '', url)
ts   = datetime.datetime.now().strftime("%H:%M:%S")
print(f"\033[38;5;240m{ts}{R}  {mc} {method} {R}  \033[38;5;255m{host}{R}")
print(f"\033[38;5;237m{'─' * 60}{R}")

# ── parse & render ────────────────────────────────────────────────────────────
try:
    data = json.loads(raw)
except Exception:
    # try to detect curl error message
    print(f"{ERR}{raw}{R}")
    sys.exit(1)

# unwrap PowerShell Invoke-RestMethod envelope if present
if (isinstance(data, dict)
        and set(data.keys()) == {"value", "Count"}
        and isinstance(data["value"], list)):
    data = data["value"]

print(dump(data))
print(f"\033[38;5;237m{'─' * 60}{R}")
PY