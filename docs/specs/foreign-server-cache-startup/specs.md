# foreign-server-cache-startup

## Problem Statement

Every MyLite embedded startup smoke still records an inherited MariaDB
diagnostic:

```text
Can't open and lock privilege tables: Table 'mysql.servers' doesn't exist
```

That diagnostic comes from foreign-server metadata cache initialization reading
`mysql.servers`. MyLite already rejects `CREATE SERVER`, `ALTER SERVER`, and
`DROP SERVER` in embedded builds, and the system schema namespace policy now
keeps `mysql` from becoming an ordinary MyLite catalog schema. Startup should
therefore initialize an empty foreign-server cache in embedded MyLite instead
of probing a missing `mysql.servers` table.

## Scope

- Change embedded startup to initialize the foreign-server cache without
  reading `mysql.servers`.
- Keep non-embedded MariaDB startup behavior unchanged.
- Keep embedded SQL command rejections for `CREATE SERVER`, `ALTER SERVER`,
  and `DROP SERVER`.
- Make the embedded bootstrap smoke fail if the old `mysql.servers` startup
  diagnostic appears again.
- Record the absence of that diagnostic in the bootstrap smoke report.
- Update docs and roadmap with the new startup behavior.

## Non-Goals

- Do not implement foreign-server metadata support.
- Do not create or emulate `mysql.servers`.
- Do not change the parser or the existing SQL command rejections for server
  metadata statements.
- Do not remove inherited Aria startup logs in this slice.
- Do not change grants, users, roles, or other `mysql.*` system-table startup
  behavior.
- Do not add public `libmylite` API.

## Source Findings

- Base source: MariaDB Server `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- `sql/sql_servers.cc:servers_init(bool dont_read_servers_table)` initializes
  the foreign-server cache lock, hash, and memory root. When
  `dont_read_servers_table` is true, it skips `servers_reload()`.
- `sql/sql_servers.cc:servers_reload()` opens `mysql.servers` through
  `open_and_lock_tables()` and logs the current startup diagnostic if the table
  is absent.
- `sql/mysqld.cc:init_server_components()` currently calls
  `servers_init(0)` when not bootstrapping, so embedded startup tries to read
  `mysql.servers` even with `--skip-grant-tables`.
- The previous `unsupported-server-surface` slice found that
  `servers_init(true)` can initialize the in-memory server cache without
  reading `mysql.servers`, but deferred changing startup initialization.
- `vendor/mariadb/server/mylite/bootstrap_smoke.cc` already verifies embedded
  SQL rejections for `CREATE SERVER`, `ALTER SERVER`, and `DROP SERVER`.
- `tools/run-embedded-bootstrap-smoke.sh` captures process stderr/stdout in
  `mylite-embedded-bootstrap-output.log` and appends it to the report.

## Proposed Design

In `sql/mysqld.cc:init_server_components()`, keep upstream behavior for
non-embedded builds, but use the existing no-table initialization path for
embedded builds:

```c++
#ifdef EMBEDDED_LIBRARY
  servers_init(1);
#else
  if (!opt_bootstrap)
    servers_init(0);
#endif
```

This preserves foreign-server cache data structures for code that expects them
to exist, while avoiding any read of `mysql.servers` during embedded startup.
The selected SQL commands remain rejected earlier in
`mysql_execute_command()` for embedded builds.

In `tools/run-embedded-bootstrap-smoke.sh`, scan the smoke output for
`mysql.servers`. If present, append an explicit startup diagnostic section and
return failure. If absent, append a passing diagnostic section. This makes the
cleanup durable in CI-style smoke runs without relying on manual report
inspection.

## Affected Subsystems

- MariaDB server component startup in `sql/mysqld.cc`.
- MyLite embedded bootstrap smoke wrapper and report.
- Roadmap and relevant slice/architecture documentation.

## DDL Metadata Routing Impact

None. This slice does not touch table definition routing, catalog schema DDL,
or `.frm` handling.

## Single-File And Embedded-Lifecycle Implications

The change removes one inherited startup attempt to read a missing `mysql.*`
system table. It does not affect the remaining known Aria startup log files,
which are still tracked separately as inherited side effects.

## Public API Or File-Format Impact

No public `libmylite` API change. No file-format change.

## Binary-Size Impact

Measured artifact sizes after implementation:

- `build/mariadb-minsize/libmysqld/libmariadbd.a`: 44,440,784 bytes.
- `build/mariadb-minsize/mylite/mylite-embedded-bootstrap-smoke`: 22,775,488
  bytes.
- `build/mariadb-minsize/mylite/mylite-compatibility-smoke`: 22,776,528 bytes.

The binary-size impact is negligible because the foreign-server cache code
still compiles; embedded startup just chooses an existing no-table branch.

## License, Trademark, And Dependency Impact

No new dependency. New code remains GPL-2.0-only.

## Test And Verification Plan

- Run `git diff --check`.
- Run `bash -n tools/run-embedded-bootstrap-smoke.sh
  tools/run-compatibility-test-harness.sh`.
- Run `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`.
- Run `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`.
- Verify `mylite-embedded-bootstrap-report.txt` records
  `mysql_servers_startup=absent`.
- Verify `CREATE SERVER`, `ALTER SERVER`, and `DROP SERVER` embedded
  rejections still pass.
- Verify dynamic plugin artifacts remain absent.

## Acceptance Criteria

- Embedded MyLite startup no longer logs the missing `mysql.servers`
  diagnostic.
- The bootstrap smoke fails if that diagnostic reappears.
- Foreign-server SQL statements remain explicitly rejected in embedded mode.
- Non-embedded startup behavior is unchanged.
- The grouped compatibility harness passes.

## Implementation Result

`sql/mysqld.cc:init_server_components()` now calls `servers_init(1)` for
embedded builds, while non-embedded builds keep the existing
`if (!opt_bootstrap) servers_init(0)` path. This initializes the cache data
structures without reading `mysql.servers`.

`tools/run-embedded-bootstrap-smoke.sh` now records startup diagnostic status
and fails if the old `mysql.servers` diagnostic appears.

Verification completed:

- `git diff --check`.
- `bash -n tools/run-embedded-bootstrap-smoke.sh
  tools/run-compatibility-test-harness.sh`.
- `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`.
- `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`.

Report evidence:

- `mylite-embedded-bootstrap-report.txt`: `status=0`,
  `mysql_servers_startup=absent`, `Smoke Process Output=none`, and
  `Dynamic Plugin Artifacts=none`.
- The same report still records passing embedded rejections for
  `CREATE SERVER`, `ALTER SERVER`, and `DROP SERVER`.
- `mylite-compatibility-harness-report.txt`: all groups report `status=0`.

## Risks And Unresolved Questions

- The empty foreign-server cache is correct while foreign-server SQL is
  unsupported. A later compatibility slice that implements foreign-server
  metadata must revisit this startup path.
- Other `mysql.*` startup probes may still exist. This slice handles the
  diagnostic currently present in every MyLite report, not a full system-table
  replacement.
- Aria startup logs remain known inherited sidecars and need a separate
  storage/bootstrap cleanup slice.
