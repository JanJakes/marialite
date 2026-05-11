#!/usr/bin/env bash
set -euo pipefail

main() {
  if [[ "${1:-}" == "--inside-container" ]]; then
    run_inside_container
    return
  fi

  local repo_root
  repo_root="$(git rev-parse --show-toplevel)"

  local image
  image="${MYLITE_MARIADB_MINSIZE_IMAGE:-mylite-mariadb-minsize:ubuntu-24.04}"

  docker build \
    -t "${image}" \
    -f "${repo_root}/tools/docker/mariadb-minsize/Dockerfile" \
    "${repo_root}/tools/docker/mariadb-minsize"

  docker run --rm \
    --user "$(id -u):$(id -g)" \
    --volume "${repo_root}:/work" \
    --workdir /work \
    --env "MYLITE_MARIADB_BUILD_DIR=${MYLITE_MARIADB_BUILD_DIR:-build/mariadb-minsize}" \
    --env "MYLITE_BUILD_JOBS=${MYLITE_BUILD_JOBS:-}" \
    "${image}" \
    /work/tools/run-compatibility-test-harness.sh --inside-container
}

run_inside_container() {
  /work/tools/build-mariadb-minsize.sh --inside-container

  local build_dir="${MYLITE_MARIADB_BUILD_DIR:-build/mariadb-minsize}"
  local jobs="${MYLITE_BUILD_JOBS:-}"
  if [[ -z "${jobs}" ]]; then
    jobs="$(nproc)"
  fi

  cmake --build "${build_dir}" \
    --target mylite-compatibility-smoke \
    --parallel "${jobs}"

  local abs_build_dir
  abs_build_dir="$(cd "${build_dir}" && pwd)"
  local report="${abs_build_dir}/mylite-compatibility-harness-report.txt"
  rm -f "${report}"

  {
    printf "# MyLite Compatibility Test Harness Report\n\n"
    printf "build_dir=%s\n" "${abs_build_dir}"
    printf "jobs=%s\n\n" "${jobs}"
  } >> "${report}"

  local status=0
  run_script_group \
    "${abs_build_dir}" \
    "${report}" \
    "embedded_lifecycle" \
    "${abs_build_dir}/mylite-embedded-bootstrap-report.txt" \
    /work/tools/run-embedded-bootstrap-smoke.sh --inside-container || status=1
  run_script_group \
    "${abs_build_dir}" \
    "${report}" \
    "libmylite_lifecycle" \
    "${abs_build_dir}/libmylite-open-close-report.txt" \
    /work/tools/run-libmylite-open-close-smoke.sh --inside-container || status=1
  run_script_group \
    "${abs_build_dir}" \
    "${report}" \
    "storage_single_file" \
    "${abs_build_dir}/mylite-storage-engine-report.txt ${abs_build_dir}/mylite-catalog-write-report.txt ${abs_build_dir}/mylite-catalog-read-report.txt ${abs_build_dir}/mylite-catalog-recovery-base-report.txt ${abs_build_dir}/mylite-catalog-recovery-latest-report.txt ${abs_build_dir}/mylite-catalog-recovery-read-report.txt" \
    /work/tools/run-storage-engine-smoke.sh --inside-container || status=1
  run_comparison_group "${abs_build_dir}" "${report}" || status=1
  run_sidecar_group "${abs_build_dir}" "${report}" || status=1

  {
    printf "\n## Harness Result\n\n"
    printf "status=%s\n" "${status}"
  } >> "${report}"

  printf "Compatibility harness report: %s\n" "${report}"
  return "${status}"
}

run_script_group() {
  local abs_build_dir="$1"
  local harness_report="$2"
  local group="$3"
  local source_reports="$4"
  shift 4

  local log="${abs_build_dir}/mylite-compatibility-${group}.log"
  local status=0
  "$@" > "${log}" 2>&1 || status=$?

  {
    printf "\n## Group: %s\n\n" "${group}"
    printf "status=%s\n" "${status}"
    printf "log=%s\n" "${log}"
    printf "reports=%s\n" "${source_reports}"
    printf "command="
    printf "%q " "$@"
    printf "\n"
  } >> "${harness_report}"

  return "${status}"
}

run_comparison_group() {
  local abs_build_dir="$1"
  local harness_report="$2"

  local smoke="${abs_build_dir}/mylite/mylite-compatibility-smoke"
  local reference_root="${abs_build_dir}/mylite-compatibility-reference"
  local mylite_root="${abs_build_dir}/mylite-compatibility-mylite"
  local diff_file="${abs_build_dir}/mylite-compatibility-fingerprint.diff"

  rm -rf "${reference_root}" "${mylite_root}"
  mkdir -p \
    "${reference_root}/datadir/mylite" \
    "${reference_root}/tmp" \
    "${mylite_root}/datadir/mylite" \
    "${mylite_root}/tmp"

  local reference_report="${abs_build_dir}/mylite-compatibility-reference-report.txt"
  local reference_fingerprint="${abs_build_dir}/mylite-compatibility-reference.fingerprint"
  local reference_log="${abs_build_dir}/mylite-compatibility-reference-output.log"
  local mylite_report="${abs_build_dir}/mylite-compatibility-mylite-report.txt"
  local mylite_fingerprint="${abs_build_dir}/mylite-compatibility-mylite.fingerprint"
  local mylite_log="${abs_build_dir}/mylite-compatibility-mylite-output.log"

  local status=0
  run_comparison_phase \
    "${smoke}" \
    "${abs_build_dir}/sql/share" \
    "${reference_root}" \
    "" \
    "Aria" \
    "${reference_report}" \
    "${reference_fingerprint}" \
    "${reference_log}" || status=1
  run_comparison_phase \
    "${smoke}" \
    "${abs_build_dir}/sql/share" \
    "${mylite_root}" \
    "${mylite_root}/catalog.mylite" \
    "MYLITE" \
    "${mylite_report}" \
    "${mylite_fingerprint}" \
    "${mylite_log}" || status=1

  if [[ "${status}" -eq 0 ]]; then
    diff -u "${reference_fingerprint}" "${mylite_fingerprint}" \
      > "${diff_file}" || status=1
  else
    : > "${diff_file}"
  fi

  {
    printf "\n## Group: mariadb_comparison\n\n"
    printf "status=%s\n" "${status}"
    printf "reference_report=%s\n" "${reference_report}"
    printf "reference_fingerprint=%s\n" "${reference_fingerprint}"
    printf "reference_log=%s\n" "${reference_log}"
    printf "mylite_report=%s\n" "${mylite_report}"
    printf "mylite_fingerprint=%s\n" "${mylite_fingerprint}"
    printf "mylite_log=%s\n" "${mylite_log}"
    printf "diff=%s\n" "${diff_file}"
    if [[ -s "${diff_file}" ]]; then
      printf "\n### Fingerprint Diff\n\n"
      cat "${diff_file}"
    fi
  } >> "${harness_report}"

  return "${status}"
}

run_comparison_phase() {
  local smoke="$1"
  local lc_messages_dir="$2"
  local runtime_root="$3"
  local catalog_file="$4"
  local engine="$5"
  local report="$6"
  local fingerprint="$7"
  local log="$8"

  rm -f "${report}" "${fingerprint}" "${log}"

  local args=(
    "--datadir=${runtime_root}/datadir"
    "--tmpdir=${runtime_root}/tmp"
    "--lc-messages-dir=${lc_messages_dir}"
    "--runtime-dir=${runtime_root}"
    "--engine=${engine}"
    "--report=${report}"
    "--fingerprint=${fingerprint}"
  )
  if [[ -n "${catalog_file}" ]]; then
    args+=("--catalog-file=${catalog_file}")
  fi

  local status=0
  "${smoke}" "${args[@]}" > "${log}" 2>&1 || status=$?
  append_observed_files "${runtime_root}" "${report}" "${log}"
  return "${status}"
}

run_sidecar_group() {
  local abs_build_dir="$1"
  local harness_report="$2"

  local unexpected="${abs_build_dir}/mylite-compatibility-unexpected-sidecars.txt"
  local known="${abs_build_dir}/mylite-compatibility-known-sidecars.txt"
  : > "${unexpected}"
  : > "${known}"

  scan_runtime_sidecars "${abs_build_dir}/mylite-embedded-bootstrap" \
    "${unexpected}" "${known}"
  scan_runtime_sidecars "${abs_build_dir}/libmylite-open-close" \
    "${unexpected}" "${known}"
  scan_runtime_sidecars "${abs_build_dir}/mylite-storage-engine" \
    "${unexpected}" "${known}"
  scan_runtime_sidecars "${abs_build_dir}/mylite-catalog-persistence" \
    "${unexpected}" "${known}"
  scan_runtime_sidecars "${abs_build_dir}/mylite-catalog-recovery" \
    "${unexpected}" "${known}"
  scan_runtime_sidecars "${abs_build_dir}/mylite-compatibility-mylite" \
    "${unexpected}" "${known}"

  local status=0
  if [[ -s "${unexpected}" ]]; then
    status=1
  fi

  {
    printf "\n## Group: sidecar_scan\n\n"
    printf "status=%s\n" "${status}"
    printf "unexpected_sidecars=%s\n" "${unexpected}"
    if [[ -s "${unexpected}" ]]; then
      cat "${unexpected}"
    else
      printf "none\n"
    fi
    printf "known_inherited_sidecars=%s\n" "${known}"
    if [[ -s "${known}" ]]; then
      cat "${known}"
    else
      printf "none\n"
    fi
  } >> "${harness_report}"

  return "${status}"
}

scan_runtime_sidecars() {
  local root="$1"
  local unexpected="$2"
  local known="$3"

  if [[ ! -d "${root}" ]]; then
    return
  fi

  while IFS= read -r -d '' file; do
    local name
    name="$(basename "${file}")"
    local rel
    rel="${file#${root}/}"

    if is_known_inherited_sidecar "${name}"; then
      printf "%s\t%s\n" "${root}" "${rel}" >> "${known}"
    elif is_allowed_mylite_primary_file "${name}"; then
      continue
    elif is_unexpected_sidecar "${name}"; then
      printf "%s\t%s\n" "${root}" "${rel}" >> "${unexpected}"
    fi
  done < <(find "${root}" -type f -print0)
}

is_known_inherited_sidecar() {
  local name="$1"
  [[ "${name}" == aria_log.* || "${name}" == "aria_log_control" ]]
}

is_allowed_mylite_primary_file() {
  local name="$1"
  [[ "${name}" == *.mylite ]]
}

is_unexpected_sidecar() {
  local name="$1"
  case "${name}" in
    *.frm|*.ibd|*.MYD|*.MYI|*.MAD|*.MAI|ib_logfile*|*binlog*|*relay-log*|*.so|*.dylib|*.dll|*.mylite.tmp)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

append_observed_files() {
  local runtime_dir="$1"
  local report="$2"
  local smoke_log="$3"

  {
    printf "\n## Smoke Process Output\n\n"
    if [[ -s "${smoke_log}" ]]; then
      cat "${smoke_log}"
    else
      printf "none\n"
    fi

    printf "\n## Observed Runtime Files\n\n"
    if find "${runtime_dir}" -mindepth 1 -type f | grep -q .; then
      find "${runtime_dir}" -mindepth 1 -type f -printf "%P\t%s bytes\n" \
        | sort
    else
      printf "none\n"
    fi

    printf "\n## Dynamic Plugin Artifacts\n\n"
    if find "${runtime_dir}" -type f \( -name "*.so" -o -name "*.dylib" -o -name "*.dll" \) \
        | grep -q .; then
      find "${runtime_dir}" -type f \( -name "*.so" -o -name "*.dylib" -o -name "*.dll" \) \
        -printf "%P\n" | sort
    else
      printf "none\n"
    fi
  } >> "${report}"
}

main "$@"
