/*
 R. J. Tidey 2017/10/17
 Basic temp sensor
 Can action via a URL like snapshot from a camera
 Reports temperature to Easy IoT server.
 Web software update service included
 WifiManager can be used to config wifi network
 
 Temperature reporting Code based on work by Igor Jarc
 Some code based on https://github.com/DennisSc/easyIoT-ESPduino/blob/master/sketches/ds18b20.ino
 
 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2 as published by the Free Software Foundation.
 */

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "Base64.h"
#include <DNSServer.h>
#include <WiFiManager.h>

/*
Wifi Manager Web set up
If WM_NAME defined then use WebManager
*/
#define WM_NAME "tempSetup"
#define WM_PASSWORD "password"
#ifdef WM_NAME
	WiFiManager wifiManager;
#endif
//uncomment to use a static IP
//#define WM_STATIC_IP 192,168,0,100
//#define WM_STATIC_GATEWAY 192,168,0,1

int timeInterval = 50;
#define WIFI_CHECK_TIMEOUT 30000
unsigned long elapsedTime;
unsigned long wifiCheckTime;
unsigned long tempCheckTime;
unsigned long tempReportTime;

//For update service
String host = "esp8266-hall";
const char* update_path = "/firmware";
const char* update_username = "admin";
const char* update_password = "password";

//bit mask for server support
#define EASY_IOT_MASK 1
#define MQTT_MASK 2
#define BOILER_MASK 4
#define BELL_MASK 8
#define SECURITY_MASK 16
#define LIGHTCONTROL_MASK 32
#define RESET_MASK 64
int serverMode = 1;

#define ONE_WIRE_BUS 13  // DS18B20 pin
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);
int tempValid;

//Timing
int minMsgInterval = 10; // in units of 1 second
int forceInterval = 300; // send message after this interval even if temp same 

//AP definitions
#define AP_SSID "ssid"
#define AP_PASSWORD "password"
#define AP_MAX_WAIT 10
String macAddr;

#define AP_AUTHID "12345678"
#define AP_PORT 80

ESP8266WebServer server(AP_PORT);
ESP8266HTTPUpdateServer httpUpdater;
WiFiClient cClient;

//Config remote fetch from web page
#define CONFIG_IP_ADDRESS  "192.168.0.7"
#define CONFIG_PORT        80
//Comment out for no authorisation else uses same authorisation as EIOT server
#define CONFIG_AUTH 1
#define CONFIG_PAGE "espConfig"
#define CONFIG_RETRIES 10

// EasyIoT server definitions
#define EIOT_USERNAME    "admin"
#define EIOT_PASSWORD    "password"
#define EIOT_IP_ADDRESS  "192.168.0.7"
#define EIOT_PORT        80
#define USER_PWD_LEN 40
char unameenc[USER_PWD_LEN];
String eiotNode = "-1";

//Home assistant access
#define mqtt_server "192.168.0.100"
#define mqtt_user "homeassistant"
#define mqtt_password "password"
#define MQTT_RETRIES 5
String temperature_topic = "sensor/studyT";
WiFiClient mClient;
PubSubClient mqttClient(mClient);

//general variables
float oldTemp, newTemp;
float diff = 0.1;

void ICACHE_RAM_ATTR  delaymSec(unsigned long mSec) {
	unsigned long ms = mSec;
	while(ms > 100) {
		delay(100);
		ms -= 100;
		ESP.wdtFeed();
	}
	delay(ms);
	ESP.wdtFeed();
	yield();
}

void ICACHE_RAM_ATTR  delayuSec(unsigned long uSec) {
	unsigned long us = uSec;
	while(us > 100000) {
		delay(100);
		us -= 100000;
		ESP.wdtFeed();
	}
	delayMicroseconds(us);
	ESP.wdtFeed();
	yield();
}

/*
  Set up basic wifi, collect config from flash/server, initiate update server
*/
void setup() {
	Serial.begin(115200);
	char uname[USER_PWD_LEN];
	String str = String(EIOT_USERNAME)+":"+String(EIOT_PASSWORD);  
	str.toCharArray(uname, USER_PWD_LEN); 
	memset(unameenc,0,sizeof(unameenc));
	base64_encode(unameenc, uname, strlen(uname));
	Serial.println("Set up Web update service");
	wifiConnect(0);
	macAddr = WiFi.macAddress();
	macAddr.replace(":","");
	Serial.println(macAddr);
	getConfig();
	mqttClient.setServer(mqtt_server, 1883);

	//Update service
	MDNS.begin(host.c_str());
	httpUpdater.setup(&server, update_path, update_username, update_password);
	server.on("/reloadConfig", reloadConfig);
	server.begin();

	MDNS.addService("http", "tcp", 80);
	Serial.println("Set up complete");
}

/*
  Connect to local wifi with retries
  If check is set then test the connection and re-establish if timed out
*/
int wifiConnect(int check) {
	if(check) {
		if((elapsedTime - wifiCheckTime) * timeInterval > WIFI_CHECK_TIMEOUT) {
			if(WiFi.status() != WL_CONNECTED) {
				Serial.println("Wifi connection timed out. Try to relink");
			} else {
				return 1;
			}
		} else {
			wifiCheckTime = elapsedTime;
			return 0;
		}
	}
	wifiCheckTime = elapsedTime;
#ifdef WM_NAME
	Serial.println("Set up managed Web");
#ifdef WM_STATIC_IP
	wifiManager.setSTAStaticIPConfig(IPAddress(WM_STATIC_IP), IPAddress(WM_STATIC_GATEWAY), IPAddress(255,255,255,0));
#endif
	if(check == 0) {
		wifiManager.setConfigPortalTimeout(180);
		//Revert to STA if wifimanager times out as otherwise APA is left on.
		if(!wifiManager.autoConnect(WM_NAME, WM_PASSWORD)) WiFi.mode(WIFI_STA);
	} else {
		WiFi.begin();
	}
#else
	Serial.println("Set up manual Web");
	int retries = 0;
	Serial.print("Connecting to AP");
	#ifdef AP_IP
		IPAddress addr1(AP_IP);
		IPAddress addr2(AP_DNS);
		IPAddress addr3(AP_GATEWAY);
		IPAddress addr4(AP_SUBNET);
		WiFi.config(addr1, addr2, addr3, addr4);
	#endif
	WiFi.begin(AP_SSID, AP_PASSWORD);
	while (WiFi.status() != WL_CONNECTED && retries < AP_MAX_WAIT) {
		delaymSec(1000);
		Serial.print(".");
		retries++;
	}
	Serial.println("");
	if(retries < AP_MAX_WAIT) {
		Serial.print("WiFi connected ip ");
		Serial.print(WiFi.localIP());
		Serial.printf(":%d mac %s\r\n", AP_PORT, WiFi.macAddress().c_str());
		return 1;
	} else {
		Serial.println("WiFi connection attempt failed"); 
		return 0;
	} 
#endif
}

/*
  Get config from server
*/
void getConfig() {
	int responseOK = 0;
	int retries = CONFIG_RETRIES;
	String line = "";

	while(retries > 0) {
		clientConnect(CONFIG_IP_ADDRESS, CONFIG_PORT);
		Serial.print("Try to GET config data from Server for: ");
		Serial.println(macAddr);

		cClient.print(String("GET /") + CONFIG_PAGE + " HTTP/1.1\r\n" +
			"Host: " + String(CONFIG_IP_ADDRESS) + "\r\n" + 
		#ifdef CONFIG_AUTH
				"Authorization: Basic " + unameenc + " \r\n" + 
		#endif
			"Content-Length: 0\r\n" + 
			"Connection: close\r\n" + 
			"\r\n");
		int config = 100;
		int timeout = 0;
		while (cClient.connected() && timeout < 10){
			if (cClient.available()) {
				timeout = 0;
				line = cClient.readStringUntil('\n');
				if(line.indexOf("HTTP") == 0 && line.indexOf("200 OK") > 0)
					responseOK = 1;
				//Don't bother processing when config complete
				if (config >= 0) {
					line.replace("\r","");
					Serial.println(line);
					//start reading config when mac address found
					if (line == macAddr) {
						config = 0;
					} else {
						if(line.charAt(0) != '#') {
							switch(config) {
								case 0: host = line;break;
								case 1: serverMode = line.toInt();break;
								case 2: eiotNode = line;break;
								case 3: break; //spare
								case 4: minMsgInterval = line.toInt();break;
								case 5:
									forceInterval = line.toInt();
									Serial.println("Config fetched from server OK");
									config = -100;
									break;
							}
							config++;
						}
					}
				}
			} else {
				delaymSec(1000);
				timeout++;
				Serial.println("Wait for response");
			}
		}
		cClient.stop();
		if(responseOK == 1)
			break;
		retries--;
	}
	Serial.println();
	Serial.println("Connection closed");
	Serial.print("host:");Serial.println(host);
	Serial.print("serverMode:");Serial.println(serverMode);
	Serial.print("eiotNode:");Serial.println(eiotNode);
	Serial.print("minMsgInterval:");Serial.println(minMsgInterval);
	Serial.print("forceInterval:");Serial.println(forceInterval);
}

/*
  reload Config
*/
void reloadConfig() {
	if (server.arg("auth") != AP_AUTHID) {
		Serial.println("Unauthorized");
		server.send(401, "text/html", "Unauthorized");
	} else {
		getConfig();
		server.send(200, "text/html", "Config reload");
	}
}



/*
  Establish client connection
*/
void clientConnect(char* host, uint16_t port) {
	int retries = 0;
   
	while(!cClient.connect(host, port)) {
		Serial.print("?");
		retries++;
		if(retries > CONFIG_RETRIES) {
			Serial.print("Client connection failed:" );
			Serial.println(host);
			wifiConnect(0); 
			retries = 0;
		} else {
			delaymSec(5000);
		}
	}
}

/*
  Establish MQTT connection for publishing to Home assistant
*/
void mqttConnect() {
	// Loop until we're reconnected
	int retries = 0;
	while (!mqttClient.connected()) {
		Serial.print("Attempting MQTT connection...");
		// Attempt to connect
		// If you do not want to use a username and password, change next line to
		// if (mqttClient.connect("ESP8266mqttClient")) {
		if (mqttClient.connect("ESP8266Client", mqtt_user, mqtt_password)) {
			Serial.println("connected");
		} else {
			Serial.print("failed, rc=");
			Serial.print(mqttClient.state());
			delay(5000);
			retries++;
			if(retries > MQTT_RETRIES) {
				wifiConnect(1);
				retries = 0;
			}
		}
	}
}

/*
  Check if value changed enough to report
*/
bool checkBound(float newValue, float prevValue, float maxDiff) {
	return !isnan(newValue) &&
         (newValue < prevValue - maxDiff || newValue > prevValue + maxDiff);
}

/*
 Send report by MQTT
*/
void mqttReport(String topic, float value) {
	int retries = CONFIG_RETRIES;
	if (!mqttClient.connected() && retries > 0) {
		mqttConnect();
		retries--;
	}
	if(mqttClient.connected()) {
		mqttClient.loop();
		mqttClient.publish(topic.c_str(), String(value).c_str(), true);
	}
}

/*
 Send report to easyIOTReport
 if digital = 1, send digital else analog
*/
void easyIOTReport(String node, float value, int digital) {
	int retries = CONFIG_RETRIES;
	int responseOK = 0;
	String url = "/Api/EasyIoT/Control/Module/Virtual/" + node;
	
	// generate EasIoT server node URL
	if(digital == 1) {
		if(value > 0)
			url += "/ControlOn";
		else
			url += "/ControlOff";
	} else
		url += "/ControlLevel/" + String(value);

	Serial.print("POST data to URL: ");
	Serial.println(url);
	while(retries > 0) {
		clientConnect(EIOT_IP_ADDRESS, EIOT_PORT);
		cClient.print(String("POST ") + url + " HTTP/1.1\r\n" +
				"Host: " + String(EIOT_IP_ADDRESS) + "\r\n" + 
				"Connection: close\r\n" + 
				"Authorization: Basic " + unameenc + " \r\n" + 
				"Content-Length: 0\r\n" + 
				"\r\n");

		delaymSec(100);
		while(cClient.available()){
			String line = cClient.readStringUntil('\r');
			if(line)
			Serial.print(line);
			if(line.indexOf("HTTP") == 0 && line.indexOf("200 OK") > 0)
				responseOK = 1;
		}
		cClient.stop();
		if(responseOK)
			break;
		else
			Serial.println("Retrying EIOT report");
		retries--;
	}
	Serial.println();
	Serial.println("Connection closed");
}

/*
 Check temperature and report if necessary
*/
void checkTemp() {
	if((serverMode & (EASY_IOT_MASK | MQTT_MASK)) && (elapsedTime - tempCheckTime) * timeInterval / 1000 > minMsgInterval) {
		DS18B20.requestTemperatures(); 
		newTemp = DS18B20.getTempCByIndex(0);
		if(newTemp != 85.0 && newTemp != (-127.0)) {
			tempCheckTime = elapsedTime;
			if(checkBound(newTemp, oldTemp, diff) || ((elapsedTime - tempReportTime) * timeInterval / 1000 > forceInterval)) {
				tempReportTime = elapsedTime;
				oldTemp = newTemp;
				Serial.print("New temperature:");
				Serial.println(String(newTemp).c_str());
				if(serverMode & EASY_IOT_MASK) easyIOTReport(eiotNode, newTemp, 0);
				if(serverMode & MQTT_MASK) mqttReport(temperature_topic, newTemp);
			}
		} else {
			Serial.println("Invalid temp reading");
		}
	}
}


/*
  Main loop to read temperature and publish as required
*/
void loop() {
	checkTemp();
	server.handleClient();
	wifiConnect(1);
	delaymSec(timeInterval);
	elapsedTime++;
}
