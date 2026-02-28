#pragma once

#include <zephyr/kernel.h>

void keymap_restore();
int keymap_shell_activate_slot(uint8_t slot_idx);
