#!/bin/sh
set -eu

sed -n 's/^rust = { version = "\([^"]*\)".*/\1/p' "$(dirname "$0")/../mise.toml"
