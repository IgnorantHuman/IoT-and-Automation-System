#include <Servo.h>

Servo doorServo;

// ---- SENSOR PINS ----
const int tempPin  = A0;   // TMP36 temperature sensor
const int ldrPin   = A1;   // LDR light sensor
const int trigPin  = 2;    // Ultrasonic trigger
const int echoPin  = 3;    // Ultrasonic echo

// ---- ACTUATOR PINS ----
const int lightPin  = 9;   // LED light (PWM)
const int fanPin    = 10;  // DC motor fan (PWM)
const int doorPin   = 6;   // Servo motor (door)

// ---- ULTRASONIC ----
long duration;
int distance;

// ---- FAN ----
int fanSpeed = 0;

// ---- DOOR ----
int doorPosition = 0;       // 0 = CLOSED, 90 = OPEN

// ---- MODE & STATE (per-device, from tinkercad pattern) ----
bool lightManualMode  = false;
bool lightState       = false;

bool fanManualMode    = false;
bool fanState         = false;

bool doorManualMode   = false;

// ---- TIMING ----
unsigned long lastSensorSend        = 0;
const unsigned long SENSOR_INTERVAL = 2000;  // Send sensor data every 2 seconds

unsigned long sensorPauseUntil      = 0;
const unsigned long CMD_PAUSE_MS    = 50;    // Brief pause after receiving a command


// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(9600);

  pinMode(lightPin,  OUTPUT);
  pinMode(fanPin,    OUTPUT);

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  doorServo.attach(doorPin);
  doorServo.write(0);  // Start CLOSED

  // --- Send initial status for all devices ---
  Serial.print("building/light/state ");
  Serial.println(lightState ? "ON" : "OFF");

  Serial.print("building/lightmode/state ");
  Serial.println(lightManualMode ? "MANUAL" : "AUTO");

  Serial.print("building/fan/state ");
  Serial.println(fanState ? "ON" : "OFF");

  Serial.print("building/fanmode/state ");
  Serial.println(fanManualMode ? "MANUAL" : "AUTO");

  Serial.print("building/door/state ");
  Serial.println(doorPosition == 90 ? "OPEN" : "CLOSED");

  Serial.print("building/doormode/state ");
  Serial.println(doorManualMode ? "MANUAL" : "AUTO");
}


// ============================================================
//  APPLY HELPERS  (mirror tinkercad's applyXxx pattern)
// ============================================================
void applyLight(bool state) {
  digitalWrite(lightPin, state ? HIGH : LOW);
  lightState = state;
  Serial.print("building/light/state ");
  Serial.println(state ? "ON" : "OFF");
}

void applyFan(bool state) {
  fanSpeed = state ? 255 : 0;
  analogWrite(fanPin, fanSpeed);
  fanState = state;
  Serial.print("building/fan/state ");
  Serial.println(state ? "ON" : "OFF");
}

void applyDoor(bool open) {
  doorPosition = open ? 90 : 0;
  doorServo.write(doorPosition);
  Serial.print("building/door/state ");
  Serial.println(open ? "OPEN" : "CLOSED");
}

// ============================================================
//  READ & HANDLE INCOMING COMMANDS
// ============================================================
void readMQTTCommands() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.length() == 0) return;

    int spaceIndex = input.indexOf(' ');
    if (spaceIndex == -1) return;

    String topic   = input.substring(0, spaceIndex);
    String payload = input.substring(spaceIndex + 1);
    topic.trim();
    payload.trim();

    // ---- LIGHT MODE ----
    if (topic == "building/lightmode") {
      lightManualMode = (payload == "MANUAL");
      Serial.print("building/lightmode/state ");
      Serial.println(lightManualMode ? "MANUAL" : "AUTO");
      // Resend current light status on mode change
      Serial.print("building/light/state ");
      Serial.println(lightState ? "ON" : "OFF");
    }

    // ---- LIGHT CONTROL ----
    else if (topic == "building/light/control") {
      if (lightManualMode) {
        applyLight(payload == "ON");
      }
    }

    // ---- FAN MODE ----
    else if (topic == "building/fanmode") {
      fanManualMode = (payload == "MANUAL");
      Serial.print("building/fanmode/state ");
      Serial.println(fanManualMode ? "MANUAL" : "AUTO");
      // Resend current fan status on mode change
      Serial.print("building/fan/state ");
      Serial.println(fanState ? "ON" : "OFF");
    }

    // ---- FAN CONTROL ----
    else if (topic == "building/fan/control") {
      if (fanManualMode) {
        applyFan(payload == "ON");
      }
    }

    // ---- DOOR MODE ----
    else if (topic == "building/doormode") {
      doorManualMode = (payload == "MANUAL");
      Serial.print("building/doormode/state ");
      Serial.println(doorManualMode ? "MANUAL" : "AUTO");
      // Resend current door status on mode change
      Serial.print("building/door/state ");
      Serial.println(doorPosition == 90 ? "OPEN" : "CLOSED");
    }

    // ---- DOOR CONTROL ----
    else if (topic == "building/door/control") {
      if (doorManualMode) {
        applyDoor(payload == "OPEN");
      }
    }

    sensorPauseUntil = millis() + CMD_PAUSE_MS;
  }
}


// ============================================================
//  PUBLISH SENSOR DATA
// ============================================================
void publishSensorData(int temp, int light, int dist) {
  Serial.print("building/temperature ");
  Serial.println(temp);

  Serial.print("building/light_level ");
  Serial.println(light);

  Serial.print("building/distance ");
  Serial.println(dist);

  Serial.print("building/motion ");
  Serial.println((dist > 0 && dist < 300) ? "DETECTED" : "CLEAR");
}


// ============================================================
//  MAIN LOOP
// ============================================================
void loop() {
  unsigned long now = millis();

  // ========================= 1. READ COMMANDS =========================
  readMQTTCommands();

  // ========================= 2. READ SENSORS ==========================

  // -- Temperature (TMP36) --
  int tempRaw = analogRead(tempPin);
  float voltage = tempRaw * (5.0 / 1023.0);
  int tempMapped = (int)((voltage - 0.5) * 100.0);  // °C

  // -- Light (LDR) --
  int ldrRaw    = analogRead(ldrPin);
  int lightValue = map(ldrRaw, 0, 1023, -4, 106);
  lightValue = constrain(lightValue, 0, 100);

  // -- Ultrasonic distance --
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  duration = pulseIn(echoPin, HIGH);
  distance = duration * 0.034 / 2;

  // ========================= 3. AUTO CONTROL ==========================

  // -- Light --
  if (!lightManualMode) {
    bool autoLight = (lightValue < 30);
    if (autoLight != lightState) applyLight(autoLight);
  }

  // -- Fan --
  if (!fanManualMode) {
    bool autoFan = (tempMapped > 30);
    if (autoFan != fanState) applyFan(autoFan);
  }

  // -- Door (ultrasonic proximity) --
  if (!doorManualMode) {
    bool shouldOpen = (distance > 0 && distance < 100);
    bool currentlyOpen = (doorPosition == 90);
    if (shouldOpen != currentlyOpen) applyDoor(shouldOpen);
  }

  // ========================= 4. PUBLISH SENSORS =======================
  if (now >= sensorPauseUntil) {
    if (now - lastSensorSend >= SENSOR_INTERVAL) {
      lastSensorSend = now;
      publishSensorData(tempMapped, lightValue, distance);
    }
  }

  delay(1000);
}