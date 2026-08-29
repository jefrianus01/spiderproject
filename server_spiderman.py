import cv2
import time
import math
import numpy as np
import asyncio
import websockets
import threading
import mediapipe as mp

mp_face_mesh = mp.solutions.face_mesh
face_mesh = mp_face_mesh.FaceMesh(
    max_num_faces=1,
    refine_landmarks=True,
    min_detection_confidence=0.7,
    min_tracking_confidence=0.7
)

mp_hands = mp.solutions.hands
hands = mp_hands.Hands(
    max_num_hands=1,
    min_detection_confidence=0.7,
    min_tracking_confidence=0.7
)

cap = cv2.VideoCapture(0)

connected_clients = set()
last_shoot_time = 0

neutral_x = 0.5
neutral_y = 0.5
is_calibrated = False

async def handler(websocket):
    connected_clients.add(websocket)
    print("Koneksi ke HTML Berhasil!")
    try:
        async for message in websocket:
            pass
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        connected_clients.remove(websocket)
        print("Koneksi terputus.")

async def broadcast(message):
    if connected_clients:
        await asyncio.gather(*[client.send(message) for client in connected_clients], return_exceptions=True)

def process_webcam():
    global last_shoot_time, neutral_x, neutral_y, is_calibrated
    print("Kamera aktif! Hadap lurus, lalu tekan 'c'. Arah putaran sudah disesuaikan agar tidak terbalik.")

    current_yaw = 0.0
    current_pitch = 0.0

    while cap.isOpened():
        success, img = cap.read()
        if not success:
            break

        img = cv2.flip(img, 1) 
        h, w, c = img.shape
        img_rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)

        face_results = face_mesh.process(img_rgb)
        hand_results = hands.process(img_rgb)

        is_shooting = False

        # --- DETEKSI ARAH KEPALA (ARAH SUDAH DIBALIK AGAR NORMAL) ---
        if face_results.multi_face_landmarks:
            for face_landmarks in face_results.multi_face_landmarks:
                nose = face_landmarks.landmark[1]
                nx, ny = nose.x, nose.y

                if not is_calibrated:
                    neutral_x = nx
                    neutral_y = ny
                    is_calibrated = True

                delta_x = nx - neutral_x
                delta_y = ny - neutral_y

                # Tanda plus/minus dibalik agar sesuai dengan gerakan kepala asli
                if delta_x < -0.06:
                    current_yaw += 0.15  # Menoleh ke kiri memutar tampilan ke kiri
                elif delta_x > 0.06:
                    current_yaw -= 0.15  # Menoleh ke kanan memutar tampilan ke kanan
                else:
                    current_pitch = delta_y * -8.0  # Menghadap ke atas/bawah agar normal

        # --- DETEKSI TEMBAK JARING DENGAN TANGAN ---
        if hand_results.multi_hand_landmarks:
            for hand_lms in hand_results.multi_hand_landmarks:
                thumb = hand_lms.landmark[4]
                index = hand_lms.landmark[8]
                pinky = hand_lms.landmark[20]
                
                tx, ty = int(thumb.x * w), int(thumb.y * h)
                ix, iy = int(index.x * w), int(index.y * h)
                px, py = int(pinky.x * w), int(pinky.y * h)
                
                dist_thumb_pinky = math.hypot(tx - px, ty - py)
                if dist_thumb_pinky > 100:
                    current_time = time.time()
                    if current_time - last_shoot_time > 0.8:
                        is_shooting = True
                        last_shoot_time = current_time

        if is_shooting:
            asyncio.run_coroutine_threadsafe(broadcast("shoot"), loop)
            cv2.putText(img, "THWIP! (MENEMBAK)", (50, 50), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 0, 255), 2)

        gyro_msg = f"g:{current_yaw:.3f}:{current_pitch:.3f}"
        asyncio.run_coroutine_threadsafe(broadcast(gyro_msg), loop)

        status_calib = "STATUS: TERKALIBRASI (Tekan 'c' Reset)" if is_calibrated else "STATUS: TEKAN 'C' UNTUK KALIBRASI"
        cv2.putText(img, status_calib, (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 2)

        cv2.imshow('Kontrol Normal Spider-Man', img)
        
        key = cv2.waitKey(1) & 0xFF
        if key == ord('c'):
            is_calibrated = False
            current_yaw = 0.0
            print("Posisi tengah dikalibrasi ulang!")
        elif key == ord('q'):
            break

    cap.release()
    cv2.destroyAllWindows()

async def main():
    global loop
    loop = asyncio.get_running_loop()
    async with websockets.serve(handler, "localhost", 81):
        print("WebSocket Server aktif di ws://localhost:81")
        threading.Thread(target=process_webcam, daemon=True).start()
        await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())