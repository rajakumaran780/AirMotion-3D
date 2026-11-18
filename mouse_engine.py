import pyautogui
import config

pyautogui.FAILSAFE = False   # no corner stop; use Ctrl+C or unplug the ESP32
pyautogui.PAUSE = 0


def shape(value, deadzone, sensitivity, accel):
    magnitude = abs(value) - deadzone
    if magnitude <= 0:
        return 0.0
    gain = sensitivity * (1.0 + accel * magnitude)
    step = min(magnitude * gain, config.MAX_STEP)
    return step if value > 0 else -step


class Axis:
    def __init__(self, source, invert, deadzone, sensitivity, accel, smoothing):
        self.source = source
        self.invert = invert
        self.deadzone = deadzone
        self.sensitivity = sensitivity
        self.accel = accel
        self.smoothing = smoothing
        self.reset()

    def reset(self):
        self.smoothed = 0.0
        self.remainder = 0.0

    def step(self, data):
        value = data.get(self.source, 0.0)
        self.smoothed += self.smoothing * (value - self.smoothed)
        delta = shape(self.smoothed, self.deadzone, self.sensitivity, self.accel)
        if self.invert:
            delta = -delta
        self.remainder += delta
        whole = int(self.remainder)
        self.remainder -= whole
        return whole


class MouseEngine:
    def __init__(self):
        self.mode = "AIR_MOUSE"
        self.left_down = False
        self.x = Axis(config.X_SOURCE, config.X_INVERT, config.X_DEADZONE,
                      config.X_SENSITIVITY, config.X_ACCEL, config.X_SMOOTHING)
        self.y = Axis(config.Y_SOURCE, config.Y_INVERT, config.Y_DEADZONE,
                      config.Y_SENSITIVITY, config.Y_ACCEL, config.Y_SMOOTHING)

    def reset(self):
        self.x.reset()
        self.y.reset()

    def set_mode(self, mode):
        if mode != self.mode:
            self.mode = mode
            self.reset()
            print("Mode:", mode)

    def update(self, data):
        if self.mode == "3D":
            return                      # cursor paused in 3D mode
        dx = self.x.step(data)
        dy = self.y.step(data)
        if dx or dy:
            pyautogui.moveRel(dx, dy)

    def gesture(self, action):
        print("Gesture:", action)
        if self.mode == "3D":
            return
        print("Gesture:", action)
        if action == "LEFT_CLICK":
            pyautogui.click(button="left")
        elif action == "RIGHT_CLICK":
            pyautogui.click(button="right")
        elif action == "LEFT_DOWN":
            pyautogui.mouseDown(button="left")
            self.left_down = True
        elif action == "LEFT_UP":
            pyautogui.mouseUp(button="left")
            self.left_down = False

    def release_all(self):
        if self.left_down:
            pyautogui.mouseUp(button="left")
            self.left_down = False