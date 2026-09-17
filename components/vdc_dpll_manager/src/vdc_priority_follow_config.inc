/* Core0 is the sole requested-configuration writer. One atomic word keeps
 * count/window indivisible; Core1 copies it once per admitted binding. */
#define PRIORITY_FOLLOW_CONFIG_WINDOW_MASK UINT32_C(0x0fffffff)
#define PRIORITY_FOLLOW_CONFIG_COUNT_SHIFT 30u
#define PRIORITY_FOLLOW_CONFIG_RESERVED_MASK UINT32_C(0x30000000)
_Static_assert(VDC_PRIORITY_FOLLOW_BASELINE_REPLACEMENTS <= 2u &&
    VDC_PRIORITY_FOLLOW_BASELINE_WINDOW_NS <= PRIORITY_FOLLOW_CONFIG_WINDOW_MASK,
    "baseline configuration fits one atomic word");
_Static_assert(PRODUCT_CONFIG_DPLL_BASELINE_MAX_REPLACEMENTS == VDC_PRIORITY_FOLLOW_BASELINE_REPLACEMENTS &&
    PRODUCT_CONFIG_DPLL_BASELINE_MAX_WINDOW_NS == VDC_PRIORITY_FOLLOW_BASELINE_WINDOW_NS &&
    PRODUCT_CONFIG_DPLL_BASELINE_DEFAULT_REPLACEMENTS == VDC_PRIORITY_FOLLOW_BASELINE_REPLACEMENTS &&
    PRODUCT_CONFIG_DPLL_BASELINE_DEFAULT_WINDOW_NS == VDC_PRIORITY_FOLLOW_BASELINE_WINDOW_NS,
    "factory defaults and persisted ranges must agree");
static uint32_t s_priority_follow_config_word =
    (VDC_PRIORITY_FOLLOW_BASELINE_REPLACEMENTS << PRIORITY_FOLLOW_CONFIG_COUNT_SHIFT) |
    (uint32_t)VDC_PRIORITY_FOLLOW_BASELINE_WINDOW_NS;

static bool priority_follow_config_valid(const vdc_priority_follow_baseline_config_t *config)
{
    return config && config->max_replacements <= VDC_PRIORITY_FOLLOW_BASELINE_REPLACEMENTS &&
        config->window_ns > 0u && config->window_ns <= VDC_PRIORITY_FOLLOW_BASELINE_WINDOW_NS;
}

static uint32_t priority_follow_config_pack(const vdc_priority_follow_baseline_config_t *config)
{
    return (config->max_replacements << PRIORITY_FOLLOW_CONFIG_COUNT_SHIFT) | config->window_ns;
}

bool vdc_dpll_manager_get_priority_follow_baseline(vdc_priority_follow_baseline_config_t *config)
{
    if (!config) return false;
    const uint32_t word = __atomic_load_n(&s_priority_follow_config_word, __ATOMIC_ACQUIRE);
    const vdc_priority_follow_baseline_config_t copy = {
        .max_replacements = word >> PRIORITY_FOLLOW_CONFIG_COUNT_SHIFT,
        .window_ns = word & PRIORITY_FOLLOW_CONFIG_WINDOW_MASK};
    if ((word & PRIORITY_FOLLOW_CONFIG_RESERVED_MASK) || !priority_follow_config_valid(&copy)) return false;
    *config = copy;
    return true;
}

static bool priority_follow_config_publish_stopped(void *context)
{
    const vdc_priority_follow_baseline_config_t *config = context;
    if (!priority_follow_config_valid(config)) return false;
    __atomic_store_n(&s_priority_follow_config_word, priority_follow_config_pack(config), __ATOMIC_RELEASE);
    return true;
}

bool vdc_dpll_manager_set_priority_follow_baseline(const vdc_priority_follow_baseline_config_t *config)
{
    if (get_core_num() != 0u || !s_vdc_tdma_service || !config) return false;
    const vdc_priority_follow_baseline_config_t copy = *config;
    if (!priority_follow_config_valid(&copy)) return false;
    return tdma_service_update_stopped_metadata(s_vdc_tdma_service,
        priority_follow_config_publish_stopped, (void *)&copy);
}

bool vdc_dpll_manager_default_priority_follow_baseline(void)
{
    const vdc_priority_follow_baseline_config_t config = {
        .max_replacements = VDC_PRIORITY_FOLLOW_BASELINE_REPLACEMENTS,
        .window_ns = (uint32_t)VDC_PRIORITY_FOLLOW_BASELINE_WINDOW_NS};
    return vdc_dpll_manager_set_priority_follow_baseline(&config);
}

static bool priority_follow_config_recall_stopped(void *context)
{
    (void)context;
    product_config_dpll_baseline_profile_t profile;
    if (!product_config_get_dpll_baseline_profile(&profile)) return false;
    const vdc_priority_follow_baseline_config_t config = {
        .max_replacements = profile.max_replacements, .window_ns = profile.window_ns};
    return priority_follow_config_publish_stopped((void *)&config);
}

bool vdc_dpll_manager_recall_priority_follow_baseline(void)
{
    return get_core_num() == 0u && s_vdc_tdma_service &&
        tdma_service_update_stopped_metadata(s_vdc_tdma_service,
            priority_follow_config_recall_stopped, NULL);
}

static bool priority_follow_config_store_maintenance(void *context)
{
    (void)context;
    vdc_priority_follow_baseline_config_t config;
    if (!vdc_dpll_manager_get_priority_follow_baseline(&config)) return false;
    const product_config_dpll_baseline_profile_t profile = {
        .max_replacements = config.max_replacements, .window_ns = config.window_ns};
    /* Flash access is legal only through the separate Core0 maintenance
     * callback. The product-config journal retains its normal XIP lockout. */
    return product_config_set_dpll_baseline_profile(&profile);
}

bool vdc_dpll_manager_store_priority_follow_baseline(void)
{
    return get_core_num() == 0u && s_vdc_tdma_service &&
        tdma_service_run_stopped_maintenance(s_vdc_tdma_service,
            priority_follow_config_store_maintenance, NULL);
}

/* Called only from manager_init before Core1 starts or feedback is enabled.
 * product_config_init has already decoded/migrated Flash into its SRAM copy. */
static bool priority_follow_baseline_init_from_product_config(void)
{
    return get_core_num() == 0u && !vdc_dpll_manager_feedback_session() &&
        priority_follow_config_recall_stopped(NULL);
}
