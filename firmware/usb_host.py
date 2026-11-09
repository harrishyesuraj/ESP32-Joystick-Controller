#!/usr/bin/env python3
"""
Host program for the ESP32 controller's USB mode  (single stick + "+" buttons).

  pip install pyserial            (required)
  pip install vgamepad            (Windows: turns the stream into a virtual Xbox 360 controller)

Usage:   python usb_host.py COM3          (Windows)
         python usb_host.py /dev/ttyUSB0  (Linux)
Close the Arduino Serial Monitor first - only one program can use the port.

Stream format from the firmware:  $,rx,ry,mask
  rx, ry : right stick -1000..1000 (up is negative)
  mask   : bit (n-1) = game button n
           1 A, 2 B, 3 X, 4 Y, 5 LB, 6 RB, 7 LT, 8 RT, 9 Select, 10 Start, 11 L3, 12 R3,
           13-16 D-pad up/down/left/right, 17-20 left-stick up/down/left/right
"""
import sys, time, serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
ser = serial.Serial()
ser.port, ser.baudrate, ser.timeout = port, 115200, 0.2
ser.dtr = False; ser.rts = False          # stops the ESP32 from resetting when the port opens
ser.open()

pad = None
try:
    import vgamepad as vg
    pad = vg.VX360Gamepad()
    B = vg.XUSB_BUTTON
    BTNS = {0: B.XUSB_GAMEPAD_A, 1: B.XUSB_GAMEPAD_B, 2: B.XUSB_GAMEPAD_X, 3: B.XUSB_GAMEPAD_Y,
            4: B.XUSB_GAMEPAD_LEFT_SHOULDER, 5: B.XUSB_GAMEPAD_RIGHT_SHOULDER,
            8: B.XUSB_GAMEPAD_BACK, 9: B.XUSB_GAMEPAD_START,
            10: B.XUSB_GAMEPAD_LEFT_THUMB, 11: B.XUSB_GAMEPAD_RIGHT_THUMB,
            12: B.XUSB_GAMEPAD_DPAD_UP, 13: B.XUSB_GAMEPAD_DPAD_DOWN,
            14: B.XUSB_GAMEPAD_DPAD_LEFT, 15: B.XUSB_GAMEPAD_DPAD_RIGHT}
    print("Virtual Xbox 360 controller created.")
except Exception as e:
    print("vgamepad not available (%s) - printing values only." % e)

last_hb = 0
held = 0
print("Listening on", port, "- Ctrl+C to quit")
while True:
    if time.time() - last_hb > 1.0:       # heartbeat: tells the ESP32 the host is there
        ser.write(b"H\n"); last_hb = time.time()
    line = ser.readline().decode(errors="ignore").strip()
    if not line.startswith("$,"):
        continue
    try:
        _, rx, ry, mask = line.split(",")
        rx, ry, mask = int(rx) / 1000, int(ry) / 1000, int(mask)
    except ValueError:
        continue
    if pad:
        lx = (1 if mask & (1 << 19) else 0) - (1 if mask & (1 << 18) else 0)
        ly = (1 if mask & (1 << 16) else 0) - (1 if mask & (1 << 17) else 0)      # up = +1 for vgamepad
        pad.left_joystick_float(x_value_float=lx, y_value_float=ly)
        pad.right_joystick_float(x_value_float=rx, y_value_float=-ry)
        for bit, b in BTNS.items():
            want, had = bool(mask & (1 << bit)), bool(held & (1 << bit))
            if want and not had: pad.press_button(button=b)
            if had and not want: pad.release_button(button=b)
        pad.left_trigger(255 if mask & (1 << 6) else 0)
        pad.right_trigger(255 if mask & (1 << 7) else 0)
        held = mask
        pad.update()
    else:
        print("R-stick(%5.2f,%5.2f)  buttons=%s" % (rx, ry, format(mask, "020b")))
