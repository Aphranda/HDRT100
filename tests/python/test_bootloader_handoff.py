from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def _jump_to_app_source() -> str:
    source = (ROOT / "bootloader/src/bootloader_main.c").read_text(encoding="utf-8")
    return source.split("static void bootloader_jump_to_app", 1)[1].split(
        "int main", 1
    )[0]


def _main_source() -> str:
    source = (ROOT / "bootloader/src/bootloader_main.c").read_text(encoding="utf-8")
    return source.split("int main(void)", 1)[1]


def test_bootloader_app_handoff_restores_interrupts_after_stack_and_vector_setup() -> None:
    source = _jump_to_app_source()

    cpsid = source.index('__asm volatile("cpsid i"')
    vtor = source.index("scb_hw->vtor", cpsid)
    msp = source.index('__asm volatile("msr msp', vtor)
    dsb = source.index('__asm volatile("dsb"', msp)
    first_isb = source.index('__asm volatile("isb")', dsb)
    cpsie = source.index('__asm volatile("cpsie i"', first_isb)
    second_isb = source.index('__asm volatile("isb" ::: "memory")', cpsie)
    entry = source.index("((app_entry_t)", second_isb)

    assert source.count('"cpsid i"') == 1
    assert source.count('"cpsie i"') == 1
    assert cpsid < vtor < msp < dsb < first_isb < cpsie < second_isb < entry
    assert '"cpsie i" ::: "memory"' in source


def test_bootloader_disables_inherited_watchdog_before_slot_validation() -> None:
    source = _main_source()

    disable = source.index("drv_watchdog_disable();")
    metadata_load = source.index("ota_metadata_load(")

    assert disable < metadata_load
