#!/bin/bash
# lte.sh — manage xmm7360 modem (C port edition)
#
# Install: sudo ./scripts/lte.sh setup
# Use:     sudo lte up | down | remove

set -euo pipefail

# ── Paths ──────────────────────────────────────────────────────────────────
SCRIPT_PATH="$(readlink -f "$0")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"
PROJECT_DIR="$(readlink -f "$SCRIPT_DIR/..")"
CONF_FILE="$PROJECT_DIR/xmm7360.ini"
RPC_BIN="$PROJECT_DIR/open_xdatachannel"
LTE_LINK="/usr/local/bin/lte"
PIDFILE="/run/open_xdatachannel.pid"

# ── Usage ──────────────────────────────────────────────────────────────────
if [[ $# -eq 0 ]]; then
    echo "Usage: lte <up|down|setup|remove>"
    exit 1
fi

# ── Config file ────────────────────────────────────────────────────────────
if [[ ! -f "$CONF_FILE" ]]; then
    echo "No configuration file found. Create one from the sample:"
    echo "  cp \"${CONF_FILE}.sample\" \"$CONF_FILE\""
    exit 1
fi

# Parse "key = value" safely (handles spaces around = and # comments)
read_ini() {
    local key="$1"
    grep -E "^[[:space:]]*${key}[[:space:]]*=" "$CONF_FILE" \
        | head -1 \
        | sed -E 's/^[^=]+=[[:space:]]*//' \
        | sed 's/[[:space:]]*#.*//' \
        | tr -d '\r\n'
}

APN="$(read_ini apn)"
METRIC="$(read_ini metric)"
METRIC="${METRIC:-1000}"

# ── Privilege check ────────────────────────────────────────────────────────
if [[ $EUID -ne 0 ]]; then
    echo "Must be run as root — elevating via pkexec..."
    exec pkexec "$SCRIPT_PATH" "$@"
fi

# ── RPC device auto-detection ──────────────────────────────────────────────
# Prefer the custom xmm7360 module path; fall back to iosm path.
detect_rpc_device() {
    for dev in /dev/xmm0/rpc /dev/wwan0xmmrpc0; do
        if [[ -e "$dev" ]]; then
            echo "$dev"
            return 0
        fi
    done
    echo ""
}

echo "lte.sh — xmm7360 modem manager"
echo "  APN:    ${APN:-<not set>}"
echo "  Config: $CONF_FILE"
echo ""

# ══════════════════════════════════════════════════════════════════════════
case "$1" in

# ── setup ─────────────────────────────────────────────────────────────────
setup)
    cd "$PROJECT_DIR"

    # Build kernel module
    echo "Building kernel module..."
    make

    # Load kernel module
    echo "Loading kernel module..."
    make load

    # Build C RPC tool
    echo "Building open_xdatachannel..."
    if [[ -f "$PROJECT_DIR/rpc_c/Makefile" ]]; then
        make -C "$PROJECT_DIR/rpc_c"
        RPC_BIN="$PROJECT_DIR/rpc_c/open_xdatachannel"
    else
        echo "Warning: rpc_c/Makefile not found; skipping C tool build."
    fi

    # Symlink lte into PATH
    ln -sf "$SCRIPT_PATH" "$LTE_LINK"
    chmod 755 "$LTE_LINK"
    echo "Installed: lte → $LTE_LINK"
    ;;

# ── up ────────────────────────────────────────────────────────────────────
up)
    if [[ -z "$APN" ]]; then
        echo "Error: 'apn' not set in $CONF_FILE"
        exit 1
    fi

    # Kill any stale instance
    if [[ -f "$PIDFILE" ]]; then
        OLD_PID="$(cat "$PIDFILE")"
        if kill -0 "$OLD_PID" 2>/dev/null; then
            echo "Stopping previous instance (PID $OLD_PID)..."
            kill "$OLD_PID" || true
            sleep 1
        fi
        rm -f "$PIDFILE"
    fi

    RPC_DEV="$(detect_rpc_device)"
    if [[ -z "$RPC_DEV" ]]; then
        echo "Error: no RPC device found. Is the xmm7360 module loaded?"
        echo "  Try: make load   (from $PROJECT_DIR)"
        exit 1
    fi
    echo "Using RPC device: $RPC_DEV"

    if [[ ! -x "$RPC_BIN" ]]; then
        echo "Error: $RPC_BIN not found or not executable."
        echo "  Run: lte setup"
        exit 1
    fi

    # Build argument list
    ARGS=(--apn "$APN" --device "$RPC_DEV" --metric "$METRIC")
    NORESOLV="$(read_ini noresolv)"
    NOROUTE="$(read_ini nodefaultroute)"
    [[ "${NORESOLV:-0}" == "1" ]] && ARGS+=(--noresolv)
    [[ "${NOROUTE:-0}"  == "1" ]] && ARGS+=(--nodefaultroute)

    echo "Bringing wwan0 up..."
    # Run in background; our C tool handles interface config and DNS internally.
    "$RPC_BIN" "${ARGS[@]}" &
    echo $! > "$PIDFILE"
    echo "open_xdatachannel started (PID $(cat "$PIDFILE"))"
    echo "  Log:  journalctl -f (or check stderr if run interactively)"
    ;;

# ── down ──────────────────────────────────────────────────────────────────
down)
    echo "Taking wwan0 down..."

    # Stop the RPC tool first so it stops talking to the modem
    if [[ -f "$PIDFILE" ]]; then
        OLD_PID="$(cat "$PIDFILE")"
        if kill -0 "$OLD_PID" 2>/dev/null; then
            echo "Stopping open_xdatachannel (PID $OLD_PID)..."
            kill "$OLD_PID" || true
        fi
        rm -f "$PIDFILE"
    fi

    ip link set wwan0 down 2>/dev/null || true

    # Remove DNS entries we added
    if grep -q "Added by xmm7360" /etc/resolv.conf 2>/dev/null; then
        sed -i '/# Added by xmm7360/,/^$/d' /etc/resolv.conf
        echo "Removed DNS entries from /etc/resolv.conf"
    fi
    ;;

# ── remove ────────────────────────────────────────────────────────────────
remove)
    # Bring interface down cleanly first
    "$SCRIPT_PATH" down || true

    ip link set wwan0 down 2>/dev/null || true
    rmmod xmm7360 2>/dev/null || true
    rm -f "$LTE_LINK"
    echo "Removed module and lte symlink."
    ;;

*)
    echo "Unknown command: $1"
    echo "Usage: lte <up|down|setup|remove>"
    exit 1
    ;;

esac
