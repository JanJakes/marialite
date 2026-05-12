# Type plugin size profile

## Problem

The current minimal embedded build still includes MariaDB's static `type_inet`,
`type_uuid`, and `type_geom` plugins. The production-size analysis found their
archive inputs are large enough to be worth testing:

- `plugin/type_inet/libtype_inet_embedded.a`: 2,384,608 bytes
- `plugin/type_uuid/libtype_uuid_embedded.a`: 1,066,660 bytes
- `type_geom` is smaller as a plugin archive, but it registers spatial
  information-schema plugins and geometry type support.

Passing `PLUGIN_TYPE_INET=NO`, `PLUGIN_TYPE_UUID=NO`, and `PLUGIN_TYPE_GEOM=NO`
did not remove the large plugins because they are declared mandatory in MariaDB
source.

## Source findings

Base source: MariaDB Server tag `mariadb-11.8.6`
(`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`).

Relevant source paths:

- `vendor/mariadb/server/cmake/plugin.cmake` treats `MANDATORY` plugins as
  always enabled by unsetting `PLUGIN_<NAME>` and setting it to `YES`.
- `vendor/mariadb/server/plugin/type_inet/CMakeLists.txt` declares
  `MYSQL_ADD_PLUGIN(type_inet ... MANDATORY RECOMPILE_FOR_EMBEDDED)`.
- `vendor/mariadb/server/plugin/type_uuid/CMakeLists.txt` declares
  `MYSQL_ADD_PLUGIN(type_uuid ... MANDATORY RECOMPILE_FOR_EMBEDDED)`.
- `vendor/mariadb/server/plugin/type_geom/CMakeLists.txt` declares
  `MYSQL_ADD_PLUGIN(TYPE_GEOM ... MANDATORY RECOMPILE_FOR_EMBEDDED)`.
- `vendor/mariadb/server/plugin/type_inet/plugin.cc` registers `INET4`,
  `INET6`, and IPv4/IPv6 functions.
- `vendor/mariadb/server/plugin/type_uuid/plugin.cc` registers the UUID data
  type and UUID functions.
- `vendor/mariadb/server/plugin/type_geom/plugin.cc` registers spatial
  information-schema tables and geometry type support.

## Design

Make these plugins optional for the MyLite embedded profile by removing their
mandatory CMake classification and setting them to `NO` in
`tools/build-mariadb-minsize.sh`.

The first attempt should be deliberately simple:

- change only plugin build declarations and the MyLite minsize build flags,
- do not remove parser tokens or SQL type code,
- let the build and current smoke tests prove whether unresolved references or
  startup assumptions remain.

If the simple attempt links and passes current smokes, record the size win and
document the compatibility cost. If it does not link, record the linker
evidence and leave the production build unchanged.

## Non-goals

- Do not remove generic SQL expression, type, geometry, or parser code in this
  slice.
- Do not claim full MariaDB compatibility for `INET4`, `INET6`, `UUID`, or
  spatial metadata when the plugins are disabled.
- Do not introduce replacement MyLite-specific data types.

## Compatibility impact

Disabling these plugins removes or changes support for MariaDB optional type
surfaces:

- `INET4` and `INET6` data types and IP address functions,
- `UUID` data type and UUID helper functions,
- spatial information-schema plugin tables and possibly geometry metadata
  behavior.

This is only acceptable as an aggressive size profile candidate. It should not
be treated as a free default-profile reduction unless compatibility policy
explicitly accepts those unsupported surfaces.

## Single-file and embedded-lifecycle impact

The slice should not change file format, catalog layout, storage pages,
locking, recovery, or `.mylite` open/close ownership. It changes only what SQL
plugins are built into the embedded server.

## Binary-size impact

Expected upper-bound archive inputs are roughly 3.45 MiB from `type_inet` and
`type_uuid`, plus whatever `type_geom` contributes. Actual linked savings may
be lower because shared SQL code can still reference generic geometry, UUID, or
network helpers.

The implementation must record archive bytes, linked open-close proxy bytes,
stripped proxy bytes, and the built-in plugin list.

## Test plan

Run:

```sh
MYLITE_BUILD_JOBS=8 tools/build-mariadb-minsize.sh
MYLITE_BUILD_JOBS=8 tools/run-libmylite-open-close-smoke.sh
MYLITE_BUILD_JOBS=8 tools/run-compatibility-test-harness.sh
```

Also inspect `build/mariadb-minsize/mylite-build-report.txt` and verify that
`builtin_maria_type_inet_plugin`, `builtin_maria_type_uuid_plugin`, and
`builtin_maria_type_geom_plugin` are absent when the profile succeeds.

## Acceptance criteria

- The build links without the three type plugins, or the blocker is documented
  with exact linker or startup evidence.
- Current MyLite smokes pass if the attempt is kept as code.
- Size deltas are recorded.
- Production-size analysis is updated with the measured result.

## Risks

- SQL layer code may directly reference plugin-defined type handlers or
  function builders, causing link failures.
- Parser support for removed data types may still exist but fail later at
  semantic resolution.
- Removing `type_geom` may have wider spatial side effects than its small plugin
  declaration suggests, because generic geometry code also exists in the SQL
  layer.
