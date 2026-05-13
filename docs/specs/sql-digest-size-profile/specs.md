# SQL Digest Size Profile

## Problem Statement

The aggressive MyLite minsize profile still links MariaDB's statement digest
normalizer from `sql_digest.cc`. That object retains the generated
`lex_token_array`, digest text rendering, and token reduction helpers even
though MyLite does not expose Performance Schema statement digest reporting in
the embedded product surface.

The current no-JSON-type linked smoke binary still contains:

- `lex_token_array` at 16,496 bytes,
- `digest_add_token()`,
- `digest_reduce_token()`,
- `compute_digest_text()`, and
- `compute_digest_md5()`.

These are server observability bytes, not storage semantics.

## Source Findings

MariaDB source references are from the imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/libmysqld/CMakeLists.txt` includes
  `../sql/sql_digest.cc` in `SQL_EMBEDDED_SOURCES`.
- `vendor/mariadb/server/sql/sql_digest.cc` includes generated parser token
  names through `lex_token.h` and emits the 16,496-byte `lex_token_array`.
- `vendor/mariadb/server/sql/sql_lex.cc` calls `digest_add_token()` and
  `digest_reduce_token()` only when `Lex_input_stream::m_digest` is non-null.
- `vendor/mariadb/server/sql/sql_parse.cc` initializes `thd->m_digest` for
  `COM_QUERY` parsing and passes digest storage to Performance Schema digest
  instrumentation when active.
- `vendor/mariadb/server/sql/sql_class.cc` allocates `m_token_array` per THD
  when `max_digest_length > 0`; the default is 1024 in `sys_vars.cc`.
- `vendor/mariadb/server/sql/sql_digest.h` exports only
  `compute_digest_md5()` and `compute_digest_text()`; digest token functions
  are declared from `sql_lex.h`.

## Official Documentation References

- [MariaDB Performance Schema Digests](https://mariadb.com/kb/en/performance-schema-digests/)
  describes statement digests as normalized statements used to gather
  statistics for similar statements.
- [MariaDB Performance Schema events_statements_summary_by_digest Table](https://mariadb.com/docs/server/reference/system-tables/performance-schema/performance-schema-tables/performance-schema-events_statements_summary_by_digest-table)
  documents digest aggregation as Performance Schema statement-event
  observability.

## Scope

This slice may:

- add `MYLITE_DISABLE_SQL_DIGEST`,
- enable it only in `tools/build-mariadb-minsize.sh`,
- replace `../sql/sql_digest.cc` with a tiny embedded stub,
- avoid initializing parser digest collection for `COM_QUERY` in the minsize
  profile,
- avoid THD digest token-array allocation in the minsize profile, and
- add smoke coverage that `max_digest_length` is `0` in the aggressive profile.

## Non-Goals

This slice does not:

- remove SQL parser tokenization,
- remove SQL text passed to execution, error reporting, logging, or diagnostics,
- remove cryptographic digest helpers used by SQL functions or authentication,
- change Performance Schema behavior in non-minsize builds,
- change public `libmylite` API behavior, or
- change `.mylite` file format.

## Proposed Design

Add an embedded CMake option, `MYLITE_DISABLE_SQL_DIGEST`, that defaults to
`OFF` and is enabled only by the aggressive minsize script.

When enabled:

- remove `../sql/sql_digest.cc` from `SQL_EMBEDDED_SOURCES`;
- add `mylite_sql_digest_stub.cc`;
- make `digest_add_token()` and `digest_reduce_token()` no-ops that return the
  existing state;
- make `compute_digest_md5()` produce the MD5 of an empty token stream;
- make `compute_digest_text()` clear the destination string;
- skip `COM_QUERY` digest state initialization and continuation digest resets;
- skip Performance Schema digest start/end plumbing in `mysql_parse()`; and
- set the `max_digest_length` default to `0` under the profile so THD
  construction does not allocate an unused token array.

This removes digest output in the aggressive embedded profile. If a retained
internal caller asks for digest text, it receives an empty digest string instead
of normalized SQL text.

## Affected Subsystems

- Embedded minsize CMake source selection.
- SQL parser digest instrumentation hooks.
- THD construction memory allocation.
- System-variable default for `max_digest_length`.
- Open/close smoke metadata coverage.
- Production size analysis.

## Single-File And Embedded-Lifecycle Impact

No file, recovery, locking, catalog, or sidecar behavior changes. This removes
process-local statement observability metadata and an unused parser token table.

## Public API Or File-Format Impact

No public `libmylite` API change and no `.mylite` file-format change.

SQL compatibility impact is limited to digest observability: embedded clients
that expect statement digest text or Performance Schema digest aggregation from
the aggressive minsize profile will no longer receive it.

## Binary-Size Impact

The implemented profile produced these measurements against the preceding
`json-type-size-profile` baseline:

| Artifact | Before | After | Delta |
| --- | ---: | ---: | ---: |
| `libmysqld/libmariadbd.a` | 25,577,556 | 25,523,714 | -53,842 |
| unstripped `mylite-open-close-smoke` | 6,639,904 | 6,620,632 | -19,272 |
| stripped `mylite-open-close-smoke` | 4,676,440 | 4,657,704 | -18,736 |

`llvm-size` total for the linked open-close smoke changed from 4,900,749 to
4,880,125 bytes (-20,624). `.data` dropped by 16,496 bytes because
`lex_token_array` is gone, and `.text` dropped by 2,272 bytes.

## License, Trademark, And Dependency Impact

No new dependency or license impact. This is a GPL-2.0-only MariaDB-derived
build-profile change.

## Test And Verification Plan

Run:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-digest \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-digest \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-digest \
  MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-digest \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
bash -n tools/build-mariadb-minsize.sh \
  tools/run-libmylite-open-close-smoke.sh \
  tools/run-storage-engine-smoke.sh \
  tools/run-compatibility-test-harness.sh
git diff --check
llvm-ar t build/mariadb-minsize-no-sql-digest/libmysqld/libmariadbd.a | \
  rg 'sql_digest|mylite_sql_digest'
llvm-nm --demangle --defined-only \
  build/mariadb-minsize-no-sql-digest/mylite/mylite-open-close-smoke | \
  rg 'lex_token_array|digest_add_token|digest_reduce_token|compute_digest_text|compute_digest_md5' || true
```

Measure:

- archive bytes,
- unstripped and stripped linked open-close smoke bytes,
- absence of `sql_digest.cc.o` from `libmariadbd.a`,
- absence of `lex_token_array` from the linked smoke binary, and
- `SHOW VARIABLES LIKE 'max_digest_length'` returns `0`.

## Acceptance Criteria

- Passed: the minsize build succeeds with `MYLITE_DISABLE_SQL_DIGEST=ON`.
- Passed: the open/close, storage-engine, and compatibility smokes pass.
- Passed: `sql_digest.cc.o` is absent from the embedded archive.
- Passed: `lex_token_array` is absent from the linked smoke binary.
- Passed: `max_digest_length` reports `0` in the aggressive minsize profile.
- Passed: size results are recorded here and in
  `docs/research/production-size-analysis.md`.

## Verification

Passed:

```sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-digest \
  MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-digest \
  MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-digest \
  MYLITE_BUILD_JOBS=8 tools/run-storage-engine-smoke.sh
MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-sql-digest \
  MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

The open-close smoke report includes:

```text
exec_sql_digest_rows=max_digest_length=max_digest_length:0
```

The archive check reports `mylite_sql_digest_stub.cc.o` and no
`sql_digest.cc.o`. The linked-symbol check returns no matches.

## Risks And Unresolved Questions

- This removes normalized statement digest text from the aggressive profile.
  That belongs to server observability and should remain available in
  non-aggressive builds.
- If any retained Performance Schema instrumentation unexpectedly depends on a
  non-empty digest, the no-op stub may reduce observability detail rather than
  fail. The current embedded profile already treats Performance Schema
  statement digest reporting as outside the product surface.
