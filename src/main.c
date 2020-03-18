/*
 * Copyright (c) 2020 Dersalis
 *
 */
#include <sys/printk.h>
#include <drivers/uart.h>

void main(void)
{
	printk("Dersalis gateway start\n");

	struct device *uart_dev = device_get_binding("UART_0");

	printk("device %p\n", uart_dev);
	
	for (;;) {	
	}
}
