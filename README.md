# ESP-NOW gateway for ESP8266/ESP32

Gateway for data exchange between ESP-NOW devices and MQTT broker via WiFi/LAN.

## Features

1. Creates an access point named "ESP-NOW gateway XXXXXXXXXXXX" with password "12345678" (IP 192.168.4.1).
2. Possibility a device search through the Windows Network Environment via SSDP (at ESP_NOW_WIFI mode).
3. Periodically transmission of system information to the MQTT broker (every 60 seconds), availability status to the ESP-NOW network and to the MQTT broker (every 10 seconds) and current date and time to the ESP-NOW network (every 10 seconds).
4. Automatically adds gateway configuration to Home Assistan via MQTT discovery as a binary_sensor.
5. Automatically adds supported ESP-NOW devices configurations to Home Assistan via MQTT discovery.
6. Possibility firmware update over OTA (at ESP_NOW_LAN mode via access point only).
7. Web interface for settings (at ESP_NOW_LAN mode via access point only).
8. 3 operating modes:

```text
ESP_NOW       ESP-NOW node only. Default mode after flashing.
ESP_NOW_WIFI  Gateway between ESP-NOW devices and MQTT broker via WiFi.
ESP_NOW_LAN   Gateway between ESP-NOW devices and MQTT broker via Ethernet. Preferred mode.
 ```

## Notes

1. ESP-NOW mesh network based on the library [ZHNetwork](https://github.com/aZholtikov/ZHNetwork).
2. Regardless of the status of connections to WiFi or MQTT the device perform ESP-NOW node function.
3. For restart the device (without using the Web interface and only if MQTT connection established) send an "restart" command to the device's root topic (example - "homeassistant/espnow_gateway/70039F44BEF7").
4. W5500 connection:

```text
ESP8266 (GPIO05 - CS, GPIO14 - SCK, GPIO12 - MISO, GPIO13 - MOSI).
ESP32   (GPIO05 - CS, GPIO18 - SCK, GPIO19 - MISO, GPIO23 - MOSI).
```

## Attention

1. ESP-NOW network name must be set same of all another ESP-NOW devices in network.
2. If encryption is used, the key must be set same of all another ESP-NOW devices in network.
3. Upload the "data" folder (with web interface) into the filesystem before flashing.
4. At ESP_NOW_WIFI mode WiFi router must be set on channel 1.

## Tested on

1. NodeMCU 1.0 (ESP-12E Module). ESP_NOW_WIFI mode. Unstable work.
2. AZ-Delivery ESP-32 Dev Kit C V4. ESP_NOW_WIFI mode. Stable work.
3. NodeMCU 1.0 (ESP-12E Module) + W5500. ESP_NOW_LAN mode. Stable work.
4. AZ-Delivery ESP-32 Dev Kit C V4 + W5500. ESP_NOW_LAN mode. Stable work.

## Supported devices

1. [RF Gateway](https://github.com/aZholtikov/RF-Gateway) (coming soon)
2. [ESP-NOW Switch](https://github.com/aZholtikov/ESP-NOW-Switch)
3. [ESP-NOW Light/Led Strip](https://github.com/aZholtikov/ESP-NOW-Light-Led-Strip)
4. [ESP-NOW Window/Door Sensor](https://github.com/aZholtikov/ESP-NOW-Window-Door-Sensor)
5. [ESP-NOW Water Leakage Sensor](https://github.com/aZholtikov/ESP-NOW-Water-Leakage-Sensor)

## To Do

- [X] Automatically add ESP-NOW devices configurations to Home Assistan via MQTT discovery.
- [X] LAN connection support.
- [ ] nRF24 device support (in current time uses "RF Gateway").
- [ ] BLE device support (for ESP32).
- [ ] LoRa device support.

Any feedback via [e-mail](mailto:github@zh.com.ru) would be appreciated. Or... [Buy me a coffee](https://paypal.me/aZholtikov).
