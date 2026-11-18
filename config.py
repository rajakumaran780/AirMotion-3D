URL = "ws://192.168.4.1:81/ws"
TIMEOUT = 3              # seconds of silence before reconnecting

MAX_STEP = 80            # maximum pixels per update (50 updates/sec)

# Sources you can use for each axis:
#   angles in degrees:      "pitch", "roll", "yaw"
#   rotation speed deg/s:   "gx", "gy", "gz"

# ---- Left/right ----
X_SOURCE = "gz"
X_INVERT = True
X_DEADZONE = 4.0         # deg/s ignored (stops creeping when still)
X_SENSITIVITY = 0.5      # base speed
X_ACCEL = 0.02           # extra speed on fast sweeps (0 = off)
X_SMOOTHING = 0.7        # 0 to 1; lower = smoother but slower

# ---- Up/down ----
Y_SOURCE = "gx"
Y_INVERT = True
Y_DEADZONE = 5.0
Y_SENSITIVITY = 0.5
Y_ACCEL = 0.02
Y_SMOOTHING = 0.7