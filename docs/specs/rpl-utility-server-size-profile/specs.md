# RPL Utility Server Size Profile

## Problem

The aggressive MyLite minsize profile still compiles
`vendor/mariadb/server/sql/rpl_utility_server.cc` even though replication and
binlog replay are unsupported in the embedded product surface. The object is
large for an embedded-only utility path:

| Artifact | Bytes |
| --- | ---: |
| `build/mariadb-minsize-no-system-versioning/libmysqld/libmariadbd.a` | 27,863,442 |
| `rpl_utility_server.cc.o` | 401,688 |
| `rpl_utility.cc.o` | 5,704 |

The goal is to test whether MyLite can remove the large row-replication
conversion implementation from the minsize build without changing ordinary SQL,
storage, or `libmylite` lifecycle behavior.

## Source Findings

MariaDB source references are from the imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

`vendor/mariadb/server/libmysqld/CMakeLists.txt` still adds both
`../sql/rpl_utility.cc` and `../sql/rpl_utility_server.cc` to
`SQL_EMBEDDED_SOURCES`. The first object is tiny and owns generic
`table_def` construction/destruction and field-size helpers. The second object
owns server-side row-replication conversion rules.

`vendor/mariadb/server/sql/rpl_utility_server.cc` has a large
`HAVE_REPLICATION` block for `table_def::compatible_with()` and
`table_def::create_conversion_table()`. Embedded builds do not define
`HAVE_REPLICATION`, but the file still provides methods referenced by retained
SQL field/type vtables:

- `Field::*::rpl_conv_type_from()`
- `Field::rpl_conv_type_from_same_data_type()`
- `Type_handler::*::max_display_length_for_field()`
- selected `Type_handler::*::show_binlog_type()`

`vendor/mariadb/server/sql/field.h` declares `rpl_conv_type_from()` as a
virtual method on `Field`; implementations therefore remain link-visible even
without active replication command paths. `vendor/mariadb/server/sql/sql_type.h`
declares `max_display_length_for_field()` as a pure virtual method on
`Type_handler`, so the minsize build must still provide definitions for the
concrete type handlers.

The current no-binlog slices already disable user-facing replication, row-event
writing, binlog open/recovery, GTID state, and replication filter behavior.
This slice therefore does not need to preserve row-replication conversion as a
hidden embedded feature.

## Scope

This slice may:

- add a MyLite-owned `MYLITE_DISABLE_RPL_UTILITY_SERVER` option,
- enable it from `tools/build-mariadb-minsize.sh`,
- remove `../sql/rpl_utility_server.cc` from `SQL_EMBEDDED_SOURCES` only when
  the option is enabled,
- add a small `libmysqld` stub object that satisfies retained field/type
  virtual methods, and
- measure archive and stripped linked-smoke size deltas.

## Non-Goals

This slice does not:

- remove `../sql/rpl_utility.cc`,
- remove parser grammar for replication/binlog syntax,
- implement replication or row-event conversion in MyLite,
- change the public `libmylite` C API,
- change the MyLite file format, or
- alter non-minsize builds.

## Proposed Design

Add `MYLITE_DISABLE_RPL_UTILITY_SERVER` to the embedded CMake options and enable
it in the aggressive minsize script.

When enabled, remove `../sql/rpl_utility_server.cc` and add
`mylite_rpl_utility_server_stub.cc`. The stub should provide the exact retained
method definitions that field and type-handler vtables require:

- every `Field::*::rpl_conv_type_from()` method returns
  `CONV_TYPE_IMPOSSIBLE`,
- `Field::rpl_conv_type_from_same_data_type()` returns
  `CONV_TYPE_IMPOSSIBLE`,
- `Type_handler::*::show_binlog_type()` falls back to the generic type-handler
  name, and
- `Type_handler::*::max_display_length_for_field()` returns small metadata-based
  values sufficient for an unsupported replication path.

Returning `CONV_TYPE_IMPOSSIBLE` is deliberately stricter than preserving
upstream conversion compatibility. If a retained path accidentally asks whether
a row-replication field conversion is possible, the minsize profile should fail
closed instead of pretending replication conversion is supported.

## Affected Subsystems

- Embedded SQL source list.
- Field and type-handler vtables.
- Unsupported replication/binlog internals.

## Single-File and Embedded Lifecycle Impact

The change aligns with the embedded single-file product shape. MyLite does not
expose replication or relay-log application, and this slice does not introduce
any durable sidecar files, daemon state, or network behavior.

Ordinary MyLite open/close, bootstrap, storage-engine, catalog, and compatibility
harness behavior must remain unchanged.

## Public API and File-Format Impact

No public API change and no file-format change.

## Binary-Size Impact

The direct archive upper bound is the current `rpl_utility_server.cc.o` size
minus the replacement stub object. Linked-runtime savings may be smaller because
section garbage collection already drops unreachable replication paths, but the
field/type virtual methods are still visible through retained class metadata.

Measured result on top of `system-versioning-size-profile`:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 27,863,442 | 27,845,446 | -17,996 |
| unstripped `mylite-open-close-smoke` | 7,436,656 | 7,430,128 | -6,528 |
| stripped `mylite-open-close-smoke` | 5,342,968 | 5,337,144 | -5,824 |
| stripped `mylite-compatibility-smoke` | 5,229,936 | 5,224,096 | -5,840 |

The retained object-level code-size delta is small: `size` reports
`rpl_utility_server.cc.o` at 53,562 bytes and the replacement stub at 47,981
bytes. Both objects still emit large field/type-handler vtables and RTTI
metadata, so the slice mainly removes the upstream conversion logic bodies, not
the class metadata that retained MariaDB SQL types still require.

## License, Trademark, and Dependency Impact

This is a GPL-2.0-only MariaDB-derived build-profile change. It adds no new
third-party dependency and changes no public trademark-facing packaging.

## Test and Verification Plan

Run:

- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-rpl-utility-server MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-rpl-utility-server tools/run-storage-engine-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-rpl-utility-server tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-rpl-utility-server tools/run-embedded-bootstrap-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-rpl-utility-server tools/run-compatibility-test-harness.sh`
- `bash -n tools/build-mariadb-minsize.sh`
- `git diff --check`

Also verify that `libsql_embedded.a` no longer contains
`rpl_utility_server.cc.o` and instead contains the MyLite stub object.

Executed verification:

- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-rpl-utility-server MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-rpl-utility-server tools/run-storage-engine-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-rpl-utility-server tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-rpl-utility-server tools/run-embedded-bootstrap-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-rpl-utility-server tools/run-compatibility-test-harness.sh`
- `bash -n tools/build-mariadb-minsize.sh`
- `git diff --check`

Docker `ar -t` verifies `libsql_embedded.a` contains `rpl_utility.cc.o` and
`mylite_rpl_utility_server_stub.cc.o`, with no `rpl_utility_server.cc.o`.

## Acceptance Criteria

- The aggressive minsize build passes.
- The relevant smoke tests and compatibility harness pass.
- `rpl_utility_server.cc.o` is absent from the embedded SQL archive when the
  option is enabled.
- Size deltas are recorded in this spec and in production size analysis.
- Non-minsize behavior remains unchanged because the option defaults to `OFF`.

## Risks and Unresolved Questions

The main risk is that a non-replication SQL path unexpectedly calls one of the
row-replication conversion helpers. The no-replication stubs should make such a
call fail closed, but the verification harness may not cover every inherited
path.

Removing the smaller `rpl_utility.cc` object is left for later because it owns
generic `table_def` lifecycle methods and is not a meaningful first target.
