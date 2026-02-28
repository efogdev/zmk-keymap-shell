# zmk-keymap-shell

Shell commands and behavior(s) for managing multiple keymap profiles on ZMK keyboards.

## What it does

Lets you save and switch between different keymap configurations without reflashing. 
Example use cases include switching between left and right hand, multiple users, a gaming profile with no layers. 

## Usage

Connect to your keyboard's serial shell (with PuTTY on Windows), then:

```
keymap status              # see what's up
keymap save 1 gaming       # save current keymap overrides to slot 1
keymap restore             # restore defaults hardcoded to the firmware
keymap activate gaming     # switch to a stored profile by name or index
keymap destroy 1           # clear slot by index
keymap free                # deinit and free memory
```

Full command list: `init`, `status`, `save`, `overwrite`, `activate`, `destroy`, `restore`, `free`

## Behaviors

For now, there is only one. Use `&skmp` with 1 parameter (slot number) to switch to the slot. Pass `0` to restore defaults.

## Requirements

- ZMK with `CONFIG_SHELL` and `CONFIG_ZMK_KEYMAP_SETTINGS_STORAGE` enabled
- Available storage for keymap data
- Serial shell to access the device

## Integration

Add to your `west.yml`:

```yaml
- name: zmk-keymap-shell
  remote: efogdev
  revision: main
```

Enable in your keyboard config:

```
# config
CONFIG_ZMK_KEYMAP_SHELL_SLOTS=4

# required
CONFIG_ZMK_KEYMAP_SHELL=y
CONFIG_CBPRINTF_COMPLETE=y
CONFIG_CBPRINTF_FP_SUPPORT=y
CONFIG_SHELL=y
CONFIG_SHELL_BACKEND_SERIAL_CHECK_DTR=y
CONFIG_UART_LINE_CTRL=y
CONFIG_SHELL_BACKEND_SERIAL_INIT_PRIORITY=51
CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT=n

# optional, recommended
CONFIG_INIT_STACKS=n
CONFIG_THREAD_STACK_INFO=n
CONFIG_KERNEL_SHELL=n
CONFIG_THREAD_MONITOR=n
CONFIG_BOOT_BANNER=n
CONFIG_THREAD_NAME=n
CONFIG_DEVICE_SHELL=n
CONFIG_POSIX_CLOCK=y
CONFIG_DATE_SHELL=n
CONFIG_THREAD_RUNTIME_STATS=n
CONFIG_THREAD_RUNTIME_STATS_USE_TIMING_FUNCTIONS=n
CONFIG_STATS=n
CONFIG_STATS_SHELL=n
```

## License

MIT
