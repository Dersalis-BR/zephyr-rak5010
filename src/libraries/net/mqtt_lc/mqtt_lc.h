
#ifndef MQTT_LC_H__
#define MQTT_LC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <kernel.h>
#include <device.h>
#include <sys/util.h>

//Cliente do PEDRO
#define CLIENT_ID_LEN 			16
#define MQTT_BROKER_HOSTNAME    "{your endpoint}.iot.us-east-2.amazonaws.com"

#define MQTT_BROKER_PORT        "8883"
#define MQTT_KEEPALIVE          "30"	 //It defines the maximum time interval between messages received from a client

#define TCP_CONNECT_ID			"2"      //MQTT socket identifier. Currently only one MQTT instance is supported.
#define SSL_CTX_ID				"2"	     //SSL context ID. The range is 0-5.
#define MSG_QOS					"1"      //0 At most once, 1 At least once, 2 Exactly once

#define ERROR_CONN_CLOSED   	1 //Connection is closed or reset by peer
#define ERROR_LINK_LOSS			7 //The link is not alive or the server is unavailable

#define RESULT_NETWORK_FAILED   -1 //Failed to open network
#define RESULT_NETWORK_OPENED   0  //Network opened successfully
#define RESULT_WRONG_PARAMETER  1  //Wrong parameter

#define MQTT_IS_INITIALIZING    1 
#define MQTT_IS_CONNECTING      2
#define MQTT_IS_CONNECTED       3
#define MQTT_IS_DISCONNECTING   4

#define MQTT_CONN_ACCEPTED		0  //Connection Accepted

#define AT_QMTCONN_PARAMS_COUNT_MAX			2
#define AT_QMTCONN_READ_REG_STATUS_INDEX	2
#define AT_QMTCONN_RESPONSE_PREFIX			"+QMTCONN: "

/** Socket address struct for mqtt. */
struct hostaddr {
	u16_t	port;      /* Port number  */
	char*	addr;      /* Host address */
};



/**
 * @}
 */
#ifdef __cplusplus
}
#endif

#endif //MQTT_LC_H__

