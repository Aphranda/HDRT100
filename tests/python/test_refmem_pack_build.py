from __future__ import annotations

import struct

from tools.refmem_pack_build import refmem_pack_build


def test_refmem_package_header_and_directory_are_consistent() -> None:
    package, entries = refmem_pack_build.build_package()

    magic, version, header_size, total_size, table_count, table_dir_size, payload_crc = struct.unpack_from(
        "<4sIIIIII", package, 0
    )

    assert magic == refmem_pack_build.MAGIC
    assert version == refmem_pack_build.FORMAT_VERSION
    assert header_size == refmem_pack_build.HEADER_SIZE
    assert total_size == len(package)
    assert table_count == refmem_pack_build.TABLE_COUNT
    assert table_dir_size == refmem_pack_build.TABLE_COUNT * 16
    assert len(entries) == refmem_pack_build.TABLE_COUNT

    payload = package[header_size + table_dir_size :]
    assert payload_crc == refmem_pack_build.crc32(payload)


def test_refmem_package_table_entries_match_payloads() -> None:
    package, entries = refmem_pack_build.build_package()
    expected_sizes = {
        1: 8 + refmem_pack_build.BOARD_CAPABILITY_COUNT * 9 * 4,
        2: 8 + refmem_pack_build.NODE_COUNT * 9 * 4,
    }

    for index, entry in enumerate(entries):
        directory_offset = refmem_pack_build.HEADER_SIZE + index * 16
        table_id, offset, size, crc32 = struct.unpack_from("<IIII", package, directory_offset)
        payload = package[offset : offset + size]

        assert table_id == entry.table_id == index
        assert offset == entry.offset
        assert size == entry.size == expected_sizes.get(index, 64)
        assert crc32 == entry.crc32 == refmem_pack_build.crc32(payload)
        if index in expected_sizes:
            assert struct.unpack_from("<II", payload, 0) == (
                refmem_pack_build.FORMAT_VERSION,
                refmem_pack_build.NODE_COUNT,
            )
        else:
            assert payload.startswith(refmem_pack_build.DEFAULT_TABLE_NAMES[index].encode("ascii"))
