#!/usr/bin/env python3
import argparse
import os
import sys
import time
import threading
import queue as thread_queue

try:
    import can
except ImportError:
    print("ERROR: python-can is required. Install with 'pip install python-can'.")
    sys.exit(1)

UPDATE_CMD_BASE = 0x1FFFFF00
UPDATE_ACK_BASE = 0x1FFFFF40
UPDATE_DATA_ID = 0x1FFFFF80

CMD_UPDATE_START = 1
CMD_UPDATE_DATA = 2
CMD_UPDATE_END = 3
CMD_UPDATE_ACK = 4
CMD_UPDATE_NACK = 5


def crc16_update(crc, data):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc


def crc16(data):
    return crc16_update(0xFFFF, data)


def format_message(can_id, data):
    return can.Message(
        arbitration_id=can_id,
        is_extended_id=True,
        data=data,
    )


def print_progress(sent_bytes, total_bytes, seq, total_packets):
    percent = (sent_bytes / total_bytes) * 100 if total_bytes else 0
    print(f"Progress: {sent_bytes}/{total_bytes} bytes ({percent:.1f}%), packet {seq}/{total_packets}", end='\r', flush=True)


def send_update_start(bus, node_id, size, crc, window=0):
    payload = bytearray(8)
    payload[0] = CMD_UPDATE_START
    payload[1] = min(window, 255)  # ACK-Fenster aushandeln (0 = Firmware-Standard)
    payload[2] = size & 0xFF
    payload[3] = (size >> 8) & 0xFF
    payload[4] = (size >> 16) & 0xFF
    payload[5] = (size >> 24) & 0xFF
    payload[6] = crc & 0xFF
    payload[7] = (crc >> 8) & 0xFF
    bus.send(format_message(UPDATE_CMD_BASE + node_id, payload))


def send_update_data(bus, seq, chunk):
    payload = bytearray(2 + len(chunk))
    payload[0] = (seq >> 8) & 0xFF
    payload[1] = seq & 0xFF
    payload[2:] = chunk
    bus.send(format_message(UPDATE_DATA_ID, payload))


def send_update_end(bus, node_id):
    payload = bytearray(1)
    payload[0] = CMD_UPDATE_END
    bus.send(format_message(UPDATE_CMD_BASE + node_id + 0x10, payload))


def parse_args():
    parser = argparse.ArgumentParser(description="Upload firmware via CANable gs_usb to a CAN OTA node.")
    parser.add_argument("--node-id", type=int, required=True, help="Target node ID")
    parser.add_argument("--bitrate", type=int, default=250000, help="CAN bitrate in bits/s")
    parser.add_argument("--channel", type=int, default=0, help="gs_usb channel number")
    parser.add_argument("--window", type=int, default=64, help="Number of packets to send before waiting for an ACK (max 255, negotiated with firmware)")
    parser.add_argument("--delay", type=float, default=0.0, help="Delay between packets in seconds")
    parser.add_argument("--timeout", type=float, default=3.0, help="Timeout for each ACK in seconds")
    parser.add_argument("firmware", help="Path to firmware binary file")
    return parser.parse_args()


def open_bus(channel, bitrate):
    try:
        bus = can.Bus(interface="gs_usb", channel=channel, bitrate=bitrate)
    except Exception as e:
        print(f"ERROR: Could not open gs_usb bus: {e}")
        sys.exit(1)
    return bus


def drain_bus(bus, timeout=0.01):
    while True:
        msg = bus.recv(timeout=timeout)
        if msg is None:
            break


class CanReceiver:
    """Background-Thread, der permanent bus.recv() aufruft.
    Stellt sicher, dass kein ACK/NACK-Frame verloren geht,
    auch wenn der Haupt-Thread gerade bus.send() aufruft."""

    def __init__(self, bus, ack_id):
        self._bus = bus
        self._ack_id = ack_id
        self._queue = thread_queue.Queue()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True, name="can-rx")
        self._thread.start()

    def _run(self):
        while not self._stop.is_set():
            msg = self._bus.recv(timeout=0.05)
            if msg is None:
                continue
            if msg.arbitration_id != self._ack_id:
                continue
            if len(msg.data) < 1:
                continue
            status = msg.data[0]
            info = msg.data[1] if len(msg.data) > 1 else 0
            self._queue.put((status, info))

    def wait_for_ack(self, timeout):
        try:
            return self._queue.get(timeout=timeout)
        except thread_queue.Empty:
            return None, None

    def stop(self):
        self._stop.set()
        self._thread.join(timeout=1.0)


def main():
    args = parse_args()

    if not os.path.isfile(args.firmware):
        print(f"ERROR: Firmware file not found: {args.firmware}")
        sys.exit(1)

    with open(args.firmware, "rb") as f:
        data = f.read()

    firmware_crc = crc16(data)
    firmware_size = len(data)

    bus = open_bus(args.channel, args.bitrate)
    drain_bus(bus)
    print(f"Opened gs_usb device channel {args.channel} at {args.bitrate} bps")

    ack_id = UPDATE_ACK_BASE + args.node_id
    receiver = CanReceiver(bus, ack_id)

    print(f"Sending UPDATE_START to node {args.node_id} size={firmware_size} crc=0x{firmware_crc:04X}")
    send_update_start(bus, args.node_id, firmware_size, firmware_crc, args.window)
    status, info = receiver.wait_for_ack(timeout=args.timeout)
    if status != CMD_UPDATE_ACK:
        print(f"ERROR: Node did not ACK start (status={status} info={info})")
        receiver.stop()
        bus.shutdown()
        sys.exit(1)

    PACKET_SIZE = 6  # 8-byte CAN frame minus 2-byte sequence header
    packets = [data[i:i + PACKET_SIZE] for i in range(0, firmware_size, PACKET_SIZE)]
    total_packets = len(packets)

    # Alle CAN-Nachrichten vorab aufbauen: verhindert bytearray/Message-Allokation im heißen Pfad
    can_messages = []
    for i, chunk in enumerate(packets):
        payload = bytearray(2 + len(chunk))
        payload[0] = (i >> 8) & 0xFF
        payload[1] = i & 0xFF
        payload[2:] = chunk
        can_messages.append(format_message(UPDATE_DATA_ID, bytes(payload)))

    sent_bytes = 0
    seq = 0
    pending_packets = 0
    t_start = time.monotonic()
    print_progress(sent_bytes, firmware_size, seq, total_packets)
    while seq < total_packets:
        bus.send(can_messages[seq])
        sent_bytes += len(packets[seq])
        seq += 1
        pending_packets += 1
        if seq % 32 == 0 or seq == total_packets:
            elapsed = time.monotonic() - t_start
            kbps = (sent_bytes / elapsed / 1024) if elapsed > 0 else 0
            print(f"Progress: {sent_bytes}/{firmware_size} bytes ({100*sent_bytes/firmware_size:.1f}%), "
                  f"pkt {seq}/{total_packets}, {kbps:.1f} KB/s", end='\r', flush=True)

        is_last_packet = seq == total_packets
        if pending_packets >= args.window or is_last_packet:
            status, info = receiver.wait_for_ack(timeout=args.timeout)
            if status is None:
                print(f"\nWARNING: no ACK for packet window ending at {seq-1} after {args.timeout}s, waiting 5s more...")
                status, info = receiver.wait_for_ack(timeout=5.0)
            if status != CMD_UPDATE_ACK:
                print(f"\nERROR: Packet window ending at {seq-1} not ACKed (status={status} info={info})")
                receiver.stop()
                bus.shutdown()
                sys.exit(1)
            pending_packets = 0
        if args.delay:
            time.sleep(args.delay)

    print("\nSending UPDATE_END")
    send_update_end(bus, args.node_id)
    status, info = receiver.wait_for_ack(timeout=10.0)
    if status != CMD_UPDATE_ACK:
        print(f"ERROR: End not ACKed (status={status} info={info})")
        receiver.stop()
        bus.shutdown()
        sys.exit(1)

    print("Firmware upload completed successfully.")
    receiver.stop()
    bus.shutdown()


if __name__ == "__main__":
    main()
