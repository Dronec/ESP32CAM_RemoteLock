#include "esp_timer.h"
#include "img_converters.h"
//#include "Arduino.h"
#include "fb_gfx.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include "FS.h" // SD Card ESP32
#include "SD_MMC.h"
// Everything else
#include <Arduino_JSON.h>
#include <Arduino.h>
//#include <Preferences.h>
#include <DefsWiFi.h>

// Local includes
#include "./libraries/Blink.h"
#include "./libraries/httpcameraserver.h"
#include "./libraries/camera.h"

#define pinLock 13  // Controls magnetic lock
#define pinLight 14 // Controls entrance lights

const char *ssid = WIFISSID_2;
const char *password = WIFIPASS_2;
const char *softwareVersion = "0.21";

bool lightOn = false;
bool doorUnlock = false;
long lockOpenTime;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create a WebSocket object
AsyncWebSocket ws("/ws");

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    // Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    break;
  case WS_EVT_DISCONNECT:
    // Serial.printf("WebSocket client #%u disconnected\n", client->id());
    break;
  case WS_EVT_DATA:
    handleWebSocketMessage(arg, data, len);
    break;
  case WS_EVT_PONG:
  case WS_EVT_ERROR:
    break;
  }
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    data[len] = 0;
    JSONVar webmsg = JSON.parse((char *)data);
    // values

    /*
    if (webmsg.hasOwnProperty("currentCamera"))
    {
      currentCamera = atoi(webmsg["currentCamera"]);
      EnableCamera(currentCamera);
    }
    if (webmsg.hasOwnProperty("frontCamTimeout"))
      frontCamTimeout = atoi(webmsg["frontCamTimeout"]);
    if (webmsg.hasOwnProperty("serialOutput"))
      serialOutput = atoi(webmsg["serialOutput"]);
    if (webmsg.hasOwnProperty("rearCamMode"))
      rearCamMode = atoi(webmsg["rearCamMode"]);
    if (webmsg.hasOwnProperty("loopDelay"))
      loopDelay = atoi(webmsg["loopDelay"]);
    if (webmsg.hasOwnProperty("canInterface"))
      canInterface = atoi(webmsg["canInterface"]);
    if (webmsg.hasOwnProperty("canSpeed"))
      canSpeed = atoi(webmsg["canSpeed"]);
      */
    // checkboxes
    if (webmsg.hasOwnProperty("doorUnlock"))
      doorUnlock = webmsg["doorUnlock"];
    if (webmsg.hasOwnProperty("lightOn"))
      lightOn = webmsg["lightOn"];

    if (webmsg.hasOwnProperty("command"))
    {
      int command = atoi(webmsg["command"]);
      if (command == 0)
        ESP.restart();
    }

    // writeEEPROMSettings();

    notifyClients(getOutputStates());
  }
}

char *millisToTime(unsigned long currentMillis)
{
  unsigned long seconds = currentMillis / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  currentMillis %= 1000;
  seconds %= 60;
  minutes %= 60;
  hours %= 24;
  static char buffer[50];
  if (days == 0 && hours == 0 && minutes == 0)
    sprintf(buffer, "%lu sec ", seconds);
  else if (days == 0 && hours == 0 && minutes > 0)
    sprintf(buffer, "%lu min %lu sec ", minutes, seconds);
  else if (days == 0 && hours > 0)
    sprintf(buffer, "%lu h %lu m %lu s ", hours, minutes, seconds);
  else
    sprintf(buffer, "%lud %luh %lum %lus ", days, hours, minutes, seconds);
  return buffer;
}

void notifyClients(String state)
{
  ws.textAll(state);
}

String getOutputStates()
{
  JSONVar myArray;

  // sending stats
  myArray["stats"]["ssid"] = ssid;
  myArray["stats"]["softwareVersion"] = softwareVersion;
  // myArray["stats"]["reverseGearActive"] = reverseGearActive;
  myArray["stats"]["uptime"] = millisToTime(millis());
  myArray["stats"]["ram"] = (int)ESP.getFreeHeap();
  /*
    // sending values
    myArray["settings"]["currentCamera"] = currentCamera;
    myArray["settings"]["frontCamTimeout"] = frontCamTimeout;
    myArray["settings"]["rearCamMode"] = rearCamMode;
    myArray["settings"]["serialOutput"] = serialOutput;
    myArray["settings"]["loopDelay"] = loopDelay;
    myArray["settings"]["canSpeed"] = canSpeed;
    myArray["settings"]["canInterface"] = canInterface;
 */
  // sending checkboxes
  myArray["checkboxes"]["doorUnlock"] = doorUnlock;
  myArray["checkboxes"]["lightOn"] = lightOn;

  String jsonString = JSON.stringify(myArray);

  return jsonString;
}

void initWebSocket()
{
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void initSDCard()
{
  Serial.println("Starting SD Card");
  if (!SD_MMC.begin())
  {
    Serial.println("SD Card Mount Failed");
    return;
  }

  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE)
  {
    Serial.println("No SD Card attached");
    return;
  }
}

void setup()
{

  Serial.begin(115200);
  // Inits

  BlinkInit();
  CameraInit();
  initSDCard();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  // Print local IP address and start web server
  Serial.println("");
  Serial.println("Version:");
  Serial.println(softwareVersion);
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  initWebSocket();

  // Start ElegantOTA
  AsyncElegantOTA.begin(&server);

  // Camera stuff
  // server.on("/bmp", HTTP_GET, sendBMP);
  // server.on("/capture", HTTP_GET, sendJpg);
  server.on("/stream", HTTP_GET, streamJpg);
  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request)
            { ESP.restart(); });
  // server.on("/status", HTTP_GET, getCameraStatus);
  //   Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SD_MMC, "/index.html", "text/html", false); });

  server.serveStatic("/", SD_MMC, "/");
  // Start server
  server.begin();
  // Pins
  pinMode(pinLock, OUTPUT);
  pinMode(pinLight, OUTPUT);
  HandleGPIO();
}

void HandleGPIO()
{
  digitalWrite(pinLock, doorUnlock);
  digitalWrite(pinLight, lightOn);
}

void loop()
{
  HandleGPIO();
  notifyClients(getOutputStates());
  delay(1000);
}