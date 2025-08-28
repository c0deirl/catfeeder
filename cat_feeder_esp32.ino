#include <WiFi.h>
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <time.h>
#include <Wire.h>
//#include <ESP_Mail_Client.h> // https://github.com/mobizt/ESP-Mail-Client
#include "esp32-hal-ledc.h" // Add this line for PWM functions
#include <WiFiClientSecure.h>


// Email Includes
#define ENABLE_SMTP
#define ENABLE_DEBUG
#define READYMAIL_DEBUG_PORT Serial
#include <ReadyMail.h>

// WIFI credentials
const char* ssid = "N8MDG";
const char* password = "mattg123";

// SMTP config
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465
#define AUTHOR_EMAIL "greathouse.matthew@gmail.com"
#define AUTHOR_PASSWORD "yofw koiw lfvt kvvv"
#define RECIPIENT_EMAIL "catfeed@stpaulwv.org"

// Email Setup
WiFiClientSecure client;
SMTPClient smtp(client);

// L298N Motor config

const int motorSpeed = 255; // PWM (0-255)

#define MOTOR_PIN1 26  // H-Bridge input 1
#define MOTOR_PIN2 27  // H-Bridge input 2
#define MOTOR_EN 25   // H-Bridge enable pin

// PWM configuration
#define PWM_CHANNEL 0
#define PWM_FREQ 5000
#define PWM_RESOLUTION 8

// Global variables
int speedValue = 255;
//int lastSpeedValue = 0;

// IR Sensor
#define IR_SENSOR_PIN 33

//NTFY Topic
const char* ntfy_topic_url = "https://notify.codemov.com/cat";

// Feeding schedule
struct FeedTime {
  int hour;
  int minute;
  bool enabled;
};
FeedTime schedule[3] = {
  {8,  0, true},
  {13, 0, true},
  {19, 0, true}
};

// Feeding duration (ms)
unsigned long feedDuration = 10000;

//Email SMTP Status Check
void smtpStatus(SMTPStatus status) {
  Serial.println(status.text);
  //writeLog(getCurrentTimeStr() + status.text);
}

// State
AsyncWebServer server(80);
bool manualFeed = false;
unsigned long lastFeedMillis = 0;
bool storageEmpty = false;

// Web GUI HTML
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Cat Feeder Control</title>
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <link href="https://fonts.googleapis.com/css?family=Roboto:400,700&display=swap" rel="stylesheet">
  <style>
    body { font-family: 'Roboto', sans-serif; background: #f2f5f7; margin: 0; }
    .container { max-width: 480px; margin: 40px auto; background: #fff; border-radius: 12px; box-shadow: 0 3px 16px rgba(0,0,0,0.12); padding: 2em; }
    h2 { color: #27496d; text-align: center; }
    .status { margin-bottom: 1em; text-align: center; }
    .empty { color: #e74c3c; font-weight: bold; }
    .full { color: #27ae60; font-weight: bold; }
    .schedule { margin-bottom: 2em; }
    .sched-row { display: flex; align-items: center; margin: 0.5em 0; }
    .sched-row input[type="time"] { flex: 1; margin-right: 0.5em; }
    .sched-row label { margin-right: 0.5em; }
    .sched-row input[type="checkbox"] { margin-right: 0.5em; }
    button { background: #27496d; color: #fff; border: none; border-radius: 6px; padding: 0.7em 1.5em; font-size: 1em; cursor: pointer; margin-top: 1em; width: 100%; }
    button:active { background: #142850; }
    .manual { background: #e67e22; }
    .manual:active { background: #d35400; }
    .duration { margin-top: 1em; }
    .duration label { margin-right: 0.5em; }
    .time-display { text-align: center; font-size: 1.2em; color: #27496d; margin-bottom: 1em; }
    @media (max-width: 540px) {
      .container { margin: 0; border-radius: 0; box-shadow: none; }
    }
  </style>
</head>
<body>
  <div class="container">
    <h2>Taco & Leggy Cat Feeder</h2>
    <div class="time-display" id="current-time">Loading time...</div>
    <div class="status" id="status">Checking storage...</div>
    <div class="schedule">
      <h3>Feeding Schedule</h3>
      <form id="sched-form">
        <div class="sched-row">
          <label>Feed 1:</label>
          <input type="time" id="feed1-time">
          <input type="checkbox" id="feed1-enable">
        </div>
        <div class="sched-row">
          <label>Feed 2:</label>
          <input type="time" id="feed2-time">
          <input type="checkbox" id="feed2-enable">
        </div>
        <div class="sched-row">
          <label>Feed 3:</label>
          <input type="time" id="feed3-time">
          <input type="checkbox" id="feed3-enable">
        </div>
        <button type="submit">Save Schedule</button>
      </form>
    </div>
    <div class="duration">
      <label for="duration">Feeding Duration (sec):</label>
      <input type="number" id="duration" min="1" max="20" value="2">
      <button id="duration-btn">Set Duration</button>
    </div>
    <button class="manual" id="manual-btn">Feed Now</button>
  </div>
  <script>
    function loadStatus() {
      fetch('/api/status').then(r=>r.json()).then(status=>{
        document.getElementById('status').innerHTML = status.empty
          ? '<span class="empty">Storage Empty!</span>'
          : '<span class="full">Food Available</span>';
      });
    }
    function loadSchedule() {
      fetch('/api/schedule').then(r=>r.json()).then(data=>{
        for (let i=0; i<3; i++) {
          let time = `${data[i].hour.toString().padStart(2,'0')}:${data[i].minute.toString().padStart(2,'0')}`;
          document.getElementById('feed'+(i+1)+'-time').value = time;
          document.getElementById('feed'+(i+1)+'-enable').checked = data[i].enabled;
        }
      });
      fetch('/api/duration').then(r=>r.json()).then(data=>{
        document.getElementById('duration').value = Math.round(data.duration/1000);
      });
    }
    function loadCurrentTime() {
      fetch('/api/time').then(r=>r.json()).then(data=>{
        document.getElementById('current-time').textContent = data.time;
      });
    }
    document.getElementById('sched-form').onsubmit = function(e) {
      e.preventDefault();
      let sched = [];
      for (let i=0; i<3; i++) {
        let time = document.getElementById('feed'+(i+1)+'-time').value.split(":");
        sched.push({
          hour: parseInt(time[0]), minute: parseInt(time[1]),
          enabled: document.getElementById('feed'+(i+1)+'-enable').checked
        });
      }
      fetch('/api/schedule', {
        method: 'POST',
        headers: {'Content-Type':'application/json'},
        body: JSON.stringify(sched)
      }).then(()=>alert('Schedule saved!'));
    };
    document.getElementById('manual-btn').onclick = function() {
      fetch('/api/feed', {method:'POST'})
        .then(()=>alert('Feeding triggered!'));
    };
    document.getElementById('duration-btn').onclick = function(e) {
      e.preventDefault();
      let duration = parseInt(document.getElementById('duration').value) * 1000;
      fetch('/api/duration', {
        method: 'POST',
        headers: {'Content-Type':'application/json'},
        body: JSON.stringify({duration: duration})
      }).then(()=>alert('Feeding duration set!'));
    };
    loadStatus();
    loadSchedule();
    loadCurrentTime();
    setInterval(loadStatus, 5000);
    setInterval(loadCurrentTime, 1000);
  </script>
</body>
</html>
)rawliteral";

// Email setup

void setup() {
  Serial.begin(115200);
  pinMode(IR_SENSOR_PIN, INPUT);

// Initialize motor pins
  pinMode(MOTOR_PIN1, OUTPUT);
  pinMode(MOTOR_PIN2, OUTPUT);
  pinMode(MOTOR_EN, OUTPUT);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  Serial.print("WiFi connected: "); Serial.println(WiFi.localIP());

  // NTP time sync
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  while (time(nullptr) < 100000) {  // Wait for time to be set
    delay(500);
    Serial.print(".");
  }
  Serial.println("Time synchronized!");

  // Serve web GUI
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // API: get current time
  server.on("/api/time", HTTP_GET, [](AsyncWebServerRequest *request){
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    char buf[32];
    sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d", 
            t->tm_year+1900, t->tm_mon+1, t->tm_mday, 
            t->tm_hour, t->tm_min, t->tm_sec);
    DynamicJsonDocument doc(64);
    doc["time"] = String(buf);
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

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

  // API: set schedule (fixed for body parsing)
  server.on("/api/schedule", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      String body;
      for (size_t i = 0; i < len; i++) body += (char)data[i];
      DynamicJsonDocument doc(256);
      deserializeJson(doc, body);
      for (int i=0; i<3; i++) {
        schedule[i].hour = doc[i]["hour"];
        schedule[i].minute = doc[i]["minute"];
        schedule[i].enabled = doc[i]["enabled"];
      }
      request->send(200, "application/json", "{\"status\":\"ok\"}");
    }
  );

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

  // API: get feeding duration
  server.on("/api/duration", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(32);
    doc["duration"] = feedDuration;
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // API: set feeding duration (fixed for body parsing)
  server.on("/api/duration", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      String body;
      for (size_t i = 0; i < len; i++) body += (char)data[i];
      DynamicJsonDocument doc(32);
      deserializeJson(doc, body);
      feedDuration = doc["duration"];
      request->send(200, "application/json", "{\"status\":\"ok\"}");
    }
  );
  server.begin();
}

void sendNotification(String body) {
  HTTPClient http;
  http.begin(ntfy_topic_url);
  http.addHeader("Content-Type", "text/plain");
  http.addHeader("X-Actions", "view, Cat Feeder, https://cat.codemov.com");
  int httpResponseCode = http.POST(body);
  http.end();
}

// DC motor run routine
void runFeeder() {
    digitalWrite(MOTOR_PIN1, LOW);
    digitalWrite(MOTOR_PIN2, HIGH);
    analogWrite(MOTOR_EN, speedValue);  // Using analogWrite instead of ledcWrite
    delay(feedDuration);
    digitalWrite(MOTOR_PIN1, LOW);
    digitalWrite(MOTOR_PIN2, LOW);
    analogWrite(MOTOR_EN, 0);
    sendNotification("Cat Feeder Alert : Scheduled Feeding - Cat food dispensed at scheduled time.");
}


// Email sending handler. Must be called with a value for emailbody, otherwise the email will be blank.
void sendemail(String emailbody) {
   client.setInsecure(); 
  configTime(-14400, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Waiting for NTP time...");
  while (time(nullptr) < 100000) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println();
  smtp.connect(SMTP_HOST, SMTP_PORT, smtpStatus, true);
  if (!smtp.isConnected()) return;

  smtp.authenticate(AUTHOR_EMAIL, AUTHOR_PASSWORD, readymail_auth_password);
  if (!smtp.isAuthenticated()) return;

  SMTPMessage msg;
  msg.headers.add(rfc822_from, AUTHOR_EMAIL);
  msg.headers.add(rfc822_to, RECIPIENT_EMAIL);
  msg.headers.add(rfc822_subject, " Cat Feeder ");
  msg.headers.addCustom("X-Priority", "1");
  msg.headers.addCustom("Importance", "High");

// The emailbody value can be passed in from another function. This can be manually set if needed, but is universal across notifications if set here.
  String body = emailbody;
  msg.html.body(body);
  msg.timestamp = time(nullptr);
  smtp.send(msg, "SUCCESS,FAILURE");
}

void loop() {
  // IR sensor check
  int ir = digitalRead(IR_SENSOR_PIN);
  if (ir == LOW && !storageEmpty) {
    storageEmpty = true;
      sendNotification("Cat Feeder Alert : Storage Empty - The cat feeder is empty. Please refill.");
      sendemail("Cat Feeder Alert: Storage Empty - The Cat feeder is empty, Please Refill");
  } else if (ir == HIGH && storageEmpty) {
    storageEmpty = false;
      sendNotification("Cat Feeder Alert : Storage Refilled - The cat feeder has been refilled.");
  }

  // Feeding schedule
  time_t now = time(nullptr);
  struct tm *t = localtime(&now);
  for (int i=0; i<3; i++) {
    if (schedule[i].enabled && t->tm_hour==schedule[i].hour && t->tm_min==schedule[i].minute && millis()-lastFeedMillis>60000) {
      runFeeder();
    }
  }

  // Manual feed
  if (manualFeed && millis()-lastFeedMillis>60000) {
    runFeeder();
      sendNotification("Cat Feeder Alert : Manual Feed - Cat food dispensed manually.");
    manualFeed = false;
  }

  delay(1000);
}
