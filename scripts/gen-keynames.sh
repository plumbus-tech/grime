#!/usr/bin/env bash
# Regenerates src/engine/keynames_table.inc from the kernel's input-event-codes.h.
# Only needed when the kernel adds keys; the output is committed.
set -euo pipefail
hdr=${1:-/usr/include/linux/input-event-codes.h}
out=$(dirname "$0")/../src/engine/keynames_table.inc
awk '
/^#define[ \t]+KEY_[A-Z0-9_]+[ \t]+/ {
	name = $2; val = $3
	if (name ~ /^KEY_(RESERVED|MAX|CNT|MIN_INTERESTING)$/) next
	if (val ~ /^KEY_/) { if (!(val in v)) next; val = v[val] }
	v[name] = val
	printf "\t{\"%s\", %s},\n", tolower(substr(name, 5)), val
}' "$hdr" > "$out"
echo "wrote $(wc -l < "$out") keys to $out"
