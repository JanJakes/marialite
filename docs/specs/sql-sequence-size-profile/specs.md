# SQL Sequence Size Profile

## Problem

The aggressive MyLite minsize profile still links MariaDB's mandatory
`sql_sequence` engine and SQL sequence implementation. MyLite does not
currently need durable sequence objects for single-file table storage, and the
sequence handler is another wrapper engine layered over a base engine rather
than a MyLite-owned persistence path.

Current measured sequence object sizes from
`build/mariadb-minsize-spatial-core`:

| Object | File bytes | `size` total |
| --- | ---: | ---: |
| `libmysqld/.../sql_sequence.cc.o` | 43,352 | 11,093 |
| `libmysqld/.../ha_sequence.cc.o` | 91,504 | 11,177 |
| `sql/libsql_sequence_embedded.a` `ha_sequence.cc.o` | 91,488 | 11,165 |

The linked open/close smoke still contains `sequence_definition`,
`ha_sequence`, `Sql_cmd_create_sequence`, `Sql_cmd_alter_sequence`,
`Item_func_nextval`, `Item_func_lastval`, `Item_func_setval`, and
`builtin_maria_sql_sequence_plugin` symbols.

## Source Findings

MariaDB source references are from imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `libmysqld/CMakeLists.txt` includes `../sql/sql_sequence.cc` and
  `../sql/ha_sequence.cc` in `SQL_EMBEDDED_SOURCES`.
- `sql/CMakeLists.txt` registers `sql_sequence` as a mandatory static storage
  engine plugin through `MYSQL_ADD_PLUGIN(sql_sequence ha_sequence.cc
  STORAGE_ENGINE MANDATORY STATIC_ONLY RECOMPILE_FOR_EMBEDDED)`.
- `sql/sql_sequence.cc` implements sequence definition validation, sequence
  table row construction, `CREATE/ALTER SEQUENCE` execution, `sequence_insert`,
  and `check_sequence_fields`.
- `sql/ha_sequence.cc` implements the `ha_sequence` wrapper handler and defines
  `sql_sequence_hton`.
- `sql/sql_yacc.yy` parses `CREATE SEQUENCE`, `ALTER SEQUENCE`,
  `DROP SEQUENCE`, `CREATE TABLE ... SEQUENCE=1`, `NEXT VALUE FOR`,
  `NEXTVAL()`, `PREVIOUS VALUE FOR`, `LASTVAL()`, and `SETVAL()`.
- After removing the sequence source objects, remaining embedded objects still
  reference `sql_sequence_hton`, `TABLE::check_sequence_privileges`,
  `sequence_insert`, `check_sequence_fields`, selected `sequence_definition`
  methods, sequence item vtables, sequence item factory methods, and
  `builtin_maria_sql_sequence_plugin`.

## Scope

Add a `MYLITE_DISABLE_SQL_SEQUENCE` aggressive minsize option that:

- skips `sql_sequence` plugin registration for the MyLite minsize profile,
- removes `../sql/sql_sequence.cc` and `../sql/ha_sequence.cc` from the embedded
  SQL source list,
- links a small embedded-only stub for the remaining sequence symbols still
  referenced by retained parser, table, SHOW, INSERT, and expression objects,
- makes `CREATE SEQUENCE`, `ALTER SEQUENCE`, sequence-table writes, and
  sequence expression execution fail explicitly, and
- leaves ordinary `AUTO_INCREMENT` columns untouched.

## Non-Goals

This slice does not remove sequence grammar from `sql_yacc.yy`. Parser-table
reduction is a larger generated-source compatibility cut and should be handled
separately if sequence stubbing proves useful.

This slice does not remove `TABLE_TYPE_SEQUENCE`, sequence error codes,
information-schema column definitions, or all sequence mentions in generic
metadata code.

This slice does not change the full MariaDB server target.

## Binary-Size Impact

Expected static archive savings are bounded by the removed sequence objects:
about 43 KiB for `sql_sequence.cc.o`, about 91 KiB for the embedded
`ha_sequence.cc.o`, and possibly another duplicate `ha_sequence.cc.o` from the
static plugin archive if plugin generation stops merging it. The replacement
stub and retained parser references will reduce the net win.

Expected linked-runtime savings are likely tens of KiB, not MiB, because the
parser and generic SQL layer still retain sequence syntax and metadata paths.

Measured on `build/mariadb-minsize-sql-sequence`, this reduced
`libmysqld/libmariadbd.a` from 33,144,206 bytes to 32,926,698 bytes, saving
217,508 bytes. The stripped `mylite-open-close-smoke` copy dropped from
6,532,968 bytes to 6,518,592 bytes, saving 14,376 bytes.

## DDL Metadata Routing Impact

No MyLite catalog-format change. MyLite must not persist sequence table
definitions in the minsize profile. `CREATE SEQUENCE`, `ALTER SEQUENCE`, and
`CREATE TABLE ... SEQUENCE=1` must fail before MyLite storage accepts metadata.

`DROP SEQUENCE IF EXISTS` may continue to route through MariaDB's missing-object
diagnostics if no object exists, but no new sequence object should be creatable.

## Single-File And Embedded-Lifecycle Implications

Removing the `ha_sequence` wrapper avoids another non-MyLite handler path that
can layer persistence over a base engine. No file-format, lock, recovery, or
startup lifecycle change is intended. Compatibility harness sidecar scans must
continue to pass.

## Public API Or File-Format Impact

No public `libmylite` API change and no `.mylite` file-format change.

## License, Trademark, And Dependency Impact

No new dependency or licensing change.

## Test And Verification Plan

Add smoke coverage that verifies:

- `CREATE SEQUENCE` fails in the minsize profile,
- `CREATE TABLE ... SEQUENCE=1` fails in the minsize profile,
- `NEXT VALUE FOR`, `NEXTVAL()`, `LASTVAL()`, and `SETVAL()` fail explicitly,
  and
- ordinary `AUTO_INCREMENT` table behavior still works through MyLite.

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-sql-sequence MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-sql-sequence MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-sql-sequence MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
git diff --check
bash -n tools/build-mariadb-minsize.sh tools/run-libmylite-open-close-smoke.sh tools/run-compatibility-test-harness.sh
```

Measure:

- `libmysqld/libmariadbd.a` bytes and object count,
- stripped `mylite-open-close-smoke` bytes,
- absence of `sql_sequence.cc.o`, `ha_sequence.cc.o`, and
  `builtin_maria_sql_sequence_plugin` from the minsize linked artifacts, and
- retained `AUTO_INCREMENT` smoke behavior.

## Acceptance Criteria

- Default minsize build links without the SQL sequence implementation objects.
- Open/close smoke and full compatibility harness pass.
- Sequence creation and sequence expression surfaces fail explicitly.
- Ordinary MyLite table creation, insert, select, and `AUTO_INCREMENT` smokes
  still pass.
- Size deltas are recorded in `docs/research/production-size-analysis.md`.

## Verification Result

Verified with:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-sql-sequence MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-sql-sequence MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-sql-sequence MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
git diff --check
bash -n tools/build-mariadb-minsize.sh tools/run-libmylite-open-close-smoke.sh tools/run-compatibility-test-harness.sh
```

Measured artifacts:

| Artifact | Bytes |
| --- | ---: |
| `libmysqld/libmariadbd.a` | 32,926,698 |
| `mylite/libmylite.a` | 122,792 |
| `storage/mylite/libmylite_embedded.a` | 388,440 |
| `mylite/mylite-open-close-smoke` | 8,941,384 |
| stripped `mylite-open-close-smoke` copy | 6,518,592 |

`sql_sequence.cc.o`, `ha_sequence.cc.o`, and
`builtin_maria_sql_sequence_plugin` are absent from the minsize linked
artifacts. The retained sequence symbols are from the MyLite unsupported stub
needed by retained parser, table-open, expression, and metadata code.

## Risks

- Sequences are wired through parser actions, SQL command flags, table-open
  metadata, `SHOW CREATE`, information schema, and insert/update paths. The
  first slice should remove the engine implementation, not attempt to erase all
  syntax.
- Parser tables will still contain sequence grammar, limiting linked-runtime
  savings.
- Existing sequence tables from a non-minsize build must fail clearly if opened
  in this profile.
