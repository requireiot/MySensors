/*
* The MySensors Arduino library handles the wireless radio link and protocol
* between your home built sensors/actuators and HA controller of choice.
* The sensors forms a self healing radio network with optional repeaters. Each
* repeater and gateway builds a routing tables in EEPROM which keeps track of the
* network topology allowing messages to be routed to nodes.
*
* Created by Henrik Ekblad <henrik.ekblad@mysensors.org>
* Copyright (C) 2013-2022 Sensnology AB
* Full contributor list: https://github.com/mysensors/MySensors/graphs/contributors
*
* Documentation: http://www.mysensors.org
* Support Forum: http://forum.mysensors.org
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* version 2 as published by the Free Software Foundation.
*/

/*
 * Modified by Eric Grammatico <eric@grammatico.me>
 *
 * Added support to secured connexion to mqtt server thanks to WiFiClientSecure class.
 * Please see comments in code. You can look for WiFiClientSecure, MY_GATEWAY_ESP8266_SECURE,
 * MY_MQTT_CA_CERT, MY_MQTT_FINGERPRINT and MY_MQTT_CLIENT_CERT in the code below to see what has
 * changed. No new method, no new class to be used by my_sensors.
 *
 * The following constants have to be defined from the gateway code:
 * MY_GATEWAY_ESP8266_SECURE    in place of MY_GATEWAY_ESP8266 to go to secure connexions.
 * MY_MQTT_CA_CERTx            Up to three root Certificates Authorities could be defined
 *                              to validate the mqtt server' certificate. The most secure.
 * MY_MQTT_FINGERPRINT           Alternatively, the mqtt server' certificate finger print
 *                              could be used. Less secure and less convenient as you'll
 *                              have to update the fingerprint each time the mqtt server'
 *                              certificate is updated
 *                              If neither MY_MQTT_CA_CERT1 nor MY_MQTT_FINGERPRINT are
 *                              defined, insecure connexion will be established. The mqtt
 *                              server' certificate will not be validated.
 * MY_MQTT_CLIENT_CERT           The mqtt server may require client certificate for
 * MY_MQTT_CLIENT_KEY            authentication.
 *
 */

// Topic structure: MY_MQTT_PUBLISH_TOPIC_PREFIX/NODE-ID/SENSOR-ID/CMD-TYPE/ACK-FLAG/SUB-TYPE

#include "MyGatewayTransport.h"

// housekeeping, remove for 3.0.0
#ifdef MY_ESP8266_SSID
#warning MY_ESP8266_SSID is deprecated, use MY_WIFI_SSID instead!
#define MY_WIFI_SSID MY_ESP8266_SSID
#undef MY_ESP8266_SSID // cleanup
#endif

#ifdef MY_ESP8266_PASSWORD
#warning MY_ESP8266_PASSWORD is deprecated, use MY_WIFI_PASSWORD instead!
#define MY_WIFI_PASSWORD MY_ESP8266_PASSWORD
#undef MY_ESP8266_PASSWORD // cleanup
#endif

#ifdef MY_ESP8266_BSSID
#warning MY_ESP8266_BSSID is deprecated, use MY_WIFI_BSSID instead!
#define MY_WIFI_BSSID MY_ESP8266_BSSID
#undef MY_ESP8266_BSSID // cleanup
#endif

#ifdef MY_ESP8266_HOSTNAME
#warning MY_ESP8266_HOSTNAME is deprecated, use MY_HOSTNAME instead!
#define MY_HOSTNAME MY_ESP8266_HOSTNAME
#undef MY_ESP8266_HOSTNAME // cleanup
#endif

#ifdef MY_MQTT_CA_CERT
#warning MY_MQTT_CA_CERT is deprecated, please use MY_MQTT_CA_CERT1 instead!
#define MY_MQTT_CA_CERT1 MY_MQTT_CA_CERT
//#undef MY_MQTT_CA_CERT // cleanup
#endif

#ifndef MY_MQTT_USER
#define MY_MQTT_USER NULL
#endif

#ifndef MY_MQTT_PASSWORD
#define MY_MQTT_PASSWORD NULL
#endif

#if defined(MY_GATEWAY_ESP8266) || defined(MY_GATEWAY_ESP8266_SECURE) || defined(MY_GATEWAY_ESP32_WIFI)
#if !defined(MY_WIFI_SSID)
#error ESP8266/ESP32 MQTT gateway: MY_WIFI_SSID not defined!
#endif
#endif

#if defined MY_CONTROLLER_IP_ADDRESS
#define _brokerIp IPAddress(MY_CONTROLLER_IP_ADDRESS)
#endif

#if defined(MY_IP_ADDRESS)
#define _MQTT_clientIp IPAddress(MY_IP_ADDRESS)
#if defined(MY_IP_GATEWAY_ADDRESS)
#define _gatewayIp IPAddress(MY_IP_GATEWAY_ADDRESS)
#elif defined(MY_GATEWAY_ESP8266) || defined(MY_GATEWAY_ESP8266_SECURE) || defined(MY_GATEWAY_ESP32)
// Assume the gateway will be the machine on the same network as the local IP
// but with last octet being '1'
#define _gatewayIp IPAddress(_MQTT_clientIp[0], _MQTT_clientIp[1], _MQTT_clientIp[2], 1)
#endif /* End of MY_IP_GATEWAY_ADDRESS */

#if defined(MY_IP_SUBNET_ADDRESS)
#define _subnetIp IPAddress(MY_IP_SUBNET_ADDRESS)
#elif defined(MY_GATEWAY_ESP8266) || defined(MY_GATEWAY_ESP8266_SECURE) || defined(MY_GATEWAY_ESP32)
#define _subnetIp IPAddress(255, 255, 255, 0)
#endif /* End of MY_IP_SUBNET_ADDRESS */
#endif /* End of MY_IP_ADDRESS */

#if defined(MY_GATEWAY_ESP8266) || defined(MY_GATEWAY_ESP32_WIFI) || defined(MY_GATEWAY_ESP32_ETHERNET)
#define EthernetClient WiFiClient
#elif defined(MY_GATEWAY_ESP8266_SECURE)
#define EthernetClient WiFiClientSecure
#if defined(MY_MQTT_CA_CERT1)
BearSSL::X509List certAuth; //List to store Certificat Authorities
#endif
#if defined(MY_MQTT_CLIENT_CERT) && defined(MY_MQTT_CLIENT_KEY)
BearSSL::X509List clientCert; //Client public key
BearSSL::PrivateKey clientPrivKey; //Client private key
#endif
// Set time via NTP, as required for x.509 validation
// BearSSL checks NotBefore and NotAfter dates in certificates
// Thus an approximated date/time is needed.
void setClock()
{
	configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");

	Serial.print("Waiting for NTP time sync: ");
	time_t now = time(nullptr);
	while (now < 8 * 3600 * 2) {
		delay(500);
		Serial.print(".");
		now = time(nullptr);
	}
	Serial.println("");
	struct tm timeinfo;
	gmtime_r(&now, &timeinfo);
	Serial.print("Current time: ");
	Serial.print(asctime(&timeinfo));
}
#elif defined(MY_GATEWAY_LINUX)
// Nothing to do here
#else
uint8_t _MQTT_clientMAC[] = { MY_MAC_ADDRESS };
#endif /* End of MY_GATEWAY_ESPxy */

#if defined(MY_GATEWAY_TINYGSM)
#if defined(MY_GSM_RX) && defined(MY_GSM_TX)
SoftwareSerial SerialAT(MY_GSM_RX, MY_GSM_TX);
#endif
static TinyGsm modem(SerialAT);
static TinyGsmClient _MQTT_ethClient(modem);
#if defined(MY_GSM_BAUDRATE)
uint32_t rate = MY_GSM_BAUDRATE;
#else /* Else part of MY_GSM_BAUDRATE */
uint32_t rate = 0;
#endif /* End of MY_GSM_BAUDRATE */
#else /* Else part of MY_GATEWAY_TINYGSM */
static EthernetClient _MQTT_ethClient;
#endif /* End of MY_GATEWAY_TINYGSM */

static PubSubClient _MQTT_client(_MQTT_ethClient);
static bool _MQTT_connecting = true;
static bool _MQTT_available = false;
static MyMessage _MQTT_msg;

// cppcheck-suppress constParameter
bool gatewayTransportSend(MyMessage &message)
{
	if (!_MQTT_client.connected()) {
		return false;
	}
	setIndication(INDICATION_GW_TX);
	char *topic = protocolMyMessage2MQTT(MY_MQTT_PUBLISH_TOPIC_PREFIX, message);
	GATEWAY_DEBUG(PSTR("GWT:TPS:TOPIC=%s,MSG SENT\n"), topic);
#if defined(MY_MQTT_CLIENT_PUBLISH_RETAIN)
	const bool retain = message.getCommand() == C_SET ||
	                    (message.getCommand() == C_INTERNAL && message.getType() == I_BATTERY_LEVEL);
#else
	const bool retain = false;
#endif /* End of MY_MQTT_CLIENT_PUBLISH_RETAIN */
	return _MQTT_client.publish(topic, message.getString(_convBuffer), retain);
}

void incomingMQTT(char *topic, uint8_t *payload, unsigned int length)
{
	GATEWAY_DEBUG(PSTR("GWT:IMQ:TOPIC=%s, MSG RECEIVED\n"), topic);
	_MQTT_available = protocolMQTT2MyMessage(_MQTT_msg, topic, payload, length);
	setIndication(INDICATION_GW_RX);
}

bool reconnectMQTT(void)
{
	GATEWAY_DEBUG(PSTR("GWT:RMQ:CONNECTING...\n"));

#if defined(MY_GATEWAY_ESP8266_SECURE)
	// Date/time are retrieved to be able to validate certificates.
	setClock();
#endif

	// Attempt to connect
	if (_MQTT_client.connect(MY_MQTT_CLIENT_ID, MY_MQTT_USER, MY_MQTT_PASSWORD)) {
		GATEWAY_DEBUG(PSTR("GWT:RMQ:OK\n"));
		// Send presentation of locally attached sensors (and node if applicable)
		presentNode();
		// Once connected, publish subscribe
		char inTopic[strlen(MY_MQTT_SUBSCRIBE_TOPIC_PREFIX) + strlen("/+/+/+/+/+") + 1];
		(void)strncpy(inTopic, MY_MQTT_SUBSCRIBE_TOPIC_PREFIX, strlen(MY_MQTT_SUBSCRIBE_TOPIC_PREFIX) + 1);
		(void)strcat(inTopic, "/+/+/+/+/+");
		_MQTT_client.subscribe(inTopic);

		return true;
	}
	delay(1000);
	GATEWAY_DEBUG(PSTR("!GWT:RMQ:FAIL\n"));
#if defined(MY_GATEWAY_ESP8266_SECURE)
	char sslErr[256];
	int errID = _MQTT_ethClient.getLastSSLError(sslErr, sizeof(sslErr));
	GATEWAY_DEBUG(PSTR("!GWT:RMQ:(%d) %s\n"), errID, sslErr);
#endif
	return false;
}

bool gatewayTransportConnect(void)
{
#if defined(MY_GATEWAY_ESP8266) || defined(MY_GATEWAY_ESP8266_SECURE) || defined(MY_GATEWAY_ESP32_WIFI)
	if (WiFi.status() != WL_CONNECTED) {
		GATEWAY_DEBUG(PSTR("GWT:TPC:CONNECTING...\n"));
		delay(1000);
		return false;
	}
	GATEWAY_DEBUG(PSTR("GWT:TPC:IP=%s\n"), WiFi.localIP().toString().c_str());

#elif defined(MY_GATEWAY_LINUX)
#if defined(MY_IP_ADDRESS)
	_MQTT_ethClient.bind(_MQTT_clientIp);
#endif /* End of MY_IP_ADDRESS */
#elif defined(MY_GATEWAY_TINYGSM)
	GATEWAY_DEBUG(PSTR("GWT:TPC:IP=%s\n"), modem.getLocalIP().c_str());
#else
#if defined(MY_IP_ADDRESS)
	Ethernet.begin(_MQTT_clientMAC, _MQTT_clientIp);
#else /* Else part of MY_IP_ADDRESS */
  /* with ESP32 Ethernet, initialization is done _outside_ MySensors */
  #if !defined(MY_GATEWAY_ESP32_ETHERNET)
	// Get IP address from DHCP
	if (!Ethernet.begin(_MQTT_clientMAC)) {
		GATEWAY_DEBUG(PSTR("!GWT:TPC:DHCP FAIL\n"));
		_MQTT_connecting = false;
		return false;
	}
  #endif
 #endif /* End of MY_IP_ADDRESS */
	GATEWAY_DEBUG(PSTR("GWT:TPC:IP=%" PRIu8 ".%" PRIu8 ".%" PRIu8 ".%" PRIu8 "\n"),
	              Ethernet.localIP()[0],
	              Ethernet.localIP()[1], Ethernet.localIP()[2], Ethernet.localIP()[3]);
	// give the Ethernet interface a second to initialize
	delay(1000);
#endif
	return true;
}

bool gatewayTransportInit(void)
{
	_MQTT_connecting = true;

#if defined(MY_GATEWAY_TINYGSM)

#if !defined(MY_GSM_BAUDRATE)
	rate = TinyGsmAutoBaud(SerialAT);
#endif /* End of MY_GSM_BAUDRATE */

	SerialAT.begin(rate);
	delay(3000);

	modem.restart();

#if defined(MY_GSM_PIN) && !defined(TINY_GSM_MODEM_ESP8266)
	modem.simUnlock(MY_GSM_PIN);
#endif /* End of MY_GSM_PIN */

#ifndef TINY_GSM_MODEM_ESP8266
	if (!modem.waitForNetwork()) {
		GATEWAY_DEBUG(PSTR("!GWT:TIN:ETH FAIL\n"));
		while (true);
	}
	GATEWAY_DEBUG(PSTR("GWT:TIN:ETH OK\n"));

	if (!modem.gprsConnect(MY_GSM_APN, MY_GSM_USR, MY_GSM_PSW)) {
		GATEWAY_DEBUG(PSTR("!GWT:TIN:ETH FAIL\n"));
		while (true);
	}
	GATEWAY_DEBUG(PSTR("GWT:TIN:ETH OK\n"));
	delay(1000);
#else /* Else part of TINY_GSM_MODEM_ESP8266 */
	if (!modem.networkConnect(MY_GSM_SSID, MY_GSM_PSW)) {
		GATEWAY_DEBUG(PSTR("!GWT:TIN:ETH FAIL\n"));
		while (true);
	}
	GATEWAY_DEBUG(PSTR("GWT:TIN:ETH OK\n"));
	delay(1000);
#endif /* End of TINY_GSM_MODEM_ESP8266 */

#endif /* End of MY_GATEWAY_TINYGSM */

#if defined(MY_CONTROLLER_IP_ADDRESS)
	_MQTT_client.setServer(_brokerIp, MY_PORT);
#else
	_MQTT_client.setServer(MY_CONTROLLER_URL_ADDRESS, MY_PORT);
#endif /* End of MY_CONTROLLER_IP_ADDRESS */

	_MQTT_client.setCallback(incomingMQTT);

#if defined(MY_GATEWAY_ESP8266) || defined(MY_GATEWAY_ESP8266_SECURE) || defined(MY_GATEWAY_ESP32_WIFI)
	// Turn off access point
	WiFi.mode(WIFI_STA);
#if defined(MY_GATEWAY_ESP8266) || defined(MY_GATEWAY_ESP8266_SECURE)
	WiFi.hostname(MY_HOSTNAME);
#elif defined(MY_GATEWAY_ESP32)
	WiFi.setHostname(MY_HOSTNAME);
#endif
#if defined(MY_IP_ADDRESS)
	WiFi.config(_MQTT_clientIp, _gatewayIp, _subnetIp);
#endif /* End of MY_IP_ADDRESS */
	(void)WiFi.begin(MY_WIFI_SSID, MY_WIFI_PASSWORD, 0, MY_WIFI_BSSID);
#endif

#if defined(MY_GATEWAY_ESP8266_SECURE)
	// Certificate Authorities are stored in the X509 list
	// At least one is needed, but you may need two, or three
	// eg to validate one certificate from  LetsEncrypt two is needed
#if defined(MY_MQTT_CA_CERT1)
	certAuth.append(MY_MQTT_CA_CERT1);
#if defined(MY_MQTT_CA_CERT2)
	certAuth.append(MY_MQTT_CA_CERT2);
#endif
#if defined(MY_MQTT_CA_CERT3)
	certAuth.append(MY_MQTT_CA_CERT3);
#endif
	_MQTT_ethClient.setTrustAnchors(&certAuth);
#elif defined(MY_MQTT_FINGERPRINT) //MY_MQTT_CA_CERT1
	// Alternatively, the certificate could be validated with its
	// fingerprint, which is less secure
	_MQTT_ethClient.setFingerprint(MY_MQTT_FINGERPRINT);
#else //MY_MQTT_CA_CERT1
	// At last, an insecure connexion is accepted. Meaning the
	// server's certificate is not validated.
	_MQTT_ethClient.setInsecure();
	GATEWAY_DEBUG(PSTR("GWT:TPC:CONNECTING WITH INSECURE SETTING...\n"));
#endif //MY_MQTT_CA_CERT1
#if defined(MY_MQTT_CLIENT_CERT) && defined(MY_MQTT_CLIENT_KEY)
	// The server may required client certificate
	clientCert.append(MY_MQTT_CLIENT_CERT);
	clientPrivKey.parse(MY_MQTT_CLIENT_KEY);
	_MQTT_ethClient.setClientRSACert(&clientCert, &clientPrivKey);
#endif
#endif //MY_GATEWAY_ESP8266_SECURE

	gatewayTransportConnect();

	_MQTT_connecting = false;
	return true;
}

bool gatewayTransportAvailable(void)
{
	if (_MQTT_connecting) {
		return false;
	}
#if defined(MY_GATEWAY_ESP8266) || defined(MY_GATEWAY_ESP8266_SECURE) || defined(MY_GATEWAY_ESP32_WIFI)
	if (WiFi.status() != WL_CONNECTED) {
#if defined(MY_GATEWAY_ESP32)
		(void)gatewayTransportInit();
#endif
		return false;
	}
#endif
	if (!_MQTT_client.connected()) {
		//reinitialise client
		if (gatewayTransportConnect()) {
			reconnectMQTT();
		}
		return false;
	}
	_MQTT_client.loop();
	return _MQTT_available;
}

MyMessage & gatewayTransportReceive(void)
{
	// Return the last parsed message
	_MQTT_available = false;
	return _MQTT_msg;
}
