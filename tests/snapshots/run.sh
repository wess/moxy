#!/bin/bash
# Snapshot test runner for Moxy codegen
# Usage: tests/snapshots/run.sh [path/to/moxy]

MOXY="${1:-./build/debug/moxy}"
DIR="$(dirname "$0")"
PASS=0
FAIL=0
ERRORS=""

for mxy in "$DIR"/*.mxy; do
    expected="$mxy.expected"
    [ -f "$expected" ] || continue

    name=$(basename "$mxy")
    actual=$("$MOXY" "$mxy" 2>/dev/null)
    rc=$?

    if [ $rc -ne 0 ]; then
        echo "  FAIL $name (transpile error, exit $rc)"
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS\n  $name: transpile failed"
        continue
    fi

    expected_content=$(cat "$expected")
    if [ "$actual" = "$expected_content" ]; then
        echo "  ok   $name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL $name (output differs)"
        diff <(echo "$actual") "$expected" | head -20
        FAIL=$((FAIL + 1))
        ERRORS="$ERRORS\n  $name: output differs"
    fi
done

echo ""
echo "  $PASS passed, $FAIL failed ($((PASS + FAIL)) total)"

if [ $FAIL -gt 0 ]; then
    echo -e "\nFailures:$ERRORS"
    exit 1
fi
