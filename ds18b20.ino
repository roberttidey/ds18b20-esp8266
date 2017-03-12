/*
 R. J. Tidey 2017/02/22
 Supports temperature sensors to Easy IoT and Home assistant servers.
 Both can be used together if required.
 Web software update service included
 
 Code based on work by Igor Jarc
 Code based on https://github.com/DennisSc/easyIoT-ESPduino/blob/master/sketches/ds18b20.ino
 
 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2 as published by the Free Software Foundation.
 */
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "Base64.h"

//For update service
String host = "esp8266-lounge";
const char* update_path = "/firmware";
const char* update_username = "admin";
const char* update_password = "12345678";

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

//bit mask for server support
#define EASY_IOT_MASK 1
#define HOME_ASSISTANT_MASK 2
int serverMode = 1;

#define ONE_WIRE_BUS 13  // DS18B20 pin
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);

int minMsgInterval = 10; // in units of 1 second
int forceInterval = 300; // send message after this interval even if temp same 

//AP definitions
#define AP_SSID "ssid"
#define AP_PASSWORD "password"
#define AP_MAX_WAIT 10
String macAddr;

//Config flash and remote fetch from web page
#define CONFIG_IP_ADDRESS  "192.168.0.100"
#define CONFIG_PORT        80
//Comment out for no authorisation else uses same as EIOT server
#define CONFIG_AUTH 1
#define CONFIG_PAGE "espConfig"
#define CONFIG_RETRIES 10
//Flash locations, allow 31 chars for strings
#define CONFIG_VALID 0
#define CONFIG_SERVERMODE 1
#define CONFIG_MSGINTERVAL 2
#define CONFIG_FORCE_INTERVAL 3
#define CONFIG_HOST 32
#define CONFIG_EIOT 64
#define CONFIG_MQTT 96
WiFiClient cClient;

// EasyIoT server definitions
#define EIOT_USERNAME    "admin"
#define EIOT_PASSWORD    "password"
#define EIOT_IP_ADDRESS  "192.168.0.100"
#define EIOT_PORT        80
#define EIOT_RETRIES 10
#define USER_PWD_LEN 40
char unameenc[USER_PWD_LEN];
String eiotNode = "N2S0";
WiFiClient eClient;

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
int forceCounter = 0;
long lastMsg = 0;
float diff = 0.1;

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
	
	wifiConnect();
	macAddr = WiFi.macAddress();
	macAddr.replace(":","");
	Serial.println(macAddr);
	getConfig();
	mqttClient.setServer(mqtt_server, 1883);

	//Update service
	MDNS.begin(host.c_str());
	httpUpdater.setup(&httpServer, update_path, update_username, update_password);
	httpServer.begin();
	MDNS.addService("http", "tcp", 80);
}

/*
  Connect to local wifi with retries
*/
int wifiConnect()
{
	int retries = 0;

	Serial.print("Connecting to AP");
	WiFi.begin(AP_SSID, AP_PASSWORD);
	while (WiFi.status() != WL_CONNECTED && retries < AP_MAX_WAIT) {
		delay(1000);
		Serial.print(".");
		retries++;
	}
	Serial.println("");
	if(retries < AP_MAX_WAIT) {
		Serial.println("WiFi connected");
		return 1;
	} else {
		Serial.println("WiFi connection attempt failed"); 
		return 0;
	} 
}

/*
  Get config from Flash and try to update from server
*/
void getConfig() {
	getFlashConfig();
	//Try to update from remote server
	if (!cClient.connected()) {
		cConnect();
	}
	Serial.print("GET config data from Server for: ");
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
	String line = "";
	while (cClient.connected() && timeout < 10){
		if (cClient.available()) {
			timeout = 0;
			line = cClient.readStringUntil('\n');
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
							case 3: temperature_topic = line;break;
							case 4: minMsgInterval = line.toInt();break;
							case 5:
								forceInterval = line.toInt();
								Serial.println("Config fetched from server OK");
								//Flag config complete
								saveFlashConfig();
								config = -100;
                break;
						}
						config++;
					}
				}
			}
		} else {
			delay(1000);
			timeout++;
			Serial.println("Wait for response");
		}
	}

	Serial.println();
	Serial.println("Connection closed");
	cClient.stop();
	Serial.print("host:");Serial.println(host);
	Serial.print("serverMode:");Serial.println(serverMode);
	Serial.print("eiotNode:");Serial.println(eiotNode);
	Serial.print("temperature_topic:");Serial.println(temperature_topic);
	Serial.print("minMsgInterval:");Serial.println(minMsgInterval);
	Serial.print("forceInterval:");Serial.println(forceInterval);
}

/*
  Get config string from Flash
*/
String getFlashString(int addr) {
	String value;
	char c;
	int i = 0;
	while(i<32) {
		c = char(EEPROM.read(addr + i));
		if (c == 0) break;
		value = value + c;
		i++;
	}
	return value;
}

/*
  Save config string to Flash
*/
void saveFlashString(int addr, String value) {
	int i;
    for (i = 0; i < value.length(); i++)
    {
      EEPROM.write(addr + i, value[i]);
    }
	EEPROM.write(addr+value.length(), 0);
}

/*
  Get config from Flash
*/
void getFlashConfig() {
  int valid;
	EEPROM.begin(512);
  valid = EEPROM.read(CONFIG_VALID);
	if (valid == 85) {
		serverMode = EEPROM.read(CONFIG_SERVERMODE);
		minMsgInterval = EEPROM.read(CONFIG_MSGINTERVAL);
		forceInterval = EEPROM.read(CONFIG_FORCE_INTERVAL);
		host = getFlashString(CONFIG_HOST);
		eiotNode = getFlashString(CONFIG_EIOT);
		temperature_topic = getFlashString(CONFIG_MQTT);
		Serial.println("Read config from Flash OK.");
	} else {
    Serial.print("Flash config not valid:");Serial.println(valid);
	}
}

/*
  Save config to Flash
*/
void saveFlashConfig() {
	EEPROM.write(CONFIG_VALID, 85);
	EEPROM.write(CONFIG_SERVERMODE, serverMode);
	EEPROM.write(CONFIG_MSGINTERVAL, minMsgInterval);
	EEPROM.write(CONFIG_FORCE_INTERVAL, forceInterval);
	saveFlashString(CONFIG_HOST, host);
	saveFlashString(CONFIG_EIOT, eiotNode);
	saveFlashString(CONFIG_MQTT, temperature_topic);
	EEPROM.commit();
	Serial.println("Wrote config to Flash OK.");
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
				wifiConnect();
				retries = 0;
			}
		}
	}
}

/*
  Establish web connection for publishing to EIOT server
*/
void eConnect() {
	int retries = 0;
   
	while(!eClient.connect(EIOT_IP_ADDRESS, EIOT_PORT)) {
		Serial.print ("?");
		retries++;
		if(retries > EIOT_RETRIES) {
			Serial.println("EIOT connection failed");
			wifiConnect(); 
			retries = 0;
		} else {
			delay(5000);
		}
	}
}

/*
  Establish web connection for collecting config
*/void cConnect() {
	int retries = 0;
   
	while(!cClient.connect(CONFIG_IP_ADDRESS, CONFIG_PORT)) {
		Serial.print ("?");
		retries++;
		if(retries > CONFIG_RETRIES) {
			Serial.println("EIOT connection failed");
			wifiConnect(); 
			retries = 0;
		} else {
			delay(5000);
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
  Main loop to read temperature and publish as required
*/
void loop() {
	do {
		DS18B20.requestTemperatures(); 
		newTemp = DS18B20.getTempCByIndex(0);
		Serial.print("Temperature: ");
		Serial.println(newTemp);
	} while (newTemp == 85.0 || newTemp == (-127.0));

	if (checkBound(newTemp, oldTemp, diff) || forceCounter >= forceInterval) {
		forceCounter = 0;
		oldTemp = newTemp;
		Serial.print("New temperature:");
		Serial.println(String(newTemp).c_str());
		if(serverMode & HOME_ASSISTANT_MASK) {
			if (!mqttClient.connected()) {
				mqttConnect();
			}
			mqttClient.loop();
			mqttClient.publish(temperature_topic.c_str(), String(newTemp).c_str(), true);
		}
		if(serverMode & EASY_IOT_MASK) {
			if (!eClient.connected()) {
				eConnect();
			}
			String url = "";
			url += "/Api/EasyIoT/Control/Module/Virtual/"+ eiotNode + "/ControlLevel/"+String(newTemp); // generate EasIoT server node URL

			Serial.print("POST data to URL: ");
			Serial.println(url);
	  
			eClient.print(String("POST ") + url + " HTTP/1.1\r\n" +
				   "Host: " + String(EIOT_IP_ADDRESS) + "\r\n" + 
				   "Connection: close\r\n" + 
				   "Authorization: Basic " + unameenc + " \r\n" + 
				   "Content-Length: 0\r\n" + 
				   "\r\n");

			delay(100);
			while(eClient.available()){
				String line = eClient.readStringUntil('\r');
				Serial.print(line);
			}

			Serial.println();
			Serial.println("Connection closed");
		}
	}
	int i;
	for(i = minMsgInterval; i > 0;i--){
		httpServer.handleClient();
		forceCounter++;
		delay(1000);
	}
}
