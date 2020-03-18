/*
 * Copyright (c) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "net_lc.h"

#include <kernel.h>
#include <device.h>
#include <sys/ring_buffer.h>
#include <sys/util.h>
#include <net/ppp.h>
#include <drivers/console/uart_pipe.h>
#include <drivers/uart.h>
#include <drivers/gpio.h>
#include <sys/printk.h>

#include "modem_context.h"
#include "modem_iface_uart.h"


#define MDM_POWER_ENABLE		1
#define MDM_POWER_DISABLE		0
#define MDM_RESET_NOT_ASSERTED	1
#define MDM_RESET_ASSERTED		0

#define GSM_CMD_READ_BUF       128
#define GSM_CMD_AT_TIMEOUT     K_SECONDS(2)
#define GSM_CMD_SETUP_TIMEOUT  K_SECONDS(6)
#define GSM_CMD_NETWORK_SCAN_TIMEOUT  K_SECONDS(30)
#define GSM_CMD_NETWORK_SCAN_TIMEOUT2  K_SECONDS(15)
#define GSM_CMD_NETWORK_CONNNECT_TIMEOUT  K_SECONDS(60)
#define GSM_RX_STACK_SIZE      1024
#define GSM_RECV_MAX_BUF       30
#define GSM_RECV_BUF_SIZE      128

static char mdm_imei[MDM_IMEI_LENGTH];
static int status_connect = 0;

/* pin settings */
enum mdm_control_pins {
	MDM_POWER = 0,
	MDM_RESET,
	MDM_WDISABLE_PIN
};

static struct modem_pin modem_pins[] = {
	/* MDM_POWER */
	MODEM_PIN("GPIO_0", 2, GPIO_DIR_OUT),
	/* MDM_RESET */
	MODEM_PIN("GPIO_0", 28, GPIO_DIR_OUT),
	/* MDM_WDISABLE */
	MODEM_PIN("GPIO_0", 29, GPIO_DIR_OUT),
};


static struct gsm_modem {
	struct modem_context context;

	struct modem_cmd_handler_data cmd_handler_data;

	u8_t cmd_read_buf[GSM_CMD_READ_BUF];
	u8_t cmd_match_buf[GSM_CMD_READ_BUF];
	struct k_sem sem_response;

	struct modem_iface_uart_data gsm_data;
	struct k_delayed_work gsm_configure_work;
	char gsm_isr_buf[PPP_MRU];
	char gsm_rx_rb_buf[PPP_MRU * 3];

	bool setup_done;
} gsm;


NET_BUF_POOL_DEFINE(gsm_recv_pool, GSM_RECV_MAX_BUF, GSM_RECV_BUF_SIZE,
		    0, NULL);
K_THREAD_STACK_DEFINE(gsm_rx_stack, GSM_RX_STACK_SIZE);

struct k_thread gsm_rx_thread;

static void gsm_rx(void)
{
	while (true) {
		k_sem_take(&gsm.gsm_data.rx_sem, K_FOREVER);

		gsm.context.cmd_handler.process(&gsm.context.cmd_handler,
						&gsm.context.iface);
	}
}

/**@brief Helper function to check if a response is what was expected
 *
 * @param response Pointer to response prefix
 * @param response_len Length of the response to be checked
 * @param check The buffer with "truth" to verify the response against,
 *		for example "+CGREG"
 *
 * @return True if the provided buffer and check are equal, false otherwise.
 */
bool response_is_valid(const char *response, size_t response_len,
			      const char *check)
{
	if ((response == NULL) || (check == NULL)) {
		printk("Invalid pointer provided\n");
		return false;
	}

	if ((response_len < strlen(check)) ||
	    (memcmp(response, check, response_len) != 0)) {
		return false;
	}

	return true;
}

MODEM_CMD_DEFINE(gsm_cmd_ok)
{
	modem_cmd_handler_set_error(data, 0);
	printk("ok\n");
	k_sem_give(&gsm.sem_response);
}

MODEM_CMD_DEFINE(gsm_cmd_error)
{
	modem_cmd_handler_set_error(data, -EINVAL);
	printk("error\n");
	k_sem_give(&gsm.sem_response);
}
MODEM_CMD_DEFINE(gsm_cmd_connect)
{
	modem_cmd_handler_set_error(data, 0);
	printk("connect\n");
	k_sem_give(&gsm.sem_response);
}
MODEM_CMD_DEFINE(gsm_cmd_msg)
{
	modem_cmd_handler_set_error(data, 0);
	printk("msg\n");
	k_sem_give(&gsm.sem_response);
}
static struct modem_cmd response_cmds[] = {
	MODEM_CMD("OK", gsm_cmd_ok, 0U, "\n"),
	MODEM_CMD("ERROR", gsm_cmd_error, 0U, "\n"),
	MODEM_CMD("CONNECT", gsm_cmd_connect, 0U, "\n"),
	MODEM_CMD(">", gsm_cmd_msg, 0U, "\n"),	
};

/* Handler: <IMEI> */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_imei)
{
	size_t out_len;

	out_len = net_buf_linearize(mdm_imei, sizeof(mdm_imei) - 1,
				    data->rx_buf, 0, len);
	mdm_imei[out_len] = '\0';

	printk("IMEI: %s\n", mdm_imei);
}

/* Handler: <AT+CGREG?> */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_cgreg)
{	
	int err, reg_status;
	size_t out_len;
	char at_response[12];
	struct at_param_list resp_list = {0};
	char  response_prefix[sizeof(AT_CGREG_RESPONSE_PREFIX)] = {0};
	size_t response_prefix_len = sizeof(response_prefix);

	out_len = net_buf_linearize(at_response, sizeof(at_response) - 1,
								data->rx_buf, 0, len);
	printk("Resposta: %s\n", at_response);
	err = at_params_list_init(&resp_list, AT_CGREG_PARAMS_COUNT_MAX);
	if (err) {
		printk("Could not init AT params list, error: %d\n", err);
		return;
	}

	/* Parse CGREG response and populate AT parameter list */
	err = at_parser_max_params_from_str(at_response,
										NULL,
										&resp_list,
										AT_CGREG_PARAMS_COUNT_MAX);
	if (err) {
		printk("Could not parse AT+CGREG response, error: %d\n", err);
		goto clean_exit;
	}

	/* Check if AT command response starts with +CGREG */
	err = at_params_string_get(&resp_list,
				   AT_RESPONSE_PREFIX_INDEX,
				   response_prefix,
				   &response_prefix_len);
	if (err) {
		printk("Could not get response prefix, error: %d\n", err);
		goto clean_exit;
	}

	if (!response_is_valid(response_prefix, response_prefix_len,
			       AT_CGREG_RESPONSE_PREFIX)) {
		/* The unsolicited response is not a CGREG response, ignore it.
		 */
		goto clean_exit;
	}

	/* Get the network registration status parameter from the response */
	err = at_params_int_get(&resp_list, AT_CGREG_READ_REG_STATUS_INDEX,
				&reg_status);
	if (err) {
		printk("Could not get registration status, error: %d\n", err);
		goto clean_exit;
	}

status_connect = reg_status;
	printk("Network registration status: %d\n", reg_status);
	

clean_exit:
	at_params_list_free(&resp_list);

	return;
}

MODEM_CMD_DEFINE(on_cmd_atcmdinfo_csq)
{	
	size_t out_len;
	char at_response[130];

	out_len = net_buf_linearize(at_response, sizeof(at_response) - 1,
								data->rx_buf, 0, len);

	printk("Qualidade do sinal: %s\n", at_response);
	printk("Len: %d\n", len);
}


/* Handler: <AT+COPS=?\> */
MODEM_CMD_DEFINE(on_cmd_atcmdinfo_cops)
{	

	size_t out_len;
	char at_response[130];

	out_len = net_buf_linearize(at_response, sizeof(at_response) - 1,
								data->rx_buf, 0, len);

	printk("Operators: %s\n", at_response);
	printk("Len: %d\n", len);
}

static int pin_init(void)
{
	printk("Setting Modem Pins\n");

	struct modem_context mctx = gsm.context;

	modem_pin_write(&mctx, MDM_RESET, MDM_POWER_DISABLE);

	modem_pin_write(&mctx, MDM_WDISABLE_PIN, MDM_POWER_DISABLE);

	modem_pin_write(&mctx, MDM_POWER, MDM_POWER_DISABLE);
	k_sleep(K_MSEC(60));

	modem_pin_write(&mctx, MDM_POWER, MDM_POWER_ENABLE);
	k_sleep(K_MSEC(300));

	modem_pin_write(&mctx, MDM_POWER, MDM_POWER_DISABLE);
	k_sleep(K_SECONDS(1));	

	modem_pin_config(&mctx, MDM_POWER, GPIO_DIR_IN);

	return 0;
}

static struct setup_cmd setup_cmds[] = {
	/* resets AT command settings to their factory default values */
	SETUP_CMD_NOHANDLE("AT&F0"),
	/* no echo */
	SETUP_CMD_NOHANDLE("ATE0"),	
	/* hang up */
	SETUP_CMD_NOHANDLE("ATH"),
	// funcionalidade do modem 0 - minima
	SETUP_CMD_NOHANDLE("AT+CFUN=0,0"),
	/* extender errors in numeric form */
	SETUP_CMD_NOHANDLE("AT+CMEE=1"),
	//coloca o modem para pegar as frequencias GSM
	SETUP_CMD_NOHANDLE("AT+QCFG=\"band\",0000000F,0,0,1"),
	//configura o roam automatico
	SETUP_CMD_NOHANDLE("AT+QCFG=\"roamservice\",255,1"),
	//configura o servicedomain para PS
	SETUP_CMD_NOHANDLE("AT+QCFG=\"servicedomain\",1,1"),
	//configura a ordem de preferencia (GSM > NB1)
	SETUP_CMD_NOHANDLE("AT+QCFG=\"nwscanseq\",010203,1"),
	//CONFIGURA O SCAN PARA PEGAR APENAS gsm
	SETUP_CMD_NOHANDLE("AT+QCFG=\"nwscanmode\",1,1"),
	/* disable unsolicited network registration codes */
	SETUP_CMD_NOHANDLE("AT+CREG=1"),
	// funcionalidade do modem 1 - Full functionality (Default)
	SETUP_CMD_NOHANDLE("AT+CFUN=1,0"),
	// time zone automatico
	SETUP_CMD_NOHANDLE("AT+CTZU=1"),

	SETUP_CMD("AT+CGREG?", "", on_cmd_atcmdinfo_cgreg, 0U, ""),

};



static struct setup_cmd register_networks[] = {
	// Modem LED should flash on-off-off-off periodically to indicate network search
	//Set to automatic network selection (seleciona a operadora automaticamente
	SETUP_CMD_NOHANDLE("AT+COPS=4,2,\"72411\",0"),

	/* create PDP context */
	SETUP_CMD_NOHANDLE("AT+CGDCONT=2,\"IP\",\"" CONFIG_MODEM_APN "\""), //<cid> = 2
	SETUP_CMD_NOHANDLE("AT+CGACT=1,2"),
	SETUP_CMD_NOHANDLE("AT+CGATT=1"),
};

static struct setup_cmd check_networks[] = {
	// Modem LED should flash on-off-off-off periodically to indicate network search
	//Set to automatic network selection (seleciona a operadora automaticamente
	SETUP_CMD("AT+CGREG?", "", on_cmd_atcmdinfo_cgreg, 0U, ""),
	SETUP_CMD("AT+COPS?", "", on_cmd_atcmdinfo_cops, 0U, ""),
	SETUP_CMD("AT+CSQ", "", on_cmd_atcmdinfo_csq, 0U, ""),
	/* query modem info */
	SETUP_CMD("AT+CGSN", "", on_cmd_atcmdinfo_imei, 0U, ""),
};


static void gsm_configure(struct k_work *work)
{
	int r = -1;
	struct gsm_modem *gsm = CONTAINER_OF(work, struct gsm_modem,
					     gsm_configure_work);

	printk("Starting modem %p configuration\n", gsm);

	pin_init();

	while (r < 0) {
		while (true) {
			r = modem_cmd_send(&gsm->context.iface,
					   &gsm->context.cmd_handler,
					   &response_cmds[0],
					   ARRAY_SIZE(response_cmds),
					   "AT", &gsm->sem_response,
					   GSM_CMD_AT_TIMEOUT);
			if (r < 0) {
				printk("modem not ready %d\n", r);
			} else {
				printk("connect with modem %d\n", r);
				break;
			}
		}

		r = -1;
		while(r < 0){
		printk("VERIFICANDO STATUS DE REDE, QUALIDADE DO SINAL\n");
		r = modem_cmd_handler_setup_cmds(&gsm->context.iface,
						 &gsm->context.cmd_handler,
						 check_networks,
						 ARRAY_SIZE(check_networks),
						 &gsm->sem_response,
						 GSM_CMD_NETWORK_CONNNECT_TIMEOUT);
		if (r < 0) {
			printk("(QUALIDADE DA REDE)modem setup returned %d, %s",
				r, "retrying...\n");
		} else {
			printk("Valor de status_connect: %d, %s",
				status_connect, "Olhar resultados\n");
			break;
				}
		}

		if (status_connect != 5 && status_connect != 1  && status_connect != 2){
			r = modem_cmd_handler_setup_cmds(&gsm->context.iface,
						 &gsm->context.cmd_handler,
						 setup_cmds,
						 ARRAY_SIZE(setup_cmds),
						 &gsm->sem_response,
						 GSM_CMD_NETWORK_SCAN_TIMEOUT);
		if (r < 0) {
			printk("modem setup returned %d, %s",
				r, "retrying...\n");
		} else {
			printk("modem setup returned %d, %s",
				r, "enable net_lc\n");
			break;
		}
		}
		

	}

	if (status_connect != 5 && status_connect != 1){
		r = -1;
		while(r < 0){
			printk("Tentando conectar a rede \n");
			r = modem_cmd_handler_setup_cmds(&gsm->context.iface,
							&gsm->context.cmd_handler,
							register_networks,
							ARRAY_SIZE(register_networks),
							&gsm->sem_response,
							GSM_CMD_NETWORK_CONNNECT_TIMEOUT);
			if (r < 0) {
				printk("(REDE)modem setup returned %d, %s",
					r, "retrying...\n");
			} else {
				printk("(REDE)modem setup returned %d, %s",
					r, "registrado na rede\n");
				break;
			}
		}

	}else {
			printk("already connected\n");
	}

	if(r >= 0) gsm->setup_done = true;
	//TODO: reboot if connection fail?
}

bool net_lc_is_setup_done( void ){
	return gsm.setup_done;
}

void net_lc_get_imei(char * imei){
	memcpy(imei,mdm_imei,sizeof(mdm_imei));
}

int net_lc_cmd_send(const u8_t *buf, u16_t timeout){

	return modem_cmd_send(&gsm.context.iface,
						&gsm.context.cmd_handler,
						NULL, 0,
						buf,
						&gsm.sem_response,
						timeout);
			
}

int net_lc_setup_cmds(struct setup_cmd *cmds,size_t cmds_len, u16_t timeout){

	return modem_cmd_handler_setup_cmds(&gsm.context.iface,
				&gsm.context.cmd_handler,
				cmds,
				cmds_len,
				&gsm.sem_response,
				timeout);
				
}

int net_lc_cmd_send_no_r(const u8_t *buf, u16_t timeout){

	return modem_cmd_send_no_r(&gsm.context.iface,
					&gsm.context.cmd_handler,
					NULL, 0,
					buf,
					&gsm.sem_response,
					timeout);				
}

int net_lc_handler_register(notify_handler handler, u8_t type){

	if(handler == NULL) return -1;

	if(type == MQTT_HANDLER){
		gsm.cmd_handler_data.mqtt_handler = handler;
	}

	return 0;				
}



int gsm_init(struct device *device)
{
	int r;

	printk("Generic GSM modem\n");

	gsm.cmd_handler_data.notify_any_data = true;
	gsm.cmd_handler_data.cmds[CMD_RESP] = response_cmds;
	gsm.cmd_handler_data.cmds_len[CMD_RESP] = ARRAY_SIZE(response_cmds);
	gsm.cmd_handler_data.read_buf = &gsm.cmd_read_buf[0];
	gsm.cmd_handler_data.read_buf_len = sizeof(gsm.cmd_read_buf);
	gsm.cmd_handler_data.match_buf = &gsm.cmd_match_buf[0];
	gsm.cmd_handler_data.match_buf_len = sizeof(gsm.cmd_match_buf);
	gsm.cmd_handler_data.buf_pool = &gsm_recv_pool;

	k_sem_init(&gsm.sem_response, 0, 1);

	r = modem_cmd_handler_init(&gsm.context.cmd_handler,
				   &gsm.cmd_handler_data);
	if (r < 0) {
		printk("cmd handler error %d\n", r);
		return r;
	}

	gsm.gsm_data.isr_buf = &gsm.gsm_isr_buf[0];
	gsm.gsm_data.isr_buf_len = sizeof(gsm.gsm_isr_buf);
	gsm.gsm_data.rx_rb_buf = &gsm.gsm_rx_rb_buf[0];
	gsm.gsm_data.rx_rb_buf_len = sizeof(gsm.gsm_rx_rb_buf);

	r = modem_iface_uart_init(&gsm.context.iface,
				  &gsm.gsm_data, CONFIG_MODEM_UART_NAME);
	if (r < 0) {
		printk("iface uart error %d\n", r);
		return r;
	}

	gsm.context.pins = modem_pins;
	gsm.context.pins_len = ARRAY_SIZE(modem_pins);

	r = modem_context_register(&gsm.context);
	if (r < 0) {
		printk("context error %d\n", r);
		return r;
	}

	k_thread_create(&gsm_rx_thread, gsm_rx_stack,
			K_THREAD_STACK_SIZEOF(gsm_rx_stack),
			(k_thread_entry_t) gsm_rx,
			NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);

	k_delayed_work_init(&gsm.gsm_configure_work, gsm_configure);

	(void)k_delayed_work_submit(&gsm.gsm_configure_work, 0);

	return 0;
}

DEVICE_INIT(bg96_gsm, "bg96", gsm_init, NULL, NULL, POST_KERNEL,42);