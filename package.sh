#!/bin/bash
# Builds the submission archive <student_id>.zip containing
# Makefile, src/ and report.pdf (the executable is not included).
set -e
if [ $# -ne 1 ]; then
    echo "usage: $0 <student_id>" >&2
    exit 1
fi
id="$1"
cd "$(dirname "$0")"
[ -f report.pdf ] || { echo "report.pdf missing" >&2; exit 1; }
tmp=$(mktemp -d)
mkdir -p "$tmp/src"
cp Makefile report.pdf "$tmp/"
cp src/*.cpp src/*.hpp "$tmp/src/"
rm -f "$id.zip"
(cd "$tmp" && zip -q -r "$OLDPWD/$id.zip" Makefile src report.pdf)
rm -rf "$tmp"
echo "created $id.zip"
unzip -l "$id.zip"
