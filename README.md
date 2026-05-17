# xmm7360-pci

Driver for the **Fibocom L850-GL / Intel XMM7360** LTE modem (PCI ID `8086:7360`),
with a C userspace RPC tool and full DKMS packaging for Arch Linux.

See [DEVICES.md](DEVICES.md) for a list of tested devices.

> ⚠️ _In development. No support provided. May not work, may crash your
> computer, may singe your jaffles._

![CI](https://github.com/xmm7360/xmm7360-pci/workflows/CI/badge.svg)

---

## How it works

The modem sits on the PCIe bus and exposes two interfaces:

| Interface | Purpose |
|---|---|
| `/dev/xmm0/rpc` | RPC channel — modem initialisation, attach, signal |
| `wwan0` | Network interface — IP data via kernel MUX layer |
| `ttyXMM1`, `ttyXMM2` | AT command ports — used by ModemManager |

`open_xdatachannel` talks to the RPC channel to wake the modem, unlock
FCC, enable RF, open the SIM subsystem, and enable signal reporting.
ModemManager then uses the AT ports for everything else (registration,
signal strength, connection management), and NetworkManager handles the
IP connection.

---

## Installation (Arch Linux — recommended)

```bash
# Clone the repo
git clone https://github.com/Andron338/xmm7360-pci.git
cd xmm7360-pci

# Build and install via pacman (handles DKMS, udev, systemd)
makepkg -si

# Reboot — iosm is blacklisted, xmm7360 loads automatically
reboot
```

After reboot the modem initialises on its own:

1. `xmm7360` module loads and creates `wwan0`, `ttyXMM1`, `ttyXMM2`
2. udev triggers `xmm7360-init.service`
3. `open_xdatachannel --init-only` wakes the modem over the RPC channel
4. ModemManager detects the SIM via the Fibocom plugin
5. Connect using NetworkManager GUI or `nmcli`

On every kernel update DKMS rebuilds the module automatically — no
manual steps needed.

---

## Manual installation (other distros)

### Dependencies

- `gcc`, `make`, `linux-headers`
- `libnm` (NetworkManager GLib library)
- `openssl`
- `libuuid`

### Build

```bash
# Build the kernel module
make
sudo make load          # load without installing

# Build the RPC tool
cd rpc
make -f ../Makefile.tool
cd ..
```

### Install

```bash
# Install kernel module via DKMS
sudo cp xmm7360.c /usr/src/xmm7360-1.0.0/
sudo cp Makefile.dkms /usr/src/xmm7360-1.0.0/Makefile
sudo cp dkms.conf /usr/src/xmm7360-1.0.0/
sudo dkms install xmm7360/1.0.0

# Install RPC tool
sudo install -m755 rpc/open_xdatachannel /usr/local/bin/

# Install udev rule and systemd service
sudo cp 80-xmm7360.rules /usr/lib/udev/rules.d/
sudo cp xmm7360-init.service /usr/lib/systemd/system/
sudo cp xmm7360-modprobe.conf /usr/lib/modprobe.d/xmm7360.conf

# Enable init service
sudo systemctl enable xmm7360-init.service

# Reboot
sudo reboot
```

---

## Usage

### Automatic (after install)

The `xmm7360-init.service` runs automatically on boot. Connect via
NetworkManager GUI or:

```bash
# Create a connection (first time only)
nmcli connection add type gsm ifname '*' apn your.apn.here con-name lte

# Connect
nmcli connection up lte
```

### Manual

```bash
# Wake modem only (let ModemManager handle the rest)
sudo open_xdatachannel --init-only

# Full manual setup (bypasses ModemManager — use with iosm module)
sudo open_xdatachannel --apn your.apn.here
```

### SIM PIN

If your SIM has a PIN enabled, unlock it via the AT port before or
after `--init-only`:

```bash
echo 'AT+CPIN="0000"' | sudo tee /dev/ttyXMM1
```

Replace `0000` with your PIN.

---

## Signal strength

Signal reporting requires the Fibocom ModemManager plugin. Check it is
being used:

```bash
mmcli -m 0 | grep plugin
# should show: plugin: fibocom
```

Then enable polling:

```bash
mmcli -m 0 --signal-setup=5
mmcli -m 0 --signal-get
```

If the plugin shows `generic` instead of `fibocom`, the udev rule in
`80-xmm7360.rules` needs to be reloaded:

```bash
sudo udevadm control --reload-rules
sudo systemctl restart ModemManager
```

---

## Uninstall

```bash
sudo pacman -R xmm7360-dkms   # Arch
# or
sudo dkms remove xmm7360/1.0.0 --all
sudo rm /usr/local/bin/open_xdatachannel
sudo rm /usr/lib/udev/rules.d/80-xmm7360.rules
sudo rm /usr/lib/systemd/system/xmm7360-init.service
sudo rm /usr/lib/modprobe.d/xmm7360.conf
```

---

## Power management

Power management is not yet supported. The modem loses its state on
suspend and must be reinitialised on resume. As a workaround, add a
systemd sleep hook:

```bash
# /usr/lib/systemd/system-sleep/xmm7360-resume.sh
#!/bin/bash
[ "$1" = "post" ] && systemctl restart xmm7360-init.service
```

```bash
sudo chmod +x /usr/lib/systemd/system-sleep/xmm7360-resume.sh
```

---

## Files

| File | Purpose |
|---|---|
| `xmm7360.c` | Kernel module source |
| `Makefile` | Manual kernel module build |
| `Makefile.dkms` | Kernel module Makefile for DKMS |
| `Makefile.tool` | C tool build |
| `dkms.conf` | DKMS configuration |
| `PKGBUILD` | Arch Linux package |
| `xmm7360-init.service` | systemd init service |
| `80-xmm7360.rules` | udev rules |
| `xmm7360-modprobe.conf` | Blacklists iosm |
| `xmm7360-dkms.install` | Arch post-install hooks |
| `rpc/` | Userspace RPC tool source |

---

## See also

- `man 8 open_xdatachannel`
- [ModemManager](https://www.freedesktop.org/wiki/Software/ModemManager/)
- [NetworkManager](https://networkmanager.dev/)
- [INSTALLING.md](INSTALLING.md)
- [DEVICES.md](DEVICES.md)
