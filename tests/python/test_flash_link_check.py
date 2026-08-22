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
