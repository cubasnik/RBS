#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "usage: ./tools/rbs_api.sh <url> [GET|POST|PUT|DELETE] [json-body]" >&2
    exit 1
fi

url="$1"
method="${2:-GET}"
body="${3:-}"

ps_escape() {
    local s="$1"
    s="${s//\'/\'\'}"
    printf '%s' "$s"
}

url_ps="$(ps_escape "$url")"
method_ps="$(ps_escape "$method")"
body_ps="$(ps_escape "$body")"

raw_json="$({
    powershell.exe -NoProfile -Command "\
        \$ProgressPreference = 'SilentlyContinue'; \
        \$url = '$url_ps'; \
        \$method = '$method_ps'; \
        \$body = '$body_ps'; \
        try { \
            if ([string]::IsNullOrEmpty(\$body)) { \
                \$resp = Invoke-RestMethod -Uri \$url -Method \$method; \
            } else { \
                \$resp = Invoke-RestMethod -Uri \$url -Method \$method -ContentType 'application/json' -Body \$body; \
            } \
            \$resp | ConvertTo-Json -Depth 10; \
        } catch { \
            if (\$_.ErrorDetails -and \$_.ErrorDetails.Message) { Write-Output \$_.ErrorDetails.Message; exit 1 }; \
            Write-Error \$_; exit 1; \
        }\
    " | tr -d '\r'
} 2>&1)"

python3 - "$url" "$method" "$raw_json" <<'PY'
import json
import sys

url = sys.argv[1]
method = sys.argv[2]
raw = sys.argv[3].strip()

RESET = "\033[0m"
DIM = "\033[38;5;245m"
KEY = "\033[38;5;153m"
STRING = "\033[38;5;221m"
NUMBER = "\033[38;5;114m"
BOOL = "\033[38;5;213m"
ERR = "\033[38;5;203m"
HDR = "\033[38;5;81m"

def dump(obj, indent=0):
    pad = "  " * indent
    if isinstance(obj, dict):
        if not obj:
            return "{}"
        parts = ["{"]
        items = list(obj.items())
        for index, (key, value) in enumerate(items):
            suffix = "," if index + 1 < len(items) else ""
            parts.append(f"{pad}  {KEY}\"{key}\"{RESET}: {dump(value, indent + 1)}{suffix}")
        parts.append(f"{pad}}}")
        return "\n".join(parts)
    if isinstance(obj, list):
        if not obj:
            return "[]"
        parts = ["["]
        for index, value in enumerate(obj):
            suffix = "," if index + 1 < len(obj) else ""
            parts.append(f"{pad}  {dump(value, indent + 1)}{suffix}")
        parts.append(f"{pad}]")
        return "\n".join(parts)
    if isinstance(obj, str):
        return f'{STRING}"{obj}"{RESET}'
    if isinstance(obj, bool):
        return f'{BOOL}{str(obj).lower()}{RESET}'
    if obj is None:
        return f'{DIM}null{RESET}'
    return f'{NUMBER}{obj}{RESET}'

print(f"{HDR}{method} {url}{RESET}")

try:
    data = json.loads(raw)
except Exception:
    print(f"{ERR}{raw}{RESET}")
    sys.exit(0)

if isinstance(data, dict) and set(data.keys()) == {"value", "Count"} and isinstance(data["value"], list):
    data = data["value"]

print(dump(data))
PY