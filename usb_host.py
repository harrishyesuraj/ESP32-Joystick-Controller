#!/usr/bin/env python3
"""
Host program for the ESP32 controller's USB mode.

  pip install pyserial            (required)
  pip install vgamepad            (optional, Windows: turns the stream into a virtual Xbox 360 pad)

Usage:   python usb_host.py COM5          (Windows)
         python usb_host.py /dev/ttyUSB0  (Linux)
Close the Arduino Serial Monitor first - only one program can use the port.
"""
import sys, time, serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM5"
ser = serial.Serial()
ser.port, ser.baudrate, ser.timeout = port, 115200, 0.2
ser.dtr = False; ser.rts = False          # stops the ESP32 from resetting when the port opens
ser.open()

pad = None
try:
    import vgamepad as vg
    pad = vg.VX360Gamepad()
    B = vg.XUSB_BUTTON
    BTNS = [B.XUSB_GAMEPAD_A, B.XUSB_GAMEPAD_B, B.XUSB_GAMEPAD_X, B.XUSB_GAMEPAD_Y,
            B.XUSB_GAMEPAD_LEFT_SHOULDER, B.XUSB_GAMEPAD_RIGHT_SHOULDER,
            B.XUSB_GAMEPAD_LEFT_THUMB, B.XUSB_GAMEPAD_RIGHT_THUMB,
            B.XUSB_GAMEPAD_START, B.XUSB_GAMEPAD_BACK]
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
        _, lx, ly, rx, ry, mask = line.split(",")
        lx, ly, rx, ry, mask = int(lx) / 1000, int(ly) / 1000, int(rx) / 1000, int(ry) / 1000, int(mask)
    except ValueError:
        continue
    if pad:
        pad.left_joystick_float(x_value_float=lx, y_value_float=-ly)    # firmware: up is negative
        pad.right_joystick_float(x_value_float=rx, y_value_float=-ry)
        for i, b in enumerate(BTNS):
            want, had = bool(mask & (1 << i)), bool(held & (1 << i))
            if want and not had: pad.press_button(button=b)
            if had and not want: pad.release_button(button=b)
        held = mask
        pad.update()
    else:
        print("L(%5.2f,%5.2f)  R(%5.2f,%5.2f)  buttons=%s" % (lx, ly, rx, ry, format(mask, "010b")))
