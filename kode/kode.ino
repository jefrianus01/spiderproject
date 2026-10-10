#include <WiFi.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include <esp_system.h>

// ===== MODE WIFI =====
#define USE_AP  true                 // true = ESP32 jadi WiFi sendiri (disarankan)
const char* AP_SSID  = "SpiderGlove";
const char* AP_PASS  = "12345678";   // minimal 8 karakter
const char* ssid     = "LogosiLT1";  // dipakai kalau USE_AP = false
const char* password = "logosi@2026";

#define SDA_PIN     21
#define SCL_PIN     22
#define BUTTON_PIN  13
#define MPU_ADDR    0x68

// ===== ARAH =====
#define ROLL_SIGN   -1
#define PITCH_SIGN   1
#define SWAP_AXES   false

#define ALPHA        0.97
#define DEBOUNCE_MS  40
#define COOLDOWN_MS  300
#define SEND_MS      40

WebSocketsServer webSocket(81);

// ===== DATA BERSAMA ANTAR INTI =====
volatile float outRoll = 0, outPitch = 0;
volatile bool  shootFlag = false;
volatile bool  needCal = false;
volatile int   activeClient = -1;
volatile bool  notifFlag = false;
char pendingNotif[160] = "";
String bootMsg = "";

// ===== VARIABEL SENSOR (inti 1) =====
float roll = 0, pitch = 0, roll0 = 0, pitch0 = 0, gbx = 0, gby = 0;
int failCount = 0;
unsigned long lastShoot = 0, lastDebug = 0, lastMicros = 0, lastLoopTime = 0;
bool btnStable = HIGH, btnLastRead = HIGH;
unsigned long btnChangeTime = 0;

String teksResetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "Nyala normal";
    case ESP_RST_SW:       return "Restart software";
    case ESP_RST_PANIC:    return "RESTART KARENA CRASH (panic)";
    case ESP_RST_INT_WDT:  return "RESTART KARENA WATCHDOG interrupt";
    case ESP_RST_TASK_WDT: return "RESTART KARENA WATCHDOG task";
    case ESP_RST_WDT:      return "RESTART KARENA WATCHDOG";
    case ESP_RST_BROWNOUT: return "RESTART KARENA BROWNOUT - daya USB kurang kuat";
    default:               return "Restart penyebab lain";
  }
}

void notif(const char* pesan) {            // aman dipanggil dari inti 1
  Serial.printf("[NOTIF] %s\n", pesan);
  strncpy(pendingNotif, pesan, sizeof(pendingNotif) - 1);
  pendingNotif[sizeof(pendingNotif) - 1] = 0;
  notifFlag = true;
}

// ===== MPU =====
void tulis(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void initMPU() {
  tulis(0x6B, 0x80); delay(100);
  tulis(0x6B, 0x01); delay(50);
  tulis(0x1B, 0x08);   // gyro +-500 dps
  tulis(0x1C, 0x10);   // accel +-8g
  tulis(0x1A, 0x03);
  tulis(0x1D, 0x03);
}

void mulaiI2C() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(200000);
  Wire.setTimeOut(20);
}

void pulihkanI2C() {
  notif("Sensor MPU gagal dibaca: kabel SDA/SCL longgar? Memulihkan...");
  Wire.end();
  pinMode(SDA_PIN, INPUT_PULLUP);
  pinMode(SCL_PIN, OUTPUT);
  for (int i = 0; i < 9; i++) {
    digitalWrite(SCL_PIN, LOW);  delayMicroseconds(10);
    digitalWrite(SCL_PIN, HIGH); delayMicroseconds(10);
  }
  mulaiI2C();
  initMPU();
  lastMicros = micros();
  failCount = 0;
}

int16_t baca16() {
  uint8_t hi = Wire.read();
  uint8_t lo = Wire.read();
  return (int16_t)((hi << 8) | lo);
}

bool bacaMPU(float &ax, float &ay, float &az, float &gx, float &gy) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)14) != 14) return false;
  ax = baca16() / 4096.0;
  ay = baca16() / 4096.0;
  az = baca16() / 4096.0;
  baca16();
  gx = baca16() / 65.5;
  gy = baca16() / 65.5;
  baca16();
  return true;
}

void sudutAccel(float ax, float ay, float az, float &r, float &p) {
  r = atan2(ay, az) * 180.0 / PI;
  p = atan2(-ax, sqrt(ay * ay + az * az)) * 180.0 / PI;
}

void kalibrasi() {
  Serial.println("Kalibrasi... tahan tangan datar & diam");
  float sr = 0, sp = 0, sgx = 0, sgy = 0;
  int n = 0;
  for (int i = 0; i < 100; i++) {
    float ax, ay, az, gx, gy, r, p;
    if (bacaMPU(ax, ay, az, gx, gy)) {
      sudutAccel(ax, ay, az, r, p);
      sr += r; sp += p; sgx += gx; sgy += gy; n++;
    }
    delay(5);
  }
  if (n > 0) {
    roll0 = sr / n;  pitch0 = sp / n;
    gbx = sgx / n;   gby = sgy / n;
    roll = roll0;    pitch = pitch0;
  }
  lastMicros = micros();
  lastLoopTime = millis();
  Serial.printf("Netral: roll=%.1f pitch=%.1f\n", roll0, pitch0);
}

// ===== TOMBOL =====
void cekTombol() {
  bool reading = digitalRead(BUTTON_PIN);
  if (reading != btnLastRead) {
    btnChangeTime = millis();
    btnLastRead = reading;
  }
  if (millis() - btnChangeTime > DEBOUNCE_MS && reading != btnStable) {
    btnStable = reading;
    if (btnStable == LOW && millis() - lastShoot > COOLDOWN_MS) {
      Serial.println("THWIP! (tombol)");
      shootFlag = true;
      lastShoot = millis();
    }
  }
}

// ===== WEBSOCKET (inti 0) =====
void kirim(const char* msg) {
  int c = activeClient;
  if (c >= 0 && webSocket.clientIsConnected((uint8_t)c)) {
    webSocket.sendTXT((uint8_t)c, msg);
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t len) {
  switch (type) {
    case WStype_CONNECTED: {
      for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
        if (i != num) webSocket.disconnect(i);
      }
      activeClient = num;
      Serial.printf("Client %u terhubung\n", num);
      String m = "n:Terhubung. " + bootMsg + " | uptime " + String(millis() / 1000) + " dtk";
      webSocket.sendTXT(num, m.c_str());
      break;
    }
    case WStype_DISCONNECTED:
      if (activeClient == (int)num) activeClient = -1;
      Serial.printf("Client %u terputus\n", num);
      break;
    case WStype_TEXT:
      if (len >= 3 && memcmp(payload, "cal", 3) == 0) needCal = true;
      break;
    default:
      break;
  }
}

void netTask(void *param) {
  unsigned long lastSend = 0;
  for (;;) {
    webSocket.loop();

    if (shootFlag) {
      shootFlag = false;
      kirim("shoot");
    }
    if (notifFlag) {
      notifFlag = false;
      String m = String("n:") + pendingNotif;
      kirim(m.c_str());
    }
    if (activeClient >= 0 && millis() - lastSend >= SEND_MS) {
      char buf[40];
      snprintf(buf, sizeof(buf), "t:%.1f:%.1f", (float)outRoll, (float)outPitch);
      kirim(buf);
      lastSend = millis();
    }
    vTaskDelay(1);
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(500);

  bootMsg = teksResetReason();
  Serial.println("Penyebab boot: " + bootMsg);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  delay(50);
  btnStable = btnLastRead = digitalRead(BUTTON_PIN);

  mulaiI2C();
  Wire.beginTransmission(MPU_ADDR);
  if (Wire.endTransmission() != 0) {
    Serial.println("MPU tidak terdeteksi! Cek kabel SDA(21)/SCL(22)/VCC/GND.");
    while (true) delay(1000);
  }
  initMPU();
  Serial.println("MPU OK");

  if (USE_AP) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.println("Mode AP aktif. Sambungkan laptop ke WiFi: " + String(AP_SSID));
    Serial.print("IP ESP32: ");
    Serial.println(WiFi.softAPIP());
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password);
    Serial.print("Menyambung WiFi");
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.print("\nIP ESP32: ");
    Serial.println(WiFi.localIP());
  }

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  webSocket.enableHeartbeat(5000, 3000, 3);

  // WiFi & pengiriman jalan di inti 0, sensor tetap di inti 1
  xTaskCreatePinnedToCore(netTask, "net", 8192, NULL, 1, NULL, 0);

  delay(300);
  kalibrasi();
  Serial.println("Siap! Tekan tombol untuk menembak.");
}

// ===== LOOP SENSOR (inti 1) =====
void loop() {
  unsigned long gap = millis() - lastLoopTime;
  if (gap > 300 && lastLoopTime != 0) {
    Serial.printf("[NOTIF] Loop sensor lambat %lu ms\n", gap);
  }
  lastLoopTime = millis();

  cekTombol();

  if (needCal) { needCal = false; kalibrasi(); }

  float ax, ay, az, gx, gy;
  if (!bacaMPU(ax, ay, az, gx, gy)) {
    if (++failCount >= 5) pulihkanI2C();
    delay(5);
    return;
  }
  failCount = 0;

  unsigned long now = micros();
  float dt = (now - lastMicros) / 1000000.0;
  lastMicros = now;
  if (dt <= 0 || dt > 0.1) dt = 0.005;

  float rAcc, pAcc;
  sudutAccel(ax, ay, az, rAcc, pAcc);

  float mag = sqrt(ax * ax + ay * ay + az * az);
  float a = (fabs(mag - 1.0) > 0.5) ? 0.999 : ALPHA;

  roll  = a * (roll  + (gx - gbx) * dt) + (1.0 - a) * rAcc;
  pitch = a * (pitch + (gy - gby) * dt) + (1.0 - a) * pAcc;

  float oR = (roll  - roll0)  * ROLL_SIGN;
  float oP = (pitch - pitch0) * PITCH_SIGN;
  if (SWAP_AXES) { float t = oR; oR = oP; oP = t; }
  outRoll = oR;
  outPitch = oP;

  if (millis() - lastDebug > 1000) {
    Serial.printf("roll=%.1f pitch=%.1f heap=%u client=%d\n",
                  oR, oP, ESP.getFreeHeap(), (int)activeClient);
    lastDebug = millis();
  }

  delay(5);
}