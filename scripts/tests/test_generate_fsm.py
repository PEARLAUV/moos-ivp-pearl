"""Tests for scripts/generate_fsm.py."""

from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock

SCRIPT = Path(__file__).resolve().parents[1] / "generate_fsm.py"
SPEC = importlib.util.spec_from_file_location("generate_fsm", SCRIPT)
assert SPEC and SPEC.loader
GENERATOR = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = GENERATOR
SPEC.loader.exec_module(GENERATOR)


def minimal_config() -> dict:
    return {
        "version": 2,
        "outputs": {
            "runtime": "out_runtime.moos",
            "d2": "out.d2",
        },
        "facts": {"go": False},
        "machine": {
            "label": "Operation",
            "initial": "idle",
            "initial_layout": {},
        },
        "states": {
            "idle": {
                "label": "Idle",
                "transitions": {
                    "start": {
                        "to": "running",
                        "when": "go == true",
                        "label": "start work",
                    }
                },
            },
            "running": {
                "label": "Running",
                "initial": "waiting",
                "initial_layout": {},
                "states": {"waiting": {"label": "Waiting"}},
                "transitions": {
                    "stop": {
                        "to": "idle",
                        "when": "go == false",
                        "label": "stop work",
                    }
                },
            },
        },
        "targets": {
            "runtime": {
                "prefix": "TEST",
                "state_name_case": "upper_snake",
            },
            "d2": {"layout": {"direction": "right"}},
        },
    }


class GenerateFSMTest(unittest.TestCase):
    def test_removed_behavior_backend_is_not_a_cli_option(self) -> None:
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "missing.toml", "--backend", "bhv"],
            capture_output=True, text=True, check=False,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("unrecognized arguments: --backend bhv", result.stderr)

    def test_external_fact_is_subscribed_but_not_initialized(self) -> None:
        config = minimal_config()
        config["facts"]["go"] = {"initial": False, "initialize": False}

        runtime = GENERATOR.generate_runtime(config, "example.toml")

        self.assertNotIn("initialize = var=GO", runtime)
        self.assertIn(
            'transition = machine=operation, id=START, from=IDLE, '
            'to=RUNNING, guard="GO=true"',
            runtime,
        )

    def test_demo_clearance_rejection_only_matches_terminal_results(self) -> None:
        mission = (SCRIPT.parents[1] / "missions/charles_river"
                   / "2026_09_29_chevron_demo/uav_fsm.toml")
        config = GENERATOR.load_config(mission)
        runtime = GENERATOR.generate_runtime(config, str(mission))
        rejection = next(line for line in runtime.splitlines()
                         if "id=CLEARANCE_REJECTED," in line)

        for status in ("REJECTED", "CANCELLED", "TIMEOUT"):
            self.assertIn(f"TAKEOFF_REQUEST_RESULT={status}", rejection)
        self.assertNotIn("TAKEOFF_REQUEST_RESULT=UNKNOWN", rejection)
        self.assertNotIn("TAKEOFF_REQUEST_RESULT!=", rejection)

    def test_omitted_when_uses_active_source_and_preserves_effects(self) -> None:
        config = minimal_config()
        config["facts"]["mode"] = "idle"
        running = config["states"]["running"]
        running["transitions"]["stop"].pop("when")
        running["entry_effects"] = {"mode": "running"}
        running["exit_effects"] = {"mode": "left"}

        runtime = GENERATOR.generate_runtime(config, "example.toml")
        stop = next(line for line in runtime.splitlines() if "id=STOP," in line)
        self.assertIn('guard="TEST_FSM_OPERATION_STATE=RUNNING"', stop)
        self.assertIn("phase=entry, var=MODE", runtime)
        self.assertIn("phase=exit, var=MODE", runtime)

    def test_state_entry_epoch_is_opt_in_and_named_from_state(self) -> None:
        config = minimal_config()
        config["states"]["running"]["entry_epoch"] = True
        runtime = GENERATOR.generate_runtime(config, "example.toml")
        self.assertIn(
            "state = machine=operation, value=RUNNING, "
            "entry_epoch_var=TEST_FSM_OPERATION_STATE_RUNNING_EPOCH", runtime
        )
        self.assertNotIn("entry_epoch_var=TEST_FSM_OPERATION_STATE_IDLE_EPOCH", runtime)
        config["states"]["running"]["entry_epoch"] = "true"
        with self.assertRaisesRegex(GENERATOR.ConfigError, "entry_epoch must be a boolean"):
            GENERATOR.validate_fsm(config)
        config["states"]["running"]["entry_epoch"] = True
        config["facts"]["test_fsm_operation_state_running_epoch"] = "none"
        with self.assertRaisesRegex(GENERATOR.ConfigError, "must not initialize"):
            GENERATOR.generate_runtime(config, "example.toml")

    def test_empty_when_is_not_an_omitted_guard(self) -> None:
        config = minimal_config()
        config["states"]["running"]["transitions"]["stop"]["when"] = ""
        with self.assertRaisesRegex(GENERATOR.ConfigError, "when"):
            GENERATOR.validate_fsm(config)

    def test_runtime_backend_encodes_hierarchy_and_initialization(self) -> None:
        runtime = GENERATOR.generate_runtime(minimal_config(), "example.toml")

        self.assertIn(
            "runtime = state_var=TEST_FSM_RUNTIME_STATE, "
            "heartbeat_var=TEST_FSM_RUNTIME_HEARTBEAT, "
            "epoch_var=TEST_FSM_RUNTIME_EPOCH, heartbeat_interval=1",
            runtime,
        )
        self.assertIn("initialize = var=GO, type=string, value=false", runtime)
        self.assertIn("machine = id=operation", runtime)
        self.assertIn("state_var=TEST_FSM_OPERATION_STATE", runtime)
        self.assertIn("initially_active=true", runtime)
        self.assertIn("machine = id=running", runtime)
        self.assertIn("initially_active=false", runtime)
        self.assertIn(
            "substate = machine=operation, parent=RUNNING, "
            "var=TEST_FSM_RUNNING_STATE",
            runtime,
        )
        self.assertIn(
            "parent_state_var=TEST_FSM_OPERATION_STATE, parent_state=RUNNING",
            runtime,
        )

    def test_state_effects_are_typed_and_emitted_for_runtime(self) -> None:
        config = minimal_config()
        config["facts"].update({"mode": "idle", "attempts": 0})
        config["states"]["idle"]["entry_effects"] = {
            "go": False,
            "mode": "ready",
            "attempts": 1,
        }
        config["states"]["idle"]["exit_effects"] = {"mode": "departing"}

        runtime = GENERATOR.generate_runtime(config, "example.toml")
        bindings = json.loads(
            GENERATOR.generate_d2_bindings(config, "example.toml", [("out.d2", None)])
        )["diagrams"]["out.d2"]

        self.assertIn(
            "effect = machine=operation, state=IDLE, phase=exit, "
            "var=MODE, type=string, value=departing",
            runtime,
        )
        self.assertIn(
            "effect = machine=operation, state=IDLE, phase=entry, "
            "var=ATTEMPTS, type=double, value=1",
            runtime,
        )
        self.assertIn(
            "effect = machine=operation, state=IDLE, phase=entry, var=GO, "
            "type=string, value=false",
            runtime,
        )
        self.assertEqual(
            bindings["states"]["TEST_FSM_OPERATION_STATE"]["IDLE"]["effects"],
            {
                "entry": [
                    {
                        "fact": "go",
                        "moos_variable": "GO",
                        "type": "string",
                        "value": False,
                    },
                    {
                        "fact": "mode",
                        "moos_variable": "MODE",
                        "type": "string",
                        "value": "ready",
                    },
                    {
                        "fact": "attempts",
                        "moos_variable": "ATTEMPTS",
                        "type": "double",
                        "value": 1,
                    },
                ],
                "exit": [
                    {
                        "fact": "mode",
                        "moos_variable": "MODE",
                        "type": "string",
                        "value": "departing",
                    }
                ],
            },
        )

    def test_state_effect_rejects_undeclared_fact(self) -> None:
        config = minimal_config()
        config["states"]["idle"]["entry_effects"] = {"missing": True}

        with self.assertRaisesRegex(
            GENERATOR.ConfigError, "assigns undeclared fact missing"
        ):
            GENERATOR.validate_fsm(config)

    def test_state_effect_type_must_match_fact(self) -> None:
        config = minimal_config()
        config["states"]["idle"]["entry_effects"] = {"go": "true"}

        with self.assertRaisesRegex(
            GENERATOR.ConfigError, "must be a boolean to match fact go"
        ):
            GENERATOR.validate_fsm(config)

    def test_state_effect_collection_must_be_a_table(self) -> None:
        config = minimal_config()
        config["states"]["idle"]["entry_effects"] = ["go"]

        with self.assertRaisesRegex(
            GENERATOR.ConfigError, "states.idle.entry_effects must be a table"
        ):
            GENERATOR.validate_fsm(config)

    def test_state_effect_value_must_be_scalar(self) -> None:
        config = minimal_config()
        config["states"]["idle"]["exit_effects"] = {"go": [False]}

        with self.assertRaisesRegex(
            GENERATOR.ConfigError,
            "states.idle.exit_effects.go must be a string, boolean, or number",
        ):
            GENERATOR.validate_fsm(config)

    def test_fact_table_rejects_unknown_metadata(self) -> None:
        config = minimal_config()
        config["facts"]["go"] = {"initial": False, "owner": "peer"}

        with self.assertRaisesRegex(GENERATOR.ConfigError, "unsupported keys: owner"):
            GENERATOR.validate_fsm(config)

    def test_fact_table_requires_boolean_initialize(self) -> None:
        config = minimal_config()
        config["facts"]["go"] = {"initial": False, "initialize": "no"}

        with self.assertRaisesRegex(
            GENERATOR.ConfigError, "facts.go.initialize must be a boolean"
        ):
            GENERATOR.validate_fsm(config)

    def test_nested_boolean_guards_compile_as_binary_expressions(self) -> None:
        config = minimal_config()
        config["facts"].update({"a": False, "b": False, "c": False, "d": False})
        config["states"]["idle"]["transitions"]["start"]["when"] = (
            "(a == true && b == true && c == true) || d == true"
        )

        runtime = GENERATOR.generate_runtime(config, "example.toml")

        self.assertIn('guard="(((A=true) and (B=true)) and (C=true)) or (D=true)"', runtime)

    def test_check_result_compiles_and_retains_binding_provenance(self) -> None:
        config = minimal_config()
        config["facts"]["ready"] = {"initial": "inactive", "initialize": False}
        config["states"]["idle"]["transitions"]["start"]["when"] = (
            "check_result(ready, success) && go==true"
        )

        runtime = GENERATOR.generate_runtime(config, "example.toml")
        self.assertIn('guard="(READY=success) and (GO=true)", '
                      'check_snapshots=READY:READY_SNAPSHOT', runtime)
        self.assertNotIn("initialize = var=READY", runtime)
        bindings = json.loads(GENERATOR.generate_d2_bindings(
            config, "example.toml", [("out.d2", None)]
        ))["diagrams"]["out.d2"]["transitions"]["TEST_FSM_OPERATION_TRANSITION"]
        guard = bindings["START"]["guard"]
        self.assertEqual(guard["expression"],
                         "check_result(ready, success) && go==true")
        self.assertEqual(guard["moos_expression"], "(READY=success) and (GO=true)")
        self.assertEqual(guard["ast"]["operands"][0], {
            "type": "predicate",
            "left": {"type": "check_result", "result_fact": "ready",
                     "moos_variable": "READY", "snapshot_var": "READY_SNAPSHOT"},
            "operator": "==", "right": {"type": "literal", "value": "success"},
        })

    def test_check_result_validation(self) -> None:
        config = minimal_config()
        transition = config["states"]["idle"]["transitions"]["start"]
        config["facts"]["ready"] = {"initial": "inactive", "initialize": False}
        for expression, error in (
            ("check_result(missing, success)", "undeclared check result fact"),
            ("check_result(ready, approved)", "unknown check status"),
            ("check_result(ready)", "requires a comma"),
            ("check_result(ready, success, fail)", r"requires '\)'"),
            ("check_result(ready, success) == true", "unexpected"),
        ):
            with self.subTest(expression=expression):
                transition["when"] = expression
                with self.assertRaisesRegex(GENERATOR.ConfigError, error):
                    GENERATOR.validate_fsm(config)
        transition["when"] = "check_result(ready, success)"
        config["facts"]["ready"] = "inactive"
        with self.assertRaisesRegex(GENERATOR.ConfigError, "externally owned"):
            GENERATOR.validate_fsm(config)
        config["facts"]["ready"] = {"initial": 0, "initialize": False}
        with self.assertRaisesRegex(GENERATOR.ConfigError, "string fact"):
            GENERATOR.validate_fsm(config)
        config["facts"]["ready"] = {"initial": "inactive", "initialize": False}
        config["check_sets"] = {"readiness": {"result_fact": "ready"}}
        with self.assertRaisesRegex(GENERATOR.ConfigError, "check_sets is obsolete"):
            GENERATOR.validate_fsm(config)

    def test_check_result_default_sink_joins_parent_guard(self) -> None:
        config = minimal_config()
        config["facts"]["ready"] = {"initial": "inactive", "initialize": False}
        running = config["states"]["running"]
        running["diagram"] = True
        running["states"]["waiting"]["transitions"] = {
            "waiting_to_sink": {"to": "sink", "label": "ready",
                                "when": "check_result(ready, success)"}
        }
        runtime = GENERATOR.generate_runtime(config, "example.toml")
        parent = next(line for line in runtime.splitlines() if "id=STOP," in line)
        self.assertIn("READY=success", parent)
        self.assertIn("GO=false", parent)
        self.assertIn("TEST_FSM_RUNNING_STATE=WAITING", parent)
        self.assertIn("check_snapshots=READY:READY_SNAPSHOT", parent)
        self.assertNotIn("id=WAITING_TO_SINK", runtime)
        bindings = json.loads(GENERATOR.generate_d2_bindings(
            config, "example.toml", [("out.d2", None)]
        ))["diagrams"]["out.d2"]["transitions"]["TEST_FSM_OPERATION_TRANSITION"]
        self.assertIn("READY=success", bindings["STOP"]["guard"]["moos_expression"])
        self.assertIn("check_result", json.dumps(bindings["STOP"]["guard"]["effective_ast"]))

    def test_check_result_named_sink_retains_provenance(self) -> None:
        config = minimal_config()
        config["facts"]["ready"] = {"initial": "inactive", "initialize": False}
        running = config["states"]["running"]
        running["diagram"] = True
        running["transitions"].pop("stop")
        running["sinks"] = {"denied": {"label": "Denied"}}
        running["transitions"]["deny"] = {"to": "idle", "exit": "denied"}
        running["states"]["waiting"]["transitions"] = {
            "waiting_denied": {"to": "sink.denied", "label": "denied",
                               "when": "check_result(ready, fail)"}
        }
        runtime = GENERATOR.generate_runtime(config, "example.toml")
        self.assertIn('id=WAITING_DENIED, from=RUNNING, to=IDLE, '
                      'guard="(TEST_FSM_RUNNING_STATE=WAITING) and (READY=fail)", '
                      'check_snapshots=READY:READY_SNAPSHOT', runtime)
        bindings = json.loads(GENERATOR.generate_d2_bindings(
            config, "example.toml", [("out.d2", None)]
        ))["diagrams"]["out.d2"]["transitions"]["TEST_FSM_OPERATION_TRANSITION"]
        self.assertIn("check_result", json.dumps(bindings["WAITING_DENIED"]["guard"]["ast"]))

    def test_bindings_preserve_diagram_order(self) -> None:
        diagrams = [
            ("out.d2", None),
            ("out.d2#layers.running", "running"),
        ]

        bindings = json.loads(
            GENERATOR.generate_d2_bindings(minimal_config(), "example.toml", diagrams)
        )

        self.assertEqual(
            bindings["diagram_order"],
            ["out.d2", "out.d2#layers.running"],
        )
        self.assertEqual(list(bindings["diagrams"]), bindings["diagram_order"])

    def test_state_bindings_include_single_line_labels_in_each_diagram(self) -> None:
        config = minimal_config()
        config["states"]["idle"]["label"] = "Waiting\nfor work"
        config["states"]["running"]["states"]["waiting"]["label"] = (
            "Awaiting\nnext step"
        )
        bindings = json.loads(
            GENERATOR.generate_d2_bindings(
                config,
                "example.toml",
                [("out.d2", None), ("out.d2#layers.running", "running")],
            )
        )["diagrams"]

        overview = bindings["out.d2"]["states"]
        detail = bindings["out.d2#layers.running"]["states"]
        self.assertEqual(
            overview["TEST_FSM_OPERATION_STATE"]["IDLE"]["label"],
            "Waiting for work",
        )
        self.assertEqual(
            overview["TEST_FSM_OPERATION_STATE"]["RUNNING"]["label"],
            "Running",
        )
        for diagram in (overview, detail):
            self.assertEqual(
                diagram["TEST_FSM_RUNNING_STATE"]["WAITING"]["label"],
                "Awaiting next step",
            )

    def test_one_state_definition_drives_runtime_and_d2(self) -> None:
        config = minimal_config()

        runtime = GENERATOR.generate_runtime(config, "example.toml")
        pshare = GENERATOR.generate_fsm_pshare(config, "example.toml")
        scopes = GENERATOR.generate_fsm_scopes(config, "example.toml")
        d2 = GENERATOR.generate_d2(config, "example.toml")
        bindings = json.loads(
            GENERATOR.generate_d2_bindings(config, "example.toml", [("out.d2", None)])
        )["diagrams"]["out.d2"]

        self.assertIn("machine = id=operation, state_var=TEST_FSM_OPERATION_STATE", runtime)
        self.assertIn("initial_node=start, initial_transition_id=INITIAL", runtime)
        self.assertEqual(runtime.count("initially_active=true"), 1)
        self.assertEqual(runtime.count("initially_active=false"), 1)
        self.assertIn("active_state_var=TEST_FSM_STATE", runtime)
        self.assertIn("state = machine=operation, value=RUNNING", runtime)
        self.assertIn("substate = machine=operation, parent=RUNNING, "
                      "var=TEST_FSM_RUNNING_STATE", runtime)
        self.assertIn("src_name=TEST_FSM_RUNTIME_STATE", pshare)
        self.assertIn("src_name=TEST_FSM_RUNTIME_HEARTBEAT", pshare)
        self.assertIn("src_name=TEST_FSM_RUNTIME_EPOCH", pshare)
        self.assertEqual(pshare.count("output ="), 4)
        self.assertNotIn("src_name=TEST_FSM_OPERATION_STATE", pshare)
        self.assertNotIn("src_name=TEST_FSM_OPERATION_TRANSITION", pshare)
        self.assertNotIn("src_name=TEST_FSM_RUNNING_STATUS", pshare)
        self.assertNotIn("_COUNT", pshare)
        self.assertIn("src_name=FSM_TRANSITION, dest_name=TEST_FSM_TRANSITION", pshare)
        self.assertIn("route=$(SHORE_IP):$(SHORE_PSHARE)", pshare)
        self.assertIn("scope = TEST_FSM_RUNTIME_STATE", scopes)
        self.assertIn("scope = TEST_FSM_RUNTIME_HEARTBEAT", scopes)
        self.assertIn("scope = TEST_FSM_RUNTIME_EPOCH", scopes)
        self.assertEqual(scopes.count("scope ="), 4)
        self.assertNotIn("scope = TEST_FSM_OPERATION_STATE", scopes)
        self.assertNotIn("scope = TEST_FSM_OPERATION_TRANSITION", scopes)
        self.assertNotIn("scope = TEST_FSM_RUNNING_STATUS", scopes)
        self.assertNotIn("_COUNT", scopes)
        self.assertIn("scope = TEST_FSM_TRANSITION", scopes)
        self.assertIn("parent_state_var=TEST_FSM_OPERATION_STATE", runtime)
        self.assertIn("parent_state=RUNNING", runtime)
        self.assertIn("reset_on_inactive=true", runtime)
        self.assertIn("initialize = var=GO, type=string, value=false", runtime)
        self.assertIn(
            'transition = machine=operation, id=START, from=IDLE, '
            'to=RUNNING, guard="GO=true"', runtime
        )
        self.assertIn('running: "Running"', d2)
        self.assertIn(
            "class: [composite; moos_state; "
            "moos_var_TEST_FSM_OPERATION_STATE; moos_value_RUNNING]",
            d2,
        )
        self.assertIn(
            "class: [transition; moos_transition; "
            "moos_var_TEST_FSM_OPERATION_TRANSITION; moos_id_START]",
            d2,
        )
        self.assertIn("moos_id_INITIAL", d2)
        self.assertIn('idle -> running: "start work"', d2)
        self.assertRegex(
            d2,
            r'idle -> running: "start work" \{\n'
            r"  class: .*\n  style: \{\n    stroke: \"#12344D\"",
        )
        self.assertIn('running -> idle: "stop work"', d2)
        self.assertIn("shape: circle", d2)
        self.assertIn('fill: "#12344D"', d2)
        self.assertIn('stroke: "#12344D"', d2)
        self.assertIn("shape: rectangle", d2)
        self.assertIn("stroke-width: 2", d2)
        self.assertIn("border-radius: 8", d2)
        self.assertIn("layout-engine: tala", d2)
        self.assertIn("direction: right", d2)
        self.assertNotIn("title:", d2)
        self.assertNotIn("classes: {", d2)
        self.assertEqual(
            bindings["states"]["TEST_FSM_OPERATION_STATE"]["RUNNING"]["d2_id"],
            "running",
        )
        self.assertEqual(
            bindings["states"]["TEST_FSM_OPERATION_STATE"]["RUNNING"]["count_variable"],
            "TEST_FSM_OPERATION_STATE_RUNNING_COUNT",
        )
        initial_binding = bindings["transitions"]["TEST_FSM_OPERATION_TRANSITION"][
            "INITIAL"
        ]
        self.assertEqual(initial_binding["from"]["d2_id"], "start")
        self.assertEqual(initial_binding["to"]["d2_id"], "idle")
        self.assertEqual(initial_binding["guard"]["expression"], "true")
        self.assertEqual(
            initial_binding["selector"],
            ".moos_var_TEST_FSM_OPERATION_TRANSITION.moos_id_INITIAL",
        )
        start_binding = bindings["transitions"]["TEST_FSM_OPERATION_TRANSITION"][
            "START"
        ]
        self.assertEqual(start_binding["d2_id"], "(idle -> running)[0]")
        self.assertEqual(start_binding["from"]["moos_value"], "IDLE")
        self.assertEqual(start_binding["to"]["moos_value"], "RUNNING")
        expected_ast = {
            "type": "predicate",
            "left": {
                "type": "fact",
                "id": "go",
                "moos_variable": "GO",
            },
            "operator": "==",
            "right": {"type": "literal", "value": True},
        }
        self.assertEqual(start_binding["guard"]["expression"], "go == true")
        self.assertEqual(start_binding["guard"]["moos_expression"], "GO=true")
        self.assertEqual(start_binding["guard"]["ast"], expected_ast)
        self.assertEqual(start_binding["guard"]["effective_ast"], expected_ast)
        self.assertEqual(
            start_binding["selector"],
            ".moos_var_TEST_FSM_OPERATION_TRANSITION.moos_id_START",
        )

    def test_marks_active_initial_composite_hierarchy(self) -> None:
        config = minimal_config()
        config["machine"]["initial"] = "running"

        runtime = GENERATOR.generate_runtime(config, "example.toml")

        self.assertEqual(runtime.count("initially_active=true"), 2)
        self.assertNotIn("initially_active=false", runtime)
        self.assertIn("initial_state=WAITING", runtime)

    def test_reserves_initial_transition_id(self) -> None:
        config = minimal_config()
        transition = config["states"]["idle"]["transitions"].pop("start")
        config["states"]["idle"]["transitions"]["initial"] = transition

        with self.assertRaisesRegex(GENERATOR.ConfigError, "reserved"):
            GENERATOR.generate_runtime(config, "example.toml")

    def test_renders_single_svg_as_index(self) -> None:
        def fake_run(command: list[str], **_: object) -> subprocess.CompletedProcess:
            Path(command[-1]).write_text("<svg>overview</svg>")
            return subprocess.CompletedProcess(command, 0, "", "")

        with mock.patch.object(GENERATOR.subprocess, "run", side_effect=fake_run):
            files = GENERATOR._render_d2_svg_set("a -> b\n", Path("/test/d2"))

        self.assertEqual(files, {"index.svg": b"<svg>overview</svg>"})

    def test_renders_linked_svg_set_with_tala(self) -> None:
        commands: list[list[str]] = []

        def fake_run(command: list[str], **_: object) -> subprocess.CompletedProcess:
            commands.append(command)
            rendered_dir = Path(command[-1]).with_suffix("")
            rendered_dir.mkdir()
            (rendered_dir / "index.svg").write_text("<svg>overview</svg>")
            (rendered_dir / "detail.svg").write_text("<svg>detail</svg>")
            return subprocess.CompletedProcess(command, 0, "", "")

        with mock.patch.object(GENERATOR.subprocess, "run", side_effect=fake_run):
            files = GENERATOR._render_d2_svg_set("a -> b\n", Path("/test/d2"))

        self.assertEqual(files["index.svg"], b"<svg>overview</svg>")
        self.assertEqual(files["detail.svg"], b"<svg>detail</svg>")
        self.assertIn("--layout=tala", commands[0])
        self.assertIn("--pad=10", commands[0])
        self.assertIn("--omit-version", commands[0])

    def test_binding_guard_ast_includes_machine_state_references(self) -> None:
        config = minimal_config()
        config["states"]["idle"]["transitions"]["start"]["when"] = (
            "(go == true) && (running.state == waiting)"
        )

        bindings = json.loads(
            GENERATOR.generate_d2_bindings(config, "example.toml", [("out.d2", None)])
        )["diagrams"]["out.d2"]
        guard = bindings["transitions"]["TEST_FSM_OPERATION_TRANSITION"]["START"][
            "guard"
        ]

        self.assertEqual(guard["ast"]["type"], "boolean")
        self.assertEqual(guard["ast"]["operator"], "and")
        self.assertEqual(
            guard["ast"]["operands"][1],
            {
                "type": "predicate",
                "left": {
                    "type": "machine_state",
                    "machine": "running",
                    "moos_variable": "TEST_FSM_RUNNING_STATE",
                },
                "operator": "==",
                "right": {
                    "type": "state",
                    "machine": "running",
                    "id": "waiting",
                    "moos_value": "WAITING",
                },
            },
        )

    def test_nested_transition_to_ancestor_state_is_lifted(self) -> None:
        config = minimal_config()
        config["states"]["running"]["states"]["waiting"]["transitions"] = {
            "exit": {
                "to": "idle",
                "when": "go == false",
                "label": "exit nested state",
            }
        }

        runtime = GENERATOR.generate_runtime(config, "example.toml")
        d2 = GENERATOR.generate_d2(config, "example.toml")
        bindings = json.loads(
            GENERATOR.generate_d2_bindings(config, "example.toml", [("out.d2", None)])
        )["diagrams"]["out.d2"]
        binding = bindings["transitions"]["TEST_FSM_OPERATION_TRANSITION"]["EXIT"]

        self.assertIn(
            "transition = machine=operation, id=EXIT, from=RUNNING, to=IDLE, "
            'guard="(TEST_FSM_RUNNING_STATE=WAITING) and (GO=false)"',
            runtime,
        )
        self.assertIn('running.waiting -> idle: "exit nested state"', d2)
        self.assertEqual(binding["from"]["machine"], "running")
        self.assertEqual(binding["from"]["state_id"], "waiting")
        self.assertEqual(binding["runtime"]["from"], "RUNNING")
        self.assertEqual(
            binding["guard"]["source_constraint"]["moos_expression"],
            "TEST_FSM_RUNNING_STATE=WAITING",
        )
        self.assertEqual(binding["guard"]["ast"]["left"]["id"], "go")
        self.assertEqual(binding["guard"]["effective_ast"]["type"], "boolean")

    def test_composite_diagram_boundary_creates_linked_layer(self) -> None:
        config = minimal_config()
        running = config["states"]["running"]
        running["diagram"] = True
        running["states"]["waiting"]["transitions"] = {
            "waiting_to_sink": {
                "to": "sink",
                "label": "detail complete",
            }
        }

        overview = GENERATOR.generate_d2(config, "example.toml")
        composition = GENERATOR.generate_d2_composition(config, "example.toml")
        bindings = json.loads(
            GENERATOR.generate_d2_bindings(
                config, "example.toml", [("overview.d2", None)]
            )
        )["diagrams"]["overview.d2"]["transitions"]["TEST_FSM_OPERATION_TRANSITION"]

        self.assertNotIn('waiting: "Waiting"', overview)
        self.assertIn("link: layers.running", composition)
        self.assertIn("layers: {\n  running: {", composition)
        self.assertIn('waiting: "Waiting"', composition)
        self.assertNotIn("overview_link", composition)
        self.assertEqual(composition.count("link: _"), 2)
        self.assertRegex(
            composition,
            r'start: "" \{\n +class: initial\n +shape: circle\n +width: 34\n +height: 34',
        )
        self.assertRegex(
            composition,
            r'sink: "" \{\n +class: sink\n +link: _\n +shape: oval\n +width: 34\n +height: 34',
        )
        self.assertIn('icon: "data:image/svg+xml', composition)
        self.assertIn('fill: "transparent"', composition)
        self.assertNotIn("double-border", composition)
        self.assertIn('waiting -> sink: "detail complete"', composition)
        self.assertIn("class: [transition; sink_transition]", composition)
        self.assertEqual(composition.count('running -> idle: "stop work"'), 1)
        self.assertIn("moos_id_STOP", overview)
        self.assertEqual(bindings["STOP"]["label"], "stop work")
        self.assertNotIn("WAITING_TO_SINK", bindings)
        self.assertEqual(GENERATOR._diagram_boundaries(config), ["running"])

    def test_default_sink_guard_ands_with_parent_guard(self) -> None:
        config = minimal_config()
        config["facts"]["ready"] = False
        running = config["states"]["running"]
        running["diagram"] = True
        running["states"]["waiting"]["transitions"] = {
            "waiting_to_sink": {
                "to": "sink",
                "when": "ready == true",
                "label": "detail ready",
            }
        }

        runtime = GENERATOR.generate_runtime(config, "example.toml")
        composition = GENERATOR.generate_d2_composition(config, "example.toml")
        bindings = json.loads(
            GENERATOR.generate_d2_bindings(config, "example.toml", [("out.d2", None)])
        )["diagrams"]["out.d2"]["transitions"]["TEST_FSM_OPERATION_TRANSITION"]

        parent_exit = next(line for line in runtime.splitlines() if "id=STOP," in line)
        self.assertIn("GO=false", parent_exit)
        self.assertIn("READY=true", parent_exit)
        self.assertIn("TEST_FSM_RUNNING_STATE=WAITING", parent_exit)
        self.assertNotIn("id=WAITING_TO_SINK", runtime)
        self.assertIn('waiting -> sink: "detail ready"', composition)
        self.assertIn("READY=true", bindings["STOP"]["guard"]["moos_expression"])
        self.assertIn("WAITING", bindings["STOP"]["guard"]["source_constraint"]["moos_expression"])

    def test_default_sink_and_parent_without_when_exit_on_entry(self) -> None:
        config = minimal_config()
        running = config["states"]["running"]
        running["diagram"] = True
        running["transitions"]["stop"].pop("when")
        running["states"]["waiting"]["transitions"] = {
            "waiting_to_sink": {"to": "sink", "label": "done"}
        }

        runtime = GENERATOR.generate_runtime(config, "example.toml")
        stop = next(line for line in runtime.splitlines() if "id=STOP," in line)
        self.assertIn("TEST_FSM_OPERATION_STATE=RUNNING", stop)
        self.assertIn("TEST_FSM_RUNNING_STATE=WAITING", stop)
        self.assertNotIn("id=WAITING_TO_SINK", runtime)

    def test_default_sink_joins_multiple_children_and_parent_routes(self) -> None:
        config = minimal_config()
        config["facts"]["ready"] = False
        running = config["states"]["running"]
        running["diagram"] = True
        config["states"]["blocked"] = {"label": "Blocked"}
        running["transitions"]["stop"]["when"] = "go == false"
        running["transitions"]["redirect"] = {
            "to": "blocked", "when": "go == true", "label": "redirect"
        }
        running["states"]["failed"] = {
            "label": "Failed",
            "transitions": {"failed_to_sink": {"to": "sink", "label": "failed"}},
        }
        running["states"]["waiting"]["transitions"] = {
            "waiting_to_failed": {
                "to": "failed", "when": "go == true", "label": "failure"
            },
            "waiting_to_sink": {
                "to": "sink", "when": "ready == true", "label": "ready"
            },
        }

        runtime = GENERATOR.generate_runtime(config, "example.toml")
        for transition_id in ("STOP", "REDIRECT"):
            parent_exit = next(
                line for line in runtime.splitlines() if f"id={transition_id}," in line
            )
            for expected in ("READY=true", "GO=", "WAITING", "FAILED", " or "):
                self.assertIn(expected, parent_exit)
            self.assertNotIn("id=WAITING_TO_SINK", runtime)
            self.assertNotIn("id=FAILED_TO_SINK", runtime)

    def test_named_sink_guards_compile_to_parent_exit_routes(self) -> None:
        config = minimal_config()
        running = config["states"]["running"]
        running["diagram"] = True
        running["sinks"] = {"failed": {"label": "Failure route"}}
        running["transitions"]["stop"].pop("when")
        running["transitions"]["stop"].pop("label")
        running["transitions"]["stop"]["exit"] = "completed_successfully"
        running["transitions"]["fail"] = {
            "exit": "failed",
            "to": "blocked",
        }
        running["states"]["waiting"]["transitions"] = {
            "waiting_completed": {
                "to": "sink.completed_successfully",
                "when": "go == false",
                "label": "completed",
            },
            "waiting_failed": {
                "to": "sink.failed",
                "when": "go == true",
                "label": "failed",
            },
            "waiting_failed_again": {
                "to": "sink.failed",
                "when": "go != true",
                "label": "failed another way",
            },
        }
        config["states"]["blocked"] = {"label": "Blocked"}

        runtime = GENERATOR.generate_runtime(config, "example.toml")
        composition = GENERATOR.generate_d2_composition(config, "example.toml")
        bindings = json.loads(
            GENERATOR.generate_d2_bindings(
                config,
                "example.toml",
                [("overview.d2", None), ("overview.d2#layers.running", "running")],
            )
        )

        self.assertIn(
            "transition = machine=operation, id=WAITING_COMPLETED, "
            'from=RUNNING, to=IDLE, guard="(TEST_FSM_RUNNING_STATE=WAITING) and (GO=false)"',
            runtime,
        )
        self.assertIn(
            "transition = machine=operation, id=WAITING_FAILED, "
            'from=RUNNING, to=BLOCKED, guard="(TEST_FSM_RUNNING_STATE=WAITING) and (GO=true)"',
            runtime,
        )
        self.assertNotIn("id=STOP,", runtime)
        self.assertIn(
            'sink_completed_successfully: "completed successfully"', composition
        )
        self.assertIn('sink_failed: "Failure route"', composition)
        self.assertIn(
            'waiting -> sink_completed_successfully: "completed"', composition
        )
        self.assertIn('waiting -> sink_failed: "failed"', composition)
        self.assertIn('running -> idle: "[completed successfully]"', composition)
        parent_failure_edge = 'running -> blocked: "[Failure route]"'
        self.assertEqual(composition.count(parent_failure_edge), 1)
        self.assertEqual(composition.count("waiting -> sink_failed:"), 2)
        self.assertIn("moos_id_WAITING_COMPLETED", composition)
        self.assertIn("moos_id_WAITING_FAILED", composition)
        self.assertIn("moos_id_WAITING_FAILED_AGAIN", composition)
        overview_bindings = bindings["diagrams"]["overview.d2"]["transitions"]
        detail_bindings = bindings["diagrams"]["overview.d2#layers.running"][
            "transitions"
        ]
        self.assertEqual(
            overview_bindings["TEST_FSM_OPERATION_TRANSITION"]["WAITING_FAILED"][
                "label"
            ],
            "[Failure route]",
        )
        self.assertEqual(
            overview_bindings["TEST_FSM_OPERATION_TRANSITION"]["WAITING_FAILED"][
                "exit"
            ]["id"],
            "failed",
        )
        failed_binding = overview_bindings["TEST_FSM_OPERATION_TRANSITION"][
            "WAITING_FAILED"
        ]
        failed_again_binding = overview_bindings["TEST_FSM_OPERATION_TRANSITION"][
            "WAITING_FAILED_AGAIN"
        ]
        self.assertEqual(failed_binding["exit"]["label"], "Failure route")
        self.assertEqual(failed_binding["d2_id"], failed_again_binding["d2_id"])
        completed_target = detail_bindings["TEST_FSM_OPERATION_TRANSITION"][
            "WAITING_COMPLETED"
        ]["to"]
        self.assertEqual(completed_target["type"], "sink")
        self.assertEqual(completed_target["label"], "completed successfully")

    def test_named_sink_requires_matching_parent_exit(self) -> None:
        config = minimal_config()
        running = config["states"]["running"]
        running["diagram"] = True
        running["states"]["waiting"]["transitions"] = {
            "waiting_completed": {
                "to": "sink.completed",
                "when": "go == false",
                "label": "completed",
            }
        }

        with self.assertRaisesRegex(GENERATOR.ConfigError, "no matching parent"):
            GENERATOR.validate_fsm(config)

    def test_named_sink_without_when_uses_active_source(self) -> None:
        config = minimal_config()
        running = config["states"]["running"]
        running["diagram"] = True
        running["transitions"]["stop"].pop("when")
        running["transitions"]["stop"]["exit"] = "completed"
        running["states"]["waiting"]["transitions"] = {
            "waiting_completed": {
                "to": "sink.completed",
                "label": "completed",
            }
        }

        runtime = GENERATOR.generate_runtime(config, "example.toml")
        self.assertIn(
            'id=WAITING_COMPLETED, from=RUNNING, to=IDLE, '
            'guard="TEST_FSM_RUNNING_STATE=WAITING"',
            runtime,
        )

    def test_diagram_boundary_requires_nested_sink_transition(self) -> None:
        config = minimal_config()
        config["states"]["running"]["diagram"] = True

        with self.assertRaisesRegex(GENERATOR.ConfigError, "no nested state"):
            GENERATOR.generate_d2_composition(config, "example.toml")

    def test_supports_near_and_top_left_layout(self) -> None:
        config = minimal_config()
        config["machine"]["initial_layout"] = {"top": 10, "left": 20}
        config["states"]["idle"]["layout"] = {
            "near": "center-left",
            "width": 240,
            "height": 80,
        }

        d2 = GENERATOR.generate_d2(config, "example.toml")

        self.assertIn("top: 10", d2)
        self.assertIn("left: 20", d2)
        self.assertIn("near: center-left", d2)
        self.assertIn("width: 240", d2)
        self.assertIn("height: 80", d2)

    def test_rejects_incomplete_or_conflicting_position_layout(self) -> None:
        config = minimal_config()
        config["states"]["idle"]["layout"] = {"top": 10}
        with self.assertRaisesRegex(GENERATOR.ConfigError, "configured together"):
            GENERATOR.generate_d2(config, "example.toml")

        config = minimal_config()
        config["states"]["idle"]["layout"] = {
            "top": 10,
            "left": 20,
            "near": "center-left",
        }
        with self.assertRaisesRegex(GENERATOR.ConfigError, "cannot combine"):
            GENERATOR.generate_d2(config, "example.toml")

    def test_region_borders_are_dashed(self) -> None:
        config = minimal_config()
        running = config["states"]["running"]
        running.pop("states")
        running["regions"] = {
            "lane": {
                "label": "Lane",
                "initial": "waiting",
                "states": {"waiting": {"label": "Waiting"}},
            }
        }

        d2 = GENERATOR.generate_d2(config, "example.toml")

        self.assertRegex(
            d2,
            r'lane: "Lane" \{\n  +class: region\n  +shape: rectangle\n'
            r"  +style: \{[^}]*stroke-dash: 5",
        )

    def test_rejects_state_with_states_and_regions(self) -> None:
        config = minimal_config()
        running = config["states"]["running"]
        running["regions"] = {"parallel": {"initial": "idle", "states": {}}}

        with self.assertRaisesRegex(GENERATOR.ConfigError, "both states and regions"):
            GENERATOR.generate_runtime(config, "example.toml")

    def test_rejects_obsolete_children(self) -> None:
        config = minimal_config()
        config["states"]["idle"]["children"] = []

        with self.assertRaisesRegex(GENERATOR.ConfigError, "children is obsolete"):
            GENERATOR.generate_runtime(config, "example.toml")

    def test_history_nodes_are_inferred_from_incoming_transitions(self) -> None:
        config = minimal_config()
        states = config["states"]
        running = states["running"]
        transitions = states["idle"]["transitions"]
        transitions["start"]["history"] = {
            "machine": "running",
            "state": "deep_history",
        }
        transitions["start_shallow"] = {
            "to": "running",
            "when": "go == true",
            "label": "start with shallow history",
            "history": {"machine": "running", "state": "history"},
        }
        running["history_layout"] = {}
        running["deep_history_layout"] = {}

        runtime = GENERATOR.generate_runtime(config, "example.toml")
        d2 = GENERATOR.generate_d2(config, "example.toml")

        self.assertIn("reset_on_inactive=false", runtime)
        self.assertIn('history: "H"', d2)
        self.assertIn('deep_history: "H*"', d2)
        self.assertIn("idle -> running.deep_history", d2)
        self.assertIn("idle -> running.history", d2)

    def test_rejects_nested_root_states(self) -> None:
        config = minimal_config()
        config["machine"]["states"] = config.pop("states")

        with self.assertRaisesRegex(
            GENERATOR.ConfigError, "machine.states is obsolete"
        ):
            GENERATOR.generate_runtime(config, "example.toml")

    def test_rejects_plural_machines(self) -> None:
        config = minimal_config()
        config["machines"] = {"operation": config.pop("machine")}

        with self.assertRaisesRegex(GENERATOR.ConfigError, "machines is obsolete"):
            GENERATOR.generate_runtime(config, "example.toml")

    def test_rejects_obsolete_fsm_wrapper(self) -> None:
        config = minimal_config()
        config["fsm"] = {
            "facts": config.pop("facts"),
            "machines": {"operation": config.pop("machine")},
        }

        with self.assertRaisesRegex(GENERATOR.ConfigError, "fsm table is obsolete"):
            GENERATOR.generate_runtime(config, "example.toml")

    def test_rejects_obsolete_d2_view(self) -> None:
        config = minimal_config()
        config["views"] = {"d2": config["targets"].pop("d2")}

        with self.assertRaisesRegex(GENERATOR.ConfigError, "views is obsolete"):
            GENERATOR.generate_d2(config, "example.toml")

    def test_rejects_class_definitions(self) -> None:
        config = minimal_config()
        config["targets"]["d2"]["class_definitions"] = {
            "state": {"style": {"stroke-width": 2}}
        }

        with self.assertRaisesRegex(GENERATOR.ConfigError, "unsupported targets.d2"):
            GENERATOR.generate_d2(config, "example.toml")

    def test_rejects_explicit_classes(self) -> None:
        config = minimal_config()
        config["states"]["idle"]["class"] = "state"

        with self.assertRaisesRegex(GENERATOR.ConfigError, "classes are inferred"):
            GENERATOR.generate_d2(config, "example.toml")

    def test_rejects_graphics(self) -> None:
        config = minimal_config()
        config["states"]["idle"]["graphics"] = {"shape": "rectangle"}

        with self.assertRaisesRegex(GENERATOR.ConfigError, "graphics is obsolete"):
            GENERATOR.generate_d2(config, "example.toml")

    def test_rejects_d2_node_declarations(self) -> None:
        config = minimal_config()
        config["targets"]["d2"]["nodes"] = [{"state": "operation.idle"}]

        with self.assertRaisesRegex(GENERATOR.ConfigError, "unsupported targets.d2"):
            GENERATOR.generate_d2(config, "example.toml")

    def test_rejects_d2_edge_declarations(self) -> None:
        config = minimal_config()
        config["targets"]["d2"]["edges"] = [{"transition": "operation.start"}]

        with self.assertRaisesRegex(GENERATOR.ConfigError, "unsupported targets.d2"):
            GENERATOR.generate_d2(config, "example.toml")

    def test_requires_transition_labels(self) -> None:
        config = minimal_config()
        del config["states"]["idle"]["transitions"]["start"]["label"]

        with self.assertRaisesRegex(GENERATOR.ConfigError, "label"):
            GENERATOR.generate_runtime(config, "example.toml")

    def test_rejects_duplicate_transition_ids(self) -> None:
        config = minimal_config()
        config["states"]["running"]["transitions"]["start"] = {
            "to": "idle",
            "when": "go == false",
            "label": "duplicate ID",
        }

        with self.assertRaisesRegex(GENERATOR.ConfigError, "duplicate transition ID"):
            GENERATOR.generate_runtime(config, "example.toml")

    def test_rejects_transition_groups(self) -> None:
        config = minimal_config()
        config["states"]["idle"]["transitions"]["start"]["group"] = "phase"

        with self.assertRaisesRegex(GENERATOR.ConfigError, "group is obsolete"):
            GENERATOR.generate_runtime(config, "example.toml")

    def test_rejects_explicit_runtime_fact_variables(self) -> None:
        config = minimal_config()
        config["targets"]["runtime"]["fact_vars"] = {"go": "GO"}

        with self.assertRaisesRegex(GENERATOR.ConfigError, "fact variables are inferred"):
            GENERATOR.generate_runtime(config, "example.toml")

    def test_rejects_explicit_runtime_machine_targets(self) -> None:
        config = minimal_config()
        config["targets"]["runtime"]["machines"] = {}

        with self.assertRaisesRegex(GENERATOR.ConfigError, "machine targets are inferred"):
            GENERATOR.generate_runtime(config, "example.toml")

    def test_rejects_initialization_groups(self) -> None:
        config = minimal_config()
        config["targets"]["runtime"]["initialization_groups"] = [{"id": "inputs"}]

        with self.assertRaisesRegex(GENERATOR.ConfigError, "obsolete"):
            GENERATOR.generate_runtime(config, "example.toml")

    def test_machine_activation_condition_and_history_are_emitted(self) -> None:
        config = minimal_config()
        config["machine"]["condition"] = "go==false"
        config["machine"]["history"] = True

        runtime = GENERATOR.generate_runtime(config, "example.toml")

        self.assertIn('activation_condition="GO=false"', runtime)
        self.assertIn("condition_history=true", runtime)

    def test_machine_history_must_be_boolean(self) -> None:
        config = minimal_config()
        config["machine"]["history"] = "deep"

        with self.assertRaisesRegex(GENERATOR.ConfigError, "must be a boolean"):
            GENERATOR.generate_d2(config, "example.toml")

    def test_accepts_flat_history_layout(self) -> None:
        config = minimal_config()
        config["machine"]["history_layout"] = {"near": "center-right"}

        GENERATOR.validate_fsm(config)

    def test_rejects_obsolete_node_presentation_tables(self) -> None:
        for obsolete_name in (
            "initial_node",
            "history_node",
            "deep_history_node",
        ):
            with self.subTest(obsolete_name=obsolete_name):
                config = minimal_config()
                config["machine"][obsolete_name] = {"layout": {}}

                with self.assertRaisesRegex(
                    GENERATOR.ConfigError, f"{obsolete_name} is obsolete"
                ):
                    GENERATOR.generate_d2(config, "example.toml")

        config = minimal_config()
        config["states"]["running"]["sink_node"] = {"layout": {}}
        with self.assertRaisesRegex(GENERATOR.ConfigError, "sink_node is obsolete"):
            GENERATOR.generate_d2(config, "example.toml")

    def test_rejects_comments(self) -> None:
        config = minimal_config()
        config["states"]["idle"]["transitions"]["start"]["comments_before"] = [
            "section"
        ]

        with self.assertRaisesRegex(
            GENERATOR.ConfigError, "comments_before is obsolete"
        ):
            GENERATOR.generate_runtime(config, "example.toml")

    def test_rejects_unreachable_state(self) -> None:
        config = minimal_config()
        config["states"]["orphan"] = {"label": "Orphan"}

        with self.assertRaisesRegex(GENERATOR.ConfigError, "unreachable"):
            GENERATOR.generate_runtime(config, "example.toml")

    def test_rejects_unknown_fact_in_neutral_guard(self) -> None:
        config = minimal_config()
        config["states"]["idle"]["transitions"]["start"]["when"] = "missing == true"

        with self.assertRaisesRegex(GENERATOR.ConfigError, "undeclared fact"):
            GENERATOR.generate_runtime(config, "example.toml")


if __name__ == "__main__":
    unittest.main()
