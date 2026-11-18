import asyncio
import json
import time
import websockets

URL = "ws://192.168.4.1:81/ws"


async def run():
    while True:
        try:
            async with websockets.connect(
                URL, ping_interval=None, open_timeout=5
            ) as ws:
                print("Connected. Sweep finger left/right, then up/down.")
                last = 0
                while True:
                    msg = await asyncio.wait_for(ws.recv(), timeout=3)
                    d = json.loads(msg)
                    if time.time() - last >= 0.2:
                        last = time.time()
                        if "gz" not in d:
                            print("NO gx/gy/gz in data -> update ESP32 firmware")
                        else:
                            print(
                                f"pitch={d['pitch']:6.1f} roll={d['roll']:6.1f} "
                                f"yaw={d['yaw']:6.1f} | "
                                f"gx={d['gx']:7.1f} gy={d['gy']:7.1f} gz={d['gz']:7.1f}"
                            )
        except Exception as e:
            print("retry:", repr(e))
            await asyncio.sleep(2)


asyncio.run(run())