#include "pota_core.h"

#include <string.h>

static uint32_t pota_align_up(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static const pota_partition_t *pota_partition_for_slot(const pota_context_t *context,
                                                       pota_slot_t slot)
{
    if (slot == POTA_SLOT_A) {
        return &context->platform.info.slot_a;
    }
    if (slot == POTA_SLOT_B) {
        return &context->platform.info.slot_b;
    }
    return NULL;
}

static pota_slot_t pota_select_target_slot(const pota_context_t *context)
{
    if (context->platform.info.boot_mode != POTA_BOOT_MODE_DIRECT_AB) {
        return POTA_SLOT_B;
    }
    return context->platform.info.active_slot == POTA_SLOT_A ? POTA_SLOT_B : POTA_SLOT_A;
}

static void pota_set_failed(pota_context_t *context, pota_error_t error)
{
    context->status.state = POTA_STATE_FAILED;
    context->status.error_code = (uint32_t)error;
    context->status.last_result = (uint32_t)POTA_RESULT_FAILED;
}

static void pota_update_progress(pota_context_t *context)
{
    if (context->status.expected_size == 0u) {
        context->status.progress_permille = 0u;
        return;
    }
    uint64_t progress = (uint64_t)context->status.received_size * 1000u;
    progress /= context->status.expected_size;
    context->status.progress_permille = progress > 1000u ? 1000u : (uint32_t)progress;
}

bool pota_init(pota_context_t *context, const pota_platform_t *platform)
{
    if (context == NULL || platform == NULL) {
        return false;
    }
    memset(context, 0, sizeof(*context));
    context->platform = *platform;
    context->status.state = (uint32_t)POTA_STATE_IDLE;
    context->target_slot = pota_select_target_slot(context);
    context->status.target_slot = (uint32_t)context->target_slot;
    return true;
}

pota_error_t pota_begin(pota_context_t *context, const pota_begin_t *begin)
{
    if (context == NULL || begin == NULL || begin->size == 0u) {
        return POTA_ERR_BAD_ARGUMENT;
    }
    if (context->status.state != (uint32_t)POTA_STATE_IDLE &&
        context->status.state != (uint32_t)POTA_STATE_FAILED &&
        context->status.state != (uint32_t)POTA_STATE_ABORTED &&
        context->status.state != (uint32_t)POTA_STATE_COMMITTED) {
        pota_set_failed(context, POTA_ERR_INVALID_STATE);
        return POTA_ERR_INVALID_STATE;
    }

    context->package_mode = begin->package_mode;
    context->package_header_received = false;
    context->target_slot = pota_select_target_slot(context);
    const pota_partition_t *partition = pota_partition_for_slot(context, context->target_slot);
    if (partition == NULL || begin->size > partition->size + POTA_PACKAGE_HEADER_SIZE) {
        pota_set_failed(context, POTA_ERR_IMAGE_TOO_LARGE);
        return POTA_ERR_IMAGE_TOO_LARGE;
    }

    context->target_offset = partition->offset;
    context->target_run_offset =
        context->platform.info.boot_mode == POTA_BOOT_MODE_DIRECT_AB ?
            partition->run_offset :
            context->platform.info.slot_a.run_offset;
    context->selected_image_size = begin->package_mode ? 0u : begin->size;
    context->selected_image_crc32 = begin->package_mode ? 0u : begin->crc32;
    context->target_erase_size = begin->package_mode ? 0u :
                                     pota_align_up(begin->size, context->platform.info.flash_sector_size);

    context->status.state = begin->package_mode ? POTA_STATE_RECEIVING : POTA_STATE_ERASE_SLOT;
    context->status.target_slot = (uint32_t)context->target_slot;
    context->status.expected_size = begin->size;
    context->status.received_size = 0u;
    context->status.programmed_size = 0u;
    context->status.crc32_expected = begin->crc32;
    context->status.crc32_running = 0u;
    context->status.error_code = (uint32_t)POTA_ERR_NONE;
    context->status.last_result = (uint32_t)POTA_RESULT_ACCEPTED;
    return POTA_ERR_NONE;
}

pota_error_t pota_write(pota_context_t *context, const uint8_t *data, uint32_t size)
{
    if (context == NULL || data == NULL || size == 0u) {
        return POTA_ERR_BAD_ARGUMENT;
    }
    if (context->status.state != (uint32_t)POTA_STATE_RECEIVING) {
        pota_set_failed(context, POTA_ERR_INVALID_STATE);
        return POTA_ERR_INVALID_STATE;
    }
    if (context->status.received_size + size > context->status.expected_size) {
        pota_set_failed(context, POTA_ERR_IMAGE_TOO_LARGE);
        return POTA_ERR_IMAGE_TOO_LARGE;
    }

    if (context->package_mode && !context->package_header_received) {
        pota_package_manifest_t manifest;
        const pota_package_constraints_t constraints = {
            .product_id = context->platform.info.product_id,
            .hardware_id = context->platform.info.hardware_id,
            .bootloader_version = context->platform.info.bootloader_version,
        };
        pota_error_t error = pota_package_parse_header(data, size, &constraints, &manifest);
        if (error != POTA_ERR_NONE || manifest.package_size != context->status.expected_size) {
            pota_set_failed(context, error == POTA_ERR_NONE ? POTA_ERR_BAD_HEADER : error);
            return (pota_error_t)context->status.error_code;
        }

        const pota_slot_t image_slot =
            context->target_run_offset == context->platform.info.slot_a.run_offset ?
                POTA_SLOT_A :
                context->target_slot;
        const pota_package_image_t *image = pota_package_find_image(&manifest, image_slot);
        const pota_partition_t *partition = pota_partition_for_slot(context, context->target_slot);
        if (image == NULL || partition == NULL ||
            image->size > partition->size ||
            image->run_offset != context->target_run_offset) {
            pota_set_failed(context, image == NULL ? POTA_ERR_BAD_HEADER : POTA_ERR_IMAGE_TOO_LARGE);
            return (pota_error_t)context->status.error_code;
        }

        context->selected_image_offset = image->offset;
        context->selected_image_size = image->size;
        context->selected_image_crc32 = image->crc32;
        context->target_erase_size = pota_align_up(image->size, context->platform.info.flash_sector_size);
        context->package_header_received = true;
    }

    context->status.crc32_running = pota_crc32_update(context->status.crc32_running, data, size);
    context->status.received_size += size;
    pota_update_progress(context);
    return POTA_ERR_NONE;
}

pota_error_t pota_end(pota_context_t *context)
{
    if (context == NULL) {
        return POTA_ERR_BAD_ARGUMENT;
    }
    if (context->status.received_size != context->status.expected_size) {
        pota_set_failed(context, POTA_ERR_READBACK);
        return POTA_ERR_READBACK;
    }
    if (context->status.crc32_running != context->status.crc32_expected) {
        pota_set_failed(context, POTA_ERR_CRC);
        return POTA_ERR_CRC;
    }

    context->status.state = (uint32_t)POTA_STATE_READY_TO_REBOOT;
    context->status.last_result = (uint32_t)POTA_RESULT_IMAGE_STAGED;
    return POTA_ERR_NONE;
}

pota_error_t pota_abort(pota_context_t *context)
{
    if (context == NULL) {
        return POTA_ERR_BAD_ARGUMENT;
    }
    context->status.state = (uint32_t)POTA_STATE_ABORTED;
    context->status.error_code = (uint32_t)POTA_ERR_ABORTED;
    context->status.last_result = (uint32_t)POTA_RESULT_ABORTED;
    return POTA_ERR_NONE;
}

pota_error_t pota_commit(pota_context_t *context)
{
    if (context == NULL || context->platform.ops.confirm_active == NULL) {
        return POTA_ERR_BAD_ARGUMENT;
    }
    if (!context->platform.ops.confirm_active()) {
        pota_set_failed(context, POTA_ERR_METADATA);
        return POTA_ERR_METADATA;
    }
    context->status.state = (uint32_t)POTA_STATE_COMMITTED;
    context->status.last_result = (uint32_t)POTA_RESULT_COMMITTED;
    return POTA_ERR_NONE;
}

void pota_get_status(const pota_context_t *context, pota_status_t *status)
{
    if (context == NULL || status == NULL) {
        return;
    }
    *status = context->status;
}
