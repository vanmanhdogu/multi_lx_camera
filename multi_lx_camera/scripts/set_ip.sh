#!/usr/bin/env bash
# Give the camera with MAC <mac> a new static IP. Power-cycle it afterwards.
#   scripts/set_ip.sh 02:3d:f9:61:85:07 192.168.100.101 [255.255.255.0] [gateway]
set -euo pipefail
TOOLS="$(cd "$(dirname "$0")/../tools" && pwd)"
[[ -x "$TOOLS/lx_setip" ]] || "$TOOLS/build.sh"
exec "$TOOLS/lx_setip" "$@"
