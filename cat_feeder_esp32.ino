#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncTCP.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include <ESP_Mail_Client.h> // https://github.com/mobizt/ESP-Mail-Client

// WIFI credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// SMTP config
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465
#define AUTHOR_EMAIL "your_email@gmail.com"
#define AUTHOR_PASSWORD "your_app_password"
#define RECIPIENT_EMAIL "recipient_email@gmail.com"

// Servo and Motor config
#define SERVO_PIN 18
#define L298N_IN1 26
#define L298N_IN2 27
#define L298N_ENA 25

Servo myservo;
int servoOpen = 120; // open angle
int servoClose = 30; // close angle
unsigned long feedDuration = 2000; // ms

// IR Sensor
#define IR_SENSOR_PIN 33

// Feeding schedule
struct FeedTime {
  int hour;
  int minute;
  bool enabled;
};

FeedTime schedule[3] = {
  {8, 0, true},   // 8:00
  {13, 0, true},  // 13:00
  {19, 0, true}   // 19:00
};

// State
AsyncWebServer server(80);
bool manualFeed = false;
unsigned long lastFeedMillis = 0;
bool storageEmpty = false;

// Email setup
SMTPSession smtp;
void sendEmail(const char* subject, const char* body) {
  SMTP_Message message;
  message.sender.name = "Cat Feeder";
  message.sender.email = AUTHOR_EMAIL;
  message.subject = subject;
  message.addRecipient("Owner", RECIPIENT_EMAIL);
  message.text.content = body;
  message.text.charSet = "us-ascii";
  smtp.callback([](SMTP_Status status){
    Serial.println(status.info());
  });
  smtp.connect(SMTP_HOST, SMTP_PORT);
  smtp.login(AUTHOR_EMAIL, AUTHOR_PASSWORD);
  smtp.sendMail(message);
  smtp.closeSession();
}

void setup() {
  Serial.begin(115200);
  pinMode(IR_SENSOR_PIN, INPUT);
  pinMode(L298N_IN1, OUTPUT);
  pinMode(L298N_IN2, OUTPUT);
  pinMode(L298N_ENA, OUTPUT);
  myservo.attach(SERVO_PIN);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  Serial.print("WiFi connected: "); Serial.println(WiFi.localIP());

  // Serve web GUI
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html");
  });
  server.serveStatic("/", SPIFFS, "/");

  // API: get schedule
  server.on("/api/schedule", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(256);
    for (int i=0; i<3; i++) {
      JsonObject item = doc.createNestedObject();
      item["hour"] = schedule[i].hour;
      item["minute"] = schedule[i].minute;
      item["enabled"] = schedule[i].enabled;
    }
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // API: set schedule
  server.on("/api/schedule", HTTP_POST, [](AsyncWebServerRequest *request){
    String body;
    if (request->hasParam("body", true)) {
      body = request->getParam("body", true)->value();
      DynamicJsonDocument doc(256);
      deserializeJson(doc, body);
      for (int i=0; i<3; i++) {
        schedule[i].hour = doc[i]["hour"];
        schedule[i].minute = doc[i]["minute"];
        schedule[i].enabled = doc[i]["enabled"];
      }
    }
    request->send(200, "application/json", "{\"status\":\"ok\"}");
  });

  // API: manual feed
  server.on("/api/feed", HTTP_POST, [](AsyncWebServerRequest *request){
    manualFeed = true;
    request->send(200, "application/json", "{\"status\":\"feeding\"}");
  });

  // API: get status
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(128);
    doc["empty"] = storageEmpty;
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  SPIFFS.begin();
  server.begin();
}

void runFeeder() {
  // Activate motor (L298N)
  digitalWrite(L298N_IN1, HIGH);
  digitalWrite(L298N_IN2, LOW);
  analogWrite(L298N_ENA, 255);
  // Open servo
  myservo.write(servoOpen);
  delay(feedDuration);
  // Close servo
  myservo.write(servoClose);
  digitalWrite(L298N_IN1, LOW);
  digitalWrite(L298N_IN2, LOW);
  analogWrite(L298N_ENA, 0);
  lastFeedMillis = millis();
}

void loop() {
  // IR sensor check
  int ir = digitalRead(IR_SENSOR_PIN);
  if (ir == LOW && !storageEmpty) {
    storageEmpty = true;
    sendEmail("Cat Feeder Alert: Storage Empty", "The cat feeder is empty. Please refill.");
  } else if (ir == HIGH && storageEmpty) {
    storageEmpty = false;
    sendEmail("Cat Feeder Info: Storage Refilled", "Cat feeder refilled.");
  }

  // Feeding schedule
  time_t now = time(nullptr);
  struct tm *t = localtime(&now);
  for (int i=0; i<3; i++) {
    if (schedule[i].enabled && t->tm_hour==schedule[i].hour && t->tm_min==schedule[i].minute && millis()-lastFeedMillis>60000) {
      runFeeder();
      sendEmail("Cat Feeder: Scheduled Feeding Triggered", "Cat food dispensed at scheduled time.");
    }
  }

  // Manual feed
  if (manualFeed && millis()-lastFeedMillis>60000) {
    runFeeder();
    sendEmail("Cat Feeder: Manual Feed Triggered", "Cat food dispensed manually.");
    manualFeed = false;
  }

  delay(1000);
}