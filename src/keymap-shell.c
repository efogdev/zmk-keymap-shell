#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
#include <zephyr/settings/settings.h>
#include "zmk/keymap.h"
#include "zmk/matrix.h"

#define DT_DRV_COMPAT zmk_keymap_shell
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if IS_ENABLED(CONFIG_SHELL) && IS_ENABLED(CONFIG_ZMK_KEYMAP_SHELL) && IS_ENABLED(CONFIG_ZMK_KEYMAP_SETTINGS_STORAGE)
#define shprint(_sh, _fmt, ...) \
do { \
  if ((_sh) != NULL) \
    shell_print((_sh), _fmt, ##__VA_ARGS__); \
} while (0)

struct binding_entry {
    ssize_t length;
    uint16_t index;
    uint8_t* data;
};

struct layer_bindings {
    uint16_t count;
    struct binding_entry* entries;
};

struct keymap_slot {
    struct layer_bindings bindings[ZMK_KEYMAP_LAYERS_LEN];

    ssize_t names_size[ZMK_KEYMAP_LAYERS_LEN];
    uint8_t* names_data[ZMK_KEYMAP_LAYERS_LEN];

    ssize_t order_size;
    uint8_t* order_data;

    uint16_t total_size;
    const char* name;

    bool is_free;
};

struct keymap_shell_config {
    bool initialized;
    struct keymap_slot slots[CONFIG_ZMK_KEYMAP_SHELL_SLOTS];
    struct keymap_slot system;
};

struct cb_param {
    const struct shell* sh;
    struct keymap_slot* slot;
};

static struct keymap_shell_config config;

static int clear_slot_cb(const char *key, const size_t len, const settings_read_cb read_cb, void *cb_arg, void *param) {
    char* name_buffer = malloc(strlen(key) + 16);
    if (name_buffer == NULL) {
        LOG_ERR("Failed to allocate memory for key name!");
        return -ENOMEM;
    }

    sprintf(name_buffer, "%s/%s", *(const char **) param, key);
    return settings_delete(name_buffer);
}

static void clear_slot(const char* key) {
    const int err = settings_load_subtree_direct(key, clear_slot_cb, &key);
    if (err != 0) {
        LOG_ERR("Failed to clear slot: %d", err);
    }

    settings_delete(key);
    settings_commit();
}

static int load_slot_cb(const char *key, const size_t len, const settings_read_cb read_cb, void *cb_arg, void *param) {
    const struct cb_param* data = (struct cb_param*) param;

    char *endptr;
    const char *next;
    if (settings_name_steq(key, "_name", &next)) {
        if (len == 0) {
            return -EIO;
        }

        char* name_buffer = malloc(len + 1);
        if (name_buffer == NULL) {
            LOG_ERR("Failed to allocate memory for slot name!");
            return -ENOMEM;
        }
        
        const size_t size = read_cb(cb_arg, name_buffer, len);
        if (size != len) {
            LOG_ERR("Failed to read slot name!");
            free(name_buffer);
            return -EIO;
        }
        
        name_buffer[len] = '\0';
        data->slot->name = name_buffer;
        data->slot->total_size += len;
    } else if (settings_name_steq(key, "layer_order", &next)) {
        shprint(data->sh, " > Found layers order (%d bytes)", len);

        data->slot->order_size = len;
        data->slot->total_size += len;

        data->slot->order_data = malloc(len);
        if (data->slot->order_data == NULL && len > 0) {
            LOG_ERR("Failed to allocate memory for layer order data!");
            return -ENOMEM;
        }

        const size_t size = read_cb(cb_arg, data->slot->order_data, len);
        if (size != len) {
            LOG_ERR("Failed to read layer order data!");
            free(data->slot->order_data);
            data->slot->order_data = NULL;
        }
    } else if (settings_name_steq(key, "l_n", &next) && next) {
        const uint8_t layer = strtoul(next, &endptr, 10);
        data->slot->total_size += len;
        data->slot->names_size[layer] = len;

        shprint(data->sh, " > Found name for layer %d (%d bytes)", layer, len);

        data->slot->names_data[layer] = malloc(len);
        if (data->slot->names_data[layer] == NULL && len > 0) {
            LOG_ERR("Failed to allocate memory for layer name data!");
            return -ENOMEM;
        }

        const size_t size = read_cb(cb_arg, data->slot->names_data[layer], len);
        if (size != len) {
            LOG_ERR("Failed to read layer name!");
            free(data->slot->names_data[layer]);
            data->slot->names_data[layer] = NULL;
        }
    } else if (settings_name_steq(key, "l", &next) && next) {
        const uint8_t layer = strtoul(next, &endptr, 10);
        struct layer_bindings* layer_bindings = &data->slot->bindings[layer];
        const uint16_t new_count = layer_bindings->count + 1;
        struct binding_entry* new_entries = realloc(layer_bindings->entries, new_count * sizeof(struct binding_entry));
        if (new_entries == NULL && new_count > 0) {
            LOG_ERR("Failed to reallocate entries array for layer %d!", layer);
            return -ENOMEM;
        }

        layer_bindings->entries = new_entries;

        uint8_t* binding_data = malloc(len);
        if (binding_data == NULL && len > 0) {
            LOG_ERR("Failed to allocate memory for binding data!");
            return -ENOMEM;
        }
        
        const size_t size = read_cb(cb_arg, binding_data, len);
        if (size != len) {
            LOG_ERR("Failed to read layer bindings!");
            free(binding_data);
            return -EIO;
        }

        const uint8_t pos = strtoul(endptr + 1, &endptr, 10);
        layer_bindings->entries[layer_bindings->count].index = pos;
        layer_bindings->entries[layer_bindings->count].length = len;
        layer_bindings->entries[layer_bindings->count].data = binding_data;
        layer_bindings->count = new_count;
        data->slot->total_size += len;
        
        shprint(data->sh, " > Found binding for layer %d (%d bytes)", layer, len);
    }

    return 0;
}

static void free_slot(struct keymap_slot* slot) {
    if (slot == NULL) {
        return;
    }

    if (slot->name != NULL) {
        free((void*)slot->name);
        slot->name = NULL;
    }

    if (slot->order_data != NULL) {
        free(slot->order_data);
        slot->order_data = NULL;
    }

    for (int i = 0; i < ZMK_KEYMAP_LAYERS_LEN; i++) {
        if (slot->names_data[i] != NULL) {
            free(slot->names_data[i]);
            slot->names_data[i] = NULL;
        }
        slot->names_size[i] = 0;
    }

    for (int i = 0; i < ZMK_KEYMAP_LAYERS_LEN; i++) {
        struct layer_bindings* layer_bindings = &slot->bindings[i];
        if (layer_bindings->entries != NULL) {
            for (uint16_t j = 0; j < layer_bindings->count; j++) {
                if (layer_bindings->entries[j].data != NULL) {
                    free(layer_bindings->entries[j].data);
                }
            }
            free(layer_bindings->entries);
            layer_bindings->entries = NULL;
        }
        layer_bindings->count = 0;
    }

    slot->order_size = 0;
    slot->total_size = 0;
    slot->is_free = true;
}

static void free_all_slots(void) {
    free_slot(&config.system);
    for (int i = 0; i < CONFIG_ZMK_KEYMAP_SHELL_SLOTS; i++) {
        free_slot(&config.slots[i]);
    }

    config.initialized = false;
}

static int keymap_shell_init(void) {
    memset(&config.system, 0, sizeof(config.system));
    for (int i = 0; i < CONFIG_ZMK_KEYMAP_SHELL_SLOTS; i++) {
        memset(&config.slots[i], 0, sizeof(config.slots[i]));
    }

    return 0;
}

static void load_system(const struct shell *sh) {
    keymap_shell_init();
    shprint(sh, "Reading system keymap...");

    struct cb_param data = { .sh = sh, .slot = &config.system };
    int err = settings_load_subtree_direct("keymap", load_slot_cb, &data);
    if (err != 0) {
        LOG_ERR("failed to load system subtree for keymap: %d", err);
    }
    config.system.is_free = config.system.total_size == 0;

    shprint(sh, "");
    shprint(sh, "Reading slots...");
    for (int i = 0; i < CONFIG_ZMK_KEYMAP_SHELL_SLOTS; i++) {
        char key[24];
        sprintf(key, "slots/%d", i);
        data.slot = &config.slots[i];
        err = settings_load_subtree_direct(key, load_slot_cb, &data);
        if (err != 0) {
            LOG_ERR("Failed to load slot %d", i);
        }

        data.slot->is_free = data.slot->total_size == 0;
    }

    config.initialized = true;
    shprint(sh, "");
}

SYS_INIT(keymap_shell_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

static int cmd_destroy(const struct shell *sh, const size_t argc, char **argv) {
    if (!config.initialized) {
        shprint(sh, "Not initialized!");
        shprint(sh, "Use \"keymap init\" or \"keymap status\" first.");
        return 1;
    }

    if (argc <= 1) {
        shprint(sh, "Usage: keymap destroy [slot]");
        shprint(sh, "Example: ");
        shprint(sh, "  keymap destroy 2");
        return 0;
    }

    char* endptr;
    const uint8_t slot_idx = strtoul(argv[1], &endptr, 10) - 1;
    if (slot_idx > CONFIG_ZMK_KEYMAP_SHELL_SLOTS) {
        shprint(sh, "Invalid slot!");
        return -EINVAL;
    }

    char key[16];
    sprintf(key, "slots/%d", slot_idx);
    clear_slot(key);

    shprint(sh, "Successfully destroyed slot.");
    return 0;
}

static int cmd_save(const struct shell *sh, const size_t argc, char **argv) {
    if (!config.initialized) {
        shprint(sh, "Not initialized!");
        shprint(sh, "Use \"keymap init\" or \"keymap status\" first.");
        return 1;
    }

    if (argc <= 2) {
        shprint(sh, "Usage: keymap save [slot] [name]");
        shprint(sh, "Example: ");
        shprint(sh, "  keymap save 2 left_hand");
        return 0;
    }

    char* endptr;
    const uint8_t slot_idx = strtoul(argv[1], &endptr, 10) - 1;
    if (slot_idx > CONFIG_ZMK_KEYMAP_SHELL_SLOTS) {
        shprint(sh, "Invalid slot!");
        return -EINVAL;
    }

    if (strcmp(argv[0], "save") == 0 && !config.slots[slot_idx].is_free) {
        shprint(sh, "The slot is occupied!");
        shprint(sh, "To overwrite, please use \"keymap overwrite\" with the same parameters. ");
        return -EEXIST;
    }

    if (config.system.is_free) {
        shprint(sh, "No overrides found.");
        shprint(sh, "Make changes with ZMK Studio first.");
        return -ENOTSUP;
    }

    char key[32];
    sprintf(key, "slots/%d", slot_idx);
    clear_slot(key);

    sprintf(key, "slots/%d/_name", slot_idx);
    int err = settings_save_one(key, argv[2], strlen(argv[2]));
    if (err != 0) {
        shprint(sh, "Failed to save slot name! Error code = %d", err);
        return err;
    }

    if (config.system.order_size > 0) {
        sprintf(key, "slots/%d/layer_order", slot_idx);
        err = settings_save_one(key, config.system.order_data, config.system.order_size);
        if (err != 0) {
            shprint(sh, "Failed to save layer order! Error code = %d", err);
            return err;
        }
    }

    for (int i = 0; i < ZMK_KEYMAP_LAYERS_LEN; i++) {
        if (config.system.names_size[i] != 0) {
            sprintf(key, "slots/%d/l_n/%d", slot_idx, i);
            err = settings_save_one(key, config.system.names_data[i], config.system.names_size[i]);
            if (err != 0) {
                shprint(sh, "Failed to save layer name! Error code = %d", err);
                return err;
            }
        }

        const struct layer_bindings* layer_bindings = &config.system.bindings[i];
        for (uint16_t j = 0; j < layer_bindings->count; j++) {
            sprintf(key, "slots/%d/l/%d/%d", slot_idx, i, layer_bindings->entries[j].index);
            err = settings_save_one(key, layer_bindings->entries[j].data, layer_bindings->entries[j].length);
            if (err != 0) {
                shprint(sh, "Failed to save layer binding! Error code = %d", err);
                return err;
            }
        }
    }

    settings_commit();
    shprint(sh, "Slot %d (%s) successfully saved!", slot_idx + 1, argv[2]);
    return 0;
}

static int cmd_init(const struct shell *sh, const size_t argc, char **argv) {
    if (config.initialized) {
        shprint(sh, "Already initialized.");
        return 0;
    }

    load_system(NULL);
    return 0;
}

static int cmd_status(const struct shell *sh, const size_t argc, char **argv) {
    bool verbose = false;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        }
    }

    load_system(verbose ? sh : NULL);
    if (config.system.is_free) {
        shprint(sh, "No changes detected: you are running the default keymap.");
        shprint(sh, "");
    }

    bool found_active = false;
    for (int i = 0; i < CONFIG_ZMK_KEYMAP_SHELL_SLOTS; i++) {
        const struct keymap_slot *slot = &config.slots[i];
        if (slot == NULL) {
            continue;
        }

        if (slot->is_free) {
            shprint(sh, "  Slot %d: unoccupied", i + 1);
        } else {
            bool is_active = true;

            if (config.system.order_size != slot->order_size) {
                is_active = false;
            } else if (config.system.order_size > 0 && 
                       memcmp(config.system.order_data, slot->order_data, config.system.order_size) != 0) {
                is_active = false;
            }

            if (is_active) {
                for (int j = 0; j < ZMK_KEYMAP_LAYERS_LEN; j++) {
                    if (config.system.names_size[j] != slot->names_size[j]) {
                        is_active = false;
                        break;
                    }
                    if (config.system.names_size[j] > 0 &&
                        memcmp(config.system.names_data[j], slot->names_data[j], config.system.names_size[j]) != 0) {
                        is_active = false;
                        break;
                    }
                }
            }

            if (is_active) {
                for (int j = 0; j < ZMK_KEYMAP_LAYERS_LEN; j++) {
                    const struct layer_bindings* sys_bindings = &config.system.bindings[j];
                    const struct layer_bindings* slot_bindings = &slot->bindings[j];

                    if (sys_bindings->count != slot_bindings->count) {
                        is_active = false;
                        break;
                    }

                    for (uint16_t k = 0; k < sys_bindings->count; k++) {
                        const struct binding_entry* sys_entry = &sys_bindings->entries[k];
                        
                        const struct binding_entry* slot_entry = NULL;
                        for (uint16_t m = 0; m < slot_bindings->count; m++) {
                            if (slot_bindings->entries[m].index == sys_entry->index) {
                                slot_entry = &slot_bindings->entries[m];
                                break;
                            }
                        }
                        
                        if (slot_entry == NULL ||
                            sys_entry->length != slot_entry->length ||
                            memcmp(sys_entry->data, slot_entry->data, sys_entry->length) != 0) {
                            is_active = false;
                            break;
                        }
                    }

                    if (!is_active) {
                        break;
                    }
                }
            }

            if (is_active) {
                found_active = true;
            }

            shprint(sh, " %sSlot %d: %d bytes, name \"%s\"", is_active ? ">" : " ", i + 1, slot->total_size, slot->name);
        }
    }

    if (!found_active && !config.system.is_free) {
        shprint(sh, "");
        shprint(sh, "Your current keymap has changes that could be stored.");
    }

    return 0;
}

static int cmd_restore(const struct shell *sh, const size_t argc, char **argv) {
    clear_slot("keymap");
    zmk_keymap_discard_changes();
    shprint(sh, "Successfully restored.");
    return 0;
}

static int cmd_free(const struct shell *sh, const size_t argc, char **argv) {
    if (!config.initialized) {
        shprint(sh, "Not initialized — nothing to free.");
        return 0;
    }

    free_all_slots();
    shprint(sh, "Successfully freed all allocated memory and uninitialized.");
    return 0;
}

static int cmd_activate(const struct shell *sh, const size_t argc, char **argv) {
    if (!config.initialized) {
        shprint(sh, "Not initialized!");
        shprint(sh, "Use \"keymap init\" or \"keymap status\" first.");
        return 1;
    }

    if (argc <= 1) {
        shprint(sh, "Usage: keymap activate [slot_index|slot_name]");
        shprint(sh, "Example: ");
        shprint(sh, "  keymap activate 2");
        shprint(sh, "  keymap activate left_hand");
        return 0;
    }

    int err = 0;
    char* endptr;
    uint8_t slot_idx;
    const unsigned long parsed = strtoul(argv[1], &endptr, 10);
    
    if (*endptr == '\0' && parsed > 0 && parsed <= CONFIG_ZMK_KEYMAP_SHELL_SLOTS) {
        slot_idx = parsed - 1;
    } else {
        slot_idx = CONFIG_ZMK_KEYMAP_SHELL_SLOTS;
        for (int i = 0; i < CONFIG_ZMK_KEYMAP_SHELL_SLOTS; i++) {
            if (!config.slots[i].is_free && config.slots[i].name != NULL && 
                strcmp(config.slots[i].name, argv[1]) == 0) {
                slot_idx = i;
                break;
            }
        }
        
        if (slot_idx >= CONFIG_ZMK_KEYMAP_SHELL_SLOTS) {
            shprint(sh, "Slot not found!");
            return -ENOENT;
        }
    }

    if (config.slots[slot_idx].is_free) {
        shprint(sh, "The slot is empty!");
        return -ENOENT;
    }

    clear_slot("keymap");

    const struct keymap_slot* slot = &config.slots[slot_idx];
    if (slot->order_size > 0) {
        err = settings_save_one("keymap/layer_order", slot->order_data, slot->order_size);
        if (err != 0) {
            shprint(sh, "Failed to activate layer order! Error code = %d", err);
            return err;
        }
    }

    char key[32];
    for (int i = 0; i < ZMK_KEYMAP_LAYERS_LEN; i++) {
        if (slot->names_size[i] != 0) {
            sprintf(key, "keymap/l_n/%d", i);
            err = settings_save_one(key, slot->names_data[i], slot->names_size[i]);
            if (err != 0) {
                shprint(sh, "Failed to activate layer name! Error code = %d", err);
                return err;
            }
        }

        const struct layer_bindings* layer_bindings = &slot->bindings[i];
        for (uint16_t j = 0; j < layer_bindings->count; j++) {
            sprintf(key, "keymap/l/%d/%d", i, layer_bindings->entries[j].index);
            err = settings_save_one(key, layer_bindings->entries[j].data, layer_bindings->entries[j].length);
            if (err != 0) {
                shprint(sh, "Failed to activate layer binding! Error code = %d", err);
                return err;
            }
        }
    }

    settings_commit();
    zmk_keymap_discard_changes();
    shprint(sh, "Slot %d (%s) successfully activated!", slot_idx + 1, slot->name);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_keymap,
    SHELL_CMD(init, NULL, "Initialize interactive slots subsystem.", cmd_init),
    SHELL_CMD(status, NULL, "Print status of all slots.", cmd_status),
    SHELL_CMD(save, NULL, "Save current keymap to a slot.", cmd_save),
    SHELL_CMD(overwrite, NULL, "Overwrite slot with the current keymap.", cmd_save),
    SHELL_CMD(activate, NULL, "Activate a saved slot by index or name.", cmd_activate),
    SHELL_CMD(destroy, NULL, "Delete the slot and its data.", cmd_destroy),
    SHELL_CMD(restore, NULL, "Restore the factory default keymap.", cmd_restore),
    SHELL_CMD(free, NULL, "Free all allocated memory and uninitialize.", cmd_free),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(keymap, &sub_keymap, "Keymap management", NULL);
#endif
