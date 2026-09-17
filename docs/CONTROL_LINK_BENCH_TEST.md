# Control link bench test procedure — Phase 5

**PROPS OFF for steps 1-2. Board clamped/restrained for step 3.** Do this
only after `docs/MOTOR_BENCH_TEST.md` and `docs/SELF_LEVEL_BENCH_TEST.md`
have both already been completed successfully over USB serial. This test
proves the same self-level behavior still works when driven over Wi-Fi
instead of the serial console — it is not a substitute for either earlier
test, it's the next layer on top.

## Setup

1. Flash `firmware/drone_firmware/drone_firmware.ino`.
2. Remove all four propellers (steps 1-2), clamp the board.
3. `pip install -r tools/requirements.txt`
4. Connect your computer's Wi-Fi to the drone's AP (SSID/password in
   `firmware/drone_firmware/telemetry.h`).
5. Optionally also keep a serial monitor open in parallel
   (`arduino-cli monitor -p COM<N> -c baudrate=115200`) — useful for
   watching FAILSAFE/BATTERY CUTOFF prints, and as an emergency disarm
   path (`disarm` over serial always works, even if Wi-Fi misbehaves —
   see the header comment in `drone_firmware.ino` for why that's
   deliberate).

## Step 1 — packets are received at all

1. `python tools/pilot_control.py`
2. Without touching any keys yet, confirm the script is sending (it
   prints nothing per-packet by design — the 20Hz send loop is silent
   unless you press a key). If you have the serial monitor open, nothing
   should print either — a valid but unremarkable packet doesn't log
   anything by default.
3. Optionally run `tools/ground_station.py` in a second terminal at the
   same time — once `pilot_control.py` sends its first packet, the drone
   learns the pilot's address (`control_link.h` -> `telemetry.setGroundStation()`)
   and starts replying with telemetry to that same address, so
   `ground_station.py`'s plot should start moving. This confirms the
   round trip: pilot app -> drone -> telemetry back to the same machine.

## Step 2 — sign and arming check (props off)

1. In `pilot_control.py`: press SPACE to arm (throttle is 0% at this
   point, so nothing will spin even once props are on).
2. Press `w` a few times to raise throttle to ~10-15%; watch
   `ground_station.py`'s battery/roll/pitch plot or a serial `status` to
   confirm the drone shows `armed=YES self_level=YES`.
3. Use the arrow keys to move the roll/pitch target a few degrees and
   physically tilt the board to match — the correction behavior should
   match what was already verified in `docs/SELF_LEVEL_BENCH_TEST.md`,
   just now driven by a nonzero *target* angle instead of a hand-tilted
   *current* angle. Both are the same PID error (target - actual); this
   step is mainly confirming the setpoint actually reaches
   `flight_controller.h` (i.e. `control_link.h`'s parsing and
   `targetRollDeg()`/`targetPitchDeg()` wiring are correct), not
   re-deriving the sign convention from scratch.
4. Press `a` to disarm. Confirm `status` (serial) shows
   `armed=no self_level=no` immediately.

## Step 3 — failsafe over Wi-Fi (props off)

1. Arm and set a small throttle as in Step 2.
2. Kill `pilot_control.py` with Ctrl+C. The script's `finally` block
   sends one last disarm packet on exit — but the real test is what
   happens if it DIDN'T get the chance to (a crash, a Wi-Fi drop, walking
   out of range). To test that specifically:
3. Re-arm, then turn off your computer's Wi-Fi (or physically move out of
   AP range) instead of closing the script cleanly. Watch the serial
   monitor: within `Motors::FAILSAFE_TIMEOUT_MILLIS` (500ms) of the last
   packet actually reaching the drone, it should print `FAILSAFE: no
   command received in time. Motors disarmed.` — same mechanism, same
   500ms number, as the Phase 3 serial failsafe test. This is the
   scenario the failsafe actually exists for (a real comms loss), not
   just a clean script exit.

## Step 4 — first powered test (props ON, board restrained)

Only after Steps 1-3 all look correct AND the equivalent serial-driven
step in `docs/SELF_LEVEL_BENCH_TEST.md` has already been done safely.
Same restraint requirements as that document's Step 3 — firmly mounted or
weighted down, low throttle, ready to hit `a` (or serial `disarm`)
immediately if anything looks wrong.

## Known limitations, honestly, as of Phase 5

- `pilot_control.py` is a bench-test keyboard tool, not a real pilot
  interface — no rate limiting beyond the fixed 20Hz send, no packet-loss
  detection on the pilot side, no acknowledgement that a command was
  actually applied (only telemetry coming back separately implies that).
- Roll/pitch setpoints are capped at ±30° in the tool as a bench-test
  safety margin, not because the firmware enforces that range — nothing
  in `control_link.h` clamps the setpoint itself, only the PID output
  that results from it (see `flight_controller.h`'s output limits).
- No encryption/authentication beyond the Wi-Fi AP's WPA2 password —
  anyone who knows the SSID/password (or the AP is left at defaults, see
  `telemetry.h`) could send control packets. Acceptable for a bench-test/
  hobby project flown somewhere controlled; would need real hardening
  before flying anywhere that mattered.
- Still no yaw, altitude, or position control — same limitations as
  Phase 4, carried through unchanged.
