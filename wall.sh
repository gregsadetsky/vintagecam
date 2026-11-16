#!/bin/sh
# one recorder per connected camera, plus one grid window showing all of them.
# closing the grid window (or ctrl-c) stops every recorder cleanly.
# usage: ./wall.sh [DIR]     recordings go to DIR (default: recordings)
cd "$(dirname "$0")" || exit 1
dir=${1:-recordings}
mkdir -p "$dir" || exit 1
pids=""
n=0
stop() {
	[ -n "$pids" ] && kill -TERM $pids 2>/dev/null
	wait
}
trap stop EXIT
trap 'exit 0' INT TERM
for path in $(./vintagecam --list | awk '$2 ~ /:/ {print $1}'); do
	./vintagecam --device "$path" --record "$dir" 2>"$dir/recorder-usb$path.log" &
	pids="$pids $!"
	n=$((n + 1))
done
[ -n "$pids" ] || { echo "no supported camera connected"; exit 1; }
# grid shape for the number of cameras: 3x2 up to 6, 4x2 up to 8, then 4x3, 4x4
cols=3 rows=2
[ $n -gt 6 ] && cols=4
[ $n -gt 8 ] && rows=3
[ $n -gt 12 ] && rows=4
./vintagegrid "$dir" --cols $cols --rows $rows
