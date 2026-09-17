/* Core0 is the sole requested-profile writer. Every shared word is atomic;
 * a bounded seqlock observation prevents a mixed tuple without a retry loop.
 * PREPARE copies one complete tuple under the existing whole-client gate. */
_Static_assert(VDC_OUTPUT_TIMING_MIN_US == PRODUCT_CONFIG_VDC_OUTPUT_TIMING_MIN_US &&
    VDC_OUTPUT_TIMING_MAX_US == PRODUCT_CONFIG_VDC_OUTPUT_TIMING_MAX_US &&
    VDC_OUTPUT_TIMING_PLAN_DEFAULT_US == PRODUCT_CONFIG_VDC_OUTPUT_PLAN_DEFAULT_US &&
    VDC_OUTPUT_TIMING_COMMIT_DEFAULT_US == PRODUCT_CONFIG_VDC_OUTPUT_COMMIT_DEFAULT_US &&
    VDC_OUTPUT_TIMING_REFILL_DEFAULT_US == PRODUCT_CONFIG_VDC_OUTPUT_REFILL_DEFAULT_US,
    "output timing defaults and persisted ranges must agree");
static uint32_t s_output_timing_guard;
static vdc_output_timing_profile_t s_output_timing_profile = {
    VDC_OUTPUT_TIMING_PLAN_DEFAULT_US, VDC_OUTPUT_TIMING_COMMIT_DEFAULT_US,
    VDC_OUTPUT_TIMING_REFILL_DEFAULT_US};

bool vdc_dpll_manager_get_output_timing_profile(vdc_output_timing_profile_t *profile)
{
    if (!profile) return false;
    const uint32_t before = __atomic_load_n(&s_output_timing_guard, __ATOMIC_ACQUIRE);
    if (before & 1u) return false;
    const vdc_output_timing_profile_t value = {
        __atomic_load_n(&s_output_timing_profile.plan_ahead_us, __ATOMIC_RELAXED),
        __atomic_load_n(&s_output_timing_profile.commit_ahead_us, __ATOMIC_RELAXED),
        __atomic_load_n(&s_output_timing_profile.refill_low_us, __ATOMIC_RELAXED)};
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (before != __atomic_load_n(&s_output_timing_guard, __ATOMIC_ACQUIRE) ||
        !vdc_output_timing_profile_valid(&value)) return false;
    *profile = value;
    return true;
}

static bool output_timing_publish_stopped(void *context)
{
    const vdc_output_timing_profile_t *profile = context;
    if (!vdc_output_timing_profile_valid(profile) || !vdc_run_output_configuration_idle()) return false;
    (void)__atomic_add_fetch(&s_output_timing_guard, 1u, __ATOMIC_ACQ_REL);
    __atomic_store_n(&s_output_timing_profile.plan_ahead_us, profile->plan_ahead_us, __ATOMIC_RELAXED);
    __atomic_store_n(&s_output_timing_profile.commit_ahead_us, profile->commit_ahead_us, __ATOMIC_RELAXED);
    __atomic_store_n(&s_output_timing_profile.refill_low_us, profile->refill_low_us, __ATOMIC_RELAXED);
    (void)__atomic_add_fetch(&s_output_timing_guard, 1u, __ATOMIC_RELEASE);
    return true;
}

bool vdc_dpll_manager_set_output_timing_profile(const vdc_output_timing_profile_t *profile)
{
    if (get_core_num() != 0u || !s_vdc_tdma_service || !profile) return false;
    const vdc_output_timing_profile_t copy = *profile;
    if (!vdc_output_timing_profile_valid(&copy)) return false;
    return tdma_service_update_stopped_metadata(s_vdc_tdma_service,
        output_timing_publish_stopped, (void *)&copy);
}

bool vdc_dpll_manager_default_output_timing(void)
{
    const vdc_output_timing_profile_t profile = {
        VDC_OUTPUT_TIMING_PLAN_DEFAULT_US, VDC_OUTPUT_TIMING_COMMIT_DEFAULT_US,
        VDC_OUTPUT_TIMING_REFILL_DEFAULT_US};
    return vdc_dpll_manager_set_output_timing_profile(&profile);
}

static bool output_timing_recall_stopped(void *context)
{
    (void)context;
    product_config_vdc_output_timing_profile_t saved;
    if (!product_config_get_vdc_output_timing_profile(&saved)) return false;
    const vdc_output_timing_profile_t profile = {
        saved.plan_ahead_us, saved.commit_ahead_us, saved.refill_low_us};
    return output_timing_publish_stopped((void *)&profile);
}

bool vdc_dpll_manager_recall_output_timing(void)
{
    return get_core_num() == 0u && s_vdc_tdma_service &&
        tdma_service_update_stopped_metadata(s_vdc_tdma_service,
            output_timing_recall_stopped, NULL);
}

static bool output_timing_store_maintenance(void *context)
{
    (void)context;
    vdc_output_timing_profile_t profile;
    if (!vdc_run_output_configuration_idle() || !vdc_dpll_manager_get_output_timing_profile(&profile))
        return false;
    const product_config_vdc_output_timing_profile_t saved = {
        profile.plan_ahead_us, profile.commit_ahead_us, profile.refill_low_us};
    return product_config_set_vdc_output_timing_profile(&saved);
}

bool vdc_dpll_manager_store_output_timing(void)
{
    return get_core_num() == 0u && s_vdc_tdma_service &&
        tdma_service_run_stopped_maintenance(s_vdc_tdma_service,
            output_timing_store_maintenance, NULL);
}

/* product_config_init migrates into RAM before Core1 starts; never write Flash here. */
static bool output_timing_init_from_product_config(void)
{
    return get_core_num() == 0u && !vdc_dpll_manager_feedback_session() &&
        output_timing_recall_stopped(NULL);
}
