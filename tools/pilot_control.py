#!/usr/bin/env python3
"""Pilot control sender: a minimal keyboard-driven ground-control app that
sends arm/throttle/roll/pitch commands to the drone's Wi-Fi control link
(see firmware/drone_firmware/control_link.h).

THIS IS A BENCH-TEST / DEVELOPMENT TOOL, not a finished pilot interface —
it exists to exercise control_link.h and the self-level loop end-to-end
over Wi-Fi before any real flight, using a keyboard because that's more
predictable and repeatable for testing than a joystick/gamepad mapping
would be. See docs/CONTROL_LINK_BENCH_TEST.md for the required test
procedure (props off first, same discipline as every other bench test in
this repo).

Wire format: comma-separated key=value UDP packets, matching the style of
ground_station.py's incoming telemetry packets (see control_link.h's
header comment for the full field list). This is Ahmed's own protocol,
not cflib/CRTP — see the top-level README's Credits section for why that
distinction is kept deliberately.

Controls (while the terminal window has focus):
  SPACE       - arm (only takes effect while held with a throttle set)
  a           - disarm immediately
  w / s       - increase / decrease base throttle by 1%
  arrow keys  - adjust roll/pitch target by 1 degree (small range only —
                this is a bench test, not real stick travel)
  0           - re-center roll/pitch target to 0,0
  q           - quit (sends a final disarm packet first)

Sends a command packet at a steady 20Hz regardless of whether anything
changed, matching control_link.h's requirement that the comms failsafe
needs a steady stream of packets to keep resetting (see that file's
header comment) — a keyboard app that only sends on keypress would let
the drone auto-disarm every time the pilot's hand is idle on the stick,
which is wrong.

Usage:
    1. Connect your computer's Wi-Fi to the drone's AP first
       (SSID/password are in firmware/drone_firmware/telemetry.h).
    2. pip install -r tools/requirements.txt
    3. python tools/pilot_control.py
"""

import socket
import sys
import time

DRONE_IP = "192.168.4.1"
CONTROL_PORT = 4210  # same port as telemetry.h — one socket, both directions
SEND_HZ = 20
SEND_INTERVAL = 1.0 / SEND_HZ

THROTTLE_STEP_PCT = 1.0
ANGLE_STEP_DEG = 1.0
MAX_ANGLE_DEG = 30.0  # matches control_link.h's documented "small range expected"


class State:
    def __init__(self):
        self.armed = False
        self.throttle_pct = 0.0
        self.roll_deg = 0.0
        self.pitch_deg = 0.0

    def packet(self) -> bytes:
        return (
            f"arm={1 if self.armed else 0},"
            f"thr={self.throttle_pct:.0f},"
            f"roll={self.roll_deg:.1f},"
            f"pitch={self.pitch_deg:.1f}"
        ).encode("ascii")


def read_key_windows():
    import msvcrt
    if not msvcrt.kbhit():
        return None
    ch = msvcrt.getch()
    if ch in (b"\x00", b"\xe0"):  # arrow key prefix
        ch2 = msvcrt.getch()
        return {b"H": "UP", b"P": "DOWN", b"K": "LEFT", b"M": "RIGHT"}.get(ch2)
    try:
        return ch.decode("ascii")
    except UnicodeDecodeError:
        return None


def read_key_unix():
    import select
    import termios
    import tty
    if select.select([sys.stdin], [], [], 0)[0]:
        ch = sys.stdin.read(1)
        if ch == "\x1b":
            ch2 = sys.stdin.read(1)
            ch3 = sys.stdin.read(1)
            return {"A": "UP", "B": "DOWN", "D": "LEFT", "C": "RIGHT"}.get(ch3)
        return ch
    return None


def main():
    is_windows = sys.platform.startswith("win")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    state = State()

    print(__doc__)
    print(f"Sending to {DRONE_IP}:{CONTROL_PORT} at {SEND_HZ}Hz.")
    print("PROPS OFF unless you have completed docs/CONTROL_LINK_BENCH_TEST.md.\n")

    old_settings = None
    if not is_windows:
        import termios
        import tty
        fd = sys.stdin.fileno()
        old_settings = termios.tcgetattr(fd)
        tty.setcbreak(fd)

    try:
        next_send = time.time()
        while True:
            key = read_key_windows() if is_windows else read_key_unix()

            if key == "q":
                break
            elif key == " ":
                state.armed = True
                print(f"ARM requested. throttle={state.throttle_pct:.0f}%")
            elif key == "a":
                state.armed = False
                print("DISARM sent.")
            elif key == "w":
                state.throttle_pct = min(100.0, state.throttle_pct + THROTTLE_STEP_PCT)
                print(f"throttle={state.throttle_pct:.0f}%")
            elif key == "s":
                state.throttle_pct = max(0.0, state.throttle_pct - THROTTLE_STEP_PCT)
                print(f"throttle={state.throttle_pct:.0f}%")
            elif key == "UP":
                state.pitch_deg = max(-MAX_ANGLE_DEG, state.pitch_deg - ANGLE_STEP_DEG)
                print(f"pitch={state.pitch_deg:.1f} roll={state.roll_deg:.1f}")
            elif key == "DOWN":
                state.pitch_deg = min(MAX_ANGLE_DEG, state.pitch_deg + ANGLE_STEP_DEG)
                print(f"pitch={state.pitch_deg:.1f} roll={state.roll_deg:.1f}")
            elif key == "LEFT":
                state.roll_deg = max(-MAX_ANGLE_DEG, state.roll_deg - ANGLE_STEP_DEG)
                print(f"pitch={state.pitch_deg:.1f} roll={state.roll_deg:.1f}")
            elif key == "RIGHT":
                state.roll_deg = min(MAX_ANGLE_DEG, state.roll_deg + ANGLE_STEP_DEG)
                print(f"pitch={state.pitch_deg:.1f} roll={state.roll_deg:.1f}")
            elif key == "0":
                state.roll_deg = 0.0
                state.pitch_deg = 0.0
                print("roll/pitch re-centered to 0,0")

            now = time.time()
            if now >= next_send:
                sock.sendto(state.packet(), (DRONE_IP, CONTROL_PORT))
                next_send = now + SEND_INTERVAL
            else:
                time.sleep(min(0.005, next_send - now))
    finally:
        # Always send a final disarm, even on Ctrl+C or an exception —
        # leaving the drone armed because the pilot app crashed would
        # defeat the whole point of the comms failsafe existing.
        state.armed = False
        state.throttle_pct = 0.0
        sock.sendto(state.packet(), (DRONE_IP, CONTROL_PORT))
        if old_settings is not None:
            import termios
            termios.tcsetattr(sys.stdin.fileno(), termios.TCSADRAIN, old_settings)
        print("\nFinal disarm packet sent. Exiting.")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
