#include <WiFi.h>
#include <ESPAsyncWebServer.h>
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

// L298N Motor config
#define L298N_IN1 26
#define L298N_IN2 27
#define L298N_ENA 25
const int motorSpeed = 255; // PWM (0-255)

// IR Sensor
#define IR_SENSOR_PIN 33

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
unsigned long feedDuration = 2000;

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
    @media (max-width: 540px) {
      .container { margin: 0; border-radius: 0; box-shadow: none; }
    }
  </style>
</head>
<body>
  <div class="container">
    <h2>Automated Cat Feeder</h2>
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
    setInterval(loadStatus, 5000);
  </script>
</body>
</html>
)rawliteral";

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

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  Serial.print("WiFi connected: "); Serial.println(WiFi.localIP());

  // Serve web GUI
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
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

  // API: get feeding duration
  server.on("/api/duration", HTTP_GET, [](AsyncWebServerRequest *request){
    DynamicJsonDocument doc(32);
    doc["duration"] = feedDuration;
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // API: set feeding duration
  server.on("/api/duration", HTTP_POST, [](AsyncWebServerRequest *request){
    String body;
    if (request->hasParam("body", true)) {
      body = request->getParam("body", true)->value();
      DynamicJsonDocument doc(32);
      deserializeJson(doc, body);
      feedDuration = doc["duration"];
    }
    request->send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server.begin();
}

// DC motor run routine
void runFeeder() {
  // Activate motor (L298N, forward direction)
  digitalWrite(L298N_IN1, HIGH);
  digitalWrite(L298N_IN2, LOW);
  ledcAttachPin(L298N_ENA, 0); // channel 0
  ledcWrite(0, motorSpeed);
  delay(feedDuration);
  // Stop motor
  ledcWrite(0, 0);
  digitalWrite(L298N_IN1, LOW);
  digitalWrite(L298N_IN2, LOW);
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
