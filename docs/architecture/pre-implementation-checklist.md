# Pre-implementation checklist

This checklist captures decisions that should be made before MyLite imports
or changes MariaDB source in earnest.

## Upstream base

- Initial base selected and imported: MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Upstream repository: <https://github.com/MariaDB/server>.
- Release status checked on 2026-05-11: MariaDB.org listed 11.8.6 as Stable,
  and MariaDB release notes described it as a Stable GA 11.8 long-term release.
- The floating `11.8` branch remains useful for observation, but MyLite
  implementation is pinned to the imported tag until a deliberate upstream
  update slice changes it.
- Keep any future upstream-tracking branch separate from MyLite patch branches.

## Build environment

- Reproducible Linux container build added:
  `tools/build-mariadb-minsize.sh`.
- Baseline environment recorded on 2026-05-11: Docker `ubuntu:24.04` on
  Linux/aarch64, CMake 3.28.3, Ninja 1.11.1, Bison 3.8.2, Flex 2.6.4, and
  GCC/G++ 13.3.0.
- Baseline embedded artifact produced:
  `build/mariadb-minsize/libmysqld/libmariadbd.a`.
- Baseline size recorded: 44,134,820 bytes, 570 archive objects.
- Dynamic plugin artifacts recorded: none.
- Embedded builtin plugins recorded in
  `build/mariadb-minsize/mylite-build-report.txt`: Aria, binlog, CSV, HEAP,
  MyISAM, MyISAMMRG, MHNSW, MySQL password, Online Alter Log, Sequence, SQL
  Sequence, Thread Pool Info, Type Geom, Type Inet, Type UUID, User Variables,
  and Userstat.
- After `mylite-engine-discovery`, the current embedded artifact is
  44,227,954 bytes and the built-in plugin evidence includes
  `builtin_maria_mylite_plugin`.
- macOS native build support remains future work; the current baseline is the
  Linux-container build.

## Source layout and patch stack

- Upstream MariaDB source enters this repository under
  `vendor/mariadb/server/`.
- Keep mechanical upstream imports separate from MyLite commits.
- Keep MyLite-owned code in clearly named modules where possible.
- Preserve upstream formatting in MariaDB-derived files.
- Document every broad fork delta in the slice spec or commit body.

## Legal, license, and trademark

- Treat MyLite as GPL-2.0-only while it contains MariaDB-derived server code.
- Document that SQLite-like lifecycle semantics do not imply permissive
  SQLite-like licensing.
- Review whether the MyLite name and public packaging are acceptable under
  MySQL and MariaDB trademark rules.
- Add clear no-affiliation notices before distributing public binaries or
  packages.
- Preserve upstream copyright and license notices.

## First engineering slices

Use [../ROADMAP.md](../ROADMAP.md) as the ordered progress tracker. Keep this
checklist focused on gates that should be satisfied before and during the first
implementation slices.

Write slice specs before implementation for:

- `upstream-11-8-import`
- `build-profile-minsize`
- `embedded-bootstrap`
- `libmylite-open-close`
- `storage-engine-skeleton`
- `mylite-engine-discovery`
- `ddl-metadata-routing`
- `single-file-catalog`
- `unsupported-server-surface`

Each slice should state scope, source refs, design, tests, acceptance criteria,
and known risks.

## Test harness

- Embedded bootstrap smoke added:
  `tools/run-embedded-bootstrap-smoke.sh`.
- The smoke starts MariaDB's embedded runtime in-process, runs `SELECT 1`,
  verifies explicit rejections for the first unsupported server surfaces, shuts
  down, and records observed files under
  `build/mariadb-minsize/mylite-embedded-bootstrap-report.txt`.
- Open/close lifecycle smoke added:
  `tools/run-libmylite-open-close-smoke.sh`.
- The first `libmylite` implementation keeps MariaDB's embedded runtime
  process-scoped after first initialization because repeated
  `mysql_server_init()` after `mysql_server_end()` currently crashes in
  inherited MariaDB startup code.
- Storage-engine registration smoke added:
  `tools/run-storage-engine-smoke.sh`.
- The first static `MYLITE` engine is visible through
  `information_schema.ENGINES`, but user DDL remains deferred until metadata
  routing can prevent `.frm` and schema-directory side effects.
- The engine-discovery smoke proves the seed table `mylite.probe` can be
  discovered and scanned without a `.frm` file.
- Identify MariaDB tests that can run against embedded mode early.
- Add file-system checks that distinguish expected MyLite companion files from
  unexpected MariaDB datadir, schema, engine, or log sidecars.
- Add repeated open/close lifecycle tests.
- Add basic crash/reopen tests once file writes exist.
- Add a MariaDB reference runtime for compatibility-sensitive SQL behavior.
- Keep unit, embedded smoke, MariaDB-comparison, and crash tests separately
  labeled.
- Grouped compatibility harness added:
  `tools/run-compatibility-test-harness.sh`.
- The harness runs embedded lifecycle, `libmylite` lifecycle,
  storage/recovery, MariaDB-reference comparison, and MyLite sidecar scan
  groups, and records known inherited Aria startup logs separately from
  unexpected sidecars.

## File format and storage decisions

- Decide whether v1 uses a custom pager, SQLite pager/B-tree code, or another
  GPL-compatible storage component.
- Define initial file header fields, magic bytes, page size, checksum policy,
  and format-version policy.
- Decide how table definition images, SQL text, and `tabledef_version` are
  stored.
- Decide whether rollback journal or WAL state lives inside the `.mylite` file
  or in documented MyLite-owned companion files.
- Define the minimum crash-safety guarantee before storing user data.
- Define how schemas, system tables, grants policy, and autoincrement state are
  represented in the catalog.

## Product scope

- Define v1 write concurrency honestly and avoid permanently closing the path to
  safer in-process write concurrency later.
- Decide which journal, WAL, lock, shared-memory, and temporary files are
  allowed and how strict no-temp-file or no-companion-files modes behave.
- Decide whether named time zones, events, stored routines, triggers, views,
  foreign keys, fulltext, spatial indexes, and generated columns are in v1 or
  explicitly deferred.
- Decide how unsupported server features fail: parse error, stable MyLite
  error, MariaDB diagnostic, warning, or documented no-op.

## Binary size and dependencies

- Establish an initial size baseline before making size promises.
- Define the default embedded profile by explicitly listing omitted
  running-server-specific and rare optional components.
- Track size changes per implementation slice once code exists.
- Require a license and size rationale for every new dependency.
- Keep dynamic plugin loading out of the default embedded profile.
