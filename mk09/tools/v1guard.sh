#!/bin/sh
# v1guard.sh - the 64KB builds (sbc09, FM-11 Level 1) must not change by the 2.0 work (docs/V2_PLAN.md, 2.1).
# Builds them again from scratch in a scratch directory and compares the SHA-256 of the outputs with tests/v1.sha256.
#   tools/v1guard.sh            compare (exit 1 when something differs)
#   tools/v1guard.sh --update   write tests/v1.sha256 (only when a 1.x change is intended, e.g. by the FM-11 session)
# Needs CMOC_BIN (export CMOC_BIN=$HOME/cmoc/bin PATH=$HOME/cmoc/bin:$PATH).
cd "$(dirname "$0")/.." || exit 2
REF=tests/v1.sha256
# the images carry time stamps: fix them (also the time zone) so that the checksums are reproducible
export MK09_FIXED_TIME=1767225600 TZ=UTC
# the build directories must be one level below mk09 (the Makefile uses ../lib)
TMP=.v1guard-$$
mkdir -p "$TMP" || exit 2
trap 'rm -rf "$TMP" .v1guard-$$-*' EXIT
OUT=$TMP/now.sha256
: > "$OUT"
# name|make arguments|output files
build() {
    name=$1; shift; files=$1; shift
    d=.v1guard-$$-$name
    if ! make -s B="$d" "$@" all >"$TMP/$name.log" 2>&1; then
        echo "v1guard: the build $name failed:"; tail -20 "$TMP/$name.log"; exit 2
    fi
    for f in $files; do (cd "$d" && sha256sum "$f" | sed "s|  |  $name/|") >> "$OUT"; done
}
build sbc09      "boot.img bootrom.bin mk09.img"        BOARD=sbc09
build fm11-l1    "boot.img ipl_hdd.bin mk09.img"        BOARD=fm11 LEVEL=1
build fm11-l1-2hd "boot.img ipl_2hd.bin mk09_2hd.img"   BOARD=fm11 LEVEL=1 BOOTDEV=2hd
if [ "$1" = "--update" ]; then cp "$OUT" "$REF"; echo "v1guard: $REF updated"; cat "$REF"; exit 0; fi
if diff "$REF" "$OUT" >"$TMP/diff"; then echo "v1guard: the 1.x builds are unchanged (OK)"; exit 0; fi
echo "v1guard: the 1.x builds CHANGED:"; cat "$TMP/diff"; exit 1
