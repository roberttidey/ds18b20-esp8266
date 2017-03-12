# ds18b20-esp8266
Temperature sensor module for esp8266 reporting to EasyIOT and Home Assistant

Features
Configurable to report to EasyIOT and / or Home Assistant servers
Configurable update interval time and force updates even temperature unchanged
Configuration fetched from a web server file keyed on Mac Address of esp-8266 to allow for multiple units
Web update of software
Resileint allowing retries on network and server connections
Config saved and restored to / from flash.

Basic network conections and server authorisation passwords are set in the code and must be separately set up.

