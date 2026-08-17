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
    assert refmem_pack_build.BOARD_CAPABILITY_COUNT == refmem_pack_build.NODE_COUNT
    expected_sizes = {
        0: 6 * 4,
        1: 8 + refmem_pack_build.BOARD_CAPABILITY_COUNT * 9 * 4,
        2: 8 + refmem_pack_build.NODE_COUNT * 9 * 4,
        3: 8 + refmem_pack_build.NODE_LOAD_COUNT * 11 * 4,
        4: 8 + refmem_pack_build.FB_INSTANCE_COUNT * 20 * 4,
        5: 8 + refmem_pack_build.EVENT_LINK_COUNT * 12 * 4,
        6: 8 + refmem_pack_build.DATA_LINK_COUNT * 15 * 4,
        7: 8 + refmem_pack_build.DEPLOYMENT_CHECK_COUNT * 9 * 4,
        8: 8 + refmem_pack_build.QUALITY_COUNT * 16 * 4,
        9: (2 + refmem_pack_build.TDMA_FOUNDATION_PROFILE_WIRE_WORDS) * 4,
    }
    expected_counts = {
        1: refmem_pack_build.NODE_COUNT,
        2: refmem_pack_build.NODE_COUNT,
        3: refmem_pack_build.NODE_LOAD_COUNT,
        4: refmem_pack_build.FB_INSTANCE_COUNT,
        5: refmem_pack_build.EVENT_LINK_COUNT,
        6: refmem_pack_build.DATA_LINK_COUNT,
        7: refmem_pack_build.DEPLOYMENT_CHECK_COUNT,
        8: refmem_pack_build.QUALITY_COUNT,
        9: refmem_pack_build.TDMA_PROFILE_TABLE_COUNT,
    }

    for index, entry in enumerate(entries):
        directory_offset = refmem_pack_build.HEADER_SIZE + index * 16
        table_id, offset, size, crc32 = struct.unpack_from("<IIII", package, directory_offset)
        payload = package[offset : offset + size]

        assert table_id == entry.table_id == index
        assert offset == entry.offset
        assert size == entry.size == expected_sizes[index]
        assert crc32 == entry.crc32 == refmem_pack_build.crc32(payload)
        if index == 0:
            assert struct.unpack_from("<IIIIII", payload, 0) == (
                refmem_pack_build.FORMAT_VERSION,
                1,
                1,
                1,
                1,
                0xFF,
            )
        else:
            expected_count = expected_counts[index]
            assert struct.unpack_from("<II", payload, 0) == (
                refmem_pack_build.FORMAT_VERSION,
                expected_count,
            )
