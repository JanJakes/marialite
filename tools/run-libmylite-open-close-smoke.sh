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
    /work/tools/run-libmylite-open-close-smoke.sh --inside-container
}

run_inside_container() {
  /work/tools/build-mariadb-minsize.sh --inside-container

  local build_dir="${MYLITE_MARIADB_BUILD_DIR:-build/mariadb-minsize}"
  local jobs="${MYLITE_BUILD_JOBS:-}"
  if [[ -z "${jobs}" ]]; then
    jobs="$(nproc)"
  fi

  cmake --build "${build_dir}" \
    --target mylite-open-close-smoke \
    --parallel "${jobs}"

  local abs_build_dir
  abs_build_dir="$(cd "${build_dir}" && pwd)"

  local runtime_dir="${abs_build_dir}/libmylite-open-close"
  local database="${runtime_dir}/open-close.mylite"
  local report="${abs_build_dir}/libmylite-open-close-report.txt"
  local readonly_report="${abs_build_dir}/libmylite-open-close-readonly-report.txt"

  rm -rf "${runtime_dir}"
  mkdir -p "${runtime_dir}"

  local smoke="${abs_build_dir}/mylite/mylite-open-close-smoke"
  local smoke_log="${abs_build_dir}/libmylite-open-close-output.log"
  local readonly_log="${abs_build_dir}/libmylite-open-close-readonly-output.log"
  rm -f "${smoke_log}"
  rm -f "${readonly_log}"
  local status=0
  "${smoke}" \
    "--database=${database}" \
    "--mode=default" \
    "--report=${report}" > "${smoke_log}" 2>&1 || status=$?

  local readonly_status=0
  "${smoke}" \
    "--database=${database}" \
    "--mode=readonly" \
    "--report=${readonly_report}" > "${readonly_log}" 2>&1 || readonly_status=$?

  append_observed_files "${runtime_dir}" "${report}" "${smoke_log}"
  append_observed_files "${runtime_dir}" "${readonly_report}" "${readonly_log}"
  printf "libmylite open/close smoke report: %s\n" "${report}"
  printf "libmylite read-only smoke report: %s\n" "${readonly_report}"

  if [[ "${status}" -ne 0 ]]; then
    return "${status}"
  fi
  return "${readonly_status}"
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
