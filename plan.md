# PROJECT-SENTINEL: Interactive Solar System Mission + Butter-Smooth Rendering

## Overview
Transform the 1D vertical rocket sim into an interactive solar system mission experience where users select a destination, watch a launch from Earth's surface, and track the rocket across space — all with butter-smooth 60fps rendering via physics-based dead-reckoning.

---

## Phase 1: Butter-Smooth Rendering (Fix Choppiness)

### Problem
Current interpolation (viewer.html:1192-1204) uses exponential smoothing on top of physics prediction, creating sluggish, laggy movement. EKF telemetry arrives at ~20Hz (50Hz sensor, batched) but SSE delivery is bursty.

### Solution: Dead-Reckoning with Snap-Correct

**In `viewer.html` animation loop:**

1. **Replace exponential smoothing with kinematic dead-reckoning:**
   - On each EKF update: store `{alt, vel, accel, timestamp}` as the "anchor state"
   - On each animation frame: compute `dt = now - anchor.timestamp`, then:
     - `displayAlt = anchor.alt + anchor.vel * dt + 0.5 * anchor.accel * 9.81 * dt²`
     - `displayVel = anchor.vel + anchor.accel * 9.81 * dt`
   - Clamp `displayAlt >= 0`

2. **Snap-correction on new data:**
   - When new EKF arrives, compute the prediction error (predicted vs actual)
   - If error is small (<2m), just update the anchor (seamless)
   - If error is large, apply a brief 150ms lerp correction to avoid visual jump

3. **Decouple graph updates from SSE events:**
   - Push interpolated values into graph history at fixed intervals (e.g., 10Hz) instead of only on EKF packets
   - This makes graphs smoother too

4. **Remove the `smoothFactor` exponential decay** — it's the main source of sluggishness

---

## Phase 2: Interactive Solar System Destination Picker

### UI Changes in `viewer.html`

1. **Add a pre-launch "Mission Select" overlay:**
   - Appears on page load before telemetry starts
   - Shows a stylized solar system map (2D top-down orbital view)
   - Destinations: Moon, Mars, Jupiter (each with distance, travel time, delta-v shown)
   - Clicking a destination selects it and configures the mission profile
   - "INITIATE LAUNCH" button arms the system

2. **Mission profiles (defined in JS):**
   ```
   Moon:    { thrust_g: 3.0, boost_s: 12, coast_s: 15, total_s: 45 }
   Mars:    { thrust_g: 2.5, boost_s: 15, coast_s: 25, total_s: 60 }
   Jupiter: { thrust_g: 4.0, boost_s: 20, coast_s: 35, total_s: 80 }
   ```
   These are display-side only — the firmware sim stays as-is, but the UI scales/labels the telemetry to match the selected mission narrative.

3. **Style:** Match existing SpaceX/Palantir aesthetic — black background, ice-blue accents, JetBrains Mono, minimal borders.

---

## Phase 3: Launch-from-Earth Visualization

### Replace the flat side-view with a layered Earth-to-Space scene

1. **Scene layers (bottom to top):**
   - **Earth surface:** Curved horizon line with gradient (dark ground → atmosphere glow)
   - **Atmosphere bands:** Troposphere (blue-ish), Stratosphere (fading), Mesosphere (dark), Space (pure black + stars)
   - **Rocket:** Current rocket drawing, positioned based on altitude
   - **Destination indicator:** Small dot/icon showing target planet in the far distance

2. **Camera behavior:**
   - At low altitude (< 1km): tight zoom on pad, Earth fills bottom 60% of viewport
   - As altitude increases: camera zooms out, Earth curves becomes visible
   - At high altitude: Earth shrinks to a sphere in the corner, rocket is in open space
   - This is purely a viewport/scale transformation — the rocket's Y position is still driven by `displayAlt`

3. **Visual atmosphere transition:**
   - Sky gradient behind the rocket transitions from dark blue → black as altitude increases
   - Stars fade in as atmosphere thins
   - Earth glow appears as a thin blue line along the curved horizon

4. **Earth rendering (Canvas 2D):**
   - Draw Earth as a large arc at the bottom of the viewport
   - Radius scales with zoom level
   - Blue atmospheric haze along the limb
   - Dark landmass silhouette (simple geometric shapes, not detailed maps)

---

## Phase 4: Enhanced Flight Phases for Interplanetary Missions

### UI-side mission phase mapping

The firmware still runs its IDLE→ARMED→BOOST→COAST→DESCENT→LANDED state machine unchanged. The viewer maps these to mission-appropriate labels:

| Firmware State | Default Label | Moon Mission | Mars Mission |
|---|---|---|---|
| IDLE | IDLE | PRE-LAUNCH | PRE-LAUNCH |
| ARMED | ARMED | COUNTDOWN | COUNTDOWN |
| BOOST | BOOST | ASCENT | ASCENT |
| COAST | COAST | TRANS-LUNAR INJECTION | TRANS-MARS INJECTION |
| DESCENT | DESCENT | LUNAR APPROACH | MARS APPROACH |
| LANDED | LANDED | LUNAR ORBIT | MARS ORBIT |

This is a pure UI mapping — no firmware changes needed for this.

### Countdown sequence
- When mission is selected and user clicks "INITIATE LAUNCH", show a T-10 countdown overlay
- The firmware auto-arms at 2s, so we time the UI countdown to sync with that

---

## Phase 5: Abort Sequencer (Firmware + UI)

### Firmware changes

1. **New file: `src/app/abort_mgr.h` and `abort_mgr.c`**
   - Abort modes: `ABORT_PAD`, `ABORT_FLIGHT`, `ABORT_NONE`
   - `abort_trigger(abort_mode_t mode)` — callable from fault_mgr or cmd_handler
   - On abort: immediately transition FSM to DESCENT (safe mode), log fault event, send abort telemetry packet

2. **New telemetry message: `MSG_ABORT` (0x0E)**
   - Payload: `{abort_mode, trigger_source, tick_ms, accel_z_mg}`

3. **Integration points:**
   - `cmd_handler.c`: Add `CMD_ABORT (0x08)` command
   - `fault_mgr.c`: Auto-trigger abort on CRITICAL severity faults
   - `flight_sm.c`: Accept forced transition to DESCENT from abort_mgr

4. **New command from ground station:**
   - Add ABORT button to viewer UI
   - Wire through launch_viewer.py command endpoint

### UI changes
- Red "ABORT" button in topbar (only visible during active flight)
- On abort: flash red overlay, show "ABORT ABORT ABORT" in mission-critical style
- Flight state badge turns red with abort mode label

---

## Phase 6: Destination Arrival Visualization

1. **As the mission progresses through COAST → DESCENT → LANDED:**
   - The destination planet grows from a dot to a visible sphere
   - Camera transitions to show approach
   - On "LANDED" (arrival), show a brief celebration animation (subtle particle effect)

2. **Mission summary panel:**
   - After LANDED, show a compact mission summary overlay:
     - Total flight time, max altitude, max velocity
     - Abort status (nominal / aborted)
     - Mission rating (just for fun)

---

## File Change Summary

| File | Changes |
|---|---|
| `tools/viewer.html` | Dead-reckoning interpolation, mission select overlay, Earth-to-space scene, phase labels, abort button, destination visualization, mission summary |
| `tools/launch_viewer.py` | Decode new MSG_ABORT, forward abort command |
| `src/app/abort_mgr.h` | New — abort mode types, public API |
| `src/app/abort_mgr.c` | New — abort sequencer logic |
| `src/app/cmd_handler.c` | Add CMD_ABORT handling |
| `src/app/flight_sm.h` | Add `flight_sm_force_descent()` for abort override |
| `src/app/flight_sm.c` | Implement force_descent, integrate abort trigger |
| `src/app/fault_mgr.c` | Auto-abort on CRITICAL faults |
| `src/app/tlm_frame.h` | Add TLM_MSG_ABORT message ID |
| `CMakeLists.txt` | Add abort_mgr.c to sources |

---

## Implementation Order

1. **Smooth rendering** — immediate visual improvement, validates the animation pipeline
2. **Earth-to-space scene** — the "wow factor" visual upgrade
3. **Mission select overlay** — interactivity layer
4. **Phase label mapping** — narrative layer
5. **Abort sequencer** — firmware depth + safety-critical thinking
6. **Destination arrival + mission summary** — polish

This order ensures we always have a working, visually impressive state at each step — important for the YC session where the journey matters as much as the destination.
