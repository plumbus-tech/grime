#!/usr/bin/env bash
# Regenerates src/engine/keynames_table.inc from the kernel's input-event-codes.h.
# Only needed when the kernel adds keys; the output is committed.
#
# KEY_* keep their bare lowercased name ("capslock"); BTN_* keep the prefix
# ("btn_left"), because unprefixed they would collide with 23 key names --
# BTN_LEFT vs KEY_LEFT, BTN_BACK vs KEY_BACK, BTN_A vs KEY_A, and so on.
#
# Seven BTN_* names are group headers that share a code with the useful
# spelling and come first in the header, so they would win grime_key_name()
# and become the canonical spelling. Skip them: BTN_MOUSE is not what anyone
# means by 0x110.
set -euo pipefail
hdr=${1:-/usr/include/linux/input-event-codes.h}
out=$(dirname "$0")/../src/engine/keynames_table.inc
awk '
/^#define[ \t]+(KEY|BTN)_[A-Z0-9_]+[ \t]+/ {
	name = $2; val = $3
	if (name ~ /^KEY_(RESERVED|MAX|CNT|MIN_INTERESTING)$/) next
	if (val ~ /^(KEY|BTN)_/) { if (!(val in v)) next; val = v[val] }
	v[name] = val   # record before the skip, so BTN_A -> BTN_SOUTH still resolves
	if (name ~ /^BTN_(MISC|MOUSE|JOYSTICK|GAMEPAD|DIGI|WHEEL|TRIGGER_HAPPY)$/) next
	printf "\t{\"%s\", %s},\n",
	       (name ~ /^BTN_/ ? "btn_" tolower(substr(name, 5)) : tolower(substr(name, 5))), val
}' "$hdr" > "$out"
echo "wrote $(wc -l < "$out") keys to $out"
