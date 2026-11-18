import asyncio
import json
import time
import pyautogui
import websockets

import config
from mouse_engine import MouseEngine

engine = MouseEngine()

async def run():
    while True:
        try:
            print("Connecting to", config.URL)
            async with websockets.connect(
                config.URL, ping_interval=None, open_timeout=5
            ) as ws:
                print("Connected! Hold your hand in the neutral pose.")
                await ws.send("recenter")
                engine.reset()
                while True:
                    message = await asyncio.wait_for(ws.recv(), timeout=config.TIMEOUT)
                    data = json.loads(message)
                    kind = data.get("type")
                    if kind == "motion":
                        engine.set_mode(data.get("mode", "AIR_MOUSE"))
                        engine.update(data)
                    elif kind == "gesture":
                        engine.gesture(data["action"])
                    elif kind == "mode":
                        engine.set_mode(data["mode"])
        except Exception as e:
            engine.release_all()
            print("Connection lost:", repr(e), "- retrying in 2 s")
            await asyncio.sleep(2)


print("AIRMOTION 3D - starting in 5 seconds.")
print("To stop: press Ctrl+C in the terminal, or switch off the ESP32.")
time.sleep(5)
try:
    asyncio.run(run())
except KeyboardInterrupt:
    engine.release_all()
    print("Stopped.")