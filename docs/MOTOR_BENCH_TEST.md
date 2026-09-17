# Motor bench test procedure — Phase 3

**PROPS OFF. BOARD CLAMPED OR RESTRAINED. Every time, no exceptions.**
These are small brushed motors, not brushless-with-ESC — even "low
throttle" can make an unrestrained board scoot across a table.

Do this before ever attempting Phase 4 (closed-loop flight). The goal is
to prove, one motor at a time, that:
1. Each `MOT_1`-`MOT_4` net actually spins the motor you expect (matches
   the confirmed arm layout in `docs/PINMAP.md`),
2. it spins the direction you expect (CW/CCW per the confirmed layout),
3. the arming sequence and failsafe actually work before you ever trust
   them in the air.

## Setup

1. Flash `firmware/drone_firmware/drone_firmware.ino` (see
   `docs/TOOLCHAIN.md` for the exact compile/upload commands).
2. **Remove all four propellers.**
3. Clamp or otherwise restrain the board so it cannot move.
4. Connect a serial monitor at 115200 baud:
   ```
   arduino-cli monitor -p COM<N> -c baudrate=115200
   ```
5. You should see the normal boot log (MPU-6050 init, Wi-Fi AP started,
   "Setup complete"), followed by the serial console's ready message.

## Commands

Type these into the serial monitor, one per line:

| Command | What it does |
|---|---|
| `arm` | Arms the motors. Required before any `mot` command has an effect. |
| `mot <1-4> <0-100>` | Sets one motor (1-4, matching MOT_1-MOT_4) to a throttle percent. Internally clamped to `Motors::MAX_THROTTLE_DURING_DEVELOPMENT` (35%) regardless of what you type. |
| `stop` | All motors to zero, stays armed. |
| `disarm` | Immediate disarm, motors to zero. |
| `status` | Prints armed state, battery voltage, current roll/pitch. |

## Test sequence

1. `status` — confirm battery voltage looks sane (should read roughly
   half your multimeter reading on the raw divider, or the real value
   once `BatteryMonitor::calibrate()` has been used — see `battery.h`).
2. `arm`
3. `mot 1 15` — MOT_1 should spin. Per the confirmed layout, this is the
   **Rear-Left** motor, spinning **CCW** (viewed from above). Confirm both.
4. `stop`
5. Repeat step 3 for `mot 2 15` (**Front-Left, CW**), `mot 3 15`
   (**Front-Right, CCW**), `mot 4 15` (**Rear-Right, CW**) — one at a
   time, confirming physical position and direction each time against
   `docs/PINMAP.md` section "Motor / arm layout."
6. **Failsafe test:** `arm`, then `mot 1 15`, then wait — do NOT send
   another command. After `Motors::FAILSAFE_TIMEOUT_MILLIS` (500ms), the
   serial monitor should print `FAILSAFE: no command received in time.
   Motors disarmed.` and MOT_1 should stop on its own. This proves the
   failsafe fires even with nothing else going wrong — it's the same
   mechanism that will protect against a dropped Wi-Fi link in Phase 5.
7. **Battery cutoff test:** only meaningful with a genuinely low battery
   — do not force this by disconnecting things while a motor is
   spinning. If/when the battery is naturally low, `status` should show
   a voltage under 3.3V and any `mot` command should refuse to spin
   (stays disarmed, `BATTERY CUTOFF` printed).

## If a motor spins the wrong direction

A brushed DC motor's spin direction on this board is fixed by the
physical motor wiring into the connector — the firmware's low-side
MOSFET switch cannot reverse it (there's no H-bridge here, just an
on/off switch to ground). If a motor spins the wrong way, that's a
hardware fact (swap the two motor wires) — reflect it in
`docs/PINMAP.md` if the physical layout ever needs re-confirming, but
don't try to "fix" it in firmware; there is nothing firmware can do
about direction with this driver circuit.

## If a motor doesn't spin at all

Check, in order: correct MOT_n net per `docs/PINMAP.md`, gate pulldown
resistor (R22-R25) not damaged, flyback diode (D2-D5) not installed
backwards, motor solder joints, `+BATT` actually present at the motor's
bulk cap (C15-C18). This is a hardware debugging checklist, not a
firmware one — if the PWM signal is present at the MOSFET gate
(verifiable with a multimeter/scope on duty cycle) and the motor still
doesn't spin, the fault is downstream of the code.
