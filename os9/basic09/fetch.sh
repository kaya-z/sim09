#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
# Fetch the BASIC09 / RunB sources (Microware, Motorola) into os9/basic09/microware/.  They are not part of this
# repository (no license is given for them upstream: see LICENSES.md 4.3).  Needs git and network access.
#
#   os9/basic09/fetch.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
URL=https://github.com/nitros9project/nitros9-languages.git
REV=c05f23c3ea4f934c54b185035e98eae34e73565b        # the version the modules of this repository were built from
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
cd "$T"
git init -q . && git remote add origin "$URL"
git fetch -q --depth 1 origin "$REV" && git checkout -q FETCH_HEAD
[ "$(git rev-parse HEAD)" = "$REV" ] || { echo "not at $REV"; exit 1; }
rm -rf "$HERE/microware"
cp -r basic09/microware "$HERE/microware"
cp basic09/ReadMe2 "$HERE/ReadMe2"
echo "fetched basic09/microware ($REV) into $HERE/microware"
