#!/bin/sh
# Launches OpenMW with frame generation preloaded from this directory.
# Usage:
#   ./openmw-fg.sh                     # 'openmw' from PATH
#   ./openmw-fg.sh /path/to/openmw     # explicit binary (extra args pass through)
DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="openmw"
if [ $# -gt 0 ]; then
    BIN="$1"
    shift
fi
exec env LD_PRELOAD="$DIR/libopenmw_fg.so${LD_PRELOAD:+:$LD_PRELOAD}" "$BIN" "$@"
