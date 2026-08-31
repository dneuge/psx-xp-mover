#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
root_dir="$(realpath "${script_dir}/../..")"

readme_path="${root_dir}/README.md"

out_dir="$(realpath "${root_dir}/release")"
out_path="${out_dir}/DISCLAIMER.txt"

cd "${script_dir}"

function die() {
	echo $@ >&2
	exit 1
}

disclaimer_headline_lno="$(grep -En '.*# Disclaimer\s*$' "${readme_path}" | cut -d':' -f1)"
content_line_offset=$(tail -n"+$(( $disclaimer_headline_lno + 1 ))" ../../README.md | grep -En '^\s*\S+' | head -n1 | cut -d':' -f1)
next_headline_offset=$(tail -n"+$(( $disclaimer_headline_lno + 1 ))" ../../README.md | grep -En '^#' | head -n1 | cut -d':' -f1)

disclaimer_content_start_lno=$(( $disclaimer_headline_lno + $content_line_offset ))
disclaimer_content_end_lno=$(( $disclaimer_headline_lno + $next_headline_offset - 1 ))
disclaimer_content_lines=$(( $disclaimer_content_end_lno - $disclaimer_content_start_lno ))

disclaimer_markdown="$(head -n${disclaimer_content_end_lno} "${readme_path}" | tail -n"+${disclaimer_content_start_lno}")"

# replace Markdown links by just link caption
disclaimer_text="$(sed -e's#\[\([^]]\+\)\]([^)]\+)#\1#g' <<<"$disclaimer_markdown")"

(
  echo "Specific Disclaimer for PSX/XP Mover"
  echo "------------------------------------"
  echo
  cat <<<"$disclaimer_text"
  echo
  echo
  echo "Refer to LICENSES.txt for complete license information incl. associated disclaimers."
) >"${out_path}"
