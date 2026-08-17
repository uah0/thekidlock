import sys
import time

import serial


port = sys.argv[1]
log_path = sys.argv[2]
reset_on_open = len(sys.argv) > 3 and sys.argv[3] == "reset"

with serial.Serial(port, 115200, timeout=1) as ser, open(log_path, "a", encoding="utf-8", buffering=1) as log:
    log.write("\n--- serial capture started %s ---\n" % time.strftime("%Y-%m-%d %H:%M:%S"))
    if reset_on_open:
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False
        time.sleep(0.2)
    while True:
        data = ser.readline()
        if data:
            text = data.decode("utf-8", errors="replace")
            sys.stdout.write(text)
            sys.stdout.flush()
            log.write(text)
