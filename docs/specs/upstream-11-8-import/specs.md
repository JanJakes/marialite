# upstream-11-8-import

## Problem Statement

MyLite needs a pinned MariaDB source base before implementation can change
embedded bootstrap, build profile, storage engine, or SQL-layer behavior. The
project currently has architecture and workflow documents only, so later code
work would otherwise rely on floating source references.

## Scope

- Import MariaDB Server 11.8 LTS source from an exact upstream tag.
- Record the upstream repository, release status, tag object, peeled commit,
  and branch head observed during import.
- Keep the import mechanical and separate from MyLite patches.
- Choose a source layout that preserves current MyLite documentation and
  keeps upstream-derived files easy to compare against MariaDB.

## Non-Goals

- Do not change MariaDB source as part of this slice.
- Do not add MyLite build files or compile options in the import commit.
- Do not claim an embedded build works before `build-profile-minsize`.
- Do not add `libmylite`, storage-engine, catalog, or bootstrap behavior yet.

## Source Findings

- Upstream repository: <https://github.com/MariaDB/server>
- Target tag: `mariadb-11.8.6`
- Tag object observed by `git ls-remote` on 2026-05-11:
  `2eeb8795e593db09241c1c5210fee34b3569ca47`
- Peeled commit observed by `git ls-remote` on 2026-05-11:
  `9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`
- Floating `11.8` branch head observed by `git ls-remote` on 2026-05-11:
  `04e09010773caf0b302b2933fff3fe95381a5e13`
- MariaDB.org listed 11.8.6 as Stable on the public release list on
  2026-05-11: <https://mariadb.org/mariadb/all-releases/>
- MariaDB release notes describe 11.8.6 as a Stable GA MariaDB Community Server
  11.8 release, released on 2026-02-04, and state that MariaDB 11.8 is a
  long-term release:
  <https://mariadb.com/docs/release-notes/community-server/11.8/11.8.6>
- Existing MyLite research inspected the `11.8` branch at
  `04e09010773caf0b302b2933fff3fe95381a5e13` and identified
  `mariadb-11.8.6` / `9bfea48ce1214cc4470f6f6f8a4e30352cef84e7` as the
  candidate initial import ref.

## Proposed Design

Import the upstream source archive for
`9bfea48ce1214cc4470f6f6f8a4e30352cef84e7` under:

```text
vendor/mariadb/server/
```

This keeps the existing MyLite root files (`README.md`, `LICENSE`, `docs/`,
`AGENTS.md`, and `.agents/`) first-party and avoids overwriting them with
upstream top-level files. It also gives later commits a clear boundary:

- pristine upstream source lives in `vendor/mariadb/server/`;
- MyLite documentation and first-party orchestration live at repository root;
- local MariaDB-derived patches can be reviewed by comparing
  `vendor/mariadb/server/` against the recorded upstream commit.

Use `git archive` from a temporary Git checkout of the pinned tag so the import
does not copy a nested `.git` directory.

## Affected Subsystems

- Source layout and patch-stack discipline.
- Future CMake/build work, because MariaDB's existing build root will be under
  `vendor/mariadb/server/`.
- Future upstream-diff tooling, which should compare that directory against
  the pinned upstream commit.

This slice intentionally does not affect embedded runtime, handler/storage
engine behavior, DDL metadata routing, catalog state, or public `libmylite`
APIs.

## DDL Metadata Routing Impact

None directly. Importing source makes later DDL metadata routing work concrete
by placing the relevant MariaDB files in the repository, but this slice does
not change table-definition writes, `.frm` handling, discovery hooks, or DDL
execution paths.

## Single-File And Embedded-Lifecycle Implications

This slice does not implement single-file behavior. It deliberately avoids
wrapping a MariaDB datadir or treating upstream embedded support as a final
MyLite runtime. The imported tree is the base for later changes that must
replace daemon-shaped bootstrap and persistent engine sidecars with MyLite-owned
file lifecycle rules.

## Public API Or File-Format Impact

None. No public MyLite ABI or `.mylite` file format is introduced here.

## Binary-Size Impact

The repository size will grow substantially because full MariaDB source is
imported. Runtime binary-size impact is not measured in this slice because no
build artifact is produced. `build-profile-minsize` must establish the first
embedded artifact baseline and component list.

## License, Trademark, And Dependency Impact

MariaDB Server is GPL-2.0-only, which is compatible with MyLite's documented
GPL-2.0-only baseline. The import must preserve upstream license and copyright
notices in the source tree. This slice adds no new third-party dependency
beyond the upstream MariaDB source that defines the project.

Public MyLite packaging still must avoid implying MariaDB or MySQL affiliation.

## Test And Verification Plan

- Verify the working tree is clean before import.
- Verify the target tag and peeled commit with `git ls-remote`.
- Fetch the target tag into a temporary checkout.
- Verify the fetched tag resolves to
  `9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`.
- Archive the pinned commit into `vendor/mariadb/server/`.
- Verify representative upstream files exist:
  - `vendor/mariadb/server/CMakeLists.txt`
  - `vendor/mariadb/server/libmysqld/CMakeLists.txt`
  - `vendor/mariadb/server/sql/handler.h`
  - `vendor/mariadb/server/sql/handler.cc`
  - `vendor/mariadb/server/include/mysql.h`
  - `vendor/mariadb/server/COPYING`
- Compare the imported tree against the fetched upstream commit with
  `git diff --no-index --quiet`.
- Do not run a full build in this slice; that belongs to
  `build-profile-minsize`.

## Verification Results

Completed on 2026-05-11:

- `git ls-remote --tags https://github.com/MariaDB/server.git
  refs/tags/mariadb-11.8.6 refs/tags/mariadb-11.8.6^{}` returned tag object
  `2eeb8795e593db09241c1c5210fee34b3569ca47` and peeled commit
  `9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`.
- `git ls-remote --heads https://github.com/MariaDB/server.git
  refs/heads/11.8` returned branch head
  `04e09010773caf0b302b2933fff3fe95381a5e13`.
- The imported tree under `vendor/mariadb/server/` was produced from
  `git archive` of `9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`.
- Representative source files were present after import, including
  `CMakeLists.txt`, `libmysqld/CMakeLists.txt`, `sql/handler.h`,
  `sql/handler.cc`, `include/mysql.h`, and `COPYING`.
- `git diff --no-index --quiet` between a fresh checkout of the pinned commit
  and `vendor/mariadb/server/` passed.
- The import commit contains only files under `vendor/mariadb/server/`.
- A full build was intentionally not run; that is part of
  `build-profile-minsize`.

## Acceptance Criteria

- `docs/specs/upstream-11-8-import/specs.md` records the selected upstream ref
  and source-layout decision.
- `vendor/mariadb/server/` contains a mechanical import of MariaDB Server
  `mariadb-11.8.6` at
  `9bfea48ce1214cc4470f6f6f8a4e30352cef84e7`.
- The import commit contains only upstream source files under
  `vendor/mariadb/server/`.
- MyLite docs are updated separately to mark the slice complete and record the
  import location.
- The imported tree compares cleanly against the fetched upstream commit.

## Risks And Unresolved Questions

- A vendored upstream directory is less like a traditional root-level fork, but
  it avoids clobbering MyLite's existing root documentation and keeps the first
  import mechanically clean. Later build slices can add root-level orchestration
  without editing upstream files prematurely.
- The repository will become large. That is expected for a source fork and
  should be managed with focused follow-up commits rather than partial imports.
- The full embedded build may still require Linux-container tooling and pinned
  dependencies. That is intentionally deferred to `build-profile-minsize`.
