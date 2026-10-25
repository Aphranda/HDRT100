/* Core0 sole writer. One atomic signed word is a complete requested value.
 * Getter has no product-config/Flash/RTOS dependency and is safe for a future
 * Core1 output-start latch. This slice does not install that consumer. */
static int32_t s_output_delay_ns = PRODUCT_CONFIG_DPLL_OUTPUT_COMPENSATION_DEFAULT_NS;

bool vdc_dpll_manager_get_output_delay_ns(int32_t *value)
{
    if (!value) return false;
    *value = __atomic_load_n(&s_output_delay_ns, __ATOMIC_ACQUIRE);
    return true;
}

static bool output_delay_publish_stopped(void *context)
{
    if (!context) return false;
    __atomic_store_n(&s_output_delay_ns, *(const int32_t *)context, __ATOMIC_RELEASE);
    return true;
}

bool vdc_dpll_manager_set_output_delay_ns(int32_t value)
{
    return get_core_num() == 0u && s_vdc_tdma_service &&
        tdma_service_update_stopped_metadata(s_vdc_tdma_service,
            output_delay_publish_stopped, &value);
}

bool vdc_dpll_manager_default_output_delay(void)
{
    return vdc_dpll_manager_set_output_delay_ns(PRODUCT_CONFIG_DPLL_OUTPUT_COMPENSATION_DEFAULT_NS);
}

static bool output_delay_recall_stopped(void *context)
{
    (void)context;
    int32_t value;
    return product_config_get_dpll_output_compensation_ns(&value) &&
        output_delay_publish_stopped(&value);
}

bool vdc_dpll_manager_recall_output_delay(void)
{
    return get_core_num() == 0u && s_vdc_tdma_service &&
        tdma_service_update_stopped_metadata(s_vdc_tdma_service,
            output_delay_recall_stopped, NULL);
}

static bool output_delay_store_maintenance(void *context)
{
    (void)context;
    int32_t value;
    return vdc_dpll_manager_get_output_delay_ns(&value) &&
        product_config_set_dpll_output_compensation_ns(value);
}

bool vdc_dpll_manager_store_output_delay(void)
{
    return get_core_num() == 0u && s_vdc_tdma_service &&
        tdma_service_run_stopped_maintenance(s_vdc_tdma_service,
            output_delay_store_maintenance, NULL);
}

/* Boot RAM restore only, after product_config_init and before Core1 starts. */
static bool output_delay_init_from_product_config(void)
{
    return get_core_num() == 0u && !vdc_dpll_manager_feedback_session() &&
        output_delay_recall_stopped(NULL);
}
