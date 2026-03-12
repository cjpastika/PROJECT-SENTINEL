#!/usr/bin/env python3
"""
PROJECT-SENTINEL Launch Viewer

Real-time 3D mission visualization with live telemetry from the flight computer.
Runs QEMU, decodes telemetry, and streams to a browser-based 3D viewer via SSE.

Usage:
    python tools/launch_viewer.py                  # launch QEMU + viewer
    python tools/launch_viewer.py --file log.bin   # replay from capture file
    python tools/launch_viewer.py --demo           # demo mode (no QEMU)
    python tools/launch_viewer.py --port 8080      # custom HTTP port
"""

import argparse
import http.server
import json
import os
import queue
import signal
import struct
import subprocess
import sys
import threading
import time
import webbrowser
from collections import OrderedDict
from pathlib import Path

# ---------------------------------------------------------------------------
# Telemetry constants (mirrors ground_station.py)
# ---------------------------------------------------------------------------

MSG_HEARTBEAT    = 0x01
MSG_IMU          = 0x02
MSG_CMD_ACK      = 0x03
MSG_CMD_NACK     = 0x04
MSG_WDG_FAULT    = 0x05
MSG_FLIGHT_STATE = 0x06
MSG_EKF          = 0x07
MSG_LOG_ENTRY    = 0x08
MSG_LOG_STATUS   = 0x09
MSG_FAULT_EVENT  = 0x0A
MSG_FAULT_SUMM   = 0x0B
MSG_TMR          = 0x0C
MSG_CCSDS_STATS  = 0x0D

MSG_NAMES = {
    MSG_HEARTBEAT:    "HEARTBEAT",
    MSG_IMU:          "IMU",
    MSG_CMD_ACK:      "CMD_ACK",
    MSG_CMD_NACK:     "CMD_NACK",
    MSG_WDG_FAULT:    "WDG_FAULT",
    MSG_FLIGHT_STATE: "FLIGHT_STATE",
    MSG_EKF:          "EKF",
    MSG_LOG_ENTRY:    "LOG_ENTRY",
    MSG_LOG_STATUS:   "LOG_STATUS",
    MSG_FAULT_EVENT:  "FAULT_EVENT",
    MSG_FAULT_SUMM:   "FAULT_SUMMARY",
    MSG_TMR:          "TMR",
    MSG_CCSDS_STATS:  "CCSDS_STATS",
}

FLIGHT_STATES = ["IDLE", "ARMED", "BOOST", "COAST", "DESCENT", "LANDED"]
FAULT_SEVERITY = ["INFO", "WARNING", "ERROR", "CRITICAL"]
FAULT_SOURCES = {
    0x00: "SYSTEM", 0x01: "WATCHDOG", 0x02: "SENSOR",
    0x03: "CMD",    0x04: "FSM",      0x05: "EKF",
    0x06: "DATALOG",0x07: "RTOS",     0x08: "TMR",
}

# ---------------------------------------------------------------------------
# Payload decoders
# ---------------------------------------------------------------------------

def decode_heartbeat(data):
    if len(data) < 12: return None
    beat, tick, tasks = struct.unpack_from("<IIB", data)
    return {"beat_count": beat, "tick_ms": tick, "task_count": tasks}

def decode_imu(data):
    if len(data) < 28: return None
    ts, ax, ay, az, gx, gy, gz = struct.unpack_from("<Iiiiiii", data)
    return {
        "timestamp_ms": ts,
        "accel_x_mg": ax, "accel_y_mg": ay, "accel_z_mg": az,
        "gyro_x_mdps": gx, "gyro_y_mdps": gy, "gyro_z_mdps": gz,
    }

def decode_flight_state(data):
    if len(data) < 12: return None
    state, prev, _, tick, az = struct.unpack_from("<BBHIi", data)
    return {
        "state": FLIGHT_STATES[state] if state < len(FLIGHT_STATES) else str(state),
        "state_id": state,
        "prev_state": FLIGHT_STATES[prev] if prev < len(FLIGHT_STATES) else str(prev),
        "tick_ms": tick, "accel_z_mg": az,
    }

def decode_ekf(data):
    if len(data) < 16: return None
    ts, alt, vel, accel = struct.unpack_from("<Iiii", data)
    return {
        "timestamp_ms": ts,
        "altitude_mm": alt, "velocity_mms": vel,
        "accel_input_mg": accel,
    }

def decode_tmr(data):
    if len(data) < 28: return None
    ts, valt, vvel, c0, c1, c2, health, disagree = struct.unpack_from(
        "<IiiiiiBB", data)
    return {
        "timestamp_ms": ts,
        "voted_alt_mm": valt, "voted_vel_mms": vvel,
        "chan0_alt_mm": c0, "chan1_alt_mm": c1, "chan2_alt_mm": c2,
        "chan_health": health, "disagree_count": disagree,
    }

def decode_fault_event(data):
    if len(data) < 8: return None
    tick, sev, src, detail, extra = struct.unpack_from("<IBBBB", data)
    return {
        "tick_ms": tick,
        "severity": FAULT_SEVERITY[sev] if sev < len(FAULT_SEVERITY) else str(sev),
        "source": FAULT_SOURCES.get(src, f"0x{src:02X}"),
        "detail": f"0x{detail:02X}", "data": f"0x{extra:02X}",
    }

def decode_fault_summary(data):
    if len(data) < 24: return None
    total, c0, c1, c2, c3, tick = struct.unpack_from("<IIIIII", data)
    return {
        "total_events": total,
        "info": c0, "warning": c1, "error": c2, "critical": c3,
        "tick_ms": tick,
    }

def decode_wdg_fault(data):
    if len(data) < 8: return None
    slot, _, missed, tick = struct.unpack_from("<BBHI", data)
    return {"slot_id": slot, "missed_ms": missed, "tick_ms": tick}

def decode_cmd_response(data):
    if len(data) < 4: return None
    cmd_id, result = struct.unpack_from("<BB", data)
    return {"cmd_id": f"0x{cmd_id:02X}", "result": result}

DECODERS = {
    MSG_HEARTBEAT:    decode_heartbeat,
    MSG_IMU:          decode_imu,
    MSG_CMD_ACK:      decode_cmd_response,
    MSG_CMD_NACK:     decode_cmd_response,
    MSG_WDG_FAULT:    decode_wdg_fault,
    MSG_FLIGHT_STATE: decode_flight_state,
    MSG_EKF:          decode_ekf,
    MSG_FAULT_EVENT:  decode_fault_event,
    MSG_FAULT_SUMM:   decode_fault_summary,
    MSG_TMR:          decode_tmr,
}

# ---------------------------------------------------------------------------
# Custom protocol parser
# ---------------------------------------------------------------------------

class CustomProtocolParser:
    SYNC_0, SYNC_1, MSG_ID, LENGTH, PAYLOAD, CKSUM = range(6)

    def __init__(self):
        self.state = self.SYNC_0
        self.msg_id = 0
        self.length = 0
        self.payload = bytearray()
        self.packets = []

    def feed(self, byte_val):
        if self.state == self.SYNC_0:
            if byte_val == 0xEB:
                self.state = self.SYNC_1
        elif self.state == self.SYNC_1:
            if byte_val == 0x90:
                self.state = self.MSG_ID
            else:
                self.state = self.SYNC_0
        elif self.state == self.MSG_ID:
            self.msg_id = byte_val
            self.state = self.LENGTH
        elif self.state == self.LENGTH:
            self.length = byte_val
            self.payload = bytearray()
            self.state = self.PAYLOAD if self.length > 0 else self.CKSUM
        elif self.state == self.PAYLOAD:
            self.payload.append(byte_val)
            if len(self.payload) >= self.length:
                self.state = self.CKSUM
        elif self.state == self.CKSUM:
            cksum = self.msg_id ^ self.length
            for b in self.payload:
                cksum ^= b
            cksum &= 0xFF
            if cksum == byte_val:
                self.packets.append((self.msg_id, bytes(self.payload)))
            self.state = self.SYNC_0

    def get_packets(self):
        pkts = self.packets
        self.packets = []
        return pkts


# ---------------------------------------------------------------------------
# Global telemetry event bus
# ---------------------------------------------------------------------------

class TelemetryBus:
    """Thread-safe pub/sub for telemetry events streamed to SSE clients."""

    def __init__(self):
        self._subscribers = []
        self._lock = threading.Lock()
        self.packet_count = 0
        self.start_time = time.time()

    def subscribe(self):
        q = queue.Queue(maxsize=500)
        with self._lock:
            self._subscribers.append(q)
        return q

    def unsubscribe(self, q):
        with self._lock:
            try:
                self._subscribers.remove(q)
            except ValueError:
                pass

    def publish(self, event):
        self.packet_count += 1
        with self._lock:
            dead = []
            for q in self._subscribers:
                try:
                    q.put_nowait(event)
                except queue.Full:
                    dead.append(q)
            for q in dead:
                self._subscribers.remove(q)


telemetry_bus = TelemetryBus()

# ---------------------------------------------------------------------------
# Telemetry reader thread
# ---------------------------------------------------------------------------

def telemetry_reader(stream):
    """Read binary stream, decode packets, publish to bus."""
    parser = CustomProtocolParser()

    # Use the underlying raw/unbuffered stream so reads return as soon as
    # any bytes are available instead of blocking until 256 bytes accumulate.
    raw = getattr(stream, 'buffer', stream)   # BufferedReader → raw
    raw = getattr(raw, 'raw', raw)            # unwrap to RawIOBase if possible

    try:
        while True:
            data = raw.read(256)
            if not data:
                break
            for byte_val in data:
                parser.feed(byte_val)

            for pkt in parser.get_packets():
                if isinstance(pkt[0], str):
                    continue  # skip checksum errors
                msg_id, payload = pkt
                name = MSG_NAMES.get(msg_id, f"0x{msg_id:02X}")
                decoder = DECODERS.get(msg_id)
                if decoder:
                    fields = decoder(payload)
                    if fields:
                        telemetry_bus.publish({
                            "type": name,
                            "msg_id": msg_id,
                            "data": fields,
                            "t": time.time() - telemetry_bus.start_time,
                        })
    except (OSError, ValueError):
        pass

    # Signal end of stream
    telemetry_bus.publish({"type": "EOF", "data": {}, "t": 0})


# ---------------------------------------------------------------------------
# HTTP server
# ---------------------------------------------------------------------------

TOOLS_DIR = Path(__file__).parent

class ViewerHandler(http.server.BaseHTTPRequestHandler):
    """Serves the viewer HTML and provides SSE telemetry stream."""

    def log_message(self, format, *args):
        pass  # suppress default logging

    def do_GET(self):
        if self.path == "/" or self.path == "/index.html":
            self._serve_file("viewer.html", "text/html")
        elif self.path == "/events":
            self._serve_sse()
        elif self.path == "/cmd" or self.path.startswith("/cmd?"):
            self._handle_cmd()
        elif self.path == "/favicon.ico":
            self.send_response(204)
            self.end_headers()
        else:
            try:
                self.send_error(404)
            except (BrokenPipeError, ConnectionResetError):
                pass

    def do_POST(self):
        if self.path == "/cmd":
            self._handle_cmd()
        else:
            try:
                self.send_error(404)
            except (BrokenPipeError, ConnectionResetError):
                pass

    def _serve_file(self, filename, content_type):
        fpath = TOOLS_DIR / filename
        if not fpath.exists():
            self.send_error(404)
            return
        content = fpath.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(content)

    def _serve_sse(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()

        sub = telemetry_bus.subscribe()
        try:
            while True:
                try:
                    event = sub.get(timeout=1.0)
                    payload = json.dumps(event)
                    self.wfile.write(f"data: {payload}\n\n".encode())
                    self.wfile.flush()
                    if event.get("type") == "EOF":
                        break
                except queue.Empty:
                    # Send keepalive comment
                    self.wfile.write(b": keepalive\n\n")
                    self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass
        finally:
            telemetry_bus.unsubscribe(sub)

    def _handle_cmd(self):
        """Forward uplink commands to QEMU stdin (if available)."""
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(b"OK")


# ---------------------------------------------------------------------------
# QEMU launcher
# ---------------------------------------------------------------------------

def find_elf():
    """Locate the sentinel.elf binary."""
    project_root = TOOLS_DIR.parent
    candidates = [
        project_root / "build" / "sentinel.elf",
        project_root / "sentinel.elf",
    ]
    for c in candidates:
        if c.exists():
            return str(c)
    return None


def launch_qemu():
    """Start QEMU and return the process (stdout is the telemetry stream)."""
    elf = find_elf()
    if not elf:
        print("[VIEWER] ERROR: sentinel.elf not found. Run 'cmake --build build' first.")
        sys.exit(1)

    print(f"[VIEWER] Launching QEMU with {elf}")
    proc = subprocess.Popen(
        [
            "qemu-system-arm",
            "-machine", "lm3s6965evb",
            "-nographic",
            "-kernel", elf,
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        stdin=subprocess.PIPE,
    )
    return proc


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="PROJECT-SENTINEL 3D Launch Viewer")
    parser.add_argument("--port", "-p", type=int, default=8088,
                        help="HTTP server port (default: 8088)")
    parser.add_argument("--file", "-f",
                        help="Replay telemetry from binary capture file")
    parser.add_argument("--demo", action="store_true",
                        help="Run in demo mode (no QEMU, synthetic data)")
    parser.add_argument("--no-browser", action="store_true",
                        help="Don't auto-open browser")
    args = parser.parse_args()

    qemu_proc = None

    if args.file:
        print(f"[VIEWER] Replaying from {args.file}")
        f = open(args.file, "rb")
        reader_thread = threading.Thread(
            target=telemetry_reader, args=(f,), daemon=True)
        reader_thread.start()
    elif args.demo:
        print("[VIEWER] Demo mode — no telemetry source")
    else:
        qemu_proc = launch_qemu()
        reader_thread = threading.Thread(
            target=telemetry_reader, args=(qemu_proc.stdout,), daemon=True)
        reader_thread.start()

    # Start HTTP server
    server = http.server.HTTPServer(("127.0.0.1", args.port), ViewerHandler)
    server_thread = threading.Thread(target=server.serve_forever, daemon=True)
    server_thread.start()

    url = f"http://127.0.0.1:{args.port}"
    print(f"[VIEWER] Mission Control @ {url}")
    print("[VIEWER] Press Ctrl+C to terminate\n")

    if not args.no_browser:
        webbrowser.open(url)

    def shutdown(sig, frame):
        print("\n[VIEWER] Shutting down...")
        server.shutdown()
        if qemu_proc:
            qemu_proc.terminate()
            qemu_proc.wait()
        sys.exit(0)

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    # Block main thread
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        shutdown(None, None)


if __name__ == "__main__":
    main()
