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

  local recovery_corrupt_offset
  recovery_corrupt_offset="$(stat -c %s "${recovery_file}")"

  run_smoke_phase \
    "${smoke}" \
    "${abs_build_dir}/sql/share" \
    "${recovery_root}/latest" \
    "${abs_build_dir}/mylite-catalog-recovery-latest-report.txt" \
    "${abs_build_dir}/mylite-catalog-recovery-latest-output.log" \
    "${recovery_file}" \
    "recovery-latest" || status=1

  corrupt_latest_generation_page "${recovery_file}" "${recovery_corrupt_offset}" || status=1

  run_smoke_phase \
    "${smoke}" \
    "${abs_build_dir}/sql/share" \
    "${recovery_root}/read" \
    "${abs_build_dir}/mylite-catalog-recovery-read-report.txt" \
    "${abs_build_dir}/mylite-catalog-recovery-read-output.log" \
    "${recovery_file}" \
    "recovery-read" || status=1

  printf "Storage engine smoke report: %s\n" "${report}"
  printf "Catalog write smoke report: %s\n" "${abs_build_dir}/mylite-catalog-write-report.txt"
  printf "Catalog read smoke report: %s\n" "${abs_build_dir}/mylite-catalog-read-report.txt"
  printf "Catalog recovery base smoke report: %s\n" "${abs_build_dir}/mylite-catalog-recovery-base-report.txt"
  printf "Catalog recovery latest smoke report: %s\n" "${abs_build_dir}/mylite-catalog-recovery-latest-report.txt"
  printf "Catalog recovery read smoke report: %s\n" "${abs_build_dir}/mylite-catalog-recovery-read-report.txt"
  return "${status}"
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
    if read_u32(page, 16) != 2 or read_u32(page, 20) != page_size:
        continue
    generation = read_u64(page, 24)
    if generation == 0:
        continue
    headers.append((generation, slot, read_u64(page, 32), read_u64(page, 40)))

if not headers:
    fail("no valid header slots")

headers.sort(reverse=True)

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
    headers[0][2], headers[0][3], 1, True
)
try:
    catalog_lines = catalog_payload.decode("ascii").splitlines()
except UnicodeDecodeError:
    fail("catalog payload is not ascii")

catalog_row_records = [line for line in catalog_lines if line.startswith("ROW\t")]
rowpage_records = [line for line in catalog_lines if line.startswith("ROWPAGE\t")]
indexpage_records = [line for line in catalog_lines if line.startswith("INDEXPAGE\t")]
if catalog_row_records:
    fail("catalog still contains ROW records")
if not rowpage_records:
    fail("catalog has no ROWPAGE records")
if not indexpage_records:
    fail("catalog has no INDEXPAGE records")

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

with report.open("a") as out:
    out.write("\n## Row And Index Page Storage\n\n")
    out.write("status=0\n")
    out.write(f"latest_generation={headers[0][0]}\n")
    out.write(f"catalog_page_types={','.join(map(str, catalog_page_types))}\n")
    out.write(f"catalog_row_records={len(catalog_row_records)}\n")
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
