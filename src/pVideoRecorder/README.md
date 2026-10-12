# pVideoRecorder

`pVideoRecorder` controls recording on one MediaMTX instance through its HTTP
Control API. It performs no video decode, encode, or file I/O. A mission decides
when to post `START` and `STOP`; application startup alone never starts recording.

Each recording is associated with the local `pLogger` directory. An optional
external recording ID (for example, an immutable task ID) or automatically
sequenced ID adds another directory level, so repeated recordings in one MOOS
run do not share an archive directory. When MediaMTX is remote, `record_root`
and all published recording paths refer to that remote host.

## Configuration

```moos
ProcessConfig = pVideoRecorder
{
  AppTick   = 4
  CommsTick = 4

  api_url     = 127.0.0.1:9997
  record_root      = /data/mission-video
  segment_duration = 1m

  // LOG directory plus optional per-recording identity. Configure either an
  // external ID variable or an automatic sequence prefix, not both.
  logger_directory_var = LOGGER_DIRECTORY
  recording_id_var     = UAV_ROUTE_ACTIVE_ID
  // recording_id_prefix = recording

  // LOGICAL_NAME:MEDIAMTX_PATH keeps archive camera names independent of
  // existing server path names. With no colon, both names are identical.
  path = nadir:drone_nadir
  path = fpv:drone_fpv

  // Recommended for live WebRTC paths: create temporary recorder-only paths
  // that pull the source paths over MediaMTX's loopback RTSP endpoint.
  record_proxy = nadir:pearl-recorder-nadir
  record_proxy = fpv:pearl-recorder-fpv
  // proxy_rtsp_address = 127.0.0.1:8554

  // Direct-path alternative: start and own an on-demand publisher while
  // recording. Do not combine activation and record_proxy for one camera.
  // activation = nadir:/opt/camera/start-nadir

  // Optional overrides:
  command_var   = VIDEO_RECORD_COMMAND
  status_prefix = VIDEO_RECORD
  record_host   = camera-hostname
  http_timeout  = 2

  // Optional HTTP Basic authentication. Keep the password outside generated
  // mission files and logs.
  // api_user          = recorder
  // api_password_file = /etc/pearl/video-recorder.password
}
```

`api_url` is an HTTP `host:port` endpoint. The scheme is omitted because MOOS
mission files interpret `//` as a comment delimiter. `record_host` defaults to
the API host and identifies where the published filesystem path lives.

`record_root` must be absolute and writable by MediaMTX. Optional
`segment_duration` accepts one positive MediaMTX duration such as `60s` or `1m`
and is sent as `recordSegmentDuration` on START. Logical camera names, logger
basenames, recording IDs, and automatic prefixes are restricted to letters,
digits, dots, underscores, and hyphens. A configured recording ID equal to
`none` is treated as unavailable.

`recording_id_var` and `recording_id_prefix` are mutually exclusive. With
`recording_id_prefix=recording`, successive START/STOP cycles in one application
run use `recording-001`, `recording-002`, and so on. The sequence resets when
`pVideoRecorder` restarts; a new mission normally has a new logger directory.

`record_proxy=CAMERA:PROXY_PATH` enables non-disruptive recording for a live
path. START removes any stale proxy, creates `PROXY_PATH` with a server-local
RTSP source such as `rtsp://127.0.0.1:8554/drone_nadir`, and enables recording
on the proxy. The proxy acts as a reader, so it also activates an existing
`runOnDemand` source when no viewer is open. STOP deletes the proxy. Neither
operation changes the source path, so existing WebRTC readers remain attached.
`proxy_rtsp_address` defaults to `127.0.0.1:8554` and accepts a MediaMTX-local
`HOST:PORT` without a URL scheme.

An optional direct-path `activation=CAMERA:/absolute/command` copies that
command into the mapped path's `runOnInit` setting on START and enables restart.
STOP disables recording and clears that recorder-owned command. MediaMTX
reloads paths when their configuration is patched, so direct mode can interrupt
publishers and WebRTC readers. `activation` and `record_proxy` are therefore
mutually exclusive for a camera.

For logger `LOG_uav_...`, ID `task-001`, and the example mappings, START sets:

```text
/data/mission-video/LOG_uav_.../task-001/nadir/%path_%Y-%m-%d_%H-%M-%S-%f
/data/mission-video/LOG_uav_.../task-001/fpv/%path_%Y-%m-%d_%H-%M-%S-%f
```

With recording proxies, the API creates `pearl-recorder-nadir` and
`pearl-recorder-fpv` while leaving `drone_nadir` and `drone_fpv` unchanged.
MediaMTX requires `%path` in every recording pattern, so each filename is
prefixed with its recorder path (for example,
`pearl-recorder-nadir_2026-...mp4`).

## Commands

```text
VIDEO_RECORD_COMMAND=START
VIDEO_RECORD_COMMAND=STOP
```

`START` waits in `WAITING_LOGGER` until `LOGGER_DIRECTORY` and any configured
external recording ID are available. Automatic IDs are allocated when a new
START is prepared. It then configures every direct or proxy recording path and
enters `RECORDING` only after every request succeeds. A partial start failure
cleans up every configured path before reporting `ERROR`.

`STOP` enforces `record=false` in direct mode and deletes recording proxies even
from local `IDLE` or `STOPPED`, which supports crash recovery. A START also
removes stale proxies before recreating them. Logger or recording-ID updates
never move an active recording. A later START latches the newest context.

## Publications

With the default prefix:

```text
VIDEO_RECORD_STATE
VIDEO_RECORDING
VIDEO_RECORD_COMMAND_RESULT
VIDEO_RECORD_LOG_DIRECTORY
VIDEO_RECORD_ID
VIDEO_RECORD_HOST
VIDEO_RECORD_DIRECTORY
VIDEO_RECORD_LOCATION
VIDEO_RECORD_STREAMS
VIDEO_RECORD_SEGMENT_DURATION
VIDEO_RECORD_API_URL
VIDEO_RECORD_API_REACHABLE
VIDEO_RECORD_LAST_HTTP_STATUS
VIDEO_RECORD_LAST_ERROR
```

`VIDEO_RECORD_LOCATION` is `host=HOST,path=ABSOLUTE_DIRECTORY`. The separate
host, directory, logger, and ID publications make the association explicit in
the node's MOOS log.

States are:

```text
IDLE, WAITING_LOGGER, STARTING, RECORDING, STOPPING, STOPPED, ERROR
```

A successful PATCH proves only that MediaMTX accepted the configuration. It
does not prove that frames arrived or that the final segment was closed.
Artifact collection should wait for `STOPPED`, verify files on
`VIDEO_RECORD_HOST`, and copy the original segments with checksums.

MediaMTX fMP4 recordings are fragmented MP4 files. Parts are short internal
fragments used to assemble each segment; one finalized recording file is one
segment. Changing segment duration changes file rollover frequency but does not
add video transcoding.
