
#include "mqtt_lc.h"
#include "net_lc.h"
#include <zephyr.h>
#include <stdio.h>
#include <net/mqtt.h>

#include "certificates.h"

#define MQTT_LC_STACK_SIZE 1024

static char client_id[CLIENT_ID_LEN];

/* MQTT Broker details. */
static struct hostaddr broker_storage;

K_THREAD_STACK_DEFINE(mqtt_lc_stack, MQTT_LC_STACK_SIZE);

struct k_thread mqtt_lc_thread;

/* mqtt status */
enum mdm_control_pins {
	MQTT_BROKER_ONLINE = 0,
	MQTT_BROKER_OFFLINE,
	MQTT_CLIENT_CONNECTED,
	MQTT_CLIENT_DISCONNECTED
};

static struct mqtt_lc {

	u8_t mqtt_conn_status;
	bool setup_done;

} mqtt;

static int mqtt_lc_reconnect( void );
static int mqtt_lc_connect( void );

static void mqtt_lc_handler(char *data, u16_t len)
{	
	u8_t tcpconnectID = 0;
	u8_t err_code = 0;
	u8_t result = 0;

	if((data[4]=='S')&&(data[5]=='T')&&(data[6]=='A')&&(data[6]=='T')){
		tcpconnectID = data[9]  - 48;
		err_code	 = data[11] - 48;
		
		switch (err_code)
		{
			case ERROR_CONN_CLOSED:
				mqtt.mqtt_conn_status = MQTT_CLIENT_DISCONNECTED;
				break;
			case ERROR_LINK_LOSS:
				mqtt.mqtt_conn_status = MQTT_CLIENT_DISCONNECTED;
				break;
			default:
				break;
		}
	}
	else if((data[4]=='R')&&(data[5]=='E')&&(data[6]=='C')&&(data[6]=='V')){

	}	
	else if((data[4]=='P')&&(data[5]=='U')&&(data[6]=='B')&&(data[6]=='E')){

	}
	else if((data[4]=='O')&&(data[5]=='P')&&(data[6]=='E')&&(data[6]=='N')){
		tcpconnectID = data[9]  - 48;
		err_code	 = data[11] - 48;

		switch (err_code)
		{
			case RESULT_NETWORK_OPENED:
				mqtt.mqtt_conn_status = MQTT_BROKER_ONLINE;
				break;
			default:
				mqtt.mqtt_conn_status = MQTT_BROKER_OFFLINE;
				break;
		}
	}
	//+QMTCONN: <tcpconnectID>,<result>[,<ret_code>]
	else if((data[4]=='C')&&(data[5]=='O')&&(data[6]=='N')&&(data[6]=='N')){
		tcpconnectID = data[9]  - 48;
		result   	 = data[11] - 48;
		err_code	 = data[13] - 48;

		switch (err_code)
		{
			case MQTT_CONN_ACCEPTED:
				mqtt.mqtt_conn_status = MQTT_CLIENT_CONNECTED;
				break;
			default:
				mqtt.mqtt_conn_status = MQTT_CLIENT_DISCONNECTED;
				break;
		}

	}

	if(mqtt.mqtt_conn_status == MQTT_CLIENT_DISCONNECTED){
		mqtt_lc_reconnect();
	}

	printk("MQTT STATUS %d\n",mqtt.mqtt_conn_status);
	printk("{%s}\n",data);
}

/* Handler: <AT+QMTCONN?> */
MODEM_CMD_DEFINE(on_cmd_atqmtconn)
{	
	int err, reg_status;
	size_t out_len;
	char at_response[14];

	out_len = net_buf_linearize(at_response, sizeof(at_response) - 1,
								data->rx_buf, 0, len);

	reg_status = at_response[12] - 48;

	switch (reg_status)
	{
		case MQTT_IS_CONNECTED: mqtt.mqtt_conn_status = MQTT_CLIENT_CONNECTED; 		break;	
		default: 				mqtt.mqtt_conn_status = MQTT_CLIENT_DISCONNECTED;	break;
	}

	printk("Mqtt registration status: %d\n", mqtt.mqtt_conn_status);
	
	return;
}

static struct setup_cmd mqtt_conn_status[] = {
	SETUP_CMD("AT+QMTCONN?", "", on_cmd_atqmtconn, 0U, "")
};

static struct setup_cmd mqtt_configs[] = {

//send keepalive message every 30 seconds
SETUP_CMD_NOHANDLE("AT+QMTCFG=\"keepalive\"," TCP_CONNECT_ID "," MQTT_KEEPALIVE),
// SSL: Configure MQTT session into SSL mode
SETUP_CMD_NOHANDLE("AT+QMTCFG=\"ssl\"," TCP_CONNECT_ID ",1,2"),
// SSL: Configure trusted CA certificate
SETUP_CMD_NOHANDLE("AT+QSSLCFG=\"cacert\"," SSL_CTX_ID ",\"" CA_CERTIFICATE_FILE_NAME "\""), 
// SSL: Configure client certificate
SETUP_CMD_NOHANDLE("AT+QSSLCFG=\"clientcert\"," SSL_CTX_ID ",\"" CLIENT_PUBLIC_CERTIFICATE_FILE_NAME "\""),
// SSL: Configure private key
SETUP_CMD_NOHANDLE("AT+QSSLCFG=\"clientkey\"," SSL_CTX_ID ",\"" CLIENT_PRIVATE_KEY_FILE_NAME "\""),
// SSL: Authentication mode: Server and client authentication
SETUP_CMD_NOHANDLE("AT+QSSLCFG=\"seclevel\"," SSL_CTX_ID ",2"),
// SSL: Authentication version. Accept all SSL versions
SETUP_CMD_NOHANDLE("AT+QSSLCFG=\"sslversion\"," SSL_CTX_ID ",4"),

SETUP_CMD_NOHANDLE("AT+QMTCFG=\"version\"," TCP_CONNECT_ID ",4"),
// SSL: Cipher suite: Support all cipher suites
SETUP_CMD_NOHANDLE("AT+QSSLCFG=\"ciphersuite\"," SSL_CTX_ID ",0xFFFF"),
// SSL: Ignore the time of authentication.
SETUP_CMD_NOHANDLE("AT+QSSLCFG=\"ignorelocaltime\"," SSL_CTX_ID),
//Configure the PDP to be used by the MQTT client
SETUP_CMD_NOHANDLE("AT+QMTCFG=\"pdpcid\"," TCP_CONNECT_ID ),

};

static int mqtt_lc_reconnect( void )
{
	int err = -1;

	while(err < 0){
		err = mqtt_lc_connect( );
		if(err < 0) k_sleep(K_SECONDS(2));
	}

	return err;
}

static int mqtt_lc_connect( void )
{
	int r = -1;	
	char command_buffer[60];
	char cmd_model[] = "AT+QMTCONN=%s,\"%s\""; 

    r = net_lc_setup_cmds(mqtt_configs,ARRAY_SIZE(mqtt_configs),K_SECONDS(15));
	if(r < 0) return r;
	else printk("AWS IOT CONFIGURADA\n");

	k_sleep(K_SECONDS(3));

	//printk("AT+QMTOPEN=" TCP_CONNECT_ID ",\"" MQTT_BROKER_HOSTNAME "\"," MQTT_BROKER_PORT "\n");
	r = net_lc_cmd_send("AT+QMTOPEN=" TCP_CONNECT_ID ",\"" MQTT_BROKER_HOSTNAME "\"," MQTT_BROKER_PORT,K_SECONDS(15));
	if(r < 0) return r;
	else printk("AWS IOT CONEXÃO ABERTA\n");

	k_sleep(K_SECONDS(15));	

	sprintf(command_buffer, cmd_model, TCP_CONNECT_ID, client_id);

	//printk("AT+QMTCONN=" TCP_CONNECT_ID ",\"" CLIENT_ID "\"\n"); 		
	r = net_lc_cmd_send(command_buffer,K_SECONDS(15));
	if(r < 0) return r;	
	else printk("AWS IOT CLIENTE CONECTADO\n");

	return r;
}

static int mqtt_certs_delete( void )
{
	int r = -1;
	while (r < 0) {
		r = net_lc_cmd_send("AT+QFDEL=\"*\"",K_SECONDS(15));
	}

	return r;
}

static int mqtt_certs_upload(const u8_t *cert_name, u16_t cert_size, const u8_t *cert){

	int r = -1;
	char command_buffer[40];

	char cmd_model[] = "AT+QFUPL=\"%s\",%d,20";

	sprintf(command_buffer, cmd_model, cert_name, cert_size);

	printk("Enviando %s \n", command_buffer);

	while (r < 0) {
		r = net_lc_cmd_send(command_buffer,K_SECONDS(15));
		
		if (r < 0) {
			printk("erro ao criar cacert %d\n", r);
		} else {			
			r = net_lc_cmd_send_no_r(cert,K_SECONDS(15));
			break;
		}
	}

	return r;
}


static int mqtt_lc_publish(char *msg_topic, char *message)
{

	int r = -1;
	char command_buffer[1540];
	char cmd_model[] = "AT+QMTPUBEX=%s,1,1,0,\"%s\",\"%s\"";

	if(mqtt.mqtt_conn_status != MQTT_CLIENT_CONNECTED) return -ENETUNREACH;

	sprintf(command_buffer, cmd_model,TCP_CONNECT_ID, msg_topic, message);
	printk("Enviando %s \n", command_buffer);

	while (r < 0) {
		r = net_lc_cmd_send(command_buffer,K_SECONDS(15));
		if (r < 0) {
			printk("Erro ao enviar mensagem %d\n", r);
			k_sleep(K_SECONDS(5));
		} 
	}

	return r;
}

static void mqtt_lc(void)
{
	int r = -1;

	printk("***MQTT LC EXE***\n");

	while (true) {

	}
}

/**@brief Initialize the MQTT client structure */
int mqtt_lc_init(struct device *device)
{
    int err = -1;

	/* The mqtt client struct */
	struct mqtt_client client;  

	printk("***MQTT LC INIT***\n"); 
	net_lc_handler_register(mqtt_lc_handler, MQTT_HANDLER);

	while(err < 0){

		if( net_lc_is_setup_done() ){
		   	err = net_lc_setup_cmds(mqtt_conn_status,ARRAY_SIZE(mqtt_conn_status),K_SECONDS(15));
			if(err < 0) k_sleep(K_SECONDS(5));
		}

	}

	net_lc_get_imei(client_id);   
	printk("***MQTT CLIENT ID %s***\n", client_id);

	if(mqtt.mqtt_conn_status != MQTT_CLIENT_CONNECTED)
	{
		while(err < 0){

			if( net_lc_is_setup_done() ){

				k_sleep(K_SECONDS(5));

				err = mqtt_certs_delete();
				if(err < 0) continue;

				err = mqtt_certs_upload(CLIENT_PRIVATE_KEY_FILE_NAME,sizeof(CLIENT_PRIVATE_KEY)-1,CLIENT_PRIVATE_KEY);
				if(err < 0) continue;

				err = mqtt_certs_upload(CLIENT_PUBLIC_CERTIFICATE_FILE_NAME,sizeof(CLIENT_PUBLIC_CERTIFICATE)-1,CLIENT_PUBLIC_CERTIFICATE);
				if(err < 0) continue;

				err = mqtt_certs_upload(CA_CERTIFICATE_FILE_NAME,sizeof(CA_CERTIFICATE)-1,CA_CERTIFICATE);
				
			}

		}

		err = -1;
		while(err < 0){
			err = mqtt_lc_connect( );
			if(err < 0) k_sleep(K_SECONDS(1));
		}
	}

	err = -1;
	while(err < 0){
		err = mqtt_lc_publish("test","corona");
	}
/*
	k_thread_create(&mqtt_lc_thread, mqtt_lc_stack,
					K_THREAD_STACK_SIZEOF(mqtt_lc_stack),
					(k_thread_entry_t) mqtt_lc,
					NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);
*/ 					
}

DEVICE_INIT(mqtt_lc, "mqtt_lc", mqtt_lc_init, NULL, NULL, POST_KERNEL,50);