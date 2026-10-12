#!/bin/bash
# Turns a call trace from the amdgpu_drm log (see compat/src/trace.c) into
# function names.
#
#   haiku/tools/symbolize-trace.sh <add-on with symbols> < log > trace
set -euo pipefail
ADDON=$1
NM=${NM:-nm}
symbols=$(mktemp)
trap 'rm -f "$symbols"' EXIT
anchor=$($NM "$ADDON" | awk '$3 == "__cyg_profile_func_enter" { print $1 }')
$NM -n "$ADDON" | awk '$2 ~ /^[tTwW]$/ { print $1, $3 }' > "$symbols"
python3 -c '
import bisect, re, sys
anchor = int(sys.argv[1], 16)
addresses, names = [], []
for line in open(sys.argv[2]):
	a, n = line.split()
	addresses.append(int(a, 16))
	names.append(n)
call = re.compile(r"^T(\d+) ([0-9a-f]+)$")
for line in sys.stdin:
	line = line.rstrip("\n")
	m = call.match(line)
	if not m:
		print(line)
		continue
	address = (anchor + int(m.group(2), 16)) & 0xffffffffffffffff
	i = bisect.bisect_right(addresses, address) - 1
	print("T%s %s" % (m.group(1), names[i] if i >= 0 else hex(address)))
' "$anchor" "$symbols"
