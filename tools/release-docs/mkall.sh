#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
cd "${script_dir}"

function die() {
	echo $@ >&2
	exit 1
}

./mkversion.sh || die "-- failed to make version info"
./mkreadme.sh || die "-- failed to make readme"
./mklicenses.sh || die "-- failed to make licenses"
./mkdisclaimer.sh --src "%{}" || die "-- failed to make disclaimer"
