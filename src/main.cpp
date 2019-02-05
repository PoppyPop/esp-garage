#include <Arduino.h>

#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DHT.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <TaskScheduler.h>
#include "Inputs.h"
#include <EEPROM.h>
#include "save.h"

// #define _GARAGE_DEBUG 1
// #define _WEB_DEBUG 1

// EEPROM
#define eepromInitAddr 0L

#define eepromInitData 'T'

#define eepromDataAddr 1L
unsigned long averageTimeOpen = 0;
unsigned long currentMillisOpen = 0;

#define eepromDataOpenAddr 10L
unsigned long averageTimeToOpen = 0;
unsigned long currentMillisToOpen = 0;

#ifdef _WEB_DEBUG

bool eepromInit = false;
unsigned int eepromInitValue = 0;
unsigned long eepromInitDataValue = 0UL;

#endif

// In
#define closePin 5 // pushbutton connected to digital pin 7
#define openPin 16

#define infinitePin 0

// Out
#define ledPin 4 // LED connected to digital pin 13
#define alertPin 13
#define statusPin 12

// Temp Humidity
#define DHTPIN 14
#define DHTTYPE DHT22
#define DHTCYCLE 11
#define DHTWAIT 300 // 5min

#ifdef _GARAGE_DEBUG

// Wait door
#define DOORWAIT 10 // 300:5min
#define ALERTWAIT 5 // 60:1min

#else

// Wait door
#define DOORWAIT 3*60 // 3min
#define ALERTWAIT 30 // 30:30sec

#endif

Scheduler runner;

void resetInfinite();

//Tasks
void ReadInputs();
Task ReadInputsTask(250, TASK_FOREVER, &ReadInputs, &runner, true); // 1/4sec

void handleClosed();
Task handleClosedTask(500, TASK_FOREVER, &handleClosed, &runner, true);

void readtemp();
Task tempTask(DHTWAIT * 1000, TASK_FOREVER, &readtemp, &runner, true); // 5Min

void infinite();
Task infiniteTask(250, TASK_FOREVER, &infinite, &runner, true); // 1sec

void closed();
Task closedTask(2000, TASK_ONCE, &closed, &runner, false); // 4sec.

void notClosed();
Task notClosedTask(0, 5, &notClosed, &runner, false); // 5Min

bool alertEnable();
void alertOn();
void alertOff();
Task alertTask(500, 2, NULL, &runner, false, &alertEnable, &alertOff);

void blinkOn();
void blinkOff();
bool blinkOnEnable();
Task blink(250, 6, NULL, &runner, false, &blinkOnEnable, &blinkOff);

Inputs _inputs;
bool _neverGiveUp = false;
bool _infinite = false;

const unsigned int OPEN_ALERT_NONE = 0;
const unsigned int OPEN_ALERT_FIRST = 1;
const unsigned int OPEN_ALERT_REAL = 2;

unsigned int _openAlertLevel = OPEN_ALERT_NONE;

DHT dht(DHTPIN, DHTTYPE, DHTCYCLE);

float humidity, temp_f, hic; // Values read from sensor

// Web Server
#define ssid "MootCity-OuterLimit"
#define password "LXMFaab8wvX8JjUfRPmscL9NQ25qMWEGRG3x4vRdxX"
#define timeoutWifi 30

ESP8266WebServer server(80);

// OTA
#define OTAPport 8266
#define OTAPass "LXMFaab8wvX8JjUfRPmscL9NQ25qMWEGRG3x4vRdxX"

#define host "ESP-Garage"

unsigned long CurrentTimeOpen()
{
  if (currentMillisOpen > 0)
  {
    return (millis() - currentMillisOpen) / 1000;
  }

  return 0;
}

unsigned long CurrentTimeToOpen()
{
  if (currentMillisToOpen > 0)
  {
    return (millis() - currentMillisToOpen) / 1000;
  }

  return 0;
}

void UpdateAverageOpen(unsigned long currentOpen)
{
  if (averageTimeOpen == 0UL)
  {
    averageTimeOpen = currentOpen;
  }
  else
  {
    averageTimeOpen = (averageTimeOpen + currentOpen) / 2;
  }

  //EEPROMWritelong(eepromDataAddr, averageTimeOpen);
  EEPROM_writeAnything(eepromDataAddr, averageTimeOpen);
}

void UpdateAverageToOpen(unsigned long current)
{
  if (averageTimeToOpen == 0UL)
  {
    averageTimeToOpen = current;
  }
  else
  {
    averageTimeToOpen = (averageTimeToOpen + current) / 2;
  }

  EEPROM_writeAnything(eepromDataOpenAddr, averageTimeToOpen);
}

String CalcState()
{

  if (_neverGiveUp)
  {
    return "NeverGiveUp";
  }

  if (_infinite)
  {
    return "Infinite";
  }

  if (_inputs.Closed)
  {
    return "Closed";
  }
  else if (_inputs.Open)
  {
    return "Open";
  }
  else if (!_inputs.Closed && !_inputs.Open)
  {
    return "Not Closed / Not Open";
  }

  return "Unknown";
}

void handle_Move()
{
  alertTask.restart();

  String webString = "{";
  webString += "\"Status\": \"OK\"";
  webString += "}";

  server.send(200, "application/json", webString);
}

void ResetAverage()
{
  averageTimeOpen = 0UL;
  UpdateAverageOpen(0UL);
  averageTimeToOpen = 0UL;
  UpdateAverageToOpen(0UL);

  EEPROM.commit();
}

void handle_ResetAverageOpen()
{
  ResetAverage();

  String webString = "{";
  webString += "\"Status\": \"OK\"";
  webString += "}";

  server.send(200, "application/json", webString);
}

void handle_openAlert()
{
  String webString = "{";

  webString += "\"Status\": \"" + CalcState() + "\"";
  webString += ",";

  webString += "\"Alert\": " + String(_openAlertLevel);

  webString += "}";

  server.send(200, "application/json", webString);
}

void handle_root()
{
  String webString = "<html><head><title>Garage ESP</title></head><body>"; // String to display

  // Wifi
  webString += "<br /> ";
  webString += "SSID: ";
  webString += WiFi.SSID();
  webString += " RSSI: ";
  webString += WiFi.RSSI();
  webString += " IP: ";
  webString += WiFi.localIP().toString();

  // Temp
  webString += "<br /> ";
  webString += "Humidity: ";
  webString += String(humidity);
  webString += " % ";
  webString += "Temperature: ";
  webString += String(temp_f);
  webString += " *C ";
  webString += "Heat index: ";
  webString += String(hic);
  webString += " *C ";

  webString += "<br /> ";
  webString += "<br /> ";
  webString += "State: <b>";
  webString += CalcState();
  webString += "</b>";

  webString += "<br /> ";
  webString += "Open Alert Level: <b>";
  webString += _openAlertLevel;
  webString += "</b>";

  webString += "<br /> ";

  // Door Closed
  webString += "<br /> ";
  webString += "Door Closed: ";
  webString += _inputs.Closed ? "Yes" : "No";

  // Door Open
  webString += "<br /> ";
  webString += "Door Open: ";
  webString += _inputs.Open ? "Yes" : "No";

  // Infinite
  webString += "<br /> ";
  webString += "Infinite (Button): ";
  webString += _inputs.Infinite ? "Yes" : "No";

  // Infinite
  webString += "<br /> ";
  webString += "Infinite: ";
  webString += _infinite ? "Yes" : "No";

  // neverGiveUp
  webString += "<br /> ";
  webString += "NeverGiveUp: ";
  webString += _neverGiveUp ? "Yes" : "No";

  // notClosedTask
  webString += "<br /> ";
  webString += "notClosedTask: ";
  webString += notClosedTask.isEnabled() ? "Yes" : "No";

  webString += "<br /> ";

  // Average
  webString += "<br /> ";
  webString += "Average Open: ";
  webString += averageTimeOpen;

  // Current
  webString += "<br /> ";
  webString += "Current Open: ";
  webString += CurrentTimeOpen();

  webString += "<br /> ";

  // Average
  webString += "<br /> ";
  webString += "Average To Open: ";
  webString += averageTimeToOpen;

  // Current
  webString += "<br /> ";
  webString += "Current To Open: ";
  webString += CurrentTimeToOpen();

#ifdef _WEB_DEBUG
  webString += "<br /> ";
  webString += "eepromInit: ";
  webString += eepromInit ? "Yes" : "No";

  webString += "<br /> ";
  webString += "eeprom Init Value: ";
  webString += eepromInitValue;

  webString += "<br /> ";
  webString += "eeprom Data Value: ";
  webString += eepromInitDataValue;

#endif
  // eepromInit

  webString += "</body></html>";

  server.send(200, "text/html", webString);
  //delay(100);
}

void setupAP()
{
  WiFi.mode(WIFI_AP);

  // Append the last two bytes of the MAC (HEX'd) to string to make unique
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.softAPmacAddress(mac);
  String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                 String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
  macID.toUpperCase();
  String AP_NameString = "ESP-GARAGE-" + macID;

  char AP_NameChar[AP_NameString.length() + 1];
  memset(AP_NameChar, AP_NameString.length() + 1, 0);

  for (unsigned int i = 0; i < AP_NameString.length(); i++)
    AP_NameChar[i] = AP_NameString.charAt(i);

  WiFi.softAP(AP_NameChar, password);

  // config static IP
  IPAddress ip(172, 16, 0, 1);
  IPAddress gateway(172, 16, 0, 1);
  Serial.print(F("Setting static ip to : "));
  Serial.println(ip);
  IPAddress subnet(255, 255, 0, 0);
  WiFi.softAPConfig(ip, gateway, subnet);
}

void Connect()
{
  if (WiFi.getMode() == WIFI_AP)
  {
    WiFi.softAPdisconnect();
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    // Disable the AP
    WiFi.mode(WIFI_STA);
    WiFi.hostname(host);
    // Connect to WiFi network
    WiFi.begin(ssid, password);
    Serial.print("\n\r \n\rWorking to connect");

    // Wait for connection
    int curtime = 0;
    int curDelay = 500;
    while (WiFi.status() != WL_CONNECTED && curtime < (timeoutWifi * 1000))
    {
      digitalWrite(ledPin, HIGH);
      delay(curDelay / 2);
      digitalWrite(ledPin, LOW);
      delay(curDelay / 2);
      curtime += curDelay;
      Serial.print(".");
    }

    if (WiFi.status() != WL_CONNECTED)
    {
      // local access point mode
      setupAP();
    }

    digitalWrite(ledPin, LOW);

    Serial.println("");
    Serial.print("State: ");
    Serial.println(WiFi.status());
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }
}

void Disconnect()
{
  WiFi.mode(WIFI_OFF);
}

void ReadInputs()
{

  bool tmpOpen = !digitalRead(openPin); // True when pull down
#ifdef _GARAGE_DEBUG
  if (tmpOpen != _inputs.Open)
  {
    Serial.println((std::string("Open: ") + (tmpOpen ? "YES" : "NO")).c_str());
  }
#endif
  _inputs.Open = tmpOpen;

  bool tmpClosed = digitalRead(closePin);
#ifdef _GARAGE_DEBUG
  if (tmpClosed != _inputs.Closed)
  {
    Serial.println((std::string("Closed: ") + (tmpClosed ? "YES" : "NO")).c_str());
  }
#endif
  _inputs.Closed = tmpClosed;

  bool tmpInfinite = !digitalRead(infinitePin); // True when pull down
#ifdef _GARAGE_DEBUG
  if (tmpInfinite != _inputs.Infinite)
  {
    Serial.println((std::string("Infinite: ") + (tmpInfinite ? "YES" : "NO")).c_str());
  }
#endif
  _inputs.Infinite = tmpInfinite;
}

void handleClosed()
{
  bool closed = _inputs.Closed;
  bool open = _inputs.Open;

  digitalWrite(statusPin, closed ? LOW : HIGH);

  if (!closed && closedTask.isEnabled())
  {
#ifdef _GARAGE_DEBUG
    Serial.println("closedTask.disable();");
#endif
    closedTask.disable();
  }

  if (closed && notClosedTask.isEnabled() && !closedTask.isEnabled())
  {
#ifdef _GARAGE_DEBUG
    Serial.println("closedTask.restartDelayed();");
#endif
    closedTask.restartDelayed();
  }

  if (closed && (_neverGiveUp || _infinite) && !closedTask.isEnabled())
  {
#ifdef _GARAGE_DEBUG
    Serial.println("closed && (_neverGiveUp || _infinite)");
#endif
    closedTask.restartDelayed();
  }

  if (!_infinite && !_neverGiveUp && !closed && !notClosedTask.isEnabled())
  {
#ifdef _GARAGE_DEBUG
    Serial.println("notClosedTask.restartDelayed();");
#endif

    notClosedTask.setInterval(DOORWAIT * 1000);
    notClosedTask.restartDelayed();

    if (currentMillisOpen == 0)
    {
      currentMillisOpen = millis();
    }

    if (currentMillisToOpen == 0)
    {
      currentMillisToOpen = millis();
    }

    tempTask.disable();
  }

  if (!open)
  {
    // reset infinite if not open
    resetInfinite();
  }
  else if (currentMillisToOpen > 0UL)
  {
    // When open, save time to open
    UpdateAverageToOpen(CurrentTimeToOpen());
    currentMillisToOpen = 0UL;
  }
}

void closed()
{
#ifdef _GARAGE_DEBUG
  Serial.println("closed()");
#endif
  notClosedTask.disable();
  _neverGiveUp = false;
  _infinite = false;
  _openAlertLevel = OPEN_ALERT_NONE;

  unsigned long currentOpen = CurrentTimeOpen();

  UpdateAverageOpen(currentOpen);
  currentMillisOpen = 0UL;

  EEPROM.commit();

  tempTask.restart();
}

void notClosed()
{
#ifdef _GARAGE_DEBUG
  Serial.println("notClosed()");
#endif
  // The door has not been closed correctly
  if (!_inputs.Closed)
  {
    if (notClosedTask.isLastIteration())
    {
// Unable to close it, let it go
#ifdef _GARAGE_DEBUG
      Serial.println("Never give Up!");
#endif
      _neverGiveUp = true;
      _openAlertLevel = OPEN_ALERT_REAL;

      if (!_inputs.Open)
      {

        // !!! TODO SEND ANDROID ALERT !!!

        return;
      }
    }
    else if (notClosedTask.isFirstIteration())
    {
#ifdef _GARAGE_DEBUG
      Serial.println("notClosed() - ALERTWAIT");
#endif
      _openAlertLevel = OPEN_ALERT_FIRST;
      notClosedTask.setInterval(ALERTWAIT * 1000);
    }

#ifdef _GARAGE_DEBUG
    Serial.println("alertTask.restart();");
#endif
    alertTask.restart();
  }
}

bool alertEnable()
{
#ifdef _GARAGE_DEBUG
  Serial.println("alertEnable()");
#endif

  alertTask.setCallback(&alertOn);

  return true; // Task should be enabled
}

void alertOn()
{
#ifdef _GARAGE_DEBUG
  Serial.println("alertOn()");
#endif
  digitalWrite(alertPin, HIGH);
  digitalWrite(ledPin, HIGH);
  alertTask.setCallback(&alertOff);
}

void alertOff()
{
#ifdef _GARAGE_DEBUG
  Serial.println("alertOff()");
#endif
  digitalWrite(alertPin, LOW);
  digitalWrite(ledPin, LOW);
  //alertTask.disable();
}

void infinite()
{
  if (_inputs.Infinite)
  {
    if (_inputs.Open)
    {
#ifdef _GARAGE_DEBUG
      Serial.println("Go To Infinite !!!");
#endif
      _infinite = true;
      notClosedTask.disable();

      digitalWrite(ledPin, HIGH);
    }
    else
    {
#ifdef _GARAGE_DEBUG
      Serial.println("Infinite Fail !!!");
#endif
      blink.restart();
    }
  }
}

bool blinkOnEnable()
{
  blink.setCallback(&blinkOn);

  return true; // Task should be enabled
}

void blinkOn()
{
  digitalWrite(ledPin, HIGH);
  blink.setCallback(&blinkOff);
}
void blinkOff()
{
  digitalWrite(ledPin, LOW);
  blink.setCallback(&blinkOn);
}

void resetInfinite()
{
  _infinite = false;
  digitalWrite(ledPin, LOW);
}

void sendDatas()
{

  String datas = "{ \"room\": 1, \"roomName\": \"ESP\", \"id\": 8888, \"name\": \"ESP\", \"temp\": " + String(temp_f) + ", \"fibType\": \"com.fibaro.temperatureSensor\" }";

  Connect();

  HTTPClient http;
  http.begin("http://yugo.mo-ot.fr/temperatures/datas.php");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  // http.POST("from=user%40mail.com&to=user%40mail.com&text=Test+message+post&subject=Alarm%21%21%21");

  int result = http.sendRequest("PUT", datas);
  if (result == HTTP_CODE_OK)
  {
    Serial.println("Send OK!");
  }
  else
  {
    Serial.println("Send KO: " + String(result));
  }

  http.end();
}

void readtemp()
{
  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  humidity = dht.readHumidity();
  // leave time between 2 readings
  delay(10);
  // Read temperature as Celsius (the default)
  temp_f = dht.readTemperature();

  // Check if any reads failed and exit early (to try again).
  if (isnan(humidity) || isnan(temp_f))
  {
    Serial.println("Failed to read from DHT sensor!");
    // lastDHTRead = -1000;
    return;
  }

  // Compute heat index in Celsius (isFahreheit = false)
  hic = dht.computeHeatIndex(temp_f, humidity, false);

  sendDatas();
}

void setup()
{
  Serial.begin(115200);

  pinMode(alertPin, OUTPUT);
  digitalWrite(alertPin, LOW);

  pinMode(statusPin, OUTPUT);
  digitalWrite(statusPin, LOW);

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  pinMode(closePin, INPUT_PULLUP);

  pinMode(openPin, INPUT_PULLUP);

  pinMode(infinitePin, INPUT_PULLUP);

  EEPROM.begin(512);

  uint8_t initValue = EEPROM.read(eepromInitAddr);
#ifdef _WEB_DEBUG
  eepromInitValue = initValue;
#endif

  if (initValue != eepromInitData)
  {
#ifdef _WEB_DEBUG
    eepromInit = true;
#endif

    EEPROM.write(eepromInitAddr, eepromInitData);
    //EEPROMWritelong(eepromDataAddr, 0UL);

    ResetAverage();
  }

  //unsigned long savedValue = EEPROMReadlong(eepromDataAddr);

  unsigned long savedValue = 0UL;
  EEPROM_readAnything(eepromDataAddr, savedValue);

#ifdef _WEB_DEBUG
  eepromInitDataValue = savedValue;
#endif

  if (savedValue > 0)
  {
    averageTimeOpen = savedValue;
  }

  unsigned long savedValueToOpen = 0UL;
  EEPROM_readAnything(eepromDataOpenAddr, savedValueToOpen);

#ifdef _WEB_DEBUG
  eepromInitDataValue = savedValue;
#endif

  if (savedValueToOpen > 0)
  {
    averageTimeToOpen = savedValueToOpen;
  }

  //Disconnect();
  Connect();

  MDNS.begin(host);

  dht.begin();

  server.on("/", handle_root);
  server.on("/state", handle_openAlert);
  server.on("/ResetAverageOpen", handle_ResetAverageOpen);
  server.on("/move", handle_Move);

  server.begin();

  MDNS.addService("http", "tcp", 80);

  // OTA
  ArduinoOTA.setPort(OTAPport);
  ArduinoOTA.setHostname(host);
  ArduinoOTA.setPassword(OTAPass);
  ArduinoOTA.begin();

  Serial.println("ESP started");

  Serial.println("Sketch size: " + String(ESP.getSketchSize()));
  Serial.println("Free size: " + String(ESP.getFreeSketchSpace()));

  Serial.println("Initialized scheduler");
  runner.startNow();
}

void loop()
{
  runner.execute();

  server.handleClient();
  ArduinoOTA.handle();
}
