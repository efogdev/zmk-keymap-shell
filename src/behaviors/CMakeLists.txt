# Copyright (c) 2023 The ZMK Contributors
# SPDX-License-Identifier: MIT

target_sources_ifdef(CONFIG_INPUT app PRIVATE behavior_switch_keymap.c)
