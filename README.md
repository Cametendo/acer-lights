# predator-lights

A small Linux tool to control the case and fan lighting of an **Acer Predator
Orion 3000** desktop: a static colour, the factory RGB cycle, or off.

Developed and tested on a Predator **PO3-655** (BIOS 1.03) running CachyOS
(Arch-based), kernel 7.2.

```console
$ predator-lights off
$ predator-lights color ff0000
$ predator-lights default
```

> **Use at your own risk.** This talks to the BIOS of your machine through an
> interface Acer never documented. It has been tested on exactly one PC. It is
> not affiliated with or endorsed by Acer.

## How it works

The lighting is not a USB device, and no RGB program finds it. The LEDs hang
off a controller that only the BIOS talks to. On Windows, PredatorSense asks
the BIOS to change them through four methods of a WMI class called
`AcerGamingFunction` (GUID `7A4DDFE7-5B5D-40B4-8595-4408E0CC7F56`):

| Method | Name                   | Purpose                    |
|-------:|------------------------|----------------------------|
| 5      | `SetGamingLedBehavior` | effect and speed           |
| 6      | `GetGamingLedBehavior` | read the effect back       |
| 7      | `SetGamingRgbSetting`  | colour                     |
| 8      | `GetGamingRgbSetting`  | read the colour back       |

Linux has no way to call WMI methods from an ordinary program, so this project
has two parts:

- **`driver/predator_lights.c`**, a kernel module that binds to that WMI class
  and exposes one file per lighting area in sysfs. It calls only the four
  methods above. The same WMI class also carries fan, overclocking and system
  configuration methods; the module never calls those.
- **`predator-lights`**, a Python command-line tool that writes to those files.

The byte layout of the calls comes from the protocol notes of
[orion-unchained](https://github.com/s0t0h/orion-unchained), which
reverse-engineered PredatorSense on the Orion 7000. If you want a GUI, more
models or lighting presets, look there; this project is deliberately minimal.

## Requirements

- An Acer Predator Orion desktop whose firmware has the WMI class. Check with:

      ls /sys/bus/wmi/devices/ | grep -i 7A4DDFE7

- Kernel 7.0 or newer, with the headers for your kernel installed
  (`linux-headers`, or `linux-cachyos-headers` on CachyOS)
- `make`, plus `clang`/`llvm` if your kernel was built with clang (CachyOS is)
- Python 3
- Optional but recommended: `dkms`, so the module is rebuilt automatically on
  kernel updates

## Install

```sh
git clone <this repository>
cd acer-lights
make -C driver
sudo sh install.sh
```

The installer puts these on your system:

| What                                         | Where                                           |
|----------------------------------------------|-------------------------------------------------|
| Kernel module, loaded automatically at boot  | `/lib/modules/<kernel>/updates/` (or via DKMS)  |
| The `predator-lights` command                | `/usr/local/bin/predator-lights`                |
| udev rule, lets the `users` group write      | `/etc/udev/rules.d/70-predator-lights.rules`    |
| Boot service that re-applies the last state  | `/etc/systemd/system/predator-lights.service`   |
| The remembered state                         | `/var/lib/predator-lights/state`                |

Your user has to be in the `users` group to change the lights without `sudo`.

### Kernel updates

A kernel module only fits the kernel it was built for.

- **With `dkms` installed** before you run `install.sh`, the module is rebuilt
  for every installed and future kernel automatically.
- **Without `dkms`**, the module is installed for the running kernel only. After
  a kernel update the lights fall back to the BIOS default until you run
  `make -C driver && sudo sh install.sh` again.

## Usage

```sh
predator-lights off                   # all lights off
predator-lights color ff0000          # static colour, rrggbb
predator-lights color purple          # or a name: red green blue white yellow
                                      #   cyan magenta orange purple pink
predator-lights default               # the RGB cycle (aliases: rainbow, cycle)
predator-lights default -s 2          # ... with a speed from 0 to 9
predator-lights effect breathing blue # other effects the firmware knows
predator-lights status                # what the firmware currently reports
```

By default a command applies to everything. `-a N` limits it to one lighting
area; the PO3-655 reports areas 1 to 4, and `predator-lights status` lists
the ones your machine has:

```sh
predator-lights -a 2 color blue
```

The last setting is saved and applied again at boot.

Without the command-line tool, the module can be used directly:

```sh
cat /sys/bus/wmi/devices/7A4DDFE7-*/area0
echo "static 00ff00" > /sys/bus/wmi/devices/7A4DDFE7-*/area0
```

## Uninstall

```sh
sudo sh uninstall.sh
```

This removes everything listed in the table above, for every installed kernel.

It also works on a system that does not boot. From a live USB, mount your root
partition, copy or clone this repository into it, then:

```sh
arch-chroot /mnt
sh /path/to/acer-lights/uninstall.sh
```

Loading the module only reads from the firmware and it changes nothing by
itself, so if you just want it out of the way for one boot, add
`module_blacklist=predator_lights` to the kernel command line instead.

## What is known to work

Tested by eye on a PO3-655, BIOS 1.03:

| Feature          | Result                                                         |
|------------------|----------------------------------------------------------------|
| Static colour    | Works                                                          |
| RGB cycle        | Works. Speed 5 runs faster than the factory look.              |
| Off              | Works, see below                                               |
| Brightness       | Unknown. The firmware accepts a value but always reports 16.   |
| Other effects    | Untested                                                       |
| Single areas     | Untested. Which area is which fan or strip is not mapped yet.  |

Quirks of this firmware:

- Its own "off" mode lights everything up in the colour sent along with it, so
  `off` is sent as static black, which is fully dark.
- The firmware silently drops a change when its internal bus is busy and still
  reports success. The tool reads the state back and retries.
- Before the first change after power-on, the firmware reports every area as
  "off" although the factory RGB cycle is running.

## Other models

Other Predator Orion desktops (3000, 5000, 7000, X) very likely use the same
interface, but may differ in details. `collect-info.sh` gathers what is needed
to find out (WMI descriptions, ACPI tables, a scan of the motherboard SMBus)
into `dump/`:

```sh
sudo sh collect-info.sh
```

It only reads. The dump contains serial numbers, so look through it before
sharing it; `dump/` is in `.gitignore` for that reason.

If the 8-byte effect payload does nothing on your machine, newer firmware may
want the 16-byte one: `sudo modprobe predator_lights long_payload=1`.

## License

GPL-2.0-or-later, see [LICENSE](LICENSE).
