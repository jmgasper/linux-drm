#!/bin/bash
# Turns call traces (compat/src/trace.c) and stacks (lx_haiku_log_stack())
# in the amdgpu_drm log into function names. Addresses outside the add-on
# (Haiku's kernel) stay numbers.
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
stack = re.compile(r"^stack of thread .* anchor 0x([0-9a-f]+):")
frame = re.compile(r"^  0x([0-9a-f]+)$")
loaded = None	# where the anchor was in memory, from the last stack header
def name(address):
	i = bisect.bisect_right(addresses, address) - 1
	if i < 0 or i == len(addresses) - 1:
		return hex(address)
	return "%s+%#x" % (names[i], address - addresses[i])
for line in sys.stdin:
	line = line.rstrip("\n")
	m = call.match(line)
	if m:
		address = (anchor + int(m.group(2), 16)) & 0xffffffffffffffff
		print("T%s %s" % (m.group(1), name(address).split("+")[0]))
		continue
	m = stack.match(line)
	if m:
		loaded = int(m.group(1), 16)
		print(line)
		continue
	m = frame.match(line)
	if m and loaded is not None:
		address = int(m.group(1), 16) - loaded + anchor
		if 0 <= address - addresses[0] < addresses[-1] - addresses[0]:
			print("  %s" % name(address))
		else:
			print(line + "  (kernel)")
		continue
	print(line)
' "$anchor" "$symbols"
