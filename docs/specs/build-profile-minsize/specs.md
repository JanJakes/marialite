# build-profile-minsize

## Problem Statement

MyLite needs a repeatable embedded MariaDB build baseline before meaningful
runtime, storage-engine, or binary-size work can be measured. The upstream
source is imported, but there is not yet a reproducible command that configures
the embedded library, records the resulting artifact, or shows which plugins
are linked into the baseline.

## Scope

- Complete the minimum upstream source needed for a MariaDB embedded build by
  importing the mandatory `libmariadb` submodule at the gitlink recorded by the
  pinned MariaDB Server commit.
- Add a reproducible Linux-container build entry point for the first minimal
  embedded profile.
- Configure an out-of-source CMake build for `libmariadbd.a`.
- Disable dynamic plugins and low-value optional engines in the baseline.
- Record the artifact size and linked static plugin surface when a build
  succeeds.
- Document any blocker with the exact failing command and evidence.

## Non-Goals

- Do not patch MariaDB source in this slice unless the build cannot otherwise
  reach the target with documented upstream CMake options.
- Do not implement MyLite runtime bootstrap, public API, storage engine, DDL
  routing, catalog, or file format behavior.
- Do not claim the baseline is the final MyLite embedded profile. It is a
  measurement starting point.
- Do not optimize for size beyond selecting a conservative minimal profile.
- Do not import optional submodule contents for disabled engines.

## Source Findings

- Official MariaDB build documentation describes an out-of-source CMake build,
  `WITH_EMBEDDED_SERVER=ON` for `libmariadbd`, `BUILD_CONFIG=mysql_release`,
  and Linux/macOS dependency requirements:
  <https://mariadb.com/docs/server/server-management/install-and-upgrade-mariadb/compiling-mariadb-from-source/compiling-mariadb-from-source-the-master-guide>
- `vendor/mariadb/server/CMakeLists.txt` includes `cmake/submodules.cmake`,
  `cmake/pcre.cmake`, `cmake/libfmt.cmake`, and
  `cmake/mariadb_connector_c.cmake`, then calls `ADD_SUBDIRECTORY(libmysqld)`
  when `WITH_EMBEDDED_SERVER` is enabled.
- `vendor/mariadb/server/cmake/submodules.cmake` fails configuration when
  `libmariadb/CMakeLists.txt` is missing.
- `vendor/mariadb/server/cmake/mariadb_connector_c.cmake` always adds
  `libmariadb` to the build.
- The pinned MariaDB Server commit records these submodule gitlinks:
  - `libmariadb`:
    `7bb4e6cdf787b32907429287a636857e3b31e6a1`
  - `storage/rocksdb/rocksdb`:
    `79f08d7ffa6d34d9ca3357777bcb335884a56cfb`
  - `wsrep-lib`:
    `7010f0ab584ab9cdebb285272a0fb0ff0a5a791d`
  - `extra/wolfssl/wolfssl`:
    `59f4fa568615396fbf381b073b220d1e8d61e4c2`
  - `storage/maria/libmarias3`:
    `0d5babbe46f17147ed51efd1f05a0001017a2aad`
  - `storage/columnstore/columnstore`:
    `df0dc363078a2016b256cef2ba8145ce267dc2d1`
- `vendor/mariadb/server/cmake/build_configurations/mysql_release.cmake`
  defines `FEATURE_SET=small` as the smallest non-`WITH_NONE` release feature
  set and only enables Archive, Blackhole, FederatedX, Feedback, InnoDB, and
  Partition at larger feature levels.
- `vendor/mariadb/server/cmake/plugin.cmake` honors
  `WITHOUT_DYNAMIC_PLUGINS`, preventing module plugins from being built when
  the profile does not need dynamic plugin loading.
- `vendor/mariadb/server/libmysqld/CMakeLists.txt` builds the static embedded
  target `mysqlserver`, whose Unix output name is `mariadbd`, by merging
  embedded SQL, mandatory libraries, and embedded static plugin libraries.

## Proposed Design

Add two first-party build files:

```text
tools/docker/mariadb-minsize/Dockerfile
tools/build-mariadb-minsize.sh
```

The Dockerfile should pin a plain Linux build environment with the packages
MariaDB documents for source builds plus Ninja and size-inspection tools. The
script should build that image and run CMake from the repository root with an
out-of-source build directory.

Initial CMake profile:

```text
-G Ninja
-DCMAKE_BUILD_TYPE=MinSizeRel
-DBUILD_CONFIG=mysql_release
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
-DPLUGIN_INNOBASE=NO
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
-DPLUGIN_OQGRAPH=NO
-DPLUGIN_SPHINX=NO
-DPLUGIN_COLUMNSTORE=NO
-DPLUGIN_AUTH_SOCKET=NO
-DPLUGIN_AUTH_PAM=NO
-DPLUGIN_AUTH_PAM_V1=NO
-DPLUGIN_HASHICORP_KEY_MANAGEMENT=NO
```

The script should build the `mysqlserver` target and write a small report under
the build directory containing:

- CMake command,
- toolchain versions,
- `libmariadbd.a` path and size,
- configured plugin cache entries,
- generated embedded builtin plugin list when available.

## Affected Subsystems

- Build system orchestration outside the upstream source tree.
- Upstream source completeness for the mandatory Connector/C submodule.
- Roadmap and checklist status for the first measurable embedded build.

This slice should not affect SQL semantics, storage behavior, public API, DDL
metadata routing, or `.mylite` file lifecycle.

## DDL Metadata Routing Impact

None. The build may include upstream DDL code in `libmariadbd.a`, but no DDL
write path is changed and no `.frm` routing claim is introduced.

## Single-File And Embedded-Lifecycle Implications

The baseline embedded library is still upstream MariaDB embedded server code.
It is useful as a starting artifact, not as a MyLite runtime. The profile must
avoid dynamic plugin loading and optional durable engines where possible, but
single-file behavior remains a later storage and bootstrap problem.

## Public API Or File-Format Impact

None. No public MyLite ABI or `.mylite` file format is introduced here.

## Binary-Size Impact

This slice should create the first recorded binary-size baseline for
`libmariadbd.a`. The baseline is expected to be large because it still includes
upstream SQL and mandatory storage/plugin code. Later slices can use it to
measure reductions or additions.

## License, Trademark, And Dependency Impact

The build uses MariaDB GPL-2.0-only source already imported into the tree. The
mandatory `libmariadb` submodule must be imported from MariaDB's Connector/C
repository at the exact upstream gitlink. System build packages are development
tools, not linked MyLite distribution dependencies.

The profile should avoid AWS SDK because MariaDB's own CMake marks Apache-2.0
SDK usage as not GPLv2-distribution-compatible unless
`NOT_FOR_DISTRIBUTION=ON` is set. MyLite should not set that for a default
baseline.

## Test And Verification Plan

- Verify the working tree is clean before importing `libmariadb`.
- Fetch `https://github.com/MariaDB/mariadb-connector-c.git` at
  `7bb4e6cdf787b32907429287a636857e3b31e6a1`.
- Archive that commit into `vendor/mariadb/server/libmariadb/`.
- Compare the imported `libmariadb` tree against a fresh checkout with
  `git diff --no-index --quiet`.
- Configure the Linux-container build through `tools/build-mariadb-minsize.sh`.
- Build the `mysqlserver` target.
- Verify `build/mariadb-minsize/libmysqld/libmariadbd.a` exists.
- Record artifact size with `stat` and embedded plugin/configuration evidence
  in the build report.
- Run `git diff --check` for first-party script and docs changes.

## Implementation Result

The mandatory Connector/C submodule was imported under
`vendor/mariadb/server/libmariadb/` from commit
`7bb4e6cdf787b32907429287a636857e3b31e6a1`. The imported tree was compared
against a fresh checkout with `git diff --no-index --quiet`.

The minimal embedded profile is available through:

```sh
MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
```

The successful baseline build was produced on 2026-05-11 in Docker
`ubuntu:24.04` on Linux/aarch64 with:

- CMake 3.28.3,
- Ninja 1.11.1,
- Bison 3.8.2,
- Flex 2.6.4,
- GCC/G++ 13.3.0.

The build generated `build/mariadb-minsize/libmysqld/libmariadbd.a`:

- size: 44,134,820 bytes,
- archive objects: 570,
- dynamic plugin artifacts: none.

The generated embedded builtin plugin list was:

- `builtin_maria_aria_plugin`
- `builtin_maria_binlog_plugin`
- `builtin_maria_csv_plugin`
- `builtin_maria_heap_plugin`
- `builtin_maria_mhnsw_plugin`
- `builtin_maria_myisam_plugin`
- `builtin_maria_myisammrg_plugin`
- `builtin_maria_mysql_password_plugin`
- `builtin_maria_online_alter_log_plugin`
- `builtin_maria_sequence_plugin`
- `builtin_maria_sql_sequence_plugin`
- `builtin_maria_thread_pool_info_plugin`
- `builtin_maria_type_geom_plugin`
- `builtin_maria_type_inet_plugin`
- `builtin_maria_type_uuid_plugin`
- `builtin_maria_user_variables_plugin`
- `builtin_maria_userstat_plugin`

Aria, MyISAM, MyISAMMRG, HEAP, CSV, Sequence, SQL Sequence, Online Alter Log,
and type/user plugins are still part of upstream's embedded baseline. Removing
or replacing those belongs to later bootstrap and storage-engine slices, not to
this build-profile slice.

## Acceptance Criteria

- `vendor/mariadb/server/libmariadb/` contains the exact upstream Connector/C
  submodule commit recorded by the pinned MariaDB Server source.
- The build entry point is documented and can be run from the repository root.
- The build uses an out-of-source directory and does not modify upstream source.
- Dynamic plugins and explicitly deferred optional engines are disabled by the
  baseline profile.
- `libmariadbd.a` is produced or a concrete blocker is documented with the
  failing command and evidence.
- The roadmap points next to `embedded-bootstrap` only after this slice has a
  usable build baseline.

## Risks And Unresolved Questions

- MariaDB's CMake may still pull mandatory engines such as Aria, MyISAM,
  MyISAMMRG, MEMORY/HEAP, CSV, and Sequence into the embedded library. Removing
  or replacing those is storage-engine/bootstrap work, not a build-profile
  shortcut.
- Docker on macOS runs Linux/aarch64 here. That is acceptable as a first Linux
  baseline but should be complemented by an x86_64 Linux CI build later.
- If the container build uncovers an upstream CMake incompatibility with the
  current host Docker architecture or CMake version, record it precisely before
  patching.
