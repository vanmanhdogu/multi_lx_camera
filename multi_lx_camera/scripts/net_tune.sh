#!/usr/bin/env bash
# Host network tuning for several GigE cameras on one NIC. Needs sudo.
# Settings are not persistent; see README.md to make them permanent.
#   sudo scripts/net_tune.sh [iface] [--jumbo]
set -euo pipefail
IFACE="${1:-enP2p1s0}"

# Socket receive buffers: 4 camera streams arrive in bursts on one socket set.
sysctl -w net.core.rmem_max=33554432
sysctl -w net.core.rmem_default=33554432
sysctl -w net.core.netdev_max_backlog=10000

# Larger NIC RX ring, if the driver allows it.
if command -v ethtool >/dev/null; then
  max_rx=$(ethtool -g "$IFACE" 2>/dev/null | awk '/Pre-set/{p=1} p&&/^RX:/{print $2; exit}')
  if [[ -n "${max_rx:-}" && "$max_rx" != "n/a" ]]; then
    ethtool -G "$IFACE" rx "$max_rx" || echo "could not set RX ring on $IFACE"
  fi
  ethtool "$IFACE" | grep -E 'Speed|Duplex|Link detected'
fi

# Jumbo frames only if every hop (router/switch ports and cameras) supports them.
if [[ "${2:-}" == "--jumbo" ]]; then
  ip link set "$IFACE" mtu 9000
fi
ip -br addr show "$IFACE"
