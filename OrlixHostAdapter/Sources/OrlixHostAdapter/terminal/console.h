#ifndef ORLIX_HOST_ADAPTER_TERMINAL_CONSOLE_H
#define ORLIX_HOST_ADAPTER_TERMINAL_CONSOLE_H

/* App-private HostAdapter SPI; not a kernel-visible Linux contract. */
__attribute__((visibility("default"))) void orlix_host_console_set_output_fd(int fd);
__attribute__((visibility("default"))) unsigned long orlix_host_console_enqueue_input(
    const void *bytes,
    unsigned long length);

__attribute__((visibility("hidden"))) void orlix_host_console_write(
    const void *bytes,
    unsigned long length);
__attribute__((visibility("hidden"))) unsigned long orlix_host_console_read_input(
    void *bytes,
    unsigned long length);

#endif
