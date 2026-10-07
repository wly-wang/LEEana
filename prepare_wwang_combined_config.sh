#!/usr/bin/env bash

set -uo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: ./prepare_wwang_combined_config.sh {FHC|RHC|all}" >&2
  exit 2
fi

case "$1" in
  FHC|fhc) beams=(FHC) ;;
  RHC|rhc) beams=(RHC) ;;
  all|ALL) beams=(FHC RHC) ;;
  *) echo "Use FHC, RHC, or all" >&2; exit 2 ;;
esac

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cd "$script_dir"

alias_dir="$script_dir/input_aliases/wwang_cv"
cv_output=configurations/cv_input_production.txt
cov_output=configurations/cov_input_production.txt
file_ch_output=configurations/file_ch_production.txt

mkdir -p "$alias_dir"

tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/wwang-combined-config.XXXXXX")
cleanup() {
  rm -rf "$tmp_dir"
}
trap cleanup EXIT

cv_tmp="$tmp_dir/cv_input.txt"
cov_tmp="$tmp_dir/cov_input.txt"
file_ch_tmp="$tmp_dir/file_ch.txt"
: > "$cv_tmp"
: > "$cov_tmp"
: > "$file_ch_tmp"

global_file_no=0
failures=0

for beam in "${beams[@]}"; do
  cv_source="configurations/cv_input_${beam}.txt"
  cov_source="configurations/cov_input_${beam}.txt"

  if [[ ! -s "$cv_source" || ! -s "$cov_source" ]]; then
    echo "ERROR: missing saved $beam configuration" >&2
    exit 1
  fi

  while read -r file_type sample period source output ext_pot old_file_no norm_pot norm_period extra; do
    [[ -z "${file_type:-}" ]] && continue
    [[ "$file_type" == \#* ]] && continue
    [[ "$file_type" == "-1" ]] && break

    if [[ -n "${extra:-}" || -z "${norm_period:-}" ]]; then
      echo "ERROR: malformed row in $cv_source" >&2
      exit 1
    fi


    if [[ "$beam" == "RHC" && "$file_type" == "15" ]]; then
      file_type=5
    fi

    global_file_no=$((global_file_no + 1))
    alias_path="$alias_dir/$(printf '%02d' "$global_file_no")_${beam}_${sample}_${period}.root"

    if [[ ! -s "$source" ]]; then
      echo "MISSING INPUT [$beam]: $source" >&2
      failures=$((failures + 1))
      continue
    fi

    ln -sfn "$source" "$alias_path"

    printf '%s %s %s %s %s %s %s %s %s\n' \
      "$file_type" "$sample" "$period" "$alias_path" "$output" \
      "$ext_pot" "$global_file_no" "$norm_pot" "$norm_period" >> "$cv_tmp"
    printf '%s all\n' "$alias_path" >> "$file_ch_tmp"
  done < "$cv_source"

  while read -r name variable bins low high obs xs_flux det add_sys same_mc cov_sec file_type weight lee_strength extra; do
    [[ -z "${name:-}" ]] && continue
    [[ "$name" == \#* ]] && continue
    [[ "$name" == "End" ]] && break

    if [[ -n "${extra:-}" || -z "${lee_strength:-}" ]]; then
      echo "ERROR: malformed row in $cov_source" >&2
      exit 1
    fi

    # FHC and RHC remain distinct observed spectra in the combined analysis.
    if [[ "$beam" == "FHC" ]]; then
      obs=1
    else
      obs=2
      if [[ "$file_type" == "15" ]]; then
        file_type=5
      else
        cov_sec=$((cov_sec + 5))
      fi
    fi

    # Overlay receives Xs/flux and detector covariance. EXT has neither.
    # DIRT follows the tutorial convention of a 50% uncorrelated uncertainty.
    xs_flux=0
    det=0
    add_sys=0
    if [[ "$file_type" == "2" || "$file_type" == "20" ]]; then
      xs_flux=1
      det=1
    elif [[ "$file_type" == "4" || "$file_type" == "40" ]]; then
      add_sys=0.5
    fi

    printf '%s %s %s %s %s %s %s %s %s %s %s %s %s %s\n' \
      "$name" "$variable" "$bins" "$low" "$high" "$obs" \
      "$xs_flux" "$det" "$add_sys" "$same_mc" "$cov_sec" \
      "$file_type" "$weight" "$lee_strength" >> "$cov_tmp"
  done < "$cov_source"
done

if (( failures != 0 )); then
  echo "ERROR: $failures source file(s) are missing; production configuration was not replaced." >&2
  exit 1
fi

printf '%s\n' '-1 end -1 end end -1 -1 -1 -1' >> "$cv_tmp"
printf '%s\n' 'End End -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 end -1' >> "$cov_tmp"

mv "$cv_tmp" "$cv_output"
mv "$cov_tmp" "$cov_output"
mv "$file_ch_tmp" "$file_ch_output"

echo "Prepared $global_file_no unique logical inputs:"
echo "  $cv_output"
echo "  $cov_output"
echo "  $file_ch_output"
