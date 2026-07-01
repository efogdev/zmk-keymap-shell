target_sources_ifdef(CONFIG_ZMK_KEYMAP_OUTPUT_ASSIGN app PRIVATE output_keymap.c)
set_source_files_properties(output_keymap.c PROPERTIES COMPILE_FLAGS "-Os")
