#!/usr/bin/env bash
set -euo pipefail

main() {
  if [[ "${1:-}" == "--inside-container" ]]; then
    build_inside_container
    return
  fi

  local repo_root
  repo_root="$(git rev-parse --show-toplevel)"

  local image
  image="${MYLITE_MARIADB_MINSIZE_IMAGE:-mylite-mariadb-minsize:ubuntu-24.04}"

  local build_dir
  build_dir="${MYLITE_MARIADB_BUILD_DIR:-build/mariadb-minsize}"

  docker build \
    -t "${image}" \
    -f "${repo_root}/tools/docker/mariadb-minsize/Dockerfile" \
    "${repo_root}/tools/docker/mariadb-minsize"

  docker run --rm \
    --user "$(id -u):$(id -g)" \
    --volume "${repo_root}:/work" \
    --workdir /work \
    --env "MYLITE_MARIADB_BUILD_DIR=${build_dir}" \
    --env "MYLITE_BUILD_JOBS=${MYLITE_BUILD_JOBS:-}" \
    --env "MYLITE_DISABLE_MYISAM_TEMP_SPILL=${MYLITE_DISABLE_MYISAM_TEMP_SPILL:-OFF}" \
    "${image}" \
    /work/tools/build-mariadb-minsize.sh --inside-container
}

build_inside_container() {
  local source_dir="vendor/mariadb/server"
  local build_dir="${MYLITE_MARIADB_BUILD_DIR:-build/mariadb-minsize}"
  local jobs="${MYLITE_BUILD_JOBS:-}"

  if [[ -z "${jobs}" ]]; then
    jobs="$(nproc)"
  fi

  mkdir -p "${build_dir}"

  local enable_section_gc
  enable_section_gc="${MYLITE_ENABLE_SECTION_GC:-ON}"

  local icf_mode
  case "${MYLITE_ENABLE_ICF:-all}" in
    all|ALL|on|ON|1|yes|YES|true|TRUE)
      icf_mode="all"
      ;;
    safe|SAFE)
      icf_mode="safe"
      ;;
    none|NONE|off|OFF|0|no|NO|false|FALSE)
      icf_mode="none"
      ;;
    *)
      printf "Invalid MYLITE_ENABLE_ICF value: %s\n" "${MYLITE_ENABLE_ICF}" >&2
      return 1
      ;;
  esac

  local disable_myisam_temp_spill
  disable_myisam_temp_spill="${MYLITE_DISABLE_MYISAM_TEMP_SPILL:-OFF}"

  local minsize_linker_flags
  minsize_linker_flags="-fuse-ld=lld -Wl,-z,pack-relative-relocs -Wl,--pack-dyn-relocs=relr"
  if [[ "${enable_section_gc}" == "ON" ]]; then
    minsize_linker_flags="${minsize_linker_flags} -Wl,--gc-sections"
  fi
  if [[ "${icf_mode}" != "none" ]]; then
    minsize_linker_flags="${minsize_linker_flags} -Wl,--icf=${icf_mode}"
  fi

  local cmake_args=(
    -G Ninja
    -DCMAKE_BUILD_TYPE=MinSizeRel
    "-DCMAKE_EXE_LINKER_FLAGS=${minsize_linker_flags}"
    "-DCMAKE_MODULE_LINKER_FLAGS=${minsize_linker_flags}"
    "-DCMAKE_SHARED_LINKER_FLAGS=${minsize_linker_flags}"
    -DBUILD_CONFIG=mysql_release
    -DENABLED_PROFILING=OFF
    -DFEATURE_SET=small
    -DWITH_EMBEDDED_SERVER=ON
    -DDISABLE_SHARED=ON
    -DWITHOUT_DYNAMIC_PLUGINS=ON
    -DUPDATE_SUBMODULES=OFF
    -DWITH_SBOM=OFF
    -DAWS_SDK_EXTERNAL_PROJECT=OFF
    -DWITH_SSL=system
    -DWITH_PCRE=system
    -DWITH_LIBFMT=system
    -DWITH_ZLIB=system
    -DWITH_JEMALLOC=auto
    -DWITH_WSREP=OFF
    -DWITH_UNIT_TESTS=OFF
    -DWITH_DBUG_TRACE=OFF
    -DWITH_PROTECT_STATEMENT_MEMROOT=OFF
    -DWITH_EXTRA_CHARSETS=none
    -DDEFAULT_COLLATION=utf8mb4_general_ci
    -DMYLITE_DISABLE_ARIA=ON
    -DMYLITE_DISABLE_BINLOG_CORE=ON
    -DMYLITE_DISABLE_BINLOG_REPLICATION=ON
    -DMYLITE_DISABLE_RPL_FILTER=ON
    -DMYLITE_DISABLE_CRYPT_FUNCTION=ON
    -DMYLITE_DISABLE_DES_FUNCTIONS=ON
    -DMYLITE_DISABLE_KDF_FUNCTION=ON
    -DMYLITE_DISABLE_DYNAMIC_PLUGIN_LOADING=ON
    -DMYLITE_DISABLE_ZLIB_COMPRESSION=ON
    -DMYLITE_DISABLE_GEOMETRY_TYPE=ON
    -DMYLITE_DISABLE_GENERAL1400_COLLATIONS=ON
    -DMYLITE_DISABLE_UCA_COLLATIONS=ON
    -DMYLITE_DISABLE_ORACLE_FUNCTIONS=ON
    -DMYLITE_DISABLE_ORACLE_PARSER=ON
    -DMYLITE_DISABLE_GIS_FUNCTIONS=ON
    -DMYLITE_DISABLE_HELP_COMMAND=ON
    -DMYLITE_DISABLE_JSON_SCHEMA_VALID=ON
    -DMYLITE_DISABLE_LEGACY_STORAGE_ENGINES=ON
    -DMYLITE_DISABLE_MYISAM_ADMIN=ON
    -DMYLITE_DISABLE_MYISAM_FULLTEXT=ON
    -DMYLITE_DISABLE_MYISAM_RTREE=ON
    "-DMYLITE_DISABLE_MYISAM_TEMP_SPILL=${disable_myisam_temp_spill}"
    -DMYLITE_DISABLE_PROCEDURE_ANALYSE=ON
    -DMYLITE_DISABLE_QUERY_CACHE=ON
    -DMYLITE_DISABLE_REGEX_FUNCTIONS=ON
    -DMYLITE_DISABLE_SERVER_UTILITY_FUNCTIONS=ON
    -DMYLITE_DISABLE_SQL_SEQUENCE=ON
    -DMYLITE_DISABLE_SPATIAL_CORE=ON
    -DMYLITE_DISABLE_VECTOR_FUNCTIONS=ON
    -DMYLITE_DISABLE_VIO_SSL=ON
    -DMYLITE_DISABLE_XML_FUNCTIONS=ON
    "-DMYLITE_ENABLE_SECTION_GC=${enable_section_gc}"
    "-DMYLITE_ENABLE_ICF=${icf_mode}"
    -DUSE_ARIA_FOR_TMP_TABLES=OFF
    -DPLUGIN_ARIA=NO
    -DPLUGIN_INNOBASE=NO
    -DPLUGIN_MHNSW=NO
    -DPLUGIN_PARTITION=NO
    -DPLUGIN_ARCHIVE=NO
    -DPLUGIN_BLACKHOLE=NO
    -DPLUGIN_FEDERATEDX=NO
    -DPLUGIN_FEEDBACK=NO
    -DPLUGIN_PERFSCHEMA=NO
    -DPLUGIN_ROCKSDB=NO
    -DPLUGIN_MROONGA=NO
    -DPLUGIN_CONNECT=NO
    -DPLUGIN_SPIDER=NO
    -DPLUGIN_S3=NO
    -DPLUGIN_OQGRAPH=NO
    -DPLUGIN_SPHINX=NO
    -DPLUGIN_COLUMNSTORE=NO
    -DPLUGIN_AUTH_SOCKET=NO
    -DPLUGIN_AUTH_PAM=NO
    -DPLUGIN_AUTH_PAM_V1=NO
    -DPLUGIN_HASHICORP_KEY_MANAGEMENT=NO
    -DPLUGIN_TYPE_GEOM=NO
    -DPLUGIN_TYPE_INET=NO
    -DPLUGIN_TYPE_UUID=NO
    -DPLUGIN_USERSTAT=NO
    -DPLUGIN_USER_VARIABLES=NO
    -DPLUGIN_SEQUENCE=NO
    -DPLUGIN_SQL_SEQUENCE=NO
    -DPLUGIN_THREAD_POOL_INFO=NO
  )

  write_cmake_command "${build_dir}" "${source_dir}" "${cmake_args[@]}"

  cmake -S "${source_dir}" -B "${build_dir}" "${cmake_args[@]}"
  cmake --build "${build_dir}" --target mysqlserver --parallel "${jobs}"
  strip_static_archive "${build_dir}"
  write_build_report "${build_dir}" "${source_dir}"
}

write_cmake_command() {
  local build_dir="$1"
  local source_dir="$2"
  shift 2

  {
    printf "cmake"
    printf " %q" -S "${source_dir}" -B "${build_dir}"
    local arg
    for arg in "$@"; do
      printf " %q" "${arg}"
    done
    printf "\n"
  } > "${build_dir}/mylite-cmake-command.txt"
}

strip_static_archive() {
  local build_dir="$1"
  local artifact="${build_dir}/libmysqld/libmariadbd.a"

  if [[ ! -f "${artifact}" ]]; then
    return
  fi

  strip --strip-unneeded "${artifact}"
  ranlib "${artifact}"
}

write_build_report() {
  local build_dir="$1"
  local source_dir="$2"
  local report="${build_dir}/mylite-build-report.txt"
  local artifact="${build_dir}/libmysqld/libmariadbd.a"

  {
    printf "# MyLite Minimal MariaDB Embedded Build Report\n\n"
    printf "Generated: %s\n" "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    printf "Source directory: %s\n" "${source_dir}"
    printf "Build directory: %s\n\n" "${build_dir}"

    printf "## CMake Command\n\n"
    cat "${build_dir}/mylite-cmake-command.txt"
    printf "\n"

    printf "## Toolchain\n\n"
    cmake --version | sed -n "1p"
    printf "ninja %s\n" "$(ninja --version)"
    bison --version | sed -n "1p"
    flex --version | sed -n "1p"
    cc --version | sed -n "1p"
    c++ --version | sed -n "1p"
    ld.lld --version | sed -n "1p"
    printf "\n"

    printf "## Artifact\n\n"
    if [[ -f "${artifact}" ]]; then
      printf "path=%s\n" "${artifact}"
      printf "strip=strip --strip-unneeded\n"
      printf "bytes=%s\n" "$(stat -c "%s" "${artifact}")"
      file "${artifact}"
      printf "objects=%s\n" "$(ar t "${artifact}" | wc -l)"
    else
      printf "missing=%s\n" "${artifact}"
    fi
    printf "\n"

    printf "## Build Profile Cache Entries\n\n"
    grep -E "^(AWS_SDK_EXTERNAL_PROJECT|BUILD_CONFIG|CMAKE_(EXE|MODULE|SHARED)_LINKER_FLAGS|DEFAULT_COLLATION|DISABLE_SHARED|ENABLED_PROFILING|FEATURE_SET|MYLITE_(DISABLE|ENABLE)_[A-Z0-9_]+|PLUGIN_[A-Z0-9_]+|UPDATE_SUBMODULES|USE_ARIA_FOR_TMP_TABLES|WITH_[A-Z0-9_]+|WITHOUT_DYNAMIC_PLUGINS):" \
      "${build_dir}/CMakeCache.txt" | sort
    printf "\n"

    printf "## Dynamic Plugin Artifacts\n\n"
    local dynamic_artifacts
    dynamic_artifacts="$(
      find "${build_dir}" -type f \( -name "*.so" -o -name "*.dylib" -o -name "*.dll" \) \
        | sort
    )"
    if [[ -n "${dynamic_artifacts}" ]]; then
      printf "%s\n" "${dynamic_artifacts}"
    else
      printf "none\n"
    fi
    printf "\n"

    printf "## Embedded Builtin Plugins\n\n"
    if [[ -f "${artifact}" ]]; then
      nm -g --defined-only "${artifact}" 2>/dev/null \
        | grep -Eo "builtin_maria_[A-Za-z0-9_]+_plugin" \
        | grep -Ev "_sizeof_" \
        | sort -u || true
    else
      printf "missing=%s\n" "${artifact}"
    fi
  } > "${report}"

  printf "Build report: %s\n" "${report}"
}

main "$@"
