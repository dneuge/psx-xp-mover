#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
root_dir="$(realpath "${script_dir}/../..")"
release_dir="$(realpath "${root_dir}/release")"

function die() {
	echo $@ >&2
	exit 1
}

cd "${root_dir}"
source _build_target.sh || die "Failed to include build target script"

mkdir -p "${release_dir}" || die "failed to create output directory: ${release_dir}"

version_path="${release_dir}/VERSION.txt"

cat >"${version_path}" <<EOF
Version:         ${XPMOVER_VERSION:-dev}
Build ID:        ${XPMOVER_BUILD_ID:-not set}
Build Reference: ${XPMOVER_BUILD_REF:-unavailable}
Build Date:      $(date -u +'%Y-%m-%d (%d %b %Y)')
EOF