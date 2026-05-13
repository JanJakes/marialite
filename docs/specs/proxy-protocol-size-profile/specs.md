# Proxy Protocol Size Profile

## Problem Statement

The aggressive embedded minsize profile still compiles MariaDB's
`proxy_protocol.cc` implementation. Proxy protocol support exists for network
listeners that accept proxied client connections. MyLite's current embedded
runtime has no network listener, remote login, or socket accept loop, so the
retained parser, CIDR validator, subnet cache, and lock have no product value
in the aggressive profile.

Current baseline after `foreign-server-cache-size-profile`:

| Artifact | Bytes |
| --- | ---: |
| `libmysqld/libmariadbd.a` | 30,072,536 |
| `proxy_protocol.cc.o` object | 11,088 |
| stripped `mylite-open-close-smoke` | 5,726,568 |

## Source Findings

- Imported MariaDB base: `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `vendor/mariadb/server/sql/proxy_protocol.cc` implements PROXY v1/v2 header
  parsing, `proxy_protocol_networks` CIDR parsing, subnet matching, a process
  global subnet cache, and a cache lock.
- `vendor/mariadb/server/sql/net_serv.cc` and `sql_connect.cc` call the
  parser and allow-list checks from network connection paths.
- `vendor/mariadb/server/sql/mysqld.cc` calls
  `init_proxy_protocol_networks()` and `destroy_proxy_protocol_networks()`
  during process lifecycle.
- `vendor/mariadb/server/sql/sys_vars.cc` exposes
  `proxy_protocol_networks` and uses `proxy_protocol_networks_valid()` plus
  `set_proxy_protocol_networks()` for validation and updates.
- In the current linked open/close smoke, section GC keeps only the sysvar and
  lifecycle-facing pieces: `set_proxy_protocol_networks()`,
  `proxy_protocol_networks_valid()`, `destroy_proxy_protocol_networks()`,
  `my_proxy_protocol_networks`, and `proxy_protocol_subnet_count`.

## Scope

Add a minsize option that removes full proxy protocol support from the embedded
library. The option will:

- remove `../sql/proxy_protocol.cc` from `SQL_EMBEDDED_SOURCES`;
- add a MyLite-owned proxy protocol stub;
- keep startup and shutdown entry points as no-ops;
- keep network parser and allow-list entry points returning disabled results;
- keep the `proxy_protocol_networks` sysvar present but empty; and
- reject non-empty `proxy_protocol_networks` values in the aggressive embedded
  profile.

## Non-Goals

- Do not implement proxy protocol header parsing.
- Do not retain the CIDR parser, subnet cache, or cache lock.
- Do not change non-embedded MariaDB behavior.
- Do not remove the inherited `proxy_protocol_networks` sysvar from the parser
  or sysvar registry in this slice.
- Do not change public `libmylite` API or `.mylite` file format.

## Proposed Design

Add `MYLITE_DISABLE_PROXY_PROTOCOL` to
`vendor/mariadb/server/libmysqld/CMakeLists.txt` and enable it in
`tools/build-mariadb-minsize.sh`.

Create `vendor/mariadb/server/libmysqld/mylite_proxy_protocol_stub.cc`.
`has_proxy_protocol_header()` and `is_proxy_protocol_allowed()` will return
false. `parse_proxy_protocol_header()` will return failure. Startup and
shutdown will not allocate any lock or subnet cache. The sysvar validation and
update hooks will accept only `NULL` or empty strings, leaving
`proxy_protocol_networks` visible but disabled.

## Affected Subsystems

- Embedded minsize SQL source list.
- Embedded startup/shutdown lifecycle hooks.
- `proxy_protocol_networks` sysvar validation.
- Binary-size documentation.

## DDL Metadata Routing Impact

No DDL impact.

## Single-File And Embedded-Lifecycle Impact

This removes a process-global network allow-list cache and lock from the
embedded lifecycle. It does not affect `.mylite` file ownership.

## Public API Or File-Format Impact

No public `libmylite` API or `.mylite` file-format change.

## Binary-Size Impact

Measured on top of `foreign-server-cache-size-profile`:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 30,072,536 | 30,064,524 | -8,012 |
| `mylite/mylite-open-close-smoke` | 7,960,568 | 7,960,400 | -168 |
| stripped `mylite-open-close-smoke` copy | 5,726,568 | 5,726,488 | -80 |

The embedded archive still contains 422 objects. It now contains the
3,144-byte `mylite_proxy_protocol_stub.cc.o` member instead of
`proxy_protocol.cc.o`. The linked smoke binary retains only the small stub
symbols for `set_proxy_protocol_networks()`,
`proxy_protocol_networks_valid()`, and `destroy_proxy_protocol_networks()`,
plus the inherited `my_proxy_protocol_networks` sysvar storage. The
`proxy_protocol_subnet_count` global is no longer linked.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-proxy-protocol \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-proxy-protocol \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-proxy-protocol \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh
git diff --check
```

Measure:

- archive bytes and object count;
- unstripped and stripped linked smoke bytes;
- absence of `proxy_protocol.cc.o` in `libmariadbd.a`;
- presence and size of the replacement stub; and
- absence of linked full proxy protocol symbols and globals.

## Acceptance Criteria

- The minsize build completes.
- Open/close smoke and compatibility harness pass.
- The smoke verifies `proxy_protocol_networks` remains empty and non-empty
  values are rejected.
- The embedded archive no longer contains `proxy_protocol.cc.o`.
- Size results are recorded here and in
  `docs/research/production-size-analysis.md`.

## Verification Results

All acceptance checks passed in
`MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-proxy-protocol`.

The open/close smoke reported `status=0`,
`exec_proxy_protocol_rows=proxy_protocol_networks:`, and
`exec_proxy_protocol_set_message` containing
`Variable 'proxy_protocol_networks' can't be set to the value of '*'`. The
compatibility harness reported `status=0` for all groups.

## Risks And Unresolved Questions

- This is a deliberate network compatibility cut. A future embedded profile
  with a network listener must disable this option.
- The sysvar remains visible for upstream compatibility, but non-empty values
  are rejected because they cannot affect any embedded network path.
