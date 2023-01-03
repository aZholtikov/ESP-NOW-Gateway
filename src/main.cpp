#include "ArduinoJson.h"
#include "ArduinoOTA.h"
#include "ESPAsyncWebServer.h"
#include "AsyncMQTTClient.h"
#include "Ticker.h"
#include "ZHNetwork.h"
#include "ZHConfig.h"
#if defined(ESP8266)
#include "ESP8266SSDP.h"
#endif
#if defined(ESP32)
#include "SPIFFS.h"
#include "ESP32SSDP.h"
#endif

void onWifiEvent(WiFiEvent_t event);

void onEspnowMessage(const char *data, const uint8_t *sender);

void onMqttConnect(bool sessionPresent);
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);

void sendKeepAliveMessage(void);
void sendAttributesMessage(void);

String getValue(String data, char separator, uint8_t index);

void loadConfig(void);
void saveConfig(void);

String xmlNode(String tags, String data);
void setupWebServer(void);

void connectToMqtt(void);

const String firmware{"1.01"};

String espnowNetName{"DEFAULT"};

String deviceName{"ESP-NOW gateway"};

String ssid{"SSID"};
String password{"PASSWORD"};

String mqttHostName{"MQTT"};
uint16_t mqttHostPort{1883};
String mqttUserLogin{""};
String mqttUserPassword{""};
String topicPrefix{"homeassistant"};

ZHNetwork myNet;
AsyncWebServer webServer(80);
AsyncMqttClient mqttClient;

Ticker mqttReconnectTimer;
bool mqttReconnectTimerSemaphore{false};
void mqttReconnectTimerCallback(void);

Ticker keepAliveMessageTimer;
bool keepAliveMessageTimerSemaphore{true};
void keepAliveMessageTimerCallback(void);

Ticker attributesMessageTimer;
bool attributesMessageTimerSemaphore{false};
void attributesMessageTimerCallback(void);

void setup()
{
    SPIFFS.begin();

    loadConfig();

    WiFi.onEvent(onWifiEvent);
#if defined(ESP8266)
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif
#if defined(ESP32)
    WiFi.setSleep(WIFI_PS_NONE);
#endif

    WiFi.persistent(false);
    WiFi.setAutoConnect(false);
    WiFi.setAutoReconnect(true);

    myNet.begin(espnowNetName.c_str(), true);

    myNet.setOnBroadcastReceivingCallback(onEspnowMessage);
    myNet.setOnUnicastReceivingCallback(onEspnowMessage);

    WiFi.softAP(("ESP-NOW Gateway " + myNet.getNodeMac()).c_str(), "12345678");
    uint8_t scan = WiFi.scanNetworks(false, false, 1);
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
        if (name == ssid)
            WiFi.begin(ssid.c_str(), password.c_str());
    }

    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.setServer(mqttHostName.c_str(), mqttHostPort);
    mqttClient.setCredentials(mqttUserLogin.c_str(), mqttUserPassword.c_str());

    setupWebServer();

    ArduinoOTA.begin();

    keepAliveMessageTimer.attach(10, keepAliveMessageTimerCallback);
}

void loop()
{
    if (mqttReconnectTimerSemaphore)
        connectToMqtt();
    if (keepAliveMessageTimerSemaphore)
        sendKeepAliveMessage();
    if (attributesMessageTimerSemaphore)
        sendAttributesMessage();
    myNet.maintenance();
    ArduinoOTA.handle();
}

void onWifiEvent(WiFiEvent_t event)
{
#if defined(ESP8266)
    if (event == WIFI_EVENT_STAMODE_GOT_IP)
#endif
#if defined(ESP32)
        if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP)
#endif
            mqttClient.connect();
}

void onEspnowMessage(const char *data, const uint8_t *sender)
{
    if (!mqttClient.connected())
        return;
    esp_now_payload_data_t incomingData;
    memcpy(&incomingData, data, sizeof(esp_now_payload_data_t));
    if (incomingData.payloadsType == ENPT_ATTRIBUTES)
        mqttClient.publish((topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/" + getValueName(incomingData.payloadsType)).c_str(), 2, true, incomingData.message);
    if (incomingData.payloadsType == ENPT_KEEP_ALIVE)
        mqttClient.publish((topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/" + getValueName(incomingData.payloadsType)).c_str(), 2, true, "online");
    if (incomingData.payloadsType == ENPT_STATE)
        mqttClient.publish((topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/" + getValueName(incomingData.payloadsType)).c_str(), 2, true, incomingData.message);
    if (incomingData.payloadsType == ENPT_CONFIG)
        if (incomingData.deviceType == ENDT_SWITCH)
        {
            esp_now_payload_data_t configData;
            memcpy(&configData.message, &incomingData.message, sizeof(esp_now_payload_data_t::message));
            StaticJsonDocument<sizeof(esp_now_payload_data_t::message)> json;
            deserializeJson(json, configData.message);
            uint8_t unit = json["unit"].as<uint8_t>();
            String type = json["type"];
            StaticJsonDocument<1024> jsonConfig;
            jsonConfig["platform"] = "mqtt";
            jsonConfig["name"] = json["name"];
            jsonConfig["unique_id"] = myNet.macToString(sender) + "-" + unit;
            jsonConfig["device_class"] = json["class"];
            jsonConfig["state_topic"] = topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/state";
            jsonConfig["value_template"] = "{{ value_json.state }}";
            jsonConfig["command_topic"] = topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/set";
            jsonConfig["json_attributes_topic"] = topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/attributes";
            jsonConfig["availability_topic"] = topicPrefix + "/" + getValueName(incomingData.deviceType) + "/" + myNet.macToString(sender) + "/status";
            jsonConfig["payload_on"] = json["reverse"] == "true" ? "OFF" : "ON";
            jsonConfig["payload_off"] = json["reverse"] == "true" ? "ON" : "OFF";
            jsonConfig["optimistic"] = "false";
            jsonConfig["qos"] = 2;
            jsonConfig["retain"] = "true";
            char buffer[1024]{0};
            serializeJsonPretty(jsonConfig, buffer);
            mqttClient.publish((topicPrefix + "/" + type + "/" + myNet.macToString(sender) + "-" + unit + "/config").c_str(), 2, true, buffer);
        }
    if (incomingData.payloadsType == ENPT_FORWARD)
    {
        esp_now_payload_data_t forwardData;
        memcpy(&forwardData.message, &incomingData.message, sizeof(esp_now_payload_data_t::message));
        StaticJsonDocument<sizeof(esp_now_payload_data_t::message)> json;
        deserializeJson(json, forwardData.message);
        mqttClient.publish((topicPrefix + "/rf_sensor/" + getValueName(json["type"].as<rf_sensor_type_t>()) + "/" + json["id"].as<uint16_t>()).c_str(), 2, false, incomingData.message);
    }
}

void onMqttConnect(bool sessionPresent)
{
    mqttClient.subscribe((topicPrefix + "/espnow_gateway/#").c_str(), 2);
    mqttClient.subscribe((topicPrefix + "/espnow_switch/#").c_str(), 2);
    mqttClient.subscribe((topicPrefix + "/espnow_led/#").c_str(), 2);

    StaticJsonDocument<1024> json;
    json["platform"] = "mqtt";
    json["name"] = deviceName;
    json["unique_id"] = myNet.getNodeMac() + "-1";
    json["device_class"] = "connectivity";
    json["state_topic"] = topicPrefix + "/espnow_gateway/" + myNet.getNodeMac() + "/status";
    json["json_attributes_topic"] = topicPrefix + "/espnow_gateway/" + myNet.getNodeMac() + "/attributes";
    json["payload_on"] = "online";
    json["expire_after"] = 30;
    json["force_update"] = "true";
    json["qos"] = 2;
    json["retain"] = "true";
    char buffer[1024]{0};
    serializeJsonPretty(json, buffer);
    mqttClient.publish((topicPrefix + "/binary_sensor/" + myNet.getNodeMac() + "-1" + "/config").c_str(), 2, true, buffer);

    sendKeepAliveMessage();
    sendAttributesMessage();
    attributesMessageTimer.attach(60, attributesMessageTimerCallback);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
    mqttReconnectTimer.once(5, mqttReconnectTimerCallback);
    sendKeepAliveMessage();
    attributesMessageTimer.detach();
}

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
    String mac = getValue(String(topic).substring(0, String(topic).length()), '/', 2);
    String message;
    bool flag{false};
    for (uint16_t i = 0; i < len; ++i)
    {
        message += (char)payload[i];
    }
    esp_now_payload_data_t outgoingData;
    outgoingData.deviceType = ENDT_GATEWAY;
    StaticJsonDocument<sizeof(esp_now_payload_data_t::message)> json;
    if (message == "update" || message == "restart")
    {
        mqttClient.publish(topic, 2, true, "");
        mqttClient.publish((String(topic) + "/status").c_str(), 2, true, "offline");
        if (mac == myNet.getNodeMac() && message == "restart")
            ESP.restart();
        flag = true;
    }
    if (String(topic) == topicPrefix + "/espnow_switch/" + mac + "/set" || String(topic) == topicPrefix + "/espnow_led/" + mac + "/set")
    {
        flag = true;
        json["set"] = message == "ON" ? "ON" : "OFF";
    }
    if (String(topic) == topicPrefix + "/espnow_led/" + mac + "/brightness")
    {
        flag = true;
        json["brightness"] = message;
    }
    if (String(topic) == topicPrefix + "/espnow_led/" + mac + "/temperature")
    {
        flag = true;
        json["temperature"] = message;
    }
    if (String(topic) == topicPrefix + "/espnow_led/" + mac + "/rgb")
    {
        flag = true;
        json["rgb"] = message;
    }
    if (flag)
    {
        if (message == "restart")
            outgoingData.payloadsType = ENPT_RESTART;
        else
            outgoingData.payloadsType = message == "update" ? ENPT_UPDATE : ENPT_SET;
        char buffer[sizeof(esp_now_payload_data_t::message)]{0};
        serializeJsonPretty(json, buffer);
        memcpy(&outgoingData.message, &buffer, sizeof(esp_now_payload_data_t::message));
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
    if (mqttClient.connected())
        mqttClient.publish((topicPrefix + "/espnow_gateway/" + myNet.getNodeMac() + "/status").c_str(), 2, true, "online");
    esp_now_payload_data_t outgoingData;
    outgoingData.deviceType = ENDT_GATEWAY;
    outgoingData.payloadsType = ENPT_KEEP_ALIVE;
    StaticJsonDocument<sizeof(esp_now_payload_data_t::message)> json;
    json["MQTT"] = mqttClient.connected() ? "online" : "offline";
    char buffer[sizeof(esp_now_payload_data_t::message)]{0};
    serializeJsonPretty(json, buffer);
    memcpy(&outgoingData.message, &buffer, sizeof(esp_now_payload_data_t::message));
    char temp[sizeof(esp_now_payload_data_t)]{0};
    memcpy(&temp, &outgoingData, sizeof(esp_now_payload_data_t));
    myNet.sendBroadcastMessage(temp);
}

void sendAttributesMessage()
{
    attributesMessageTimerSemaphore = false;
    uint32_t secs = millis() / 1000;
    uint32_t mins = secs / 60;
    uint32_t hours = mins / 60;
    uint32_t days = hours / 24;
    StaticJsonDocument<sizeof(esp_now_payload_data_t::message)> json;
    json["Type"] = "ESP-NOW Gateway";
#if defined(ESP8266)
    json["MCU"] = "ESP8266";
#endif
#if defined(ESP32)
    json["MCU"] = "ESP32";
#endif
    json["MAC"] = myNet.getNodeMac();
    json["Firmware"] = firmware;
    json["Library"] = myNet.getFirmwareVersion();
    json["IP"] = WiFi.localIP().toString();
    json["Uptime"] = "Days:" + String(days) + " Hours:" + String(hours - (days * 24)) + " Mins:" + String(mins - (hours * 60));
    char buffer[sizeof(esp_now_payload_data_t::message)]{0};
    serializeJsonPretty(json, buffer);
    mqttClient.publish((topicPrefix + "/espnow_gateway/" + myNet.getNodeMac() + "/attributes").c_str(), 2, true, buffer);
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
    if (!SPIFFS.exists("/config.json"))
        saveConfig();
    File file = SPIFFS.open("/config.json", "r");
    String jsonFile = file.readString();
    StaticJsonDocument<1024> json;
    deserializeJson(json, jsonFile);
    espnowNetName = json["espnowNetName"].as<String>();
    deviceName = json["deviceName"].as<String>();
    ssid = json["ssid"].as<String>();
    password = json["password"].as<String>();
    mqttHostName = json["mqttHostName"].as<String>();
    mqttHostPort = json["mqttHostPort"];
    mqttUserLogin = json["mqttUserLogin"].as<String>();
    mqttUserPassword = json["mqttUserPassword"].as<String>();
    topicPrefix = json["topicPrefix"].as<String>();
    file.close();
}

void saveConfig()
{
    StaticJsonDocument<1024> json;
    json["firmware"] = firmware;
    json["espnowNetName"] = espnowNetName;
    json["deviceName"] = deviceName;
    json["ssid"] = ssid;
    json["password"] = password;
    json["mqttHostName"] = mqttHostName;
    json["mqttHostPort"] = mqttHostPort;
    json["mqttUserLogin"] = mqttUserLogin;
    json["mqttUserPassword"] = mqttUserPassword;
    json["topicPrefix"] = topicPrefix;
    json["system"] = "empty";
    File file = SPIFFS.open("/config.json", "w");
    serializeJsonPretty(json, file);
    file.close();
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
        ssdpDescription += xmlNode("friendlyName", deviceName);
        ssdpDescription += xmlNode("presentationURL", "/");
        ssdpDescription += xmlNode("serialNumber", "0000000" + String(random(1000)));
        ssdpDescription += xmlNode("modelName", "ESP-NOW Gateway");
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
                 { request->send(SPIFFS, "/index.htm"); });

    webServer.on("/setting", HTTP_GET, [](AsyncWebServerRequest *request)
                 {
        ssid = request->getParam("ssid")->value();
        password = request->getParam("password")->value();
        mqttHostName = request->getParam("host")->value();
        mqttHostPort = request->getParam("port")->value().toInt();
        mqttUserLogin = request->getParam("login")->value();
        mqttUserPassword = request->getParam("pass")->value();
        topicPrefix = request->getParam("prefix")->value();
        deviceName = request->getParam("name")->value();
        espnowNetName = request->getParam("net")->value();
        request->send(200);
        saveConfig(); });

    webServer.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request)
                 {
        request->send(200);
        ESP.restart(); });

    webServer.onNotFound([](AsyncWebServerRequest *request)
                         { 
        if (SPIFFS.exists(request->url()))
        request->send(SPIFFS, request->url());
        else
        {
        request->send(404, "text/plain", "File Not Found");
        } });

    SSDP.begin();
    webServer.begin();
}

void connectToMqtt()
{
    mqttReconnectTimerSemaphore = false;
    mqttClient.connect();
}

void mqttReconnectTimerCallback()
{
    mqttReconnectTimerSemaphore = true;
}

void keepAliveMessageTimerCallback()
{
    keepAliveMessageTimerSemaphore = true;
}

void attributesMessageTimerCallback()
{
    attributesMessageTimerSemaphore = true;
}