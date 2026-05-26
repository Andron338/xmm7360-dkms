# xmm7360-pci

Driver for the **Fibocom L850-GL / Intel XMM7360** LTE modem (PCI ID `8086:7360`),
with a C userspace RPC tool and full DKMS packaging for Arch Linux.

Confirmed working with this C port on the **Lenovo ThinkPad T14s (Intel)**
(Fibocom L850 / XMM7360) under Arch Linux, kernels 6.18-lts and 7.x.
See [DEVICES.md](DEVICES.md) for the full list of tested devices.

> ⚠️ _In development. No support provided. May not work, may crash your
> computer, may singe your jaffles._

![CI](https://github.com/Andron338/xmm7360-dkms/actions/workflows/tests.yaml/badge.svg)

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
git clone https://github.com/Andron338/xmm7360-dkms.git
cd xmm7360-dkms

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
make -C kernel
sudo make -C kernel load   # load without installing

# Build the RPC tool
make -C tool
```

### Install

```bash
# Pick any version string; it just has to match what you pass to dkms.
VER=1.0.0

# Install the kernel module source via DKMS. dkms.conf ships with a
# @VERSION@ placeholder that must be substituted (the Arch package does
# this automatically; do it by hand here).
sudo mkdir -p "/usr/src/xmm7360-$VER"
sudo cp kernel/xmm7360.c     "/usr/src/xmm7360-$VER/"
sudo cp kernel/Makefile.dkms "/usr/src/xmm7360-$VER/Makefile"
sudo sed "s/@VERSION@/$VER/" kernel/dkms.conf \
    | sudo tee "/usr/src/xmm7360-$VER/dkms.conf" >/dev/null
sudo dkms install "xmm7360/$VER"

# Install the RPC tool. NOTE: xmm7360-init.service runs
# /usr/bin/open_xdatachannel, so install it there (not /usr/local/bin).
sudo install -Dm755 tool/open_xdatachannel /usr/bin/open_xdatachannel
sudo install -Dm755 scripts/xmm7360-reset  /usr/bin/xmm7360-reset

# udev rule + iosm blacklist
sudo cp udev/80-xmm7360.rules      /usr/lib/udev/rules.d/
sudo cp conf/xmm7360-modprobe.conf /usr/lib/modprobe.d/xmm7360.conf

# systemd units: boot init, signal polling, presence watchdog, and the
# udev-triggered last-resort module reload.
sudo cp systemd/xmm7360-init.service     /usr/lib/systemd/system/
sudo cp systemd/xmm7360-signal.service   /usr/lib/systemd/system/
sudo cp systemd/xmm7360-rescan.service   /usr/lib/systemd/system/
sudo cp systemd/xmm7360-recovery.service /usr/lib/systemd/system/

# Suspend/hibernate hook — releases the modem's device nodes before the
# kernel unbinds the driver. Required for safe hibernation (see below).
sudo install -Dm755 scripts/xmm7360-sleep /usr/lib/systemd/system-sleep/xmm7360

# Default config (set your carrier's APN)
echo 'XMM_APN=internet' | sudo tee /etc/xmm7360.conf >/dev/null

# Activate. Do NOT 'enable' xmm7360-recovery.service: it has no [Install]
# section and is started on demand by udev.
sudo udevadm control --reload-rules
sudo systemctl daemon-reload
sudo systemctl enable xmm7360-init.service xmm7360-signal.service
sudo systemctl enable --now xmm7360-rescan.service

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
sudo pacman -R xmm7360-dkms-git    # Arch (this is the package name)
```

Or, to undo a manual install (use the same `$VER` you installed with):

```bash
sudo dkms remove "xmm7360/$VER" --all
sudo rm -f /usr/bin/open_xdatachannel /usr/bin/xmm7360-reset
sudo rm -f /usr/lib/systemd/system-sleep/xmm7360
sudo rm -f /usr/lib/udev/rules.d/80-xmm7360.rules
sudo rm -f /usr/lib/modprobe.d/xmm7360.conf
sudo rm -f /usr/lib/systemd/system/xmm7360-{init,signal,rescan,recovery}.service
sudo systemctl daemon-reload
sudo udevadm control --reload-rules
```

---

## Power management

Suspend/resume and hibernation are handled in the kernel module, with a small
userspace hook for safety:

- **Suspend (S3 / s2idle):** the driver's `.suspend`/`.resume` callbacks quiesce
  the data path on the way down. On resume it checks the PCIe link and the
  firmware status word: if the modem survived it re-attaches `wwan0`; if the
  platform cut power to the slot it triggers an in-kernel rebuild from scratch.
- **Hibernation (S4):** the driver unbinds itself before the hibernation image
  is written and re-probes cleanly afterwards, so no stale DMA/ring state is
  ever restored.
- **`xmm7360-sleep`** (installed to `/usr/lib/systemd/system-sleep/`) runs
  *before* the kernel unbinds the driver and releases the modem's device nodes
  (stops ModemManager, drops the connection, kills `pppd`/`open_xdatachannel`).
  This is required so that close() can't race the unbind during hibernation. It
  also restarts those services on resume.
- On resume/recovery the module emits a `udev` uevent (`XMM7360_STATE=resumed`
  / `recovered`) that re-runs `xmm7360-init.service`, re-scans ModemManager and
  restarts signal polling — so the modem comes back without manual steps.

If ModemManager ever drops the modem after a disconnect, the
`xmm7360-rescan.service` re-scans it automatically; `sudo xmm7360-reset`
forces a full module reload as a last resort.

---

## Project layout

```
xmm7360-dkms/
├── PKGBUILD / .SRCINFO / xmm7360-dkms.install   Arch packaging (flat for AUR)
├── kernel/    xmm7360.c, Makefile, Makefile.dkms, dkms.conf
├── tool/      open_xdatachannel.c, xmm_*.{c,h}, Makefile, man page
├── tests/     userspace unit tests + KUnit module
├── systemd/   init / signal / rescan / recovery services
├── udev/      80-xmm7360.rules
├── conf/      xmm7360-modprobe.conf (blacklists iosm)
├── scripts/   xmm7360-reset (manual recovery), xmm7360-sleep (system-sleep hook)
├── docs/      INSTALLING.md, DEVICES.md
└── .github/workflows/   CI/CD (see below)
```

### CI/CD workflows

| Workflow | Purpose |
|---|---|
| `tests.yaml` | unit tests (gcc/clang), ASan+UBSan, valgrind, KUnit build |
| `rpc-tool.yml` | tool build + tests + cppcheck |
| `kernel-module-ci.yaml` | build `.ko` against 5 kernels |
| `makepkg.yaml` | build + lint package, DKMS-build per kernel |
| `aur-publish.yaml` | auto-publish to the AUR on packaging changes |

---

## See also

- `man 8 open_xdatachannel`
- [ModemManager](https://www.freedesktop.org/wiki/Software/ModemManager/)
- [NetworkManager](https://networkmanager.dev/)
- [INSTALLING.md](INSTALLING.md)
- [DEVICES.md](DEVICES.md)
