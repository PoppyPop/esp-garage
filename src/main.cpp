#include <Arduino.h>

#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DHT.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <TaskScheduler.h>
#include "Inputs.h"

/*
   python /Users/steve/Library/Arduino15/packages/esp8266/hardware/esp8266/2.2.0/tools/espota.py -i 192.168.0.48 -p 8266 --auth=EAdYGW81CskIXGzSYJBzdj5eeeUVdXT1JNeZj6GLeZ -f /var/folders/1v/srr92bkd24q3d5w_wx17hymr0000gn/T/build315f8e6936860c20a295485da5909e09.tmp/garage.ino.bin -d -r
*/

#define _GARAGE_DEBUG 1

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

// Wait door
#define DOORWAIT 10 // 300:5min
#define ALERTWAIT 5 // 60:1min

Scheduler runner;

void handleClosed();
void readtemp();
void notClosed();
void infinite();
void resetInfinite();
void closed();

//Tasks
Task handleClosedTask(1000, TASK_FOREVER, &handleClosed, &runner, true);
Task tempTask(DHTWAIT * 1000, TASK_FOREVER, &readtemp, &runner, true); // 5Min

Task infiniteTask(250, TASK_FOREVER, &infinite, &runner, true); // 1sec

Task closedTask(4000, TASK_ONCE, &closed, &runner, false); // 4sec.

Task notClosedTask(0, 5, &notClosed, &runner, false); // 5Min

bool alertEnable();
void alertOn();
void alertOff();
Task alertWrapperTask(5000, TASK_ONCE, NULL, &runner, false, &alertEnable);
Task alertTask(0, TASK_FOREVER, NULL, &runner, false);

Inputs _inputs;
bool _neverGiveUp = false;
bool _infinite = false;

DHT dht(DHTPIN, DHTTYPE, DHTCYCLE);

float humidity, temp_f, hic; // Values read from sensor

// Web Server
#define ssid "MootCity-2G"
#define password "EAdYGW81CskIXGzSYJBzdj5eeeUVdXT1JNeZj6GLeZ"
#define timeoutWifi 30

ESP8266WebServer server(80);

// OTA
#define OTAPport 8266
#define OTAPass "EAdYGW81CskIXGzSYJBzdj5eeeUVdXT1JNeZj6GLeZ"

#define host "ESP-Garage"

void handle_root()
{

  //unsigned long current = millis() ;

  String webString = "<html><head><title>Garage ESP</title></head><body>"; // String to display

  // Wifi
  webString += "<br /> ";
  webString += "SSID: ";
  webString += WiFi.SSID();
  webString += " RSSI: ";
  webString += WiFi.RSSI();

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

  webString += "</body></html>";

  server.send(200, "text/html", webString);
  delay(100);
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

void ReadSerial()
{
  String readString = "";

  while (Serial.available())
  {
    char c = Serial.read(); //gets one byte from serial buffer
    readString += c;        //makes the String readString
    delay(2);               //slow looping to allow buffer to fill with next character
  }

  if (readString.length() > 0)
  {
    readString.toUpperCase();
    if (readString == "IP")
    {
      Serial.println(WiFi.localIP());
    }
    else if (readString == "CONNECT")
    {
      Connect();
    }

    readString = "";
  }
}

void ReadInputs()
{

  bool tmpOpen = !digitalRead(openPin); // True when pull down
#ifdef _GARAGE_DEBUG
  if (tmpOpen != _inputs.Open)
  {
    Serial.println((std::string("Open: ") + (tmpOpen ? "NO" : "YES")).c_str());
  }
#endif
  _inputs.Open = tmpOpen;

  bool tmpClosed = digitalRead(closePin);
#ifdef _GARAGE_DEBUG
  if (tmpClosed != _inputs.Closed)
  {
    Serial.println((std::string("Closed: ") + (tmpClosed ? "NO" : "YES")).c_str());
  }
#endif
  _inputs.Closed = tmpClosed;

  bool tmpInfinite = !digitalRead(infinitePin); // True when pull down
#ifdef _GARAGE_DEBUG
  if (tmpInfinite != _inputs.Infinite)
  {
    Serial.println((std::string("Infinite: ") + (tmpInfinite ? "NO" : "YES")).c_str());
  }
#endif
  _inputs.Infinite = tmpInfinite;
}

void handleClosed()
{
  bool closed = _inputs.Closed;
  bool open = _inputs.Open;

  unsigned long waitTime = (closed || _infinite) ? 1000 : 500;
  handleClosedTask.setInterval(waitTime);

  digitalWrite(statusPin, closed ? LOW : HIGH);

  if (!_infinite)
  {
    if (!closed && !notClosedTask.isEnabled())
    {
      notClosedTask.setInterval(DOORWAIT * 1000);
      notClosedTask.restartDelayed();
    }
    else if (closed)
    {
      closedTask.restartDelayed();
    }
  }

  if (!open)
  {
    // reset infinite if not open
    resetInfinite();
  }

  //     else if (retryCounter >= 6)
  //     {
  //       // send alert
  //       for (int count = 0; count < 3; count++)
  //       {
  //         digitalWrite(ledPin, HIGH);
  //         delay(250);
  //         digitalWrite(ledPin, LOW);
  //         delay(250);
  //       }
  //     }
}

void closed()
{
  notClosedTask.disable();
  _neverGiveUp = false;
}

void notClosed()
{
#ifdef _GARAGE_DEBUG
  Serial.println("notClosed()");
#endif
  // The door has not been closed correctly
  if (!_inputs.Closed)
  {
    if (notClosedTask.isLastIteration() && !_inputs.Open)
    {
// Unable to close it, let it go
#ifdef _GARAGE_DEBUG
      Serial.println("Never give Up!");
#endif
      _neverGiveUp = true;
      return;
    }
    else
    {
      if (notClosedTask.isFirstIteration())
      {
#ifdef _GARAGE_DEBUG
        Serial.println("notClosed() - ALERTWAIT");
#endif
        notClosedTask.setInterval(ALERTWAIT * 1000);
      }

#ifdef _GARAGE_DEBUG
      Serial.println("alertWrapperTask.enable()");
#endif
      alertWrapperTask.enable();
    }
  }
}

bool alertEnable()
{
#ifdef _GARAGE_DEBUG
  Serial.println("alertEnable()");
#endif

  alertTask.setInterval(500);
  alertTask.setCallback(&alertOn);
  alertTask.enable();

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
  alertTask.disable();
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
    }
  }
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

  //Disconnect();
  Connect();

  MDNS.begin(host);

  dht.begin();

  server.on("/", handle_root);

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
  ReadInputs();

  ReadSerial();

  server.handleClient();
  ArduinoOTA.handle();

  runner.execute();
}
