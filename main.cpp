#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncMqttClient.h>
#include <ESP32SSDP.h>
#include "ArduinoJson.h" // Версия 5. С другой не работает.
#include <SPIFFS.h>
#include <FS.h>
#include <time.h>
#include <Ticker.h>
#include <ArduinoOTA.h>
#include "AsyncTelegram.h"

//*******************************************************************************************************************************//
String ssidStaName = "ZH-SMART";  // Имя Wi-Fi сети.
String passwordSta = "Firan1978"; // Пароль Wi-Fi сети.
String ssidApName = "ESP32";      // Имя точки доступа.
String passwordAp = "";           // Пароль точки доступа.

String deviceName = "Smart Home Controller";
String modelName = "Smart Home Controller";
String modelNumber = "00000003";
String serialNumber = "00000001";
String uuid = "3c1b475a-e586-40e9-8605-f818f0ad5891";

IPAddress IP(192, 168, 4, 1);           // IP адрес точки доступа.
String mqttHostName = "mqtt.zh.com.ru"; // Адрес MQTT сервера.
uint mqttHostPort = 1883;               // Порт MQTT сервера.
String mqttUserLogin = "";
String mqttUserPassword = "";

String token = "1471595796:AAGZPvrk8fa6-KaV0oTaveuPXMzA-_3Ql9U"; // Telegram токен.
ulong userID = 1472083376;

String controllerTopicHead = "Квартира/Контроллеры/Шлюз";

bool countertop_lighting_status_Kitchen;

bool heating_battery_1_valve_status_Living_Room;
int heating_battery_1_temperature_Living_Room;
//*******************************************************************************************************************************//

//*******************************************************************************************************************************//
void loadNetConfigFile(void); // Функция загрузки конфигурации из файла netconfig.json.
void saveNetConfigFile(void); // Функция записи конфигурации в файл netconfig.json.
void setupSsdp(void);         // Функция настройки протокола SSDP.
void setupWebServer(void);    // Функция настройки WEB сервера.
void connectToWiFi(void);     // Функция подключения к Wi-Fi сети.
void connectToMqtt(void);     // Функция подключения к MQTT серверу.
void reboot(void);            // Функция перезагрузки при работе в режиме точки доступа.

String processor(const String &var);                          // Функция обработки HTTP запросов.
String xmlNode(String tags, String data);                     // Функция "сборки" информационного SSDP файла.
String decToHex(uint32_t decValue, byte desiredStringLength); // Функция перевода в шестнадцатеричную систему.

void onMqttConnect(bool sessionPresent);                                                                                             // Событие. Если выполнено подключение к MQTT серверу.
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);                                                                       // Событие. Если произошло отключение от MQTT сервера.
void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total); // Событие. Если получен топик.
//*******************************************************************************************************************************//

//*******************************************************************************************************************************//
AsyncWebServer server(80);  // Создаём объект server для работы с библиотекой ESPAsyncWebServer.
AsyncMqttClient mqttClient; // Создаём объект mqttClient для работы с библиотекой AsyncMqttClient.
WiFiClient client;          // Создаём объект client для работы с библиотекой WiFi.
AsyncTelegram myBot;        // Создаём объект myBot для работы с библиотекой AsyncTelegram.

Ticker mqttReconnectTimer; // Создаём таймер переподключения к MQTT серверу (при потере соединения).
Ticker apModeRebootTimer;  // Создаём таймер перезагрузки при работе в режиме точки доступа.
//*******************************************************************************************************************************//

void setup()
{
  SPIFFS.begin();       // Инициируем работу файловой системы.
  Serial.begin(115200); // Инициируем работу SERIAL.

  mqttClient.onConnect(onMqttConnect);                      // Включаем обработчик события подключения к MQTT серверу.
  mqttClient.onDisconnect(onMqttDisconnect);                // Включаем обработчик события отключения от MQTT сервера.
  mqttClient.onMessage(onMqttMessage);                      // Включаем обработчик события получения топика.
  mqttClient.setServer(mqttHostName.c_str(), mqttHostPort); // Устанавливаем параметры подключения к MQTT серверу.
  mqttClient.setCredentials(mqttUserLogin.c_str(), mqttUserPassword.c_str());

  saveNetConfigFile();
  loadNetConfigFile(); // Загружаем конфигурацию из файла netconfig.json.
  connectToWiFi();     // Подключаемся к Wi-Fi.
  setupSsdp();         // Настраиваем протокол SSDP.
  setupWebServer();    // Настраиваем WEB сервер.
  connectToMqtt();     // Подключаемся к MQTT серверу.

  myBot.setTelegramToken(token.c_str());
  myBot.begin();

  ArduinoOTA.begin(); // Запускаем сервер обновления "по воздуху".

  apModeRebootTimer.attach(300, reboot); // Запускаем таймер перезагрузки при работе в режиме точки доступа.
}

void loop()
{
  ArduinoOTA.handle();
}

void loadNetConfigFile(void) // Функция загрузки конфигурации из файла netconfig.json.
{
  if (!SPIFFS.exists("/netconfig.json"))           // Если файл не существует:
    saveNetConfigFile();                           // Создаем файл, записав в него данные по умолчанию.
  File file = SPIFFS.open("/netconfig.json", "r"); // Открываем файл для чтения.
  String jsonFile = file.readString();             // Читаем файл в переменную.
  DynamicJsonDocument json(1024);                  // Резервируем память для JSON объекта.
  deserializeJson(json, jsonFile);
  ssidStaName = json["ssidStaName"].as<String>(); // Читаем поля JSON.
  passwordSta = json["passwordSta"].as<String>();
  ssidApName = json["ssidApName"].as<String>();
  passwordAp = json["passwordAp"].as<String>();
  deviceName = json["deviceName"].as<String>();
  mqttHostName = json["mqttHostName"].as<String>();
  mqttHostPort = json["mqttHostPort"];
  mqttUserLogin = json["mqttUserLogin"].as<String>();
  mqttUserPassword = json["mqttUserPassword"].as<String>();
  token = json["token"].as<String>();
  userID = json["userID"];
  file.close(); // Закрываем файл.
}

void saveNetConfigFile(void) // Функция записи конфигурации в файл netconfig.json.
{
  DynamicJsonDocument json(1024);    // Резервируем память для JSON объекта.
  json["ssidStaName"] = ssidStaName; // Заполняем поля JSON.
  json["passwordSta"] = passwordSta;
  json["ssidApName"] = ssidApName;
  json["passwordAp"] = passwordAp;
  json["deviceName"] = deviceName;
  json["mqttHostName"] = mqttHostName;
  json["mqttHostPort"] = mqttHostPort;
  json["mqttUserLogin"] = mqttUserLogin;
  json["mqttUserPassword"] = mqttUserPassword;
  json["token"] = token;
  json["userID"] = userID;
  File file = SPIFFS.open("/netconfig.json", "w"); // Открываем файл для записи.
  serializeJsonPretty(json, file);                 // Записываем строку JSON в файл.
  file.close();                                    // Закрываем файл.
}

void setupSsdp(void) // Функция настройки протокола SSDP.
{
  SSDP.setSchemaURL("description.xml");
  SSDP.setDeviceType("upnp:rootdevice");

  server.on("/description.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
    String ssdpSend = "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">";
    String ssdpHeder = xmlNode("major", "1");
    ssdpHeder += xmlNode("minor", "0");
    ssdpHeder = xmlNode("specVersion", ssdpHeder);
    ssdpHeder += xmlNode("URLBase", "http://" + WiFi.localIP().toString());
    String ssdpDescription = xmlNode("deviceType", "upnp:rootdevice");
    ssdpDescription += xmlNode("friendlyName", deviceName);
    ssdpDescription += xmlNode("presentationURL", "/");
    ssdpDescription += xmlNode("serialNumber", serialNumber);
    ssdpDescription += xmlNode("modelName", modelName);
    ssdpDescription += xmlNode("modelNumber", modelNumber);
    ssdpDescription += xmlNode("modelURL", "http://zh.com.ru");
    ssdpDescription += xmlNode("manufacturer", "Alexey Zholtikov");
    ssdpDescription += xmlNode("manufacturerURL", "http://zh.com.ru");
    ssdpDescription += xmlNode("UDN", uuid);
    ssdpDescription = xmlNode("device", ssdpDescription);
    ssdpHeder += ssdpDescription;
    ssdpSend += ssdpHeder;
    ssdpSend += "</root>";
    request->send(200, "text/xml", ssdpSend);
  });

  SSDP.begin(); // Запускаем протокол SSDP.
}

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
  if(!index){
    Serial.printf("UploadStart: %s\n", filename.c_str());
  }
  for(size_t i=0; i<len; i++){
    Serial.write(data[i]);
  }
  if(final){
    Serial.printf("UploadEnd: %s, %u B\n", filename.c_str(), index+len);
  }
}

void setupWebServer(void) // Функция настройки WEB сервера.
{
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.htm", String(), false, processor);
  });

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  server.on(
      "/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200);
      },
      handleUpload);

  server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request) { // Перезагрузка модуля по запросу вида /restart?device=ok.
    if (request->getParam("device")->value() == "ok")
    {
      request->send(200, "text/plain", "Reset OK");
      ESP.restart();
    }
    else
    {
      request->send(200, "text/plain", "No Reset");
    }
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) { // Получение статуса модуля.
    request->send(200, "text/plain", "OK");
  });

  server.on("/countertop_lighting_status_Kitchen", HTTP_GET, [](AsyncWebServerRequest *request) { // Получение статуса подсветки на кухне.
    StaticJsonDocument<100> json;
    json["value"] = countertop_lighting_status_Kitchen;
    char buffer[100];
    serializeJson(json, buffer);
    request->send(200, "text/json", buffer);

  });

  server.on("/heating_battery_1_valve_status_Living_Room", HTTP_GET, [](AsyncWebServerRequest *request) { // Получение статуса клапана батареи №1 в гостиной.
    StaticJsonDocument<100> json;
    json["value"] = heating_battery_1_valve_status_Living_Room;
    char buffer[100];
    serializeJson(json, buffer);
    request->send(200, "text/json", buffer);
  });

  server.on("/heating_battery_1_temperature_Living_Room", HTTP_GET, [](AsyncWebServerRequest *request) { // Получение температуры батареи №1 в гостиной.
    StaticJsonDocument<100> json;
    json["value"] = heating_battery_1_temperature_Living_Room;
    char buffer[100];
    serializeJson(json, buffer);
    request->send(200, "text/json", buffer);
  });

  server.onNotFound([](AsyncWebServerRequest *request) { // Передача файлов на страницу.
    if (SPIFFS.exists(request->url()))
      request->send(SPIFFS, request->url(), String(), false);
    else
    {
      request->send(404, "text/plain", "File Not Found");
    }
  });
  server.onFileUpload(handleUpload);
  server.begin(); // Запускаем WEB сервер.
}

void connectToWiFi(void) // Функция подключения к Wi-Fi сети.
{
  WiFi.mode(WIFI_STA);                                  // Устанавливаем режим работы (WIFI_STA - подключение к сети Wi-Fi).
  byte tries = 10;                                      // Счетчик количества попыток подключения.
  WiFi.begin(ssidStaName.c_str(), passwordSta.c_str()); // Подключаемся к Wi-Fi сети.
  while (tries-- && WiFi.status() != WL_CONNECTED)      // Пытаемся подключиться к Wi-Fi сети.
  {
    delay(1000); // Пауза между попытками подключения.
  }
  if (WiFi.status() != WL_CONNECTED) // Если подключение не удалось:
  {
    WiFi.disconnect();                                      // Отключаем Wi-Fi.
    WiFi.mode(WIFI_AP);                                     // Устанавливаем режим работы (WIFI_AP - точка доступа).
    WiFi.softAPConfig(IP, IP, IPAddress(255, 255, 255, 0)); // Задаем настройки сети.
    WiFi.softAP(ssidApName.c_str(), passwordAp.c_str());    // Включаем Wi-Fi в режиме точки доступа.
  }
}

void connectToMqtt() // Функция подключения к MQTT серверу.
{
  mqttClient.connect();
}

void reboot(void) // Функция перезагрузки при работе в режиме точки доступа.
{
  if (WiFi.getMode() == WIFI_AP)
  {
    ESP.restart();
  }
}

String processor(const String &var) // Функция обработки HTTP запросов.
{
  return String();
}

String xmlNode(String tags, String data) // Функция "сборки" информационного SSDP файла.
{
  String temp = "<" + tags + ">" + data + "</" + tags + ">";
  return temp;
}

String decToHex(uint32_t decValue, byte desiredStringLength) // Функция перевода в шестнадцатеричную систему.
{
  String hexString = String(decValue, HEX);
  while (hexString.length() < desiredStringLength)
    hexString = "0" + hexString;
  return hexString;
}

void onMqttConnect(bool sessionPresent) // Если выполнено подключение к MQTT серверу:
{
  mqttReconnectTimer.detach(); // Отключаем таймер переподключения к MQTT серверу.

  mqttClient.publish(String(controllerTopicHead + "/IP").c_str(), 2, true, String(WiFi.localIP().toString()).c_str()); // Публикуем IP.
  mqttClient.publish(String(controllerTopicHead + "/ID").c_str(), 2, true, decToHex(ESP.getEfuseMac(), 6).c_str());    // Публикуем Chip ID.
  mqttClient.publish(String(controllerTopicHead + "/Статус").c_str(), 2, true, "Работает");                            // Публикуем статус.

  mqttClient.subscribe("#", 2); // Подписываемся на все топики.
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) // Если произошло отключение от MQTT сервера:
{
  mqttReconnectTimer.attach(10, connectToMqtt); // Включаем таймер переподключения к MQTT серверу.
}

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) // Если получен топик:
{
  if (String(topic) == "Квартира/Освещение/Кухня/Подсветка столешницы/Состояние/Реле")
  {
    if (String(payload).substring(0, len) == "Вкл")
    {
      countertop_lighting_status_Kitchen = true;
      return;
    }
    if (String(payload).substring(0, len) == "Выкл")
    {
      countertop_lighting_status_Kitchen = false;
      return;
    }
  }

  if (String(topic) == "Квартира/Отопление/Гостиная/Батарея №1/Состояние/Клапан")
  {
    if (String(payload).substring(0, len) == "Открыт")
    {
      heating_battery_1_valve_status_Living_Room = true;
      return;
    }
    if (String(payload).substring(0, len) == "Закрыт")
    {
      heating_battery_1_valve_status_Living_Room = false;
      return;
    }
  }

  if (String(topic) == "Квартира/Отопление/Гостиная/Батарея №1/Состояние/Температура")
  {
    heating_battery_1_temperature_Living_Room = String(payload).substring(0, len).toInt();
    return;
  }
}