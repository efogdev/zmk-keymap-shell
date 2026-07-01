#pragma once

#include <zephyr/kernel.h>

struct shell;

void keymap_restore();
int keymap_shell_activate_slot(uint8_t slot_idx);

/* Loads slots from settings if not already initialized. Returns 0. */
int keymap_shell_ensure_initialized(void);

/* Resolves a slot identifier (1-based index or name) to a 0-based index, or -1. */
int keymap_shell_resolve_slot(const char *str);

/* Returns the name of an occupied slot, or NULL if free/unnamed/out of range. */
const char *keymap_shell_slot_name(uint8_t slot_idx);

/* Shell handler for "keymap assign" (defined in the output_keymap service). */
int keymap_assign_cmd(const struct shell *sh, size_t argc, char **argv);
