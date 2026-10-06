# Ducted-unit fan field observations, 6–7 October 2026

These observations concern one Midea-compatible ducted installation with
motorised zone dampers, follow-me control, static-pressure setting 2 and an
M5 ATOM S3/Tail485 interface. The HVAC model was not recorded in this review;
do not generalise its thresholds or power levels to other models.
ESPHome 2026.7.3 ran component revision
`2cef7ea8357dae6d22df3401d90c3ce8f0946597`, which separates the requested
fan setting from C0 feedback and exposes full-byte C0/C3 diagnostics.
All times below are Australia/Adelaide (UTC+10:30 on these dates).

## Method and limits

Existing HA/Influx history supplies climate requests, C0 byte 9, last emitted
C3 byte 7, coil inlet temperature, status flags and independent Shelly
indoor/outdoor electrical power. C3 sensor history publishes changes and
holds the last transmitted byte; it is not a record of every C3 transmission.
No commands were sent to provoke these events. A separate short API log
capture supplied C4/C6 frames during the heating window.

Samples arrive asynchronously: seconds below are rounded publication times,
not precise bus latency measurements. Coil transition temperatures are
approximate, using neighbouring samples. Power means carry preceding samples
forward on a one-second grid, excluding samples older than 60 seconds.
Neither power nor categorical protocol codes measure RPM or duct airflow.

## Heating, 7 October, 06:20–07:49

Requested fan remained Low; the last emitted C3 byte remained `0x04`.

| Local time | C0 byte 9 | Coil inlet near transition |
| --- | --- | --- |
| 06:20:41 | `0x00` | Startup |
| 06:22:45 | `0x04` | ~32°C |
| 06:24:41 | `0x00` | ~27.5°C |
| 07:05:53 | `0x04` | ~32°C |
| 07:20:39 | `0x00` | ~27.5°C |

This repeated rising/falling pattern supports temperature-gated regulation
with hysteresis. It does not establish universal thresholds or physical
fan motion. Indoor mean draw was ~50.9W during 06:24:41–07:05:53 (`0x00`),
~83.4W during 07:05:53–07:20:39 (`0x04`), and ~59.2W during
07:20:39–07:49:16 (`0x00`). Nonzero draw alone cannot prove fan rotation.

At 07:48, requested Low / C0 `0x00` / last C3 `0x04` coincided with coil
30°C, indoor ~58W, outdoor ~634W and compressor/outdoor-fan reports ON.
The legacy climate action nevertheless reported idle.

## C4/C6 requested-speed corroboration

A short passive capture around 07:49 contained 11 C4 and 3 C6 responses.
Every frame had byte 17 (zero-based, counting the initial AA) equal to
`0x04`; additive byte sums modulo 256 were 255. Representative C4:

```text
AA:C4:00:00:00:00:00:00:00:30:1C:00:00:00:00:00:84:04:14:91:9E:41:00:00:22:00:00:00:00:00:C2:55
```

Concurrent live HA state had C0 `0x00` and requested Low. No C3 was emitted
during this short capture. This corroborates the Low requested-speed
interpretation in [HomeOps issue #120](https://github.com/HomeOps/ESPHome-Midea-XYE/issues/120).
Only Low was captured; this does not validate other settings, external-control
synchronisation, or a universal mapping across hardware.

## Cooling, 6 October, 16:38–17:54

Target was 20°C. A brief C3 Auto (`0x80`) at 16:39:36 was followed by Low
at 16:39:37. History does not identify the initial Auto command's source.

| Requested setting/time | C3 byte 7/time | Matching C0 byte 9/time |
| --- | --- | --- |
| Low, 16:39:36 | `0x04`, 16:39:37 | `0x04`, 16:39:42 |
| Medium, 16:52:15 | `0x02`, 16:52:16 | `0x02`, 16:52:19 |
| Low, 17:17:15 | `0x04`, 17:17:16 | `0x04`, 17:17:19 |

No recorded feedback-driven request corruption occurred. Coil inlet fell
from 22.5°C to a minimum 10°C without a C0 zero transition: the observed
heating behaviour is not a universal rule across modes. Indoor mean draw
was ~94.4W during initial Low/startup, ~104.5W during Medium, and ~90.4W
during subsequent Low; demand and damper differences prevent an airflow
calibration from these figures.

Compressor reported OFF at 17:50:33, outdoor fan OFF at 17:51:01 and climate
OFF at 17:52:45. Indoor draw subsequently returned to roughly 5W, yet C0
retained `0x04` through the extended history window ending 18:05.
The legacy climate action reported idle throughout actual cooling.
Error/protect flags stayed zero and pressure setting stayed 2 in both runs.

## Consequences for consumers

Keep fan command intent, reported code and measured physical behaviour
separate. C0 cannot recover the requested setting reliably, and neither a
zero code in heating nor a Low code while OFF establishes fan motion.
Fan-code-derived climate action is not independent compressor evidence.

For zoned duct minimum-opening control, retaining an allowance based on the
requested speed is supported by these observations. Prepare ducts for an
imminent speed increase before commanding it; retain the previous allowance
until issuing a decrease. Do not reduce the allowance solely on a transient
C0 zero or idle action: the unit may resume without a new fan request.
Power-based opening tables remain installation-specific empirical proxies,
not validated airflow measurements. No table recalibration or action-code
change is justified by this passive review alone.
