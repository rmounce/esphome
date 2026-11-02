# Overview

This component communicates with Midea-like air conditioners (heat pumps) via the XYE protocol over RS485.
Kudos to these projects:
- Reverse engineering of the protocol: https://codeberg.org/xye/xye
- Working implementation using ESP32: https://github.com/Bunicutz/ESP32_Midea_RS485
- Fully integrated Midea Climate component: https://github.com/esphome/esphome/tree/dev/esphome/components/midea

To get this to work you need an esphome configuration like this:

```yaml
esphome:
  name: heatpump
  friendly_name: Heatpump

esp8266:  #also works with esp32
  board: d1_mini

# Enable logging (but not via UART)
logger:
  baud_rate: 0

external_components:
  - source: 
      type: git
      url: https://github.com/exciton/esphome
      ref: dev
    components: [midea_xye]
  
# UART settings for RS485 covnerter dongle (required)
uart:
  tx_pin: TX
  rx_pin: RX
  baud_rate: 4800
  debug: #If you want to help reverse engineer
    direction: BOTH


# Main settings
climate:
  - platform: midea_xye
    name: Heatpump
    period: 1s                  # Optional. Defaults to 1s
    timeout: 100ms              # Optional. Defaults to 100ms
    use_fahrenheit: false           # Optional. Defaults to false.
    #beeper: true               # Optional. Beep on commands.
    visual:                     # Optional. Example of visual settings override.
      min_temperature: 17 °C    # min: 17
      max_temperature: 30 °C    # max: 30
      temperature_step: 1.0 °C  # min: 0.5
    supported_modes:            # Optional. 
      - FAN_ONLY
      - HEAT_COOL              
      - COOL
      - HEAT
      - DRY
    custom_fan_modes:           # Optional
      - SILENT
      - TURBO
    supported_presets:          # Optional. 
      - BOOST
      - SLEEP
    supported_swing_modes:      # Optional
      - VERTICAL
    outdoor_temperature:        # Optional. Outdoor temperature sensor
      name: Outside Temp
    temperature_2a:             # Optional. Inside coil temperature
      name: Inside Coil Inlet Temp
    temperature_2b:             # Optional. Inside coil temperature
      name: Inside Coil Outlet Temp
    temperature_3:             # Optional. Outside coil temperature
      name: Outside Coil Temp
    current:                    # Optional. Current measurement
      name: Current
    timer_start:                # Optional. On timer duration
      name: Timer Start
    timer_stop:                 # Optional. Off timer duration
      name: Timer Stop
    status_fresh:               # Optional. Valid C0 received within 30 seconds
      name: XYE Status Fresh
    defrost:                    # Optional. Protection flags bit 0x02 (not Error Flags)
      name: Defrost Active
    error_flags:                # Optional.
      name: Error Flags
    protect_flags:              # Optional. 
      name: Protect Flags
    fan_speed:                  # Optional. Legacy low-nibble label; not measured RPM
      name: Fan Speed

```

# What works
- Setting mode (off, auto, fan, cool, heat, dry).
- Setting temperature. Can send in C or F. Handles AC results in C or F. Must manually set in YAML.
- Setting fan mode (auto, low, med, high).
- Reading inside, outside air temperatures, inside coil temperature, and outside coil temperature.
- Reading timer start/stop times (set by remote)
- Follow-Me temperature. Point it at a sensor and this works well.

# What doesn't work
- Current reading always shows 255
- Setting swing mode 

# Not yet implemented
- Setting timers direct to unit. No real need since automations can do this 
- Figure out how to force display to C or F. Setting temp in C doesn't force display to C.
- Freeze protection
- Silent mode
- Lock/Unlock

# Not tested
- IR integration (However, not needed for Follow-Me)

# Requested fan and observed feedback

`climate.fan_mode` represents the requested setting: Auto, Low, Medium or
High. Every C3 packet uses separately stored command intent. C0 fan feedback
never changes it, including when feedback arrives after a partial climate
call is queued. Target, mode, preset and swing calls retain the fan request.
Pending target/preset/swing commands also survive intervening feedback.

Startup defaults to requested Auto without sending a C3 command. The device's
existing fan request cannot be recovered reliably from observed speed; after
restart, the first explicit partial command therefore uses Auto unless a fan
request has been supplied. Protocol HEAT_COOL is full-auto: entering it clears
a manual request to Auto, which remains Auto when subsequently leaving it.
OFF mode retains the request and the existing OFF command encoding; it does
not turn a zero feedback code into a new fan request. Fan Off is no longer
advertised as a selectable setting because C3 cannot request it here.
External controls' fan requests cannot be inferred from C0 feedback.

Optional diagnostic sensors publish decimal bytes (0–255):

```yaml
climate:
  - platform: midea_xye
    # ...existing settings...
    fan_feedback:
      name: C0 fan feedback byte
    fan_command:
      name: C3 fan command byte
```

`fan_feedback` preserves all of C0 byte 9, including the auto bit 0x80 and
unknown combinations. `fan_command` records C3 byte 7 only when actually
written to UART; it has no state until the first C3 transmission. Sensors
publish on first observation and value changes. DEBUG logs show each changed
C0 byte and each emitted C3 command, never each unchanged C0 poll.

The legacy `fan_speed` text sensor decodes only the low nibble: 0 → Off,
4 → Low, 2 → Medium, 1 → High, other values → Off. It ignores upper bits
and cannot represent unknown combinations. These are protocol labels,
not RPM, airflow or proof that the fan has stopped. In particular, zero
feedback has coincided with indoor electrical draw during heating.
`hvac_action` keeps its existing fan-code-based heating/idle derivation;
it is not independent evidence of compressor activity.

# Local adoption proposal and passive capture

Review the component commit before publication. Once approved and published,
replace the installation's moving `xye-units-switch` ref with the reviewed
full 40-character SHA. Do not use the old base SHA as a pin for this fix.
A proposed patch is in
`tests/components/midea_xye/installation-adoption.patch`; it is intentionally
unapplied and its SHA placeholder must be replaced before validation.
Keep the public ATOM baseline update separate. Preserve the existing local
inhibit callback and all installation-specific imports.

Add the two byte sensors alongside the installation's existing coil sensors,
static-pressure number, protect/error flags, defrost, compressor-status and
outdoor-fan-status entities. Component DEBUG logging includes bounded
transaction summaries (including routine polls); raw frames require VERBOSE:

```yaml
logger:
  logs:
    midea_xye: DEBUG
```

After a separately approved deployment, capture passively through the next
natural heating event; do not send fan/mode/pressure commands to provoke it:

```sh
uv run python -m esphome logs hvac-xye.yaml --device <existing-device-address> \
  | TZ=UTC awk '{ print strftime("%Y-%m-%dT%H:%M:%SZ"), $0; fflush(); }' \
  | tee xye-passive.log
```

Host receipt timestamps include transport latency; preserve original device
uptime timestamps too. Record capture start/end in UTC, firmware SHA,
requested mode/fan/target, C0/C3 full bytes, T1 return/T2A/T2B coil temperatures,
static-pressure value, protect/error flags and defrost/status entities.
Use existing Shelly indoor/outdoor power history on the same UTC timeline.
Align the C0 byte transitions with coil temperature and indoor-power changes;
report sample intervals, clock alignment and gaps. Pressure reports are a
configuration value, not a measured airflow/static-pressure trace.

For current state, query HA directly using the existing credential mechanism;
do not place credentials in this fixture, patch or capture notes. Use history
for retrospective correlation. Preserve raw codes and power traces; do not
infer RPM/airflow, stopped motion, compressor activity from action labels, or
fixed coil thresholds from these observations.

# Validation

From the ESPHome implementation worktree:

```sh
uv run --extra test pytest -q tests/components/midea_xye/test_requested_fan.py
uv run python -m esphome compile tests/components/midea_xye/fan_feedback.yaml
git diff --check
```

The native test builds the production `air_conditioner.cpp` with g++ and
runs parsing, queueing, deferred RX callbacks and actual UART writes against
small framework/I/O shims. It uses real climate enum definitions, not a
Python copy of the protocol logic. It does not exercise the real ESPHome
scheduler, ClimateCall validation/callback dispatch, hardware UART or a live
unit. The ESP32-S3 Arduino fixture compiles the real framework, diagnostics,
Fahrenheit switch and existing inhibit callback API, using local source and
no installation secrets. Neither validation requires heating availability.

`status_fresh` starts false and becomes true on an expected checksum-valid C0
reply. Each valid C0 resets the named 30-second scheduler timeout; expiry sets
it false even if polling has stopped. C3/C4/C6 acknowledgements and malformed
frames do not refresh it. Binary sensor deduplication sends only transitions;
no periodic heartbeat or force_update is needed. Consumers must also require
node connectivity before trusting the bit.

## Command transactions and pressure admission

- Each queued operation snapshots its mode, target, requested fan, preset,
  swing, follow-me temperature/beeper or pressure value when requested.
  FIFO order and intentional duplicate retransmissions are preserved; later
  requests and status parsing cannot rewrite older queued payloads.
- Follow-me initialization resets when a queued mode C3 is transmitted, rather
  than when a later mode request arrives. Initialization flags reflect preceding
  transmitted commands. Pressure packets retain the last transmitted follow-me
  temperature; that protocol context is deliberately evaluated at TX time.
- Queue bound:64 pending plus one active operation. Reject newest at capacity
  with a warning; do not silently overwrite payloads or coalesce retransmits.
- Consume every received byte with a non-wrapping length counter. Require
  exactly32 bytes, framing/destination/checksum and the outstanding request's
  opcode before parsing or publishing observations. C3 ACKs are validated too;
  ACK validity is not proof the requested physical operation has completed.
- Use configured response timeout (previously hard-coded100ms). A failed
  transaction has unknown delivery; do not automatically replay potentially
  non-idempotent ramp operations. Log failure, retain later FIFO operations,
  and return to polling after the queued work. Partial/extra/wrong replies do
  not silently discard the queue. Drain leftover RX before the next TX.
- Pressure requires OFF established by an expected, valid C0 within30s,
  with no newer mode request. Check permission at enqueue AND actual TX;
  cancel the individual pressure operation if invalidated. Expiry applies
  without an optional status-fresh sensor. Unknown mode codes never grant OFF.
  A query sent before a newer mode request cannot reauthorize pressure from
  its old OFF response. Reported climate OFF is still optimistic; the component
  gate uses separate C0 evidence, not that optimistic climate property.
- DEBUG summaries: device uptime milliseconds, command ID (0 for polling),
  queue depth, TX fields, expected/received opcode, length and validation result;
  pressure observations identify C4/C6 source and raw ambient byte. IDs are
  diagnostic only and do not exist on the wire. VERBOSE raw capture is limited
  to one16-byte TX and at most32 received bytes per transaction. Byte count
  still reports all received data. No unbounded frame history is retained.
- Enable VERBOSE in the firmware's logger configuration to compile raw capture;
  changing only runtime level cannot restore logging compiled out of a DEBUG
  build. Revert higher logging after a bounded passive capture.

Only C4 publishes extended temperature, setpoint and pressure observations. C6
is validated as an acknowledgement and then completes its transaction without
publishing sensor/climate state. Passive captures on the installation confirmed
C4 byte24 low nibble tracks both selected SP1 (`0x21`) and SP2 (`0x22`). Thus
pressure confirmation waits for a normal C4 poll rather than interpreting C6
ACK bytes as extended data. This also prevents a C6 byte21=`0x68` from creating
a spurious32°C ambient reading. It does not establish all C6 field semantics.

Limits: protocol has no transaction IDs; a late same-opcode reply arriving after
a newer request cannot be distinguished reliably. Fresh OFF does not establish
compressor coast-down completion. C4 temperature/pressure interpretation is
unchanged; actual C6 field semantics and the original ambient32°C cause remain
unverified. No new startup ramp, settling interval or airflow policy.

Native regression evidence: seventeen new scenarios fail on the previous
component and pass on these fixes. Expanded suite34 scenarios covers payload
snapshots, invalid/unexpected replies, stale OFF, mode generation, pressure
cancellation, init ordering, queue capacity, configured timeout, and RX lengths
0/1/31/33/64/300/512 and C6 ACK suppression with C4 observation recovery.
The preceding33-scenario transaction suite also passed undefined-behaviour sanitizer.
These tests do not substitute for passive hardware validation after an approved
firmware rollout.
