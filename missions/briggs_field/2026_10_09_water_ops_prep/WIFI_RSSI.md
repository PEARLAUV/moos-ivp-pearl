# UAV-board Wi-Fi RSSI: local history first, shoreside scope second

This path measures Wi-Fi **received by the UAV companion board**. It does not
query Sherlock, an AP metrics endpoint, Prometheus, or the Pixhawk. It is separate
from RC receiver RSSI, transmitter telemetry, driver-reported transmit power, and
end-to-end network latency/loss.

## Existing flight history: 10–12 October 2026

Read-only inspection of `pi@100.112.119.114` on 14 October found no retained
network/kernel journal entries for 10–12 October UTC and no matching rotated
system-log files. The available boot listing begins after the reported flights.
No historical Wi-Fi RSSI was recovered from those sources. Absence of records is
not evidence that the radio was healthy.

Commands, source times, statuses, raw outputs and SHA-256 checksums are preserved
in [`field_logs/recovered/20261014T132014.998514Z_uav-wifi-evidence/`](field_logs/recovered/20261014T132014.998514Z_uav-wifi-evidence/).
`current-*` files are inspection-time snapshots only. They showed the board's
`wlan1` providing approximately −50 dBm through both `iw` and `/proc/net/wireless`.
They must **not** be attributed to earlier flights. No services, radio settings,
interfaces or parameters were changed remotely.

## Priority 1: after-flight recovery from board-local logs

On the UAV, updated `make logs` first creates `system-wifi/` in the artifact:

- `journal-network.stdout`: NetworkManager and wpa_supplicant journal JSON.
- `journal-kernel.stdout`: retained kernel journal JSON, across available boots.
- `journal-wifi-samples.stdout`: dedicated local samples tagged `pHostInfo-wifi`.
- `journal-boots.stdout`: retained boot coverage.
- `rotated/`: available syslog/kernel/daemon/wpa_supplicant/NetworkManager text
  and compressed logs, copied verbatim to their observed length. Changes, short
  reads, symlinks and permission errors are recorded, not silently ignored.
- `current-*`: current `iw` interface/link/station information and proc snapshots,
  explicitly separate from historical evidence. No Wi-Fi scan is performed.
- `WIFI_EVIDENCE.json`: commands, statuses, errors, availability and provenance.
- `journal-rssi-candidates.json`: candidate explicit signal/RSSI observations with
  source file/line, message, interface when present, boot ID and journal clocks.
  Unverified units remain unverified; candidates still require review.

The journal interval is the selected MOOS `LOGSTART` through the largest alog
elapsed timestamp, with 120 seconds of padding on each side. This is the **host
clock**, not FC GPS time. Clock changes and missing boots can limit alignment.
Queries do not implicitly restrict recovery to the current boot.

This evidence is checksummed with the rest of the bundle. The fetch target stays
fetch-only and retrieves the already assembled files. `system_wifi.capture_complete`
and `rssi_history_status` distinguish failed/partial capture from an empty but
successful query. Diagnostic gaps warn without preventing independent video,
MOOS or native-log collection. Overall core-bundle success does **not** guarantee
that Linux retained historical RSSI.

Run collection **before reboot, power-off, log cleanup or journal rotation**.
No journald persistence, retention, logging level or NetworkManager configuration
is changed automatically. Ordinary connection logs often contain no periodic
RSSI samples; a current `iw` reading cannot recreate them.

## Future local history without another MOOS app

The existing `pHostInfo` app has an optional Linux Wi-Fi reader in the sibling
`moos-ivp` checkout (`ivp/src/pHostInfo/`). It reads only:

- `/proc/net/wireless` for the selected interface's driver signal level;
- `/sys/class/net/<interface>/wireless` (or `phy80211`) and `carrier` for identity/link status.

It uses no shell/network command, radio scan, FC connection or remote dependency.
The feature is disabled by default. This mission enables it only for the REAL
UAV, including when the PEARL counterpart is GRASS:

```text
ProcessConfig = pHostInfo
{
  // Existing IP configuration is unchanged.
  wifi_interface = wlan1
  wifi_prefix = UAV_WIFI
  wifi_poll_interval = 2
  wifi_syslog = true
}
```

Verify the interface identity on deployment; `wlan1` was the inspected UAV Alfa.
An absent interface or unsupported driver is reported as unavailable, not as
healthy. Negative driver dBm values are required; relative/unsigned levels and
unknown sentinels such as −256 are rejected. Read time is **not** a hardware RF
packet timestamp, and driver calibration/update latency remains a limitation.

Every two seconds, `pHostInfo` records a local syslog observation under
`SYSLOG_IDENTIFIER=pHostInfo-wifi`, including read time, interface, link state and
signal validity. Invalid/disconnected samples say `signal=unavailable`, not a
held signal value. This local history does not depend on shoreside delivery or
pLogger. It exists while the mission's `pHostInfo` runs, **not** continuously
between missions. Actual retention still depends on the board's logging system.

After deploying the updated binary/config, verify the local records read-only:

```sh
journalctl --utc --no-pager --since '10 minutes ago' SYSLOG_IDENTIFIER=pHostInfo-wifi
```

The syslog reader is included in subsequent `make logs` exports. No separate
application, daemon, service installation or privilege escalation is required.

## Priority 2: real-time MOOS scope

The same observation is published in the UAV MOOSDB, recorded by its wildcard
pLogger, and forwarded by its broker directly to shoreside. No PEARL/Sherlock
telemetry app is involved. Shoreside pMarineViewer scopes:

| Variable | Interpretation |
| --- | --- |
| `UAV_WIFI_INTERFACE` | Explicit sampled board interface |
| `UAV_WIFI_STATUS` | Connected, disconnected, or reason unavailable |
| `UAV_WIFI_STATUS_VALID` | Carrier/link status was read successfully |
| `UAV_WIFI_LINK_UP` | Carrier state; interpret only with valid status |
| `UAV_WIFI_SIGNAL_DBM` | Last valid local receive signal; lower/more negative is weaker |
| `UAV_WIFI_SIGNAL_VALID` | This observation has usable signal and an up link |
| `UAV_WIFI_SAMPLE_TIME` | Host/MOOS epoch seconds when the status was sampled |

A valid signal requires both `SIGNAL_VALID=1` **and a recent, advancing sample
time**. For a two-second sample period, use at most ten seconds of source age,
with synchronized clocks; negative/future ages are not fresh. MOOSDB can retain
old values when radio delivery stops, including an old validity flag of one.
The scope does not automatically clear those remote values or implement a new
failsafe. This telemetry is observability, not flight authorization or evidence
that a link meets operational range/reliability requirements.

## Build and deployment status

The changes require the updated existing `pHostInfo` binary from `moos-ivp`, the
mission templates, and `collect_mission_logs.py` plus `wifi_evidence.py` from
`moos-ivp-pearl`. A stock `pHostInfo` will not recognize the Wi-Fi settings.
Local target build and fixture tests are used for validation. **No updated
binary/configuration has been deployed to the UAV as part of this work.**
