#!/usr/bin/env bash
set -euo pipefail

main() {
  if [[ "${1:-}" == "--inside-container" ]]; then
    run_inside_container
    return
  fi

  local repo_root
  repo_root="$(git rev-parse --show-toplevel)"

  local image
  image="${MYLITE_MARIADB_MINSIZE_IMAGE:-mylite-mariadb-minsize:ubuntu-24.04}"

  docker build \
    -t "${image}" \
    -f "${repo_root}/tools/docker/mariadb-minsize/Dockerfile" \
    "${repo_root}/tools/docker/mariadb-minsize"

  docker run --rm \
    --user "$(id -u):$(id -g)" \
    --volume "${repo_root}:/work" \
    --workdir /work \
    --env "MYLITE_MARIADB_BUILD_DIR=${MYLITE_MARIADB_BUILD_DIR:-build/mariadb-minsize}" \
    --env "MYLITE_BUILD_JOBS=${MYLITE_BUILD_JOBS:-}" \
    "${image}" \
    /work/tools/run-storage-engine-smoke.sh --inside-container
}

run_inside_container() {
  /work/tools/build-mariadb-minsize.sh --inside-container

  local build_dir="${MYLITE_MARIADB_BUILD_DIR:-build/mariadb-minsize}"
  local jobs="${MYLITE_BUILD_JOBS:-}"
  if [[ -z "${jobs}" ]]; then
    jobs="$(nproc)"
  fi

  cmake --build "${build_dir}" \
    --target mylite-storage-engine-smoke \
    --parallel "${jobs}"

  local abs_build_dir
  abs_build_dir="$(cd "${build_dir}" && pwd)"

  local runtime_dir="${abs_build_dir}/mylite-storage-engine"
  local report="${abs_build_dir}/mylite-storage-engine-report.txt"

  local smoke="${abs_build_dir}/mylite/mylite-storage-engine-smoke"
  local status=0

  run_smoke_phase \
    "${smoke}" \
    "${abs_build_dir}/sql/share" \
    "${runtime_dir}" \
    "${report}" \
    "${abs_build_dir}/mylite-storage-engine-output.log" \
    "" \
    "none" || status=1

  local catalog_root="${abs_build_dir}/mylite-catalog-persistence"
  local catalog_file="${catalog_root}/catalog.mylite"
  rm -rf "${catalog_root}"
  mkdir -p "${catalog_root}"

  run_smoke_phase \
    "${smoke}" \
    "${abs_build_dir}/sql/share" \
    "${catalog_root}/write" \
    "${abs_build_dir}/mylite-catalog-write-report.txt" \
    "${abs_build_dir}/mylite-catalog-write-output.log" \
    "${catalog_file}" \
    "write" || status=1

  run_smoke_phase \
    "${smoke}" \
    "${abs_build_dir}/sql/share" \
    "${catalog_root}/read" \
    "${abs_build_dir}/mylite-catalog-read-report.txt" \
    "${abs_build_dir}/mylite-catalog-read-output.log" \
    "${catalog_file}" \
    "read" || status=1
  verify_row_page_storage \
    "${catalog_file}" \
    "${abs_build_dir}/mylite-catalog-read-report.txt" || status=1

  local transaction_root="${abs_build_dir}/mylite-transaction-boundary"
  local transaction_file="${transaction_root}/catalog.mylite"
  rm -rf "${transaction_root}"
  mkdir -p "${transaction_root}"

  run_smoke_phase \
    "${smoke}" \
    "${abs_build_dir}/sql/share" \
    "${transaction_root}/write" \
    "${abs_build_dir}/mylite-transaction-boundary-write-report.txt" \
    "${abs_build_dir}/mylite-transaction-boundary-write-output.log" \
    "${transaction_file}" \
    "transaction-write" || status=1

  run_smoke_phase \
    "${smoke}" \
    "${abs_build_dir}/sql/share" \
    "${transaction_root}/read" \
    "${abs_build_dir}/mylite-transaction-boundary-read-report.txt" \
    "${abs_build_dir}/mylite-transaction-boundary-read-output.log" \
    "${transaction_file}" \
    "transaction-read" || status=1

  local legacy_root="${abs_build_dir}/mylite-catalog-legacy-v2"
  local legacy_file="${legacy_root}/catalog.mylite"
  rm -rf "${legacy_root}"
  mkdir -p "${legacy_root}"

  if write_legacy_v2_catalog_fixture "${legacy_file}"; then
    verify_legacy_v2_catalog_fixture \
      "${legacy_file}" \
      "${abs_build_dir}/mylite-catalog-legacy-v2-fixture-report.txt" || status=1

    run_smoke_phase \
      "${smoke}" \
      "${abs_build_dir}/sql/share" \
      "${legacy_root}/write" \
      "${abs_build_dir}/mylite-catalog-legacy-v2-write-report.txt" \
      "${abs_build_dir}/mylite-catalog-legacy-v2-write-output.log" \
      "${legacy_file}" \
      "write" || status=1
    verify_legacy_v2_catalog_rewrite \
      "${legacy_file}" \
      "${abs_build_dir}/mylite-catalog-legacy-v2-write-report.txt" || status=1

    run_smoke_phase \
      "${smoke}" \
      "${abs_build_dir}/sql/share" \
      "${legacy_root}/read" \
      "${abs_build_dir}/mylite-catalog-legacy-v2-read-report.txt" \
      "${abs_build_dir}/mylite-catalog-legacy-v2-read-output.log" \
      "${legacy_file}" \
      "read" || status=1
    verify_row_page_storage \
      "${legacy_file}" \
      "${abs_build_dir}/mylite-catalog-legacy-v2-read-report.txt" || status=1
  else
    status=1
  fi

  local recovery_root="${abs_build_dir}/mylite-catalog-recovery"
  local recovery_file="${recovery_root}/catalog.mylite"
  rm -rf "${recovery_root}"
  mkdir -p "${recovery_root}"

  run_smoke_phase \
    "${smoke}" \
    "${abs_build_dir}/sql/share" \
    "${recovery_root}/base" \
    "${abs_build_dir}/mylite-catalog-recovery-base-report.txt" \
    "${abs_build_dir}/mylite-catalog-recovery-base-output.log" \
    "${recovery_file}" \
    "recovery-base" || status=1

  local recovery_orphan_offset
  recovery_orphan_offset="$(stat -c %s "${recovery_file}")"

  run_smoke_phase \
    "${smoke}" \
    "${abs_build_dir}/sql/share" \
    "${recovery_root}/latest" \
    "${abs_build_dir}/mylite-catalog-recovery-latest-report.txt" \
    "${abs_build_dir}/mylite-catalog-recovery-latest-output.log" \
    "${recovery_file}" \
    "recovery-latest" || status=1

  local recovery_latest_size
  recovery_latest_size="$(stat -c %s "${recovery_file}")"
  local recovery_corrupt_offset=""
  if ! recovery_corrupt_offset="$(latest_catalog_payload_offset "${recovery_file}")"; then
    status=1
  fi
  if [[ -n "${recovery_corrupt_offset}" ]]; then
    corrupt_latest_generation_page "${recovery_file}" "${recovery_corrupt_offset}" || status=1
  fi

  run_smoke_phase \
    "${smoke}" \
    "${abs_build_dir}/sql/share" \
    "${recovery_root}/read" \
    "${abs_build_dir}/mylite-catalog-recovery-read-report.txt" \
    "${abs_build_dir}/mylite-catalog-recovery-read-output.log" \
    "${recovery_file}" \
    "recovery-read" || status=1
  verify_orphan_page_reclaim \
    "${recovery_file}" \
    "${abs_build_dir}/mylite-catalog-recovery-read-report.txt" \
    "${recovery_orphan_offset}" \
    "${recovery_latest_size}" || status=1

  printf "Storage engine smoke report: %s\n" "${report}"
  printf "Catalog write smoke report: %s\n" "${abs_build_dir}/mylite-catalog-write-report.txt"
  printf "Catalog read smoke report: %s\n" "${abs_build_dir}/mylite-catalog-read-report.txt"
  printf "Transaction boundary write smoke report: %s\n" "${abs_build_dir}/mylite-transaction-boundary-write-report.txt"
  printf "Transaction boundary read smoke report: %s\n" "${abs_build_dir}/mylite-transaction-boundary-read-report.txt"
  printf "Legacy v2 catalog fixture report: %s\n" "${abs_build_dir}/mylite-catalog-legacy-v2-fixture-report.txt"
  printf "Legacy v2 catalog write smoke report: %s\n" "${abs_build_dir}/mylite-catalog-legacy-v2-write-report.txt"
  printf "Legacy v2 catalog read smoke report: %s\n" "${abs_build_dir}/mylite-catalog-legacy-v2-read-report.txt"
  printf "Catalog recovery base smoke report: %s\n" "${abs_build_dir}/mylite-catalog-recovery-base-report.txt"
  printf "Catalog recovery latest smoke report: %s\n" "${abs_build_dir}/mylite-catalog-recovery-latest-report.txt"
  printf "Catalog recovery read smoke report: %s\n" "${abs_build_dir}/mylite-catalog-recovery-read-report.txt"
  return "${status}"
}

verify_legacy_v2_catalog_fixture() {
  local catalog_file="$1"
  local report="$2"

  if [[ ! -f "${catalog_file}" ]]; then
    printf "Legacy v2 catalog fixture does not exist: %s\n" "${catalog_file}" >&2
    return 1
  fi

  python3 - "${catalog_file}" "${report}" <<'PY'
import struct
import sys
from pathlib import Path

catalog_file = Path(sys.argv[1])
report = Path(sys.argv[2])
data = catalog_file.read_bytes()
page_size = 4096
header_checksum_offset = 56
page_checksum_offset = 56
page_payload_offset = 64
fnv_offset_basis = 14695981039346656037
fnv_prime = 1099511628211


def fail(message):
    print(message, file=sys.stderr)
    sys.exit(1)


def checksum(payload):
    result = fnv_offset_basis
    for byte in payload:
        result ^= byte
        result = (result * fnv_prime) & 0xffffffffffffffff
    return result


def read_u32(payload, offset):
    return struct.unpack_from("<I", payload, offset)[0]


def read_u64(payload, offset):
    return struct.unpack_from("<Q", payload, offset)[0]


def read_chain(root_offset, length, expected_type):
    page_id = root_offset // page_size
    remaining = length
    payload = bytearray()
    while remaining > 0:
        offset = page_id * page_size
        page = bytearray(data[offset:offset + page_size])
        if len(page) != page_size:
            fail("short legacy fixture page chain")
        if page[0:16] != b"MYLITEPAGESTORE\0":
            fail("invalid legacy fixture page magic")
        stored_page_checksum = read_u64(page, page_checksum_offset)
        struct.pack_into("<Q", page, page_checksum_offset, 0)
        if checksum(page) != stored_page_checksum:
            fail("invalid legacy fixture page checksum")
        if read_u32(page, 16) != 1 or read_u32(page, 20) != expected_type:
            fail("invalid legacy fixture page header")
        payload_length = read_u32(page, 40)
        expected_length = min(remaining, page_size - page_payload_offset)
        if payload_length != expected_length:
            fail("invalid legacy fixture payload length")
        payload.extend(page[page_payload_offset:page_payload_offset + payload_length])
        remaining -= payload_length
        next_page_id = read_u64(page, 32)
        if remaining == 0:
            if next_page_id != 0:
                fail("unexpected legacy fixture trailing page")
            break
        page_id = next_page_id
    return bytes(payload)


header = bytearray(data[0:page_size])
if len(header) != page_size:
    fail("short legacy fixture header")
if header[0:16] != b"MYLITEFMTPAGE2\0\0":
    fail("invalid legacy fixture header magic")
if read_u32(header, 16) != 2 or read_u32(header, 20) != page_size:
    fail("legacy fixture header is not v2")
stored_header_checksum = read_u64(header, header_checksum_offset)
struct.pack_into("<Q", header, header_checksum_offset, 0)
if checksum(header) != stored_header_checksum:
    fail("invalid legacy fixture header checksum")

generation = read_u64(header, 24)
payload_offset = read_u64(header, 32)
payload_length = read_u64(header, 40)
payload_checksum = read_u64(header, 48)
if generation != 1:
    fail("unexpected legacy fixture generation")

catalog_payload = read_chain(payload_offset, payload_length, 1)
if checksum(catalog_payload) != payload_checksum:
    fail("invalid legacy fixture catalog checksum")
try:
    catalog_lines = catalog_payload.decode("ascii").splitlines()
except UnicodeDecodeError:
    fail("legacy fixture catalog payload is not ascii")
freepage_records = [
    line for line in catalog_lines if line.startswith("FREEPAGE\t")
]
if not freepage_records:
    fail("legacy fixture catalog has no FREEPAGE records")

with report.open("w") as out:
    out.write("# MyLite Legacy V2 Catalog Fixture Report\n\n")
    out.write("status=0\n")
    out.write("format_version=2\n")
    out.write(f"generation={generation}\n")
    out.write(f"catalog_freepage_records={len(freepage_records)}\n")
PY
}

run_smoke_phase() {
  local smoke="$1"
  local lc_messages_dir="$2"
  local runtime_dir="$3"
  local report="$4"
  local smoke_log="$5"
  local catalog_file="$6"
  local persistence_phase="$7"

  local datadir="${runtime_dir}/datadir"
  local tmpdir="${runtime_dir}/tmp"

  rm -rf "${runtime_dir}"
  mkdir -p "${datadir}/mylite" "${tmpdir}"
  rm -f "${smoke_log}" "${report}"

  local args=(
    "--datadir=${datadir}"
    "--tmpdir=${tmpdir}"
    "--lc-messages-dir=${lc_messages_dir}"
    "--runtime-dir=${runtime_dir}"
    "--persistence-phase=${persistence_phase}"
    "--report=${report}"
  )
  if [[ -n "${catalog_file}" ]]; then
    args+=("--catalog-file=${catalog_file}")
  fi

  local status=0
  "${smoke}" "${args[@]}" > "${smoke_log}" 2>&1 || status=$?

  append_observed_files "${runtime_dir}" "${report}" "${smoke_log}"
  append_catalog_files "${catalog_file}" "${report}"
  if [[ -n "${catalog_file}" && -e "${catalog_file}.tmp" ]]; then
    status=1
  fi
  if has_frm_artifacts "${runtime_dir}"; then
    status=1
  fi
  return "${status}"
}

latest_catalog_payload_offset() {
  local catalog_file="$1"

  if [[ ! -f "${catalog_file}" ]]; then
    printf "Catalog recovery file does not exist: %s\n" "${catalog_file}" >&2
    return 1
  fi

  python3 - "${catalog_file}" <<'PY'
import struct
import sys
from pathlib import Path

catalog_file = Path(sys.argv[1])
data = catalog_file.read_bytes()
page_size = 4096

def fail(message):
    print(message, file=sys.stderr)
    raise SystemExit(1)

def read_u32(page, offset):
    return struct.unpack_from("<I", page, offset)[0]

def read_u64(page, offset):
    return struct.unpack_from("<Q", page, offset)[0]

headers = []
for slot in (0, 1):
    start = slot * page_size
    page = data[start:start + page_size]
    if len(page) != page_size:
        continue
    if page[:16] != b"MYLITEFMTPAGE2\0\0":
        continue
    format_version = read_u32(page, 16)
    if format_version not in (2, 3) or read_u32(page, 20) != page_size:
        continue
    generation = read_u64(page, 24)
    payload_offset = read_u64(page, 32)
    payload_length = read_u64(page, 40)
    if generation == 0 or payload_offset < page_size * 2:
        continue
    if payload_offset % page_size != 0 or payload_length == 0:
        continue
    if payload_offset + page_size > len(data):
        continue
    headers.append((generation, payload_offset))

if not headers:
    fail("no catalog generation header to corrupt")

headers.sort(reverse=True)
print(headers[0][1])
PY
}

corrupt_latest_generation_page() {
  local catalog_file="$1"
  local corrupt_offset="$2"

  if [[ ! -f "${catalog_file}" ]]; then
    printf "Catalog recovery file does not exist: %s\n" "${catalog_file}" >&2
    return 1
  fi
  if [[ -z "${corrupt_offset}" || "${corrupt_offset}" -lt 8192 ]]; then
    printf "Invalid catalog generation corruption offset: %s\n" "${corrupt_offset}" >&2
    return 1
  fi

  printf '\0' | dd of="${catalog_file}" bs=1 seek="${corrupt_offset}" count=1 conv=notrunc status=none
}

write_legacy_v2_catalog_fixture() {
  local catalog_file="$1"

  python3 - "${catalog_file}" <<'PY'
import struct
import sys
from pathlib import Path

catalog_file = Path(sys.argv[1])
page_size = 4096
payload_offset = page_size * 2
header_checksum_offset = 56
page_checksum_offset = 56
page_payload_offset = 64
fnv_offset_basis = 14695981039346656037
fnv_prime = 1099511628211


def checksum(data):
    result = fnv_offset_basis
    for byte in data:
        result ^= byte
        result = (result * fnv_prime) & 0xffffffffffffffff
    return result


def store_u32(data, offset, value):
    struct.pack_into("<I", data, offset, value)


def store_u64(data, offset, value):
    struct.pack_into("<Q", data, offset, value)


payload = b"MYLITE CATALOG 1\nFREEPAGE\t3\t1\n"
data = bytearray(page_size * 4)

page = bytearray(page_size)
page[0:16] = b"MYLITEPAGESTORE\0"
store_u32(page, 16, 1)
store_u32(page, 20, 1)
store_u64(page, 24, 2)
store_u64(page, 32, 0)
store_u32(page, 40, len(payload))
page[page_payload_offset:page_payload_offset + len(payload)] = payload
store_u64(page, page_checksum_offset, 0)
store_u64(page, page_checksum_offset, checksum(page))
data[payload_offset:payload_offset + page_size] = page

header = bytearray(page_size)
header[0:16] = b"MYLITEFMTPAGE2\0\0"
store_u32(header, 16, 2)
store_u32(header, 20, page_size)
store_u64(header, 24, 1)
store_u64(header, 32, payload_offset)
store_u64(header, 40, len(payload))
store_u64(header, 48, checksum(payload))
store_u64(header, header_checksum_offset, 0)
store_u64(header, header_checksum_offset, checksum(header))
data[0:page_size] = header

catalog_file.parent.mkdir(parents=True, exist_ok=True)
catalog_file.write_bytes(data)
PY
}

verify_legacy_v2_catalog_rewrite() {
  local catalog_file="$1"
  local report="$2"

  if [[ ! -f "${catalog_file}" ]]; then
    printf "Legacy v2 catalog file does not exist: %s\n" "${catalog_file}" >&2
    return 1
  fi

  python3 - "${catalog_file}" "${report}" <<'PY'
import struct
import sys
from pathlib import Path

catalog_file = Path(sys.argv[1])
report = Path(sys.argv[2])
data = catalog_file.read_bytes()
page_size = 4096
header_checksum_offset = 56
page_checksum_offset = 56
page_payload_offset = 64
fnv_offset_basis = 14695981039346656037
fnv_prime = 1099511628211


def fail(message):
    print(message, file=sys.stderr)
    sys.exit(1)


def checksum(payload):
    result = fnv_offset_basis
    for byte in payload:
        result ^= byte
        result = (result * fnv_prime) & 0xffffffffffffffff
    return result


def read_u32(payload, offset):
    return struct.unpack_from("<I", payload, offset)[0]


def read_u64(payload, offset):
    return struct.unpack_from("<Q", payload, offset)[0]


def page_range(root_offset, length):
    return (
        root_offset // page_size,
        (length + page_size - page_payload_offset - 1)
        // (page_size - page_payload_offset)
    )


def read_chain(root_offset, length, expected_type):
    if root_offset % page_size != 0 or root_offset < page_size * 2:
        fail("invalid chain root offset")
    if length == 0:
        fail("invalid chain length")

    page_id = root_offset // page_size
    remaining = length
    payload = bytearray()
    page_types = []
    while remaining > 0:
        offset = page_id * page_size
        page = bytearray(data[offset:offset + page_size])
        if len(page) != page_size:
            fail("short page chain read")
        if page[0:16] != b"MYLITEPAGESTORE\0":
            fail("invalid page magic")
        stored_page_checksum = read_u64(page, page_checksum_offset)
        struct.pack_into("<Q", page, page_checksum_offset, 0)
        if checksum(page) != stored_page_checksum:
            fail("invalid page checksum")
        if read_u32(page, 16) != 1:
            fail("invalid page format version")
        page_type = read_u32(page, 20)
        if page_type != expected_type:
            fail("unexpected page type")
        if read_u64(page, 24) != page_id:
            fail("unexpected page id")
        payload_length = read_u32(page, 40)
        expected_length = min(remaining, page_size - page_payload_offset)
        if payload_length != expected_length:
            fail("unexpected page payload length")
        payload.extend(page[page_payload_offset:page_payload_offset + payload_length])
        page_types.append(page_type)
        remaining -= payload_length
        next_page_id = read_u64(page, 32)
        if remaining == 0:
            if next_page_id != 0:
                fail("unexpected trailing page")
            break
        if next_page_id != page_id + 1:
            fail("unexpected next page")
        page_id = next_page_id
    return bytes(payload), page_types


headers = []
for slot in (0, 1):
    offset = slot * page_size
    page = bytearray(data[offset:offset + page_size])
    if len(page) != page_size:
        continue
    if page[0:16] != b"MYLITEFMTPAGE2\0\0":
        continue
    format_version = read_u32(page, 16)
    if format_version not in (2, 3) or read_u32(page, 20) != page_size:
        continue
    stored_header_checksum = read_u64(page, header_checksum_offset)
    struct.pack_into("<Q", page, header_checksum_offset, 0)
    if checksum(page) != stored_header_checksum:
        continue
    generation = read_u64(page, 24)
    if generation == 0:
        continue
    free_offset = read_u64(page, 64) if format_version == 3 else 0
    free_length = read_u64(page, 72) if format_version == 3 else 0
    headers.append((
        generation, slot, format_version, read_u64(page, 32),
        read_u64(page, 40), free_offset, free_length
    ))

if not headers:
    fail("no valid catalog headers after legacy v2 rewrite")

headers.sort(reverse=True)
header_formats = sorted({header[2] for header in headers})
if headers[0][2] != 3:
    fail("legacy v2 catalog was not rewritten to v3")

catalog_payload, _ = read_chain(headers[0][3], headers[0][4], 1)
try:
    catalog_lines = catalog_payload.decode("ascii").splitlines()
except UnicodeDecodeError:
    fail("catalog payload is not ascii after legacy rewrite")
catalog_freepage_records = [
    line for line in catalog_lines if line.startswith("FREEPAGE\t")
]
if catalog_freepage_records:
    fail("rewritten v3 catalog still contains FREEPAGE records")

free_payload, free_page_types = read_chain(headers[0][5], headers[0][6], 4)
try:
    free_lines = free_payload.decode("ascii").splitlines()
except UnicodeDecodeError:
    fail("free page payload is not ascii after legacy rewrite")
if not free_lines or free_lines[0] != "MYLITE FREE LIST 1":
    fail("rewritten v3 free page payload magic is invalid")
freepage_records = [
    line for line in free_lines if line.startswith("FREEPAGE\t")
]
if not freepage_records:
    fail("rewritten v3 free page payload has no FREEPAGE records")

with report.open("a") as out:
    latest_catalog_range = page_range(headers[0][3], headers[0][4])
    out.write("\n## Legacy V2 Catalog Rewrite\n\n")
    out.write("status=0\n")
    out.write(
        f"header_formats_after_rewrite={','.join(map(str, header_formats))}\n"
    )
    out.write(f"latest_format_version={headers[0][2]}\n")
    out.write(
        f"latest_catalog_range={latest_catalog_range[0]}:"
        f"{latest_catalog_range[1]}\n"
    )
    out.write(f"catalog_freepage_records={len(catalog_freepage_records)}\n")
    out.write(f"freepage_records={len(freepage_records)}\n")
    out.write(f"free_payload_page_types={','.join(map(str, free_page_types))}\n")
PY
}

verify_row_page_storage() {
  local catalog_file="$1"
  local report="$2"

  if [[ ! -f "${catalog_file}" ]]; then
    printf "Catalog file does not exist: %s\n" "${catalog_file}" >&2
    return 1
  fi

  python3 - "${catalog_file}" "${report}" <<'PY'
import struct
import sys
from pathlib import Path

catalog_file = Path(sys.argv[1])
report = Path(sys.argv[2])
data = catalog_file.read_bytes()
page_size = 4096
page_payload_offset = 64
page_payload_capacity = page_size - page_payload_offset

def fail(message):
    with report.open("a") as out:
        out.write("\n## Row And Index Page Storage\n\n")
        out.write(f"status=1\nmessage={message}\n")
    raise SystemExit(1)

def read_u32(page, offset):
    return struct.unpack_from("<I", page, offset)[0]

def read_u64(page, offset):
    return struct.unpack_from("<Q", page, offset)[0]

headers = []
for slot in (0, 1):
    start = slot * page_size
    page = data[start:start + page_size]
    if len(page) != page_size:
        continue
    if page[:16] != b"MYLITEFMTPAGE2\0\0":
        continue
    format_version = read_u32(page, 16)
    if format_version not in (2, 3) or read_u32(page, 20) != page_size:
        continue
    generation = read_u64(page, 24)
    if generation == 0:
        continue
    free_offset = read_u64(page, 64) if format_version == 3 else 0
    free_length = read_u64(page, 72) if format_version == 3 else 0
    headers.append((
        generation, slot, format_version, read_u64(page, 32),
        read_u64(page, 40), free_offset, free_length
    ))

if not headers:
    fail("no valid header slots")

headers.sort(reverse=True)

def page_count(length):
    return (length + page_payload_capacity - 1) // page_payload_capacity

def page_range(root_offset, length):
    return (root_offset // page_size, page_count(length))

def ranges_overlap(left, right):
    left_start, left_count = left
    right_start, right_count = right
    return (
        left_start < right_start + right_count and
        right_start < left_start + left_count
    )

def range_contains(outer, inner):
    outer_start, outer_count = outer
    inner_start, inner_count = inner
    return (
        outer_start <= inner_start and
        inner_start + inner_count <= outer_start + outer_count
    )

def parse_freepage_records(catalog_lines):
    ranges = []
    for line in catalog_lines:
        if not line.startswith("FREEPAGE\t"):
            continue
        parts = line.split("\t")
        if len(parts) != 3:
            fail("invalid FREEPAGE record")
        page_id = int(parts[1])
        page_count_value = int(parts[2])
        if page_id < 2 or page_count_value == 0:
            fail("invalid FREEPAGE range")
        ranges.append((page_id, page_count_value))
    ranges.sort()
    for previous, current in zip(ranges, ranges[1:]):
        if previous[0] + previous[1] >= current[0]:
            fail("overlapping FREEPAGE ranges")
    return ranges

def read_chain(root_offset, length, expected_type, exact_payload_lengths):
    if root_offset < page_size * 2 or root_offset % page_size != 0:
        fail("invalid page-chain root offset")
    if length == 0:
        fail("empty page chain")

    page_id = root_offset // page_size
    remaining = length
    payload = bytearray()
    page_types = []
    page_payloads = []
    while remaining > 0:
        start = page_id * page_size
        page = data[start:start + page_size]
        if len(page) != page_size:
            fail("short page-chain read")
        if page[:16] != b"MYLITEPAGESTORE\0":
            fail("invalid page magic")
        page_type = read_u32(page, 20)
        stored_page_id = read_u64(page, 24)
        next_page_id = read_u64(page, 32)
        used = read_u32(page, 40)
        if page_type != expected_type:
            fail("unexpected page type")
        if stored_page_id != page_id:
            fail("unexpected page id")
        if exact_payload_lengths:
            expected_used = min(remaining, page_payload_capacity)
            if used != expected_used:
                fail("unexpected page payload length")
        elif used == 0 or used > remaining:
            fail("unexpected page payload length")
        page_types.append(page_type)
        page_payload = page[page_payload_offset:page_payload_offset + used]
        page_payloads.append(bytes(page_payload))
        payload.extend(page_payload)
        remaining -= used
        if remaining == 0:
            if next_page_id != 0:
                fail("nonzero terminal next page")
            break
        if next_page_id != page_id + 1:
            fail("nonsequential next page")
        page_id = next_page_id
    return bytes(payload), page_types, page_payloads

catalog_payload, catalog_page_types, _ = read_chain(
    headers[0][3], headers[0][4], 1, True
)
try:
    catalog_lines = catalog_payload.decode("ascii").splitlines()
except UnicodeDecodeError:
    fail("catalog payload is not ascii")

catalog_format_version = headers[0][2]
catalog_row_records = [line for line in catalog_lines if line.startswith("ROW\t")]
catalog_freepage_records = [
    line for line in catalog_lines if line.startswith("FREEPAGE\t")
]
rowpage_records = [line for line in catalog_lines if line.startswith("ROWPAGE\t")]
indexpage_records = [line for line in catalog_lines if line.startswith("INDEXPAGE\t")]
if catalog_row_records:
    fail("catalog still contains ROW records")
if not rowpage_records:
    fail("catalog has no ROWPAGE records")
if not indexpage_records:
    fail("catalog has no INDEXPAGE records")
if catalog_format_version == 3:
    if catalog_freepage_records:
        fail("v3 catalog still contains FREEPAGE records")
    free_payload, free_page_types, _ = read_chain(
        headers[0][5], headers[0][6], 4, True
    )
    try:
        free_lines = free_payload.decode("ascii").splitlines()
    except UnicodeDecodeError:
        fail("free page payload is not ascii")
    if not free_lines or free_lines[0] != "MYLITE FREE LIST 1":
        fail("invalid free page payload magic")
    freepage_records = [
        line for line in free_lines if line.startswith("FREEPAGE\t")
    ]
else:
    free_lines = catalog_lines
    free_page_types = []
    freepage_records = catalog_freepage_records
if not freepage_records:
    fail("free page payload has no FREEPAGE records")

free_ranges = parse_freepage_records(free_lines)
live_ranges = [page_range(headers[0][3], headers[0][4])]
if catalog_format_version == 3:
    live_ranges.append(page_range(headers[0][5], headers[0][6]))

row_payloads = []
row_payload_page_counts = []
row_overflow_payloads = []
row_overflow_page_counts = []
for line in rowpage_records:
    parts = line.split("\t")
    if len(parts) != 6:
        fail("invalid ROWPAGE record")
    root_offset = int(parts[3])
    length = int(parts[4])
    if root_offset == 0 and length == 0:
        continue
    live_ranges.append(page_range(root_offset, length))
    row_payload = (
        f"{bytes.fromhex(parts[1]).decode('ascii')}."
        f"{bytes.fromhex(parts[2]).decode('ascii')}"
    )
    payload, page_types, page_payloads = read_chain(
        root_offset, length, 2, False
    )
    slot_pages = 0
    overflow_pages = 0
    for page_payload in page_payloads:
        if page_payload.startswith(b"MYLITEROWSLOT2\0\0"):
            if read_u32(page_payload, 16) != 2:
                fail("invalid row slot format version")
            row_count = read_u32(page_payload, 20)
            if row_count == 0:
                fail("row slot payload has no row records")
            slot_pages += 1
        elif page_payload.startswith(b"MYLITEROWOVF3\0\0\0"):
            if len(page_payload) < 52:
                fail("short row overflow payload")
            if read_u32(page_payload, 16) != 3:
                fail("invalid row overflow format version")
            rowid = read_u64(page_payload, 20)
            total_length = read_u64(page_payload, 28)
            segment_offset = read_u64(page_payload, 36)
            segment_length = read_u32(page_payload, 44)
            reserved = read_u32(page_payload, 48)
            if rowid == 0 or total_length <= 3984:
                fail("invalid row overflow row metadata")
            if segment_length == 0 or len(page_payload) != 52 + segment_length:
                fail("invalid row overflow segment length")
            if segment_offset > total_length:
                fail("invalid row overflow segment offset")
            if segment_length > total_length - segment_offset:
                fail("row overflow segment exceeds total length")
            if reserved != 0:
                fail("nonzero row overflow reserved bytes")
            overflow_pages += 1
        else:
            fail("invalid row payload magic")
    if row_payload == "mylite.persisted_wide" and len(page_types) < 2:
        fail("wide row payload did not span pages")
    if row_payload == "mylite.persisted_large" and overflow_pages < 2:
        fail("large row payload did not use overflow pages")
    row_payloads.append(row_payload)
    row_payload_page_counts.append(f"{row_payload}:{len(page_types)}")
    if overflow_pages:
        row_overflow_payloads.append(row_payload)
        row_overflow_page_counts.append(f"{row_payload}:{overflow_pages}")

if not row_payloads:
    fail("no nonempty row payload chains")

index_payloads = []
index_payload_page_counts = []
for line in indexpage_records:
    parts = line.split("\t")
    if len(parts) != 8:
        fail("invalid INDEXPAGE record")
    owner = (
        f"{bytes.fromhex(parts[1]).decode('ascii')}."
        f"{bytes.fromhex(parts[2]).decode('ascii')}"
    )
    key_index = int(parts[3])
    key_length = int(parts[4])
    root_offset = int(parts[5])
    length = int(parts[6])
    live_ranges.append(page_range(root_offset, length))
    payload, page_types, _ = read_chain(root_offset, length, 3, True)
    if len(payload) < 36:
        fail("short index payload")
    if not payload.startswith(b"MYLITEINDEXPG1\0\0"):
        fail("invalid index payload magic")
    if read_u32(payload, 16) != 1:
        fail("invalid index payload format version")
    if read_u32(payload, 20) != key_index:
        fail("index payload key index mismatch")
    if read_u32(payload, 24) != key_length:
        fail("index payload key length mismatch")
    entry_count = read_u64(payload, 28)
    if entry_count == 0:
        fail("index payload has no entries")
    if len(payload) != 36 + entry_count * (8 + key_length):
        fail("invalid index payload length")
    index_payload = f"{owner}:{key_index}"
    index_payloads.append(index_payload)
    index_payload_page_counts.append(f"{index_payload}:{len(page_types)}")

if "mylite.persisted_keyed:0" not in index_payloads:
    fail("persisted keyed primary index payload missing")
if "mylite.persisted_keyed:1" not in index_payloads:
    fail("persisted keyed secondary index payload missing")

for free_range in free_ranges:
    for live_range in live_ranges:
        if ranges_overlap(free_range, live_range):
            fail("FREEPAGE range overlaps latest live payload")

reused_ranges = []
catalog_reused_ranges = []
if len(headers) > 1:
    if headers[1][2] == 3:
        previous_payload, _, _ = read_chain(headers[1][5], headers[1][6], 4, True)
        try:
            previous_lines = previous_payload.decode("ascii").splitlines()
        except UnicodeDecodeError:
            fail("previous free page payload is not ascii")
        if not previous_lines or previous_lines[0] != "MYLITE FREE LIST 1":
            fail("previous free page payload magic is invalid")
    else:
        previous_payload, _, _ = read_chain(headers[1][3], headers[1][4], 1, True)
        try:
            previous_lines = previous_payload.decode("ascii").splitlines()
        except UnicodeDecodeError:
            fail("previous catalog payload is not ascii")
    previous_free_ranges = parse_freepage_records(previous_lines)
    catalog_range = page_range(headers[0][3], headers[0][4])
    for live_range in live_ranges:
        for previous_free_range in previous_free_ranges:
            if range_contains(previous_free_range, live_range):
                reused_ranges.append(f"{live_range[0]}:{live_range[1]}")
                if live_range == catalog_range:
                    catalog_reused_ranges.append(f"{live_range[0]}:{live_range[1]}")
                break
if not reused_ranges:
    fail("no latest live payload reused a previous FREEPAGE range")
if not catalog_reused_ranges:
    fail("no latest catalog payload reused a previous FREEPAGE range")

with report.open("a") as out:
    out.write("\n## Row And Index Page Storage\n\n")
    out.write("status=0\n")
    out.write(f"latest_generation={headers[0][0]}\n")
    out.write(f"catalog_format_version={catalog_format_version}\n")
    out.write(f"catalog_page_types={','.join(map(str, catalog_page_types))}\n")
    out.write(f"free_payload_page_types={','.join(map(str, free_page_types))}\n")
    out.write(f"catalog_row_records={len(catalog_row_records)}\n")
    out.write(f"catalog_freepage_records={len(catalog_freepage_records)}\n")
    out.write(f"freepage_records={len(freepage_records)}\n")
    out.write(
        f"freepage_pages={sum(count for _, count in free_ranges)}\n"
    )
    out.write(f"reused_page_ranges={','.join(reused_ranges)}\n")
    out.write(f"catalog_reused_page_ranges={','.join(catalog_reused_ranges)}\n")
    out.write(f"rowpage_records={len(rowpage_records)}\n")
    out.write(f"indexpage_records={len(indexpage_records)}\n")
    out.write(f"row_payloads={','.join(row_payloads)}\n")
    out.write(f"row_payload_page_counts={','.join(row_payload_page_counts)}\n")
    out.write("row_payload_magic=MYLITEROWSLOT2\n")
    out.write("row_payload_page_type=2\n")
    out.write(f"row_overflow_payloads={','.join(row_overflow_payloads)}\n")
    out.write(f"row_overflow_page_counts={','.join(row_overflow_page_counts)}\n")
    out.write("row_overflow_magic=MYLITEROWOVF3\n")
    out.write(f"index_payloads={','.join(index_payloads)}\n")
    out.write(f"index_payload_page_counts={','.join(index_payload_page_counts)}\n")
    out.write("index_payload_magic=MYLITEINDEXPG1\n")
    out.write("index_payload_page_type=3\n")
    out.write("free_payload_magic=MYLITE FREE LIST 1\n")
    out.write("free_payload_page_type=4\n")
PY
}

verify_orphan_page_reclaim() {
  local catalog_file="$1"
  local report="$2"
  local orphan_offset="$3"
  local latest_size="$4"

  if [[ ! -f "${catalog_file}" ]]; then
    printf "Catalog file does not exist: %s\n" "${catalog_file}" >&2
    return 1
  fi

  python3 - "${catalog_file}" "${report}" "${orphan_offset}" "${latest_size}" <<'PY'
import struct
import sys
from pathlib import Path

catalog_file = Path(sys.argv[1])
report = Path(sys.argv[2])
orphan_offset = int(sys.argv[3])
latest_size = int(sys.argv[4])
data = catalog_file.read_bytes()
page_size = 4096
page_payload_offset = 64
page_payload_capacity = page_size - page_payload_offset

def fail(message):
    with report.open("a") as out:
        out.write("\n## Orphan Page Reclaim\n\n")
        out.write(f"status=1\nmessage={message}\n")
    raise SystemExit(1)

def read_u32(page, offset):
    return struct.unpack_from("<I", page, offset)[0]

def read_u64(page, offset):
    return struct.unpack_from("<Q", page, offset)[0]

def page_count(length):
    return (length + page_payload_capacity - 1) // page_payload_capacity

def page_range(root_offset, length):
    if root_offset < page_size * 2 or root_offset % page_size != 0:
        fail("invalid page-chain root offset")
    if length == 0:
        fail("empty page chain")
    return (root_offset // page_size, page_count(length))

def ranges_overlap(left, right):
    left_start, left_count = left
    right_start, right_count = right
    return (
        left_start < right_start + right_count and
        right_start < left_start + left_count
    )

def range_intersection(left, right):
    left_start, left_count = left
    right_start, right_count = right
    start = max(left_start, right_start)
    end = min(left_start + left_count, right_start + right_count)
    if start >= end:
        return None
    return (start, end - start)

def parse_freepage_records(catalog_lines):
    ranges = []
    for line in catalog_lines:
        if not line.startswith("FREEPAGE\t"):
            continue
        parts = line.split("\t")
        if len(parts) != 3:
            fail("invalid FREEPAGE record")
        page_id = int(parts[1])
        page_count_value = int(parts[2])
        if page_id < 2 or page_count_value == 0:
            fail("invalid FREEPAGE range")
        ranges.append((page_id, page_count_value))
    ranges.sort()
    for previous, current in zip(ranges, ranges[1:]):
        if previous[0] + previous[1] >= current[0]:
            fail("overlapping FREEPAGE ranges")
    return ranges

def read_chain(root_offset, length, expected_type):
    if root_offset < page_size * 2 or root_offset % page_size != 0:
        fail("invalid page-chain root offset")
    if length == 0:
        fail("empty page chain")

    page_id = root_offset // page_size
    remaining = length
    payload = bytearray()
    while remaining > 0:
        start = page_id * page_size
        page = data[start:start + page_size]
        if len(page) != page_size:
            fail("short page-chain read")
        if page[:16] != b"MYLITEPAGESTORE\0":
            fail("invalid page magic")
        page_type = read_u32(page, 20)
        stored_page_id = read_u64(page, 24)
        next_page_id = read_u64(page, 32)
        used = read_u32(page, 40)
        if page_type != expected_type:
            fail("unexpected page type")
        if stored_page_id != page_id:
            fail("unexpected page id")
        expected_used = min(remaining, page_payload_capacity)
        if used != expected_used:
            fail("unexpected page payload length")
        payload.extend(page[page_payload_offset:page_payload_offset + used])
        remaining -= used
        if remaining == 0:
            if next_page_id != 0:
                fail("nonzero terminal next page")
            break
        if next_page_id != page_id + 1:
            fail("nonsequential next page")
        page_id = next_page_id
    return bytes(payload)

if not report.exists():
    fail("recovery report does not exist")
report_text = report.read_text()
if "recovery_marker=absent\n" not in report_text:
    fail("recovery fallback did not hide corrupted marker table")
if "recovery_reclaim=13\n" not in report_text:
    fail("recovery reclaim write did not persist expected rows")
if orphan_offset % page_size != 0:
    fail("orphan offset is not page-aligned")
if latest_size % page_size != 0:
    fail("latest rejected size is not page-aligned")
if latest_size <= orphan_offset:
    fail("rejected generation did not append complete pages")

orphan_interval = (
    orphan_offset // page_size,
    latest_size // page_size - orphan_offset // page_size,
)

headers = []
for slot in (0, 1):
    start = slot * page_size
    page = data[start:start + page_size]
    if len(page) != page_size:
        continue
    if page[:16] != b"MYLITEFMTPAGE2\0\0":
        continue
    format_version = read_u32(page, 16)
    if format_version not in (2, 3) or read_u32(page, 20) != page_size:
        continue
    generation = read_u64(page, 24)
    if generation == 0:
        continue
    free_offset = read_u64(page, 64) if format_version == 3 else 0
    free_length = read_u64(page, 72) if format_version == 3 else 0
    headers.append((
        generation, slot, format_version, read_u64(page, 32),
        read_u64(page, 40), free_offset, free_length
    ))

if not headers:
    fail("no valid header slots")

headers.sort(reverse=True)
catalog_payload = read_chain(headers[0][3], headers[0][4], 1)
try:
    catalog_lines = catalog_payload.decode("ascii").splitlines()
except UnicodeDecodeError:
    fail("catalog payload is not ascii")

if headers[0][2] == 3:
    free_payload = read_chain(headers[0][5], headers[0][6], 4)
    try:
        free_lines = free_payload.decode("ascii").splitlines()
    except UnicodeDecodeError:
        fail("free page payload is not ascii")
    if not free_lines or free_lines[0] != "MYLITE FREE LIST 1":
        fail("invalid free page payload magic")
else:
    free_lines = catalog_lines

free_ranges = parse_freepage_records(free_lines)
live_ranges = [page_range(headers[0][3], headers[0][4])]
if headers[0][2] == 3:
    live_ranges.append(page_range(headers[0][5], headers[0][6]))
reclaimed_range = None

for line in catalog_lines:
    if line.startswith("ROWPAGE\t"):
        parts = line.split("\t")
        if len(parts) != 6:
            fail("invalid ROWPAGE record")
        owner = (
            f"{bytes.fromhex(parts[1]).decode('ascii')}."
            f"{bytes.fromhex(parts[2]).decode('ascii')}"
        )
        root_offset = int(parts[3])
        length = int(parts[4])
        if root_offset == 0 and length == 0:
            continue
        row_range = page_range(root_offset, length)
        live_ranges.append(row_range)
        if owner == "mylite.recovery_reclaim":
            reclaimed_range = row_range
    elif line.startswith("INDEXPAGE\t"):
        parts = line.split("\t")
        if len(parts) != 8:
            fail("invalid INDEXPAGE record")
        live_ranges.append(page_range(int(parts[5]), int(parts[6])))

if reclaimed_range is None:
    fail("recovery reclaim row page was not found")

for free_range in free_ranges:
    for live_range in live_ranges:
        if ranges_overlap(free_range, live_range):
            fail("FREEPAGE range overlaps latest live payload")

rejected_free_ranges = []
for free_range in free_ranges:
    overlap = range_intersection(free_range, orphan_interval)
    if overlap:
        rejected_free_ranges.append(f"{overlap[0]}:{overlap[1]}")
if not rejected_free_ranges:
    fail("rejected generation pages were not published as FREEPAGE")

with report.open("a") as out:
    out.write("\n## Orphan Page Reclaim\n\n")
    out.write("status=0\n")
    out.write(f"orphan_page_interval={orphan_interval[0]}:{orphan_interval[1]}\n")
    out.write(f"reclaimed_page_ranges={','.join(rejected_free_ranges)}\n")
    out.write(
        f"recovery_reclaim_row_range={reclaimed_range[0]}:{reclaimed_range[1]}\n"
    )
    out.write(f"freepage_records={len(free_ranges)}\n")
    out.write(f"freepage_pages={sum(count for _, count in free_ranges)}\n")
PY
}

append_observed_files() {
  local runtime_dir="$1"
  local report="$2"
  local smoke_log="$3"

  {
    printf "\n## Smoke Process Output\n\n"
    if [[ -s "${smoke_log}" ]]; then
      cat "${smoke_log}"
    else
      printf "none\n"
    fi

    printf "\n## Observed Runtime Files\n\n"
    if find "${runtime_dir}" -mindepth 1 -type f | grep -q .; then
      find "${runtime_dir}" -mindepth 1 -type f -printf "%P\t%s bytes\n" \
        | sort
    else
      printf "none\n"
    fi

    printf "\n## Dynamic Plugin Artifacts\n\n"
    if find "${runtime_dir}" -type f \( -name "*.so" -o -name "*.dylib" -o -name "*.dll" \) \
        | grep -q .; then
      find "${runtime_dir}" -type f \( -name "*.so" -o -name "*.dylib" -o -name "*.dll" \) \
        -printf "%P\n" | sort
    else
      printf "none\n"
    fi

    printf "\n## FRM Artifacts\n\n"
    if has_frm_artifacts "${runtime_dir}"; then
      find "${runtime_dir}" -type f -name "*.frm" -printf "%P\n" | sort
    else
      printf "none\n"
    fi
  } >> "${report}"
}

append_catalog_files() {
  local catalog_file="$1"
  local report="$2"

  if [[ -z "${catalog_file}" ]]; then
    return
  fi

  {
    printf "\n## Catalog File\n\n"
    if [[ -f "${catalog_file}" ]]; then
      printf "%s\t%s bytes\n" "$(basename "${catalog_file}")" "$(stat -c %s "${catalog_file}")"
    else
      printf "none\n"
    fi

    printf "\n## Catalog Sidecars\n\n"
    if [[ -e "${catalog_file}.tmp" ]]; then
      printf "%s\n" "$(basename "${catalog_file}.tmp")"
    else
      printf "none\n"
    fi
  } >> "${report}"
}

has_frm_artifacts() {
  local runtime_dir="$1"
  find "${runtime_dir}" -type f -name "*.frm" | grep -q .
}

main "$@"
