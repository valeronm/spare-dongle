#!/usr/bin/env bash
# usage: build-firmware.sh <ncs-workspace> <build-dir>, with BOARD set in the environment
set -euo pipefail

app=$(cd "$(dirname "$0")/.." && pwd)
case $2 in /*) build=$2 ;; *) build=$PWD/$2 ;; esac
# west finds Zephyr and the NCS modules from the workspace it runs in.
cd "$1"
# Each patch names its fix; one that applies in neither direction stops the build.
for patch in "$app"/patches/zephyr/*.patch; do
	git -C zephyr apply --reverse --check "$patch" 2>/dev/null || git -C zephyr apply "$patch"
done
west build --no-sysbuild -d "$build" "$app"
