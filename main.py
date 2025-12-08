from fastapi import FastAPI, WebSocket, Request
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates
import asyncio
import json
import requests

# === KONFIGURASI ===
THINGSBOARD_HOST = "192.168.1.8"
THINGSBOARD_RPC_PORT = 9090

DEVICE_ID = "0186b6b0-d3e6-11f0-9256-45837a919917"
DEVICE_ACCESS_TOKEN = "f3llv0g8y1ig0e8ehz6x"

TB_USER = "tenant@thingsboard.org"
TB_PASSWORD = "tenant"

# Global state
current_state = {
    "waterLevel": 0,
    "pumpStatus": False,
    "mode": "automatic",
    "alert": None
}

# Global JWT token
JWT_TOKEN = None

# WebSocket clients
websocket_clients = set()

# === LOGIN ===
def login_and_get_jwt():
    global JWT_TOKEN
    url = f"http://{THINGSBOARD_HOST}:{THINGSBOARD_RPC_PORT}/api/auth/login"
    payload = {"username": TB_USER, "password": TB_PASSWORD}
    headers = {"Content-Type": "application/json"}

    try:
        response = requests.post(url, json=payload, headers=headers)
        if response.status_code == 200:
            JWT_TOKEN = response.json()["token"]
            print("✅ Login berhasil.")
            return True
        else:
            print(f"❌ Login gagal: {response.status_code}")
            return False
    except Exception as e:
        print(f"❌ Error login: {e}")
        return False

# === KIRIM RPC ===
def send_rpc_to_device(method, params):
    if not JWT_TOKEN:
        print("❌ JWT token belum didapatkan.")
        return

    url = f"http://{THINGSBOARD_HOST}:{THINGSBOARD_RPC_PORT}/api/plugins/rpc/twoway/{DEVICE_ID}"
    payload = {"method": method, "params": params}
    headers = {"Content-Type": "application/json", "X-Authorization": f"Bearer {JWT_TOKEN}"}

    try:
        response = requests.post(url, json=payload, headers=headers)
        if response.status_code == 200:
            print(f"✅ RPC '{method}' dikirim: {params}")
        else:
            print(f"❌ Gagal kirim RPC: {response.status_code}")
    except Exception as e:
        print(f"❌ Error: {e}")

# === AMBIL DATA TERKINI DARI THINGSBOARD (TELEMETRY) ===
def fetch_latest_telemetry():
    if not JWT_TOKEN:
        return

    url = f"http://{THINGSBOARD_HOST}:{THINGSBOARD_RPC_PORT}/api/plugins/telemetry/DEVICE/{DEVICE_ID}/values/timeseries?keys=waterLevel,pumpStatus,mode,alert&limit=1"
    headers = {"X-Authorization": f"Bearer {JWT_TOKEN}"}

    print("🌐 Mengakses telemetri...")

    try:
        response = requests.get(url, headers=headers)
        print(f"📡 Status Code: {response.status_code}")
        print(f"📦 Response: {response.text}")

        if response.status_code == 200:
            data = response.json()
            for key in ["waterLevel", "pumpStatus", "mode", "alert"]:
                if key in data and len(data[key]) > 0:
                    value = data[key][0]["value"]
                    if key == "waterLevel":
                        current_state[key] = float(value) if isinstance(value, str) else value
                    elif key == "pumpStatus":
                        if isinstance(value, str):
                            current_state[key] = value.lower() == "true"
                        else:
                            current_state[key] = bool(value)
                    else:
                        current_state[key] = value
                    print(f"✅ {key} = {current_state[key]}")
        else:
            print(f"❌ Gagal ambil telemetri: {response.status_code}")

    except Exception as e:
        print(f"❌ Error ambil telemetri: {e}")

# === BACKGROUND LOOP (Async) ===
async def background_loop():
    while True:
        fetch_latest_telemetry()
        # Kirim update ke semua WebSocket
        if websocket_clients:
            message = json.dumps(current_state)
            for ws in list(websocket_clients):
                try:
                    await ws.send_text(message)
                except:
                    websocket_clients.discard(ws)
        await asyncio.sleep(2)

# === FASTAPI SETUP ===
app = FastAPI()
app.mount("/static", StaticFiles(directory="static"), name="static")
templates = Jinja2Templates(directory="templates")

@app.get("/")
def get_index(request: Request):
    return templates.TemplateResponse("index.html", {"request": request})

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    websocket_clients.add(websocket)
    print(f"🔌 WebSocket terhubung. Jumlah klien: {len(websocket_clients)}")

    try:
        while True:
            data = await websocket.receive_text()
            command = json.loads(data)
            if command["type"] == "set_mode":
                send_rpc_to_device("setMode", {"mode": command["value"]})
            elif command["type"] == "set_pump":
                send_rpc_to_device("setPumpStatus", {"pumpStatus": command["value"]})
    except Exception as e:
        print(f"❌ WebSocket error: {e}")
    finally:
        websocket_clients.discard(websocket)
        print(f"🔌 WebSocket terputus. Jumlah klien: {len(websocket_clients)}")

# === LIFESPAN UNTUK BACKGROUND LOOP ===
@app.on_event("startup")
async def startup_event():
    if login_and_get_jwt():
        print("🔄 Memulai background loop...")
        asyncio.create_task(background_loop())
    else:
        print("❌ Gagal login. Background loop tidak dimulai.")

# === START APPLICATION ===
if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
