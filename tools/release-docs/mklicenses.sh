#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
cd "${script_dir}"

root_dir="$(realpath "${script_dir}/../..")"
out_dir="$(realpath "${root_dir}/release")"

function die() {
	echo $@ >&2
	exit 1
}

mkdir -p "${out_dir}" || die "failed to create output directory: ${out_dir}"

python3 "${script_dir}/mklicenses.py" $@
