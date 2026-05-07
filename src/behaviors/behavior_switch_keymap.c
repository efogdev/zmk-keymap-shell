#include <stdlib.h>
#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include "drivers/behavior.h"
#include "zephyr/logging/log.h"
#include "zephyr/settings/settings.h"
#include "zmk/behavior.h"
#include "drivers/keymap_shell.h"
#if IS_ENABLED(CONFIG_ZMK_FEEDBACK_COMMON)
#include <zmk/feedback_common/feedback_gpio.h>
#endif

#define DT_DRV_COMPAT zmk_behavior_switch_keymap
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);
#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static uint8_t g_dev_num = 0;
static const char* g_devices[CONFIG_ZMK_KEYMAP_SHELL_SLOTS] = { NULL };

struct behavior_switch_keymap_config {
    const uint32_t feedback_duration;
};

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata mtd_param1_values[] = {
    {
        .display_name = "Slot",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_RANGE,
        .range =
            {
                .min = 0,
                .max = CONFIG_ZMK_KEYMAP_SHELL_SLOTS,
            },
    },
};

static const struct behavior_parameter_metadata_set profile_index_metadata_set = {
    .param1_values = mtd_param1_values,
    .param1_values_len = ARRAY_SIZE(mtd_param1_values),
};

static const struct behavior_parameter_metadata_set metadata_sets[] = {profile_index_metadata_set};
static const struct behavior_parameter_metadata metadata = { .sets_len = ARRAY_SIZE(metadata_sets), .sets = metadata_sets};
#endif

// ReSharper disable once CppParameterMayBeConstPtrOrRef
static int on_skmp_binding_pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    const struct device* dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_switch_keymap_config *cfg = dev->config;
    const uint8_t slot_idx = binding->param1 - 1;
    int err = 0;

    if (binding->param1 == 0) {
        keymap_restore();
    } else {
        err = keymap_shell_activate_slot(slot_idx);
    }

#if IS_ENABLED(CONFIG_ZMK_FEEDBACK_COMMON)
    if (err == 0 && cfg->feedback_duration > 0) {
        fbc_trigger(cfg->feedback_duration);
    }
#else
    ARG_UNUSED(err);
    ARG_UNUSED(cfg);
#endif

    return ZMK_BEHAVIOR_OPAQUE;
}

static int behavior_switch_keymap_init(const struct device *dev) {
    g_devices[g_dev_num++] = dev->name;
    return 0;
}

static const struct behavior_driver_api behavior_switch_keymap_driver_api = {
    .binding_pressed = on_skmp_binding_pressed,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define SKMP_INST(n)                                                                                  \
    static const struct behavior_switch_keymap_config behavior_switch_keymap_config_##n = {                    \
        .feedback_duration = DT_INST_PROP_OR(n, feedback_duration, 0),                                   \
    };                                                                                                   \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_switch_keymap_init, NULL, NULL,                                  \
        &behavior_switch_keymap_config_##n, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_switch_keymap_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SKMP_INST)

#endif
