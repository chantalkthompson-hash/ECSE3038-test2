import os
import json
import secrets
from datetime import datetime, timezone
from typing import Optional

import paho.mqtt.client as mqtt
from dotenv import load_dotenv
from fastapi import FastAPI, Header, HTTPException, Query, Response, status
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field
from pymongo import MongoClient

load_dotenv()

MONGODB_URI = os.getenv("MONGODB_URI")
MQTT_NAME = os.getenv("MQTT_NAME", "").strip()
MQTT_BROKER = "135.232.203.144"
MQTT_PORT = 1883

client = MongoClient(MONGODB_URI)
db = client["ecse3038_test2"]
fans_col = db["fans"]
readings_col = db["readings"]

mqtt_client = mqtt.Client()
try:
    mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
    mqtt_client.loop_start()
except Exception as e:
    print(f"MQTT connection warning: {e}")

app = FastAPI(title="ECSE3038 Test 2 - Bathroom Fan API")


# --- Pydantic Models ---
class FanCreate(BaseModel):
    mac: str
    room: str

class ReadingCreate(BaseModel):
    mac: str
    humidity: float

class SettingsUpdate(BaseModel):
    mode: str
    max_humidity: float


# --- A1: Fans ---
@app.post("/fans", status_code=201)
def create_fan(fan: FanCreate):
    if fans_col.find_one({"mac": fan.mac}):
        raise HTTPException(status_code=409, detail="Fan with this MAC already exists")

    api_key = secrets.token_urlsafe(16)
    doc = {
        "mac": fan.mac,
        "room": fan.room,
        "api_key": api_key,
    }
    fans_col.insert_one(doc)
    return {"mac": fan.mac, "room": fan.room, "api_key": api_key}


@app.get("/fans")
def list_fans():
    fans = list(fans_col.find({}, {"_id": 0, "api_key": 0}))
    return fans


@app.get("/fans/{mac}")
def get_fan(mac: str):
    fan = fans_col.find_one({"mac": mac}, {"_id": 0, "api_key": 0})
    if not fan:
        raise HTTPException(status_code=404, detail="Fan not found")
    return fan


# --- A2: Post Readings ---
@app.post("/readings", status_code=201)
def create_reading(
    reading: ReadingCreate,
    x_api_key: Optional[str] = Header(default=None, alias="X-API-Key"),
):
    if not x_api_key:
        raise HTTPException(status_code=401, detail="Missing X-API-Key header")

    key_owner = fans_col.find_one({"api_key": x_api_key})
    if not key_owner:
        raise HTTPException(status_code=401, detail="Invalid X-API-Key")

    if key_owner["mac"] != reading.mac:
        raise HTTPException(status_code=403, detail="API key does not belong to this MAC")

    if reading.humidity < 0 or reading.humidity > 100:
        raise HTTPException(status_code=422, detail="Humidity must be between 0 and 100")

    timestamp = datetime.now(timezone.utc).isoformat()
    doc = {
        "mac": reading.mac,
        "humidity": reading.humidity,
        "time": timestamp,
    }
    readings_col.insert_one(doc)
    return {"mac": reading.mac, "humidity": reading.humidity, "time": timestamp}


# --- A3: Get Readings ---
@app.get("/readings")
def get_readings(
    mac: Optional[str] = Query(default=None),
    limit: int = Query(default=10, ge=1, le=100),
):
    query = {"mac": mac} if mac else {}
    cursor = readings_col.find(query, {"_id": 0}).sort("_id", -1).limit(limit)
    return list(cursor)


# --- A4: Update Fan Settings & Publish MQTT ---
@app.put("/fans/{mac}/settings")
def update_settings(mac: str, settings: SettingsUpdate):
    if settings.mode not in ("auto", "on", "off"):
        raise HTTPException(status_code=422, detail="mode must be auto, on, or off")

    if settings.max_humidity < 0 or settings.max_humidity > 100:
        raise HTTPException(status_code=422, detail="max_humidity must be between 0 and 100")

    fan = fans_col.find_one({"mac": mac})
    if not fan:
        raise HTTPException(status_code=404, detail="Fan not found")

    is_replacement = "settings" in fan and fan["settings"] is not None
    new_settings = {
        "mode": settings.mode,
        "max_humidity": float(settings.max_humidity),
    }

    fans_col.update_one({"mac": mac}, {"$set": {"settings": new_settings}})

    topic = f"ecse3038/{MQTT_NAME}/fan/settings"
    mqtt_client.publish(topic, json.dumps(new_settings), retain=True)

    status_code = 200 if is_replacement else 201
    return JSONResponse(status_code=status_code, content=new_settings)


# --- A5: Delete Fan ---
@app.delete("/fans/{mac}", status_code=204)
def delete_fan(
    mac: str,
    x_api_key: Optional[str] = Header(default=None, alias="X-API-Key"),
):
    if not x_api_key:
        raise HTTPException(status_code=401, detail="Missing X-API-Key header")

    key_owner = fans_col.find_one({"api_key": x_api_key})
    if not key_owner:
        raise HTTPException(status_code=401, detail="Invalid X-API-Key")

    fan = fans_col.find_one({"mac": mac})
    if not fan:
        raise HTTPException(status_code=404, detail="Fan not found")

    if key_owner["mac"] != mac:
        raise HTTPException(status_code=403, detail="API key does not belong to this MAC")

    fans_col.delete_one({"mac": mac})
    return Response(status_code=204)