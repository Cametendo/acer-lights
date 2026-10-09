#!/bin/sh
# Remove everything install.sh put on the system. Run as root: sudo sh uninstall.sh
#
# Also works from a chroot (e.g. booted from a live USB): it does not depend on
# the running kernel or a running systemd, and removes the module for every
# installed kernel.
set -u
ver=0.1

if [ "$(id -u)" != 0 ]; then
    echo "Run this as root: sudo sh $0"
    exit 1
fi

# Unload the module and stop the service where that is possible (not in a chroot).
rmmod predator_lights 2>/dev/null
systemctl disable predator-lights.service 2>/dev/null
rm -f /etc/systemd/system/multi-user.target.wants/predator-lights.service \
      /etc/systemd/system/predator-lights.service

if command -v dkms >/dev/null 2>&1; then
    dkms remove "predator-lights/$ver" --all 2>/dev/null
fi
rm -rf /usr/src/predator-lights-* /var/lib/dkms/predator-lights

for dir in /lib/modules/*/; do
    found=$(find "$dir" -name 'predator_lights.ko*' 2>/dev/null)
    [ -n "$found" ] || continue
    echo "$found" | xargs rm -f
    depmod -a "$(basename "$dir")" 2>/dev/null
done

rm -f /etc/udev/rules.d/70-predator-lights.rules /usr/local/bin/predator-lights
rm -rf /var/lib/predator-lights

udevadm control --reload 2>/dev/null
systemctl daemon-reload 2>/dev/null

echo "predator-lights removed. The lights stay as they are until the next power-off."
