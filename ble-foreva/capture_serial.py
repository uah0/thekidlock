"""Capture USB Serial/JTAG output without losing gaps during a long RF test."""

import argparse
import datetime as dt
import time

import serial


def marker(name, **fields):
    now = dt.datetime.now().astimezone().isoformat(timespec="milliseconds")
    return "# " + name + " " + now + " " + " ".join(f"{k}={v}" for k, v in fields.items())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--mode", choices=("STAT", "RAW", "CSV"), default="STAT")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    with open(args.output, "a", encoding="utf-8", buffering=1) as output:
        def emit(line):
            print(line, flush=True)
            output.write(line + "\n")

        emit(marker("HOST_CAPTURE_START", port=args.port, mode=args.mode))
        while True:
            try:
                port = serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=1)
                port.dtr = False
                port.rts = False
                port.port = args.port
                port.open()
                emit(marker("HOST_PORT_CONNECTED", port=args.port))
                try:
                    if args.mode != "STAT":
                        time.sleep(1)
                        try:
                            port.write((args.mode + "\n").encode("ascii"))
                        except serial.SerialTimeoutException as exc:
                            emit(marker("HOST_MODE_COMMAND_FAILED", mode=args.mode, error=repr(exc)))
                            raise SystemExit("Scanner did not accept the mode command") from exc
                    while True:
                        raw = port.readline()
                        if raw:
                            emit(raw.decode("utf-8", errors="replace").rstrip("\r\n"))
                finally:
                    port.close()
            except (serial.SerialException, OSError) as exc:
                emit(marker("HOST_PORT_DISCONNECTED", error=repr(exc)))
                time.sleep(2)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
