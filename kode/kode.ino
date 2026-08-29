#include <WiFi.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// Konfigurasi Wi-Fi 
const char* ssid = "LOGOSI 2.4G";
const char* password = "logosi@24g";

WebSocketsServer webSocket = WebSocketsServer(81);
Adafruit_MPU6050 mpu;

#define BUZZER_PIN 15 // Pin buzzer

// Variabel untuk menembak
unsigned long lastShootTime = 0;
const int cooldown = 500; // Jeda 500ms agar jaring tidak spam

// Variabel untuk pengiriman Gyroscope (pergerakan tangan 360°)
unsigned long lastGyroTime = 0;
const int gyroInterval = 20; // Kirim data arah setiap 20ms (50 FPS untuk web)

// Variabel untuk Buzzer Non-Blocking
bool isBuzzerOn = false;
unsigned long buzzerStartTime = 0;
const int buzzerDuration = 100; // Buzzer menyala 100ms

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Koneksi ke Jaringan Lokal
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Terhubung!");
  Serial.print("Catat IP Address ini: ");
  Serial.println(WiFi.localIP());

  webSocket.begin();

  // Berikan jeda 1 detik agar sensor MPU siap sepenuhnya setelah ESP32 menyala
  delay(1000);

  // Inisialisasi Sensor MPU dengan alamat I2C 0x68 yang sudah terdeteksi
  if (!mpu.begin(0x68)) {
    Serial.println("Sensor MPU tidak terdeteksi!");
    while (1) { delay(10); }
  }
  
  Serial.println("Sensor MPU Berhasil Terdeteksi!");

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
}

void loop() {
  webSocket.loop();
  
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // 1. DETEKSI TEMBAKAN (Menggunakan Akselerometer)
  if (abs(a.acceleration.x) > 15.0) { 
    if (millis() - lastShootTime > cooldown) {
      Serial.println("Thwip! Menembak jaring!");
      
      // Bunyikan Buzzer
      digitalWrite(BUZZER_PIN, HIGH);
      isBuzzerOn = true;
      buzzerStartTime = millis();
      
      // Kirim pesan tembak ke Web
      webSocket.broadcastTXT("shoot");
      
      lastShootTime = millis();
    }
  }

  // Matikan buzzer otomatis tanpa delay()
  if (isBuzzerOn && (millis() - buzzerStartTime > buzzerDuration)) {
    digitalWrite(BUZZER_PIN, LOW);
    isBuzzerOn = false;
  }

  // 2. BACA ARAH GERAKAN 360° (Menggunakan Gyroscope)
  if (millis() - lastGyroTime > gyroInterval) {
    String gyroData = "g:" + String(g.gyro.z, 3) + ":" + String(g.gyro.y, 3);
    webSocket.broadcastTXT(gyroData);
    lastGyroTime = millis();
  }
}