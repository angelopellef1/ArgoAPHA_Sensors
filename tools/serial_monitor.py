#!/usr/bin/env python3
"""Serial monitor for the wifi6_sensor board.

Opens the port without toggling DTR/RTS (no reset), survives the USB CDC port
disappearing during light sleep, and forwards single-key debug commands
(s=status, p=periodic, w=stay awake, r=restart, h=help). Ctrl+C exits.
"""

import argparse
import datetime
import sys
import threading
import time

import serial
from serial.tools import list_ports

ESPRESSIF_VID = 0x303A


def find_port():
    for info in list_ports.comports():
        if info.vid == ESPRESSIF_VID:
            return info.device
    return None


def open_port(name, baud):
    port = serial.Serial()
    port.port = name
    port.baudrate = baud
    port.timeout = 0.2
    # Set before open() so the board is not reset or put into download mode.
    port.dtr = False
    port.rts = False
    port.open()
    return port


class Monitor:
    def __init__(self, args):
        self.args = args
        self.port = None
        self.lock = threading.Lock()
        self.running = True
        self.log = open(args.log, "a", encoding="utf-8") if args.log else None

    def info(self, text):
        self.emit(f"--- {text} ---")

    def emit(self, line):
        if self.args.timestamps:
            line = f"{datetime.datetime.now():%H:%M:%S.%f}"[:-3] + " " + line
        print(line, flush=True)
        if self.log:
            self.log.write(line + "\n")
            self.log.flush()

    def send(self, data):
        with self.lock:
            if self.port is None:
                self.info("not connected, key dropped")
                return
            try:
                self.port.write(data)
            except serial.SerialException:
                pass

    def connect(self):
        waiting_reported = False
        while self.running:
            name = self.args.port or find_port()
            if name:
                try:
                    port = open_port(name, self.args.baud)
                    with self.lock:
                        self.port = port
                    self.info(f"connected to {name} @ {self.args.baud}")
                    return True
                except serial.SerialException:
                    pass
            if not waiting_reported:
                self.info("waiting for device (asleep or unplugged)")
                waiting_reported = True
            time.sleep(0.3)
        return False

    def disconnect(self):
        with self.lock:
            if self.port is not None:
                try:
                    self.port.close()
                except serial.SerialException:
                    pass
                self.port = None
        self.info("disconnected")

    def run(self):
        buffer = b""
        while self.running:
            if self.port is None and not self.connect():
                break
            try:
                chunk = self.port.read(256)
            except (serial.SerialException, OSError):
                self.disconnect()
                buffer = b""
                continue
            if not chunk:
                continue
            buffer += chunk
            while b"\n" in buffer:
                raw, buffer = buffer.split(b"\n", 1)
                line = raw.decode("utf-8", "replace").rstrip("\r")
                if self.args.hide_status and (line.find("] STATUS ") >= 0 or line.find("] STATS ") >= 0):
                    continue
                if self.args.filter and self.args.filter not in line:
                    continue
                self.emit(line)


def key_reader(monitor):
    if sys.platform == "win32":
        import msvcrt

        while monitor.running:
            if msvcrt.kbhit():
                ch = msvcrt.getwch()
                if ch == "\x03":  # Ctrl+C consumed by getwch instead of raising SIGINT
                    monitor.running = False
                    return
                if ch in ("\x00", "\xe0"):
                    msvcrt.getwch()  # swallow arrow/function key code
                    continue
                monitor.send(ch.encode("utf-8", "ignore"))
            else:
                time.sleep(0.05)
    else:
        for line in sys.stdin:
            monitor.send(line.strip().encode("utf-8", "ignore"))


def main():
    parser = argparse.ArgumentParser(description="wifi6_sensor serial monitor")
    parser.add_argument("-p", "--port", help="serial port (default: auto-detect Espressif USB)")
    parser.add_argument("-b", "--baud", type=int, default=115200)
    parser.add_argument("-l", "--log", help="append output to this file")
    parser.add_argument("-t", "--timestamps", action="store_true", help="prefix lines with host time")
    parser.add_argument("--hide-status", action="store_true", help="hide periodic STATUS/STATS lines")
    parser.add_argument("-f", "--filter", help="only show lines containing this text")
    args = parser.parse_args()

    monitor = Monitor(args)
    monitor.info("keys: s=status p=periodic w=stay-awake r=restart h=help | Ctrl+C to quit")
    threading.Thread(target=key_reader, args=(monitor,), daemon=True).start()
    try:
        monitor.run()
    except KeyboardInterrupt:
        pass
    finally:
        monitor.running = False
        if monitor.port is not None:
            monitor.port.close()
        if monitor.log:
            monitor.log.close()
        print("--- exit ---")


if __name__ == "__main__":
    main()
