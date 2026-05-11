# MyLite roadmap

This roadmap orders the first engineering slices and tracks implementation
progress. It is intentionally higher-level than the slice specs in
`docs/specs/`: each slice still needs its own source-linked design before code
lands.

## Status key

- `Done`: accepted and represented in the repository.
- `Planned`: expected work, but not started.
- `In progress`: active implementation or research.
- `Blocked`: waiting on a named prerequisite or decision.

## Current state

MyLite currently has project documentation, source analysis, architecture
direction, API sketches, workflow guidance, and a mechanical MariaDB Server
11.8.6 source import under `vendor/mariadb/server/`. It also has a
reproducible Docker-based minimal embedded MariaDB build profile that produces
`build/mariadb-minsize/libmysqld/libmariadbd.a`, plus an embedded bootstrap
smoke target that starts the runtime in-process and runs `SELECT 1` under
controlled temporary paths.

The next implementation step is `unsupported-server-surface`, which should
begin turning server-only behavior discovered during bootstrap into explicit
MyLite decisions rather than accidental inherited behavior.

## Implementation plan

| Order | Slice | Status | Purpose |
| --- | --- | --- | --- |
| 0 | Project foundation | Done | Define the product goal, GPL baseline, architecture direction, workflow, and initial research. |
| 1 | `upstream-11-8-import` | Done | Import a pinned MariaDB 11.8 LTS source tag mechanically and record upstream refs. |
| 2 | `build-profile-minsize` | Done | Produce a reproducible embedded build, record artifact size, and document which server-only or rare optional components are omitted by default. |
| 3 | `embedded-bootstrap` | Done | Start an in-process MariaDB-derived runtime under MyLite-owned defaults without exposing daemon administration as the library model. |
| 4 | `unsupported-server-surface` | In progress | Make daemon-only and unsupported features fail explicitly instead of leaking partial server behavior. |
| 5 | `libmylite-open-close` | Planned | Add the first public C API for opening and closing a `.mylite` file with handle-owned diagnostics. |
| 6 | `storage-engine-skeleton` | Planned | Add a static MyLite storage engine with enough handler shape for controlled smoke tests. |
| 7 | `mylite-engine-discovery` | Planned | Reopen table definitions from the MyLite catalog through MariaDB table-discovery APIs. |
| 8 | `ddl-metadata-routing` | Planned | Prove `CREATE`, `ALTER`, `DROP`, and `RENAME` do not leave durable `.frm` or schema-directory sidecars. |
| 9 | `single-file-catalog` | Planned | Store schema, table definitions, engine metadata, and catalog versioning inside the `.mylite` file. |
| 10 | `file-format-recovery` | Planned | Define and implement the first durable file header, page layout, journal or WAL lifecycle, transaction metadata, and crash recovery guarantees. |
| 11 | `row-index-storage` | Planned | Implement row storage, index access, autoincrement state, and core read/write handler methods. |
| 12 | `compatibility-test-harness` | Planned | Run embedded lifecycle, unexpected-sidecar detection, crash/reopen, and MariaDB comparison tests in repeatable groups. |

## Size and profile direction

MyLite should be smaller than a full MariaDB server distribution, but size is a
measured engineering constraint, not a slogan. The default embedded profile
should omit running-server-specific services and low-value optional components
that do not fit a local file-owned library, including:

- network listener and server account administration,
- replication, binlog, relay log, and Galera/wsrep,
- dynamic plugin loading and external durable storage engines,
- performance schema and server audit plugins,
- rarely used optional engines or plugins unless a slice justifies them.

The `build-profile-minsize` slice should establish the first baseline size and
the exact component list. Later slices should record meaningful size changes
when they add or remove runtime surface.

## Research still needed

The existing research is enough to start implementation, but several decisions
need focused slice-level research before code is written:

- reproducible Linux build environment and macOS secondary build notes,
- DDL metadata routing through `CREATE`, `ALTER`, `DROP`, and `RENAME`,
- pager, B-tree, transaction, and crash recovery design,
- journal or WAL placement, companion-file lifecycle, and write concurrency
  target,
- minimal system schema policy and replacement for server-only tables,
- first compatibility test subset and unexpected-sidecar detector.

These should be answered inside the relevant slice specs, not as detached
general research.
