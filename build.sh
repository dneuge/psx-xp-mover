#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
root_dir="${script_dir}"
cd "${script_dir}"

src_dir="${script_dir}/src"
build_dir="${script_dir}/build"
release_dir="${script_dir}/release"

build_info_path="${src_dir}/_buildinfo.h"

function die {
    echo $@
    exit 1
}

source _build_target.sh || die "Failed to include build target script"

num_jobs=$(( $NUM_CPUS + 1 ))

[[ -d "${build_dir}" ]] && rm -Rf "${build_dir}"
mkdir -p "${build_dir}"

[[ -d "${release_dir}" ]] && rm -Rf "${release_dir}"
mkdir -p "${release_dir}"

## GENERATE BUILD INFO FILE
cat >"${build_info_path}" <<EOF
#ifndef XPMOVER__BUILDINFO_H
#define XPMOVER__BUILDINFO_H

#define XPMOVER_VERSION "${XPMOVER_VERSION:-dev}"
#define XPMOVER_BUILD_ID "${XPMOVER_BUILD_ID:-}"
#define XPMOVER_BUILD_REF "${XPMOVER_BUILD_REF:-}"
#define XPMOVER_BUILD_TARGET "${XPLANE_TARGET} ${BUILD_TARGET} ${BUILD_SYSTEM}"
#define XPMOVER_BUILD_TIME "$(date -u +'%Y-%m-%dT%H:%M:%SZ')"

#endif //XPMOVER__BUILDINFO_H
EOF

echo
echo "------ BUILD INFO FILE: ${build_info_path}"
cat "${build_info_path}"
echo "------ END OF BUILD INFO FILE"
echo

## BUILD
cd "${build_dir}"

cmake -D XPLANE_TARGET="${XPLANE_TARGET}" -D I_WILL_NOT_DISTRIBUTE_BUILD_RESULTS="${I_WILL_NOT_DISTRIBUTE_BUILD_RESULTS:-False}" $@ .. || die "CMake failed"
if [[ "${BUILD_SYSTEM}" == "vs" ]]; then
    MSYS_NO_PATHCONV=1 msbuild.exe xpmover.vcxproj /t:Build /p:Configuration=Release || die "msbuild xpmover failed"
    MSYS_NO_PATHCONV=1 msbuild.exe manualtest-psx-client.vcxproj /t:Build /p:Configuration=Release || die "msbuild manualtest-psx-client failed"
    MSYS_NO_PATHCONV=1 msbuild.exe manualtest-psx-parse.vcxproj /t:Build /p:Configuration=Release || die "msbuild manualtest-psx-parse failed"
else
    make -j$num_jobs || die "make failed"
fi

## MSVC: change directory
if [[ "$BUILD_SYSTEM" == "vs" ]]; then
    cd Release
fi

## COPY
mkdir -p "${script_dir}/release/xpmover/${XPLANE_PLATFORM_ID}" || die "Failed to create release directory ${XPLANE_PLATFORM_ID}"
cp -a xpmover.xpl "${script_dir}/release/xpmover/${XPLANE_PLATFORM_ID}/xpmover.xpl" || die "Failed to copy plugin to release directory ${XPLANE_PLATFORM_ID}"

echo Build complete.
