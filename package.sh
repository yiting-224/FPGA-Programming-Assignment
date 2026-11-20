#!/bin/bash
# Builds the submission archive <student_id>.zip containing
# Makefile, src/ and report.pdf (the executable is not included).
# Usage: ./package.sh [student_id] [zh]   (student_id defaults to 115062556)
#   zh: submit the Chinese report (report_zh.pdf) as report.pdf
set -e
if [ $# -gt 2 ]; then
    echo "usage: $0 [student_id] [zh]" >&2
    exit 1
fi
id="${1:-115062556}"
cd "$(dirname "$0")"
rep=report.pdf
[ "${2:-}" = "zh" ] && rep=report_zh.pdf
[ -f "$rep" ] || { echo "$rep missing" >&2; exit 1; }
tmp=$(mktemp -d)
mkdir -p "$tmp/src"
cp Makefile "$tmp/"
cp "$rep" "$tmp/report.pdf"
cp src/*.cpp src/*.hpp "$tmp/src/"
rm -f "$id.zip"
(cd "$tmp" && zip -q -r "$OLDPWD/$id.zip" Makefile src report.pdf)
rm -rf "$tmp"
echo "created $id.zip"
unzip -l "$id.zip"
