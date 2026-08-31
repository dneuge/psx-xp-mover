#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
cd "${script_dir}"

root_dir="$(realpath "${script_dir}/../..")"

template_path="${root_dir}/dist/README.template.txt"
out_dir="$(realpath "${root_dir}/release")"
out_path="${out_dir}/README.txt"

function die() {
	echo $@ >&2
	exit 1
}

GIT_REF_HASH="$(git -C "${root_dir}" rev-parse HEAD)"
[[ "${GIT_REF_HASH}" =~ ^[0-9a-f]{40}$ ]] || die "invalid Git ref hash: ${GIT_REF_HASH}"

mkdir -p "${out_dir}" || die "failed to create output directory: ${out_dir}"
sed -e "s|###GIT_REF_HASH###|${GIT_REF_HASH}|g" "${template_path}" >"${out_path}" || die "failed to write ${out_path}"
