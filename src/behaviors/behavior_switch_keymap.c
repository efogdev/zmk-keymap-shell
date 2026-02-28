#include <stdlib.h>
#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include "drivers/behavior.h"
#include "zephyr/drivers/gpio.h"
#include "zephyr/logging/log.h"
#include "zephyr/settings/settings.h"
#include "zmk/behavior.h"
#include "drivers/keymap_shell.h"

#define DT_DRV_COMPAT zmk_behavior_switch_keymap
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);
#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static uint8_t g_dev_num = 0;
static const char* g_devices[CONFIG_ZMK_KEYMAP_SHELL_SLOTS] = { NULL };
static int g_from_settings = -1;

struct behavior_switch_keymap_config {
    const struct gpio_dt_spec feedback_gpios;
    const struct gpio_dt_spec feedback_extra_gpios;
    const uint32_t feedback_duration;
};

struct behavior_switch_keymap_data {
    const struct device *dev;
    struct k_work_delayable feedback_off_work;
    int previous_feedback_extra_state;
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
    struct behavior_switch_keymap_data *data = dev->data;
    const uint8_t slot_idx = binding->param1 - 1;
    int err = 0;

    if (binding->param1 == 0) {
        keymap_restore();
    } else {
        err = keymap_shell_activate_slot(slot_idx);
    }

    if (err == 0) {
        if (cfg->feedback_gpios.port != NULL) {
            if (cfg->feedback_extra_gpios.port != NULL) {
                data->previous_feedback_extra_state = gpio_pin_get_dt(&cfg->feedback_extra_gpios);
                gpio_pin_set_dt(&cfg->feedback_extra_gpios, 1);
            }
            gpio_pin_set_dt(&cfg->feedback_gpios, 1);
            
            if (cfg->feedback_duration > 0) {
                k_work_reschedule(&data->feedback_off_work, K_MSEC(cfg->feedback_duration));
            }
        }
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static void feedback_off_work_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    const struct behavior_switch_keymap_data *data = CONTAINER_OF(dwork, struct behavior_switch_keymap_data, feedback_off_work);
    const struct device *dev = data->dev;
    const struct behavior_switch_keymap_config *config = dev->config;

    if (config->feedback_extra_gpios.port != NULL) {
        gpio_pin_set_dt(&config->feedback_extra_gpios, data->previous_feedback_extra_state);
    }

    if (config->feedback_gpios.port != NULL) {
        gpio_pin_set_dt(&config->feedback_gpios, 0);
    }

    LOG_DBG("Feedback turned off, extra GPIOs restored to previous state");
}

static int behavior_switch_keymap_init(const struct device *dev) {
    const struct behavior_switch_keymap_config *cfg = dev->config;
    struct behavior_switch_keymap_data *data = dev->data;
    data->previous_feedback_extra_state = 0;

    if (cfg->feedback_gpios.port != NULL) {
        if (gpio_pin_configure_dt(&cfg->feedback_gpios, GPIO_OUTPUT) != 0) {
            LOG_WRN("Failed to configure rate limit feedback GPIO");
        } else {
            LOG_DBG("Rate limit feedback GPIO configured");
        }

        k_work_init_delayable(&data->feedback_off_work, feedback_off_work_cb);
    } else {
        LOG_DBG("No feedback set up for rate limit cycling");
    }

    if (cfg->feedback_extra_gpios.port != NULL) {
        if (gpio_pin_configure_dt(&cfg->feedback_extra_gpios, GPIO_OUTPUT) != 0) {
            LOG_WRN("Failed to configure rate limit extra feedback GPIO");
        } else {
            LOG_DBG("Rate limit extra feedback GPIO configured");
        }
    } else {
        LOG_DBG("No extra feedback set up for rate limit cycling");
    }

    data->dev = dev;
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
    static struct behavior_switch_keymap_data behavior_switch_keymap_data_##n = {};                            \
    static const struct behavior_switch_keymap_config behavior_switch_keymap_config_##n = {                    \
        .feedback_gpios = GPIO_DT_SPEC_INST_GET_OR(n, feedback_gpios, { .port = NULL }),                 \
        .feedback_extra_gpios = GPIO_DT_SPEC_INST_GET_OR(n, feedback_extra_gpios, { .port = NULL }),     \
        .feedback_duration = DT_INST_PROP_OR(n, feedback_duration, 0),                                   \
    };                                                                                                   \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_switch_keymap_init, NULL, &behavior_switch_keymap_data_##n,            \
        &behavior_switch_keymap_config_##n, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_switch_keymap_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SKMP_INST)

#endif
