# Self-level bench test procedure — Phase 4

**PROPS OFF for steps 1-2. Board clamped or restrained for step 3.**
Do this only after `docs/MOTOR_BENCH_TEST.md` has already been completed
successfully (motor identity/direction and the failsafe are proven first —
this test builds on top of that, not instead of it).

Goal: prove the self-level control loop (`flight_controller.h`) reacts in
the *correct direction* before it's ever allowed to spin a prop, then
prove it produces a *reasonable magnitude* of correction, in that order.

## Commands (in addition to the Phase 3 set)

| Command | What it does |
|---|---|
| `level <0-100>` | Arms and enters self-level mode at the given base throttle percent. The PID/mixer loop takes over motor commands — `mot` is disabled until `manual`. |
| `manual` | Leaves self-level mode, motors zeroed, `mot` works again. |
| `status` | Now also prints `self_level=` and, when active, all four motors' commanded throttle percent. |

## Step 1 — sign check at zero throttle (props off)

1. Flash, remove all props, clamp the board (motors will not spin in this
   step regardless, but keep the habit).
2. `level 0` — base throttle 0%, so motors report a throttle but won't
   actually reach a spinning duty cycle.
3. `status` repeatedly while gently tilting the board BY HAND (roll it
   right, then left; pitch nose-down, then nose-up). Watch the four motor
   percentages printed by `status`.
4. Expected, per `mixer.h`'s convention and the confirmed arm layout:
   - Tilt **right** (roll right) → MOT_1 (Rear-Left) and MOT_2 (Front-Left)
     percentages should read HIGHER than MOT_3/MOT_4.
   - Tilt **nose-down** (pitch forward) → MOT_1 (Rear-Left) and MOT_4
     (Rear-Right) should read HIGHER than MOT_2/MOT_3.
   - Release (board level) → all four should settle back toward roughly
     equal (near 0%, since base throttle is 0 here).
5. If any direction is backwards, **do not touch the mixing signs to
   "make the demo look right."** Work backward instead: confirm the
   physical tilt direction and the confirmed arm layout in
   `docs/PINMAP.md` are exactly what you think they are, then check
   `complementary_filter.h`'s roll/pitch sign convention against the
   IMU's actual mounting orientation on this board (not yet independently
   verified on hardware — this is exactly the kind of thing this test is
   for). A sign flip here is a real bug to find, not a magnitude to tune.

## Step 2 — magnitude sanity check (props off)

1. Still with props off, try `level 15`, `level 25` (base throttle 15%,
   25%). Repeat the tilt-and-watch from Step 1.
2. Confirm the correction is proportionate — a small tilt should produce
   a small throttle spread between opposing motors, a bigger tilt a
   bigger spread, and no motor should be reported pinned at 0% or 100%
   from a moderate hand tilt. If corrections look violent or barely
   perceptible, that's a gain-tuning question for Step 3, not something
   to fix by guessing at numbers now — `flight_controller.h`'s PID gains
   are documented starting points, not final values.
3. `manual` to leave self-level mode when done.

## Step 3 — first powered test (props ON, board restrained)

**Only after Steps 1-2 both look correct.** This step is the first time
in the whole project a prop spins under closed-loop control instead of a
fixed manual command.

1. Reattach props.
2. Firmly restrain the board — mounted to a stand or heavily weighted
   down, such that even a large unexpected reaction cannot make it come
   loose or flip. This step assumes the board CAN move slightly (that's
   how you'll see a response) but cannot escape the restraint.
3. `level 10` (start as low as the motors will reliably spin at).
4. Observe: does the airframe visibly resist a hand-applied tilt (like a
   spring pushing back), or does it overshoot/oscillate, or does it do
   nothing? Feed this observation into gain tuning per the TODO at the
   bottom of `flight_controller.h` (raise `kp` first if the response is
   too weak, add `kd` if it overshoots/oscillates, only add `ki` if
   there's a persistent steady-state lean that proportional correction
   alone won't remove).
5. `manual` or `disarm` immediately if anything looks wrong — both are
   safe at any point (disarm forces zero duty; leaving self-level mode
   without disarming stops the PID loop but the comms failsafe is still
   live from whatever last fed it).

## Reminders carried over from Phase 3

- The 500ms comms failsafe is still live in self-level mode — `status`
  (or any command) counts as feeding it, same as `mot` did. If testing
  goes quiet for over 500ms the motors auto-disarm; that's expected, not
  a bug.
- The battery cutoff (3.3V) and the 35% development throttle ceiling both
  still apply unconditionally, regardless of what the PID loop computes.
- Nothing here tests yaw, altitude, or position — see
  `flight_controller.h`'s header comment for why those are out of scope
  this phase.
