# MyLite

**MySQL/MariaDB in a single file.**

> [!NOTE]
> **Status:** Early development.

## Overview

MyLite is an embedded MySQL/MariaDB drop-in built on a bundled MariaDB foundation.

At a glance:

| 💡 | ℹ️ |
| --- | --- |
| **Compatibility** | MariaDB LTS API (currently MariaDB 11.8) |
| **Storage** | Single `.mylite` file + transient MyLite-owned companions |
| **Engine** | MariaDB embedded runtime with a custom MyLite storage engine |
| **API** | A `libmylite` C library |
| **Validation** | Compatibility dashboard & extensive test suite |
| **Repository** | Monorepo for `libmylite`, tooling, protocol support, extensions, and integration wiring |
| **License** | GPL-2.0 (derived from MariaDB) |

## Goals

MyLite should make MariaDB semantics available in an embedded, file-owned
runtime. Here is a list of the main goals:

- **MySQL/MariaDB drop-in:** Work as an effortless drop-in replacement for MySQL/MariaDB.
- **Single file:** Keep the database portable as a single .mylite file.
- **Uncompromising compatibility:** Support the MySQL/MariaDB API surface that real applications depend on.
- **In-process runtime:** Execute SQL through `libmylite` without a database server.
- **Write concurrency:** Implement full write concurrency support in the MyLite storage.
- **Extensive test suite:** Create and maintain a large test suite.
- **Small profile:** Keep the minimum necessary slice of MariaDB codebase.
- **Coverage matrix:** Track MySQL/MariaDB functionality coverage in a detailed document.

## Compatibility

MyLite targets MySQL/MariaDB compatibility where that compatibility fits an embedded
single-file runtime. MyLite makes compatibility with MySQL and MariaDB a fundamental principle of the project. The compatibility is carefully evaluated, tracked, and covered with tests.

See [COMPATIBILITY.md](COMPATIBILITY.md) for the current compatibility status.

## Architecture

MyLite is build on MariaDB foundations with some custom key components.

At a glance:

| 💡 | ℹ️ |
| --- | --- |
| **MariaDB (`libmysqld`)** | MariaDB's `libmysqld` trimmed down to the necessary minimum. |
| **MyLite C API (libmylite)** | An embedded C API inspired by SQLite. |
| **MyLite storage** | A new single-file storage layer inspired by SQLite. |

MyLite is lean and significantly smaller than a full MariaDB build. Most notably:

- **No daemon.** MyLite is an embedded database without a server.
- **No networking.** MyLite exposes an embedded C API.
- **NO server management.** MyLite doesn't need to manage and maintain a running server.
- **No exotic features.** MyLite supports MySQL/MariaDB API extensively, but leaves out some special features and experiments.

### SQL pipeline

1. **Parse:** MariaDB parses supported SQL syntax.
2. **Analyze:** MariaDB resolves metadata, types, functions, warnings, errors,
   and statement semantics under MyLite's embedded configuration.
3. **Plan:** MariaDB chooses execution plans over MyLite engine tables and
   indexes.
4. **Execute:** The MyLite storage engine provides rows, indexes, catalog data,
   and transaction hooks from the `.mylite` file.

### Storage engine

MyLite implements a custom storage engine with the following properties:
- **Single file:** All database data is stored in a single `.mylite` file.
- **Companion files:** Companion files can be used, but only for journals, WAL, locks, shared memory, or temporary scratch work when part of the MyLite file lifecycle.
- **Safety:** The storage format must provide transaction and crash recovery guarantees.
- **Concurrency:** The storage must be capable to fully support write concurrency.

Major MySQL and MariaDB storage engines will be routed to the MyLite custom storage implementation. The current goal is to support `InnoDB`, `MyISAM`, and `Aria`. Zero-file storage engines like `MEMORY` and `BLACKHOLE` should be supported as well.

MyLite should also be capable of setting up a full in-memory database regardless of what engines are defined for table storage in the SQL. This should be supported via the special `:memory:` database filename.

### Integration packages

The repository is intended to hold the core library, as well as some surrounding integration packages. The main components are:

- The `libmylite` library code.
- MySQL/MariaDB wire protocol support.
- Command-line tooling.
- Language and runtime integration packages.
- Test suites.

## Development

MyLite contains project documentation, workflow guidance, and a mechanical
MariaDB Server 11.8.6 source import under `vendor/mariadb/server/`. It also
has a reproducible Linux-container build entry point for the current minimal
embedded MariaDB baseline:

```sh
tools/build-mariadb-minsize.sh
```

That command builds `build/mariadb-minsize/libmysqld/libmariadbd.a` and writes
`build/mariadb-minsize/mylite-build-report.txt` with toolchain, size, and
static plugin evidence. The current embedded bootstrap smoke can be run with:

```sh
tools/run-embedded-bootstrap-smoke.sh
```

That smoke starts MariaDB's embedded runtime in-process with controlled
temporary paths, runs `SELECT 1`, verifies explicit rejections for the first
unsupported server surfaces, shuts the runtime down, and records observed
startup side effects. Implementation work should keep MyLite changes narrow and
separate from upstream source imports.

The first `libmylite` open/close lifecycle smoke can be run with:

```sh
tools/run-libmylite-open-close-smoke.sh
```

That smoke builds the initial static `libmylite` wrapper, opens and closes a
placeholder `.mylite` path, verifies handle-owned diagnostics, and records the
current temporary runtime side effects.

The first static MyLite storage-engine registration smoke can be run with:

```sh
tools/run-storage-engine-smoke.sh
```

That smoke verifies that MariaDB's embedded plugin registry sees the built-in
`MYLITE` storage engine, discovers catalog-backed MyLite tables without durable
`.frm` sidecars, exercises supported DDL and row/index storage paths, and
verifies catalog-backed schema namespaces without creating schema directories.

Current design documents:

The grouped compatibility harness can be run with:

```sh
tools/run-compatibility-test-harness.sh
```

That harness runs the embedded lifecycle, `libmylite` lifecycle, storage,
catalog recovery, MariaDB reference comparison, and MyLite sidecar scan groups.
The reference comparison includes common application SQL over MyLite tables,
including joins, grouping, subqueries, unions, temporary CTAS, and
query-driven DML.

- [Roadmap](docs/ROADMAP.md) tracks the ordered engineering slices and current
  progress.
- [Storage engine compatibility matrix](docs/compatibility/storage-engine-compatibility-matrix.md)
  compares current MyLite behavior with InnoDB, MyISAM, and Aria.
- [MariaDB source analysis](docs/research/mariadb-source-analysis.md) records
  the initial source-level findings.
- [Single-file storage design](docs/architecture/single-file-storage.md)
  describes the target storage architecture.
- [libmylite C API](docs/api/libmylite-c-api.md) sketches the first public API.
- [Engineering standards](docs/architecture/engineering-standards.md) defines
  the project rules for fork hygiene, API design, tests, and documentation.

## References

- [MariaDB Server](https://github.com/MariaDB/server) is the upstream source
  repository.
- [MariaDB releases](https://mariadb.org/mariadb/all-releases/) list supported
  release lines and release status.
- [MariaDB source-code documentation](https://mariadb.com/docs/server/clients-and-utilities/server-client-software/download/getting-the-mariadb-source-code)
  explains how to get the MariaDB source.
- [Compiling MariaDB from source](https://mariadb.com/docs/server/server-management/install-and-upgrade-mariadb/compiling-mariadb-from-source/compiling-mariadb-from-source-the-master-guide)
  documents the upstream build process.
- [Embedded MariaDB interface](https://mariadb.com/docs/general-resources/development-articles/mariadb-internals/using-mariadb-with-your-programs-api/libmysqld/embedded-mariadb-interface)
  describes MariaDB's embedded server interface.
- [MariaDB storage-engine overview](https://mariadb.com/docs/server/server-usage/storage-engines/storage-engines-storage-engines-overview)
  introduces MariaDB storage engines.
- [MariaDB licensing FAQ](https://mariadb.com/docs/general-resources/community/community/faq/licensing-questions/licensing-faq)
  explains MariaDB's GPL licensing.
- [MariaDB trademark policy](https://mariadb.com/trademarks/) documents
  MariaDB trademark guidance.
- [MySQL legal policies](https://www.mysql.com/about/legal/) document MySQL
  trademark and legal guidance.

## License

MyLite is GPL-2.0 because it is derived from MariaDB.
