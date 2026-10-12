# UAV mission video recording

The UAV community includes `pVideoRecorder` in every REAL launch. When support
is enabled, the UAV FSM posts `VIDEO_RECORD_COMMAND=START` as the accepted task
enters `TAKEOFF_OPERATIONS`. It keeps recording through takeoff, route execution,
rendezvous, and landing. It posts `STOP` only when:

- nominal landing has passed `LANDING_GROUND_CHECKS` and returned to `IDLE`; or
- a pre-takeoff abort returns to grounded `IDLE`.

The route-complete transition does **not** stop video. An off-nominal/secondary
landing also does **not** stop it, even after ground checks pass; this preserves
post-landing evidence. The operator must issue `VIDEO_RECORD_COMMAND=STOP`
before shutdown or artifact collection when the UAV remains `OFFNOMINAL`.
Recording state does not gate arming, flight, landing, or recovery.

## Camera names and MediaMTX compatibility

Archive camera names are:

| Archive camera | Existing MediaMTX path | Source |
| --- | --- | --- |
| `fpv` | `oak` | OAK RGB, H.264 Main, 1280x720 at 30 FPS / 4 Mbit/s |
| `nadir` | `main` | Landmark ISP Output0, 1640x1232 |

The `nadir` archive deliberately does **not** use MediaMTX `lores`. That
820x616 path is a downscaled viewer preview. The continuously running main
router reads the exact ISP Output0 buffers consumed by Landmark and performs one
8 Mbit/s hardware H.264 encoding; its Matroska and local RTSP branches share
those encoded access units.

`oak` is the only OAK video stream. Its camera-encoded H.264 access units feed
both live WebRTC and the `fpv` archive; there is no separate HQ encoder or
`oak-record` path. START creates temporary `pearl-recorder-fpv` and
`pearl-recorder-nadir` paths that read `oak` and full-resolution `main` through
MediaMTX's loopback RTSP endpoint. STOP deletes only the temporary recording
paths. Existing WebRTC preview sessions stay attached to `oak` and `lores`
throughout both operations.

## Per-task directories

The recorder latches both the UAV's `LOGGER_DIRECTORY` and immutable
`UAV_ROUTE_ACTIVE_ID` before START. Every task therefore receives a separate
directory:

```text
/var/lib/landmark-recorder/mission-video/
  <UAV_LOGGER_DIRECTORY>/<UAV_ROUTE_ACTIVE_ID>/fpv/
  <UAV_LOGGER_DIRECTORY>/<UAV_ROUTE_ACTIVE_ID>/nadir/
```

A logger restart or a later route-ID update does not move an active recording.
The next START latches the new values. Duplicate task IDs are already rejected
by the route buffer, preventing an old task from silently reusing a new flight.

## Logged host and path

The UAV MOOSDB receives, and `pLogger` captures:

```text
VIDEO_RECORD_LOG_DIRECTORY
VIDEO_RECORD_ID
VIDEO_RECORD_HOST
VIDEO_RECORD_DIRECTORY
VIDEO_RECORD_LOCATION
VIDEO_RECORD_STREAMS
```

For this UAV, `VIDEO_RECORD_HOST` defaults to `100.112.119.114` and
`VIDEO_RECORD_LOCATION` has the form:

```text
host=100.112.119.114,path=/var/lib/landmark-recorder/mission-video/<log>/<task>
```

These variables are also bridged to shoreside for operational visibility, but
the authoritative publication is made in the UAV MOOSDB and is preserved in
the UAV log.

Additional status includes:

```text
VIDEO_RECORD_STATE
VIDEO_RECORDING
VIDEO_RECORD_COMMAND_RESULT
VIDEO_RECORD_API_URL
VIDEO_RECORD_API_REACHABLE
VIDEO_RECORD_LAST_HTTP_STATUS
VIDEO_RECORD_LAST_ERROR
VIDEO_RECORD_START_COUNT
VIDEO_RECORD_STOP_COUNT
```

Wait for `VIDEO_RECORD_STATE=STOPPED` before collecting files. `STOPPED` means
MediaMTX deleted every temporary recording proxy. After stopping the local UAV
mission, `make logs` requires that state in the newest `.alog`, verifies both
`fpv` and `nadir` directories are nonempty, copies them into the local artifact
directory, and generates SHA-256 checksums. It refuses an active recording or an
open MOOS log rather than snapshotting files that may still change.

## Segment format and load

The mission sets `recordSegmentDuration=1m` on both recording proxy paths at START.
Each completed file is therefore one **one-minute fMP4 segment** (the final file
can be shorter). MediaMTX internally uses one-second fMP4 parts while assembling
each segment; parts are not separate archive files.

fMP4 means **fragmented MP4**. Instead of requiring one final index written only
at the end of a classic MP4, media samples are stored in a sequence of small MP4
fragments. This supports incremental recording and limits the amount at risk in
an interruption. It remains MP4, not Matroska/MKV.

`pVideoRecorder` is lightweight: it sleeps except for MOOS mail and a small
number of HTTP PATCH requests at START/STOP. MediaMTX remuxes the already
compressed streams into fMP4 without decoding or re-encoding them. One-minute
segments create more files and filesystem metadata than five-minute segments,
but do not introduce transcoding. The camera publishers and encoders remain the
meaningful onboard load.

## Modes

### REAL

Recording support is required:

```bash
./launch_uav.sh --mode=REAL ...
```

Expected deployment values are:

```text
MediaMTX API: 100.112.119.114:9997
Authentication: none (trusted private network)
Record root:  /var/lib/landmark-recorder/mission-video
```

The locally changed `pVideoRecorder` must be built and deployed through the
normal repository/deployment workflow before launch. It is intentionally not
uploaded by these mission changes.

### SITL

SITL leaves physical-UAV recording control disabled by default. Enable it only
when a simulated UAV task should control the physical recorder:

```bash
./launch.sh --mode=SITL --video-recording
# or
make sim-briggs SIM_ARGS=--video-recording
```

Override deployment details with:

```text
--video-api=HOST:PORT
--video-record-root=/absolute/remote/path
```

The API omits `http://` because MOOS mission files interpret `//` as a comment
delimiter. The deployed API is currently unauthenticated and must remain
reachable only through the trusted private network.
