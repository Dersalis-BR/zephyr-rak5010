/*
 * Copyright (c) 2019 Foundries.io
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <init.h>
#include <drivers/gpio.h>
#include "board.h"


static int board_rak5010_init(struct device *dev)
{
	ARG_UNUSED(dev);

#if defined(CONFIG_MODEM_BG96)
	struct device *gpio_dev;

	/* Enable the serial buffer for QUECTEL BG96 modem */
	gpio_dev = device_get_binding(SERIAL_BUFFER_ENABLE_GPIO_NAME);
	if (!gpio_dev) {
		return -ENODEV;
	}

	gpio_pin_configure(gpio_dev, V_INT_DETECT_GPIO_PIN, GPIO_DIR_IN);

	gpio_pin_configure(gpio_dev, SERIAL_BUFFER_ENABLE_GPIO_PIN,
			   GPIO_DIR_OUT);
	gpio_pin_write(gpio_dev, SERIAL_BUFFER_ENABLE_GPIO_PIN, 0);
#endif

	return 0;
}

/* needs to be done after GPIO driver init */
SYS_INIT(board_rak5010_init, POST_KERNEL,
	 CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
