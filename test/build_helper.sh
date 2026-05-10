#!/bin/sh
#
# Build the test-only iommap_repro_helper NIF used by
# test/iommap_two_nif_tests.erl. Invoked from the eunit pre_hook in
# rebar.config under the test profile.
#
# This script is intentionally NOT shipped to hex (test/ is excluded
# from the package files list in src/iommap.app.src), so end users
# never run it.

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/test/c_src/iommap_repro_helper.c"
OUT="$ROOT/priv/iommap_repro_helper.so"

if [ ! -f "$SRC" ]; then
    echo "iommap repro helper source missing at $SRC; skipping helper build"
    exit 0
fi

ERLINC="$(erl -noshell -eval 'io:format("~s", [filename:join([code:root_dir(), "usr", "include"])]), halt().' 2>/dev/null)"
if [ -z "$ERLINC" ] || [ ! -f "$ERLINC/erl_nif.h" ]; then
    echo "could not locate erl_nif.h via erl; tried '$ERLINC'" >&2
    exit 1
fi

CC="${CC:-cc}"
CFLAGS="${CFLAGS:-} -fPIC -O2 -Wall -Wextra -I$ERLINC"

case "$(uname -s)" in
    Darwin)
        LDFLAGS="${LDFLAGS:-} -bundle -flat_namespace -undefined suppress -lpthread"
        ;;
    Linux)
        CFLAGS="$CFLAGS -D_GNU_SOURCE"
        LDFLAGS="${LDFLAGS:-} -shared -lpthread"
        ;;
    *)
        # FreeBSD, OpenBSD, NetBSD: -shared with libpthread.
        LDFLAGS="${LDFLAGS:-} -shared -lpthread"
        ;;
esac

mkdir -p "$ROOT/priv"
$CC $CFLAGS "$SRC" -o "$OUT" $LDFLAGS

echo "built $OUT"
