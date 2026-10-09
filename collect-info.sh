#!/bin/sh
# Read-only hardware dump for working out how the Predator lighting is driven.
# Run as root: sudo sh collect-info.sh
# Copies the firmware's WMI descriptions and ACPI tables and lists the
# devices answering on the motherboard SMBus. Nothing is written to hardware.
set -u
out="$(dirname "$0")/dump"
mkdir -p "$out/acpi" "$out/bmof"

for d in /sys/bus/wmi/devices/*; do
    [ -r "$d/bmof" ] && cp "$d/bmof" "$out/bmof/$(basename "$d").bmof"
done

for t in /sys/firmware/acpi/tables/DSDT /sys/firmware/acpi/tables/SSDT*; do
    cp "$t" "$out/acpi/$(basename "$t").aml"
done

modprobe i2c-dev 2>/dev/null
for bus in /sys/bus/i2c/devices/i2c-*; do
    n="${bus##*-}"
    case "$(cat "$bus/name")" in
        *SMBus*) { echo "# $(cat "$bus/name")"; i2cdetect -y "$n"; } > "$out/smbus-$n.txt" 2>&1 ;;
    esac
done

dmidecode > "$out/dmidecode.txt" 2>&1
chown -R "${SUDO_UID:-0}:${SUDO_GID:-0}" "$out"
echo "Done, wrote $out"
