# json-schema-valid-size-profile

## Problem

The minsize profile still links MariaDB's JSON Schema validator for the
`JSON_SCHEMA_VALID()` SQL function. MyLite keeps the general JSON type and JSON
path function surface, but JSON Schema Draft 2020 validation is a rare optional
surface for an embedded default profile and currently pulls in a dedicated
schema-keyword registry at process startup.

This slice tests whether MyLite can omit `JSON_SCHEMA_VALID()` from the default
minsize embedded profile without removing the ordinary JSON type and JSON
functions that are more central to MariaDB compatibility.

## Source baseline

- MariaDB Server tag `mariadb-11.8.6`
  (`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).
- Current MyLite size baseline after `section-gc-size-profile`:
  - `libmariadbd.a`: 36,520,566 bytes,
  - stripped `mylite-open-close-smoke`: 8,458,680 bytes,
  - linked `size` total: 8,755,392 bytes.

## Source findings

- MariaDB's official `JSON_SCHEMA_VALID()` documentation describes it as
  validating a JSON document against JSON Schema Draft 2020 and notes the
  function is available from MariaDB 11.1:
  - <https://mariadb.com/docs/server/reference/sql-functions/special-functions/json-functions/json_schema_valid>
- MariaDB's general JSON function documentation lists `JSON_SCHEMA_VALID`
  alongside the broader JSON function set, so this is a single JSON function
  surface rather than the whole JSON subsystem:
  - <https://mariadb.com/docs/server/reference/sql-functions/special-functions/json-functions>
- `vendor/mariadb/server/libmysqld/CMakeLists.txt` includes
  `../sql/item_jsonfunc.cc`, `../sql/json_schema.cc`, and
  `../sql/json_schema_helper.cc` in `SQL_EMBEDDED_SOURCES`.
- `vendor/mariadb/server/sql/item_create.cc:1481` declares
  `Create_func_json_schema_valid`, `item_create.cc:4872` defines its singleton
  and builder, and `item_create.cc:6468` registers `JSON_SCHEMA_VALID` in the
  native function array.
- `vendor/mariadb/server/sql/item_jsonfunc.h:875` declares
  `Item_func_json_schema_valid`, including `List<Json_schema_keyword>` members
  from `json_schema.h`.
- `vendor/mariadb/server/sql/item_jsonfunc.cc:4980` implements
  `Item_func_json_schema_valid::val_bool()`,
  `item_jsonfunc.cc:5052` implements schema parsing in
  `fix_length_and_dec()`, and `item_jsonfunc.cc:5097` cleans the keyword
  lists.
- `vendor/mariadb/server/sql/json_schema.cc:204` defines the
  `json_schema_func_array` keyword map and `json_schema.cc:2849` initializes
  the hash used by schema parsing.
- `vendor/mariadb/server/sql/mysqld.cc:4279` calls
  `setup_json_schema_keyword_hash()` during server initialization, and
  `mysqld.cc:2008` calls `cleanup_json_schema_keyword_hash()` during shutdown.
- Current archive member sizes in the section-GC profile:
  - `json_schema.cc.o`: 299,224 bytes,
  - `json_schema_helper.cc.o`: 6,336 bytes,
  - `item_jsonfunc.cc.o`: 652,216 bytes.
- Current linked symbols still include `Item_func_json_schema_valid`, many
  `Json_schema_*` classes, and `Create_func_json_schema_valid`, so section GC
  cannot drop the schema validator while the function is registered and startup
  hash initialization remains unconditional.

## Proposed design

Add an embedded-library CMake option:

```cmake
MYLITE_DISABLE_JSON_SCHEMA_VALID
```

When enabled:

- define `MYLITE_DISABLE_JSON_SCHEMA_VALID` for the embedded library sources,
- remove `../sql/json_schema.cc` and `../sql/json_schema_helper.cc` from
  `SQL_EMBEDDED_SOURCES`,
- guard `Create_func_json_schema_valid`, its singleton, its builder method, and
  the `JSON_SCHEMA_VALID` native function array entry in `item_create.cc`,
- guard `Item_func_json_schema_valid` declaration and method definitions in
  `item_jsonfunc.h` and `item_jsonfunc.cc`,
- guard the `json_schema.h` and `json_schema_helper.h` includes that become
  unnecessary in the disabled profile,
- skip `setup_json_schema_keyword_hash()` and
  `cleanup_json_schema_keyword_hash()` in `mysqld.cc`,
- set `-DMYLITE_DISABLE_JSON_SCHEMA_VALID=ON` in
  `tools/build-mariadb-minsize.sh`.

Do not remove `item_jsonfunc.cc`, `my_json_writer.cc`, `sql_type_json.cc`,
`json_table.cc`, or ordinary JSON function registrations. This is a narrow
single-function profile.

## Non-goals

- Do not remove the `JSON` type alias or `sql_type_json.cc`.
- Do not remove ordinary JSON scalar functions such as `JSON_VALID()`,
  `JSON_EXTRACT()`, `JSON_VALUE()`, `JSON_OBJECT()`, or `JSON_ARRAY()`.
- Do not remove `JSON_TABLE`; that is parser-backed table-function work and
  needs a separate slice.
- Do not change the full MariaDB server target behavior.
- Do not add a public `libmylite` API change.

## Affected subsystems

- Embedded library build graph.
- Native SQL function registration.
- JSON function implementation declarations.
- Embedded startup and shutdown hash lifecycle for JSON Schema keywords.
- `libmylite` open/close smoke coverage for unsupported minsize SQL surfaces.

## DDL metadata routing impact

No direct DDL routing change is expected. `JSON_SCHEMA_VALID()` can be used
inside a `CHECK` constraint expression in upstream MariaDB, so omitting it means
MyLite's minsize profile rejects such constraints through the unknown-function
path rather than persisting a constraint that cannot execute.

## Single-file and embedded-lifecycle impact

No file-format, catalog, lock, recovery, or lifecycle changes are expected.
Startup should get simpler because the JSON Schema keyword hash is not
initialized in the minsize profile.

## Public API and file-format impact

No public `libmylite` API change. No `.mylite` file-format change.

## Binary-size impact

Expected archive savings are bounded by `json_schema.cc.o`,
`json_schema_helper.cc.o`, and the linked portions of
`Item_func_json_schema_valid` and `Create_func_json_schema_valid`. The final
decision must use production artifact measurements because section GC may
already drop unrelated `item_jsonfunc.cc` sections.

## License, trademark, and dependency impact

No new dependency or license impact. The change removes MariaDB-derived JSON
Schema validation code from the default minsize profile but leaves the imported
source available in the tree.

## Test plan

Run:

```sh
MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

Add open/close smoke assertions that:

- `SELECT JSON_SCHEMA_VALID('{"type":"object"}', '{}')` fails in the minsize
  profile,
- the failure uses MariaDB's unknown-function path after the native builder is
  absent,
- representative retained JSON functions such as `JSON_VALID()` still work.

Measure:

```sh
stat -c "%s" build/mariadb-minsize/libmysqld/libmariadbd.a
llvm-ar t build/mariadb-minsize/libmysqld/libmariadbd.a | wc -l
stat -c "%s" build/mariadb-minsize/mylite/libmylite.a
stat -c "%s" build/mariadb-minsize/storage/mylite/libmylite_embedded.a
stat -c "%s" build/mariadb-minsize/mylite/mylite-open-close-smoke
cp build/mariadb-minsize/mylite/mylite-open-close-smoke \
  build/mariadb-minsize/mylite/mylite-open-close-smoke.stripped
llvm-strip --strip-unneeded \
  build/mariadb-minsize/mylite/mylite-open-close-smoke.stripped
stat -c "%s" build/mariadb-minsize/mylite/mylite-open-close-smoke.stripped
llvm-size build/mariadb-minsize/mylite/mylite-open-close-smoke
```

## Acceptance criteria

- The minsize build succeeds with `MYLITE_DISABLE_JSON_SCHEMA_VALID=ON`.
- The linked smoke binary no longer contains
  `Item_func_json_schema_valid`, `Create_func_json_schema_valid`, or
  `Json_schema_*` validator symbols.
- `JSON_SCHEMA_VALID()` is rejected in the minsize profile.
- At least one retained JSON function still succeeds in the open/close smoke.
- Build, open/close smoke, and compatibility harness pass.
- Size deltas are recorded in `docs/research/production-size-analysis.md`.

## Risks and unresolved questions

- Removing `JSON_SCHEMA_VALID()` is a SQL compatibility tradeoff. It is
  acceptable only for the minsize profile because the function is a rare
  validator surface compared with core JSON extraction and construction.
- If a future compatibility goal includes JSON Schema `CHECK` constraints, this
  profile decision must be revisited.
- `JSON_TABLE` and ordinary JSON functions remain; this slice does not make
  broad JSON compatibility claims.
