#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <zmk/endpoints.h>
#include <zmk/ble.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>

#include "drivers/keymap_shell.h"

#if IS_ENABLED(CONFIG_ZMK_KEYMAP_OUTPUT_ASSIGN)

#if IS_ENABLED(CONFIG_ZMK_RUNTIME_CONFIG)
#include <zmk_runtime_config/runtime_config.h>
#else
#define ZRC_GET(key, default_val) (default_val)
#endif

#define KMA_ENABLED_KEY "keymap/autoswitch"
#define KMA_EP_COUNT   (1 + ZMK_BLE_PROFILE_COUNT)
static char assign_names[KMA_EP_COUNT][CONFIG_ZMK_KEYMAP_SHELL_SLOT_NAME_MAX];
static bool ready;

static int endpoint_to_epkey(const struct zmk_endpoint_instance ep) {
    if (ep.transport == ZMK_TRANSPORT_BLE) {
        return 1 + ep.ble.profile_index;
    }
    return 0;
}

static int target_to_epkey(const char *s) {
    if (strcmp(s, "usb") == 0) {
        return 0;
    }
    if (strncmp(s, "wireless-", 9) == 0) {
        char *endptr;
        const unsigned long n = strtoul(s + 9, &endptr, 10);
        if (*endptr == '\0' && n >= 1 && n <= ZMK_BLE_PROFILE_COUNT) {
            return (int)n;
        }
    }
    return -1;
}

static void apply_endpoint(const struct zmk_endpoint_instance ep) {
    if (!ZRC_GET(KMA_ENABLED_KEY, 1)) {
        return;
    }

    const int epkey = endpoint_to_epkey(ep);
    if (epkey < 0 || epkey >= KMA_EP_COUNT) {
        return;
    }

    const char *name = assign_names[epkey];
    if (name[0] == '\0') {
        return;
    }

    if (ep.transport == ZMK_TRANSPORT_BLE) {
        const uint8_t pidx = ep.ble.profile_index;
#if IS_ENABLED(CONFIG_ZMK_ESB_ENDPOINT)
        if (pidx != ZMK_BLE_PROFILE_COUNT - 1)
#endif
        {
            if (zmk_ble_profile_is_open(pidx)) {
                return;
            }
        }
    }

    keymap_shell_ensure_initialized();
    const int idx = keymap_shell_resolve_slot(name);
    if (idx < 0) {
        return;
    }
    keymap_shell_activate_slot((uint8_t)idx);
}

static void activate_work_handler(struct k_work *work) {
    if (ready) {
        apply_endpoint(zmk_endpoints_selected());
    }
}
static K_WORK_DEFINE(activate_work, activate_work_handler);

static int load_cb(const char *key, const size_t len, const settings_read_cb read_cb,
                   void *cb_arg, void *param) {
    char *endptr;
    const unsigned long ep = strtoul(key, &endptr, 10);
    if (*endptr != '\0' || ep >= KMA_EP_COUNT) {
        return 0;
    }

    const size_t n = MIN(len, (size_t)(CONFIG_ZMK_KEYMAP_SHELL_SLOT_NAME_MAX - 1));
    const ssize_t rd = read_cb(cb_arg, assign_names[ep], n);
    assign_names[ep][rd > 0 ? rd : 0] = '\0';
    return 0;
}

static void boot_sync_work(struct k_work *work) {
    memset(assign_names, 0, sizeof(assign_names));
    settings_load_subtree_direct("kto", load_cb, NULL);
    ready = true;
    apply_endpoint(zmk_endpoints_selected());
}
static K_WORK_DELAYABLE_DEFINE(boot_work, boot_sync_work);

static int output_keymap_listener(const zmk_event_t *eh) {
    if (ready) {
        k_work_submit(&activate_work);
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(output_keymap, output_keymap_listener);
ZMK_SUBSCRIPTION(output_keymap, zmk_endpoint_changed);

static int output_keymap_init(void) {
#if IS_ENABLED(CONFIG_ZMK_RUNTIME_CONFIG)
    zrc_register(KMA_ENABLED_KEY, 1, 0, 1);
#endif
    k_work_schedule(&boot_work, K_MSEC(CONFIG_ZMK_KEYMAP_OUTPUT_ASSIGN_BOOT_DELAY_MS));
    return 0;
}
SYS_INIT(output_keymap_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

int keymap_assign_cmd(const struct shell *sh, const size_t argc, char **argv) {
    if (argc == 1) {
        shell_print(sh, "Output assignments:");
        for (int ep = 0; ep < KMA_EP_COUNT; ep++) {
            char label[16];
            if (ep == 0) {
                strcpy(label, "usb");
            } else {
                snprintf(label, sizeof(label), "wireless-%d", ep);
            }
            shell_print(sh, "  %-12s %s", label,
                        assign_names[ep][0] ? assign_names[ep] : "(none)");
        }
        return 0;
    }

    const int epkey = target_to_epkey(argv[1]);
    if (epkey < 0) {
        shell_print(sh, "Invalid output. Use: usb, wireless-1..%d", ZMK_BLE_PROFILE_COUNT);
        return -EINVAL;
    }

    char key[24];
    snprintf(key, sizeof(key), "kto/%d", epkey);

    if (argc == 2) {
        settings_delete(key);
        settings_commit();
        assign_names[epkey][0] = '\0';
        shell_print(sh, "Cleared assignment for %s.", argv[1]);
        return 0;
    }

    keymap_shell_ensure_initialized();
    const int idx = keymap_shell_resolve_slot(argv[2]);
    if (idx < 0) {
        shell_print(sh, "Slot not found!");
        return -ENOENT;
    }

    const char *name = keymap_shell_slot_name((uint8_t)idx);
    if (name == NULL) {
        shell_print(sh, "That slot is empty or unnamed. Name it first with \"keymap save\".");
        return -EINVAL;
    }

    const size_t len = strlen(name);
    if (len >= CONFIG_ZMK_KEYMAP_SHELL_SLOT_NAME_MAX) {
        shell_print(sh, "Slot name too long (max %d).", CONFIG_ZMK_KEYMAP_SHELL_SLOT_NAME_MAX - 1);
        return -ENAMETOOLONG;
    }

    const int err = settings_save_one(key, name, len);
    if (err != 0) {
        shell_print(sh, "Failed to save assignment! Error code = %d", err);
        return err;
    }
    settings_commit();

    memcpy(assign_names[epkey], name, len);
    assign_names[epkey][len] = '\0';
    shell_print(sh, "Assigned %s -> slot %d (%s).", argv[1], idx + 1, name);
    return 0;
}

#endif /* CONFIG_ZMK_KEYMAP_OUTPUT_ASSIGN */
