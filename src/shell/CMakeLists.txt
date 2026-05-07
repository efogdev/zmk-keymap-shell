target_sources(app PRIVATE keymap_shell.c)
set_source_files_properties(keymap_shell.c PROPERTIES COMPILE_FLAGS "-Os")