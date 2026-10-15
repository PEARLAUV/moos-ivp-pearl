# Run each physical node target on its corresponding host. sim-* starts all
# three nodes locally via launch.sh, with a new owned SITL instance. Physical
# drone MODEL selection requests guarded model->REAL->mission upload before MOOS.
# No SSH, flight commands or pre-launch cleanup; --just_make never uploads.
# Default make prints help; real applications start only on an explicit target.
.DEFAULT_GOAL := help
.NOTPARALLEL:
.PHONY: help clean kill logs stop-recording collect-logs-briggs collect-logs-charles collect-logs-sim shoreside-briggs pearl-briggs drone-briggs shoreside-charles pearl-charles drone-charles sim-briggs sim-charles

MISSION_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
PEARL_ROOT := $(abspath $(MISSION_DIR)/../../..)
# SIM keeps an x500 default; physical drone targets require an explicit choice.
MODEL ?= x500
ifneq ($(filter drone-briggs drone-charles,$(MAKECMDGOALS)),)
ifneq ($(origin MODEL),command line)
$(error Physical drone targets require an explicit MODEL: make drone-briggs MODEL=x500 or make drone-charles MODEL=x500)
endif
ifeq ($(strip $(MODEL)),)
$(error MODEL must not be empty for physical drone targets)
endif
endif
SHORE_IP ?= 100.88.214.50
DRONE_IP ?= 100.112.119.114
PEARL_REAL_IP ?= 100.113.99.58
PEARL_GRASS_IP ?= 100.71.137.47

# Local metres in the selected site datum; heading degrees is unchanged.
# REAL/GRASS starts are NOT injected into navigation: actual sensors own pose.
BRIGGS_START ?= 51.033305,-56.215822,0
CHARLES_START ?= -4,-22,0
# User-selected Briggs geographic defaults converted to local WGS-84 UTM
# offsets from 42.3569186052,-71.0990291116 using scripts/local_to_latlon.py.
# Return: 42.356575,-71.098486; rally: 42.356454,-71.098831.
# Charles uses its own approved nearby geographic points below. Verify sites before task commit;
# these are NOT FC Home. Native frozen rally ACK/readback still own readiness.
BRIGGS_RETURN ?= 43.787856,-39.259280
BRIGGS_RALLY ?= 15.042299,-51.993939
# Charles datum 42.358436,-71.087448, same UTM-offset conversion.
# Return 42.358465,-71.087338; rally 42.358542,-71.087357 (confirmed typos).
CHARLES_RETURN ?= 9.138278,2.997768
CHARLES_RALLY ?= 7.783478,11.586484
# Example: make shoreside-charles LAUNCH_ARGS=--just_make
# Or preview without executing anything: make -n drone-briggs MODEL=x500
# Physical drone targets include --upload-params for the full ordered stack.
# MODEL=NAME explicitly requests this guarded upload; --just_make never uploads.
LAUNCH_ARGS ?=
SHORESIDE_ARGS ?=
PEARL_ARGS ?=
DRONE_ARGS ?=
SIM_ARGS ?=
SIM_WARP ?= 1

# Post-mission, local-node artifact collection. LOG_NODE normally resolves from
# the configured Tailnet addresses; override it only for diagnostics/tests.
LOG_NODE ?= auto
LOG_OUTPUT_ROOT ?= $(MISSION_DIR)field_logs
LOG_DIRECTORY ?=
LOG_VIDEO_ROOT ?= /var/lib/landmark-recorder/mission-video
LOG_AP_PROTOCOL ?= serial
LOG_AP_ENDPOINT ?= serial/by-id/usb-ArduPilot_Pixhawk6X_200023000E51333531343838-if00:115200
LOG_ARGS ?=
VIDEO_RECORD_API ?= http://127.0.0.1:9997/v3/config/paths

# Shoreside retrieval of artifacts already assembled by `make logs` remotely.
COLLECT_DRONE_HOST ?= pearl@$(DRONE_IP)
COLLECT_PEARL_REAL_HOST ?= pearl@$(PEARL_REAL_IP)
COLLECT_PEARL_GRASS_HOST ?= pearl@$(PEARL_GRASS_IP)
COLLECT_REMOTE_ROOT ?= /home/pearl/moos-ivp-pearl/missions/briggs_field/2026_10_09_water_ops_prep/field_logs
COLLECT_DRONE_REMOTE_ROOT ?= $(COLLECT_REMOTE_ROOT)
COLLECT_PEARL_REMOTE_ROOT ?= $(COLLECT_REMOTE_ROOT)
COLLECT_OUTPUT_ROOT ?= $(LOG_OUTPUT_ROOT)/fetched
COLLECT_SIM_OUTPUT_ROOT ?= $(LOG_OUTPUT_ROOT)/sim
COLLECT_ARGS ?=
COLLECT_SIM_ARGS ?=

help:
	@printf '%s\n' 'Run locally on the corresponding node host:' \
	  '  shoreside-briggs  pearl-briggs (GRASS)  drone-briggs' \
	  '  shoreside-charles pearl-charles (REAL)  drone-charles' \
	  'All other physical nodes use REAL; drone targets REQUIRE command-line MODEL=NAME.' \
	  'Example: make drone-briggs MODEL=x500 (or drone-charles MODEL=x500).' \
	  'Physical drone targets upload/verify model -> REAL -> mission before MOOS.' \
	  'Local simulation: sim-briggs / sim-charles (SIM_WARP=$(SIM_WARP)).' \
	  'Preview: make -n TARGET; generate only: make TARGET LAUNCH_ARGS=--just_make' \
	  'Return/rally markers are provisional; validate before task commit.' \
	  'logs: after shutdown, stop recording if needed and assemble artifacts with SHA-256 sums.' \
	  'stop-recording: optional stopped-drone recording cleanup without full collection.' \
	  'collect-logs-briggs/charles: fetch all saved drone/PEARL bundles; reuse verified local copies.' \
	  'collect-logs-sim: assemble the latest closed local three-community SIM logs.' \
	  'clean: removes mission logs/targets; kill: host-wide ktm + owned SITL/VNC cleanup.'

# Read-only collection is explicit and never a launch prerequisite. The drone
# collector refuses a running pArduBridge and uses grounded/disarmed native
# evidence before requesting parameters or mission-overlapping DataFlash logs.
logs:
	python3 "$(PEARL_ROOT)/scripts/collect_mission_logs.py" \
	  --mission-dir "$(MISSION_DIR)" --node "$(LOG_NODE)" \
	  --output-root "$(LOG_OUTPUT_ROOT)" \
	  --shore-ip "$(SHORE_IP)" --drone-ip "$(DRONE_IP)" \
	  --pearl-real-ip "$(PEARL_REAL_IP)" --pearl-grass-ip "$(PEARL_GRASS_IP)" \
	  --video-root "$(LOG_VIDEO_ROOT)" --video-api-url "$(VIDEO_RECORD_API)" \
	  --ap-protocol "$(LOG_AP_PROTOCOL)" --ap-endpoint "$(LOG_AP_ENDPOINT)" \
	  --log-directory "$(LOG_DIRECTORY)" $(LOG_ARGS)

# Optional post-mission cleanup without full artifact collection. `make logs`
# performs the same exact-log-bound cleanup automatically when needed.
stop-recording:
	python3 "$(PEARL_ROOT)/scripts/collect_mission_logs.py" \
	  --mission-dir "$(MISSION_DIR)" --stop-recording-only \
	  --video-api-url "$(VIDEO_RECORD_API)"

# Fetch only: each remote node must have already run `make logs`. Retrieve every
# finalized bundle (including partial ones), not just a latest-by-mtime pair.
collect-logs-briggs:
	python3 "$(PEARL_ROOT)/scripts/fetch_collected_logs.py" \
	  --site briggs --shore-ip "$(SHORE_IP)" \
	  --drone-host "$(COLLECT_DRONE_HOST)" \
	  --pearl-real-host "$(COLLECT_PEARL_REAL_HOST)" \
	  --pearl-grass-host "$(COLLECT_PEARL_GRASS_HOST)" \
	  --drone-remote-root "$(COLLECT_DRONE_REMOTE_ROOT)" \
	  --pearl-remote-root "$(COLLECT_PEARL_REMOTE_ROOT)" \
	  --output-root "$(COLLECT_OUTPUT_ROOT)" $(COLLECT_ARGS)

collect-logs-charles:
	python3 "$(PEARL_ROOT)/scripts/fetch_collected_logs.py" \
	  --site charles --shore-ip "$(SHORE_IP)" \
	  --drone-host "$(COLLECT_DRONE_HOST)" \
	  --pearl-real-host "$(COLLECT_PEARL_REAL_HOST)" \
	  --pearl-grass-host "$(COLLECT_PEARL_GRASS_HOST)" \
	  --drone-remote-root "$(COLLECT_DRONE_REMOTE_ROOT)" \
	  --pearl-remote-root "$(COLLECT_PEARL_REMOTE_ROOT)" \
	  --output-root "$(COLLECT_OUTPUT_ROOT)" $(COLLECT_ARGS)

# SIM is entirely local: preserve the matching UAV/PEARL/shoreside MOOS logs
# and the latest owned SITL launcher log after the simulation has stopped.
collect-logs-sim:
	python3 "$(PEARL_ROOT)/scripts/collect_sim_logs.py" \
	  --mission-dir "$(MISSION_DIR)" \
	  --output-root "$(COLLECT_SIM_OUTPUT_ROOT)" $(COLLECT_SIM_ARGS)

# Destructive operations are explicit, never prerequisites of launch targets.
clean:
	cd "$(MISSION_DIR)" && ./clean.sh

# ktm may terminate unrelated MOOS communities on the same host.
kill:
	cd "$(MISSION_DIR)" && { ./launch_sitl.sh --cleanup-ui; _ui_status=$$?; \
	  ktm; _moos_status=$$?; ./launch_sitl.sh --cleanup-owned; _sim_status=$$?; \
	  test "$$_ui_status" -eq 0 && test "$$_moos_status" -eq 0 && test "$$_sim_status" -eq 0; }

shoreside-briggs:
	cd "$(MISSION_DIR)" && ./launch_shoreside.sh --mode=REAL --site=briggs \
	  --ip="$(SHORE_IP)" --uav_ip="$(DRONE_IP)" --pearl_ip="$(PEARL_GRASS_IP)" \
	  --pearl_return="$(BRIGGS_RETURN)" --uav_rally="$(BRIGGS_RALLY)" \
	  $(LAUNCH_ARGS) $(SHORESIDE_ARGS)

pearl-briggs:
	cd "$(MISSION_DIR)" && ./launch_pearl.sh --mode=GRASS --site=briggs \
	  --ip="$(PEARL_GRASS_IP)" --shore="$(SHORE_IP)" --uav_ip="$(DRONE_IP)" \
	  --start_pos="$(BRIGGS_START)" --pearl_return="$(BRIGGS_RETURN)" \
	  $(LAUNCH_ARGS) $(PEARL_ARGS)

drone-briggs:
	cd "$(MISSION_DIR)" && ./launch_uav.sh --mode=REAL --site=briggs --model="$(MODEL)" --upload-params \
	  --ip="$(DRONE_IP)" --shore="$(SHORE_IP)" --pearl_ip="$(PEARL_GRASS_IP)" \
	  --secondary_landing="$(BRIGGS_RALLY)" $(LAUNCH_ARGS) $(DRONE_ARGS)

shoreside-charles:
	cd "$(MISSION_DIR)" && ./launch_shoreside.sh --mode=REAL --site=charles \
	  --ip="$(SHORE_IP)" --uav_ip="$(DRONE_IP)" --pearl_ip="$(PEARL_REAL_IP)" \
	  --pearl_return="$(CHARLES_RETURN)" --uav_rally="$(CHARLES_RALLY)" \
	  $(LAUNCH_ARGS) $(SHORESIDE_ARGS)

pearl-charles:
	cd "$(MISSION_DIR)" && ./launch_pearl.sh --mode=REAL --site=charles \
	  --ip="$(PEARL_REAL_IP)" --shore="$(SHORE_IP)" --uav_ip="$(DRONE_IP)" \
	  --start_pos="$(CHARLES_START)" --pearl_return="$(CHARLES_RETURN)" \
	  $(LAUNCH_ARGS) $(PEARL_ARGS)

drone-charles:
	cd "$(MISSION_DIR)" && ./launch_uav.sh --mode=REAL --site=charles --model="$(MODEL)" --upload-params \
	  --ip="$(DRONE_IP)" --shore="$(SHORE_IP)" --pearl_ip="$(PEARL_REAL_IP)" \
	  --secondary_landing="$(CHARLES_RALLY)" $(LAUNCH_ARGS) $(DRONE_ARGS)

# Site/geometry match the physical recipes; endpoints are deliberately local.
sim-briggs:
	cd "$(MISSION_DIR)" && ./launch.sh --mode=SITL --site=briggs --model="$(MODEL)" \
	  --pearl_start="$(BRIGGS_START)" --pearl_return="$(BRIGGS_RETURN)" \
	  --uav_rally="$(BRIGGS_RALLY)" \
	  $(SIM_WARP) $(LAUNCH_ARGS) $(SIM_ARGS)

sim-charles:
	cd "$(MISSION_DIR)" && ./launch.sh --mode=SITL --site=charles --model="$(MODEL)" \
	  --pearl_start="$(CHARLES_START)" --pearl_return="$(CHARLES_RETURN)" \
	  --uav_rally="$(CHARLES_RALLY)" \
	  $(SIM_WARP) $(LAUNCH_ARGS) $(SIM_ARGS)
