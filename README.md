# MyLite

**MariaDB in a single file.**

> [!NOTE]
> **Status:** Early development.

## Overview

MyLite is an embedded MariaDB-derived database library with SQLite-like file
ownership semantics.

MyLite aims to keep MariaDB's SQL parser, analyzer, optimizer, execution
behavior, diagnostics, data types, collations, and DDL semantics while exposing
a local application-owned database file: open one `.mylite` file, execute SQL
in-process, and close it from the host application.

At a glance:

| 💡 | ℹ️ |
| --- | --- |
| **Compatibility** | MariaDB SQL semantics, initially based on MariaDB 11.8 LTS. |
| **Storage** | One primary durable `.mylite` file, with documented MyLite-owned companions when needed. |
| **Engine** | MariaDB embedded runtime with a MyLite storage engine. |
| **API** | `libmylite`, an in-process C library with explicit handle ownership. |
| **Validation** | MariaDB behavior checks plus embedded lifecycle and single-file tests. |
| **License** | GPL-2.0-only, because this project is derived from MariaDB Server. |

## Goals

MyLite should make MariaDB semantics available in an embedded, file-owned
runtime. Here is a list of the main goals:

- **MariaDB semantics:** Preserve MariaDB SQL behavior where practical.
- **Single file:** Expose one primary `.mylite` database file, with any
  journals, WAL files, locks, or temporary spill files owned and documented by
  MyLite.
- **In-process runtime:** Execute SQL through `libmylite` without a daemon,
  socket, or network handshake.
- **Explicit ownership:** Let applications open, configure, and close database
  handles directly.
- **Small embedded profile:** Omit daemon-only services, unrelated storage
  engines, and rare optional features from the default library build.
- **Measurable compatibility:** Use MariaDB tests and focused embedded tests to
  track behavior.

## Compatibility

MyLite targets MariaDB compatibility where that compatibility fits an embedded
single-file runtime.

The goal is not to reproduce a production MariaDB daemon inside an application.
Server-oriented features such as network users, replication, dynamic plugins,
and external durable engine files need explicit design before they can be
supported.

Write concurrency is valuable and should be preserved where the storage design
can do so safely. MyLite should not claim cross-process write concurrency until
locking and recovery are implemented and tested.

The default profile should be smaller than a full MariaDB server distribution by
leaving out running-server-specific services and low-value optional components
that do not fit a local file-owned library. Size claims must still be measured
against reproducible builds.

## Architecture

MyLite is a layered MariaDB fork with a MyLite-owned embedded API and storage
engine.

### Core library

- `libmylite` is the embedded runtime.
- MariaDB embedded server code provides the SQL parser, analyzer, optimizer,
  execution engine, diagnostics, type system, and collation behavior.
- MyLite owns the public file-oriented API and hides internal MariaDB handles
  behind explicit MyLite handles.
- The default profile avoids network server behavior, replication, dynamic
  plugin loading, external durable storage engines, Galera/wsrep,
  performance schema, server audit plugins, and similar daemon-oriented
  components.

### SQL pipeline

1. **Parse:** MariaDB parses supported SQL syntax.
2. **Analyze:** MariaDB resolves metadata, types, functions, warnings, errors,
   and statement semantics under MyLite's embedded configuration.
3. **Plan:** MariaDB chooses execution plans over MyLite engine tables and
   indexes.
4. **Execute:** The MyLite storage engine provides rows, indexes, catalog data,
   and transaction hooks from the `.mylite` file.

### Storage engine

- The MyLite engine is a static MariaDB storage engine.
- It stores table definitions, rows, indexes, and catalog metadata in the
  `.mylite` file, while owning any recovery companion files it needs.
- It uses MariaDB handler and table-discovery APIs where they fit.
- DDL metadata routing must prove `CREATE`, `ALTER`, `DROP`, and `RENAME` do
  not leave durable `.frm` sidecars.

### File format

- The primary database asset is one `.mylite` file.
- Documented companion files may be used for journals, WAL, locks, shared
  memory, or temporary scratch work when they are part of the MyLite file
  lifecycle.
- The file format must provide transaction and crash recovery guarantees before
  it stores user data.

### Integration packages

The repository is intended to hold the core library and surrounding integration
work:

- `libmylite` library code.
- Command-line and migration tooling.
- Language and runtime integration packages.
- MariaDB compatibility, embedded lifecycle, and single-file storage tests.

## Challenges

MariaDB already has embedded server support, but the embedded runtime still
starts much of a server-shaped system. MyLite needs to keep the useful SQL layer
while replacing the server filesystem and bootstrap model.

- **Bootstrap:** The embedded runtime must start without exposing a server
  administration model.
- **Catalog:** Schema and table metadata must live in the `.mylite` file
  instead of database directories or durable `.frm` sidecars.
- **Storage:** Existing durable engines such as Aria and InnoDB normally create
  sidecar files and are not the final storage answer.
- **Recovery:** The file format and any companion journal or WAL files need
  transaction and crash recovery guarantees before they store user data.
- **Size:** The library can be smaller than a full server distribution by
  omitting daemon-only and rare optional subsystems, but it will not be
  SQLite-small.

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

- [Roadmap](docs/ROADMAP.md) tracks the ordered engineering slices and current
  progress.
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

MyLite is GPL-2.0-only because it is derived from MariaDB Server.

SQLite-like file ownership does not imply SQLite-like licensing. Applications
that distribute MyLite as part of a combined work must account for GPL
obligations, and public packaging must avoid implying MariaDB or MySQL
affiliation.
