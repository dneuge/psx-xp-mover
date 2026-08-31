#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
cd "${script_dir}"

function die() {
	echo $@ >&2
	exit 1
}

python3 "${script_dir}/mklicenses.py" $@
