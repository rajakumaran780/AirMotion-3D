#include <Wire.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------- Settings ----------------
#define MPU_ADDR 0x68
#define LOOP_US 10000            // 100 Hz sensor loop
#define SEND_MS 20               // 50 Hz Wi-Fi updates
#define ALPHA 0.98f              // complementary filter weight (gyro)

#define TOUCH_PIN 27
#define TOUCH_ACTIVE_HIGH 1      // set to 0 if your board outputs LOW when touched
#define DEBOUNCE_MS 30
#define HOLD_MS 450              // hold time for right click / drag
#define DOUBLE_MS 280            // double-tap window

#define OLED_ADDR 0x3C
#define OLED_MS 250              // OLED refresh (4 Hz)

// Flip any of these to -1 to reverse a direction
#define PITCH_SIGN 1
#define ROLL_SIGN  1
#define YAW_SIGN   1

const char* AP_SSID = "AIRMOTION-3D";
const char* AP_PASS = "YOUR PASSWORD";

WebSocketsServer ws(81);
Adafruit_SSD1306 display(128, 32, &Wire, -1, 400000UL, 400000UL);
bool oledOk = false;
unsigned long lastOled = 0;

// ---------------- Orientation ----------------
// Axis that gravity is on at power-on (the neutral pose)
// 0:+Z 1:-Z 2:+Y 3:-Y 4:+X 5:-X
int orient = 0;
const char* orientName[] = {"+Z", "-Z", "+Y", "-Y", "+X", "-X"};

float gxBias = 0, gyBias = 0, gzBias = 0;
float pitch = 0, roll = 0, yaw = 0;
float pitchOff = 0, rollOff = 0;
unsigned long lastLoop = 0, lastSend = 0;
uint32_t seq = 0;

// ---------------- Last gesture text for the OLED ----------------
char lastGesture[12] = "";
unsigned long lastGestureMs = 0;

void setLast(const char* s) {
  strncpy(lastGesture, s, sizeof(lastGesture) - 1);
  lastGesture[sizeof(lastGesture) - 1] = 0;
  lastGestureMs = millis();
}

// ---------------- OLED ----------------
void oledMsg(const char* l1, const char* l2) {
  if (!oledOk) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print(l1);
  display.setTextSize(1);
  display.setCursor(0, 22);
  display.print(l2);
  display.display();
}

// ---------------- Gestures ----------------
enum GState { G_IDLE, G_PRESSED, G_HOLDING, G_WAIT_SECOND, G_SECOND_PRESSED, G_IGNORE };
GState gState = G_IDLE;
bool rawTouch = false, stableTouch = false;
unsigned long rawChangeMs = 0, pressMs = 0, releaseMs = 0;
bool mode3D = false;

void sendGesture(const char* action) {
  char b[64];
  snprintf(b, sizeof(b), "{\"type\":\"gesture\",\"action\":\"%s\"}", action);
  ws.broadcastTXT(b);
  Serial.println(action);

  if (!strcmp(action, "LEFT_CLICK")) setLast("L-CLICK");
  else if (!strcmp(action, "RIGHT_CLICK")) setLast("R-CLICK");
  else if (!strcmp(action, "LEFT_DOWN")) setLast("DRAG");
  else if (!strcmp(action, "LEFT_UP")) setLast("DROP");
}

void sendMode() {
  char b[64];
  snprintf(b, sizeof(b), "{\"type\":\"mode\",\"mode\":\"%s\"}", mode3D ? "3D" : "AIR_MOUSE");
  ws.broadcastTXT(b);
  Serial.println(mode3D ? "MODE 3D" : "MODE AIR_MOUSE");
  setLast(mode3D ? "3D ON" : "3D OFF");
}

void updateGestures() {
  unsigned long now = millis();

  // Debounce
  bool raw = (digitalRead(TOUCH_PIN) == (TOUCH_ACTIVE_HIGH ? HIGH : LOW));
  if (raw != rawTouch) { rawTouch = raw; rawChangeMs = now; }
  bool pressedEdge = false, releasedEdge = false;
  if (rawTouch != stableTouch && now - rawChangeMs >= DEBOUNCE_MS) {
    stableTouch = rawTouch;
    if (stableTouch) pressedEdge = true; else releasedEdge = true;
  }

  switch (gState) {
    case G_IDLE:
      if (pressedEdge) { pressMs = now; gState = G_PRESSED; }
      break;

    case G_PRESSED:
      if (releasedEdge) { releaseMs = now; gState = G_WAIT_SECOND; }          // short tap
      else if (now - pressMs >= HOLD_MS) { sendGesture("RIGHT_CLICK"); gState = G_IGNORE; }
      break;

    case G_HOLDING:                                                           // drag in progress
      if (releasedEdge) { sendGesture("LEFT_UP"); gState = G_IDLE; }
      break;

    case G_WAIT_SECOND:
      if (pressedEdge) { pressMs = now; gState = G_SECOND_PRESSED; }
      else if (now - releaseMs >= DOUBLE_MS) { sendGesture("LEFT_CLICK"); gState = G_IDLE; }
      break;

    case G_SECOND_PRESSED:
      if (releasedEdge) { mode3D = !mode3D; sendMode(); gState = G_IDLE; }    // double tap
      else if (now - pressMs >= HOLD_MS) { sendGesture("LEFT_DOWN"); gState = G_HOLDING; }
      break;

    case G_IGNORE:
      if (releasedEdge) gState = G_IDLE;
      break;
  }
}

// ---------------- Running OLED screen ----------------
void updateOLED() {
  if (!oledOk) return;
  if (millis() - lastOled < OLED_MS) return;
  lastOled = millis();

  char b[28];
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Line 1: name + mode
  display.setCursor(0, 0);
  display.print("AIRMOTION 3D");
  display.setCursor(mode3D ? 108 : 96, 0);
  display.print(mode3D ? "3D" : "MOUSE");

  // Line 2: Wi-Fi + connected clients
  snprintf(b, sizeof(b), "WiFi AP  Clients:%d", WiFi.softAPgetStationNum());
  display.setCursor(0, 8);
  display.print(b);

  // Line 3: orientation
  snprintf(b, sizeof(b), "P%4d R%4d Y%4d",
           (int)(PITCH_SIGN * (pitch - pitchOff)),
           (int)(ROLL_SIGN * (roll - rollOff)),
           (int)(YAW_SIGN * yaw));
  display.setCursor(0, 16);
  display.print(b);

  // Line 4: touch + last gesture
  display.setCursor(0, 24);
  display.print(stableTouch ? "TOUCH:ON " : "TOUCH:-- ");
  if (millis() - lastGestureMs < 2000) {
    display.setCursor(66, 24);
    display.print(lastGesture);
  }

  display.display();
}

// ---------------- WebSocket ----------------
void recenter() {
  pitchOff = pitch;
  rollOff = roll;
  yaw = 0;
  Serial.println("Re-centered.");
  setLast("RE-CENTER");
}

void onEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.printf("Client %u connected\n", num);
  } else if (type == WStype_DISCONNECTED) {
    Serial.printf("Client %u disconnected\n", num);
  } else if (type == WStype_TEXT) {
    if (length >= 8 && memcmp(payload, "recenter", 8) == 0) recenter();
  }
}

// ---------------- MPU6050 ----------------
void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

// Raw read, no remap
bool readRaw(float &ax, float &ay, float &az, float &gx, float &gy, float &gz) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)14) != 14) return false;

  int16_t rax = (Wire.read() << 8) | Wire.read();
  int16_t ray = (Wire.read() << 8) | Wire.read();
  int16_t raz = (Wire.read() << 8) | Wire.read();
  Wire.read(); Wire.read();                       // skip temperature
  int16_t rgx = (Wire.read() << 8) | Wire.read();
  int16_t rgy = (Wire.read() << 8) | Wire.read();
  int16_t rgz = (Wire.read() << 8) | Wire.read();

  ax = rax / 16384.0f; ay = ray / 16384.0f; az = raz / 16384.0f;
  gx = rgx / 131.0f;   gy = rgy / 131.0f;   gz = rgz / 131.0f;
  return true;
}

// Rotate a vector so the neutral pose's gravity axis becomes +Z
void remap(float &x, float &y, float &z) {
  float ox = x, oy = y, oz = z;
  switch (orient) {
    case 0: break;
    case 1: y = -oy; z = -oz; break;
    case 2: y = -oz; z = oy;  break;
    case 3: y = oz;  z = -oy; break;
    case 4: x = -oz; z = ox;  break;
    case 5: x = oz;  z = -ox; break;
  }
}

bool readMPU(float &ax, float &ay, float &az, float &gx, float &gy, float &gz) {
  if (!readRaw(ax, ay, az, gx, gy, gz)) return false;
  remap(ax, ay, az);
  remap(gx, gy, gz);
  return true;
}

void detectOrientation() {
  float sx = 0, sy = 0, sz = 0;
  int n = 0;
  while (n < 50) {
    float ax, ay, az, gx, gy, gz;
    if (readRaw(ax, ay, az, gx, gy, gz)) {
      sx += ax; sy += ay; sz += az;
      n++;
    }
    delay(5);
  }
  float ax = sx / n, ay = sy / n, az = sz / n;

  if (fabsf(az) >= fabsf(ax) && fabsf(az) >= fabsf(ay)) orient = (az > 0) ? 0 : 1;
  else if (fabsf(ay) >= fabsf(ax))                      orient = (ay > 0) ? 2 : 3;
  else                                                  orient = (ax > 0) ? 4 : 5;

  Serial.printf("Raw accel: x=%.2f y=%.2f z=%.2f\n", ax, ay, az);
  Serial.printf("Detected gravity axis: %s\n", orientName[orient]);
}

void calibrateGyro() {
  Serial.println("Calibrating gyro - keep sensor still...");
  float sx = 0, sy = 0, sz = 0;
  int n = 0;
  while (n < 500) {
    float ax, ay, az, gx, gy, gz;
    if (readMPU(ax, ay, az, gx, gy, gz)) {
      sx += gx; sy += gy; sz += gz;
      n++;
    }
    delay(4);
  }
  gxBias = sx / n; gyBias = sy / n; gzBias = sz / n;
  Serial.println("Gyro calibration done.");
}

// ---------------- Setup / loop ----------------
void setup() {
  Serial.begin(115200);
  pinMode(TOUCH_PIN, TOUCH_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
  Wire.begin(21, 22);
  Wire.setClock(400000);

  oledOk = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (oledOk) oledMsg("AIRMOTION", "3D  starting...");
  else Serial.println("OLED not found - continuing without display");

  delay(3000);

  writeReg(0x6B, 0x00);   // wake up
  writeReg(0x1C, 0x00);   // accel +/-2 g
  writeReg(0x1B, 0x00);   // gyro +/-250 deg/s
  writeReg(0x1A, 0x03);   // low-pass filter

  oledMsg("ORIENT", "Detecting pose...");
  detectOrientation();

  oledMsg("CALIBRATE", "Keep sensor still");
  calibrateGyro();
  oledMsg("CAL OK", "Starting Wi-Fi...");

  float ax, ay, az, gx, gy, gz;
  if (readMPU(ax, ay, az, gx, gy, gz)) {
    roll  = atan2f(ay, az) * 180.0f / PI;
    pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * 180.0f / PI;
    Serial.printf("Start pose: pitch=%.1f roll=%.1f\n", pitch, roll);
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.setSleep(false);
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP());

  ws.begin();
  ws.onEvent(onEvent);

  oledMsg("READY", WiFi.softAPIP().toString().c_str());
  delay(1200);

  lastLoop = micros();
  Serial.println("Ready. Send 'c' to re-center.");
}

void loop() {
  ws.loop();
  updateGestures();
  updateOLED();

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'c' || c == 'C') recenter();
  }

  unsigned long now = micros();
  if (now - lastLoop < LOOP_US) return;
  float dt = (now - lastLoop) / 1000000.0f;
  lastLoop = now;

  float ax, ay, az, gx, gy, gz;
  if (!readMPU(ax, ay, az, gx, gy, gz)) return;

  gx -= gxBias; gy -= gyBias; gz -= gzBias;

  float accRoll  = atan2f(ay, az) * 180.0f / PI;
  float accPitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * 180.0f / PI;

  roll  = ALPHA * (roll  + gx * dt) + (1.0f - ALPHA) * accRoll;
  pitch = ALPHA * (pitch + gy * dt) + (1.0f - ALPHA) * accPitch;

  if (fabsf(gz) > 0.3f) yaw += gz * dt;
  else yaw *= 0.999f;                    // slowly pulls yaw back to neutral when still
  if (yaw > 180) yaw -= 360;
  if (yaw < -180) yaw += 360;

  if (millis() - lastSend >= SEND_MS) {
    lastSend = millis();
    char buf[240];
    snprintf(buf, sizeof(buf),
      "{\"type\":\"motion\",\"mode\":\"%s\",\"pitch\":%.1f,\"roll\":%.1f,\"yaw\":%.1f,"
      "\"gx\":%.1f,\"gy\":%.1f,\"gz\":%.1f,\"touch\":%s,\"seq\":%lu}",
      mode3D ? "3D" : "AIR_MOUSE",
      PITCH_SIGN * (pitch - pitchOff),
      ROLL_SIGN * (roll - rollOff),
      YAW_SIGN * yaw,
      gx, gy, gz,
      stableTouch ? "true" : "false",
      (unsigned long)seq++);
    ws.broadcastTXT(buf);
  }
}