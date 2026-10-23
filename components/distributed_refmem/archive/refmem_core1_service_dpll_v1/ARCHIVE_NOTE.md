# RefMem core1 service in DPLL path v1 (ARCHIVED)

Status: ARCHIVED / NOT BUILT

The extra distributed_refmem_tdma_core1_service() call was inserted before the refmem publish service in the VDC/DPLL core1 loop. P3 DPLL joint capture then failed with all scope windows missed (internal health false), so the call was removed from the active loop and the patch kept here. The REFMEM command path uses the maintenance stream, not this realtime scheduler.
