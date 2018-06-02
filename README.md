# ds18b20-esp8266
Temperature sensor module for esp8266 reporting to EasyIOT and Home Assistant

Features
Report to EasyIOT Home Assistant servers
Configurable update interval time and force updates even temperature unchanged
Configuration fetched from a web server file keyed on Mac Address of esp-8266 to allow for multiple units
Web update of software
Resilient allowing retries on network and server connections
Config saved and restored to / from flash.
Web Manager for initial wifi set up via AP mode

On first use Wifi Manager will start an access point. Connect to this from a wifi client then browse to 192.168.4.1 to set up wifi

Needs OneWire, DallasTemperature libraries to be added

