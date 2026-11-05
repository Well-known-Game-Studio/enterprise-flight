#!/bin/zsh
# Run the Slonk automation tests headless (no window, no GPU) and print the results.
#
#   tools/run_tests.sh              # every test under "Slonk."
#   tools/run_tests.sh Slonk.Fleet  # a prefix
#
# Needs a built editor module (Build.sh SlonkEditor Mac Development, or a running editor's build). Runs a second
# editor process, so it works alongside an open editor; the MCP plugin is told to use another port.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
PROJ="$HERE/../unreal/Slonk.uproject"
UE="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
CMD="$UE/Engine/Binaries/Mac/UnrealEditor-Cmd"
FILTER="${1:-Slonk.}"
OUT="${TEST_REPORT_DIR:-/tmp/slonk-tests}"
mkdir -p "$OUT"
# A stale SDKROOT in the shell breaks the editor's Metal toolchain lookups.
unset SDKROOT
"$CMD" "$PROJ" -unattended -nopause -nosplash -nullrhi -NoSound -log \
  -ModelContextProtocolPort=8099 \
  -ExecCmds="Automation RunTests $FILTER; Quit" \
  -ReportExportPath="$OUT" -TestExit="Automation Test Queue Empty" \
  > "$OUT/run.log" 2>&1 || true
python3 - "$OUT" <<'EOF'
import json, os, sys
out = sys.argv[1]
rep = os.path.join(out, "index.json")
if not os.path.exists(rep):
    print("No report written; see", os.path.join(out, "run.log")); sys.exit(2)
r = json.load(open(rep, encoding="utf-8-sig"))
fails = 0
for t in r.get("tests", []):
    ok = t.get("state") == "Success"
    fails += 0 if ok else 1
    print(("PASS " if ok else "FAIL ") + t.get("fullTestPath", "?"))
    if not ok:
        for e in t.get("entries", []):
            if e.get("event", {}).get("type") in ("Error", "Warning"):
                print("     ", e["event"].get("message", "")[:200])
print(f"{r.get('succeeded', 0)} passed, {r.get('failed', 0)} failed")
sys.exit(1 if fails else 0)
EOF
