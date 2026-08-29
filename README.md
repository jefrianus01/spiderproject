# Spider-Man 3D Web Shooter

Proyek kontrol gerak **Spider-Man** di dunia 3D (Three.js) menggunakan
pendeteksian gerakan tubuh. Ada dua mode kontrol:

1. **Mode PC (Webcam + MediaPipe)** — `server_spiderman.py`
   Menggunakan kamera webcam, mendeteksi arah kepala (Face Mesh) dan
   gerakan tangan menembak jaring (Hands), lalu mengirimkan data lewat
   WebSocket ke halaman web.
2. **Mode ESP32 (Hardware)** — `kode/kode.ino`
   Menggunakan board ESP32 + sensor MPU6050 (gyroscope/akselerometer) dan
   buzzer, mengirim data arah & tembakan lewat WebSocket.

Antarmuka 3D (`index.html`) menampilkan kota 360° tempat kamu "berayun"
sebagai Spider-Man: arah pandang diatur dari kepala/tangan, dan jaring
ditembakkan ke gedung.

---

## Cara Kerja Singkat

```
[Webcam/ESP32] --WebSocket ws://localhost:81--> [browser index.html / Three.js]
```

- Python/ESP32 membuka server WebSocket di port **81**.
- `index.html` terhubung ke `ws://localhost:81`, menerima pesan:
  - `g:<yaw>:<pitch>` → mengatur arah pandang kamera 360°.
  - `shoot` → menembak jaring ke gedung (raycaster) dan berayun.

---

## Mode 1: PC Webcam (MediaPipe) — Paling Mudah

### Prasyarat
- Python 3.8+
- Webcam / kamera laptop
- Browser modern (Chrome/Edge/Firefox)

### Instalasi
```powershell
pip install opencv-python mediapipe numpy websockets
```

### Menjalankan
1. Jalankan server Python (menyalakan webcam + WebSocket):
   ```powershell
   python server_spiderman.py
   ```
   - Server WebSocket aktif di `ws://localhost:81`.
   - Arahkan wajah lurus ke kamera lalu tekan **`c`** untuk kalibrasi posisi tengah.
   - Tekan **`q`** untuk keluar.

2. Buka `index.html` di browser (bisa diklik dua / buka langsung,
   atau lewat server lokal statis):
   ```powershell
   # opsional, jika browser memblokir file://
   python -m http.server 8000
   # lalu buka http://localhost:8000/index.html
   ```

3. Klik layar untuk mulai.
   - **Gerakkan kepala** → arah pandang 360° (kiri/kanan/atas/bawah).
   - **Buka telunjuk & kelingking** (jarak lebar) → menembak jaring (`THWIP!`).
   - Backup kontrol mouse: klik layar untuk menembak, gerakkan mouse
     (pointer lock) untuk melihat sekeliling.

---

## Mode 2: ESP32 + MPU6050 (Hardware)

### Hardware
- Board ESP32
- Sensor MPU6050 (I2C, alamat `0x68`)
- Buzzer (pin `15`)
- Kabel jumper & breadboard

### Library Arduino
Install via Library Manager:
- `WiFi` (bawaan ESP32)
- `WebSocketsServer` (oleh Markus Sattler)
- `Wire` (bawaan)
- `Adafruit MPU6050`
- `Adafruit Unified Sensor`

### Setup
1. Edit `ssid` & `password` di `kode/kode.ino` sesuai WiFi kamu.
2. Upload sketch ke ESP32.
3. Buka Serial Monitor (115200 baud) untuk melihat IP Address ESP32.
4. Hubungkan HTML ke IP tersebut: ubah di `index.html`
   ```js
   ws = new WebSocket('ws://<IP_ESP32>:81');
   ```
5. Buka `index.html`, gerakkan ESP32 (gyro = arah pandang),
   dan goyang keras (akselerometer x > 15) untuk menembak.

> Catatan: Mode ESP32 menggunakan server WebSocket di ESP32 (port 81),
> sehingga `index.html` harus diarahkan ke IP ESP32, bukan `localhost`.

---

## Struktur File

| File | Fungsi |
|------|--------|
| `server_spiderman.py` | Server Python: webcam + MediaPipe + WebSocket (Mode PC) |
| `index.html` | Antarmuka 3D Three.js + koneksi WebSocket ke browser |
| `kode/kode.ino` | Firmware ESP32 + MPU6050 (Mode Hardware) |
| `hand_landmarker.task` | Model MediaPipe Tasks (tidak dipakai oleh script di atas; cadangan) |

---

## Troubleshooting

- **HTML: "Gagal terhubung"** → pastikan `server_spiderman.py` sudah jalan
  dan tidak ada firewall yang memblokir port 81.
- **Kamera tidak terbuka** → cek index kamera di `cv2.VideoCapture(0)`;
  ganti `0` jika punya banyak kamera.
- **Arah pandang terbalik** → tekan `c` untuk kalibrasi ulang posisi netral.
- **Tangan tidak mendeteksi tembakan** → pastikan telunjuk & kelingking
  direnggangkan lebar (>100px) dan tunggu cooldown 0.8 detik.
