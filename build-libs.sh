#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
root_dir="${script_dir}"
download_dir="${root_dir}/lib/_downloads"
cd "${script_dir}"

function die {
    echo $@
    exit 1
}

function cd_real_path {
    cd "$(realpath -e "$1")" || die "Failed to change into real path of $1"
}

source ./_build_target.sh || die "Failed to include build target script"

[[ -d lib/_build ]] && rm -Rf lib/_build
mkdir -p lib/_build
mkdir -p "${script_dir}/lib/_build/GL" || die "Failed to create GL target directory"

num_cpus=$(cat /proc/cpuinfo | grep -E 'processor\s*:' | nl | tail -n1 | sed -e 's/\s*\([0-9]\+\)\s.*/\1/')
num_jobs=$(( $num_cpus + 1 ))

echo "no libraries need to be built"
