#!/usr/bin/env bash

set -uo pipefail

usage() {
  cat <<'EOF'
Usage: ./run_wwang_cv_all.sh {FHC|RHC|all}

Environment variables:
  MAX_JOBS=N       Number of conversions to run concurrently (default: 4)
  CHECK_ONLY=1     Validate inputs/configuration without running conversions
  OVERWRITE=1      Replace existing output ROOT files
  KEEP_CONFIG=1    Leave the combined configuration active after success

Examples:
  CHECK_ONLY=1 ./run_wwang_cv_all.sh all
  KEEP_CONFIG=1 OVERWRITE=1 MAX_JOBS=4 ./run_wwang_cv_all.sh all
EOF
}

if [[ $# -ne 1 ]]; then
  usage
  exit 2
fi

case "$1" in
  FHC|fhc) beams=(FHC) ;;
  RHC|rhc) beams=(RHC) ;;
  all|ALL) beams=(FHC RHC) ;;
  *) usage; exit 2 ;;
esac

max_jobs=${MAX_JOBS:-4}
check_only=${CHECK_ONLY:-0}
overwrite=${OVERWRITE:-0}
keep_config=${KEEP_CONFIG:-0}
production_succeeded=0

if ! [[ "$max_jobs" =~ ^[1-9][0-9]*$ ]]; then
  echo "ERROR: MAX_JOBS must be a positive integer" >&2
  exit 2
fi

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cd "$script_dir"

converter=./bin/convert_checkout_hist
bdt_name=wwang_numu_numubar_BDT

if [[ ! -x "$converter" ]]; then
  echo "ERROR: $converter is missing or not executable. Build/install wcp-uboone-bdt first." >&2
  exit 1
fi

for xml in \
  bdt_weights/${bdt_name}_FHC_others.xml \
  bdt_weights/${bdt_name}_FHC_numuCC.xml \
  bdt_weights/${bdt_name}_FHC_numubarCC.xml \
  bdt_weights/${bdt_name}_RHC_others.xml \
  bdt_weights/${bdt_name}_RHC_numuCC.xml \
  bdt_weights/${bdt_name}_RHC_numubarCC.xml; do
  if [[ ! -s "$xml" ]]; then
    echo "ERROR: missing or empty BDT weight file: $xml" >&2
    exit 1
  fi
done

prepare_script=./prepare_wwang_combined_config.sh
if [[ ! -x "$prepare_script" ]]; then
  echo "ERROR: $prepare_script is missing or not executable." >&2
  exit 1
fi

# Build one internally consistent configuration. Every logical period receives
# a unique symlink pathname because CovMatrix keys its input map by filename.
"$prepare_script" "$1" || exit 1

backup_dir=$(mktemp -d "${TMPDIR:-/tmp}/wwang-cv-config.XXXXXX")
restore_configs() {
  if [[ "$keep_config" == "1" && "$production_succeeded" == "1" ]]; then
    echo "Combined production configuration remains active for merge/covariance steps."
    rm -rf "$backup_dir"
    return
  fi

  for name in cv_input cov_input file_ch; do
    if [[ -e "$backup_dir/$name.txt" ]]; then
      cp "$backup_dir/$name.txt" "configurations/$name.txt"
    else
      rm -f "configurations/$name.txt"
    fi
  done
  rm -rf "$backup_dir"
}
trap restore_configs EXIT INT TERM

for name in cv_input cov_input file_ch; do
  if [[ -e "configurations/$name.txt" ]]; then
    cp "configurations/$name.txt" "$backup_dir/$name.txt"
  fi
done

activate_production() {
  local name

  for name in cv_input cov_input file_ch; do
    if [[ ! -s "configurations/${name}_production.txt" ]]; then
      echo "ERROR: missing configurations/${name}_production.txt" >&2
      return 1
    fi
    cp "configurations/${name}_production.txt" "configurations/${name}.txt"
  done
}

collect_jobs() {
  local config="configurations/cv_input_production.txt"
  local file_type sample period input output ext_pot file_no norm norm_period extra

  job_inputs=()
  job_outputs=()
  job_beams=()

  while read -r file_type sample period input output ext_pot file_no norm norm_period extra; do
    [[ -z "${file_type:-}" ]] && continue
    [[ "$file_type" == \#* ]] && continue
    [[ "$file_type" == "-1" ]] && break

    if [[ -n "${extra:-}" || -z "${norm_period:-}" ]]; then
      echo "ERROR: malformed row in $config: $file_type $sample $period $input $output $ext_pot $file_no $norm $norm_period ${extra:-}" >&2
      return 1
    fi

    job_inputs+=("$input")
    job_outputs+=("$output")
    if [[ "$output" == */FHC/* ]]; then
      job_beams+=("FHC")
    elif [[ "$output" == */RHC/* ]]; then
      job_beams+=("RHC")
    else
      echo "ERROR: cannot determine beam mode from output path: $output" >&2
      return 1
    fi
  done < "$config"

  if [[ ${#job_inputs[@]} -eq 0 ]]; then
    echo "ERROR: no jobs found in $config" >&2
    return 1
  fi
}

preflight_production() {
  local failures=0
  local i input output beam

  collect_jobs || return 1

  for i in "${!job_inputs[@]}"; do
    input=${job_inputs[$i]}
    output=${job_outputs[$i]}
    beam=${job_beams[$i]}

    if [[ ! -s "$input" ]]; then
      echo "MISSING INPUT [$beam]: $input" >&2
      failures=$((failures + 1))
    fi

    if [[ -e "$output" && "$overwrite" != "1" && "$check_only" != "1" ]]; then
      echo "EXISTING OUTPUT [$beam]: $output (set OVERWRITE=1 to replace it)" >&2
      failures=$((failures + 1))
    fi
  done

  if (( failures != 0 )); then
    echo "ERROR: production preflight found $failures problem(s)." >&2
    return 1
  fi

  echo "Production preflight OK: ${#job_inputs[@]} unique logical input files"
}

run_production() {
  local failures=0
  local in_batch=0
  local i input output beam log_dir log pid
  local -a batch_pids=()
  local -a batch_labels=()

  activate_production || return 1
  collect_jobs || return 1
  mkdir -p hist_rootfiles/FHC hist_rootfiles/RHC logs/wwang_cv/FHC logs/wwang_cv/RHC

  wait_batch() {
    local j
    for j in "${!batch_pids[@]}"; do
      if wait "${batch_pids[$j]}"; then
        echo "DONE: ${batch_labels[$j]}"
      else
        echo "FAILED: ${batch_labels[$j]}" >&2
        failures=$((failures + 1))
      fi
    done
    batch_pids=()
    batch_labels=()
    in_batch=0
  }

  for i in "${!job_inputs[@]}"; do
    input=${job_inputs[$i]}
    output=${job_outputs[$i]}
    beam=${job_beams[$i]}
    log_dir="logs/wwang_cv/${beam}"
    log="$log_dir/$(basename "${output%.root}").log"

    mkdir -p "$(dirname "$output")"
    if [[ "$overwrite" == "1" ]]; then
      rm -f "$output"
    fi

    echo "START [$beam] $((i + 1))/${#job_inputs[@]}: $(basename "$input")"
    "$converter" "$input" "$output" "-b${bdt_name}" >"$log" 2>&1 &
    pid=$!
    batch_pids+=("$pid")
    batch_labels+=("$output (log: $log)")
    in_batch=$((in_batch + 1))

    if (( in_batch == max_jobs )); then
      wait_batch
    fi
  done

  if (( in_batch != 0 )); then
    wait_batch
  fi

  for output in "${job_outputs[@]}"; do
    if [[ ! -s "$output" ]]; then
      echo "MISSING OUTPUT [$beam]: $output" >&2
      failures=$((failures + 1))
    fi
  done

  if (( failures != 0 )); then
    echo "ERROR: production had $failures failure(s)." >&2
    return 1
  fi

  echo "Production complete: ${#job_outputs[@]} ROOT files"
}

preflight_production || exit 1

if [[ "$check_only" == "1" ]]; then
  echo "Preflight complete; no conversions were run."
  exit 0
fi

run_production || exit 1
production_succeeded=1

echo "All requested CV histogram files were produced successfully."
