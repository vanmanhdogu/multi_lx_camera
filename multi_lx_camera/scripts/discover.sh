#!/usr/bin/env bash
# List every MRDVS camera on the network and flag duplicate IPs.
# Exit code 2 means at least two cameras share an IP.
set -euo pipefail
TOOLS="$(cd "$(dirname "$0")/../tools" && pwd)"
[[ -x "$TOOLS/lx_discover" ]] || "$TOOLS/build.sh"
exec "$TOOLS/lx_discover"
