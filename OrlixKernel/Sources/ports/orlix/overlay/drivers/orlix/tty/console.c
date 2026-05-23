// SPDX-License-Identifier: GPL-2.0

#include <linux/init.h>
#include <linux/console.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_port.h>
#include <internal/asm/host_console.h>

static struct tty_driver *orlix_tty_driver;
static struct tty_port orlix_tty_port;

static int orlix_tty_activate(struct tty_port *port, struct tty_struct *tty)
{
	(void)port;
	(void)tty;
	return 0;
}

static void orlix_tty_shutdown(struct tty_port *port)
{
	(void)port;
}

static const struct tty_port_operations orlix_tty_port_ops = {
	.activate = orlix_tty_activate,
	.shutdown = orlix_tty_shutdown,
};

static int orlix_tty_install(struct tty_driver *driver, struct tty_struct *tty)
{
	if (tty->index != 0)
		return -ENODEV;
	return tty_port_install(&orlix_tty_port, driver, tty);
}

static int orlix_tty_open(struct tty_struct *tty, struct file *file)
{
	if (tty->index != 0)
		return -ENODEV;
	return tty_port_open(&orlix_tty_port, tty, file);
}

static void orlix_tty_close(struct tty_struct *tty, struct file *file)
{
	tty_port_close(&orlix_tty_port, tty, file);
}

static ssize_t orlix_tty_write(struct tty_struct *tty, const u8 *bytes,
			       size_t length)
{
	(void)tty;
	orlix_host_console_write(bytes, length);
	return length;
}

static unsigned int orlix_tty_write_room(struct tty_struct *tty)
{
	(void)tty;
	return 65536;
}

static const struct tty_operations orlix_tty_ops = {
	.install = orlix_tty_install,
	.open = orlix_tty_open,
	.close = orlix_tty_close,
	.write = orlix_tty_write,
	.write_room = orlix_tty_write_room,
};

static void orlix_tty_console_write(struct console *console,
				    const char *bytes,
				    unsigned int length)
{
	(void)console;
	orlix_host_console_write(bytes, length);
}

static struct tty_driver *orlix_tty_console_device(struct console *console,
						   int *index)
{
	*index = console->index < 0 ? 0 : console->index;
	return orlix_tty_driver;
}

static int __init orlix_tty_console_setup(struct console *console,
					  char *options)
{
	(void)options;
	if (console->index < 0)
		console->index = 0;
	return console->index == 0 ? 0 : -ENODEV;
}

static struct console orlix_tty_console = {
	.name = "ttyS",
	.write = orlix_tty_console_write,
	.device = orlix_tty_console_device,
	.setup = orlix_tty_console_setup,
	.flags = CON_PRINTBUFFER,
	.index = -1,
};

static int __init orlix_tty_console_init(void)
{
	register_console(&orlix_tty_console);
	return 0;
}

console_initcall(orlix_tty_console_init);

static int __init orlix_tty_driver_init(void)
{
	struct device *device;
	struct tty_driver *driver;
	int ret;

	tty_port_init(&orlix_tty_port);
	orlix_tty_port.ops = &orlix_tty_port_ops;

	driver = tty_alloc_driver(1, TTY_DRIVER_RESET_TERMIOS |
				     TTY_DRIVER_REAL_RAW |
				     TTY_DRIVER_DYNAMIC_DEV);
	if (IS_ERR(driver)) {
		ret = PTR_ERR(driver);
		goto destroy_port;
	}

	driver->driver_name = "orlix_tty";
	driver->name = "ttyS";
	driver->major = 0;
	driver->type = TTY_DRIVER_TYPE_SERIAL;
	driver->subtype = SERIAL_TYPE_NORMAL;
	driver->init_termios = tty_std_termios;
	tty_set_operations(driver, &orlix_tty_ops);

	ret = tty_register_driver(driver);
	if (ret)
		goto put_driver;

	device = tty_port_register_device(&orlix_tty_port, driver, 0, NULL);
	if (IS_ERR(device)) {
		ret = PTR_ERR(device);
		goto unregister_driver;
	}

	orlix_tty_driver = driver;
	return 0;

unregister_driver:
	tty_unregister_driver(driver);
put_driver:
	tty_driver_kref_put(driver);
destroy_port:
	tty_port_destroy(&orlix_tty_port);
	return ret;
}

device_initcall(orlix_tty_driver_init);

MODULE_LICENSE("GPL");
