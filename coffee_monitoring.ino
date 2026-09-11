#define BLYNK_TEMPLATE_ID "TMPL2t4QyShVU"
#define BLYNK_TEMPLATE_NAME "Coffee Dryer Monitoring System"
#define BLYNK_AUTH_TOKEN "iT-QY1pYSOJxHhGqHteznxzAZrXFkc8A"
#define BLYNK_PRINT Serial

#include <Wire.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include "rgb_lcd.h"

// ---------------- PIN MAP ----------------
#define LM35_PIN      34   // Analog input only pin - temperature sensor
#define STEAM_PIN     35   // Analog input only pin - steam/humidity sensor
#define RELAY_PIN     25   // Controls fan/motor
#define BUZZER_PIN    26   // Passive buzzer
#define RED_LED_PIN   27
#define GREEN_LED_PIN 4    
#define WIFI_LED      16

// ---------------- NETWORK CREDENTIALS ----------------
char ssid[] = "SHIELD~GEN";
char pass[] = "shieldgena";


// ---------------- GLOBAL OBJECTS & VARIABLES ----------------
BlynkTimer timer;
rgb_lcd lcd;

const bool RELAY_ACTIVE_LOW = false;

// Thresholds
const float TEMP_MIN   = 21.0;   // C - lower edge of ideal drying range
const float TEMP_MAX   = 25.0;   // C - upper edge of ideal drying range
const float TEMP_LIMIT = 45.0;   // C - absolute maximum, hard cutoff
const float HUM_MIN    = 0.0;   // % RH - lower edge of ideal range
const float HUM_MAX    = 60.0;   // % RH - upper edge of ideal range
const float MOISTURE_STOP_LOW  = 0.0;  // % - "stop drying" warning band
const float MOISTURE_STOP_HIGH = 12.0;

int STEAM_RAW_DRY = 500;   // Measured "dry" raw value
int STEAM_RAW_WET = 3500;  
// Add this line near your steam thresholds

// Buzzer-specific thresholds
const float BUZZ_TEMP_MIN = 21.0;
const float BUZZ_TEMP_MAX = 30.0;
const float BUZZ_HUM_MIN  = 20.0;
const float BUZZ_HUM_MAX  = 70.0;

const int STEAM_ALERT_THRESHOLD = 3000; 
const int RAIN_ALERT_HUMIDITY = 80.0;
const unsigned long READ_INTERVAL = 1000; 
 // Read sensors every 1s
const unsigned long BUZZ_INTERVAL = 2000;  // Buzzer toggle interval
unsigned long lastReadTime   = 0;
unsigned long lastBuzzToggle = 0;
unsigned long lastStatus     = 0;

bool buzzState = false;
bool manualOverride = false; 

bool tempAlertSent = false;
bool humidAlertSent = false;
bool stopDryingSent = false;
bool rainAlertSent = false;

// Live readings
float tempC = 0;
float humidity = 0;
float moisture = 0; 
int steamRaw = 0;


// Function Prototypes
void setFan(bool on);
void readSensors();
void evaluateSystem();
void handleBuzzer();
void send_telemetry();
void sendSensor();

// ---------------- BLYNK CALLBACKS ----------------
// Virtual Pin V0: Remote Switch
// Virtual Pin V0: Remote Switch Button
BLYNK_WRITE(V0) {
    int control_value = param.asInt();
    if (control_value == 1) {
        manualOverride = true;
        setFan(true);
        Serial.println("Manual Override: Motor ON");
    } else {
        manualOverride = false;
        Serial.println("Manual Override: OFF ");
    }
}

// Syncs dashboard state automatically when reconnecting or opening app from anywhere
BLYNK_CONNECTED() {
    Blynk.syncVirtual(V0);
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  analogReadResolution(12);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(WIFI_LED, OUTPUT);

  setFan(false); 
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, LOW);
  noTone(BUZZER_PIN);

  lcd.begin(16, 2);
  lcd.setRGB(255, 255, 255);
  lcd.setCursor(0, 0);
  lcd.print("Coffee Dryer");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");
  delay(1500);
  lcd.clear();

  // Connect to Blynk and Wi-Fi
  Serial.println("Connecting to Blynk & WiFi...");
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // Set up timer callbacks (pushes data every 2s)
  timer.setInterval(2000L, send_telemetry);
}

// ---------------- TELEMETRY TIMER ----------------
void send_telemetry(void) {
    // Pushes live data to Blynk Cloud. 
    // Anyone logged into the app or browser anywhere in the world will see this update immediately.
    Blynk.virtualWrite(V1, tempC); 
    Blynk.virtualWrite(V2, humidity);
    Blynk.virtualWrite(V3, buzzState ? 1:0);
    Blynk.virtualWrite(V4, steamRaw);
    sendSensor();
}

void sendSensor() {
    if (humidity > 80) {
        Blynk.virtualWrite(V5, "⚠️Rain Expected.");
    } else {
        Blynk.virtualWrite(V5, "Normal");
    }
}

// ---------------- MAIN LOOP ----------------
void loop() {
    Blynk.run();
    timer.run();

    // WiFi LED indicator update
    if (Blynk.connected()) {
        digitalWrite(WIFI_LED, HIGH);
    } else {
        digitalWrite(WIFI_LED, LOW);
    }

    // Status serial logging every 5 seconds
    if (millis() - lastStatus > 5000) {
        lastStatus = millis();
        if (Blynk.connected())
            Serial.println("Blynk Connected");
        else
            Serial.println("Blynk Disconnected");
    }

    // Sensor processing loop
    unsigned long now = millis();
    if (now - lastReadTime >= READ_INTERVAL) {
        lastReadTime = now;
        readSensors();
        evaluateSystem();
    }

    handleBuzzer();
}

// ---------------- SENSOR READING ----------------
void readSensors() {
  int lm35Raw = analogRead(LM35_PIN);
  float voltage = lm35Raw * (3.3 / 4095.0);
  tempC = voltage * 100.0; // LM35: 10mV per degree C

  steamRaw = analogRead(STEAM_PIN); // Pin 35
  float steamVoltage = steamRaw * (3.3 / 4095.0); // Convert raw ADC to voltage

  humidity = map(steamRaw, STEAM_RAW_DRY, STEAM_RAW_WET, 0, 100);
  humidity = constrain(humidity, 0, 100);

  moisture = humidity;

  // Serial logging showing raw ADC, voltage, mapped humidity, and temperature
  Serial.print("Temp: "); Serial.print(tempC); Serial.print(" C | ");
  Serial.print("Steam Raw: "); Serial.print(steamRaw); Serial.print("  | ");
  
  Serial.print("Humidity: "); Serial.print(humidity); Serial.println(" %");
}



// ---------------- MAIN DECISION LOGIC ----------------
void evaluateSystem() {
  bool tempNormal   = (tempC >= TEMP_MIN && tempC <= TEMP_MAX);
  bool humNormal    = (humidity >= HUM_MIN && humidity <= HUM_MAX);
  bool systemNormal = tempNormal && humNormal;

  // Fan control logic
  // Fan control logic (Runs if Manual Override is ON OR if sensors require it)
  bool sensorNeedsFan = (tempC > TEMP_MAX);
  bool fanShouldRun   = manualOverride || sensorNeedsFan;

  setFan(fanShouldRun);
  
  // Status LEDs
  digitalWrite(GREEN_LED_PIN, systemNormal ? HIGH : LOW);
  digitalWrite(RED_LED_PIN, systemNormal ? LOW : HIGH);

  // LCD Line 1: Live readings
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print(tempC, 1);
  lcd.print("C H:");
  lcd.print(humidity, 0);
  lcd.print("%  ");

  // LCD Line 2: Status messages & Blynk App Notifications
  lcd.setCursor(0, 1);

  // --- 1. STOP DRYING WARNING ---
  if (moisture >= MOISTURE_STOP_LOW && moisture <= MOISTURE_STOP_HIGH) {
    lcd.print("CHECK COFFEE   ");
    if (!stopDryingSent) {
      Blynk.logEvent("stop_drying_warning", "Target moisture LOW! Check the Coffee.");
      stopDryingSent = true;
    }
  } else {
    stopDryingSent = false;
  }

  // --- 2. HIGH STEAM ALERT ---
  if (steamRaw >= STEAM_ALERT_THRESHOLD) {
    lcd.print("HUMIDITY HIGH ");
    if (!humidAlertSent) {
      Blynk.logEvent("high_humidity_steam_alert", "Steam alert! High moisture detected.");
      humidAlertSent = true;
    }

  } else {
    humidAlertSent = false;
  }

  // --- 3. HIGH TEMPERATURE ALERT ---
  if (tempC > TEMP_MAX) {
    if (!tempAlertSent) {
      Blynk.logEvent("high_temperature_alert", "High Temperature Alert.its above 40");
      tempAlertSent = true;
    }
  } else {
    tempAlertSent = false;
  }
  // --- 4. RAIN / HIGH HUMIDITY ALERT (> 80%) ---
  if (humidity >= RAIN_ALERT_HUMIDITY) {
    if (!rainAlertSent) {
      Blynk.logEvent("rain_alert", "Rain Warning! Humidity has exceeded 80%.");
      rainAlertSent = true;
    }
  } else {
    rainAlertSent = false;
  }

  // General Status Display if no primary warning is active
  if (systemNormal) {
    lcd.print("SYSTEM OKAY       ");
  } else if (!stopDryingSent && !humidAlertSent && tempC <= TEMP_MAX) {
    lcd.print("TEMP/HUMID ALERT");
  }
}
// ---------------- FAN / RELAY CONTROL ----------------
void setFan(bool on) {
  bool signalLevel = RELAY_ACTIVE_LOW ? !on : on;
  digitalWrite(RELAY_PIN, signalLevel ? HIGH : LOW);
}


// ---------------- BUZZER CONTROL ----------------
void handleBuzzer() {
  bool tempNormal = (tempC >= BUZZ_TEMP_MIN && tempC <= BUZZ_TEMP_MAX);
  bool humNormal  = (humidity >= BUZZ_HUM_MIN && humidity <= BUZZ_HUM_MAX);
  bool stable     = tempNormal && humNormal;

  if (stable) {
    // Normal conditions: stay quiet
    if (buzzState) {
      noTone(BUZZER_PIN);
      buzzState = false;
    }
    return;
  }

  // Out-of-band: constant alarm
  if (!buzzState) {
    tone(BUZZER_PIN, 2000);
    buzzState = true;
  }
}

  
