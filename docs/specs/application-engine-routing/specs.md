# Application engine routing

## Problem

Application SQL commonly omits `ENGINE=` or specifies a server engine such as
`InnoDB` even when the SQL is otherwise portable. WordPress core and plugins
regularly emit `CREATE TABLE ... ENGINE=InnoDB`. In the current MyLite smokes,
every durable table is created with `ENGINE=MYLITE`, which hides two product
gaps:

- `CREATE TABLE ...` without an engine follows MariaDB embedded defaults and
  can choose MyISAM.
- `CREATE TABLE ... ENGINE=InnoDB` can fail before MyLite storage is considered
  because the minsize profile does not include InnoDB and MariaDB's default SQL
  mode includes `NO_ENGINE_SUBSTITUTION`.

Both outcomes are incompatible with the file-owned MyLite profile. Application
DDL should create MyLite tables by default and should not create durable
MyISAM, Aria, InnoDB, or `.frm` sidecars when the embedded runtime is operating
as a MyLite database handle.

## Source Findings

The selected base is MariaDB Server tag `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/sql/mysqld.cc:4056` initializes the embedded default
  storage engine to MyISAM when InnoDB is not compiled in. MyLite's minsize
  profile excludes InnoDB, so unqualified application `CREATE TABLE` needs an
  explicit MyLite runtime default.
- `vendor/mariadb/server/sql/mysqld.cc:5538` resolves
  `default_storage_engine`, `default_tmp_storage_engine`, and
  `enforce_storage_engine` into global plugin refs after plugin
  initialization.
- `vendor/mariadb/server/sql/handler.h:2317` uses
  `ha_default_handlerton()` or `ha_default_tmp_handlerton()` when no engine was
  specified.
- `vendor/mariadb/server/sql/handler.cc:306` implements
  `Storage_engine_name::resolve_storage_engine_with_error()`. If the named
  engine cannot be resolved and the command is `CREATE TABLE` or `ALTER TABLE`,
  it still returns an error when the session has `NO_ENGINE_SUBSTITUTION`.
- `vendor/mariadb/server/sql/sys_vars.cc:4050` defaults `sql_mode` to include
  `NO_ENGINE_SUBSTITUTION`.
- `vendor/mariadb/server/sql/sql_table.cc:13353` implements `check_engine()`.
  It applies `@@enforce_storage_engine`, but currently rejects that override
  under `NO_ENGINE_SUBSTITUTION`.
- `vendor/mariadb/server/sql/sql_table.cc:13451` resolves explicit
  `CREATE TABLE` engine clauses before the normal create path. The unresolved
  engine error happens here before `check_engine()`.
- `vendor/mariadb/server/sql/sql_alter.cc:530` uses the same explicit engine
  resolution path for `ALTER TABLE`.
- `vendor/mariadb/server/sql/handler.cc:6504` skips durable `.frm` writes when
  the selected engine provides `discover_table()`. MyLite already provides
  table discovery, so routing DDL to MyLite also keeps metadata in the MyLite
  catalog path.
- `vendor/mariadb/server/mylite/mylite.cc:1138` starts the embedded runtime for
  `libmylite` with controlled server arguments, but does not set MyLite as the
  default or enforced storage engine yet.
- `vendor/mariadb/server/mylite/storage_engine_smoke.cc:380` starts the storage
  smoke without a MyLite default or enforced engine. Existing tests explicitly
  use `ENGINE=MYLITE`, so they do not catch application-style DDL.
- `vendor/mariadb/server/mylite/compatibility_smoke.cc:136` is shared by the
  MariaDB reference comparison and MyLite comparison phases. Its reference
  phase intentionally creates MyISAM tables, so MyLite engine enforcement must
  be applied only when a MyLite catalog file is present.

## Design

Use existing MariaDB runtime knobs first:

- pass `--default-storage-engine=MYLITE` for MyLite-owned embedded runtimes,
- pass `--default-tmp-storage-engine=MYLITE` so user-created temporary tables
  follow the same application-facing default,
- pass `--enforce-storage-engine=MYLITE` so explicit application engine clauses
  are routed to the MyLite handler.

Add a narrow SQL-layer exception for this enforced MyLite profile:

- when an explicit engine name cannot be resolved during `CREATE TABLE` or
  `ALTER TABLE`, and `@@enforce_storage_engine` names the ready `MYLITE`
  engine, resolve the requested engine to MyLite instead of returning
  `ER_UNKNOWN_STORAGE_ENGINE` solely because `NO_ENGINE_SUBSTITUTION` is set;
- when `check_engine()` sees a different requested engine but the enforced
  engine is MyLite, choose MyLite before the generic `NO_ENGINE_SUBSTITUTION`
  rejection path.

This keeps normal MariaDB behavior for non-MyLite enforced engines and for
reference comparison runs that do not set `--enforce-storage-engine=MYLITE`.
The exception is intentionally tied to the MyLite engine name because it is a
product-level file ownership rule, not a general change to
`@@enforce_storage_engine`.

## Affected Subsystems

- Embedded runtime argument construction in `libmylite`.
- MyLite storage smoke startup arguments.
- MyLite compatibility smoke startup arguments for the catalog-backed MyLite
  comparison phase only.
- MariaDB engine-name resolution for explicit `CREATE TABLE` and `ALTER TABLE`
  engine clauses when `@@enforce_storage_engine=MYLITE`.
- MariaDB `check_engine()` enforcement behavior for the same MyLite-only case.

## DDL Metadata Routing Impact

Routing omitted and explicit application engine clauses to MyLite means regular
`CREATE TABLE`, `CREATE TEMPORARY TABLE`, `CREATE TABLE ... LIKE`,
`CREATE TABLE ... SELECT`, and `ALTER TABLE ... ENGINE=...` enter the same
MyLite table-definition and discovery paths as explicit `ENGINE=MYLITE` DDL.
Because the selected handlerton has `discover_table()`, the existing
`ha_create_table()` `.frm` suppression continues to apply.

This slice does not add new support for server-oriented engines. It prevents
application SQL from selecting external durable engines inside MyLite-owned
runtime phases.

## Single-File and Embedded Lifecycle

The embedded runtime must not create durable `.MYD`, `.MYI`, `.MAD`, `.MAI`,
`.ibd`, or `.frm` files for ordinary application DDL. Tests must cover omitted
engine, explicit `ENGINE=InnoDB`, explicit `ENGINE=MyISAM`, `CREATE TABLE ...
LIKE`, `CREATE TABLE ... SELECT`, `ALTER TABLE ... ENGINE=InnoDB`, and
user-created temporary tables.

The MariaDB reference comparison remains allowed to create MyISAM files in its
own reference runtime because that phase is not a MyLite database handle and is
not scanned as a MyLite single-file runtime.

## Public API and File Format Impact

No public C API or file-format changes are required. The behavior change is in
the embedded runtime configuration and SQL-layer routing to existing MyLite
storage.

## Binary Size Impact

The slice adds no new dependency and does not enable InnoDB, Aria, or dynamic
plugins. Expected binary-size impact is a few helper branches and additional
test code only.

## License and Dependency Impact

No new license or dependency impact. Changes stay within GPL-2.0-only
MariaDB-derived source and MyLite first-party smoke tests.

## Test Plan

- Extend the storage-engine smoke to create and reopen application-style tables:
  omitted engine, explicit `ENGINE=InnoDB`, explicit `ENGINE=MyISAM`,
  `CREATE TABLE ... LIKE`, `CREATE TABLE ... SELECT`, `ALTER TABLE ...
  ENGINE=InnoDB`, and `CREATE TEMPORARY TABLE` without an engine.
- Verify `SHOW CREATE TABLE` reports `ENGINE=MYLITE` for routed durable tables.
- Verify inserted rows and indexed lookups survive fresh-process reopen.
- Extend the compatibility MyLite phase so catalog-backed comparison runs use
  MyLite default/enforced engine routing without changing the MyISAM reference
  phase.
- Run:
  - `MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh`
  - `MYLITE_BUILD_JOBS=8 tools/run-embedded-bootstrap-smoke.sh`
  - `bash -n` on changed scripts
  - `git diff --check`

## Acceptance Criteria

- MyLite-owned runtime phases default ordinary durable and user temporary table
  DDL to `ENGINE=MYLITE`.
- Explicit `ENGINE=InnoDB` and explicit `ENGINE=MyISAM` in MyLite-owned runtime
  phases produce MyLite tables instead of errors or external durable sidecars,
  even with the default `NO_ENGINE_SUBSTITUTION` SQL mode.
- MyISAM reference comparison remains MyISAM.
- Sidecar scan reports no unexpected `.frm`, `.MYD`, `.MYI`, `.MAD`, `.MAI`,
  `.ibd`, or Aria log files in MyLite runtime directories.
- Docs and roadmap identify application engine routing as supported for MyLite
  runtime phases.

## Risks and Open Questions

- The MyLite-only override must stay narrow. A generic change to
  `@@enforce_storage_engine` semantics would be an unnecessary upstream delta.
- Some plugins may inspect `SHOW WARNINGS` after DDL. Unresolved engine names
  routed to MyLite may still produce a warning. This is acceptable for the first
  slice but should be reviewed against WordPress import behavior once a larger
  WordPress smoke exists.
- Partitioned tables remain outside this slice. If application SQL combines
  `ENGINE=InnoDB` with `PARTITION BY`, routing to MyLite will reach MyLite's
  existing partition support boundary.

## Implementation Result

Implemented in this slice:

- `libmylite` startup now passes `--default-storage-engine=MYLITE`,
  `--default-tmp-storage-engine=MYLITE`, and
  `--enforce-storage-engine=MYLITE`.
- The storage smoke runs with the same MyLite default/enforced storage engine
  policy.
- The compatibility smoke applies that policy only to catalog-backed MyLite
  comparison runs, leaving the MyISAM reference phase unchanged.
- `Storage_engine_name::resolve_storage_engine_with_error()` routes unresolved
  explicit engine names to the enforced ready MyLite handlerton for
  `CREATE TABLE` and `ALTER TABLE` instead of failing solely because
  `NO_ENGINE_SUBSTITUTION` is set.
- `check_engine()` chooses enforced MyLite before the generic
  `NO_ENGINE_SUBSTITUTION` rejection path, so explicit available engines such
  as MyISAM are also routed to MyLite in the MyLite profile.
- The storage smoke now verifies omitted engine, explicit `ENGINE=InnoDB`,
  explicit `ENGINE=MyISAM`, `ALTER TABLE ... ENGINE=InnoDB`,
  `CREATE TABLE ... LIKE`, `CREATE TABLE ... SELECT`, user-created temporary
  table routing, indexed lookup, `SHOW CREATE TABLE` `ENGINE=MYLITE`
  reporting, fresh-process reopen, and absence of external engine sidecars.
- Measured post-slice artifacts from `MYLITE_BUILD_JOBS=8` verification:
  `libmariadbd.a` 43,506,116 bytes, `mylite-storage-engine-smoke`
  22,460,512 bytes, `libmylite.a` 94,112 bytes,
  `mylite-open-close-smoke` 22,404,632 bytes, and
  `mylite-embedded-bootstrap-smoke` 22,262,040 bytes.
