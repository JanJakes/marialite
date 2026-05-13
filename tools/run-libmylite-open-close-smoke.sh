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
  cmake --build "${build_dir}" \
    --target mylite-digest-smoke \
    --parallel "${jobs}"

  local abs_build_dir
  abs_build_dir="$(cd "${build_dir}" && pwd)"

  local digest_smoke="${abs_build_dir}/mylite/mylite-digest-smoke"
  local digest_log="${abs_build_dir}/mylite-digest-smoke-output.log"
  rm -f "${digest_log}"
  "${digest_smoke}" > "${digest_log}" 2>&1
  printf "mylite digest smoke output: %s\n" "${digest_log}"

  local runtime_dir="${abs_build_dir}/libmylite-open-close"
  local database="${runtime_dir}/open-close.mylite"
  local exclusive_database="${runtime_dir}/exclusive-open.mylite"
  local uri_database="${runtime_dir}/uri open.mylite"
  local report="${abs_build_dir}/libmylite-open-close-report.txt"
  local exclusive_report="${abs_build_dir}/libmylite-open-close-exclusive-report.txt"
  local uri_report="${abs_build_dir}/libmylite-open-close-uri-report.txt"
  local uri_readonly_report="${abs_build_dir}/libmylite-open-close-uri-readonly-report.txt"
  local readonly_report="${abs_build_dir}/libmylite-open-close-readonly-report.txt"

  rm -rf "${runtime_dir}"
  mkdir -p "${runtime_dir}"

  local smoke="${abs_build_dir}/mylite/mylite-open-close-smoke"
  assert_no_plsql_cursor_attribute_symbols "${smoke}"
  assert_no_status_metadata_symbols "${smoke}"

  local smoke_log="${abs_build_dir}/libmylite-open-close-output.log"
  local exclusive_log="${abs_build_dir}/libmylite-open-close-exclusive-output.log"
  local uri_log="${abs_build_dir}/libmylite-open-close-uri-output.log"
  local uri_readonly_log="${abs_build_dir}/libmylite-open-close-uri-readonly-output.log"
  local readonly_log="${abs_build_dir}/libmylite-open-close-readonly-output.log"
  rm -f "${smoke_log}"
  rm -f "${exclusive_log}"
  rm -f "${uri_log}"
  rm -f "${uri_readonly_log}"
  rm -f "${readonly_log}"
  rm -f "${report}"
  rm -f "${exclusive_report}"
  rm -f "${uri_report}"
  rm -f "${uri_readonly_report}"
  rm -f "${readonly_report}"

  local exclusive_status=0
  "${smoke}" \
    "--database=${exclusive_database}" \
    "--mode=exclusive" \
    "--report=${exclusive_report}" > "${exclusive_log}" 2>&1 || exclusive_status=$?

  local uri_status=0
  "${smoke}" \
    "--database=${uri_database}" \
    "--mode=uri" \
    "--report=${uri_report}" > "${uri_log}" 2>&1 || uri_status=$?

  local uri_readonly_status=0
  "${smoke}" \
    "--database=${uri_database}" \
    "--mode=uri-readonly" \
    "--report=${uri_readonly_report}" > "${uri_readonly_log}" 2>&1 || uri_readonly_status=$?

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

  append_observed_files "${runtime_dir}" "${exclusive_report}" "${exclusive_log}"
  append_observed_files "${runtime_dir}" "${uri_report}" "${uri_log}"
  append_observed_files "${runtime_dir}" "${uri_readonly_report}" "${uri_readonly_log}"
  append_observed_files "${runtime_dir}" "${report}" "${smoke_log}"
  append_observed_files "${runtime_dir}" "${readonly_report}" "${readonly_log}"
  printf "libmylite exclusive-open smoke report: %s\n" "${exclusive_report}"
  printf "libmylite URI-open smoke report: %s\n" "${uri_report}"
  printf "libmylite URI read-only smoke report: %s\n" "${uri_readonly_report}"
  printf "libmylite open/close smoke report: %s\n" "${report}"
  printf "libmylite read-only smoke report: %s\n" "${readonly_report}"

  if [[ "${exclusive_status}" -ne 0 ]]; then
    return "${exclusive_status}"
  fi
  if [[ "${uri_status}" -ne 0 ]]; then
    return "${uri_status}"
  fi
  if [[ "${uri_readonly_status}" -ne 0 ]]; then
    return "${uri_readonly_status}"
  fi
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

assert_no_plsql_cursor_attribute_symbols() {
  local binary="$1"
  local symbols
  symbols="$(
    nm --defined-only -C "${binary}" 2>/dev/null \
      | grep -E "Item_func_cursor_(isopen|found|notfound|rowcount)|Item_func_cursor_bool_attr" \
      || true
  )"
  if [[ -n "${symbols}" ]]; then
    printf "unexpected PL/SQL cursor attribute symbols in %s:\n%s\n" \
      "${binary}" "${symbols}" >&2
    return 1
  fi
  printf "libmylite PL/SQL cursor attribute symbols: none\n"
}

assert_no_status_metadata_symbols() {
  local binary="$1"
  local symbols
  symbols="$(
    nm --defined-only "${binary}" 2>/dev/null \
      | grep -E "[[:space:]](status_vars|com_status_vars)$" \
      || true
  )"
  if [[ -n "${symbols}" ]]; then
    printf "unexpected status metadata symbols in %s:\n%s\n" \
      "${binary}" "${symbols}" >&2
    return 1
  fi
  printf "libmylite status metadata symbols: none\n"
}

main "$@"
