# ESP-NOW gateway for ESP8266/ESP32

Gateway for data exchange between ESP-NOW devices and MQTT broker via WiFi.

## Features

1. Creates an access point named "ESP-NOW Gateway XXXXXXXXXXXX" with password "12345678" (IP 192.168.4.1).
2. Possibility a device search through the Windows Network Environment via SSDP.
3. Periodically transmission of system information to the MQTT broker (every 60 seconds) and availability status to the ESP-NOW network and to the MQTT broker (every 10 seconds).
4. Automatically adds gateway configuration to Home Assistan via MQTT discovery as a binary_sensor.
5. Automatically adds supported ESP-NOW devices configurations to Home Assistan via MQTT discovery.
6. Possibility firmware update over OTA.
7. Web interface for settings.
  
## Notes

1. ESP-NOW mesh network based on the library [ZHNetwork](https://github.com/aZholtikov/ZHNetwork).
2. Regardless of the status of connections to WiFi or MQTT the device perform ESP-NOW node function.
3. For restart the device (without using the Web interface and only if MQTT connection established) send an "restart" command to the device's root topic (example - "homeassistant/espnow_gateway/70039F44BEF7").

## Attention

1. ESP-NOW network name must be set same of all another ESP-NOW devices in network.
2. Upload the "data" folder (with web interface) into the filesystem before flashing.
3. WiFi router must be set on channel 1.

## Tested on

1. NodeMCU 1.0 (ESP-12E Module). ESP-NOW + WiFi mode. Unstable work.
2. AZ-Delivery ESP-32 Dev Kit C V4. ESP-NOW + WiFi mode. Stable work.

## Supported devices

1. [RF Gateway](https://github.com/aZholtikov/RF-Gateway) (coming soon)
2. [ESP-NOW Switch](https://github.com/aZholtikov/ESP-NOW-Switch)
3. [ESP-NOW Light/Led Strip](https://github.com/aZholtikov/ESP-NOW-Light-Led-Strip)
4. [ESP-NOW Window/Door Sensor](https://github.com/aZholtikov/ESP-NOW-Window-Door-Sensor)
5. [ESP-NOW Water Leakage Sensor](https://github.com/aZholtikov/ESP-NOW-Water-Leakage-Sensor) (coming soon)

## To Do

- [X] Automatically add ESP-NOW devices configurations to Home Assistan via MQTT discovery.
- [ ] LAN connection support.
- [ ] nRF24 device support (in current time uses "RF Gateway").
- [ ] BLE device support (for ESP32).
- [ ] LoRa device support.
