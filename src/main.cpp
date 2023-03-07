#include "ArduinoJson.h"
#include "ArduinoOTA.h"
#include "ESPAsyncWebServer.h" // https://github.com/aZholtikov/Async-Web-Server
#include "Ethernet.h"          // https://github.com/arduino-libraries/Ethernet
#include "PubSubClient.h"
#include "LittleFS.h"
#include "EEPROM.h"
#include "Ticker.h"
#include "NTPClient.h"
#include "ZHNetwork.h"
#include "ZHConfig.h"
#if defined(ESP8266)
#include "ESP8266SSDP.h"
#endif
#if defined(ESP32)
#include "ESP32SSDP.h"
#endif

void onEspnowMessage(const char *data, const uint8_t *sender);

void onMqttMessage(char *topic, byte *payload, unsigned int length);

void sendKeepAliveMessage(void);
void sendAttributesMessage(void);
void sendConfigMessage(void);

String getValue(String data, char separator, uint8_t index);

void loadConfig(void);
void saveConfig(void);

String xmlNode(String tags, String data);
void setupWebServer(void);

void checkMqttAvailability(void);

void mqttPublish(const char *topic, const char *payload, bool retained);

typedef enum : uint8_t
{
    ESP_NOW,
    ESP_NOW_WIFI,
    ESP_NOW_LAN
} work_mode_t;

struct deviceConfig
{
#if defined(ESP8266)
    String deviceName = "ESP-NOW gateway " + String(ESP.getChipId(), HEX);
#endif
#if defined(ESP32)
    String deviceName = "ESP-NOW gateway " + String(ESP.getEfuseMac(), HEX);
#endif
    String espnowNetName{"DEFAULT"};
    uint8_t workMode{ESP_NOW};
    String ssid{"SSID"};
    String password{"PASSWORD"};
    String mqttHostName{"MQTT"};
    uint16_t mqttHostPort{1883};
    String mqttUserLogin{""};
    String mqttUserPassword{""};
    String topicPrefix{"homeassistant"};
    String ntpHostName{"NTP"};
    uint16_t gmtOffset{10800};
} config;

const String firmware{"1.6"};

const char *mqttUserID{"ESP"};

uint8_t w5500Mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED}; // Change it if necessary.

ZHNetwork myNet;
AsyncWebServer webServer(80);

EthernetClient ethClient;
WiFiClient wifiClient;

PubSubClient mqttEthClient(ethClient);
PubSubClient mqttWifiClient(wifiClient);

WiFiUDP udpWiFiClient;
EthernetUDP udpEthClient;

NTPClient ntpWiFiClient(udpWiFiClient, config.ntpHostName.c_str(), config.gmtOffset);
NTPClient ntpEthClient(udpEthClient, config.ntpHostName.c_str(), config.gmtOffset);

Ticker mqttAvailabilityCheckTimer;
bool mqttAvailabilityCheckTimerSemaphore{true};
bool isMqttAvailable{false};
void mqttAvailabilityCheckTimerCallback(void);

Ticker keepAliveMessageTimer;
bool keepAliveMessageTimerSemaphore{true};
void keepAliveMessageTimerCallback(void);

Ticker attributesMessageTimer;
bool attributesMessageTimerSemaphore{true};
void attributesMessageTimerCallback(void);

void setup()
{
#if defined(ESP8266)
    LittleFS.begin();
#endif
#if defined(ESP32)
    LittleFS.begin(true);
#endif

    loadConfig();

    if (config.workMode == ESP_NOW_LAN)
    {
        Ethernet.init(5);
        Ethernet.begin(w5500Mac);
    }

    if (config.workMode == ESP_NOW_WIFI)
    {
#if defined(ESP8266)
        WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif
#if defined(ESP32)
        WiFi.setSleep(WIFI_PS_NONE);
#endif
        WiFi.persistent(false);
        WiFi.setAutoConnect(false);
        WiFi.setAutoReconnect(true);
    }

    myNet.begin(config.espnowNetName.c_str(), true);

    if (config.workMode)
    {
        // myNet.setCryptKey("VERY_LONG_CRYPT_KEY"); // If encryption is used, the key must be set same of all another ESP-NOW devices in network.
        myNet.setOnBroadcastReceivingCallback(onEspnowMessage);
        myNet.setOnUnicastReceivingCallback(onEspnowMessage);
    }

#if defined(ESP8266)
    WiFi.softAP(("ESP-NOW gateway " + String(ESP.getChipId(), HEX)).c_str(), "12345678");
#endif
#if defined(ESP32)
    WiFi.softAP(("ESP-NOW gateway " + String(ESP.getEfuseMac(), HEX)).c_str(), "12345678");
#endif

    if (config.workMode == ESP_NOW_WIFI)
    {
        uint8_t scan = WiFi.scanNetworks(false, false);
        String name;
        int32_t rssi;
        uint8_t encryption;
        uint8_t *bssid;
        int32_t channel;
        bool hidden;
        for (int8_t i = 0; i < scan; i++)
        {
#if defined(ESP8266)
            WiFi.getNetworkInfo(i, name, encryption, rssi, bssid, channel, hidden);
#endif
#if defined(ESP32)
            WiFi.getNetworkInfo(i, name, encryption, rssi, bssid, channel);
#endif
            if (name == config.ssid)
                WiFi.begin(config.ssid.c_str(), config.password.c_str());
        }
    }

    if (config.workMode == ESP_NOW_WIFI)
    {
        ntpWiFiClient.begin();
        mqttWifiClient.setBufferSize(2048);
        mqttWifiClient.setServer(config.mqttHostName.c_str(), config.mqttHostPort);
        mqttWifiClient.setCallback(onMqttMessage);
    }

    if (config.workMode == ESP_NOW_LAN)
    {
        ntpEthClient.begin();
        mqttEthClient.setBufferSize(2048);
        mqttEthClient.setServer(config.mqttHostName.c_str(), config.mqttHostPort);
        mqttEthClient.setCallback(onMqttMessage);
    }

    setupWebServer();

    ArduinoOTA.begin();

    keepAliveMessageTimer.attach(10, keepAliveMessageTimerCallback);
    mqttAvailabilityCheckTimer.attach(5, mqttAvailabilityCheckTimerCallback);
    attributesMessageTimer.attach(60, attributesMessageTimerCallback);
}

void loop()
{
    if (mqttAvailabilityCheckTimerSemaphore)
        checkMqttAvailability();
    if (keepAliveMessageTimerSemaphore)
        sendKeepAliveMessage();
    if (attributesMessageTimerSemaphore)
        sendAttributesMessage();
    if (config.workMode == ESP_NOW_WIFI)
        mqttWifiClient.loop();
    if (config.workMode == ESP_NOW_LAN)
        mqttEthClient.loop();
    myNet.maintenance();
    ArduinoOTA.handle();
}

void onEspnowMessage(const char *data, const uint8_t *sender)
{
    if (!isMqttAvailable)
        return;
    esp_now_payload_data_t incomingData;
    memcpy(&incomingData, data, sizeof(esp_now_payload_data_t));
    if (incomingData.payloadsType == ENPT_ATTRIBUTES)
        mqttPublish((config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/" + getValueName(incomingData.payloadsType)).c_str(), incomingData.message, true);
    if (incomingData.payloadsType == ENPT_KEEP_ALIVE)
        mqttPublish((config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/" + getValueName(incomingData.payloadsType)).c_str(), "online", true);
    if (incomingData.payloadsType == ENPT_STATE)
        mqttPublish((config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/" + getValueName(incomingData.payloadsType)).c_str(), incomingData.message, true);
    if (incomingData.payloadsType == ENPT_CONFIG)
    {
        if (incomingData.deviceType == ENDT_SWITCH)
        {
            esp_now_payload_data_t configData;
            memcpy(&configData.message, &incomingData.message, sizeof(esp_now_payload_data_t::message));
            DynamicJsonDocument json(sizeof(esp_now_payload_data_t::message));
            deserializeJson(json, configData.message);
            uint8_t unit = json[MCMT_DEVICE_UNIT].as<uint8_t>();
            DynamicJsonDocument jsonConfig(2048); // Same as PubSubClient buffer size.
            jsonConfig["platform"] = "mqtt";
            jsonConfig["name"] = json[MCMT_DEVICE_NAME];
            jsonConfig["unique_id"] = myNet.macToString(sender) + "-" + unit;
            jsonConfig["device_class"] = getValueName(json[MCMT_DEVICE_CLASS].as<ha_switch_device_class_t>());
            jsonConfig["state_topic"] = config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/state";
            jsonConfig["value_template"] = "{{ value_json." + json[MCMT_VALUE_TEMPLATE].as<String>() + " }}";
            jsonConfig["command_topic"] = config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/set";
            jsonConfig["json_attributes_topic"] = config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/attributes";
            jsonConfig["availability_topic"] = config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/status";
            if (json[MCMT_PAYLOAD_ON])
                jsonConfig["payload_on"] = json[MCMT_PAYLOAD_ON];
            if (json[MCMT_PAYLOAD_OFF])
                jsonConfig["payload_off"] = json[MCMT_PAYLOAD_OFF];
            jsonConfig["optimistic"] = "false";
            jsonConfig["retain"] = "true";
            char buffer[2048]{0};
            serializeJsonPretty(jsonConfig, buffer);
            mqttPublish((config.topicPrefix + "/" + getValueName(json[MCMT_COMPONENT_TYPE].as<ha_component_type_t>()) + "/" + myNet.macToString(sender) + "-" + unit + "/config").c_str(), buffer, true);
        }
        if (incomingData.deviceType == ENDT_LED)
        {
            esp_now_payload_data_t configData;
            memcpy(&configData.message, &incomingData.message, sizeof(esp_now_payload_data_t::message));
            DynamicJsonDocument json(sizeof(esp_now_payload_data_t::message));
            deserializeJson(json, configData.message);
            uint8_t unit = json[MCMT_DEVICE_UNIT].as<uint8_t>();
            esp_now_led_type_t ledClass = json[MCMT_DEVICE_CLASS];
            DynamicJsonDocument jsonConfig(2048); // Same as PubSubClient buffer size.
            jsonConfig["platform"] = "mqtt";
            jsonConfig["name"] = json[MCMT_DEVICE_NAME];
            jsonConfig["unique_id"] = myNet.macToString(sender) + "-" + unit;
            jsonConfig["state_topic"] = config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/state";
            jsonConfig["state_value_template"] = "{{ value_json.state }}";
            jsonConfig["command_topic"] = config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/set";
            jsonConfig["brightness_state_topic"] = config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/state";
            jsonConfig["brightness_value_template"] = "{{ value_json.brightness }}";
            jsonConfig["brightness_command_topic"] = config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/brightness";
            if (ledClass == ENLT_RGB || ledClass == ENLT_RGBW || ledClass == ENLT_RGBWW)
            {
                jsonConfig["rgb_state_topic"] = config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/state";
                jsonConfig["rgb_value_template"] = "{{ value_json.rgb | join(',') }}";
                jsonConfig["rgb_command_topic"] = config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/rgb";
            }
            if (ledClass == ENLT_WW || ledClass == ENLT_RGBWW)
            {
                jsonConfig["color_temp_state_topic"] = config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/state";
                jsonConfig["color_temp_value_template"] = "{{ value_json.temperature }}";
                jsonConfig["color_temp_command_topic"] = config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/temperature";
            }
            jsonConfig["json_attributes_topic"] = config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/attributes";
            jsonConfig["availability_topic"] = config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/status";
            if (json[MCMT_PAYLOAD_ON])
                jsonConfig["payload_on"] = json[MCMT_PAYLOAD_ON];
            if (json[MCMT_PAYLOAD_OFF])
                jsonConfig["payload_off"] = json[MCMT_PAYLOAD_OFF];
            jsonConfig["optimistic"] = "false";
            jsonConfig["retain"] = "true";
            char buffer[2048]{0};
            serializeJsonPretty(jsonConfig, buffer);
            mqttPublish((config.topicPrefix + "/" + getValueName(json[MCMT_COMPONENT_TYPE].as<ha_component_type_t>()) + "/" + myNet.macToString(sender) + "-" + unit + "/config").c_str(), buffer, true);
        }
        if (incomingData.deviceType == ENDT_SENSOR)
        {
            esp_now_payload_data_t configData;
            memcpy(&configData.message, &incomingData.message, sizeof(esp_now_payload_data_t::message));
            DynamicJsonDocument json(sizeof(esp_now_payload_data_t::message));
            deserializeJson(json, configData.message);
            uint8_t unit = json[MCMT_DEVICE_UNIT].as<uint8_t>();
            ha_component_type_t type = json[MCMT_COMPONENT_TYPE].as<ha_component_type_t>();
            DynamicJsonDocument jsonConfig(2048); // Same as PubSubClient buffer size.
            jsonConfig["platform"] = "mqtt";
            jsonConfig["name"] = json[MCMT_DEVICE_NAME];
            jsonConfig["unique_id"] = myNet.macToString(sender) + "-" + unit;
            jsonConfig["state_topic"] = config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/state";
            jsonConfig["value_template"] = "{{ value_json." + json[MCMT_VALUE_TEMPLATE].as<String>() + " }}";
            jsonConfig["json_attributes_topic"] = config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/attributes";
            jsonConfig["force_update"] = "true";
            jsonConfig["retain"] = "true";
            if (type == HACT_SENSOR)
            {
                jsonConfig["device_class"] = getValueName(json[MCMT_DEVICE_CLASS].as<ha_sensor_device_class_t>());
                jsonConfig["unit_of_measurement"] = json[MCMT_UNIT_OF_MEASUREMENT];
            }
            if (type == HACT_BINARY_SENSOR)
                jsonConfig["device_class"] = getValueName(json[MCMT_DEVICE_CLASS].as<ha_binary_sensor_device_class_t>());
            if (json[MCMT_EXPIRE_AFTER])
                jsonConfig["expire_after"] = json[MCMT_EXPIRE_AFTER];
            if (json[MCMT_OFF_DELAY])
                jsonConfig["off_delay"] = json[MCMT_OFF_DELAY];
            if (json[MCMT_PAYLOAD_ON])
                jsonConfig["payload_on"] = json[MCMT_PAYLOAD_ON];
            if (json[MCMT_PAYLOAD_OFF])
                jsonConfig["payload_off"] = json[MCMT_PAYLOAD_OFF];
            char buffer[2048]{0};
            serializeJsonPretty(jsonConfig, buffer);
            mqttPublish((config.topicPrefix + "/" + getValueName(type) + "/" + myNet.macToString(sender) + "-" + unit + "/config").c_str(), buffer, true);
        }
        if (incomingData.deviceType == ENDT_RF_SENSOR)
        {
            esp_now_payload_data_t configData;
            memcpy(&configData.message, &incomingData.message, sizeof(esp_now_payload_data_t::message));
            DynamicJsonDocument json(sizeof(esp_now_payload_data_t::message));
            deserializeJson(json, configData.message);
            uint8_t unit = json[MCMT_DEVICE_UNIT].as<uint8_t>();
            ha_component_type_t haComponentType = json[MCMT_COMPONENT_TYPE].as<ha_component_type_t>();
            rf_sensor_type_t rfSensorType = json[MCMT_RF_SENSOR_TYPE].as<rf_sensor_type_t>();
            uint16_t rfSensorId = json[MCMT_RF_SENSOR_ID].as<uint16_t>();
            String valueTemplate = json[MCMT_VALUE_TEMPLATE].as<String>();
            DynamicJsonDocument jsonConfig(2048); // Same as PubSubClient buffer size.
            jsonConfig["platform"] = "mqtt";
            jsonConfig["name"] = getValueName(rfSensorType) + " " + rfSensorId + " " + valueTemplate;
            jsonConfig["unique_id"] = String(rfSensorId) + "-" + unit;
            jsonConfig["state_topic"] = config.topicPrefix + "/rf_sensor/" + getValueName(rfSensorType) + "/" + rfSensorId + "/state";
            jsonConfig["value_template"] = "{{ value_json." + valueTemplate + " }}";
            jsonConfig["force_update"] = "true";
            jsonConfig["retain"] = "true";
            if (haComponentType == HACT_SENSOR)
            {
                jsonConfig["device_class"] = getValueName(json[MCMT_DEVICE_CLASS].as<ha_sensor_device_class_t>());
                jsonConfig["unit_of_measurement"] = json[MCMT_UNIT_OF_MEASUREMENT];
            }
            if (haComponentType == HACT_BINARY_SENSOR)
                jsonConfig["device_class"] = getValueName(json[MCMT_DEVICE_CLASS].as<ha_binary_sensor_device_class_t>());
            if (json[MCMT_EXPIRE_AFTER])
                jsonConfig["expire_after"] = json[MCMT_EXPIRE_AFTER];
            if (json[MCMT_OFF_DELAY])
                jsonConfig["off_delay"] = json[MCMT_OFF_DELAY];
            if (json[MCMT_PAYLOAD_ON])
                jsonConfig["payload_on"] = json[MCMT_PAYLOAD_ON];
            if (json[MCMT_PAYLOAD_OFF])
                jsonConfig["payload_off"] = json[MCMT_PAYLOAD_OFF];
            char buffer[2048]{0};
            serializeJsonPretty(jsonConfig, buffer);
            mqttPublish((config.topicPrefix + "/" + getValueName(haComponentType) + "/" + rfSensorId + "-" + unit + "/config").c_str(), buffer, true);
        }
        if (incomingData.deviceType == ENDT_RF_GATEWAY)
        {
            esp_now_payload_data_t configData;
            memcpy(&configData.message, &incomingData.message, sizeof(esp_now_payload_data_t::message));
            DynamicJsonDocument json(sizeof(esp_now_payload_data_t::message));
            deserializeJson(json, configData.message);
            uint8_t unit = json[MCMT_DEVICE_UNIT].as<uint8_t>();
            DynamicJsonDocument jsonConfig(2048); // Same as PubSubClient buffer size.
            jsonConfig["platform"] = "mqtt";
            jsonConfig["name"] = json[MCMT_DEVICE_NAME];
            jsonConfig["unique_id"] = myNet.macToString(sender) + "-" + unit;
            jsonConfig["device_class"] = getValueName(json[MCMT_DEVICE_CLASS].as<ha_binary_sensor_device_class_t>());
            jsonConfig["state_topic"] = config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/status";
            jsonConfig["json_attributes_topic"] = config.topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/attributes";
            jsonConfig["payload_on"] = json[MCMT_PAYLOAD_ON];
            jsonConfig["expire_after"] = json[MCMT_EXPIRE_AFTER];
            jsonConfig["force_update"] = "true";
            jsonConfig["retain"] = "true";
            char buffer[2048]{0};
            serializeJsonPretty(jsonConfig, buffer);
            mqttPublish((config.topicPrefix + "/" + getValueName(json[MCMT_COMPONENT_TYPE].as<ha_component_type_t>()) + "/" + myNet.macToString(sender) + "-" + unit + "/config").c_str(), buffer, true);
        }
    }
    if (incomingData.payloadsType == ENPT_FORWARD)
    {
        esp_now_payload_data_t forwardData;
        memcpy(&forwardData.message, &incomingData.message, sizeof(esp_now_payload_data_t::message));
        DynamicJsonDocument json(sizeof(esp_now_payload_data_t::message));
        deserializeJson(json, forwardData.message);
        if (incomingData.deviceType == ENDT_RF_GATEWAY)
            mqttPublish((config.topicPrefix + "/rf_sensor/" + getValueName(json["type"].as<rf_sensor_type_t>()) + "/" + json["id"].as<uint16_t>() + "/state").c_str(), incomingData.message, false);
    }
}

void onMqttMessage(char *topic, byte *payload, unsigned int length)
{
    String mac = getValue(String(topic).substring(0, String(topic).length()), '/', 2);
    String message;
    bool flag{false};
    for (uint16_t i = 0; i < length; ++i)
    {
        message += (char)payload[i];
    }
    esp_now_payload_data_t outgoingData;
    outgoingData.deviceType = ENDT_GATEWAY;
    outgoingData.payloadsType = ENPT_SET;
    DynamicJsonDocument json(sizeof(esp_now_payload_data_t::message));
    if (message == "update" || message == "restart")
    {
        if (mac == myNet.getNodeMac() && message == "restart")
            ESP.restart();
        flag = true;
    }
    if (String(topic) == config.topicPrefix + "/espnow_switch/" + mac + "/set" || String(topic) == config.topicPrefix + "/espnow_led/" + mac + "/set")
    {
        flag = true;
        json["set"] = message;
    }
    if (String(topic) == config.topicPrefix + "/espnow_led/" + mac + "/brightness")
    {
        flag = true;
        json["brightness"] = message;
    }
    if (String(topic) == config.topicPrefix + "/espnow_led/" + mac + "/temperature")
    {
        flag = true;
        json["temperature"] = message;
    }
    if (String(topic) == config.topicPrefix + "/espnow_led/" + mac + "/rgb")
    {
        flag = true;
        json["rgb"] = message;
    }
    if (flag)
    {
        if (message == "restart")
            outgoingData.payloadsType = ENPT_RESTART;
        if (message == "update")
            outgoingData.payloadsType = ENPT_UPDATE;
        serializeJsonPretty(json, outgoingData.message);
        char temp[sizeof(esp_now_payload_data_t)]{0};
        memcpy(&temp, &outgoingData, sizeof(esp_now_payload_data_t));
        uint8_t target[6];
        myNet.stringToMac(mac, target);
        myNet.sendUnicastMessage(temp, target);
    }
}

void sendKeepAliveMessage()
{
    keepAliveMessageTimerSemaphore = false;
    if (isMqttAvailable)
        mqttPublish((config.topicPrefix + "/espnow_gateway/" + myNet.getNodeMac() + "/status").c_str(), "online", true);
    esp_now_payload_data_t outgoingData;
    outgoingData.deviceType = ENDT_GATEWAY;
    outgoingData.payloadsType = ENPT_KEEP_ALIVE;
    DynamicJsonDocument json(sizeof(esp_now_payload_data_t::message));
    json["MQTT"] = isMqttAvailable ? "online" : "offline";
    json["frequency"] = 10; // For compatibility with the previous version. Will be removed in future releases.
    if (config.workMode == ESP_NOW_WIFI && WiFi.isConnected())
    {
        ntpWiFiClient.update();
        uint64_t epochTime = ntpWiFiClient.getEpochTime();
        struct tm *time = gmtime((time_t *)&epochTime);
        json["time"] = ntpWiFiClient.getFormattedTime();
        json["date"] = String(time->tm_mday) + "." + String(time->tm_mon + 1) + "." + String(time->tm_year + 1900);
    }
    if (config.workMode == ESP_NOW_LAN && Ethernet.linkStatus() == LinkON)
    {
        ntpEthClient.update();
        uint64_t epochTime = ntpEthClient.getEpochTime();
        struct tm *time = gmtime((time_t *)&epochTime);
        json["time"] = ntpEthClient.getFormattedTime();
        json["date"] = String(time->tm_mday) + "." + String(time->tm_mon + 1) + "." + String(time->tm_year + 1900);
    }
    char buffer[sizeof(esp_now_payload_data_t::message)]{0};
    serializeJsonPretty(json, buffer);
    memcpy(&outgoingData.message, &buffer, sizeof(esp_now_payload_data_t::message));
    char temp[sizeof(esp_now_payload_data_t)]{0};
    memcpy(&temp, &outgoingData, sizeof(esp_now_payload_data_t));
    myNet.sendBroadcastMessage(temp);
}

void sendAttributesMessage()
{
    if (!isMqttAvailable)
        return;
    attributesMessageTimerSemaphore = false;
    uint32_t secs = millis() / 1000;
    uint32_t mins = secs / 60;
    uint32_t hours = mins / 60;
    uint32_t days = hours / 24;
    DynamicJsonDocument json(sizeof(esp_now_payload_data_t::message));
    json["Type"] = "ESP-NOW gateway";
#if defined(ESP8266)
    json["MCU"] = "ESP8266";
#endif
#if defined(ESP32)
    json["MCU"] = "ESP32";
#endif
    json["MAC"] = myNet.getNodeMac();
    json["Firmware"] = firmware;
    json["Library"] = myNet.getFirmwareVersion();
    if (config.workMode == ESP_NOW_WIFI)
        json["IP"] = WiFi.localIP().toString();
    if (config.workMode == ESP_NOW_LAN)
        json["IP"] = Ethernet.localIP().toString();
    json["Uptime"] = "Days:" + String(days) + " Hours:" + String(hours - (days * 24)) + " Mins:" + String(mins - (hours * 60));
    char buffer[sizeof(esp_now_payload_data_t::message)]{0};
    serializeJsonPretty(json, buffer);
    mqttPublish((config.topicPrefix + "/espnow_gateway/" + myNet.getNodeMac() + "/attributes").c_str(), buffer, true);
}

void sendConfigMessage()
{
    DynamicJsonDocument json(2048); // Same as PubSubClient buffer size.
    json["platform"] = "mqtt";
    json["name"] = config.deviceName;
    json["unique_id"] = myNet.getNodeMac() + "-1";
    json["device_class"] = "connectivity";
    json["state_topic"] = config.topicPrefix + "/espnow_gateway/" + myNet.getNodeMac() + "/status";
    json["json_attributes_topic"] = config.topicPrefix + "/espnow_gateway/" + myNet.getNodeMac() + "/attributes";
    json["payload_on"] = "online";
    json["expire_after"] = 30;
    json["force_update"] = "true";
    json["retain"] = "true";
    char buffer[1024]{0};
    serializeJsonPretty(json, buffer);
    mqttPublish((config.topicPrefix + "/binary_sensor/" + myNet.getNodeMac() + "-1" + "/config").c_str(), buffer, true);
}

String getValue(String data, char separator, uint8_t index)
{
    uint8_t found{0};
    int8_t strIndex[]{0, -1};
    uint8_t maxIndex = data.length() - 1;
    for (uint8_t i{0}; i <= maxIndex && found <= index; i++)
        if (data.charAt(i) == separator || i == maxIndex)
        {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i + 1 : i;
        }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void loadConfig()
{
    EEPROM.begin(4096);
    if (EEPROM.read(4095) == 254)
    {
        EEPROM.get(0, config);
        EEPROM.end();
    }
    else
    {
        EEPROM.end();
        saveConfig();
    }
    delay(50);
}

void saveConfig()
{
    EEPROM.begin(4096);
    EEPROM.write(4095, 254);
    EEPROM.put(0, config);
    EEPROM.end();
    delay(50);
}

String xmlNode(String tags, String data)
{
    String temp = "<" + tags + ">" + data + "</" + tags + ">";
    return temp;
}

void setupWebServer()
{
    SSDP.setSchemaURL("description.xml");
    SSDP.setDeviceType("upnp:rootdevice");

    webServer.on("/description.xml", HTTP_GET, [](AsyncWebServerRequest *request)
                 {
        String ssdpSend = "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">";
        String ssdpHeader = xmlNode("major", "1");
        ssdpHeader += xmlNode("minor", "0");
        ssdpHeader = xmlNode("specVersion", ssdpHeader);
        ssdpHeader += xmlNode("URLBase", "http://" + WiFi.localIP().toString());
        String ssdpDescription = xmlNode("deviceType", "upnp:rootdevice");
        ssdpDescription += xmlNode("friendlyName", config.deviceName);
        ssdpDescription += xmlNode("presentationURL", "/");
        ssdpDescription += xmlNode("serialNumber", "0000000" + String(random(1000)));
        ssdpDescription += xmlNode("modelName", "ESP-NOW gateway");
        ssdpDescription += xmlNode("modelNumber", firmware);
        ssdpDescription += xmlNode("modelURL", "https://github.com/aZholtikov/ESP-NOW-Gateway");
        ssdpDescription += xmlNode("manufacturer", "Alexey Zholtikov");
        ssdpDescription += xmlNode("manufacturerURL", "https://github.com/aZholtikov");
        ssdpDescription += xmlNode("UDN", "DAA26FA3-D2D4-4072-BC7A-" + myNet.getNodeMac());
        ssdpDescription = xmlNode("device", ssdpDescription);
        ssdpHeader += ssdpDescription;
        ssdpSend += ssdpHeader;
        ssdpSend += "</root>";
        request->send(200, "text/xml", ssdpSend); });

    webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                 { request->send(LittleFS, "/index.htm"); });

    webServer.on("/function.js", HTTP_GET, [](AsyncWebServerRequest *request)
                 { request->send(LittleFS, "/function.js"); });

    webServer.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request)
                 { request->send(LittleFS, "/style.css"); });

    webServer.on("/setting", HTTP_GET, [](AsyncWebServerRequest *request)
                 {
        config.ssid = request->getParam("ssid")->value();
        config.password = request->getParam("password")->value();
        config.mqttHostName = request->getParam("mqttHostName")->value();
        config.mqttHostPort = request->getParam("mqttHostPort")->value().toInt();
        config.mqttUserLogin = request->getParam("mqttUserLogin")->value();
        config.mqttUserPassword = request->getParam("mqttUserPassword")->value();
        config.topicPrefix = request->getParam("topicPrefix")->value();
        config.deviceName = request->getParam("deviceName")->value();
        config.espnowNetName = request->getParam("espnowNetName")->value();
        config.workMode = request->getParam("workMode")->value().toInt();
        config.ntpHostName = request->getParam("ntpHostName")->value();
        config.gmtOffset = request->getParam("gmtOffset")->value().toInt();
        request->send(200);
        saveConfig(); });

    webServer.on("/config", HTTP_GET, [](AsyncWebServerRequest *request)
                 {
        String configJson;
        DynamicJsonDocument json(2048); // For overflow protection.
        json["firmware"] = firmware;
        json["espnowNetName"] = config.espnowNetName;
        json["deviceName"] = config.deviceName;
        json["ssid"] = config.ssid;
        json["password"] = config.password;
        json["mqttHostName"] = config.mqttHostName;
        json["mqttHostPort"] = config.mqttHostPort;
        json["mqttUserLogin"] = config.mqttUserLogin;
        json["mqttUserPassword"] = config.mqttUserPassword;
        json["topicPrefix"] = config.topicPrefix;
        json["workMode"] = config.workMode;
        json["ntpHostName"] = config.ntpHostName;
        json["gmtOffset"] = config.gmtOffset;
        serializeJsonPretty(json, configJson);
        request->send(200, "application/json", configJson); });

    webServer.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request)
                 {request->send(200);
        ESP.restart(); });

    webServer.onNotFound([](AsyncWebServerRequest *request)
                         { request->send(404, "text/plain", "File Not Found"); });

    if (config.workMode == ESP_NOW_WIFI)
        SSDP.begin();

    webServer.begin();
}

void checkMqttAvailability()
{
    mqttAvailabilityCheckTimerSemaphore = false;

    if (config.workMode == ESP_NOW_WIFI)
        if (WiFi.isConnected())
            if (!mqttWifiClient.connected())
            {
                isMqttAvailable = false;
                if (mqttWifiClient.connect(mqttUserID, config.mqttUserLogin.c_str(), config.mqttUserPassword.c_str()))
                {
                    isMqttAvailable = true;

                    mqttWifiClient.subscribe((config.topicPrefix + "/espnow_gateway/#").c_str());
                    mqttWifiClient.subscribe((config.topicPrefix + "/espnow_switch/#").c_str());
                    mqttWifiClient.subscribe((config.topicPrefix + "/espnow_led/#").c_str());

                    sendConfigMessage();
                    sendAttributesMessage();
                    sendKeepAliveMessage();
                }
            }

    if (config.workMode == ESP_NOW_LAN)
        if (Ethernet.linkStatus() == LinkON)
            if (!mqttEthClient.connected())
            {
                isMqttAvailable = false;
                if (mqttEthClient.connect(mqttUserID, config.mqttUserLogin.c_str(), config.mqttUserPassword.c_str()))
                {
                    isMqttAvailable = true;

                    mqttEthClient.subscribe((config.topicPrefix + "/espnow_gateway/#").c_str());
                    mqttEthClient.subscribe((config.topicPrefix + "/espnow_switch/#").c_str());
                    mqttEthClient.subscribe((config.topicPrefix + "/espnow_led/#").c_str());

                    sendConfigMessage();
                    sendAttributesMessage();
                    sendKeepAliveMessage();
                }
            }
}

void mqttPublish(const char *topic, const char *payload, bool retained)
{
    if (config.workMode == ESP_NOW_WIFI)
        mqttWifiClient.publish(topic, payload, retained);
    if (config.workMode == ESP_NOW_LAN)
        mqttEthClient.publish(topic, payload, retained);
}

void mqttAvailabilityCheckTimerCallback()
{
    mqttAvailabilityCheckTimerSemaphore = true;
}

void keepAliveMessageTimerCallback()
{
    keepAliveMessageTimerSemaphore = true;
}

void attributesMessageTimerCallback()
{
    attributesMessageTimerSemaphore = true;
}