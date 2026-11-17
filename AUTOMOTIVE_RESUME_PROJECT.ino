#include <WiFi.h>
#include <WebServer.h>

// --- PINS (Using ADC1 Channel - Safe with Wi-Fi!) ---
#define PEDAL_SENSOR_PIN 34  // Orange wire connected to GPIO 34 (D34 / G34)

// --- FreeRTOS Task Handles ---
TaskHandle_t Core1_SensorTask;
TaskHandle_t Core0_ServerTask;

// --- Global Shared Variables (Protected by FreeRTOS Mutex) ---
portMUX_TYPE dataMutex = portMUX_INITIALIZER_UNLOCKED;
volatile float shared_pedalPercent = 0.0;
volatile int shared_engineRPM = 0;
volatile int shared_vehicleSpeed = 0;
String shared_ecuStatus = "SYSTEM_OK"; 

// --- Network Config ---
const char *ssid = "ESP32_OBD_Gateway";
const char *password = "diagnostic123";
WebServer server(80);

// --- Moving Average Filter variables ---
const int FILTER_SAMPLES = 10;
int sensorReadings[FILTER_SAMPLES];
int readIndex = 0;
long total = 0;

// --- Web Dashboard HTML Markup ---
const char HTML_DASHBOARD[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ECU Telematics Gateway</title>
    <style>
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; text-align: center; background: #0f172a; color: #f8fafc; margin: 0; padding: 20px; }
        .container { max-width: 600px; margin: auto; background: #1e293b; padding: 25px; border-radius: 15px; box-shadow: 0 10px 25px rgba(0,0,0,0.5); }
        h1 { color: #38bdf8; margin-bottom: 5px; }
        .sub { color: #94a3b8; font-size: 0.9em; margin-bottom: 25px; }
        .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-bottom: 20px; }
        .card { background: #0f172a; padding: 20px; border-radius: 10px; border: 1px solid #334155; }
        .label { font-size: 0.85em; color: #64748b; text-transform: uppercase; letter-spacing: 1px; }
        .value { font-size: 1.8em; font-weight: bold; margin-top: 5px; color: #f1f5f9; }
        .alert-ok { color: #4ade80; font-weight: bold; }
        .alert-fail { color: #f87171; font-weight: bold; animation: blink 1s infinite; }
        @keyframes blink { 0%, 100% { opacity: 1; } 50% { opacity: 0.4; } }
        .bar-container { background: #334155; border-radius: 8px; height: 20px; width: 100%; margin-top: 10px; overflow: hidden; }
        .bar { background: #38bdf8; height: 100%; width: 0%; transition: width 0.1s ease-out; }
    </style>
</head>
<body>
    <div class="container">
        <h1>📊 ECU Telematics Gateway</h1>
        <p class="sub">V2X / Telematics Node - Live OBD-II Diagnostic Stream</p>
        <div class="grid">
            <div class="card">
                <div class="label">Throttle Pedal</div>
                <div id="pedal" class="value">0.0%</div>
                <div class="bar-container"><div id="pedalBar" class="bar"></div></div>
            </div>
            <div class="card">
                <div class="label">Engine Speed</div>
                <div id="rpm" class="value">0 RPM</div>
            </div>
            <div class="card">
                <div class="label">Vehicle Speed</div>
                <div id="speed" class="value">0 km/h</div>
            </div>
            <div class="card">
                <div class="label">OBD Diagnostic State</div>
                <div id="status" class="alert-ok">SYSTEM_OK</div>
            </div>
        </div>
    </div>
    <script>
        setInterval(function() {
            fetch('/data')
            .then(response => response.json())
            .then(data => {
                document.getElementById("pedal").innerText = data.pedal.toFixed(1) + "%";
                document.getElementById("pedalBar").style.width = data.pedal + "%";
                document.getElementById("rpm").innerText = data.rpm + " RPM";
                document.getElementById("speed").innerText = data.speed + " km/h";
                
                let statusEl = document.getElementById("status");
                statusEl.innerText = data.status;
                if(data.status === "SYSTEM_OK") {
                    statusEl.className = "alert-ok";
                } else {
                    statusEl.className = "alert-fail";
                }
            });
        }, 150);
    </script>
</body>
</html>
)rawliteral";

// --- Sensor Processing (Core 1 - Safety Critical) ---
void SensorTask_Code(void *pvParameters) {
  for (;;) {
    int rawValue = analogRead(PEDAL_SENSOR_PIN);

    // 1. Digital Filter: Moving Average
    total = total - sensorReadings[readIndex];
    sensorReadings[readIndex] = rawValue;
    total = total + sensorReadings[readIndex];
    readIndex = (readIndex + 1) % FILTER_SAMPLES;
    float filteredValue = (float)total / FILTER_SAMPLES;

    // Convert raw 12-bit ADC (0 - 4095) to Throttle (0.0% to 100.0%)
    float pedalPercent = (filteredValue / 4095.0) * 100.0;
    if (pedalPercent < 0) pedalPercent = 0;
    if (pedalPercent > 100) pedalPercent = 100;

    // 2. Virtual Vehicle Engine Model
    int targetRPM = 800 + (pedalPercent * 62); // 800 RPM to 7000 RPM
    int targetSpeed = (pedalPercent * 1.8);    // 0 to 180 km/h

    // 3. Diagnostic Fault Evaluation
    String ecuState = "SYSTEM_OK";
    if (pedalPercent > 92.0) {
      ecuState = "OVER_SPEED_WARN";
    }

    // 4. Thread-Safe Mutex Lock
    portENTER_CRITICAL(&dataMutex);
    shared_pedalPercent = pedalPercent;
    shared_engineRPM = targetRPM;
    shared_vehicleSpeed = targetSpeed;
    shared_ecuStatus = ecuState;
    portEXIT_CRITICAL(&dataMutex);

    vTaskDelay(pdMS_TO_TICKS(15)); // 66Hz sampling frequency
  }
}

// --- Infotainment & Telematics Server (Core 0) ---
void ServerTask_Code(void *pvParameters) {
  WiFi.softAP(ssid, password);
  
  server.on("/", []() {
    server.send(200, "text/html", HTML_DASHBOARD);
  });

  server.on("/data", []() {
    float local_pedal;
    int local_rpm;
    int local_speed;
    String local_status;

    portENTER_CRITICAL(&dataMutex);
    local_pedal = shared_pedalPercent;
    local_rpm = shared_engineRPM;
    local_speed = shared_vehicleSpeed;
    local_status = shared_ecuStatus;
    portEXIT_CRITICAL(&dataMutex);

    String json = "{\"pedal\":" + String(local_pedal, 1) + 
                  ",\"rpm\":" + String(local_rpm) + 
                  ",\"speed\":" + String(local_speed) + 
                  ",\"status\":\"" + local_status + "\"}";
                  
    server.send(200, "application/json", json);
  });

  server.begin();

  for (;;) {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void setup() {
  // Configure GPIO 34 for analog input (0 - 3.3V)
  analogSetPinAttenuation(PEDAL_SENSOR_PIN, ADC_11db);

  for (int i = 0; i < FILTER_SAMPLES; i++) sensorReadings[i] = 0;

  // Pin Sensor Task to Core 1
  xTaskCreatePinnedToCore(
    SensorTask_Code,
    "SensorTask",
    4096,
    NULL,
    2,
    &Core1_SensorTask,
    1
  );

  // Pin Server Task to Core 0
  xTaskCreatePinnedToCore(
    ServerTask_Code,
    "ServerTask",
    8192,
    NULL,
    1,
    &Core0_ServerTask,
    0
  );
}

void loop() {
  vTaskDelete(NULL); 
}
