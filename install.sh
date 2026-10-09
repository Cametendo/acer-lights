#!/bin/sh
# Install the predator_lights module, its udev rule and the predator-lights
# command. Run as root: sudo sh install.sh   (sudo sh uninstall.sh undoes it)
set -eu
cd "$(dirname "$0")"
ver=0.1
kver="$(uname -r)"

if [ "${1:-}" = remove ]; then
    exec sh ./uninstall.sh
fi

rmmod predator_lights 2>/dev/null || true
if command -v dkms >/dev/null; then
    # DKMS rebuilds the module automatically on every kernel update.
    dkms remove "predator-lights/$ver" --all 2>/dev/null || true
    rm -rf "/usr/src/predator-lights-$ver"
    mkdir -p "/usr/src/predator-lights-$ver"
    cp driver/predator_lights.c driver/Makefile driver/dkms.conf "/usr/src/predator-lights-$ver/"
    dkms install "predator-lights/$ver"
else
    [ -f driver/predator_lights.ko ] || { echo "Run 'make -C driver' as your user first."; exit 1; }
    install -Dm644 driver/predator_lights.ko "/lib/modules/$kver/updates/predator_lights.ko"
    depmod -a
    echo "Note: dkms is not installed, so after a kernel update run 'make -C driver' and this script again."
fi

install -Dm644 70-predator-lights.rules /etc/udev/rules.d/70-predator-lights.rules
install -Dm755 predator-lights /usr/local/bin/predator-lights
install -Dm644 predator-lights.service /etc/systemd/system/predator-lights.service
install -d -m 775 -g users /var/lib/predator-lights
udevadm control --reload
modprobe predator_lights
systemctl daemon-reload
systemctl enable predator-lights.service
echo "Installed. Try: predator-lights status"
