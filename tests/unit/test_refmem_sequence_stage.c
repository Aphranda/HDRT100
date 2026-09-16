#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "refmem_application_model.h"
#include "refmem_table_registry.h"
#include "pota_types.h"

uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, size_t size)
{ return pota_crc32_update(crc, data, size); }

static uint32_t find_instance(uint32_t type)
{
    const refmem_fb_instance_table_t *table = refmem_application_model_get_fb_instance_table();
    for (uint32_t i = 0; i < table->instance_count; ++i)
        if (table->instance[i].fb_type == type) return table->instance[i].instance_id;
    assert(0);
    return 0;
}

static refmem_fb_instance_entry_t instance(uint32_t id, bool staged)
{
    refmem_fb_instance_entry_t result;
    assert(refmem_application_model_get_sequence_instance(id, staged, &result));
    return result;
}

static uint32_t u32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }

static void test_sd_replaces_inline_role_intent(uint32_t dut, uint32_t vna)
{
    assert(refmem_application_model_init());
    assert(refmem_application_model_stage_scpi_node_config(3, vna,
             REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER, REFMEM_APP_PERSONA_GATEWAY, 0, 0, 1));
    refmem_table_view_t view;
    assert(refmem_table_registry_access_table(REFMEM_TABLE_IMAGE_STAGING,
                                               REFMEM_APP_TABLE_FB_INSTANCE, &view));
    uint8_t package[REFMEM_TABLE_IMAGE_BUFFER_SIZE];
    const uint8_t *start = view.data - view.image_offset;
    const size_t size = u32(start + 12);
    assert(size <= sizeof(package));
    memcpy(package, start, size);
    assert(refmem_table_registry_release_table(&view));
    refmem_table_package_validation_t validation;
    assert(refmem_table_registry_validate_package(package, size, &validation));
    assert(refmem_application_model_stage_sequence_role(2, dut, REFMEM_APP_ROLE_LINK_SWITCHER));
    assert(instance(dut, true).enable_condition == 1);
    assert(refmem_application_model_stage_sd_system_pack(
        "/test.rmtp", 1, REFMEM_APP_MODEL_SD_MANIFEST_OK, 1, 0, 0, "test",
        validation.package_crc32, 1, 0));
    refmem_application_model_load_snapshot_t snapshot;
    refmem_application_model_get_load_snapshot(&snapshot);
    assert(refmem_table_registry_stage_package_image(&snapshot, package, size, &validation));
    assert(instance(dut, true).enable_condition == 0);
    assert(refmem_application_model_stage_scpi_node_config(3, vna,
             REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER, REFMEM_APP_PERSONA_GATEWAY, 0, 0, 1));
    assert(instance(dut, true).enable_condition == 0);
    assert(instance(dut, false).enable_condition == 0);

    assert(refmem_application_model_stage_sequence_role(2, dut, REFMEM_APP_ROLE_LINK_SWITCHER));
    assert(!refmem_application_model_stage_sd_system_pack(
        "/bad.rmtp", 2, 0, 1, 1, 1, "bad", 0, 0, 1));
    assert(refmem_application_model_stage_scpi_node_config(3, vna,
             REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER, REFMEM_APP_PERSONA_GATEWAY, 0, 0, 1));
    assert(instance(dut, true).enable_condition == 0);
}

int main(void)
{
    assert(refmem_application_model_init());
    const uint32_t dut = find_instance(REFMEM_APP_FB_LINK_SWITCHER);
    const uint32_t vna = find_instance(REFMEM_APP_FB_INSTRUMENT_CONTROLLER);
    const uint32_t fake = find_instance(REFMEM_APP_FB_MODEL_VNA);
    assert(instance(dut, false).enable_condition == 0);
    assert(instance(vna, false).enable_condition == 0);
    assert(!refmem_application_model_stage_sequence_role(2, fake,
                                                         REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER));
    assert(!refmem_application_model_stage_sequence_role(2, dut, REFMEM_APP_ROLE_GATEWAY));
    assert(refmem_application_model_stage_sequence_role(2, dut, REFMEM_APP_ROLE_LINK_SWITCHER));
    assert(instance(dut, true).enable_condition == 1);
    assert(instance(dut, true).io_claim ==
           (REFMEM_APP_IO_SMA_IN | REFMEM_APP_IO_SMA_OUT | REFMEM_APP_IO_LINK_CONTROL));
    assert(instance(dut, false).enable_condition == 0);
    assert(refmem_application_model_stage_sequence_role(3, vna,
                                                        REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER));
    assert(instance(dut, true).enable_condition == 1);
    assert(instance(vna, true).enable_condition == 1);
    assert(instance(vna, true).io_claim == (REFMEM_APP_IO_SMA_IN | REFMEM_APP_IO_SMA_OUT));
    assert(instance(vna, true).ip_core_claim ==
           (REFMEM_APP_IP_PULSE_CAPTURE | REFMEM_APP_IP_PULSE_FIRE));
    assert(instance(vna, false).enable_condition == 0);

    refmem_table_image_descriptor_t before, after;
    assert(refmem_table_registry_get_image_descriptor(REFMEM_TABLE_IMAGE_STAGING, &before));
    assert(!refmem_application_model_stage_sequence_role(0, dut,
                                                         REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER));
    assert(!refmem_application_model_stage_sequence_role(REFMEM_APP_MODEL_NODE_COUNT, dut,
                                                         REFMEM_APP_ROLE_LINK_SWITCHER));
    assert(refmem_table_registry_get_image_descriptor(REFMEM_TABLE_IMAGE_STAGING, &after));
    assert(memcmp(&before, &after, sizeof(before)) == 0);

    /* The actual registry image and parser, not an in-memory intent, are activated. */
    assert(refmem_application_model_prepare_staging_table_views());
    const refmem_table_activation_gate_t gate = {
        .refmem_idle=1, .realtime_idle=1, .flash_safe=1, .crc_ok=1, .owner_ok=1,
        .slot_claim_ok=1, .deployment_gate_ok=1, .command_ack_ok=1
    };
    assert(refmem_table_registry_activate_staging(&gate));
    assert(refmem_application_model_commit_prepared_table_views());
    assert(instance(dut, false).enable_condition == 1);
    assert(instance(vna, false).enable_condition == 1);
    assert(instance(fake, false).enable_condition == 0);
    assert(instance(vna, false).ip_core_claim ==
           (REFMEM_APP_IP_PULSE_CAPTURE | REFMEM_APP_IP_PULSE_FIRE));
    const refmem_node_load_table_t *loads = refmem_application_model_get_node_load_table();
    for (uint32_t i=0; i<loads->load_count; ++i) {
        if (loads->load[i].instance_id == dut) {
            assert(loads->load[i].node_id == 2 && loads->load[i].enabled == 1);
            assert(loads->load[i].role_mask == REFMEM_APP_ROLE_LINK_SWITCHER);
        }
        if (loads->load[i].instance_id == vna) {
            assert(loads->load[i].node_id == 3 && loads->load[i].enabled == 1);
            assert(loads->load[i].role_mask == REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER);
        }
    }
    /* A failed staging lease cannot poison the next transaction's FB override. */
    assert(refmem_application_model_stage_scpi_node_config(3, vna,
             REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER, REFMEM_APP_PERSONA_GATEWAY, 1, 1, 1));
    refmem_table_view_t lease;
    assert(refmem_table_registry_access_table(REFMEM_TABLE_IMAGE_STAGING,
                                               REFMEM_APP_TABLE_FB_INSTANCE, &lease));
    assert(!refmem_application_model_stage_sequence_role(1, dut, REFMEM_APP_ROLE_LINK_SWITCHER));
    assert(refmem_table_registry_release_table(&lease));
    assert(refmem_application_model_stage_scpi_node_config(3, vna,
             REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER, REFMEM_APP_PERSONA_GATEWAY, 1, 1, 1));
    assert(instance(dut, true).default_node_id == 2);
    assert(instance(vna, true).enable_condition == 1);
    test_sd_replaces_inline_role_intent(dut, vna);
    puts("real sequence role staging, activation and rollback passed");
    return 0;
}
