#!/usr/bin/env bash
# One-time: let your user run grime without sudo.
#  - adds you to the 'input' group (read /dev/input/event*)
#  - installs a udev rule so the 'input' group can write /dev/uinput
# Log out and back in afterwards for the group change to apply.
set -euo pipefail
rule=/etc/udev/rules.d/60-grime-uinput.rules
if [ -w /dev/uinput ]; then
	echo "/dev/uinput already writable, skipping udev rule"
else
	echo 'KERNEL=="uinput", GROUP="input", MODE="0660", OPTIONS+="static_node=uinput"' | sudo tee "$rule" >/dev/null
	sudo udevadm control --reload-rules
	sudo udevadm trigger --name-match=uinput
fi
sudo usermod -aG input "$USER"
echo "done — log out and back in, then scripts/grime-try.sh runs without sudo"
