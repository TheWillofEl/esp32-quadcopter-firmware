#!/usr/bin/env python3
"""Ground station: connects to the drone's Wi-Fi AP and plots its live
telemetry (roll, pitch, battery voltage) in real time.

How this talks to the drone: the ESP32 firmware creates its own Wi-Fi
access point (see firmware/drone_firmware/telemetry.h) and broadcasts a
plain-text UDP packet ~20 times per second, formatted like:

    roll=1.23,pitch=-0.45,batt=3.98,hz=412

This script is a UDP client for that format. It's a from-scratch
implementation of a simple point-to-point protocol Ahmed designed for
this board — it is NOT cflib (Bitcraze's Crazyflie Python library) and
does not implement the Crazyflie/CRTP wire protocol, deliberately, to
keep this codebase unencumbered by cflib's GPL license. See the top-level
README's Credits section and HANDOFF_DRONE_FIRMWARE.md section 1 for why
that distinction matters.

Usage:
    1. Connect your computer's Wi-Fi to the drone's AP first
       (SSID/password are in firmware/drone_firmware/telemetry.h).
    2. python tools/ground_station.py
    3. Send one UDP packet to the drone once, so it learns your address
       and replies directly instead of broadcasting (this script does
       that automatically on startup).
"""

import socket
import sys
import time
from collections import deque

import matplotlib.pyplot as plt
import matplotlib.animation as animation

DRONE_IP = "192.168.4.1"       # ESP32 Arduino core's default AP gateway IP
TELEMETRY_PORT = 4210
HISTORY_SECONDS = 10
EXPECTED_RATE_HZ = 20           # matches TELEMETRY_INTERVAL_MILLIS in telemetry.h
HISTORY_LEN = HISTORY_SECONDS * EXPECTED_RATE_HZ


def parse_packet(raw: bytes) -> dict:
    """Parses 'roll=1.23,pitch=-0.45,batt=3.98,hz=412' into a dict of floats."""
    text = raw.decode("utf-8", errors="replace")
    fields = {}
    for part in text.split(","):
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        try:
            fields[key] = float(value)
        except ValueError:
            pass
    return fields


def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(0.5)

    # Say hello once so the drone learns our address and stops broadcasting.
    sock.sendto(b"hello", (DRONE_IP, TELEMETRY_PORT))
    sock.bind(("0.0.0.0", TELEMETRY_PORT))

    times = deque(maxlen=HISTORY_LEN)
    rolls = deque(maxlen=HISTORY_LEN)
    pitches = deque(maxlen=HISTORY_LEN)
    batteries = deque(maxlen=HISTORY_LEN)

    fig, (ax_angle, ax_batt) = plt.subplots(2, 1, figsize=(9, 6), sharex=True)
    fig.suptitle("drone-firmware live telemetry")

    line_roll, = ax_angle.plot([], [], label="roll (deg)")
    line_pitch, = ax_angle.plot([], [], label="pitch (deg)")
    ax_angle.set_ylabel("degrees")
    ax_angle.set_ylim(-90, 90)
    ax_angle.legend(loc="upper right")
    ax_angle.grid(True)

    line_batt, = ax_batt.plot([], [], color="tab:red", label="battery (V)")
    ax_batt.set_ylabel("volts")
    ax_batt.set_xlabel("seconds")
    ax_batt.set_ylim(3.0, 4.3)
    ax_batt.axhline(3.3, color="tab:orange", linestyle="--", linewidth=1,
                     label="min safe (3.3V)")
    ax_batt.legend(loc="upper right")
    ax_batt.grid(True)

    start_time = time.time()
    last_packet_time = [start_time]

    def poll_and_update(_frame):
        # Drain every packet waiting in the socket buffer, not just one,
        # so the plot doesn't fall behind if matplotlib's redraw is slow.
        got_any = False
        while True:
            try:
                raw, _addr = sock.recvfrom(256)
            except socket.timeout:
                break
            except BlockingIOError:
                break
            fields = parse_packet(raw)
            if "roll" not in fields or "pitch" not in fields:
                continue
            got_any = True
            now = time.time() - start_time
            times.append(now)
            rolls.append(fields.get("roll", 0.0))
            pitches.append(fields.get("pitch", 0.0))
            batteries.append(fields.get("batt", 0.0))
            last_packet_time[0] = time.time()

        if got_any:
            line_roll.set_data(times, rolls)
            line_pitch.set_data(times, pitches)
            line_batt.set_data(times, batteries)
            if times:
                ax_angle.set_xlim(max(0, times[-1] - HISTORY_SECONDS), times[-1] + 0.5)

        if time.time() - last_packet_time[0] > 2.0:
            fig.suptitle("drone-firmware live telemetry  [NO PACKETS - check Wi-Fi connection]")
        else:
            fig.suptitle("drone-firmware live telemetry")

        return line_roll, line_pitch, line_batt

    ani = animation.FuncAnimation(fig, poll_and_update, interval=50, blit=False)
    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
