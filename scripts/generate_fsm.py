#!/usr/bin/env python3
"""Generate pFSMRuntime configuration and views from a neutral FSM model."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict, deque
from contextlib import suppress
from dataclasses import dataclass
from pathlib import Path
from typing import Any

try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover - Python < 3.11 fallback
    try:
        import tomli as tomllib  # type: ignore[import-not-found,no-redef]
    except ModuleNotFoundError as exc:  # pragma: no cover
        raise SystemExit(
            "generate_fsm.py requires Python 3.11+ or the 'tomli' package"
        ) from exc


class ConfigError(ValueError):
    """Raised when the FSM source is internally inconsistent."""


_ID = re.compile(r"^[A-Za-z_][A-Za-z0-9_-]*$")
_DOTTED_ID = re.compile(r"^[A-Za-z_][A-Za-z0-9_-]*(?:\.[A-Za-z_][A-Za-z0-9_-]*)+$")
_RAW_D2_KEYS = {"class", "direction", "layout-engine", "shape", "near"}
_FSM_DIAGRAM_COLOR = "#12344D"
_D2_SVG_PAD = 10
_CHECK_STATUSES = frozenset({"inactive", "pending", "success", "fail", "expired"})
_SINK_INNER_CIRCLE_ICON = (
    "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' "
    "width='24' height='24'%3E%3Ccircle cx='12' cy='12' r='12' "
    "fill='%2312344D'/%3E%3C/svg%3E"
)


@dataclass(frozen=True)
class Predicate:
    lhs: str
    operator: str
    rhs: str | bool | int | float
    check_result: bool = False


@dataclass(frozen=True)
class CheckResult:
    result_fact: str
    status: str


@dataclass(frozen=True)
class BooleanExpression:
    operator: str
    operands: tuple[Condition, ...]


Condition = Predicate | CheckResult | BooleanExpression


@dataclass
class MachineIndex:
    config: dict[str, Any]
    states: dict[str, dict[str, Any]]
    transitions: dict[str, dict[str, Any]]


@dataclass
class FSMIndex:
    facts: dict[str, dict[str, Any]]
    machines: dict[str, MachineIndex]
    sink_transitions: dict[str, dict[str, dict[str, Any]]]


@dataclass(frozen=True)
class Token:
    kind: str
    value: Any
    offset: int


def _expect_table(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ConfigError(f"{context} must be a table")
    return value


def _expect_list(value: Any, context: str) -> list[Any]:
    if not isinstance(value, list):
        raise ConfigError(f"{context} must be an array")
    return value


def _expect_string(value: Any, context: str, *, nonempty: bool = True) -> str:
    if not isinstance(value, str) or (nonempty and not value):
        qualifier = " non-empty" if nonempty else ""
        raise ConfigError(f"{context} must be a{qualifier} string")
    return value


def _identifier(value: Any, context: str) -> str:
    identifier = _expect_string(value, context)
    if not _ID.fullmatch(identifier):
        raise ConfigError(f"{context} has invalid identifier: {identifier!r}")
    return identifier


def _reference(value: Any, context: str) -> tuple[str, str]:
    reference = _expect_string(value, context)
    if not _DOTTED_ID.fullmatch(reference) or reference.count(".") != 1:
        raise ConfigError(f"{context} must be a machine.item reference: {reference!r}")
    return tuple(reference.split(".", 1))  # type: ignore[return-value]


def _scalar(value: Any, context: str) -> str | bool | int | float:
    if isinstance(value, (str, bool, int, float)) and not isinstance(value, complex):
        return value
    raise ConfigError(f"{context} must be a string, boolean, or number")


def _scalar_type(value: Any) -> str:
    if isinstance(value, bool):
        return "boolean"
    if isinstance(value, (int, float)):
        return "number"
    return "string"


def _validate_state_effects(
    state: dict[str, Any], facts: dict[str, dict[str, Any]], context: str
) -> None:
    for phase in ("entry", "exit"):
        field = f"{phase}_effects"
        effects = _expect_table(state.get(field, {}), f"{context}.{field}")
        for fact_id_value, effect_value in effects.items():
            effect_context = f"{context}.{field}.{fact_id_value}"
            fact_id = _identifier(fact_id_value, effect_context)
            if fact_id not in facts:
                raise ConfigError(
                    f"{effect_context} assigns undeclared fact {fact_id}"
                )
            value = _scalar(effect_value, effect_context)
            expected_type = _scalar_type(facts[fact_id]["initial"])
            actual_type = _scalar_type(value)
            if actual_type != expected_type:
                raise ConfigError(
                    f"{effect_context} must be a {expected_type} to match fact "
                    f"{fact_id}"
                )


def _validate_layout(value: Any, context: str) -> dict[str, Any]:
    layout = _expect_table(value, context)
    for key, item_value in layout.items():
        _scalar(item_value, f"{context}.{key}")
    has_top = "top" in layout
    has_left = "left" in layout
    if has_top != has_left:
        raise ConfigError(f"{context} top and left must be configured together")
    for coordinate in ("top", "left"):
        if coordinate in layout and (
            isinstance(layout[coordinate], bool)
            or not isinstance(layout[coordinate], (int, float))
        ):
            raise ConfigError(f"{context}.{coordinate} must be a number")
    if "near" in layout:
        _expect_string(layout["near"], f"{context}.near")
        if has_top:
            raise ConfigError(f"{context} cannot combine near with top and left")
    for dimension in ("width", "height"):
        if dimension in layout and (
            isinstance(layout[dimension], bool)
            or not isinstance(layout[dimension], (int, float))
            or layout[dimension] <= 0
        ):
            raise ConfigError(f"{context}.{dimension} must be a positive number")
    return layout


def _validate_node_presentation(value: Any, context: str) -> dict[str, Any]:
    presentation = _expect_table(value, context)
    if "comments" in presentation:
        raise ConfigError(f"{context}.comments is obsolete")
    if "class" in presentation:
        raise ConfigError(f"{context}.class is obsolete; classes are inferred")
    if "graphics" in presentation:
        raise ConfigError(
            f"{context}.graphics is obsolete; put width and height in layout"
        )
    _validate_layout(presentation.get("layout", {}), f"{context}.layout")
    return presentation


def _tokenize(expression: str, context: str) -> list[Token]:
    tokens: list[Token] = []
    offset = 0
    while offset < len(expression):
        if expression[offset].isspace():
            offset += 1
            continue
        start = offset
        operator = next(
            (
                op
                for op in ("&&", "||", "==", "!=", "(", ")", ",")
                if expression.startswith(op, offset)
            ),
            None,
        )
        if operator:
            tokens.append(Token("operator", operator, start))
            offset += len(operator)
            continue
        if expression[offset] == '"':
            match = re.match(r'"(?:\\.|[^"\\])*"', expression[offset:])
            if not match:
                raise ConfigError(
                    f"{context} has unterminated string at offset {offset}"
                )
            raw = match.group(0)
            try:
                string_value = json.loads(raw)
            except json.JSONDecodeError as exc:
                raise ConfigError(
                    f"{context} has invalid string at offset {offset}"
                ) from exc
            tokens.append(Token("value", string_value, start))
            offset += len(raw)
            continue
        number = re.match(r"-?(?:\d+(?:\.\d*)?|\.\d+)", expression[offset:])
        if number:
            raw = number.group(0)
            try:
                number_value: int | float = float(raw) if "." in raw else int(raw)
            except ValueError as exc:  # Defensive; the number regex is stricter.
                raise ConfigError(
                    f"{context} has invalid number at offset {offset}"
                ) from exc
            tokens.append(Token("value", number_value, start))
            offset += len(raw)
            continue
        identifier = re.match(
            r"[A-Za-z_][A-Za-z0-9_-]*(?:\.[A-Za-z_][A-Za-z0-9_-]*)?",
            expression[offset:],
        )
        if identifier:
            raw = identifier.group(0)
            if raw == "true":
                tokens.append(Token("value", True, start))
            elif raw == "false":
                tokens.append(Token("value", False, start))
            else:
                tokens.append(Token("identifier", raw, start))
            offset += len(raw)
            continue
        raise ConfigError(
            f"{context} contains unsupported token at offset {offset}: "
            f"{expression[offset : offset + 12]!r}"
        )
    return tokens


class _ConditionParser:
    def __init__(self, expression: str, context: str):
        self.expression = expression
        self.context = context
        self.tokens = _tokenize(expression, context)
        self.offset = 0

    def parse(self) -> Condition:
        if not self.tokens:
            raise ConfigError(f"{self.context} cannot be empty")
        condition = self._parse_or()
        if self.offset != len(self.tokens):
            token = self.tokens[self.offset]
            raise ConfigError(
                f"{self.context} has unexpected {token.value!r} at offset {token.offset}"
            )
        return condition

    def _peek(self, value: str) -> bool:
        return (
            self.offset < len(self.tokens) and self.tokens[self.offset].value == value
        )

    def _take(self) -> Token:
        if self.offset >= len(self.tokens):
            raise ConfigError(f"{self.context} ends unexpectedly")
        token = self.tokens[self.offset]
        self.offset += 1
        return token

    def _parse_or(self) -> Condition:
        operands = [self._parse_and()]
        while self._peek("||"):
            self._take()
            operands.append(self._parse_and())
        if len(operands) == 1:
            return operands[0]
        return BooleanExpression("or", tuple(operands))

    def _parse_and(self) -> Condition:
        operands = [self._parse_primary()]
        while self._peek("&&"):
            self._take()
            operands.append(self._parse_primary())
        if len(operands) == 1:
            return operands[0]
        return BooleanExpression("and", tuple(operands))

    def _parse_primary(self) -> Condition:
        if self._peek("("):
            self._take()
            expression = self._parse_or()
            if not self._peek(")"):
                raise ConfigError(f"{self.context} has an unmatched '('")
            self._take()
            return expression
        if self._peek("check_result"):
            return self._parse_check_result()
        return self._parse_predicate()

    def _parse_check_result(self) -> CheckResult:
        self._take()  # Function name.
        if not self._peek("("):
            raise ConfigError(f"{self.context} requires check_result(result_fact, status)")
        self._take()
        result_fact = self._take()
        if result_fact.kind != "identifier" or not _ID.fullmatch(result_fact.value):
            raise ConfigError(f"{self.context} requires a result fact identifier")
        if not self._peek(","):
            raise ConfigError(f"{self.context} requires a comma in check_result")
        self._take()
        status = self._take()
        if status.kind != "identifier" or not _ID.fullmatch(status.value):
            raise ConfigError(f"{self.context} requires a check status identifier")
        if not self._peek(")"):
            raise ConfigError(f"{self.context} requires ')' after check_result")
        self._take()
        return CheckResult(result_fact.value, status.value)

    def _parse_predicate(self) -> Predicate:
        lhs = self._take()
        if lhs.kind != "identifier":
            raise ConfigError(
                f"{self.context} expected a fact or machine.state at offset {lhs.offset}"
            )
        operator = self._take()
        if operator.value not in ("==", "!="):
            raise ConfigError(
                f"{self.context} expected == or != at offset {operator.offset}"
            )
        rhs = self._take()
        if rhs.kind not in ("identifier", "value"):
            raise ConfigError(f"{self.context} expected a value at offset {rhs.offset}")
        return Predicate(lhs.value, operator.value, rhs.value)


def parse_condition(expression: str, context: str) -> Condition:
    return _ConditionParser(expression, context).parse()


def _resolve_check_results(
    condition: Condition, facts: dict[str, dict[str, Any]], context: str
) -> Condition:
    if isinstance(condition, BooleanExpression):
        return BooleanExpression(
            condition.operator,
            tuple(_resolve_check_results(part, facts, context)
                  for part in condition.operands),
        )
    if isinstance(condition, CheckResult):
        fact = facts.get(condition.result_fact)
        if fact is None:
            raise ConfigError(
                f"{context} references undeclared check result fact {condition.result_fact}"
            )
        if fact["initialize"] or not isinstance(fact["initial"], str):
            raise ConfigError(
                f"{context} check result fact {condition.result_fact} must be "
                "an externally owned string fact"
            )
        if condition.status not in _CHECK_STATUSES:
            raise ConfigError(f"{context} has unknown check status {condition.status}")
        return Predicate(condition.result_fact, "==", condition.status,
                         check_result=True)
    return condition


def _transition_guard(
    transition: dict[str, Any], context: str, machine_id: str, source: str,
    facts: dict[str, dict[str, Any]],
) -> Condition:
    # A state-var predicate is always true while this transition's source is
    # active, and compiles to a valid LogicCondition for pFSMRuntime.
    if "when" not in transition:
        transition["when"] = f"{machine_id}.state=={source}"
    expression = _expect_string(transition["when"], f"{context}.when")
    return _resolve_check_results(
        parse_condition(expression, f"{context}.when"),
        facts, f"{context}.when",
    )


def _validate_condition(condition: Condition, index: FSMIndex, context: str) -> None:
    if isinstance(condition, BooleanExpression):
        for operand in condition.operands:
            _validate_condition(operand, index, context)
        return
    if isinstance(condition, CheckResult):
        raise ConfigError(f"{context} has an unresolved check_result")

    if condition.lhs.endswith(".state"):
        machine_id = condition.lhs.removesuffix(".state")
        if machine_id not in index.machines:
            raise ConfigError(f"{context} references unknown machine {machine_id}")
        if not isinstance(condition.rhs, str):
            raise ConfigError(
                f"{context} compares {condition.lhs} to a non-state value"
            )
        if condition.rhs not in index.machines[machine_id].states:
            raise ConfigError(
                f"{context} references unknown state {machine_id}.{condition.rhs}"
            )
    elif condition.lhs not in index.facts:
        raise ConfigError(f"{context} references undeclared fact {condition.lhs}")


def _normalize_machines(config: dict[str, Any]) -> dict[str, dict[str, Any]]:
    if "machines" in config:
        raise ConfigError("machines is obsolete; use the singular machine table")
    root = dict(_expect_table(config.get("machine"), "machine"))
    if "states" in root:
        raise ConfigError("machine.states is obsolete; use the top-level states table")
    root["states"] = _expect_table(config.get("states"), "states")
    normalized: dict[str, dict[str, Any]] = {}

    def add_machine(
        machine_id_value: str,
        value: Any,
        context: str,
        states_context: str | None = None,
        parent_machine: str | None = None,
        parent_state: str | None = None,
    ) -> None:
        machine_id = _identifier(machine_id_value, context)
        if machine_id in normalized:
            raise ConfigError(f"duplicate inferred machine ID: {machine_id}")
        machine = dict(_expect_table(value, context))
        normalized[machine_id] = machine
        states_context = states_context or f"{context}.states"
        machine["_source_context"] = context
        machine["_states_context"] = states_context
        machine["_parent_machine"] = parent_machine
        machine["_parent_state"] = parent_state
        states_raw = _expect_table(machine.get("states"), states_context)
        normalized_states: dict[str, dict[str, Any]] = {}
        for state_id_value, state_value in states_raw.items():
            state_context = f"{states_context}.{state_id_value}"
            state_id = _identifier(state_id_value, state_context)
            state = dict(_expect_table(state_value, state_context))
            if "children" in state:
                raise ConfigError(
                    f"{state_context}.children is obsolete; use states or regions"
                )
            has_states = "states" in state
            has_regions = "regions" in state
            if has_states and has_regions:
                raise ConfigError(
                    f"{state_context} cannot contain both states and regions"
                )
            diagram = state.get("diagram", False)
            if not isinstance(diagram, bool):
                raise ConfigError(f"{state_context}.diagram must be a boolean")
            if diagram and not (has_states or has_regions):
                raise ConfigError(
                    f"{state_context}.diagram requires nested states or regions"
                )

            for obsolete_name in (
                "initial_node",
                "initial_transition",
                "history_node",
                "deep_history_node",
            ):
                if obsolete_name in state:
                    replacement = (
                        "initial_layout"
                        if obsolete_name in ("initial_node", "initial_transition")
                        else f"{obsolete_name.removesuffix('_node')}_layout"
                    )
                    raise ConfigError(
                        f"{state_context}.{obsolete_name} is obsolete; use "
                        f"{replacement}"
                    )

            children: list[dict[str, Any]] = []
            if has_states:
                child_config = {
                    key: state[key]
                    for key in (
                        "label",
                        "initial",
                        "states",
                        "initial_layout",
                        "history_layout",
                        "deep_history_layout",
                    )
                    if key in state
                }
                add_machine(
                    state_id,
                    child_config,
                    state_context,
                    parent_machine=machine_id,
                    parent_state=state_id,
                )
                children.append(
                    {
                        "machine": state_id,
                        "expanded": not diagram,
                        "inline": True,
                    }
                )
            elif has_regions:
                regions = _expect_table(state["regions"], f"{state_context}.regions")
                if not regions:
                    raise ConfigError(f"{state_context}.regions cannot be empty")
                for region_id, region in regions.items():
                    region_context = f"{state_context}.regions.{region_id}"
                    add_machine(
                        region_id,
                        region,
                        region_context,
                        parent_machine=machine_id,
                        parent_state=state_id,
                    )
                    children.append(
                        {
                            "machine": region_id,
                            "section": region_id,
                            "expanded": not diagram,
                            "inline": False,
                        }
                    )

            for nested_key in (
                "states",
                "regions",
                "initial",
                "initial_layout",
                "history_layout",
                "deep_history_layout",
                "initial_node",
                "initial_transition",
                "history_node",
                "deep_history_node",
            ):
                state.pop(nested_key, None)
            if children:
                state["children"] = children
            normalized_states[state_id] = state
        machine["states"] = normalized_states

    add_machine("operation", root, "machine", "states")
    return normalized


def validate_fsm(config: dict[str, Any]) -> FSMIndex:
    if "check_sets" in config:
        raise ConfigError(
            "check_sets is obsolete; use check_result(result_fact, status)"
        )
    if "fsm" in config:
        raise ConfigError(
            "the fsm table is obsolete; use top-level facts, machine, and states"
        )
    facts_raw = _expect_table(config.get("facts"), "facts")
    facts: dict[str, dict[str, Any]] = {}
    for fact_id_value, fact_value in facts_raw.items():
        context = f"facts.{fact_id_value}"
        fact_id = _identifier(fact_id_value, context)
        initialize = True
        if isinstance(fact_value, dict):
            unknown = set(fact_value) - {"initial", "initialize"}
            if unknown:
                raise ConfigError(
                    f"{context} has unsupported keys: {', '.join(sorted(unknown))}"
                )
            if "initial" not in fact_value:
                raise ConfigError(f"{context}.initial is required")
            initial = _scalar(fact_value["initial"], f"{context}.initial")
            initialize = fact_value.get("initialize", True)
            if not isinstance(initialize, bool):
                raise ConfigError(f"{context}.initialize must be a boolean")
        else:
            initial = _scalar(fact_value, context)
        facts[fact_id] = {
            "id": fact_id,
            "initial": initial,
            "initialize": initialize,
        }

    machines_raw = _normalize_machines(config)
    if not machines_raw:
        raise ConfigError("machine must define the root state machine")
    machines: dict[str, MachineIndex] = {}
    for machine_id_value, machine_value in machines_raw.items():
        internal_context = f"inferred machine {machine_id_value}"
        machine_config = _expect_table(machine_value, internal_context)
        context = _expect_string(
            machine_config.get("_source_context"), f"{internal_context} source context"
        )
        states_context = _expect_string(
            machine_config.get("_states_context"), f"{internal_context} states context"
        )
        machine_id = _identifier(machine_id_value, context)
        if "id" in machine_config:
            raise ConfigError(f"{context}.id is obsolete; the table key is the ID")
        if "transitions" in machine_config:
            raise ConfigError(
                f"{context}.transitions is obsolete; nest transitions under states"
            )
        _expect_string(machine_config.get("label"), f"{context}.label")
        _validate_node_presentation(machine_config, context)
        if "state_class" in machine_config:
            raise ConfigError(
                f"{context}.state_class is obsolete; classes are inferred"
            )

        states_raw = _expect_table(machine_config.get("states"), states_context)
        states: dict[str, dict[str, Any]] = {}
        transition_tables: dict[str, dict[str, Any]] = {}
        for state_id_value, state_value in states_raw.items():
            state_context = f"{states_context}.{state_id_value}"
            state_id = _identifier(state_id_value, state_context)
            state = _expect_table(state_value, state_context)
            if "id" in state:
                raise ConfigError(
                    f"{state_context}.id is obsolete; the table key is the ID"
                )
            _expect_string(state.get("label"), f"{state_context}.label")
            _validate_node_presentation(state, state_context)
            _validate_state_effects(state, facts, state_context)
            if "entry_epoch" in state and not isinstance(state["entry_epoch"], bool):
                raise ConfigError(f"{state_context}.entry_epoch must be a boolean")
            if "sink_node" in state:
                raise ConfigError(
                    f"{state_context}.sink_node is obsolete; use sink_layout"
                )
            if "sink_layout" in state:
                if not state.get("diagram", False):
                    raise ConfigError(
                        f"{state_context}.sink_layout requires diagram = true"
                    )
                _validate_layout(state["sink_layout"], f"{state_context}.sink_layout")
            sinks = _expect_table(state.get("sinks", {}), f"{state_context}.sinks")
            if sinks and not state.get("diagram", False):
                raise ConfigError(f"{state_context}.sinks requires diagram = true")
            for sink_id_value, sink_value in sinks.items():
                sink_context = f"{state_context}.sinks.{sink_id_value}"
                _identifier(sink_id_value, sink_context)
                sink = _expect_table(sink_value, sink_context)
                unknown = set(sink) - {"label", "layout"}
                if unknown:
                    raise ConfigError(
                        f"{sink_context} has unsupported keys: "
                        f"{', '.join(sorted(unknown))}"
                    )
                if "label" in sink:
                    _expect_string(sink["label"], f"{sink_context}.label")
                _validate_layout(sink.get("layout", {}), f"{sink_context}.layout")
            transition_tables[state_id] = _expect_table(
                state.get("transitions", {}), f"{state_context}.transitions"
            )
            states[state_id] = state

        initial = _identifier(machine_config.get("initial"), f"{context}.initial")
        if initial not in states:
            raise ConfigError(f"{context} initial state {initial} is not declared")
        for obsolete_name in (
            "initial_node",
            "initial_transition",
            "history_node",
            "deep_history_node",
        ):
            if obsolete_name in machine_config:
                replacement = (
                    "initial_layout"
                    if obsolete_name in ("initial_node", "initial_transition")
                    else f"{obsolete_name.removesuffix('_node')}_layout"
                )
                raise ConfigError(
                    f"{context}.{obsolete_name} is obsolete; use {replacement}"
                )
        for layout_name in (
            "initial_layout",
            "history_layout",
            "deep_history_layout",
        ):
            _validate_layout(
                machine_config.get(layout_name, {}), f"{context}.{layout_name}"
            )
        if "condition" in machine_config:
            expression = _expect_string(
                machine_config["condition"], f"{context}.condition"
            )
            machine_config["_activation_condition"] = _resolve_check_results(
                parse_condition(expression, f"{context}.condition"),
                facts, f"{context}.condition",
            )
        history = machine_config.get("history", True)
        if not isinstance(history, bool):
            raise ConfigError(f"{context}.history must be a boolean")
        machine_config["history"] = history

        transitions: dict[str, dict[str, Any]] = {}
        for source, transitions_raw in transition_tables.items():
            for transition_id_value, transition_value in transitions_raw.items():
                transition_context = (
                    f"{context}.states.{source}.transitions.{transition_id_value}"
                )
                transition_id = _identifier(transition_id_value, transition_context)
                transition = dict(_expect_table(transition_value, transition_context))
                if "id" in transition:
                    raise ConfigError(
                        f"{transition_context}.id is obsolete; the table key is the ID"
                    )
                if "from" in transition:
                    raise ConfigError(
                        f"{transition_context}.from is obsolete; the source state is inferred"
                    )
                transition["from"] = source
                transition["_model_machine"] = machine_id
                transition["_model_from"] = source
                transition["_context"] = transition_context
                target_value = _expect_string(
                    transition.get("to"), f"{transition_context}.to"
                )
                named_sink = re.fullmatch(
                    r"sink\.([A-Za-z_][A-Za-z0-9_-]*)", target_value
                )
                if target_value == "sink":
                    target = target_value
                    transition["_diagram_sink"] = True
                    if "when" in transition:
                        expression = _expect_string(
                            transition["when"], f"{transition_context}.when"
                        )
                        transition["_condition"] = _resolve_check_results(
                            parse_condition(expression, f"{transition_context}.when"),
                            facts, f"{transition_context}.when",
                        )
                elif named_sink:
                    target = target_value
                    transition["_diagram_sink"] = True
                    transition["_named_sink_id"] = named_sink.group(1)
                    transition["_condition"] = _transition_guard(
                        transition, transition_context, machine_id, source, facts
                    )
                    transition["_explicit_condition"] = transition["_condition"]
                else:
                    target = _identifier(target_value, f"{transition_context}.to")
                    if "exit" in transition:
                        transition["_exit_id"] = _identifier(
                            transition["exit"], f"{transition_context}.exit"
                        )
                        if "when" in transition:
                            raise ConfigError(
                                f"{transition_context}.when belongs on transitions "
                                "to the named sink"
                            )
                    else:
                        transition["_condition"] = _transition_guard(
                            transition, transition_context, machine_id, source, facts
                        )
                        transition["_explicit_condition"] = transition["_condition"]
                transition["to"] = target
                if "_exit_id" in transition:
                    if "label" in transition:
                        _expect_string(
                            transition["label"], f"{transition_context}.label"
                        )
                else:
                    _expect_string(
                        transition.get("label"), f"{transition_context}.label"
                    )
                if "class" in transition:
                    raise ConfigError(
                        f"{transition_context}.class is obsolete; classes are inferred"
                    )
                if "graphics" in transition:
                    raise ConfigError(
                        f"{transition_context}.graphics is obsolete; transition "
                        "appearance is inferred"
                    )
                if "sink_node" in transition:
                    raise ConfigError(
                        f"{transition_context}.sink_node is obsolete; put "
                        "sink_layout on the containing diagram state"
                    )
                if "sink_layout" in transition:
                    raise ConfigError(
                        f"{transition_context}.sink_layout belongs on the "
                        "containing diagram state"
                    )
                if "history" in transition:
                    if transition.get("_diagram_sink", False):
                        raise ConfigError(
                            f"{transition_context} cannot combine sink and history"
                        )
                    history_target = _expect_table(
                        transition["history"], f"{transition_context}.history"
                    )
                    _identifier(
                        history_target.get("machine"),
                        f"{transition_context}.history.machine",
                    )
                    history_state = _expect_string(
                        history_target.get("state"),
                        f"{transition_context}.history.state",
                    )
                    if history_state not in ("history", "deep_history"):
                        raise ConfigError(
                            f"{transition_context}.history.state must be history or "
                            "deep_history"
                        )
                if transition.get("_diagram_sink", False) and "exit" in transition:
                    raise ConfigError(
                        f"{transition_context}.exit belongs on the containing "
                        "composite transition"
                    )
                if "group" in transition:
                    raise ConfigError(f"{transition_context}.group is obsolete")
                if "comments_before" in transition:
                    raise ConfigError(
                        f"{transition_context}.comments_before is obsolete"
                    )
                if "comments" in transition:
                    raise ConfigError(f"{transition_context}.comments is obsolete")
                if transition_id in transitions:
                    raise ConfigError(
                        f"duplicate transition ID in machine {machine_id}: "
                        f"{transition_id}"
                    )
                transitions[transition_id] = transition

        machines[machine_id] = MachineIndex(machine_config, states, transitions)

    resolved_transitions: dict[str, dict[str, dict[str, Any]]] = {
        machine_id: {} for machine_id in machines
    }
    sink_transitions: dict[str, dict[str, dict[str, Any]]] = {
        machine_id: {} for machine_id in machines
    }
    for model_machine_id, model_machine in machines.items():
        for transition_id, transition in model_machine.transitions.items():
            if transition.get("_diagram_sink", False):
                sink_transitions[model_machine_id][transition_id] = transition

    provisional_index = FSMIndex(facts, machines, sink_transitions)
    exit_routes: dict[tuple[str, str, str], tuple[str, dict[str, Any]]] = {}
    for model_machine_id, model_machine in machines.items():
        for transition_id, transition in model_machine.transitions.items():
            exit_id = transition.get("_exit_id")
            if not isinstance(exit_id, str):
                continue
            route_state_id = transition["_model_from"]
            route_state = model_machine.states[route_state_id]
            if not route_state.get("diagram", False):
                raise ConfigError(
                    f"{transition['_context']}.exit requires its source state "
                    "to have diagram = true"
                )
            route_key = (model_machine_id, route_state_id, exit_id)
            if route_key in exit_routes:
                raise ConfigError(
                    f"duplicate parent exit route for "
                    f"{model_machine_id}.{transition['_model_from']}.{exit_id}"
                )
            exit_routes[route_key] = (transition_id, transition)

    # A default sink is a join point, not an independent transition. A parent
    # exit is enabled only when its own guard AND at least one active nested
    # transition to that sink (including its optional guard) are satisfied.
    legacy_sink_conditions: dict[tuple[str, str], list[Condition]] = {}
    for model_machine_id, transitions in sink_transitions.items():
        for transition in transitions.values():
            if "_named_sink_id" in transition:
                continue
            boundary = _sink_transition_boundary(provisional_index, model_machine_id)
            if boundary is None:
                continue  # The boundary check below reports the model error.
            _, owner_machine, owner_state = boundary
            source_machine = model_machine_id
            source_state = transition["_model_from"]
            predicates: list[Condition] = []
            while source_machine != owner_machine:
                predicates.append(Predicate(f"{source_machine}.state", "==", source_state))
                parent_machine = machines[source_machine].config.get("_parent_machine")
                parent_state = machines[source_machine].config.get("_parent_state")
                if not isinstance(parent_machine, str) or not isinstance(parent_state, str):
                    raise ConfigError(
                        f"{transition['_context']} cannot reach its containing diagram state"
                    )
                source_machine, source_state = parent_machine, parent_state
            if "_condition" in transition:
                predicates.append(transition["_condition"])
            condition = (
                predicates[0]
                if len(predicates) == 1
                else BooleanExpression("and", tuple(predicates))
            )
            legacy_sink_conditions.setdefault((owner_machine, owner_state), []).append(
                condition
            )

    for (owner_machine, owner_state), conditions in legacy_sink_conditions.items():
        sink_condition = (
            conditions[0]
            if len(conditions) == 1
            else BooleanExpression("or", tuple(conditions))
        )
        for transition in machines[owner_machine].transitions.values():
            if transition["_model_from"] != owner_state or "_exit_id" in transition:
                continue
            if transition.get("_diagram_sink", False):
                continue
            transition["_source_condition"] = sink_condition
            transition["_condition"] = BooleanExpression(
                "and", (transition["_condition"], sink_condition)
            )

    def add_runtime_transition(
        transition_id: str,
        transition: dict[str, Any],
        runtime_machine_id: str,
        runtime_from: str,
        target: str,
        source_predicates: list[Predicate],
    ) -> None:
        while target not in machines[runtime_machine_id].states:
            source_predicates.append(
                Predicate(f"{runtime_machine_id}.state", "==", runtime_from)
            )
            parent_machine = machines[runtime_machine_id].config.get("_parent_machine")
            parent_state = machines[runtime_machine_id].config.get("_parent_state")
            if not isinstance(parent_machine, str) or not isinstance(parent_state, str):
                raise ConfigError(
                    f"{transition['_context']} target {target!r} is not in the "
                    "source machine or any ancestor machine"
                )
            runtime_machine_id = parent_machine
            runtime_from = parent_state

        transition["_runtime_machine"] = runtime_machine_id
        transition["from"] = runtime_from
        transition["to"] = target
        if source_predicates:
            source_condition: Condition
            if len(source_predicates) == 1:
                source_condition = source_predicates[0]
            else:
                source_condition = BooleanExpression("and", tuple(source_predicates))
            transition["_source_condition"] = source_condition
            current_condition = transition["_condition"]
            effective_operands: list[Condition] = list(source_predicates)
            if (
                isinstance(current_condition, BooleanExpression)
                and current_condition.operator == "and"
            ):
                for operand in current_condition.operands:
                    if operand not in effective_operands:
                        effective_operands.append(operand)
            elif current_condition not in effective_operands:
                effective_operands.append(current_condition)
            transition["_condition"] = BooleanExpression(
                "and", tuple(effective_operands)
            )

        runtime_transitions = resolved_transitions[runtime_machine_id]
        if transition_id in runtime_transitions:
            raise ConfigError(
                f"duplicate transition ID in machine {runtime_machine_id}: "
                f"{transition_id}"
            )
        runtime_transitions[transition_id] = transition

    for model_machine_id, model_machine in machines.items():
        for transition_id, transition in model_machine.transitions.items():
            if transition.get("_diagram_sink", False) or "_exit_id" in transition:
                continue
            add_runtime_transition(
                transition_id,
                transition,
                model_machine_id,
                transition["from"],
                transition["to"],
                [],
            )

    for model_machine_id, transitions in sink_transitions.items():
        for transition_id, transition in transitions.items():
            sink_id = transition.get("_named_sink_id")
            if not isinstance(sink_id, str):
                continue
            boundary = _sink_transition_boundary(provisional_index, model_machine_id)
            if boundary is None:
                raise ConfigError(
                    f"{transition['_context']} targets named sink {sink_id!r} "
                    "outside a diagram detail boundary"
                )
            _, owner_machine, owner_state = boundary
            route_key = (owner_machine, owner_state, sink_id)
            if route_key not in exit_routes:
                raise ConfigError(
                    f"named sink {owner_machine}.{owner_state}.{sink_id} has no "
                    "matching parent transition with exit = " + repr(sink_id)
                )
            route_id, route = exit_routes[route_key]
            sink_config = _expect_table(
                machines[owner_machine].states[owner_state].get("sinks", {}),
                f"state {owner_machine}.{owner_state}.sinks",
            ).get(sink_id, {})
            sink_label = sink_config.get("label", sink_id.replace("_", " "))
            transition["_named_sink"] = True
            transition["_named_sink_label"] = sink_label
            transition["_sink_boundary"] = boundary
            transition["_exit_route_id"] = route_id
            transition["_exit_route_label"] = route.get("label", f"[{sink_label}]")
            transition["_exit_route_target"] = route["to"]
            if "history" in route:
                transition["history"] = route["history"]

            runtime_machine_id = model_machine_id
            runtime_from = transition["_model_from"]
            source_predicates: list[Predicate] = []
            while runtime_machine_id != owner_machine:
                source_predicates.append(
                    Predicate(f"{runtime_machine_id}.state", "==", runtime_from)
                )
                parent_machine = machines[runtime_machine_id].config.get(
                    "_parent_machine"
                )
                parent_state = machines[runtime_machine_id].config.get("_parent_state")
                if not isinstance(parent_machine, str) or not isinstance(
                    parent_state, str
                ):
                    raise ConfigError(
                        f"{transition['_context']} cannot reach its containing "
                        "diagram state"
                    )
                runtime_machine_id = parent_machine
                runtime_from = parent_state

            add_runtime_transition(
                transition_id,
                transition,
                runtime_machine_id,
                runtime_from,
                route["to"],
                source_predicates,
            )

    for machine_id, machine in machines.items():
        machine.transitions = resolved_transitions[machine_id]

    index = FSMIndex(facts, machines, sink_transitions)
    for machine_id, machine in machines.items():
        for state_id, state in machine.states.items():
            children = _expect_list(
                state.get("children", []),
                f"state {machine_id}.{state_id}.children",
            )
            seen_sections: set[str] = set()
            for child_index, child_value in enumerate(children):
                context = f"state {machine_id}.{state_id}.children[{child_index}]"
                child = _expect_table(child_value, context)
                child_machine = _identifier(child.get("machine"), f"{context}.machine")
                if child_machine not in machines:
                    raise ConfigError(
                        f"{context} references unknown machine {child_machine}"
                    )
                expanded = child.get("expanded", False)
                if not isinstance(expanded, bool):
                    raise ConfigError(f"{context}.expanded must be a boolean")
                inline = child.get("inline", False)
                if not isinstance(inline, bool):
                    raise ConfigError(f"{context}.inline must be a boolean")
                section = child.get("section", "")
                if section:
                    section = _identifier(section, f"{context}.section")
                if len(children) > 1 and not section:
                    raise ConfigError(
                        f"{context} requires section for parallel children"
                    )
                if section in seen_sections:
                    raise ConfigError(
                        f"state {machine_id}.{state_id} duplicates child section {section!r}"
                    )
                seen_sections.add(section)

        for transition_id, transition in machine.transitions.items():
            transition_context = f"transition {machine_id}.{transition_id}"
            _validate_condition(
                transition["_condition"],
                index,
                transition_context,
            )
            if "history" in transition:
                history_target = transition["history"]
                history_machine = history_target["machine"]
                if history_machine not in machines:
                    raise ConfigError(
                        f"{transition_context} references unknown history machine "
                        f"{history_machine}"
                    )
                target_state = machine.states[transition["to"]]
                child_ids = {
                    child["machine"] for child in target_state.get("children", [])
                }
                if history_machine not in child_ids:
                    raise ConfigError(
                        f"{transition_context} history machine {history_machine} "
                        f"is not a child of target state {transition['to']}"
                    )

        reachable = {machine.config["initial"]}
        pending = deque(reachable)
        while pending:
            source = pending.popleft()
            for transition in machine.transitions.values():
                target = transition["to"]
                if transition["from"] == source and target not in reachable:
                    reachable.add(target)
                    pending.append(target)
        unreachable = [state for state in machine.states if state not in reachable]
        if unreachable:
            raise ConfigError(
                f"machine {machine_id} states unreachable from {machine.config['initial']}: "
                + ", ".join(unreachable)
            )

    for (
        detail_machine,
        owner_machine,
        owner_state,
    ) in _diagram_boundary_specs_from_index(index):
        containing_state = index.machines[owner_machine].states[owner_state]
        direct_exits = [
            transition
            for transition in index.machines[owner_machine].transitions.values()
            if transition["_model_machine"] == owner_machine
            and transition["_model_from"] == owner_state
            and not transition.get("_named_sink", False)
        ]
        boundary_sink_exits = [
            transition
            for sink_model_machine_id, transitions in index.sink_transitions.items()
            for transition in transitions.values()
            if _sink_transition_boundary(index, sink_model_machine_id)
            == (detail_machine, owner_machine, owner_state)
        ]
        legacy_sink_exits = [
            transition
            for transition in boundary_sink_exits
            if "_named_sink_id" not in transition
        ]
        if "sink_layout" in containing_state and not legacy_sink_exits:
            raise ConfigError(
                f"diagram state {owner_machine}.{owner_state}.sink_layout is unused; "
                "at least one nested state must transition to sink"
            )
        if direct_exits and not legacy_sink_exits:
            raise ConfigError(
                f"diagram state {owner_machine}.{owner_state} exits in its parent "
                "board but no nested state transitions to sink"
            )
        if legacy_sink_exits and not direct_exits:
            raise ConfigError(
                f"diagram state {owner_machine}.{owner_state} has nested legacy "
                "sink transitions but no outgoing transition in its parent board"
            )

        declared_sink_ids = set(
            _expect_table(
                containing_state.get("sinks", {}),
                f"state {owner_machine}.{owner_state}.sinks",
            )
        )
        incoming_sinks = {
            transition["_named_sink_id"]
            for transition in boundary_sink_exits
            if "_named_sink_id" in transition
        }
        routed_sinks = {
            exit_id
            for route_machine, route_state, exit_id in exit_routes
            if route_machine == owner_machine and route_state == owner_state
        }
        for sink_id in sorted(declared_sink_ids - incoming_sinks):
            raise ConfigError(
                f"named sink {owner_machine}.{owner_state}.{sink_id} has no "
                "incoming transition"
            )
        for sink_id in sorted(routed_sinks - incoming_sinks):
            raise ConfigError(
                f"parent exit route {owner_machine}.{owner_state}.{sink_id} "
                "has no matching nested transition to that named sink"
            )

    for sink_model_machine_id, transitions in index.sink_transitions.items():
        if (
            transitions
            and _sink_transition_boundary(index, sink_model_machine_id) is None
        ):
            raise ConfigError(
                f"machine {sink_model_machine_id} transitions to sink outside a diagram "
                "detail boundary"
            )

    return index


def _case_name(value: str, mode: str, context: str) -> str:
    if mode == "preserve":
        return value
    if mode == "upper_snake":
        return value.upper().replace("-", "_")
    if mode == "lower_snake":
        return value.lower().replace("-", "_")
    raise ConfigError(f"{context} has unsupported name case: {mode}")


def _root_machine(index: FSMIndex) -> str:
    child_machines = {
        child["machine"]
        for machine in index.machines.values()
        for state in machine.states.values()
        for child in state.get("children", [])
    }
    roots = [
        machine_id for machine_id in index.machines if machine_id not in child_machines
    ]
    if len(roots) != 1:
        raise ConfigError("the FSM must have exactly one root machine")
    return roots[0]


def _diagram_boundary_specs_from_index(
    index: FSMIndex,
) -> list[tuple[str, str, str]]:
    boundaries: list[tuple[str, str, str]] = []
    for machine_id, machine in index.machines.items():
        for state_id, state in machine.states.items():
            if not state.get("diagram", False):
                continue
            inline_children = [
                child
                for child in state.get("children", [])
                if child.get("inline", False)
            ]
            if len(inline_children) != 1:
                raise ConfigError(
                    f"state {machine_id}.{state_id}.diagram requires one nested "
                    "composite state machine"
                )
            boundaries.append((inline_children[0]["machine"], machine_id, state_id))
    return boundaries


def _sink_transition_boundary(
    index: FSMIndex, model_machine: str
) -> tuple[str, str, str] | None:
    boundaries = {
        detail_machine: (detail_machine, owner_machine, owner_state)
        for detail_machine, owner_machine, owner_state in (
            _diagram_boundary_specs_from_index(index)
        )
    }
    current: str | None = model_machine
    while current is not None:
        boundary = boundaries.get(current)
        if boundary is not None:
            return boundary
        parent = index.machines[current].config.get("_parent_machine")
        current = parent if isinstance(parent, str) else None
    return None


def _bhv_value(value: str | bool | float) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return str(value)
    if re.fullmatch(r"[A-Za-z0-9_.:+-]+", value):
        return value
    return json.dumps(value)


def _effect_wire_type(value: Any) -> str:
    return "double" if _scalar_type(value) == "number" else "string"


def _effect_bindings(
    state: dict[str, Any], fact_vars: dict[str, str]
) -> dict[str, list[dict[str, Any]]]:
    result: dict[str, list[dict[str, Any]]] = {}
    for phase in ("entry", "exit"):
        effects = _expect_table(state.get(f"{phase}_effects", {}), "state effects")
        if effects:
            result[phase] = [
                {
                    "fact": fact_id,
                    "moos_variable": fact_vars[fact_id],
                    "type": _effect_wire_type(value),
                    "value": value,
                }
                for fact_id, value in effects.items()
            ]
    return result


def _validate_runtime_target(
    config: dict[str, Any], index: FSMIndex
) -> tuple[dict[str, Any], dict[str, str], dict[str, dict[str, Any]]]:
    targets = _expect_table(config.get("targets"), "targets")
    target = _expect_table(targets.get("runtime"), "targets.runtime")
    if "initialization_groups" in target:
        raise ConfigError("targets.runtime.initialization_groups is obsolete")
    for comments_field in ("comments", "state_initialization_comments"):
        if comments_field in target:
            raise ConfigError(f"targets.runtime.{comments_field} is obsolete")
    if "fact_vars" in target:
        raise ConfigError(
            "targets.runtime.fact_vars is obsolete; fact variables are inferred"
        )
    fact_vars = {
        fact_id: _case_name(fact_id, "upper_snake", "FSM fact ID")
        for fact_id in index.facts
    }
    if len(set(fact_vars.values())) != len(fact_vars):
        raise ConfigError("FSM fact IDs collide after MOOS variable name conversion")

    if "machines" in target:
        raise ConfigError(
            "targets.runtime.machines is obsolete; machine targets are inferred"
        )
    prefix = _case_name(
        _identifier(target.get("prefix"), "targets.runtime.prefix"),
        "upper_snake",
        "targets.runtime.prefix",
    )
    root_machine = _root_machine(index)
    machine_targets: dict[str, dict[str, Any]] = {}
    for machine_id in index.machines:
        machine_name = _case_name(machine_id, "upper_snake", "fsm machine ID")
        stem = f"{prefix}_FSM_{machine_name}"
        machine_target = {
            "machine": machine_id,
            "name": stem,
            "state_var": f"{stem}_STATE",
            "status_var": f"{stem}_STATUS",
            "transition_var": f"{stem}_TRANSITION",
        }
        if machine_id == root_machine:
            machine_target["active_state_var"] = f"{prefix}_FSM_STATE"
        machine_targets[machine_id] = machine_target

    history_machines = {
        transition["history"]["machine"]
        for machine in index.machines.values()
        for transition in machine.transitions.values()
        if "history" in transition
    }
    machine_targets[root_machine]["initially_active"] = True
    for machine_id, machine in index.machines.items():
        condition = machine.config.get("_activation_condition")
        if condition is not None:
            machine_targets[machine_id]["activation_condition"] = _compile_condition(
                condition, fact_vars, machine_targets, target["state_name_case"]
            )
            machine_targets[machine_id]["condition_history"] = machine.config[
                "history"
            ]
    for parent_machine_id, parent_machine in index.machines.items():
        for parent_state, state in parent_machine.states.items():
            for child in state.get("children", []):
                child_id = child["machine"]
                child_target = machine_targets[child_id]
                parent_target = machine_targets[parent_machine_id]
                parent_condition = parent_target.get("activation_condition")
                child_condition = child_target.get("activation_condition")
                if parent_condition and child_condition:
                    child_target["activation_condition"] = (
                        f"({parent_condition}) and ({child_condition})"
                    )
                    child_target["condition_history"] = bool(
                        parent_target["condition_history"]
                        and child_target["condition_history"]
                    )
                elif parent_condition:
                    child_target["activation_condition"] = parent_condition
                    child_target["condition_history"] = parent_target[
                        "condition_history"
                    ]
                child_target["parent_state_var"] = machine_targets[parent_machine_id][
                    "state_var"
                ]
                child_target["parent_state"] = parent_state
                child_target["initially_active"] = bool(
                    machine_targets[parent_machine_id].get("initially_active", False)
                    and parent_machine.config["initial"] == parent_state
                )
                child_target["reset_on_inactive"] = child_id not in history_machines

    for machine_id, machine in index.machines.items():
        for state_id, state in machine.states.items():
            if not state.get("entry_epoch", False):
                continue
            state_value = _case_name(state_id, target["state_name_case"],
                                     "targets.runtime.state_name_case")
            epoch_var = f"{machine_targets[machine_id]['state_var']}_{state_value}_EPOCH"
            for fact_id, variable in fact_vars.items():
                if variable == epoch_var and (
                    index.facts[fact_id]["initialize"] or
                    not isinstance(index.facts[fact_id]["initial"], str)
                ):
                    raise ConfigError(
                        f"{machine_id}.{state_id} entry epoch {epoch_var} "
                        "must not initialize or collide with a non-string fact"
                    )

    return target, fact_vars, machine_targets


def _compile_condition(
    condition: Condition,
    fact_vars: dict[str, str],
    machine_targets: dict[str, dict[str, Any]],
    state_case: str,
) -> str:
    if isinstance(condition, BooleanExpression):
        rendered = [
            _compile_condition(operand, fact_vars, machine_targets, state_case)
            for operand in condition.operands
        ]
        expression = rendered[0]
        for operand in rendered[1:]:
            expression = f"({expression}) {condition.operator} ({operand})"
        return expression

    if isinstance(condition, CheckResult):
        raise ConfigError("unresolved check_result in runtime condition")
    operator = "=" if condition.operator == "==" else "!="
    rhs: str | bool | float
    if condition.lhs.endswith(".state"):
        machine_id = condition.lhs.removesuffix(".state")
        lhs = machine_targets[machine_id]["state_var"]
        assert isinstance(condition.rhs, str)
        rhs = _case_name(condition.rhs, state_case, "targets.bhv.state_name_case")
    else:
        lhs = fact_vars[condition.lhs]
        rhs = condition.rhs
    return f"{lhs}{operator}{_bhv_value(rhs)}"


def _check_snapshot_pairs(
    condition: Condition, fact_vars: dict[str, str]
) -> list[tuple[str, str]]:
    if isinstance(condition, BooleanExpression):
        return sorted({pair for operand in condition.operands
                       for pair in _check_snapshot_pairs(operand, fact_vars)})
    if isinstance(condition, Predicate) and condition.check_result:
        variable = fact_vars[condition.lhs]
        return [(variable, variable + "_SNAPSHOT")]
    return []


def _condition_ast(
    condition: Condition,
    fact_vars: dict[str, str],
    machine_targets: dict[str, dict[str, Any]],
    state_case: str,
) -> dict[str, Any]:
    if isinstance(condition, BooleanExpression):
        return {
            "type": "boolean",
            "operator": condition.operator,
            "operands": [
                _condition_ast(operand, fact_vars, machine_targets, state_case)
                for operand in condition.operands
            ],
        }

    if isinstance(condition, CheckResult):
        raise ConfigError("unresolved check_result in binding condition")
    if condition.lhs.endswith(".state"):
        machine_id = condition.lhs.removesuffix(".state")
        assert isinstance(condition.rhs, str)
        right: dict[str, Any] = {
            "type": "state",
            "machine": machine_id,
            "id": condition.rhs,
            "moos_value": _case_name(
                condition.rhs, state_case, "targets.bhv.state_name_case"
            ),
        }
        left = {
            "type": "machine_state",
            "machine": machine_id,
            "moos_variable": machine_targets[machine_id]["state_var"],
        }
    elif condition.check_result:
        variable = fact_vars[condition.lhs]
        left = {
            "type": "check_result",
            "result_fact": condition.lhs,
            "moos_variable": variable,
            "snapshot_var": variable + "_SNAPSHOT",
        }
        right = {"type": "literal", "value": condition.rhs}
    else:
        left = {
            "type": "fact",
            "id": condition.lhs,
            "moos_variable": fact_vars[condition.lhs],
        }
        right = {"type": "literal", "value": condition.rhs}

    return {
        "type": "predicate",
        "left": left,
        "operator": condition.operator,
        "right": right,
    }


def generate_runtime(config: dict[str, Any], source_name: str) -> str:
    """Generate a pFSMRuntime ProcessConfig include plug."""
    index = validate_fsm(config)
    target, fact_vars, machine_targets = _validate_runtime_target(config, index)
    state_case = _expect_string(
        target.get("state_name_case", "preserve"),
        "targets.bhv.state_name_case",
    )
    section_case = _expect_string(
        target.get("section_name_case", state_case),
        "targets.bhv.section_name_case",
    )
    initial_transition_id = _case_name(
        "initial", state_case, "inferred initial transition ID"
    )
    for machine_id, machine in index.machines.items():
        encoded_states = [
            _case_name(state_id, state_case, "targets.runtime.state_name_case")
            for state_id in machine.states
        ]
        if len(set(encoded_states)) != len(encoded_states):
            raise ConfigError(f"state name conversion collides in machine {machine_id}")
        encoded_transitions = [
            _case_name(transition_id, state_case, "FSM transition ID")
            for transition_id in machine.transitions
        ]
        if len(set(encoded_transitions)) != len(encoded_transitions):
            raise ConfigError(f"transition ID conversion collides in machine {machine_id}")
        if initial_transition_id in encoded_transitions:
            raise ConfigError(
                f"machine {machine_id} transition ID {initial_transition_id} is "
                "reserved for the inferred initial transition"
            )

    prefix = _case_name(
        _identifier(target.get("prefix"), "targets.runtime.prefix"),
        "upper_snake",
        "targets.runtime.prefix",
    )
    lines = [
        "  //-------------------------------------------------",
        "  // GENERATED FILE - DO NOT EDIT.",
        f"  // Source: {source_name}",
        "  // Include inside ProcessConfig = pFSMRuntime.",
        "  //-------------------------------------------------",
        (
            f"  runtime = state_var={prefix}_FSM_RUNTIME_STATE, "
            f"heartbeat_var={prefix}_FSM_RUNTIME_HEARTBEAT, "
            f"epoch_var={prefix}_FSM_RUNTIME_EPOCH, heartbeat_interval=1"
        ),
    ]

    for fact_id, fact in index.facts.items():
        if not fact["initialize"]:
            continue
        initial = fact["initial"]
        value_type = (
            "double"
            if isinstance(initial, (int, float)) and not isinstance(initial, bool)
            else "string"
        )
        lines.append(
            f"  initialize = var={fact_vars[fact_id]}, type={value_type}, "
            f"value={_bhv_value(initial)}"
        )

    for machine_id, machine_target in machine_targets.items():
        machine = index.machines[machine_id]
        params: list[tuple[str, Any]] = [
            ("id", machine_id),
            ("state_var", machine_target["state_var"]),
            (
                "initial_state",
                _case_name(
                    machine.config["initial"],
                    state_case,
                    "targets.bhv.state_name_case",
                ),
            ),
            ("initial_node", "start"),
            ("initial_transition_id", initial_transition_id),
            ("initially_active", machine_target["initially_active"]),
            ("status_var", machine_target["status_var"]),
            ("transition_var", machine_target["transition_var"]),
        ]
        if "active_state_var" in machine_target:
            params.append(("active_state_var", machine_target["active_state_var"]))
        if "activation_condition" in machine_target:
            params.extend(
                [
                    ("activation_condition", machine_target["activation_condition"]),
                    ("condition_history", machine_target["condition_history"]),
                ]
            )
        if "parent_state_var" in machine_target:
            params.extend(
                [
                    ("parent_state_var", machine_target["parent_state_var"]),
                    (
                        "parent_state",
                        _case_name(
                            machine_target["parent_state"],
                            state_case,
                            "targets.bhv.state_name_case",
                        ),
                    ),
                    ("reset_on_inactive", machine_target["reset_on_inactive"]),
                ]
            )
        rendered = ", ".join(f"{key}={_bhv_value(value)}" for key, value in params)
        lines.extend(["", f"  machine = {rendered}"])

        for state_id in machine.states:
            state_value = _case_name(
                state_id, state_case, "targets.bhv.state_name_case"
            )
            entry_epoch = (
                f", entry_epoch_var={machine_target['state_var']}_{state_value}_EPOCH"
                if machine.states[state_id].get("entry_epoch", False) else ""
            )
            lines.append(
                f"  state = machine={machine_id}, value={state_value}{entry_epoch}"
            )

        for state_id, state in machine.states.items():
            state_value = _case_name(
                state_id, state_case, "targets.bhv.state_name_case"
            )
            for phase in ("entry", "exit"):
                effects = _expect_table(
                    state.get(f"{phase}_effects", {}),
                    f"state {machine_id}.{state_id}.{phase}_effects",
                )
                for fact_id, effect_value in effects.items():
                    lines.append(
                        f"  effect = machine={machine_id}, state={state_value}, "
                        f"phase={phase}, var={fact_vars[fact_id]}, "
                        f"type={_effect_wire_type(effect_value)}, "
                        f"value={_bhv_value(effect_value)}"
                    )

        for state_id, state in machine.states.items():
            for child in state.get("children", []):
                parts = [
                    f"machine={machine_id}",
                    "parent="
                    + _case_name(
                        state_id, state_case, "targets.bhv.state_name_case"
                    ),
                ]
                if child.get("section"):
                    parts.append(
                        "section="
                        + _case_name(
                            child["section"],
                            section_case,
                            "targets.bhv.section_name_case",
                        )
                    )
                parts.append(f"var={machine_targets[child['machine']]['state_var']}")
                lines.append("  substate = " + ", ".join(parts))

        for transition_id, transition in machine.transitions.items():
            encoded_transition_id = _case_name(
                transition_id, state_case, "FSM transition ID"
            )
            source = _case_name(
                transition["from"], state_case, "targets.bhv.state_name_case"
            )
            destination = _case_name(
                transition["to"], state_case, "targets.bhv.state_name_case"
            )
            guard = _compile_condition(
                transition["_condition"], fact_vars, machine_targets, state_case
            )
            snapshots = _check_snapshot_pairs(transition["_condition"], fact_vars)
            snapshot_field = (
                ", check_snapshots="
                + ";".join(f"{result}:{snapshot}" for result, snapshot in snapshots)
                if snapshots else ""
            )
            lines.append(
                f"  transition = machine={machine_id}, id={encoded_transition_id}, "
                f"from={source}, to={destination}, guard={json.dumps(guard)}"
                f"{snapshot_field}"
            )

    return "\n".join(lines) + "\n"


def _state_count_var(
    machine_target: dict[str, Any], state_id: str, state_case: str
) -> str:
    state_value = _case_name(state_id, state_case, "targets.bhv.state_name_case")
    return f"{machine_target['state_var']}_{state_value}_COUNT"


def _fsm_health_vars(config: dict[str, Any]) -> list[str]:
    index = validate_fsm(config)
    target, _, _ = _validate_runtime_target(config, index)
    prefix = _case_name(
        _identifier(target.get("prefix"), "targets.runtime.prefix"),
        "upper_snake",
        "targets.runtime.prefix",
    )
    return [
        f"{prefix}_FSM_RUNTIME_STATE",
        f"{prefix}_FSM_RUNTIME_HEARTBEAT",
        f"{prefix}_FSM_RUNTIME_EPOCH",
    ]


def generate_fsm_pshare(config: dict[str, Any], source_name: str) -> str:
    variables = _fsm_health_vars(config)
    width = max(len(variable) for variable in variables)
    lines = [
        f"  // GENERATED FILE - source: {source_name}",
        "  // Include inside ProcessConfig = pShare.",
    ]
    lines.extend(
        f"  output = src_name={variable + ',':<{width + 1}} route=$(SHORE_IP):$(SHORE_PSHARE)"
        for variable in variables
    )
    prefix = variables[0].removesuffix("_FSM_RUNTIME_STATE")
    lines.append(
        f"  output = src_name=FSM_TRANSITION, dest_name={prefix}_FSM_TRANSITION, "
        "route=$(SHORE_IP):$(SHORE_PSHARE)"
    )
    return "\n".join(lines) + "\n"


def generate_fsm_scopes(config: dict[str, Any], source_name: str) -> str:
    lines = [
        f"  // GENERATED FILE - source: {source_name}",
        "  // Include inside the shoreside viewer ProcessConfig section.",
    ]
    variables = _fsm_health_vars(config)
    lines.extend(f"  scope = {variable}" for variable in variables)
    prefix = variables[0].removesuffix("_FSM_RUNTIME_STATE")
    lines.append(f"  scope = {prefix}_FSM_TRANSITION")
    return "\n".join(lines) + "\n"


def _d2_scalar(key: str, value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        return str(value)
    if isinstance(value, str):
        if key in _RAW_D2_KEYS and re.fullmatch(r"[A-Za-z0-9_-]+", value):
            return value
        return json.dumps(value)
    raise ConfigError(f"unsupported D2 scalar for {key}: {value!r}")


def _emit_d2_mapping(lines: list[str], key: str, value: Any, indent: int) -> None:
    prefix = " " * indent
    if isinstance(value, dict):
        lines.append(f"{prefix}{key}: {{")
        for child_key, child_value in value.items():
            _emit_d2_mapping(lines, child_key, child_value, indent + 2)
        lines.append(f"{prefix}}}")
    else:
        lines.append(f"{prefix}{key}: {_d2_scalar(key, value)}")


def generate_d2(
    config: dict[str, Any],
    source_name: str,
    *,
    root_machine_override: str | None = None,
    board_links: dict[str, str] | None = None,
    embedded: bool = False,
) -> str:
    index = validate_fsm(config)
    bhv_target, _, machine_targets = _validate_runtime_target(config, index)
    state_case = _expect_string(
        bhv_target.get("state_name_case", "preserve"),
        "targets.bhv.state_name_case",
    )
    initial_transition_id = _case_name(
        "initial", state_case, "inferred initial transition ID"
    )
    if "views" in config:
        raise ConfigError("views is obsolete; use targets.d2")
    targets = _expect_table(config.get("targets"), "targets")
    diagram = _expect_table(targets.get("d2"), "targets.d2")
    unsupported = set(diagram).difference({"layout"})
    if unsupported:
        fields = ", ".join(sorted(unsupported))
        raise ConfigError(f"unsupported targets.d2 fields: {fields}")
    layout = _expect_table(diagram.get("layout"), "targets.d2.layout")
    unsupported_layout = set(layout).difference({"direction"})
    if unsupported_layout:
        fields = ", ".join(sorted(unsupported_layout))
        raise ConfigError(f"unsupported targets.d2.layout fields: {fields}")
    direction = _expect_string(layout.get("direction"), "targets.d2.layout.direction")
    by_path: dict[str, dict[str, Any]] = {}
    children: dict[str, list[dict[str, Any]]] = defaultdict(list)
    state_paths: dict[str, str] = {}
    initial_paths: dict[str, str] = {}
    machine_paths: dict[str, str] = {}
    rendered_machines: list[str] = []

    def add_node(
        node_id: str,
        parent: str,
        label: str,
        node_class: str | list[str],
        presentation: dict[str, Any],
        context: str,
    ) -> str:
        node_id = _identifier(node_id, f"{context}.id")
        path = f"{parent}.{node_id}" if parent else node_id
        if path in by_path:
            raise ConfigError(f"duplicate inferred D2 node path: {path}")
        structural_class = node_class if isinstance(node_class, str) else node_class[0]
        inferred_attrs: dict[str, Any]
        if structural_class == "initial":
            inferred_attrs = {
                "shape": "circle",
                "width": 34,
                "height": 34,
                "style": {
                    "fill": _FSM_DIAGRAM_COLOR,
                    "stroke": _FSM_DIAGRAM_COLOR,
                    "stroke-width": 1,
                },
            }
        elif structural_class == "history":
            inferred_attrs = {
                "shape": "circle",
                "width": 34,
                "height": 34,
                "style": {
                    "stroke": _FSM_DIAGRAM_COLOR,
                    "stroke-width": 2,
                },
            }
        elif structural_class == "sink":
            inferred_attrs = {
                "shape": "oval",
                "width": 34,
                "height": 34,
                "icon": _SINK_INNER_CIRCLE_ICON,
                "style": {
                    "fill": "transparent",
                    "stroke": _FSM_DIAGRAM_COLOR,
                    "stroke-width": 2,
                },
            }
        else:
            rectangle_style: dict[str, Any] = {
                "stroke": _FSM_DIAGRAM_COLOR,
                "stroke-width": 2,
                "border-radius": 6 if structural_class == "region" else 8,
            }
            if structural_class == "region":
                rectangle_style["stroke-dash"] = 5
            inferred_attrs = {
                "shape": "rectangle",
                "style": rectangle_style,
            }
        configured_layout = _validate_layout(
            presentation.get("layout", {}), f"{context}.layout"
        )
        for dimension in ("width", "height"):
            if dimension in configured_layout:
                inferred_attrs[dimension] = configured_layout[dimension]
        node = {
            "_id": node_id,
            "_path": path,
            "_label": label,
            "class": node_class,
            "attrs": inferred_attrs,
            "layout": {
                key: value
                for key, value in configured_layout.items()
                if key not in ("width", "height")
            },
        }
        by_path[path] = node
        children[parent].append(node)
        return path

    child_machines = {
        child["machine"]
        for machine in index.machines.values()
        for state in machine.states.values()
        for child in state.get("children", [])
    }
    root_candidates = [
        machine_id for machine_id in index.machines if machine_id not in child_machines
    ]
    if root_machine_override is not None:
        root_machine = _identifier(root_machine_override, "D2 root machine")
        if root_machine not in index.machines:
            raise ConfigError(f"unknown D2 root machine: {root_machine}")
    elif len(root_candidates) == 1:
        root_machine = root_candidates[0]
    else:
        raise ConfigError("the FSM must have exactly one root machine")

    def render_machine(
        machine_id: str,
        parent: str,
        wrap: bool,
        ancestors: tuple[str, ...],
    ) -> None:
        if machine_id in ancestors:
            raise ConfigError(f"cyclic rendered machine hierarchy at {machine_id}")
        if machine_id in rendered_machines:
            raise ConfigError(
                f"machine {machine_id} is expanded more than once in the D2 view"
            )
        rendered_machines.append(machine_id)
        machine = index.machines[machine_id]
        machine_parent = parent
        if wrap:
            machine_parent = add_node(
                machine_id,
                parent,
                machine.config["label"],
                "region",
                machine.config,
                f"machine {machine_id}",
            )
        machine_paths[machine_id] = machine_parent

        initial_paths[machine_id] = add_node(
            "start",
            machine_parent,
            "",
            "initial",
            {"layout": machine.config.get("initial_layout", {})},
            f"machine {machine_id}.initial_layout",
        )

        for state_id, state in machine.states.items():
            state_class = "composite" if state.get("children") else "state"
            state_value = _case_name(
                state_id, state_case, "targets.bhv.state_name_case"
            )
            state_classes = [
                state_class,
                "moos_state",
                f"moos_var_{machine_targets[machine_id]['state_var']}",
                f"moos_value_{state_value}",
            ]
            state_path = add_node(
                state_id,
                machine_parent,
                state["label"],
                state_classes,
                state,
                f"state {machine_id}.{state_id}",
            )
            state_ref = f"{machine_id}.{state_id}"
            state_paths[state_ref] = state_path
            if board_links and state_ref in board_links:
                by_path[state_path]["link"] = board_links[state_ref]
            for child in state.get("children", []):
                if child.get("expanded", False):
                    render_machine(
                        child["machine"],
                        state_path,
                        not child.get("inline", False),
                        (*ancestors, machine_id),
                    )

    render_machine(root_machine, "", False, ())

    sink_paths: dict[str, str] = {}
    detail_boundary = next(
        (
            boundary
            for boundary in _diagram_boundary_specs_from_index(index)
            if boundary[0] == root_machine
        ),
        None,
    )
    if detail_boundary is not None:
        by_path[initial_paths[root_machine]]["link"] = "_"
        sink_transitions = [
            transition
            for model_machine, transitions in index.sink_transitions.items()
            for transition in transitions.values()
            if _sink_transition_boundary(index, model_machine) == detail_boundary
        ]
        if sink_transitions:
            _, owner_machine, owner_state = detail_boundary
            containing_state = index.machines[owner_machine].states[owner_state]
            if any("_named_sink_id" not in item for item in sink_transitions):
                legacy_sink_path = add_node(
                    "sink",
                    "",
                    "",
                    "sink",
                    {"layout": containing_state.get("sink_layout", {})},
                    f"state {owner_machine}.{owner_state}.sink_layout",
                )
                by_path[legacy_sink_path]["link"] = "_"
                sink_paths["sink"] = legacy_sink_path
            named_sink_configs = _expect_table(
                containing_state.get("sinks", {}),
                f"state {owner_machine}.{owner_state}.sinks",
            )
            named_sink_ids = list(
                dict.fromkeys(
                    transition["_named_sink_id"]
                    for transition in sink_transitions
                    if "_named_sink_id" in transition
                )
            )
            for sink_id in named_sink_ids:
                sink_config = named_sink_configs.get(sink_id, {})
                sink_label = sink_config.get("label", sink_id.replace("_", " "))
                sink_path = add_node(
                    f"sink_{sink_id}",
                    "",
                    sink_label,
                    ["sink", f"sink_{sink_id}"],
                    sink_config,
                    f"state {owner_machine}.{owner_state}.sinks.{sink_id}",
                )
                by_path[sink_path]["link"] = "_"
                sink_paths[sink_id] = sink_path

    history_paths: dict[tuple[str, str], str] = {}
    for machine_id in rendered_machines:
        machine = index.machines[machine_id]
        for transition_id, transition in machine.transitions.items():
            if "history" not in transition:
                continue
            history_target = transition["history"]
            history_machine = history_target["machine"]
            history_state = history_target["state"]
            if history_machine not in machine_paths:
                raise ConfigError(
                    f"transition {machine_id}.{transition_id} targets history in "
                    f"unrendered machine {history_machine}"
                )
            history_key = (history_machine, history_state)
            if history_key in history_paths:
                continue
            layout_name = f"{history_state}_layout"
            default_label = "H*" if history_state == "deep_history" else "H"
            history_paths[history_key] = add_node(
                history_state,
                machine_paths[history_machine],
                default_label,
                "history",
                {"layout": index.machines[history_machine].config.get(layout_name, {})},
                f"machine {history_machine}.{layout_name}",
            )

    def edge_attrs() -> dict[str, Any]:
        return {"style": {"stroke": _FSM_DIAGRAM_COLOR}}

    edges: list[dict[str, Any]] = []
    for machine_id in rendered_machines:
        machine = index.machines[machine_id]
        initial_ref = f"{machine_id}.{machine.config['initial']}"
        edges.append(
            {
                "_source": initial_paths[machine_id],
                "_target": state_paths[initial_ref],
                "_label": None,
                "class": [
                    "transition",
                    "moos_transition",
                    f"moos_var_{machine_targets[machine_id]['transition_var']}",
                    f"moos_id_{initial_transition_id}",
                ],
                "attrs": edge_attrs(),
            }
        )

    named_parent_edges: dict[tuple[str, str, str], dict[str, Any]] = {}
    for runtime_machine_id, machine in index.machines.items():
        if runtime_machine_id not in rendered_machines:
            continue
        for transition_id, transition in machine.transitions.items():
            if transition.get("_named_sink", False):
                _, sink_owner_machine, sink_owner_state = transition["_sink_boundary"]
                source_ref = f"{sink_owner_machine}.{sink_owner_state}"
            else:
                source_ref = (
                    f"{transition['_model_machine']}.{transition['_model_from']}"
                )
            source_path = state_paths.get(source_ref)
            if source_path is None:
                raise ConfigError(
                    f"transition {runtime_machine_id}.{transition_id} has an "
                    f"unrendered D2 source {source_ref}"
                )

            target_ref = f"{runtime_machine_id}.{transition['to']}"
            if target_ref not in state_paths:
                raise ConfigError(
                    f"transition {runtime_machine_id}.{transition_id} has an "
                    f"unrendered D2 target {target_ref}"
                )
            target_path = state_paths[target_ref]
            if "history" in transition:
                history_target = transition["history"]
                history_key = (
                    history_target["machine"],
                    history_target["state"],
                )
                target_path = history_paths[history_key]
            transition_value = _case_name(
                transition_id, state_case, "FSM transition ID"
            )
            edge = {
                "_source": source_path,
                "_target": target_path,
                "_label": transition.get("_exit_route_label", transition.get("label")),
                "class": [
                    "transition",
                    "moos_transition",
                    f"moos_var_{machine_targets[runtime_machine_id]['transition_var']}",
                    f"moos_id_{transition_value}",
                ],
                "attrs": edge_attrs(),
            }
            if transition.get("_named_sink", False):
                named_edge_key = (
                    source_path,
                    target_path,
                    transition["_named_sink_id"],
                )
                existing_edge = named_parent_edges.get(named_edge_key)
                if existing_edge is not None:
                    for edge_class in edge["class"]:
                        if edge_class not in existing_edge["class"]:
                            existing_edge["class"].append(edge_class)
                    continue
                named_parent_edges[named_edge_key] = edge
            edges.append(edge)

    if detail_boundary is not None and sink_paths:
        for model_machine_id in rendered_machines:
            for transition_id, transition in index.sink_transitions[
                model_machine_id
            ].items():
                if (
                    _sink_transition_boundary(index, model_machine_id)
                    != detail_boundary
                ):
                    continue
                source_ref = f"{model_machine_id}.{transition['_model_from']}"
                source_path = state_paths.get(source_ref)
                if source_path is None:
                    raise ConfigError(
                        f"sink transition {model_machine_id}.{transition_id} has "
                        f"an unrendered D2 source {source_ref}"
                    )
                sink_id = transition.get("_named_sink_id", "sink")
                target_sink_path = sink_paths.get(sink_id)
                if target_sink_path is None:
                    raise ConfigError(
                        f"sink transition {model_machine_id}.{transition_id} "
                        f"references unrendered sink {sink_id!r}"
                    )
                edge_classes = ["transition", "sink_transition"]
                if transition.get("_named_sink", False):
                    runtime_machine_id = transition["_runtime_machine"]
                    transition_value = _case_name(
                        transition_id, state_case, "FSM transition ID"
                    )
                    edge_classes.extend(
                        [
                            "moos_transition",
                            f"moos_var_{machine_targets[runtime_machine_id]['transition_var']}",
                            f"moos_id_{transition_value}",
                        ]
                    )
                edges.append(
                    {
                        "_source": source_path,
                        "_target": target_sink_path,
                        "_label": transition.get("label"),
                        "class": edge_classes,
                        "attrs": edge_attrs(),
                    }
                )

    lines: list[str] = []
    if not embedded:
        lines.extend(
            [
                f"# GENERATED FILE - source: {source_name}",
                "# Regenerate with scripts/generate_fsm.py.",
                "",
                "vars: {",
                "  d2-config: {",
                "    layout-engine: tala",
                "  }",
                "}",
            ]
        )
    lines.extend(["", f"direction: {_d2_scalar('direction', direction)}"])
    lines.extend(
        [
            "",
            "# Inferred class attributes are unstyled SVG/CSS hooks.",
            "# Runtime states: moos_var_<VAR> + moos_value_<VALUE>.",
            "# Runtime transitions: moos_var_<VAR> + moos_id_<REPORT_ID>.",
        ]
    )

    def class_value(value: str | list[str]) -> str:
        if isinstance(value, str):
            return value
        return "[" + "; ".join(value) + "]"

    def emit_node(node: dict[str, Any], indent: int) -> None:
        prefix = " " * indent
        node_id = node["_id"]
        body_needed = bool(
            node.get("class")
            or node.get("attrs")
            or node.get("layout")
            or node.get("link")
            or children.get(node["_path"])
        )
        declaration = (
            f"{prefix}{node_id}: {json.dumps(node['_label'], ensure_ascii=False)}"
        )
        if not body_needed:
            lines.append(declaration)
            return
        lines.append(declaration + " {")
        if node.get("class"):
            lines.append(f"{prefix}  class: {class_value(node['class'])}")
        if node.get("link"):
            lines.append(f"{prefix}  link: {node['link']}")
        for key, value in node.get("attrs", {}).items():
            _emit_d2_mapping(lines, key, value, indent + 2)
        for key, value in node.get("layout", {}).items():
            _emit_d2_mapping(lines, key, value, indent + 2)
        child_nodes = children.get(node["_path"], [])
        if child_nodes:
            lines.append("")
            for child_index, child in enumerate(child_nodes):
                if child_index:
                    lines.append("")
                emit_node(child, indent + 2)
        lines.append(f"{prefix}}}")

    for node in children.get("", []):
        lines.append("")
        emit_node(node, 0)

    if edges:
        lines.append("")
    for edge in edges:
        declaration = f"{edge['_source']} -> {edge['_target']}"
        if edge.get("_label"):
            declaration += f": {json.dumps(edge['_label'])}"
        body_needed = bool(edge.get("class") or edge.get("attrs"))
        if not body_needed:
            lines.append(declaration)
            continue
        lines.append(declaration + " {")
        if edge.get("class"):
            lines.append(f"  class: {class_value(edge['class'])}")
        for key, value in edge.get("attrs", {}).items():
            _emit_d2_mapping(lines, key, value, 2)
        lines.append("}")

    return "\n".join(lines) + "\n"


def generate_d2_bindings(
    config: dict[str, Any],
    source_name: str,
    diagrams: list[tuple[str, str | None]],
) -> str:
    index = validate_fsm(config)
    bhv_target, fact_vars, machine_targets = _validate_runtime_target(config, index)
    state_case = _expect_string(
        bhv_target.get("state_name_case", "preserve"),
        "targets.bhv.state_name_case",
    )
    initial_transition_id = _case_name(
        "initial", state_case, "inferred initial transition ID"
    )

    result: dict[str, Any] = {
        "schema_version": 1,
        "source": source_name,
        "diagram_order": [diagram_name for diagram_name, _ in diagrams],
        "diagrams": {},
    }
    for diagram_name, root_override in diagrams:
        root_machine = root_override or _root_machine(index)
        if root_machine not in index.machines:
            raise ConfigError(f"unknown D2 binding root machine: {root_machine}")

        state_paths: dict[str, str] = {}
        machine_paths: dict[str, str] = {}
        rendered_machines: list[str] = []
        states: dict[str, dict[str, Any]] = {}

        def render_machine(
            machine_id: str,
            parent: str,
            wrap: bool,
            ancestors: tuple[str, ...],
            state_paths_for_diagram: dict[str, str] = state_paths,
            machine_paths_for_diagram: dict[str, str] = machine_paths,
            rendered_for_diagram: list[str] = rendered_machines,
            states_for_diagram: dict[str, dict[str, Any]] = states,
        ) -> None:
            if machine_id in ancestors:
                raise ConfigError(f"cyclic rendered machine hierarchy at {machine_id}")
            if machine_id in rendered_for_diagram:
                raise ConfigError(
                    f"machine {machine_id} is expanded more than once in D2 bindings"
                )
            rendered_for_diagram.append(machine_id)
            machine_parent = (
                f"{parent}.{machine_id}"
                if wrap and parent
                else machine_id
                if wrap
                else parent
            )
            machine_paths_for_diagram[machine_id] = machine_parent
            machine = index.machines[machine_id]
            state_var = machine_targets[machine_id]["state_var"]
            variable_states = states_for_diagram.setdefault(state_var, {})
            for state_id, state in machine.states.items():
                state_path = (
                    f"{machine_parent}.{state_id}" if machine_parent else state_id
                )
                state_paths_for_diagram[f"{machine_id}.{state_id}"] = state_path
                state_value = _case_name(
                    state_id, state_case, "targets.bhv.state_name_case"
                )
                if state_value in variable_states:
                    raise ConfigError(
                        f"duplicate D2 state binding for {state_var}={state_value}"
                    )
                state_binding: dict[str, Any] = {
                    "d2_id": state_path,
                    "label": state["label"].replace("\r", " ").replace("\n", " "),
                    "count_variable": _state_count_var(
                        machine_targets[machine_id], state_id, state_case
                    ),
                    "selector": (f".moos_var_{state_var}.moos_value_{state_value}"),
                }
                effects = _effect_bindings(state, fact_vars)
                if effects:
                    state_binding["effects"] = effects
                variable_states[state_value] = state_binding
                for child in state.get("children", []):
                    if child.get("expanded", False):
                        render_machine(
                            child["machine"],
                            state_path,
                            not child.get("inline", False),
                            (*ancestors, machine_id),
                        )

        render_machine(root_machine, "", False, ())

        transitions: dict[str, dict[str, Any]] = {}
        connection_counts: dict[tuple[str, str], int] = defaultdict(int)
        named_parent_connection_indices: dict[tuple[str, str, str], int] = {}
        for machine_id in rendered_machines:
            machine = index.machines[machine_id]
            transition_var = machine_targets[machine_id]["transition_var"]
            variable_transitions = transitions.setdefault(transition_var, {})
            initial_node_id = "start"
            machine_parent = machine_paths[machine_id]
            initial_path = (
                f"{machine_parent}.{initial_node_id}"
                if machine_parent
                else initial_node_id
            )
            initial_state = machine.config["initial"]
            target_path = state_paths[f"{machine_id}.{initial_state}"]
            initial_value = _case_name(
                initial_state, state_case, "targets.bhv.state_name_case"
            )
            connection_key = (initial_path, target_path)
            connection_index = connection_counts[connection_key]
            connection_counts[connection_key] += 1
            variable_transitions[initial_transition_id] = {
                "machine": machine_id,
                "label": "",
                "from": {
                    "type": "initial",
                    "d2_id": initial_path,
                },
                "to": {
                    "machine": machine_id,
                    "state_id": initial_state,
                    "moos_variable": machine_targets[machine_id]["state_var"],
                    "moos_value": initial_value,
                    "d2_id": target_path,
                },
                "runtime": {
                    "machine": machine_id,
                    "transition_variable": transition_var,
                    "from": initial_node_id,
                    "to": initial_value,
                },
                "guard": {
                    "expression": "true",
                    "ast": {"type": "literal", "value": True},
                    "moos_expression": "true",
                    "effective_ast": {"type": "literal", "value": True},
                },
                "d2_id": (f"({initial_path} -> {target_path})[{connection_index}]"),
                "d2_source": initial_path,
                "d2_target": target_path,
                "selector": (
                    f".moos_var_{transition_var}.moos_id_{initial_transition_id}"
                ),
            }
        for runtime_machine_id, machine in index.machines.items():
            if runtime_machine_id not in rendered_machines:
                continue
            transition_var = machine_targets[runtime_machine_id]["transition_var"]
            for transition_id, transition in machine.transitions.items():
                variable_transitions = transitions.setdefault(transition_var, {})
                transition_value = _case_name(
                    transition_id, state_case, "FSM transition ID"
                )
                if transition_value in variable_transitions:
                    raise ConfigError(
                        "duplicate D2 transition binding for "
                        f"{transition_var}={transition_value}"
                    )
                model_machine_id = transition["_model_machine"]
                model_from = transition["_model_from"]
                if transition.get("_named_sink", False):
                    _, sink_owner_machine, sink_owner_state = transition[
                        "_sink_boundary"
                    ]
                    source_path = state_paths.get(
                        f"{sink_owner_machine}.{sink_owner_state}"
                    )
                else:
                    source_path = state_paths.get(f"{model_machine_id}.{model_from}")
                if source_path is None:
                    raise ConfigError(
                        f"transition {runtime_machine_id}.{transition_id} has an "
                        "unrendered D2 binding source"
                    )

                target_ref = f"{runtime_machine_id}.{transition['to']}"
                if target_ref not in state_paths:
                    raise ConfigError(
                        f"transition {runtime_machine_id}.{transition_id} has an "
                        "unrendered D2 binding target"
                    )
                target_path = state_paths[target_ref]
                if "history" in transition:
                    history_target = transition["history"]
                    history_machine = history_target["machine"]
                    if history_machine not in machine_paths:
                        raise ConfigError(
                            f"transition {runtime_machine_id}.{transition_id} "
                            f"targets history in unrendered machine {history_machine}"
                        )
                    history_parent = machine_paths[history_machine]
                    history_state = history_target["state"]
                    target_path = (
                        f"{history_parent}.{history_state}"
                        if history_parent
                        else history_state
                    )
                connection_key = (source_path, target_path)
                if transition.get("_named_sink", False):
                    named_connection_key = (
                        source_path,
                        target_path,
                        transition["_named_sink_id"],
                    )
                    existing_connection_index = named_parent_connection_indices.get(
                        named_connection_key
                    )
                    if existing_connection_index is None:
                        connection_index = connection_counts[connection_key]
                        connection_counts[connection_key] += 1
                        named_parent_connection_indices[named_connection_key] = (
                            connection_index
                        )
                    else:
                        connection_index = existing_connection_index
                else:
                    connection_index = connection_counts[connection_key]
                    connection_counts[connection_key] += 1
                from_value = _case_name(
                    model_from, state_case, "targets.bhv.state_name_case"
                )
                runtime_from_value = _case_name(
                    transition["from"], state_case, "targets.bhv.state_name_case"
                )
                to_value = _case_name(
                    transition["to"], state_case, "targets.bhv.state_name_case"
                )
                explicit_condition = transition["_explicit_condition"]
                effective_condition = transition["_condition"]
                guard_binding: dict[str, Any] = {
                    "expression": transition["when"],
                    "ast": _condition_ast(
                        explicit_condition,
                        fact_vars,
                        machine_targets,
                        state_case,
                    ),
                    "moos_expression": _compile_condition(
                        effective_condition,
                        fact_vars,
                        machine_targets,
                        state_case,
                    ),
                    "effective_ast": _condition_ast(
                        effective_condition,
                        fact_vars,
                        machine_targets,
                        state_case,
                    ),
                }
                if "_source_condition" in transition:
                    source_condition = transition["_source_condition"]
                    guard_binding["source_constraint"] = {
                        "moos_expression": _compile_condition(
                            source_condition,
                            fact_vars,
                            machine_targets,
                            state_case,
                        ),
                        "ast": _condition_ast(
                            source_condition,
                            fact_vars,
                            machine_targets,
                            state_case,
                        ),
                    }
                transition_binding: dict[str, Any] = {
                    "machine": runtime_machine_id,
                    "label": transition.get("_exit_route_label", transition["label"]),
                    "from": {
                        "machine": model_machine_id,
                        "state_id": model_from,
                        "moos_variable": machine_targets[model_machine_id]["state_var"],
                        "moos_value": from_value,
                        "d2_id": source_path,
                    },
                    "to": {
                        "machine": runtime_machine_id,
                        "state_id": transition["to"],
                        "moos_variable": machine_targets[runtime_machine_id][
                            "state_var"
                        ],
                        "moos_value": to_value,
                        "d2_id": target_path,
                    },
                    "runtime": {
                        "machine": runtime_machine_id,
                        "transition_variable": transition_var,
                        "from": runtime_from_value,
                        "to": to_value,
                    },
                    "guard": guard_binding,
                    "d2_id": (f"({source_path} -> {target_path})[{connection_index}]"),
                    "d2_source": source_path,
                    "d2_target": target_path,
                    "selector": (
                        f".moos_var_{transition_var}.moos_id_{transition_value}"
                    ),
                }
                if transition.get("_named_sink", False):
                    transition_binding["exit"] = {
                        "id": transition["_named_sink_id"],
                        "label": transition["_named_sink_label"],
                        "route_id": transition["_exit_route_id"],
                        "detail_source": {
                            "machine": model_machine_id,
                            "state_id": model_from,
                        },
                    }
                if "history" in transition:
                    transition_binding["history"] = dict(transition["history"])
                variable_transitions[transition_value] = transition_binding

        detail_boundary = next(
            (
                boundary
                for boundary in _diagram_boundary_specs_from_index(index)
                if boundary[0] == root_machine
            ),
            None,
        )
        if detail_boundary is not None:
            for model_machine_id in rendered_machines:
                for transition_id, transition in index.sink_transitions[
                    model_machine_id
                ].items():
                    if not transition.get("_named_sink", False):
                        continue
                    if (
                        _sink_transition_boundary(index, model_machine_id)
                        != detail_boundary
                    ):
                        continue
                    runtime_machine_id = transition["_runtime_machine"]
                    transition_var = machine_targets[runtime_machine_id][
                        "transition_var"
                    ]
                    variable_transitions = transitions.setdefault(transition_var, {})
                    transition_value = _case_name(
                        transition_id, state_case, "FSM transition ID"
                    )
                    if transition_value in variable_transitions:
                        raise ConfigError(
                            "duplicate D2 transition binding for "
                            f"{transition_var}={transition_value}"
                        )
                    model_from = transition["_model_from"]
                    source_path = state_paths[f"{model_machine_id}.{model_from}"]
                    sink_id = transition["_named_sink_id"]
                    target_path = f"sink_{sink_id}"
                    connection_key = (source_path, target_path)
                    connection_index = connection_counts[connection_key]
                    connection_counts[connection_key] += 1
                    explicit_condition = transition["_explicit_condition"]
                    effective_condition = transition["_condition"]
                    detail_guard_binding: dict[str, Any] = {
                        "expression": transition["when"],
                        "ast": _condition_ast(
                            explicit_condition,
                            fact_vars,
                            machine_targets,
                            state_case,
                        ),
                        "moos_expression": _compile_condition(
                            effective_condition,
                            fact_vars,
                            machine_targets,
                            state_case,
                        ),
                        "effective_ast": _condition_ast(
                            effective_condition,
                            fact_vars,
                            machine_targets,
                            state_case,
                        ),
                    }
                    if "_source_condition" in transition:
                        source_condition = transition["_source_condition"]
                        detail_guard_binding["source_constraint"] = {
                            "moos_expression": _compile_condition(
                                source_condition,
                                fact_vars,
                                machine_targets,
                                state_case,
                            ),
                            "ast": _condition_ast(
                                source_condition,
                                fact_vars,
                                machine_targets,
                                state_case,
                            ),
                        }
                    from_value = _case_name(
                        model_from, state_case, "targets.bhv.state_name_case"
                    )
                    runtime_from_value = _case_name(
                        transition["from"],
                        state_case,
                        "targets.bhv.state_name_case",
                    )
                    runtime_to_value = _case_name(
                        transition["to"], state_case, "targets.bhv.state_name_case"
                    )
                    variable_transitions[transition_value] = {
                        "machine": runtime_machine_id,
                        "label": transition["label"],
                        "from": {
                            "machine": model_machine_id,
                            "state_id": model_from,
                            "moos_variable": machine_targets[model_machine_id][
                                "state_var"
                            ],
                            "moos_value": from_value,
                            "d2_id": source_path,
                        },
                        "to": {
                            "type": "sink",
                            "id": sink_id,
                            "label": transition["_named_sink_label"],
                            "d2_id": target_path,
                        },
                        "runtime": {
                            "machine": runtime_machine_id,
                            "transition_variable": transition_var,
                            "from": runtime_from_value,
                            "to": runtime_to_value,
                        },
                        "exit": {
                            "id": sink_id,
                            "label": transition["_named_sink_label"],
                            "route_id": transition["_exit_route_id"],
                            "target_state_id": transition["to"],
                        },
                        "guard": detail_guard_binding,
                        "d2_id": (
                            f"({source_path} -> {target_path})[{connection_index}]"
                        ),
                        "d2_source": source_path,
                        "d2_target": target_path,
                        "selector": (
                            f".moos_var_{transition_var}.moos_id_{transition_value}"
                        ),
                    }

        result["diagrams"][diagram_name] = {
            "states": states,
            "transitions": transitions,
        }

    return json.dumps(result, indent=2) + "\n"


def _diagram_boundary_specs(
    config: dict[str, Any],
) -> list[tuple[str, str, str]]:
    return _diagram_boundary_specs_from_index(validate_fsm(config))


def _diagram_boundaries(config: dict[str, Any]) -> list[str]:
    return [machine_id for machine_id, _, _ in _diagram_boundary_specs(config)]


def generate_d2_composition(config: dict[str, Any], source_name: str) -> str:
    boundary_specs = _diagram_boundary_specs(config)
    board_links = {
        f"{owner_machine}.{owner_state}": f"layers.{detail_machine}"
        for detail_machine, owner_machine, owner_state in boundary_specs
    }
    overview = generate_d2(
        config,
        source_name,
        board_links=board_links,
    ).rstrip()
    if not boundary_specs:
        return overview + "\n"

    lines = overview.splitlines()
    lines.extend(["", "layers: {"])
    for detail_machine, _, _ in boundary_specs:
        detail = generate_d2(
            config,
            source_name,
            root_machine_override=detail_machine,
            embedded=True,
        ).strip()
        lines.append(f"  {detail_machine}: {{")
        lines.extend(f"    {line}" if line else "" for line in detail.splitlines())
        lines.append("  }")
    lines.append("}")
    return "\n".join(lines) + "\n"


def load_config(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as stream:
            config = tomllib.load(stream)
    except (OSError, tomllib.TOMLDecodeError) as exc:
        raise ConfigError(f"unable to read {path}: {exc}") from exc
    if not isinstance(config, dict):
        raise ConfigError("top-level TOML value must be a table")
    version = config.get("version")
    if version != 2:
        raise ConfigError(f"unsupported or missing config version: {version!r}")
    return config


def _resolve_output(config_path: Path, value: Any, override: Path | None) -> Path:
    if override is not None:
        return override.resolve()
    output = Path(_expect_string(value, "output path"))
    if not output.is_absolute():
        output = config_path.parent / output
    return output.resolve()


def _write_if_changed(path: Path, content: str) -> bool:
    if path.exists() and path.read_text() == content:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "w") as stream:
            stream.write(content)
        os.replace(temp_name, path)
    except Exception:
        with suppress(FileNotFoundError):
            os.unlink(temp_name)
        raise
    return True


def _resolved_executable(path: Path, context: str) -> Path:
    try:
        return path.resolve(strict=True)
    except OSError as exc:
        raise ConfigError(f"unable to resolve {context}: {exc}") from exc


def _is_executable_file(path: Path) -> bool:
    try:
        return path.is_file() and os.access(path, os.X_OK)
    except OSError:
        return False


def _resolve_d2_binary() -> Path:
    configured = os.environ.get("D2_BIN", "").strip()
    if configured:
        configured_path = Path(configured).expanduser()
        if _is_executable_file(configured_path):
            return _resolved_executable(configured_path, "D2_BIN")
        resolved = shutil.which(configured)
        if resolved:
            return _resolved_executable(Path(resolved), "D2_BIN")
        raise ConfigError(f"D2_BIN is not an executable: {configured}")

    resolved = shutil.which("d2")
    if resolved:
        return _resolved_executable(Path(resolved), "d2 executable")
    go_binary = Path.home() / "go" / "bin" / "d2"
    if _is_executable_file(go_binary):
        return _resolved_executable(go_binary, "Go-installed d2 executable")
    raise ConfigError(
        "a TALA-capable d2 executable is required to render SVG; install d2 "
        "or set D2_BIN"
    )


def _render_d2_svg_set(d2_content: str, d2_binary: Path) -> dict[str, bytes]:
    with tempfile.TemporaryDirectory(prefix="pearl-fsm-d2-") as temp_name:
        temp_dir = Path(temp_name)
        input_path = temp_dir / "diagram.d2"
        output_path = temp_dir / "diagram.svg"
        input_path.write_text(d2_content)
        command = [
            str(d2_binary),
            "--layout=tala",
            f"--pad={_D2_SVG_PAD}",
            "--omit-version",
            str(input_path),
            str(output_path),
        ]
        try:
            result = subprocess.run(
                command,
                capture_output=True,
                check=False,
                text=True,
                timeout=180,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise ConfigError(f"unable to render D2 SVG set: {exc}") from exc
        if result.returncode != 0:
            detail = result.stderr.strip() or result.stdout.strip() or "unknown error"
            raise ConfigError(f"D2 SVG rendering failed: {detail}")

        rendered_dir = output_path.with_suffix("")
        if output_path.is_file():
            return {"index.svg": output_path.read_bytes()}
        if not rendered_dir.is_dir():
            raise ConfigError(
                "D2 did not produce an SVG file or the expected linked SVG "
                "directory for the composed diagram"
            )
        files = {
            str(path.relative_to(rendered_dir)): path.read_bytes()
            for path in sorted(rendered_dir.rglob("*"))
            if path.is_file()
        }
        if "index.svg" not in files:
            raise ConfigError("D2 linked SVG output is missing index.svg")
        return files


def _svg_contents(path: Path) -> dict[str, bytes]:
    if not path.is_dir():
        return {}
    return {
        str(file_path.relative_to(path)): file_path.read_bytes()
        for file_path in sorted(path.rglob("*.svg"))
        if file_path.is_file()
    }


def _write_svg_set_if_changed(path: Path, files: dict[str, bytes]) -> bool:
    if _svg_contents(path) == files:
        return False
    if path.exists() and not path.is_dir():
        raise ConfigError(f"SVG output path is not a directory: {path}")
    path.mkdir(parents=True, exist_ok=True)
    expected = set(files)
    for relative_path, content in files.items():
        output_path = path / relative_path
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_bytes(content)
    for stale_path in sorted(path.rglob("*.svg"), reverse=True):
        if str(stale_path.relative_to(path)) not in expected:
            stale_path.unlink()
    return True


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path, help="implementation-neutral FSM TOML")
    parser.add_argument("--runtime-out", type=Path, help="override runtime plug path")
    parser.add_argument("--d2-out", type=Path, help="override D2 output path")
    parser.add_argument(
        "--bindings-out", type=Path, help="override D2/MOOS binding output path"
    )
    parser.add_argument(
        "--check", action="store_true", help="fail if generated files are stale"
    )
    args = parser.parse_args(argv)

    config_path = args.config.resolve()
    try:
        config = load_config(config_path)
        outputs = _expect_table(config.get("outputs"), "outputs")
        runtime_path = _resolve_output(
            config_path, outputs.get("runtime"), args.runtime_out
        )
        d2_path = _resolve_output(config_path, outputs.get("d2"), args.d2_out)
        svg_path = _resolve_output(config_path, outputs.get("svg"), None)
        pshare_path = _resolve_output(config_path, outputs.get("pshare"), None)
        scopes_path = _resolve_output(config_path, outputs.get("scopes"), None)
        if args.bindings_out is None:
            bindings_path = _resolve_output(config_path, outputs.get("bindings"), None)
        else:
            bindings_path = args.bindings_out.resolve()
        runtime_content = generate_runtime(config, config_path.name)
        pshare_content = generate_fsm_pshare(config, config_path.name)
        scopes_content = generate_fsm_scopes(config, config_path.name)
        boundary_machines = _diagram_boundaries(config)
        d2_content = generate_d2_composition(config, config_path.name)
        svg_files = _render_d2_svg_set(d2_content, _resolve_d2_binary())
        legacy_d2_paths = [
            d2_path.with_name(f"{d2_path.stem}_{machine_id}{d2_path.suffix}")
            for machine_id in boundary_machines
        ]
        binding_diagrams: list[tuple[str, str | None]] = [(d2_path.name, None)]
        binding_diagrams.extend(
            (f"{d2_path.name}#layers.{machine_id}", machine_id)
            for machine_id in boundary_machines
        )
        bindings_content = generate_d2_bindings(
            config,
            config_path.name,
            binding_diagrams,
        )
    except ConfigError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    if args.check:
        stale = []
        if not runtime_path.exists() or runtime_path.read_text() != runtime_content:
            stale.append(str(runtime_path))
        if not d2_path.exists() or d2_path.read_text() != d2_content:
            stale.append(str(d2_path))
        if _svg_contents(svg_path) != svg_files:
            stale.append(str(svg_path))
        if not pshare_path.exists() or pshare_path.read_text() != pshare_content:
            stale.append(str(pshare_path))
        if not scopes_path.exists() or scopes_path.read_text() != scopes_content:
            stale.append(str(scopes_path))
        if not bindings_path.exists() or bindings_path.read_text() != bindings_content:
            stale.append(str(bindings_path))
        stale.extend(
            f"{legacy_path} (obsolete)"
            for legacy_path in legacy_d2_paths
            if legacy_path.exists()
        )
        if stale:
            print("stale generated files:", file=sys.stderr)
            for stale_path in stale:
                print(f"  {stale_path}", file=sys.stderr)
            return 1
        print("FSM generated files are up to date.")
        return 0

    changed = []
    if _write_if_changed(runtime_path, runtime_content):
        changed.append(str(runtime_path))
    if _write_if_changed(d2_path, d2_content):
        changed.append(str(d2_path))
    if _write_svg_set_if_changed(svg_path, svg_files):
        changed.append(str(svg_path))
    if _write_if_changed(pshare_path, pshare_content):
        changed.append(str(pshare_path))
    if _write_if_changed(scopes_path, scopes_content):
        changed.append(str(scopes_path))
    if _write_if_changed(bindings_path, bindings_content):
        changed.append(str(bindings_path))
    for legacy_path in legacy_d2_paths:
        if legacy_path.exists():
            legacy_path.unlink()
            changed.append(f"{legacy_path} (removed)")
    if changed:
        print("Generated:")
        for changed_path in changed:
            print(f"  {changed_path}")
    else:
        print("FSM generated files already up to date.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
