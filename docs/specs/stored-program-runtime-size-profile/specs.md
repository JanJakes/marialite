# Stored Program Runtime Size Profile

## Problem

The aggressive MyLite minsize profile already rejects stored routine, trigger,
and event DDL; returns empty stored routine Information Schema tables; disables
stored-function lookup; and omits PL/SQL cursor attribute item runtime.
However, MariaDB's stored-program parser support still roots substantial
stored-program implementation objects in the embedded archive and keeps about
100 KiB of stored-program symbols in the stripped open/close smoke.

Stored programs currently have no MyLite-owned catalog representation and no
single-file lifecycle design. The minsize profile should not retain the full
routine compiler and runtime if it can fail closed without changing ordinary
SQL execution.

## Source Findings

MariaDB source references are from the imported MariaDB Server tag
`mariadb-11.8.6` (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

- `vendor/mariadb/server/sql/sql_yacc.yy` includes `sp_head.h`,
  `sp_instr.h`, `sp_rcontext.h`, and `sp.h` in the generated parser. The
  generated MariaDB parser action function references `LEX::make_sp_head()`,
  `sp_head::add_instr()`, `sp_head::reset_lex()`,
  `sp_pcontext::add_variable()`, `sp_pcontext::find_variable()`, stored-program
  handler globals, and several `sp_instr_*` vtables.
- `vendor/mariadb/server/sql/sql_lex.cc` contains the stored-program semantic
  helpers used by parser actions: block setup/finalization, variable
  declarations, cursor declarations, assignment instruction creation, and
  routine/package creation helpers.
- `vendor/mariadb/server/sql/sp.cc` implements `Sp_handler` routine lookup,
  cache, create/drop, package resolution, and routine privilege/catalog
  plumbing.
- `vendor/mariadb/server/sql/sp_head.cc` implements stored-program compilation
  state, execution, parameter binding, table-list merging, optimization, and
  `SHOW CREATE` support.
- `vendor/mariadb/server/sql/sp_instr.cc` implements stored-program
  instruction execution and vtables for statement, jump, handler, cursor, set,
  return, and error instructions.
- `vendor/mariadb/server/sql/sp_pcontext.cc` and
  `vendor/mariadb/server/sql/sp_rcontext.cc` implement stored-program parse
  and runtime contexts, variables, handlers, cursor state, and condition
  handling.
- `vendor/mariadb/server/libmysqld/CMakeLists.txt` currently includes
  `sp.cc`, `sp_cache.cc`, `sp_head.cc`, `sp_pcontext.cc`, `sp_rcontext.cc`, and
  `sp_instr.cc` in `SQL_EMBEDDED_SOURCES`.

Current measurements from
`build/mariadb-minsize-no-query-logs`:

| Object | Bytes |
| --- | ---: |
| `sp_instr.cc.o` | 271,920 |
| `sp_head.cc.o` | 240,168 |
| `sp.cc.o` | 170,272 |
| `sp_rcontext.cc.o` | 166,312 |
| `sp_pcontext.cc.o` | 30,024 |
| `sp_cache.cc.o` | 11,664 |
| Total | 890,360 |

Removing those archive members from the merged static archive reduces
`libmariadbd.a` from 27,309,098 bytes to 26,383,128 bytes, an upper-bound
archive reduction of 925,970 bytes. The relink fails because generated parser
and `sql_lex.cc` references still need stored-program symbols, starting with:

- `sp_handler_procedure`,
- `sp_handler_function`,
- `sp_handler_package_spec`,
- `sp_handler_package_body`,
- `Row_definition_list::append_uniq()`,
- `sp_head::reset_lex()`,
- `sp_head::add_instr()`,
- `sp_head::new_cont_backpatch()`,
- `sp_head::do_cont_backpatch()`,
- `sp_pcontext::add_variable()`,
- `sp_pcontext::find_variable()`,
- `sp_pcontext::find_cursor()`, and
- vtables for `sp_instr_cclose` and related instruction classes.

The linked open/close smoke currently contains about 995 symbols matching
stored-program naming patterns, totaling about 101,646 bytes by symbol size.
That excludes parser action code inside `MYSQLparse()`, so the practical
linked-runtime opportunity may be above 100 KiB if parser actions are pruned.

## Scope

This slice may:

- add `MYLITE_DISABLE_STORED_PROGRAM_RUNTIME`,
- enable it from `tools/build-mariadb-minsize.sh`,
- reject stored routine, trigger, event, package, and compound-statement
  stored-program compilation paths at parse time or the earliest safe semantic
  action,
- replace stored-program runtime objects with fail-closed embedded stubs where
  generated parser or shared SQL code still needs symbols,
- omit the full `sp*.cc` runtime objects from the aggressive embedded archive
  if the stub set can satisfy ordinary SQL links,
- keep ordinary prepared statements, transactions, DDL, DML, expressions,
  table metadata, and MyLite catalog access working, and
- add smoke and symbol checks that the full stored-program runtime is absent.

## Non-Goals

This slice does not:

- add stored routine, trigger, event, or package support,
- design a MyLite routine catalog or file format,
- remove ordinary scalar expressions or native SQL functions,
- remove `CALL` syntax unless it is needed to fail closed consistently,
- remove unsupported-feature diagnostics already covered by earlier slices, or
- change non-minsize builds.

## Proposed Design

Use a fail-closed minsize profile rather than trying to keep a half-functional
stored-program compiler.

First, add `MYLITE_DISABLE_STORED_PROGRAM_RUNTIME` as a libmysqld CMake option
and define. Enable it in the minsize build after the already-applied routine
Information Schema, stored-function lookup, PL/SQL cursor-attribute, event,
trigger, and view runtime reductions.

Second, make the first reachable stored-program semantic actions return
`ER_NOT_SUPPORTED_YET` with a stable message such as
`stored program runtime in the MyLite minsize profile`. The high-value entry
points are `LEX::make_sp_head()`, `LEX::make_sp_head_no_recursive()`,
`LEX::create_package_start()`, event body setup, trigger body setup, and
standalone compound statement setup.

Third, attempt to remove `sp.cc`, `sp_cache.cc`, `sp_head.cc`,
`sp_pcontext.cc`, `sp_rcontext.cc`, and `sp_instr.cc` from
`SQL_EMBEDDED_SOURCES`, replacing them with a small
`mylite_stored_program_runtime_stub.cc`. The stub should implement only the
symbols still referenced by generated parser actions or retained SQL helpers.
Most methods should report unsupported behavior or return conservative failure
values. Parser-only helper methods may be inert if the profile aborts before
they can be used for successful stored-program compilation.

Fourth, if the full stub replacement proves too risky, fall back to guarding
large execution-only method bodies inside the existing `sp*.cc` files. That
fallback will save less, but it can still remove dead execution paths such as
stored-program instruction execution, cursor runtime, routine execution, and
condition handling while preserving parser object layout.

## Affected Subsystems

- Generated MariaDB parser actions from `sql_yacc.yy`.
- Stored-program semantic helpers in `sql_lex.cc`.
- Stored routine handlers, caches, parse contexts, runtime contexts, and
  instruction classes.
- Embedded minsize CMake source selection.
- MyLite open/close unsupported-profile and symbol checks.
- Production size analysis.

## Single-File and Embedded Lifecycle Impact

This aligns the aggressive profile with MyLite's current single-file scope:
stored programs have no MyLite catalog storage, no recovery model, and no file
lifecycle guarantees. The removed runtime would otherwise assume inherited
MariaDB routine metadata and sidecar-like server catalog behavior.

## Public API and File-Format Impact

No public `libmylite` API change and no `.mylite` file-format change.

SQL compatibility impact is high for stored-program surfaces: routines,
triggers, events, packages, and compound statements must remain explicitly
unsupported in the aggressive minsize profile.

## Binary-Size Impact

The archive upper bound from deleting the six `sp*.cc` objects is 925,970
bytes. The linked open/close smoke has about 101,646 bytes of visible
stored-program symbols, plus any stored-program parser action code inside
`MYSQLparse()`. A successful full stub replacement should therefore be judged
on both archive and stripped linked binary measurements.

Expected outcomes:

- Full stub replacement: potentially up to 0.88 MiB archive reduction and
  roughly 0.10 MiB or more stripped linked reduction before stub overhead.
- Execution-body fallback: lower risk, probably tens of KiB linked reduction,
  with much smaller archive impact because the original objects remain.

## License, Trademark, and Dependency Impact

This is a GPL-2.0-only MariaDB-derived build-profile change. It adds no new
dependency and changes no trademark-facing packaging.

## Test and Verification Plan

Run:

- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-stored-program-runtime MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-stored-program-runtime tools/run-libmylite-open-close-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-stored-program-runtime tools/run-storage-engine-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-stored-program-runtime tools/run-embedded-bootstrap-smoke.sh`
- `MYLITE_MARIADB_BUILD_DIR=build/mariadb-minsize-no-stored-program-runtime tools/run-compatibility-test-harness.sh`
- `bash -n tools/build-mariadb-minsize.sh`
- `bash -n tools/run-libmylite-open-close-smoke.sh`
- `git diff --check`

Measure:

- `libmariadbd.a` bytes,
- stripped and unstripped open/close smoke bytes,
- stripped and unstripped compatibility smoke bytes,
- remaining `sp*.cc.o` archive members,
- visible stored-program symbols in the linked smoke, and
- representative unsupported diagnostics for `CREATE PROCEDURE`,
  `CREATE FUNCTION`, `CREATE TRIGGER`, `CREATE EVENT`, packages, and compound
  statements.

## Acceptance Criteria

- Current open/close, storage, bootstrap, and compatibility smokes pass.
- Stored routine, trigger, event, package, and compound-statement surfaces fail
  explicitly in the aggressive minsize profile.
- Ordinary MyLite open/close, table creation, insert/select/update/delete,
  metadata, transactions, and native functions covered by existing smokes still
  work.
- The linked open/close smoke no longer defines representative full
  stored-program runtime symbols when the full stub replacement is successful.
- Size changes are recorded in
  `docs/research/production-size-analysis.md`.

## Risks and Unresolved Questions

- The generated parser is a single large function. Removing stored-program
  runtime objects without pruning parser actions may still leave parser-side
  code and references in the linked binary.
- `sp_head`, `sp_pcontext`, and `sp_instr` declarations are used by many SQL
  headers. A stub replacement must preserve enough ABI shape for retained
  compilation units without accidentally enabling partial stored-program
  behavior.
- Some ordinary SQL paths use shared helpers such as `sp_get_flags_for_command`
  or routine handler enums. These must remain conservative and fail closed.
- If a later MyLite release wants stored routines, this profile should remain
  an aggressive size option rather than the only architecture.
