from tools.flash_map.flash_link_check import validate_link_contract


def valid_map() -> str:
    return """Memory Configuration

Name             Origin             Length             Attributes
FLASH            0x101c0000         0x00180000         xr
RAM              0x20000000         0x00080000         xrw
SCRATCH_X        0x20080000         0x00001000         xrw
SCRATCH_Y        0x20081000         0x00001000         xrw
*default*        0x00000000         0xffffffff

Linker script and memory map
                0x10000000 FLASH_COMPAT_GEOMETRY_XIP_BASE = 0x10000000
                0x01000000 FLASH_COMPAT_GEOMETRY_TOTAL_SIZE = 0x1000000
                0x20000100 drv_flash_core1_lockout_poll
                0x20000110 drv_flash_lockout_core1_poll
                0x20000200 flash_range_erase
                0x20000300 flash_range_program
 .bss.s_lockout
                0x20001000 0x2c lockout.o
"""


def valid_disassembly() -> str:
    return """20000100 <drv_flash_core1_lockout_poll>:
 20000100: b.w 20000110 <drv_flash_lockout_core1_poll>
20000110 <drv_flash_lockout_core1_poll>:
 20000110: mrs r7, PRIMASK
 20000114: cpsid i
 20000118: msr PRIMASK, r7
10100000 <flash_transaction_erase>:
 10100000: b.w 10110000 <drv_flash_erase_parked>
10100004 <flash_transaction_program>:
 10100004: b.w 10110010 <drv_flash_program_parked>
10110000 <drv_flash_erase_parked>:
 10110000: bx lr
10110010 <drv_flash_program_parked>:
 10110010: bx lr
"""


def test_link_contract_accepts_ram_closure_and_single_owner() -> None:
    assert validate_link_contract(valid_map(), valid_disassembly()) == []


def test_link_contract_rejects_xip_park_loop() -> None:
    bad_map = valid_map().replace(
        "0x20000110 drv_flash_lockout_core1_poll",
        "0x101C0110 drv_flash_lockout_core1_poll",
    )

    failures = validate_link_contract(bad_map, valid_disassembly())

    assert any("outside SRAM" in failure for failure in failures)


def test_link_contract_rejects_unowned_parked_call() -> None:
    bad_disassembly = valid_disassembly() + """10120000 <unowned_writer>:
 10120000: bl 10110000 <drv_flash_erase_parked>
"""

    failures = validate_link_contract(valid_map(), bad_disassembly)

    assert any("parked raw caller drift" in failure for failure in failures)


def test_link_contract_rejects_xip_literal_in_park_loop() -> None:
    bad_disassembly = valid_disassembly().replace(
        " 20000118: msr PRIMASK, r7",
        " 20000118: .word 0x101C1234\n 2000011c: msr PRIMASK, r7",
    )

    failures = validate_link_contract(valid_map(), bad_disassembly)

    assert any("references XIP" in failure for failure in failures)


def test_link_contract_rejects_direct_synchronous_raw_call() -> None:
    bad_disassembly = valid_disassembly() + """10120100 <legacy_writer>:
 10120100: bl 10120200 <drv_flash_program>
10120200 <drv_flash_program>:
 10120200: bx lr
"""

    failures = validate_link_contract(valid_map(), bad_disassembly)

    assert any("synchronous raw caller linked into App" in failure for failure in failures)


def test_boot_link_contract_rejects_unapproved_raw_caller() -> None:
    boot_disassembly = """10000000 <main>:
 10000000: bl 10000100 <drv_flash_erase>
10000100 <drv_flash_erase>:
 10000100: bx lr
10000200 <ota_metadata_flash_erase>:
 10000200: bl 10000100 <drv_flash_erase>
10000300 <ota_metadata_flash_program>:
 10000300: bl 10000400 <drv_flash_program>
10000400 <drv_flash_program>:
 10000400: bx lr
10000500 <legacy_writer>:
 10000500: bl 10000100 <drv_flash_erase>
"""

    failures = validate_link_contract(valid_map(), boot_disassembly, profile="boot")

    assert any("Boot raw caller drift" in failure for failure in failures)


def boot_map_with_size_symbols() -> str:
    return valid_map().replace(
        "0x10000000 FLASH_COMPAT_GEOMETRY_XIP_BASE = 0x10000000",
        "0x10000000 FLASH_COMPAT_GEOMETRY_XIP_BASE = 0x10000000\n"
        "0x10000000 FLASH_COMPAT_MAP_BOOTLOADER_ORIGIN = 0x10000000\n"
        "0x00040000 FLASH_COMPAT_MAP_BOOTLOADER_LENGTH = 0x40000\n"
        "0x10002be8 PROVIDE (__flash_binary_end = .)\n"
        "0x10000500 bootloader_validate_slot_direct\n"
        "0x10000510 ota_metadata_load\n"
        "0x10000520 ota_metadata_store\n"
        "0x10000530 drv_flash_erase\n"
        "0x10000540 drv_flash_program",
    ).replace(
        "FLASH            0x101c0000         0x00180000         xr",
        "FLASH            0x10000000         0x00040000         xr",
    )


def test_boot_link_contract_accepts_generated_partition_size_gate() -> None:
    disassembly = """10000000 <main>:
 10000000: bl 10000100 <boot_flash_service_erase>
 10000004: bl 10000400 <boot_flash_service_program>
10000100 <drv_flash_erase>:
 10000100: bx lr
10000200 <boot_flash_service_erase>:
 10000200: bl 10000100 <drv_flash_erase>
10000300 <boot_flash_service_program>:
 10000300: bl 10000400 <drv_flash_program>
10000400 <drv_flash_program>:
 10000400: bx lr
"""

    assert validate_link_contract(boot_map_with_size_symbols(), disassembly,
                                 profile="boot") == []


def test_boot_link_contract_ignores_build_directory_in_disassembly_header() -> None:
    # objdump repeats the absolute ELF path in its file-format header.  A
    # valid build directory may contain a dependency-token substring.
    disassembly = (
        "D:/build/tdma-p0t/DHRT100_BOOT.elf:     file format elf32-littlearm\n"
        "\n10000000 <main>:\n"
        " 10000000: bl 10000200 <boot_flash_service_erase>\n"
        "10000100 <drv_flash_erase>:\n 10000100: bx lr\n"
        "10000110 <drv_flash_program>:\n 10000110: bx lr\n"
        "10000200 <boot_flash_service_erase>:\n"
        " 10000200: bl 10000100 <drv_flash_erase>\n"
        "10000300 <boot_flash_service_program>:\n"
        " 10000300: bl 10000110 <drv_flash_program>\n"
    )
    assert validate_link_contract(boot_map_with_size_symbols(), disassembly,
                                  profile="boot") == []


def test_boot_link_contract_rejects_partition_size_overflow() -> None:
    bad_map = boot_map_with_size_symbols().replace(
        "0x10002be8 PROVIDE (__flash_binary_end = .)",
        "0x10040004 PROVIDE (__flash_binary_end = .)",
    )
    disassembly = """10000000 <main>:
 10000000: bl 10000100 <drv_flash_erase>
10000100 <drv_flash_erase>:
 10000100: bx lr
10000200 <boot_flash_service_erase>:
 10000200: bl 10000100 <drv_flash_erase>
10000300 <boot_flash_service_program>:
 10000300: bl 10000400 <drv_flash_program>
10000400 <drv_flash_program>:
 10000400: bx lr
"""

    failures = validate_link_contract(bad_map, disassembly, profile="boot")

    assert any("Bootloader exceeds generated partition" in failure
               for failure in failures)


def recovery_map_with_size_symbols() -> str:
    return valid_map().replace(
        "0x10000000 FLASH_COMPAT_GEOMETRY_XIP_BASE = 0x10000000",
        "0x10000000 FLASH_ACTIVE_GEOMETRY_XIP_BASE = 0x10000000\n"
        "0x10480000 FLASH_ACTIVE_MAP_RECOVERY_ORIGIN = 0x10480000\n"
        "0x00100000 FLASH_ACTIVE_MAP_RECOVERY_LENGTH = 0x100000\n"
        "0x10483000 PROVIDE (__flash_binary_end = .)\n"
        "0x10480100 recovery_get_bcb_health\n"
        "0x10480180 recovery_get_bcb_wear\n"
        "0x10480200 pota_bcb_store_init_read_only\n"
        "0x10480300 pota_bcb_store_get_health_snapshot\n"
        "0x10480380 pota_bcb_store_get_wear_snapshot\n"
        "0x104803A0 pota_metadata_is_valid\n"
        "0x104803C0 pota_image_validate_app_vector\n"
        "0x104803E0 pota_crc32_update\n"
        "0x10480400 reset_usb_boot",
    ).replace(
        "FLASH            0x101c0000         0x00180000         xr",
        "FLASH            0x10480000         0x00100000         xr",
    )


def test_recovery_link_contract_accepts_read_only_image() -> None:
    assert validate_link_contract(
        recovery_map_with_size_symbols(), "10480000 <main>:\n",
        profile="recovery") == []


def test_recovery_link_contract_rejects_raw_writer() -> None:
    failures = validate_link_contract(
        recovery_map_with_size_symbols() + "\n0x10480500 drv_flash_program\n",
        "10480000 <main>:\n", profile="recovery")
    assert any("forbidden Recovery dependency" in failure
               for failure in failures)
