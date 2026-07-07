#include "sync_io_mode.h"

#include <stddef.h>

#include "sync_io_mode_biss_tap.h"
#include "sync_io_mode_enc_count.h"
#include "sync_io_mode_seq_step.h"

const sync_io_mode_ops_t *sync_io_mode_get_ops(sync_io_mode_id_t id)
{
    switch (id) {
    case SYNC_IO_MODE_ID_SEQ_STEP:
        return sync_io_seq_step_mode_ops();
    case SYNC_IO_MODE_ID_ENC_COUNT:
        return sync_io_enc_count_mode_ops();
    case SYNC_IO_MODE_ID_BISS_TAP:
        return sync_io_biss_tap_mode_ops();
    case SYNC_IO_MODE_ID_NONE:
    case SYNC_IO_MODE_ID_AUX_DIFF_TRIGGER:
    case SYNC_IO_MODE_ID_SELF_CAL:
        return NULL;
    default:
        return NULL;
    }
}

const sync_io_mode_ops_t *sync_io_mode_get_by_index(size_t index)
{
    switch (index) {
    case 0u:
        return sync_io_seq_step_mode_ops();
    case 1u:
        return sync_io_enc_count_mode_ops();
    case 2u:
        return sync_io_biss_tap_mode_ops();
    default:
        return NULL;
    }
}

size_t sync_io_mode_count(void)
{
    return 3u;
}
