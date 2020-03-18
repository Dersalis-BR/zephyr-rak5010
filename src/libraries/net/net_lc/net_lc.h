#ifndef NET_LC_H__
#define NET_LC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <kernel.h>
#include <device.h>
#include <sys/ring_buffer.h>
#include <sys/util.h>

#include "modem_cmd_handler.h"
#include "at_cmd_parser.h"
#include "at_params.h"

#define CONFIG_MODEM_UART_NAME  "UART_0"
#define CONFIG_MODEM_APN        "your APN here"

#define AT_RESPONSE_PREFIX_INDEX		0
#define AT_CGREG_5				  		"AT+CGREG=5"
#define AT_CGREG_READ			  		"AT+CGREG?"
#define AT_CGREG_RESPONSE_PREFIX  		"+CGREG"
#define AT_CGREG_PARAMS_COUNT_MAX		10
#define AT_CGREG_REG_STATUS_INDEX		1
#define AT_CGREG_READ_REG_STATUS_INDEX	2
#define AT_CGREG_ACTIVE_TIME_INDEX		8
#define AT_CGREG_TAU_INDEX			    9
#define AT_CGREG_RESPONSE_MAX_LEN		80
#define AT_COPS_RESPONSE_PREFIX         "+COPS"
#define AT_CSQ_RESPONSE_PREFIX          "+CSQ"
#define AT_QFLST_RESPONSE_PREFIX        "+QFLST"
#define AT_CSQ_PARAMS_COUNT_MAX		    2
#define MDM_IMEI_LENGTH			16

bool response_is_valid(const char *response, size_t response_len,const char *check);
int net_lc_setup_cmds(struct setup_cmd *cmds,size_t cmds_len, u16_t timeout);
int net_lc_cmd_send(const u8_t *buf, u16_t timeout);
int net_lc_cmd_send_no_r(const u8_t *buf, u16_t timeout);
bool net_lc_is_setup_done( void );
void net_lc_get_imei(char * imei);
int net_lc_handler_register(notify_handler handler, u8_t type);
/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#endif //NET_LC_H__

