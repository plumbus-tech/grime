#!/usr/bin/env bash
# Build grime and run it on this machine with a safety net.
#
#   scripts/grime-try.sh                         # configs/default.json, auto-exit after 60 s
#   scripts/grime-try.sh my.json --timeout 300
#   scripts/grime-try.sh --dry-run               # don't grab the keyboard, just log
#   scripts/grime-try.sh --timeout 0             # no auto-exit (you know what you're doing)
#
# Get out at any time: hold Esc + Backspace (or all three mouse buttons) for 1
# second, or Ctrl-C this terminal
# from another one (pkill grime).
set -euo pipefail
cd "$(dirname "$0")/.."

config=configs/default.json
timeout=60
extra=()
while (($#)); do
	case $1 in
	-t | --timeout) timeout=$2; shift 2 ;;
	-n | --dry-run) extra+=(--dry-run); shift ;;
	-v | --verbose) extra+=(-v); shift ;;
	-h | --help) sed -n '2,11p' "$0"; exit 0 ;;
	*) config=$1; shift ;;
	esac
done

for pkg in json-c libcurl; do
	pkg-config --exists "$pkg" || { echo "missing dev package for $pkg (apt install libjson-c-dev libcurl4-openssl-dev)"; exit 1; }
done
make -s
./build/grime --check -c "$config"

# Reading /dev/input needs root or the 'input' group; writing /dev/uinput needs
# root or a udev rule. grime drops back to you once the devices are open.
run=()
if ! { id -nG | grep -qw input && [ -w /dev/uinput ]; }; then
	echo "(using sudo for /dev/input access — scripts/setup-permissions.sh makes this unnecessary)"
	# carry your environment through sudo (DISPLAY, DBus, tokens for http actions, ...)
	run=(sudo env)
	for name in $(compgen -e); do
		[[ $name == SUDO_* ]] || run+=("$name=${!name}")
	done
fi

if [[ " ${extra[*]} " == *" --dry-run "* ]]; then
	echo -e "\n  dry run: your keyboard keeps working; grime logs what it would do\n"
else
	cat <<MSG

  grime is taking over your keyboard with $config
  keys that aren't in the keymap will do NOTHING.
  emergency exit: hold Esc + Backspace, or all three mouse buttons, for 1 second
  $([ "$timeout" -gt 0 ] && echo "auto-exit in ${timeout}s")

MSG
fi
args=(-c "$config" "${extra[@]}")
[ "$timeout" -gt 0 ] && args+=(--timeout "$timeout")
exec "${run[@]}" ./build/grime "${args[@]}"
