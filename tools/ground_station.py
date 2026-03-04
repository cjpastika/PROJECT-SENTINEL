#!/usr/bin/env python3
"""
PROJECT-SENTINEL Ground Station Decoder

Decodes both the custom [0xEB 0x90] telemetry protocol and CCSDS
space packets from a serial port or binary log file.

Custom protocol:
    [0xEB][0x90][MSG_ID][LENGTH][PAYLOAD...][XOR_CKSUM]
    Little-endian payloads, XOR checksum over bytes 2..end-of-payload.

CCSDS Space Packet:
    [Primary Header 6B][Secondary Header 4B][Payload][CRC-16 2B]
    Big-endian headers, CRC-16/CCITT.

Usage:
    python ground_station.py /dev/ttyUSB0          # live serial
    python ground_station.py --baud 115200 /dev/ttyACM0
    python ground_station.py --file capture.bin     # replay from file
    python ground_station.py --file capture.bin --csv out.csv
"""

import argparse
import struct
import sys
import csv
import time
from collections import OrderedDict

# ---------------------------------------------------------------------------
# Message IDs (custom protocol)
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

# CCSDS APID-to-name mapping
CCSDS_APID_NAMES = {
    0x001: "HEARTBEAT", 0x002: "IMU",         0x003: "CMD_ACK",
    0x004: "CMD_NACK",  0x005: "WDG_FAULT",   0x006: "FLIGHT_STATE",
    0x007: "EKF",       0x008: "LOG_ENTRY",    0x009: "LOG_STATUS",
    0x00A: "FAULT_EVENT",0x00B: "FAULT_SUMM",  0x00C: "TMR",
    0x7FF: "IDLE",
}

# ---------------------------------------------------------------------------
# Payload decoders (custom protocol, little-endian)
# ---------------------------------------------------------------------------

def decode_heartbeat(data):
    if len(data) < 12:
        return {"raw": data.hex()}
    beat, tick, tasks = struct.unpack_from("<IIB", data)
    return OrderedDict([
        ("beat_count", beat), ("tick_ms", tick), ("task_count", tasks),
    ])

def decode_imu(data):
    if len(data) < 28:
        return {"raw": data.hex()}
    ts, ax, ay, az, gx, gy, gz = struct.unpack_from("<Iiiiiii", data)
    return OrderedDict([
        ("timestamp_ms", ts),
        ("accel_x_mg", ax), ("accel_y_mg", ay), ("accel_z_mg", az),
        ("gyro_x_mdps", gx), ("gyro_y_mdps", gy), ("gyro_z_mdps", gz),
    ])

def decode_cmd_response(data):
    if len(data) < 4:
        return {"raw": data.hex()}
    cmd_id, result = struct.unpack_from("<BB", data)
    return OrderedDict([("cmd_id", f"0x{cmd_id:02X}"), ("result", result)])

def decode_wdg_fault(data):
    if len(data) < 8:
        return {"raw": data.hex()}
    slot, _, missed, tick = struct.unpack_from("<BBHI", data)
    return OrderedDict([
        ("slot_id", slot), ("missed_ms", missed), ("tick_ms", tick),
    ])

def decode_flight_state(data):
    if len(data) < 12:
        return {"raw": data.hex()}
    state, prev, _, tick, az = struct.unpack_from("<BBHIi", data)
    return OrderedDict([
        ("state", FLIGHT_STATES[state] if state < len(FLIGHT_STATES) else state),
        ("prev_state", FLIGHT_STATES[prev] if prev < len(FLIGHT_STATES) else prev),
        ("tick_ms", tick), ("accel_z_mg", az),
    ])

def decode_ekf(data):
    if len(data) < 16:
        return {"raw": data.hex()}
    ts, alt, vel, accel = struct.unpack_from("<Iiii", data)
    return OrderedDict([
        ("timestamp_ms", ts),
        ("altitude_mm", alt), ("velocity_mms", vel),
        ("accel_input_mg", accel),
    ])

def decode_log_status(data):
    if len(data) < 16:
        return {"raw": data.hex()}
    entries, used, wraps, size = struct.unpack_from("<IIII", data)
    return OrderedDict([
        ("entries_written", entries), ("bytes_used", used),
        ("wrap_count", wraps), ("flash_size", size),
    ])

def decode_fault_event(data):
    if len(data) < 8:
        return {"raw": data.hex()}
    tick, sev, src, detail, extra = struct.unpack_from("<IBBBB", data)
    return OrderedDict([
        ("tick_ms", tick),
        ("severity", FAULT_SEVERITY[sev] if sev < len(FAULT_SEVERITY) else sev),
        ("source", FAULT_SOURCES.get(src, f"0x{src:02X}")),
        ("detail", f"0x{detail:02X}"), ("data", f"0x{extra:02X}"),
    ])

def decode_fault_summary(data):
    if len(data) < 24:
        return {"raw": data.hex()}
    total, c0, c1, c2, c3, tick = struct.unpack_from("<IIIIII", data)
    return OrderedDict([
        ("total_events", total),
        ("info", c0), ("warning", c1), ("error", c2), ("critical", c3),
        ("tick_ms", tick),
    ])

def decode_tmr(data):
    if len(data) < 28:
        return {"raw": data.hex()}
    ts, valt, vvel, c0, c1, c2, health, disagree = struct.unpack_from(
        "<IiiiiiBB", data)
    return OrderedDict([
        ("timestamp_ms", ts),
        ("voted_alt_mm", valt), ("voted_vel_mms", vvel),
        ("chan0_alt_mm", c0), ("chan1_alt_mm", c1), ("chan2_alt_mm", c2),
        ("chan_health", f"0b{health:03b}"), ("disagree_count", disagree),
    ])

def decode_ccsds_stats(data):
    if len(data) < 16:
        return {"raw": data.hex()}
    pkts, byt, seq, crc_err, ts = struct.unpack_from("<IIHHI", data)
    return OrderedDict([
        ("packets_sent", pkts), ("bytes_sent", byt),
        ("seq_count", seq), ("crc_errors", crc_err), ("timestamp_ms", ts),
    ])

DECODERS = {
    MSG_HEARTBEAT:    decode_heartbeat,
    MSG_IMU:          decode_imu,
    MSG_CMD_ACK:      decode_cmd_response,
    MSG_CMD_NACK:     decode_cmd_response,
    MSG_WDG_FAULT:    decode_wdg_fault,
    MSG_FLIGHT_STATE: decode_flight_state,
    MSG_EKF:          decode_ekf,
    MSG_LOG_STATUS:   decode_log_status,
    MSG_FAULT_EVENT:  decode_fault_event,
    MSG_FAULT_SUMM:   decode_fault_summary,
    MSG_TMR:          decode_tmr,
    MSG_CCSDS_STATS:  decode_ccsds_stats,
}

# ---------------------------------------------------------------------------
# CRC-16/CCITT for CCSDS validation
# ---------------------------------------------------------------------------

def crc16_ccitt(data):
    """CRC-16/CCITT: polynomial 0x1021, init 0xFFFF."""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc

# ---------------------------------------------------------------------------
# Custom protocol parser
# ---------------------------------------------------------------------------

class CustomProtocolParser:
    """State machine parser for the [0xEB 0x90] framed protocol."""

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
            # Compute XOR checksum over msg_id + length + payload
            cksum = self.msg_id ^ self.length
            for b in self.payload:
                cksum ^= b
            cksum &= 0xFF

            if cksum == byte_val:
                self.packets.append((self.msg_id, bytes(self.payload)))
            else:
                self.packets.append(("CKSUM_ERR", self.msg_id, byte_val, cksum))
            self.state = self.SYNC_0

    def get_packets(self):
        pkts = self.packets
        self.packets = []
        return pkts


# ---------------------------------------------------------------------------
# CCSDS Space Packet parser
# ---------------------------------------------------------------------------

class CcsdsParser:
    """Parses CCSDS space packets from a byte stream.

    Looks for valid CCSDS headers by checking version=0, type=TLM,
    sec_hdr=1, and reasonable data_length. Validates CRC-16.
    """

    def __init__(self):
        self.buf = bytearray()
        self.packets = []

    def feed(self, byte_val):
        self.buf.append(byte_val)
        self._try_parse()

    def _try_parse(self):
        while len(self.buf) >= 6:
            # Check primary header fields
            pkt_id = (self.buf[0] << 8) | self.buf[1]
            version = (pkt_id >> 13) & 0x07
            pkt_type = (pkt_id >> 12) & 0x01
            sec_hdr = (pkt_id >> 11) & 0x01
            apid = pkt_id & 0x7FF

            # Validate header markers
            if version != 0 or pkt_type != 0 or sec_hdr != 1:
                self.buf.pop(0)
                continue

            data_length = (self.buf[4] << 8) | self.buf[5]
            total_len = 6 + data_length + 1  # primary hdr + data field

            # Minimum: sec_hdr(4) + CRC(2) = 6 data bytes, so total >= 12
            if total_len < 12 or total_len > 256:
                self.buf.pop(0)
                continue

            if len(self.buf) < total_len:
                return  # wait for more data

            pkt_data = bytes(self.buf[:total_len])

            # CRC is last 2 bytes of the packet
            crc_received = (pkt_data[-2] << 8) | pkt_data[-1]
            crc_computed = crc16_ccitt(pkt_data[:-2])

            seq_count = ((self.buf[2] << 8) | self.buf[3]) & 0x3FFF

            # Secondary header: 4-byte timestamp
            timestamp_ms = struct.unpack_from(">I", pkt_data, 6)[0]

            # Payload starts after secondary header (offset 10),
            # ends before CRC (offset -2)
            payload = pkt_data[10:-2]

            if crc_computed == crc_received:
                self.packets.append((apid, seq_count, timestamp_ms, payload))
            else:
                self.packets.append(("CRC_ERR", apid, crc_received, crc_computed))

            self.buf = self.buf[total_len:]
            return

    def get_packets(self):
        pkts = self.packets
        self.packets = []
        return pkts


# ---------------------------------------------------------------------------
# Packet formatter
# ---------------------------------------------------------------------------

def format_custom_packet(msg_id, payload):
    """Format a decoded custom protocol packet as a human-readable string."""
    name = MSG_NAMES.get(msg_id, f"UNKNOWN(0x{msg_id:02X})")
    decoder = DECODERS.get(msg_id)

    if decoder:
        fields = decoder(payload)
        field_str = "  ".join(f"{k}={v}" for k, v in fields.items())
        return f"[CUSTOM] {name}: {field_str}"
    else:
        return f"[CUSTOM] {name}: {payload.hex()}"


def format_ccsds_packet(apid, seq, timestamp_ms, payload):
    """Format a decoded CCSDS packet as a human-readable string."""
    name = CCSDS_APID_NAMES.get(apid, f"APID(0x{apid:03X})")

    # Try to decode payload using the same decoders (APID maps to MSG_ID)
    decoder = DECODERS.get(apid)
    if decoder:
        fields = decoder(payload)
        field_str = "  ".join(f"{k}={v}" for k, v in fields.items())
        return f"[CCSDS] {name} seq={seq} t={timestamp_ms}ms: {field_str}"
    else:
        return f"[CCSDS] {name} seq={seq} t={timestamp_ms}ms: {payload.hex()}"


# ---------------------------------------------------------------------------
# CSV logger
# ---------------------------------------------------------------------------

class CsvLogger:
    """Writes decoded telemetry to a CSV file."""

    def __init__(self, path):
        self.file = open(path, "w", newline="")
        self.writer = csv.writer(self.file)
        self.writer.writerow([
            "timestamp", "protocol", "msg_name", "fields"
        ])

    def log(self, protocol, msg_name, fields_dict):
        self.writer.writerow([
            time.strftime("%H:%M:%S"),
            protocol,
            msg_name,
            str(fields_dict),
        ])
        self.file.flush()

    def close(self):
        self.file.close()


# ---------------------------------------------------------------------------
# Main processing loop
# ---------------------------------------------------------------------------

def process_stream(stream, csv_logger=None, quiet=False):
    """Read bytes from stream and decode both protocols."""
    custom = CustomProtocolParser()
    ccsds = CcsdsParser()
    pkt_count = 0

    try:
        while True:
            data = stream.read(1)
            if not data:
                break

            b = data[0]
            custom.feed(b)
            ccsds.feed(b)

            # Process custom protocol packets
            for pkt in custom.get_packets():
                pkt_count += 1
                if isinstance(pkt[0], str) and pkt[0] == "CKSUM_ERR":
                    _, mid, got, exp = pkt
                    if not quiet:
                        print(f"[CUSTOM] CHECKSUM ERROR msg=0x{mid:02X} "
                              f"got=0x{got:02X} exp=0x{exp:02X}")
                else:
                    msg_id, payload = pkt
                    line = format_custom_packet(msg_id, payload)
                    if not quiet:
                        print(line)
                    if csv_logger:
                        name = MSG_NAMES.get(msg_id, f"0x{msg_id:02X}")
                        decoder = DECODERS.get(msg_id)
                        fields = decoder(payload) if decoder else {"raw": payload.hex()}
                        csv_logger.log("CUSTOM", name, fields)

            # Process CCSDS packets
            for pkt in ccsds.get_packets():
                pkt_count += 1
                if isinstance(pkt[0], str) and pkt[0] == "CRC_ERR":
                    _, apid, got, exp = pkt
                    if not quiet:
                        print(f"[CCSDS] CRC ERROR apid=0x{apid:03X} "
                              f"got=0x{got:04X} exp=0x{exp:04X}")
                else:
                    apid, seq, ts, payload = pkt
                    line = format_ccsds_packet(apid, seq, ts, payload)
                    if not quiet:
                        print(line)
                    if csv_logger:
                        name = CCSDS_APID_NAMES.get(apid, f"0x{apid:03X}")
                        decoder = DECODERS.get(apid)
                        fields = decoder(payload) if decoder else {"raw": payload.hex()}
                        csv_logger.log("CCSDS", name, fields)

    except KeyboardInterrupt:
        pass

    return pkt_count


def main():
    parser = argparse.ArgumentParser(
        description="PROJECT-SENTINEL Ground Station Decoder")
    parser.add_argument("port", nargs="?",
                        help="Serial port (e.g., /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=115200,
                        help="Baud rate (default: 115200)")
    parser.add_argument("--file", "-f",
                        help="Read from binary file instead of serial")
    parser.add_argument("--csv",
                        help="Write decoded packets to CSV file")
    parser.add_argument("--quiet", "-q", action="store_true",
                        help="Suppress console output (only write CSV)")
    args = parser.parse_args()

    csv_logger = CsvLogger(args.csv) if args.csv else None

    if args.file:
        print(f"Reading from file: {args.file}")
        with open(args.file, "rb") as f:
            count = process_stream(f, csv_logger, args.quiet)
        print(f"\nDecoded {count} packets.")
    elif args.port:
        try:
            import serial
        except ImportError:
            print("ERROR: pyserial is required for serial port mode.")
            print("Install with: pip install pyserial")
            sys.exit(1)

        print(f"Connecting to {args.port} at {args.baud} baud...")
        with serial.Serial(args.port, args.baud, timeout=1) as ser:
            print("Listening (Ctrl+C to stop)...\n")
            process_stream(ser, csv_logger, args.quiet)
    else:
        parser.print_help()
        sys.exit(1)

    if csv_logger:
        csv_logger.close()
        print(f"CSV written to: {args.csv}")


if __name__ == "__main__":
    main()
