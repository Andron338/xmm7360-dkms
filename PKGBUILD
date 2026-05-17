# Maintainer: Your Name <you@example.com>
#
# Place this PKGBUILD at the root of the xmm7360-pci repo alongside:
#   xmm7360.c          (the fixed kernel module)
#   dkms.conf
#   Makefile.dkms      (kernel module Makefile for DKMS)
#   Makefile.tool      (rpc tool Makefile)
#   80-xmm7360.rules
#   xmm7360-init.service
#   xmm7360-modprobe.conf
#   xmm7360-dkms.install
#   rpc/             (all .c/.h source files)
#
# Build and install:
#   makepkg -si

pkgname=xmm7360-dkms
pkgver=1.0.0
pkgrel=1
pkgdesc="Intel XMM7360 / Fibocom L850 LTE modem driver (DKMS) with RPC init tool"
arch=('x86_64')
url="https://github.com/xmm7360/xmm7360-pci"
license=('GPL2')

depends=(
    'dkms'
    'libnm'
    'openssl'
    'util-linux-libs'   # libuuid
    'networkmanager'
    'modemmanager'
)
makedepends=('linux-headers')
optdepends=('linux-headers: required by DKMS to build on kernel update')

provides=('xmm7360-dkms')
conflicts=('xmm7360' 'xmm7360-git' 'xmm7360-pci-dkms')
install="${pkgname}.install"

# No remote source — build from the local repo tree.
# makepkg must be run from the repo root.
source=()
sha256sums=()

build() {
    # ── RPC userspace tool ──────────────────────────────────────────────
    cd "$startdir/rpc"
    make -f "$startdir/Makefile.tool" clean
    make -f "$startdir/Makefile.tool"
}

package() {
    local _module="xmm7360"
    local _dkms_src="/usr/src/${_module}-${pkgver}"

    # ── Kernel module source for DKMS ───────────────────────────────────
    install -dm755 "${pkgdir}${_dkms_src}"
    install -m644 "$startdir/xmm7360.c"    "${pkgdir}${_dkms_src}/"
    install -m644 "$startdir/Makefile.dkms" "${pkgdir}${_dkms_src}/Makefile"
    install -m644 "$startdir/dkms.conf"    "${pkgdir}${_dkms_src}/"
    sed -i "s/@VERSION@/${pkgver}/" "${pkgdir}${_dkms_src}/dkms.conf"

    # ── RPC userspace tool ───────────────────────────────────────────────
    make -f "$startdir/Makefile.tool" \
        -C "$startdir/rpc" \
        DESTDIR="${pkgdir}" PREFIX=/usr install

    # ── udev rules ───────────────────────────────────────────────────────
    install -Dm644 "$startdir/80-xmm7360.rules" \
        "${pkgdir}/usr/lib/udev/rules.d/80-xmm7360.rules"

    # ── systemd service ──────────────────────────────────────────────────
    install -Dm644 "$startdir/xmm7360-init.service" \
        "${pkgdir}/usr/lib/systemd/system/xmm7360-init.service"

    # ── modprobe config (blacklist iosm) ─────────────────────────────────
    install -Dm644 "$startdir/xmm7360-modprobe.conf" \
        "${pkgdir}/usr/lib/modprobe.d/xmm7360.conf"

    # ── Default config file (only installed if /etc/xmm7360.ini absent) ─
    install -Dm644 /dev/null "${pkgdir}/etc/xmm7360.ini.example"
    cat > "${pkgdir}/etc/xmm7360.ini.example" << 'EOF'
# XMM7360 / Fibocom L850 configuration
# Copy to /etc/xmm7360.ini and set your APN:
#
# apn    = internet
# metric = 1000
EOF
}
