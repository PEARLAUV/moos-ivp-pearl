# Sherlock-routed Alfa backhaul

This mission uses a dedicated routed Wi-Fi path between the UAV Pi and the
PEARL Ethernet network. Sherlock owns the ground-side Alfa access point and
routes traffic between the radio and PEARL. Direct mission traffic is routed
without NAT. Sherlock separately provides a low-priority, NATed internet path
so the UAV's existing Tailscale node can operate over Alfa.

Last bench preparation: 2026-08-14

## Topology

```text
UAV Pi                          Sherlock                         PEARL
wlan1 172.22.90.2/30  <------> wlan1 172.22.90.1/30
                                 eth0 192.168.88.252/24  <----> eth0 192.168.88.253/24
```

The two Alfa adapters are MediaTek `0e8d:7961` devices driven by `mt7921u`:

| Host | MAC | Role |
| --- | --- | --- |
| Sherlock | `00:c0:ca:b8:00:84` | Access point and IPv4 router |
| UAV Pi | `00:c0:ca:b8:00:88` | Managed client with autoconnect enabled |

The common radio configuration is:

| Setting | Value |
| --- | --- |
| SSID | `PEARL-UAV-BACKHAUL` |
| Regulatory domain | `US` |
| Band | 2.4 GHz |
| Channel | 11 |
| Channel width | 20 MHz |
| Security | WPA-PSK, RSN, CCMP |
| Power saving | Disabled |
| IPv6 | Disabled on the backhaul |

The WPA passphrase is held in the Rigging Ansible Vault and is intentionally
absent from this repository.

## Why this is valid for MOOS-IvP

The UAV and PEARL do not need to share a Layer-2 broadcast domain. This mission
uses explicit unicast pShare routes, and pHostInfo is forced to advertise the
address passed with `--ip`. The requirement is bidirectional IP/UDP
reachability, not direct radio association between the two mission computers.

Sherlock forwards direct mission packets without translating their addresses:

- UAV route: `192.168.88.0/24 via 172.22.90.1`
- PEARL route: `172.22.90.0/30 via 192.168.88.252`
- Sherlock: unmodified forwarding for PEARL/UAV traffic, plus masquerading
  only for internet-bound traffic sourced from `172.22.90.0/30`

The UAV installs a fallback default route through `172.22.90.1` with metric
2000. Its normal MIT Wi-Fi route currently has metric 600, so normal Wi-Fi wins
when both links are available. If normal Wi-Fi disappears, the existing
`tailscaled` service can use Alfa without creating a new Tailscale node.

The preferred mission path advertises the Mac, UAV, and PEARL Tailnet addresses
to shoreside. Shoreside and management traffic therefore use Tailscale, while
the explicit `--pearl_ip=192.168.88.253` and `--uav_ip=172.22.90.2` arguments
keep PEARL/UAV traffic on the directly routed Alfa network.

A Tailnet address is a logical endpoint. Tailscale normally establishes a
direct UDP data path, which may remain local when the endpoints share a
reachable network or may cross Sherlock's upstream internet connection. It
can use a DERP relay when direct traversal is unavailable. If that upstream is
Starlink, remote shoreside, SSH, and other Tailscale traffic may cross
Starlink. The explicit PEARL/UAV private-address path never needs Starlink or
Tailscale.

The OAK four-view JPEG monitor follows the same management path directly. An
authorized Tailnet client opens `http://uav-pi-1:8082/`; health, snapshots, and
metrics use the direct UAV Tailnet API on TCP `9102`. The independently
decodable MJPEG profile favors reliable late and reconnect viewing over motion
smoothness; its bitrate varies with scene content and is measured by camera
metrics. Rigging's `./.bin/oak-camera-viewer` is an optional localhost relay for
TCP `8082` and `9102`, not a required intermediate hop. PEARL does not proxy
the video, and no SSH tunnel, Tailscale Serve, Funnel, or fixed deployed relay
is used. See [`OAK_VIDEO.md`](OAK_VIDEO.md) for the operator workflow and
deployed stream profile.

Shoreside sends ordinary pShare traffic directly to the UAV's Tailnet address.
PEARL does not relay shoreside traffic. Rigging retires and removes the old
`wifi-backhaul-udp-relay-*` services and executable when its PEARL route role
is applied. The only mission UDP listeners are ports `9200`-`9202`. The camera
viewer and API use direct Tailnet TCP and add no fixed UDP relay or mission
listener.

## Bench-prepared state

Rigging applies the Sherlock AP, PEARL route, and UAV profile. Activation and
autoconnect were initially disabled for bench preparation, then enabled after
the first successful dock association on 2026-08-13.

Sherlock uses Ansible-managed `hostapd` for the AP. NetworkManager is kept off
Sherlock's `wlan1`; attempts to create this MT7921U AP through
NetworkManager/wpa_supplicant timed out. NetworkManager continues to own the
UAV client.

The 2026-08-13 bench deployment confirmed:

- Sherlock `hostapd` is enabled and reports `AP-ENABLED` on channel 11 at
  20 MHz, with `172.22.90.1/30` assigned and IPv4 forwarding enabled.
- PEARL has an enabled persistent route to `172.22.90.0/30` through
  `192.168.88.252` and reaches Sherlock's AP address over Ethernet.
- At a few meters, the UAV associated successfully at -44 dBm with negotiated
  rates around 104-130 Mbit/s. Twenty-packet tests in both directions had 0%
  loss, UAV-to-PEARL latency settled around 1-3 ms, and Sherlock reported no
  transmit retries or failures.
- Direct UDP probes also succeeded from UAV to PEARL port 9202 and from PEARL
  to UAV port 9201, matching the mission's pShare listener ports.
- The UAV profile now has activation and autoconnect enabled.

On 2026-08-13, the obstructed roughly 50 m placement was not usable. The link
briefly associated near -78 dBm at 6-6.5 Mbit/s but delivered 0 of 20 ICMP
packets in either direction and repeatedly disconnected. Do not use that
placement for flight. A clearer line of sight, better antenna placement, or a
radio transmit-power correction must be qualified first.

The first dock association attempt used 5 GHz channel 44. The UAV could see
many other networks but not Sherlock's beacon. The deployed profile was
therefore changed to 2.4 GHz channel 11 for better propagation; channel 11 was
also the quietest non-overlapping 2.4 GHz channel in the UAV's dock scan. The
`3.00 dBm` value shown by `iw` is a known MT7921 driver reporting defect and is
not a reliable measurement of actual output power. Treat usable range under
traffic as a field-qualification item.

The intended deployed UAV state is:

```sh
nmcli -f NAME,AUTOCONNECT connection show pearl-uav-backhaul-client
nmcli -f GENERAL.STATE,GENERAL.CONNECTION device show wlan1
```

The profile should report `AUTOCONNECT=yes`. In radio range, `wlan1` should
report the backhaul connection and hold `172.22.90.2/30`.

## Field activation

If manual recovery is required, associate explicitly:

```sh
sudo nmcli connection up pearl-uav-backhaul-client ifname wlan1
```

The normal deployed profile autoconnects. Verify the link before launching any
mission:

```sh
# UAV
ip -br address show wlan1
ip route get 192.168.88.253
ip route get 1.1.1.1
ping -c 5 172.22.90.1
ping -c 5 192.168.88.252
ping -c 5 192.168.88.253
ping -c 5 1.1.1.1
getent hosts login.tailscale.com
tailscale status
tailscale ping 100.127.231.65

# Shoreside Tailnet client
tailscale ping uav-pi-1
curl -fsS http://uav-pi-1:9102/readyz

# PEARL
ip route get 172.22.90.2
ping -c 5 172.22.90.2

# Sherlock
sudo /usr/sbin/iw dev wlan1 station dump
sudo /usr/sbin/iw dev wlan1 info
ip -s link show wlan1
```

For the normal live camera check, open `http://uav-pi-1:8082/` directly. If a
localhost alias is useful for troubleshooting, run this optional helper from
the Rigging checkout:

```sh
./.bin/oak-camera-viewer
```

Then open `http://127.0.0.1:8082/` on shoreside.

Confirm that the radios remain associated without recurring `mt7921u` timeouts:

```sh
sudo journalctl -k --since "10 minutes ago" | grep -Ei "mt7921|wlan1|firmware"
```

Do not treat the close-range association as an operational range test. Repeat
the packet-loss, signal, and retry checks with the UAV in its intended launch
position before flight.

## Mission signal logging

Sherlock's Ansible-managed Telegraf collector samples the configured UAV
station every two seconds. PEARL REAL mode reads those measurements through
`iSherlockTelemetry`; wildcard `pLogger` logging stores them in PEARL's
mission `.alog`, and `uFldNodeBroker` sends them to shoreside.

The primary variables are:

| Variable | Meaning |
| --- | --- |
| `ALFA_LINK_UP` | The configured UAV station is associated with Sherlock |
| `ALFA_SIGNAL_DBM` | Latest UAV signal received by Sherlock |
| `ALFA_SIGNAL_AVG_DBM` | Driver-reported average UAV signal |
| `ALFA_TX_BITRATE_MBPS` | Sherlock-to-UAV negotiated bitrate |
| `ALFA_RX_BITRATE_MBPS` | UAV-to-Sherlock negotiated bitrate |
| `ALFA_TX_RETRIES_TOTAL` | Cumulative retries since association |
| `ALFA_TX_FAILED_TOTAL` | Cumulative failures since association |
| `ALFA_INACTIVE_MS` | Time since Sherlock last received a UAV packet |
| `ALFA_STATION_COUNT` | Number of stations currently visible to the AP |
| `ALFA_DATA_VALID` | Collector data exists, is healthy, and is fresh |
| `ALFA_DATA_AGE` | Seconds since Sherlock actually measured the radio |
| `ALFA_SIGNAL_DATA_VALID` | Signal and bitrate values are fresh and the UAV is linked |

A disconnected UAV is a valid observation: `ALFA_LINK_UP=0` with
`ALFA_DATA_VALID=1`. A failed or stale collector reports
`ALFA_DATA_VALID=0`. MOOSDB retains the last numeric signal after a disconnect,
so only use signal and bitrate values while `ALFA_SIGNAL_DATA_VALID=1`. This
explicit flag becomes zero when the link drops, the collector fails, or the
measurement ages out.

The collector embeds its wall-clock measurement timestamp in every result.
PEARL calculates `ALFA_DATA_AGE` from that timestamp rather than from the time
it re-read Telegraf's page, so cached samples cannot become fresh again.
Sherlock collects and Telegraf flushes every two seconds. Normal clock
synchronization on Sherlock and PEARL is therefore also a mission prerequisite.

The repository integration test starts a temporary MOOSDB and feeds known
connected, disconnected, collector-failed, and stale Prometheus samples through
the real `iSherlockTelemetry` binary. Run it after building the repository:

```sh
python3 src/iSherlockTelemetry/tests/test_alfa_telemetry.py
```

The disconnected path was also verified live on 2026-08-19 after deploying the
collector to Sherlock and rebuilding `iSherlockTelemetry` on PEARL. A
telemetry-only MOOS community on isolated port 19402 published healthy, fresh
link-down data, and pLogger recorded it.

For the management-path qualification, retain direct Ethernet as recovery,
associate Alfa, and temporarily disconnect the UAV's normal `wlan0` profile.
Verify that the metric-2000 default route uses `wlan1`, Tailscale remains
online at its existing address, and shore can SSH to that address. With the
OAK connected, open `http://uav-pi-1:8082/` directly and verify direct
`/readyz` and both snapshots while watching link signal, retries, inactive
time, and packet loss. Reconnect normal Wi-Fi afterward. This management-path
test does not launch MOOS or arm the UAV.

## Mission addresses

The preferred topology uses Tailscale for shoreside reachability and the
private Alfa/LAN addresses only for direct PEARL-to-UAV traffic. The UAV keeps
its existing Tailnet identity; when normal Wi-Fi is unavailable, its Tailscale
traffic reaches the internet through Alfa and Sherlock.

Use:

```sh
# Shoreside
./launch_shoreside.sh --auto --mode=REAL \
  --ip=100.127.231.65 \
  --uav_ip=100.70.189.91 --uav_pshare=9201

# UAV Pi
./launch_uav.sh --auto --mode=REAL \
  --ip=100.70.189.91 \
  --shore=100.127.231.65 --shore_pshare=9200 \
  --pearl_ip=192.168.88.253 --pearl_pshare=9202

# PEARL
./launch_pearl.sh --auto --mode=REAL \
  --ip=100.69.111.61 \
  --shore=100.127.231.65 --shore_pshare=9200 \
  --uav_ip=172.22.90.2 --uav_pshare=9201
```

Here, each `--ip` value is the address advertised to shoreside. The explicit
`--pearl_ip` and `--uav_ip` values keep vehicle-to-vehicle traffic on the direct
Alfa path. The required mission UDP listeners are shoreside `9200`, UAV `9201`,
and PEARL `9202`. MOOSDB ports remain host-local and should not be used as the
inter-host route. There is no PEARL UDP relay and port `9300` is not part of
the current topology.

## Three-computer dock simulation

On 2026-08-14, a second headless SIM used the preferred address split above
with isolated MOOSDB ports `19100`-`19102` and pShare ports `19200`-`19202`.
Shoreside reported two connected nodes and a healthy process watch. A route
submitted at shoreside was acknowledged by the UAV as `DEPLOY_ACCEPTED`, and
the simulated UAV moved from approximately `x=10` to `x=22.49` at `1.6 m/s`.
The UAV target contained no `pArduBridge`, and no arm request was sent.

On 2026-08-13, the mission ran in SIM mode across the shoreside Mac, PEARL Pi,
and UAV Pi. Isolated ports `19100`-`19102`, `19200`-`19202`, and `19300` kept
the run separate from other work. The generated UAV target contained no
`pArduBridge` or hardware interface applications.

The observed end-to-end sequence used the original, now-retired relay design:

1. Shoreside submitted a route through the PEARL UDP relay.
2. The UAV received `MEDIATED_MESSAGE`, published `ROUTE_UPDATE`, accepted
   deployment, and moved in simulation.
3. Shoreside deployed PEARL, and PEARL moved in simulation.
4. SIM-only ARM and TAKEOFF requests produced simulated results and altitude
   8 m; no flight controller or real arm command was involved.
5. `RENDEZVOUS_START` produced the same session and meeting point on both
   vehicles: session `uav_1786659149728036`, point `(3.32,-5.63)`.
6. Both coordinators reached `COMPLETE`; the UAV reported reason `landed` and
   PEARL reported reason `uav_landed`.
7. Before shutdown, the UAV MOOSDB reported `UAV_IS_ARMED=false`,
   `UAV_LANDED_STATE=ON_GROUND`, and `NAV_ALTITUDE=0`.

All isolated communities and temporary relays were stopped after the test.
That result remains historical evidence; current shoreside commands address
the UAV Tailnet node directly.

This proves application routing and the state-machine sequence at dock range;
it does not qualify radio range or replace the REAL-mode flight checklist.

## Close-range REAL qualification

On 2026-08-20, the physical radios and all three ordinary REAL communities
were tested concurrently at approximately 2-3 m. Bidirectional PEARL/UAV
packet tests had zero loss, the PEARL `.alog` recorded 757 linked samples from
-52 to -40 dBm with zero TX retries or failures, and actual PEARL battery and
wind reached the UAV takeoff gate over the direct Alfa route.

The run also proved the fail-closed telemetry path, the acknowledged
shoreside-to-UAV operator path, and the no-navigation rendezvous guard. The UAV
Pixhawk was disconnected, so actual UAV battery/health/navigation and real
arm/takeoff were not tested. Independent Sherlock and UAV Pi reboots also
restored the AP/client association, private routes, Tailscale, upstream
internet, and fresh metrics automatically. Sherlock's AP returned first, but
its NAT and Telegraf units remained queued behind `network-online.target` for
roughly two minutes; wait for a fresh port `9273` timestamp before launching a
mission. See
[`FIELD_TEST_2026-08-20.md`](FIELD_TEST_2026-08-20.md) for exact results,
limitations, log locations, and hashes.

A same-day walking test exercised loss and recovery while watching Sherlock's
live collector. Nearby readings from roughly -39 to -65 dBm carried traffic
with zero observed loss. Around -70 dBm, latency and loss became noticeably
less reliable. Sustained readings around -77 to -82 dBm produced 100% packet
loss even while the driver sometimes still reported an association; rising
`ALFA_INACTIVE_MS` exposed that unusable state. Walking back toward Sherlock
restored association, packets, and fresh telemetry automatically. This is a
useful qualitative threshold, not a distance rating: antenna placement and
obstructions dominated the earlier 50 m result.

## Mission qualification

Before REAL operation:

1. Generate all REAL targets and verify the forced pHostInfo addresses.
2. Run each community in SIM mode on its intended host over the radio path.
3. Confirm both vehicle brokers complete all connection phases.
4. Confirm periodic `NODE_REPORT` traffic reaches shoreside and PEARL.
5. Prove bidirectional pMediator and `NODE_REPORT` delivery over the explicit
   UAV/PEARL private routes without shoreside participation.
6. Test a rendezvous with controlled packet loss on the direct vehicle path.
7. Reboot all three Pis and repeat the route and association checks.
8. Exercise the direct Tailnet OAK monitor while monitoring Alfa and mission
   broker freshness, and verify that the added traffic does not degrade mission
   delivery.

The repository test proves parsing and stale-data behavior without the radios.
The 2026-08-20 close-range REAL test now proves physical association, signal
logging, zero-loss bidirectional mission connectivity, and the fail-closed
gate/operator path. Before flight, repeat packet-loss, signal, retry, and
throughput checks at the intended launch position with clear line of sight;
the earlier obstructed 50 m placement remains unqualified. Connect the Pixhawk
with propellers removed and prove actual UAV battery, health, GPS, armed, and
landed-state telemetry before any outdoor arm/takeoff test.

The rendezvous coordinator aborts when navigation or UAV reports become stale,
so a successful ping alone is not sufficient qualification.
