#include <SPI.h>
#include <WiFiNINA.h>
#include <DHT.h>
#include <Servo.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "secrets.h"

// DHT-sensor
#define DHTPIN 2
#define DHTTYPE DHT11

// OLED-näyttö
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// Objektit
DHT dht(DHTPIN, DHTTYPE);
Servo valveServo;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int servoPin = 3;
String lastValveStatus = "unknown";

// Raja-arvot
float tempMin = 0;
float tempMax = 0;

// -------------------------------------------------------------
// Lue venttiilin tila palvelimelta JSON-vastauksesta
//
// readValveStatus vähän huono funktionimi, koska sillä haetaan
// valven lisäksi myös tempit
// -------------------------------------------------------------
String readValveStatus(WiFiSSLClient &client) {
  String response = "";

  while (client.available()) {
    response += client.readStringUntil('\n') + "\n";
  }

  client.stop();
  Serial.println(response);

  int jsonStart = response.indexOf('{');
  if (jsonStart == -1) return "";

  String json = response.substring(jsonStart);

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return "";
  }

  const char* valve = doc["valve"];
  tempMin = doc["tempMin"];
  tempMax = doc["tempMax"];
  return valve ? String(valve) : "";
}

// -------------------------------------------------------------
// Näytä lämpö ja venttiilin tila OLED-näytöllä
// -------------------------------------------------------------
void showOnOLED(float temperature, String valveStatus) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 10);
  display.print(F("Temp: "));
  display.print(temperature);
  display.println(F(" C"));

  display.setCursor(0, 20);
  display.print(F("Valve: "));
  display.println(valveStatus);

  display.setCursor(0, 30);
  display.print(F("Min temp: "));
  display.println(tempMin);

  display.setCursor(0, 40);
  display.print(F("Max temp: "));
  display.println(tempMax);

  display.display();
}

// -------------------------------------------------------------
// Lähetä lämpötila HTTPS ja ohjaa servoa vastauksen mukaan
// -------------------------------------------------------------
void sendTemperatureHTTPS(float temperature) {
  const char* server = "termak-iot.azurewebsites.net";
  const int port = 443;

  WiFiSSLClient client;

  Serial.print(F("Connecting to server... "));
  if (!client.connect(server, port)) {
    Serial.println(F("Connection failed"));
    return;
  }
  Serial.println(F("Connected"));

  String postData = "temperature=" + String(temperature);

  client.println("POST /temperature.php HTTP/1.1");
  client.println("Host: termak-iot.azurewebsites.net");
  client.println("Content-Type: application/x-www-form-urlencoded");
  client.print("Content-Length: ");
  client.println(postData.length());
  client.println("Connection: close");
  client.println();
  client.println(postData);

  delay(1000);

  String valveStatus = readValveStatus(client);

  if (valveStatus == "open") {
    valveServo.write(90);
    Serial.println(F("Valve opened"));
  } else if (valveStatus == "close") {
    valveServo.write(0);
    Serial.println(F("Valve closed"));
  } else {
    Serial.println(F("Valve_status not found"));
  }

  lastValveStatus = valveStatus;
  showOnOLED(temperature, valveStatus);
}

// -------------------------------------------------------------
// SETUP
// -------------------------------------------------------------
void setup() {
  valveServo.attach(servoPin);
  Serial.begin(9600);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 init failed"));
    for (;;);
  }

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println(F("Connecting..."));
  }
  Serial.println(F("Connected"));

  dht.begin();
}

// -------------------------------------------------------------
// LOOP
// -------------------------------------------------------------
void loop() {
  delay(10000);

  float temp = dht.readTemperature();
  if (isnan(temp)) {
    Serial.println(F("DHT read failure"));
    showOnOLED(0, "Sensor error");
    return;
  }

  Serial.println(temp);
  sendTemperatureHTTPS(temp);
}
