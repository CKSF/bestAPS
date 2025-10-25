import os, json, sys, time
import paho.mqtt.client as mqtt
from dotenv import load_dotenv
load_dotenv()

TEAM = os.getenv("TEAM_NAME")
HOST = os.getenv("MQTT_HOST")
PORT = int(os.getenv("MQTT_PORT", "1883"))
USER = os.getenv("USERNAME")
PASS = os.getenv("PASSWORD")

if not TEAM or not HOST or not USER or not PASS:
    print("Missing env: TEAM_NAME/MQTT_HOST/USERNAME/PASSWORD"); sys.exit(1)

PFX = f"cis441-541/{TEAM}"

TOPICS = {
    "CGM_VP":        f"{PFX}/cgm",                  # VP →（transit）→ OpenAPS
    "CGM_OAPS":      f"{PFX}/cgm-openaps",
    "INS_OAPS":      f"{PFX}/insulin-pump-openaps", # OpenAPS →（transit）→ VP
    "INS_VP":        f"{PFX}/insulin-pump",
}

# --- add minimal validators to avoid NameError and drop malformed payloads ---
def is_valid_cgm(payload: str) -> bool:
    try:
        data = json.loads(payload)
        g = data.get("Glucose")
        t = data.get("time")
        return isinstance(g, (int, float)) and (t is not None)
    except Exception:
        return False

def is_valid_insulin(payload: str) -> bool:
    try:
        data = json.loads(payload)
        r = data.get("insulin_rate")
        return isinstance(r, (int, float))
    except Exception:
        return False

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

def on_connect(c, u, f, rc, p):
    print("relay connected:", rc)
    # 订阅 VP 发布的 CGM、以及 OpenAPS 发布的 insulin
    c.subscribe(TOPICS["CGM_VP"], qos=1)
    c.subscribe(TOPICS["INS_OAPS"], qos=1)
    print("subscribed:", TOPICS["CGM_VP"], TOPICS["INS_OAPS"]) 


def on_message(c, u, m):
    t = m.topic
    p = m.payload.decode("utf-8", "ignore")
    try:
        if t == TOPICS["CGM_VP"]:
            if is_valid_cgm(p):
                c.publish(TOPICS["CGM_OAPS"], p, qos=1)   # VP→OpenAPS
                print("CGM bridged VP→OAPS")
            else:
                print("drop invalid CGM:", p[:120])
        elif t == TOPICS["INS_OAPS"]:
            if is_valid_insulin(p):
                c.publish(TOPICS["INS_VP"], p, qos=1)     # OpenAPS→VP
                print("INS bridged OAPS→VP")
            else:
                print("drop invalid INS:", p[:120])
        else:
            print("ignored:", t)
    except Exception as e:
        print("relay error:", e)

client.username_pw_set(USER, PASS)
client.on_connect = on_connect
client.on_message = on_message
client.connect(HOST, PORT, 60)
client.loop_start()

try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    pass
finally:
    client.loop_stop(); client.disconnect()
