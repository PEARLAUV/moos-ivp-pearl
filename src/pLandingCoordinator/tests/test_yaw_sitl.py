#!/usr/bin/env python3
"""Opt-in elevated-deck SITL yaw/precision landing; isolated ports and owned cleanup.

Reuses the landing integration's strict flight validator. This test does not
exercise its separate post-disarm geometry-link-loss scenario or the Briggs FSMs.
"""
import argparse
import importlib.util
import json
import math
import os
import re
import shutil
import signal
import socket
import subprocess
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MISSION = ROOT / "missions/pearl_uav_integration_tests/landing"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--warp", type=int, default=2)
    parser.add_argument("--target-heading", type=float, default=205,
                        help="Isolated pad grid heading, independent of boat yaw; exercises a large turn")
    parser.add_argument("--rendezvous-distance", type=float, default=0.5,
                        help="Isolated capture tolerance; larger values exercise off-center LAND")
    parser.add_argument("--min-initial-error", type=float, default=0,
                        help="Require at least this many metres lateral error at AUTOLAND")
    args = parser.parse_args()
    if not 0 < args.rendezvous_distance <= 3 or args.min_initial_error < 0:
        parser.error("invalid isolated capture/error bounds")
    if args.warp < 1:
        parser.error("warp must be positive")
    spec = importlib.util.spec_from_file_location("recovery_fixture", MISSION / "tests/test_recovery_sitl.py")
    assert spec is not None and spec.loader is not None
    fixture = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixture)
    fixture.TRACKED.update({"YAW_ALIGN_REQUEST", "UAV_YAW_ALIGN_RESULT", "UAV_YAW",
                            "LANDING_OWN_YAW_GOAL", "LANDING_OWN_YAW_ERROR",
                            "LANDING_OWN_YAW_STATE", "LANDING_OWN_YAW_INPUT_REASON",
                            "PEARL_NAV_X", "PEARL_NAV_Y"})
    directory = Path(tempfile.mkdtemp(prefix="precision-yaw-sitl-"))
    for path in MISSION.iterdir():
        if path.name.startswith("meta_") or path.name in ("launch.sh", "plug_origin_warp.moos", "sitl.parm"):
            shutil.copy2(path, directory / path.name)
    for name in ("meta_uav.moos", "meta_pearl.moos"):
        path = directory / name
        text, count = re.subn(r"(rendezvous_distance\s*=\s*)[0-9.]+",
                              rf"\g<1>{args.rendezvous_distance}", path.read_text())
        assert count == 1, (name, count)
        path.write_text(text)
    mission = directory / "meta_uav.moos"
    text = mission.read_text()
    assert text.count("  role = aircraft") == 1
    text = text.replace("  role = aircraft", "  role = aircraft\n  align_yaw = true\n  yaw_timeout = 120")
    # Dedicated simulated pad axes; never overwrite PEARL navigation/ship pose.
    text, count = re.subn(r"target_nav_prefix\s*=\s*PEARL_NAV", "target_nav_prefix = PAD_NAV", text)
    assert count == 1
    text = text.replace("  Echo = UAV_YAW",
                        "  Echo = PEARL_NAV_LAT -> PAD_NAV_LAT\n"
                        "  Echo = PEARL_NAV_LONG -> PAD_NAV_LONG\n"
                        "  Echo = PEARL_NAV_X -> PAD_NAV_X\n"
                        "  Echo = PEARL_NAV_Y -> PAD_NAV_Y\n"
                        "  Echo = UAV_YAW")
    text = text.replace("  Run = pMavlinkLandingTarget",
                        "  Run = uTimerScript @ NewConsole = false ~ uPadOrientation\n"
                        "  Run = pMavlinkLandingTarget")
    text += f"""\nProcessConfig = uPadOrientation
{{
  AppTick = 20
  CommsTick = 20
  paused = false
  reset_max = unlimited
  reset_time = 0.1
  event = var=PAD_NAV_HEADING,val={args.target_heading},time=0
}}
"""
    mission.write_text(text)
    reservations, ports = [], []
    try:
        for _ in range(100):
            tcp = socket.socket()
            tcp.bind(("127.0.0.1", 0))
            port = tcp.getsockname()[1]
            udp = socket.socket(type=socket.SOCK_DGRAM)
            try:
                udp.bind(("127.0.0.1", port))
            except OSError:
                tcp.close()
                udp.close()
                continue
            reservations.extend((tcp, udp))
            ports.append(port)
            if len(ports) == 9:
                break
        assert len(ports) == 9, "Cannot reserve nine isolated TCP/UDP port pairs"
    finally:
        for sock in reservations:
            sock.close()
    names = ("shore_mport", "uav_mport", "pearl_mport", "shore_pshare", "uav_pshare",
             "pearl_pshare", "sitl_port", "sitl_target_port", "sitl_diagnostic_port")
    command = [str(directory / "launch.sh"), "-ng", "-x", str(args.warp), "--deck_altitude=1"]
    command.extend(f"--{name}={port}" for name, port in zip(names, ports))
    logs = fixture.Logs(directory)
    stop, messages = threading.Event(), []
    capture = threading.Thread(target=fixture.capture_status, args=(ports[-1], stop, messages), daemon=True)
    process = None
    result = {"result": "FAIL", "briggs_fsm_test": False, "post_disarm_loss_test": False}
    print("ARTIFACTS:", directory, flush=True)
    (directory / "run.json").write_text(json.dumps({"command": command, "ports": dict(zip(names, ports))}, indent=2))
    try:
        with (directory / "launcher.log").open("w") as output:
            process = subprocess.Popen(command, cwd=directory, stdout=output,
                                       stderr=subprocess.STDOUT, start_new_session=True)
        capture.start()
        fixture.wait_for(logs, process, lambda: logs.last["UAV"].get("UAV_AUTO_START") == "lane", 150)
        subprocess.run(["uPokeDB", str(directory / "targ_shoreside.moos"), "--quiet", "RECOVER_UAV:=true"],
                       check=True, capture_output=True, timeout=12)
        fixture.wait_for(logs, process, lambda: all(logs.last[n].get("LANDING_OWN_PHASE") == "COMPLETE"
                                                  for n in ("UAV", "PEARL")), 240)
        time.sleep(2)
        logs.update()
        result.update(fixture.validate(logs, messages, 1, 12))
        requests = list(dict.fromkeys(logs.matching("UAV", "YAW_ALIGN_REQUEST")))
        assert len(requests) == 1, requests
        accepted = list(dict.fromkeys(r for r in logs.matching("UAV", "UAV_YAW_ALIGN_RESULT")
                                      if "#status=ACCEPTED" in r[3]))
        assert len(accepted) == 1, accepted
        aligned = logs.first("UAV", "LANDING_OWN_YAW_STATE", "ALIGNED")
        land = logs.first("UAV", "ARDU_COMMAND", "AUTOLAND")
        assert requests[0][0] >= logs.first("PEARL", "LANDING_OWN_CLEARANCE", "true")
        assert requests[0][0] <= accepted[0][0] <= aligned <= land
        def latest_before(variable):
            values = [r for r in logs.matching("UAV", variable) if r[0] <= land + 0.01]
            assert values, variable
            return float(values[-1][3])
        initial_error = math.hypot(latest_before("NAV_X")-latest_before("PEARL_NAV_X"),
                                   latest_before("NAV_Y")-latest_before("PEARL_NAV_Y"))
        assert initial_error >= args.min_initial_error, initial_error
        yaw = latest_before("UAV_YAW")
        error = latest_before("LANDING_OWN_YAW_ERROR")
        assert abs(error) <= 5, (yaw, error)
        request_fields = dict(field.split("=", 1) for field in requests[0][3].split("#"))
        rotation = abs(float(request_fields["angle"]))
        assert rotation > 90, request_fields
        initial_yaw = float([r for r in logs.matching("UAV", "UAV_YAW")
                             if r[0] <= requests[0][0]][-1][3])
        result.update(result="PASS", initial_lateral_error_m=initial_error,
                      yaw_true_deg=yaw, target_relative_yaw_error_deg=error,
                      initial_yaw_true_deg=initial_yaw, requested_rotation_deg=rotation,
                      yaw_request=requests[0][3], yaw_ack=accepted[0][3])
        print(json.dumps(result, indent=2), flush=True)
    except Exception as error:
        result["error"] = repr(error)
        raise
    finally:
        stop.set()
        if capture.is_alive():
            capture.join(timeout=3)
        (directory / "mavlink_status.json").write_text(json.dumps(messages, indent=2))
        result["last"] = logs.last
        (directory / "result.json").write_text(json.dumps(result, indent=2))
        if process and process.poll() is None:
            process.send_signal(signal.SIGINT)
            try:
                process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5)
        logs.close()
        print(result["result"], directory, flush=True)


if __name__ == "__main__":
    main()
