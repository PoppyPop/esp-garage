#include <Arduino.h>

#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DHT.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>

/*
   python /Users/steve/Library/Arduino15/packages/esp8266/hardware/esp8266/2.2.0/tools/espota.py -i 192.168.0.48 -p 8266 --auth=EAdYGW81CskIXGzSYJBzdj5eeeUVdXT1JNeZj6GLeZ -f /var/folders/1v/srr92bkd24q3d5w_wx17hymr0000gn/T/build315f8e6936860c20a295485da5909e09.tmp/garage.ino.bin -d -r
*/

#define ledPin 4 // LED connected to digital pin 13
#define doorPin 5   // pushbutton connected to digital pin 7
bool lastClosed = true;

// Out
#define alertPin 13
#define openPin 12

// Temp Humidity
#define DHTPIN 14
#define DHTTYPE DHT22
#define DHTCYCLE 11
#define waitSeconds 300 // 5min

DHT dht(DHTPIN, DHTTYPE, DHTCYCLE);

float humidity, temp_f, hic;  // Values read from sensor

// Web Server
#define ssid "MootCity-2G"
#define password "EAdYGW81CskIXGzSYJBzdj5eeeUVdXT1JNeZj6GLeZ"
#define timeoutWifi 30

ESP8266WebServer server(80);

// OTA
#define OTAPport 8266
#define OTAPass "EAdYGW81CskIXGzSYJBzdj5eeeUVdXT1JNeZj6GLeZ"

#define host "ESP-Garage"

void handle_root() {

  //unsigned long current = millis() ;

  String webString = "<html><head><title>Garage ESP</title></head><body>";   // String to display

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

  // Door
  webString += "<br /> ";
  webString += "Door: ";
  webString += lastClosed ? "Closed" : "Open";

    // Open
  /*webString += "<br /> ";
  webString += "Last Open: ";
  webString += String(lastDistance) + " cm";*/

  // Alert
  /*webString += "<br /> ";
  webString += "Last Alert: ";
  webString += String(lastDistance) + " cm";*/

  webString += "</body></html>";

  server.send(200, "text/html", webString);
  delay(100);
}

void Connect() {
  if (WiFi.status() != WL_CONNECTED) {
    // Disable the AP
    WiFi.mode(WIFI_STA);
    WiFi.hostname(host);
    // Connect to WiFi network
    WiFi.begin(ssid, password);
    Serial.print("\n\r \n\rWorking to connect");

    // Wait for connection
    int curtime = 0;
    int curDelay = 500;
    while (WiFi.status() != WL_CONNECTED && curtime < (timeoutWifi * 1000)) {
      digitalWrite(ledPin, HIGH);
      delay(curDelay / 2);
      digitalWrite(ledPin, LOW);
      delay(curDelay / 2);
      curtime += curDelay;
      Serial.print(".");
    }

    digitalWrite(ledPin, LOW);

    Serial.println("");
    Serial.print("State: ");
    Serial.println(WiFi.status());
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }
}

void setup()
{
  Serial.begin (115200);
  pinMode(alertPin, OUTPUT);
  digitalWrite(alertPin, LOW);

  pinMode(openPin, OUTPUT);
  digitalWrite(openPin, LOW);

  pinMode(ledPin, OUTPUT);      // sets the digital pin 5 as output
  digitalWrite(ledPin, LOW);

  pinMode(doorPin, INPUT);      // sets the digital pin 7 as input

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

  Serial.println("Pio Ready !");

  Serial.println("Sketch size: " + String(ESP.getSketchSize()));
  Serial.println("Free size: " + String(ESP.getFreeSketchSpace()));
}

void Disconnect() {
  WiFi.mode(WIFI_OFF);
}

void ReadSerial() {
  String readString = "";

  while (Serial.available()) {
    char c = Serial.read();  //gets one byte from serial buffer
    readString += c; //makes the String readString
    delay(2);  //slow looping to allow buffer to fill with next character
  }

  if (readString.length() > 0) {
    readString.toUpperCase();
    if (readString == "IP")
    {
      Serial.println(WiFi.localIP());
    } else if (readString == "CONNECT") {
      Connect();
    }

    readString = "";
  }
}

unsigned long lastClosedRead = 0;
bool isClosed() {
  // Wait a few seconds between measurements.
  // 1 sec.
  unsigned long current = millis() ;
  bool isClosed = lastClosed;

  if (current - lastClosedRead > 1000) {
    lastClosedRead = current;

    bool doorClosed = digitalRead(doorPin);   // read the input pin

    isClosed = (doorClosed && lastClosed);

    lastClosed = doorClosed;
  }

  return isClosed;
}

unsigned long lasthandleClosedRead = 0;
unsigned long lastOpen = 0;
unsigned long lastAlert = 0;
unsigned int retryCounter = 0; 

#define waitHandle 300 // 5min
#define waitPostPone 60 // 1min

void handleClosed(bool closed) {
  // Wait a few seconds between measurements.
  // 1 sec.
  unsigned long current = millis();

  unsigned long waitTime = closed ? 1000 : 500;

  if (current - lasthandleClosedRead > waitTime) {

    digitalWrite(openPin, closed ? LOW : HIGH);

    if (!closed) 
    {
      if (lastOpen == 0) 
      {
        lastOpen = current;
      } 
      else if (current - lastOpen > (waitHandle * 1000))  
      {
        if (lastAlert == 0 && retryCounter < 5)
        {
          digitalWrite(alertPin, HIGH);
          lastAlert = current;
          retryCounter++;

          digitalWrite(ledPin, HIGH);
        } 
        else if (current - lastAlert > 500) 
        {
          digitalWrite(ledPin, LOW);

          // Alert for 500ms
          digitalWrite(alertPin, LOW);
          lastAlert = 0;

          // postpone next alert in 1min
          lastOpen = lastOpen + (waitPostPone * 1000);
        } 
        else if (retryCounter >= 6) 
        {
          // send alert
          for (int count = 0; count<3; count++)
          {
            digitalWrite(ledPin, HIGH);
            delay(250);
            digitalWrite(ledPin, LOW);
            delay(250);
          }
        }
      }
    } else {
      lastOpen = 0;
      retryCounter = 0;
    }
  }
}

void sendDatas() {

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
  } else {
    Serial.println("Send KO: " + String(result));
  }


  http.end();
}

unsigned long lastDHTRead = 10000;
void readtemp(bool closed) {
  if (closed) {
    // Wait a few seconds between measurements.
    // 10sec.
    unsigned long current = millis();
    if (current - lastDHTRead > (waitSeconds * 1000)) {
      lastDHTRead = current;

      // Reading temperature or humidity takes about 250 milliseconds!
      // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
      humidity = dht.readHumidity();
      // leave time between 2 readings
      delay(10);
      // Read temperature as Celsius (the default)
      temp_f = dht.readTemperature();

      // Check if any reads failed and exit early (to try again).
      if (isnan(humidity) || isnan(temp_f)) {
        Serial.println("Failed to read from DHT sensor!");
        // lastDHTRead = -1000;
        return;
      }

      // Compute heat index in Celsius (isFahreheit = false)
      hic = dht.computeHeatIndex(temp_f, humidity, false);

      sendDatas();
    }
  }
}

void loop()
{
  bool closed = isClosed();
  // Disabled
  // readDistance(closed);
  handleClosed(closed);

  readtemp(closed);

  ReadSerial();

  server.handleClient();
  ArduinoOTA.handle();
}



