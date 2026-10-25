#!/usr/bin/env python3
"""Validate the static PIO/SM/DMA flight-persona contract."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
DEFAULT_RESOURCES = ROOT / "components/tdma/inc/tdma_state_machine_resources.h"
DEFAULT_PHYS = ROOT / "components/tdma/src/tdma_pio_spi_phys.c"
# N5.0.4o: the phys core is split into three units; the gate scans the union.
DEFAULT_PHYS_UNITS = (DEFAULT_PHYS,
                      ROOT / "components/tdma/src/tdma_pio_spi_phys_ring_io.c",
                      ROOT / "components/tdma/src/tdma_pio_spi_phys_arm.c")
DEFAULT_FLIGHT_IO = ROOT / "components/tdma/src/tdma_pio_spi_phys_flight_io.c"
DEFAULT_SYNC_MODEL = ROOT / "components/sync_io/src/sync_io_model_sched.c"
DEFAULT_SYNC_CORE = ROOT / "components/sync_io/src/sync_io.c"
# N5.0.4i: the SYNC runtime core is split into two translation units; the gate
# scans the union so function bodies may live in either of them.
DEFAULT_SYNC_CORE_OUTPUT = ROOT / "components/sync_io/src/sync_io_output.c"
DEFAULT_SYNC_CORE_UNITS = (DEFAULT_SYNC_CORE, DEFAULT_SYNC_CORE_OUTPUT)
DEFAULT_SYNC_PERSONAS = ROOT / "components/sync_io/src/sync_io_persona_resources.c"


REQUIRED = {
    "BOARD_TDMA_TX_PIO_BLOCK_ID": 1,
    "BOARD_TDMA_RX_PIO_BLOCK_ID": 2,
    "BOARD_TDMA_SMA_PIO_BLOCK_ID": 0,
    "BOARD_TDMA_TX_CONTROL_OUT_SM": 0,
    "BOARD_TDMA_TX_RTT_EVIDENCE_SM": 1,
    "BOARD_TDMA_TX_CLOCK_LATCH_SM": 2,
    "BOARD_TDMA_TX_DATA_CAPTURE_SM": 3,
    "BOARD_TDMA_RX_RESERVED_CONTROL_SM": 0,
    "BOARD_TDMA_RX_RESERVED_EVIDENCE_SM": 1,
    "BOARD_TDMA_RX_DATA_FLIGHT_SM": 2,
    "BOARD_TDMA_RX_CLOCK_LATCH_SM": 3,
    # Deprecated directional names remain checked as compatibility slots.
    "BOARD_TDMA_TX_CLK_OUT_SM": 0,
    "BOARD_TDMA_TX_SYNC_OUT_SM": 1,
    "BOARD_TDMA_TX_DATA_IN_FORWARD_SM": 2,
    "BOARD_TDMA_TX_DATA_IN_CAPTURE_SM": 3,
    "BOARD_TDMA_RX_CLK_IN_SM": 0,
    "BOARD_TDMA_RX_SYNC_IN_SM": 1,
    "BOARD_TDMA_RX_DATA_OUT_SM": 2,
    "BOARD_TDMA_RX_EVIDENCE_IN_SM": 3,
    "BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL": 4,
    "BOARD_TDMA_RX_DATA_OUT_DMA_CHANNEL": 5,
    "BOARD_TDMA_TX_DATA_IN_FORWARD_DMA_CHANNEL": 6,
    "BOARD_TDMA_TX_SYNC_EDGE_DMA_CHANNEL": 7,
    # Deprecated runtime aliases stay on the old PIO2 numbering until the
    # crossed-PIO implementation consumes the directional roles directly.
    "BOARD_TDMA_TX_CONTROL_SM": 0,
    "BOARD_TDMA_TX_DATA_SM": 1,
    "BOARD_TDMA_TX_EDGE_SM": 2,
    "BOARD_TDMA_TX_RECOVERY_SM": 3,
    "BOARD_TDMA_RX_FORWARD_SM": 0,
    "BOARD_TDMA_RX_CAPTURE_SM": 1,
    "BOARD_TDMA_RX_CONTROL_SM": 2,
    "BOARD_TDMA_RX_EVIDENCE_SM": 3,
    "BOARD_TDMA_TX_PAYLOAD_DMA_CHANNEL": 5,
    "BOARD_TDMA_RX_CAPTURE_DMA_CHANNEL": 4,
    "BOARD_TDMA_RX_FORWARD_DMA_CHANNEL": 6,
    "BOARD_TDMA_TX_EDGE_DMA_CHANNEL": 7,
}


def macros(text: str) -> dict[str, int]:
    return {
        name: int(value)
        for name, value in re.findall(
            r"^#define\s+(BOARD_TDMA_[A-Z0-9_]+)\s+([0-9]+)u?\s*$",
            text,
            re.MULTILINE,
        )
    }


def program_body(text: str, name: str) -> str:
    match = re.search(
        rf"\.program\s+{re.escape(name)}\b(?P<body>.*?)(?=^\.program\s+|^% c-sdk|\Z)",
        text,
        re.MULTILINE | re.DOTALL,
    )
    if not match:
        raise ValueError(f"missing PIO program {name}")
    return match.group("body")


def instruction_text(body: str) -> str:
    return "\n".join(line.split(";", 1)[0] for line in body.splitlines())


def _c_character_literal_end(text: str, opening: int) -> int:
    escaped = False
    for index in range(opening + 1, len(text)):
        char = text[index]
        if char in "\r\n":
            return -1
        if escaped:
            escaped = False
        elif char == "\\":
            escaped = True
        elif char == "'":
            return index
    return -1


_C_TRIGRAPHS = {
    "??=": "#", "??/": "\\", "??'": "^", "??(": "[", "??)": "]",
    "??!": "|", "??<": "{", "??>": "}", "??-": "~",
}


def _c_phase1_source(text: str, masked: list[str]) -> tuple[str, list[int]]:
    logical: list[str] = []
    positions: list[int] = []
    index = 0
    while index < len(text):
        replacement = _C_TRIGRAPHS.get(text[index:index + 3])
        if replacement is not None:
            masked[index] = replacement
            masked[index + 1] = masked[index + 2] = " "
            logical.append(replacement)
            positions.append(index)
            index += 3
            continue
        logical.append(text[index])
        positions.append(index)
        index += 1
    return "".join(logical), positions


def _c_phase2_source(text: str, masked: list[str]) -> tuple[str, list[int]]:
    """Splice escaped newlines and retain logical-to-physical positions."""
    phase1, phase1_positions = _c_phase1_source(text, masked)
    logical: list[str] = []
    positions: list[int] = []
    index = 0
    while index < len(phase1):
        if phase1[index] == "\\" and index + 1 < len(phase1):
            newline_length = 0
            if phase1[index + 1] == "\n":
                newline_length = 1
            elif (phase1[index + 1] == "\r" and index + 2 < len(phase1) and
                  phase1[index + 2] == "\n"):
                newline_length = 2
            if newline_length:
                masked[phase1_positions[index]] = " "
                index += newline_length + 1
                continue
        logical.append(phase1[index])
        positions.append(phase1_positions[index])
        index += 1
    return "".join(logical), positions


def _c_preprocessor_literal(expression: str) -> bool | None:
    """Evaluate the bounded integer subset used by #if expressions."""
    expression = re.sub(r"/\*.*?\*/|//.*", " ", expression)
    token_pattern = re.compile(
        r"\s*(?:(&&|\|\||<<|>>|<=|>=|==|!=|[!~+\-*/%<>&^|?:()])|"
        r"(0[xX][0-9A-Fa-f]+|0[bB][01]+|0[0-7]*|[1-9][0-9]*)"
        r"([uUlL]*))"
    )
    tokens: list[tuple[str, str]] = []
    cursor = 0
    while cursor < len(expression):
        match = token_pattern.match(expression, cursor)
        if match is None:
            if expression[cursor:].strip() == "":
                break
            return None
        operator, number, suffix = match.groups()
        # Exact C integer rank/signedness depends on spelling and target ABI.
        # Keep those expressions unknown instead of guessing conversions.
        if number is not None and (
                suffix or (number != "0" and number.startswith("0"))):
            return None
        tokens.append(("operator", operator) if operator is not None else
                      ("number", number))
        cursor = match.end()

    min_value = -(1 << 63)
    max_value = (1 << 63) - 1

    def bounded(value: int) -> int | None:
        return value if min_value <= value <= max_value else None

    position = 0

    def parse_primary() -> int | None:
        nonlocal position
        if position >= len(tokens):
            return None
        kind, value = tokens[position]
        if kind == "number":
            position += 1
            if value.lower().startswith("0x"):
                return bounded(int(value, 16))
            if value.lower().startswith("0b"):
                return bounded(int(value, 2))
            if len(value) > 1 and value.startswith("0"):
                return bounded(int(value, 8))
            return bounded(int(value, 10))
        if value != "(":
            return None
        position += 1
        result = parse_conditional()
        if (result is None or position >= len(tokens) or
                tokens[position] != ("operator", ")")):
            return None
        position += 1
        return result

    def parse_unary() -> int | None:
        nonlocal position
        if (position < len(tokens) and tokens[position][0] == "operator" and
                tokens[position][1] in ("+", "-", "!", "~")):
            operator = tokens[position][1]
            position += 1
            operand = parse_unary()
            if operand is None:
                return None
            if operator == "+":
                return operand
            if operator == "-":
                return bounded(-operand)
            if operator == "!":
                return int(operand == 0)
            return bounded(~operand)
        return parse_primary()

    def parse_binary(next_parser, operators: tuple[str, ...], operation) -> int | None:
        nonlocal position
        result = next_parser()
        if result is None:
            return None
        while (position < len(tokens) and tokens[position][0] == "operator" and
               tokens[position][1] in operators):
            operator = tokens[position][1]
            position += 1
            operand = next_parser()
            if operand is None:
                return None
            result = operation(operator, result, operand)
            if result is None:
                return None
        return result

    def parse_mul() -> int | None:
        def operation(operator: str, left: int, right: int) -> int | None:
            if operator == "*":
                return bounded(left * right)
            if right == 0:
                return None
            quotient = abs(left) // abs(right)
            if operator == "/":
                return bounded(-quotient if (left < 0) != (right < 0) else quotient)
            remainder = abs(left) % abs(right)
            return bounded(-remainder if left < 0 else remainder)
        return parse_binary(parse_unary, ("*", "/", "%"), operation)

    def parse_add() -> int | None:
        return parse_binary(
            parse_mul, ("+", "-"),
            lambda operator, left, right: bounded(
                left + right if operator == "+" else left - right))

    def parse_shift() -> int | None:
        def operation(operator: str, left: int, right: int) -> int | None:
            if right < 0 or right >= 64:
                return None
            return bounded(left << right if operator == "<<" else left >> right)
        return parse_binary(parse_add, ("<<", ">>"), operation)

    def parse_relational() -> int | None:
        return parse_binary(
            parse_shift, ("<", "<=", ">", ">="),
            lambda operator, left, right: int({
                "<": left < right, "<=": left <= right,
                ">": left > right, ">=": left >= right,
            }[operator]))

    def parse_equality() -> int | None:
        return parse_binary(
            parse_relational, ("==", "!="),
            lambda operator, left, right: int(
                left == right if operator == "==" else left != right))

    def parse_bit_and() -> int | None:
        return parse_binary(parse_equality, ("&",), lambda _o, l, r: bounded(l & r))

    def parse_bit_xor() -> int | None:
        return parse_binary(parse_bit_and, ("^",), lambda _o, l, r: bounded(l ^ r))

    def parse_bit_or() -> int | None:
        return parse_binary(parse_bit_xor, ("|",), lambda _o, l, r: bounded(l | r))

    def parse_and() -> int | None:
        return parse_binary(parse_bit_or, ("&&",), lambda _o, l, r: int(bool(l) and bool(r)))

    def parse_or() -> int | None:
        return parse_binary(parse_and, ("||",), lambda _o, l, r: int(bool(l) or bool(r)))

    def parse_conditional() -> int | None:
        nonlocal position
        result = parse_or()
        if result is None:
            return None
        if position >= len(tokens) or tokens[position] != ("operator", "?"):
            return result
        position += 1
        true_value = parse_conditional()
        if position >= len(tokens) or tokens[position] != ("operator", ":"):
            return None
        position += 1
        false_value = parse_conditional()
        if true_value is None or false_value is None:
            return None
        return true_value if result != 0 else false_value

    result = parse_conditional()
    return bool(result) if result is not None and position == len(tokens) else None


def _c_update_conditional_stack(
    directive: str,
    stack: list[dict[str, bool]],
    *,
    unknown_active: bool,
    include_guard: str | None = None,
) -> None:
    directive = re.sub(r"^(\s*)%:", r"\1#", directive, count=1)
    match = re.match(r"\s*#\s*([A-Za-z_][A-Za-z0-9_]*)(.*)", directive)
    if match is None:
        return
    keyword = match.group(1)
    expression = match.group(2)
    active = stack[-1]["active"] if stack else True
    if keyword in ("if", "ifdef", "ifndef"):
        if keyword == "if":
            condition = _c_preprocessor_literal(expression)
        elif (keyword == "ifndef" and include_guard is not None and
              expression.strip() == include_guard):
            condition = True
        else:
            condition = None
        known = condition is not None
        stack.append({
            "parent": active,
            "known": known,
            "taken": bool(condition) if known else False,
            "active": active and (
                bool(condition) if known else unknown_active),
        })
    elif keyword == "elif" and stack:
        frame = stack[-1]
        if not frame["known"]:
            frame["active"] = frame["parent"] and unknown_active
        elif frame["taken"]:
            frame["active"] = False
        else:
            condition = _c_preprocessor_literal(expression)
            if condition is None:
                frame["known"] = False
                frame["active"] = frame["parent"] and unknown_active
            else:
                frame["taken"] = bool(condition)
                frame["active"] = frame["parent"] and bool(condition)
    elif keyword == "else" and stack:
        frame = stack[-1]
        frame["active"] = (
            frame["parent"] and
            (not frame["taken"] if frame["known"] else unknown_active)
        )
        frame["taken"] = True
    elif keyword == "endif" and stack:
        stack.pop()


def _mask_c_comments_and_literals(
    text: str,
    *,
    unknown_active: bool = False,
    preserve_directives: bool = False,
) -> str:
    """Return an equal-length phase-2 view with inactive code masked."""
    masked = list(text)
    logical, positions = _c_phase2_source(text, masked)
    guard_match = re.match(
        r"\s*(?:#|%:)\s*ifndef\s+([A-Za-z_][A-Za-z0-9_]*)\s*\r?\n"
        r"\s*(?:#|%:)\s*define\s+\1\b",
        logical,
    )
    include_guard = guard_match.group(1) if guard_match is not None else None
    index = 0
    state = "code"
    line_leading_space = True
    conditional_stack: list[dict[str, bool]] = []

    def active() -> bool:
        return conditional_stack[-1]["active"] if conditional_stack else True

    def mask(index_to_mask: int) -> None:
        physical = positions[index_to_mask]
        if text[physical] not in "\r\n":
            masked[physical] = " "

    while index < len(logical):
        char = logical[index]
        following = logical[index + 1] if index + 1 < len(logical) else ""
        if state == "code":
            if char in "\r\n":
                line_leading_space = True
                index += 1
                continue
            if char.isspace():
                if not active():
                    mask(index)
                index += 1
                continue
            if char == "/" and following == "*":
                mask(index)
                mask(index + 1)
                index += 2
                state = "block_comment"
                continue
            if char == "/" and following == "/":
                mask(index)
                mask(index + 1)
                index += 2
                state = "line_comment"
                continue
            if (char == "#" or (char == "%" and following == ":")) and \
                    line_leading_space:
                line_end = index
                while line_end < len(logical) and logical[line_end] not in "\r\n":
                    line_end += 1
                was_active = active()
                _c_update_conditional_stack(
                    logical[index:line_end], conditional_stack,
                    unknown_active=unknown_active,
                    include_guard=include_guard)
                if not preserve_directives or not was_active:
                    for directive_index in range(index, line_end):
                        mask(directive_index)
                index = line_end
                continue
            if char == '"' or (
                char == "'" and _c_character_literal_end(logical, index) >= 0
            ):
                mask(index)
                index += 1
                state = "string" if char == '"' else "character"
                line_leading_space = False
                continue
            if not active():
                mask(index)
            line_leading_space = False
        elif state == "block_comment":
            if char == "*" and following == "/":
                mask(index)
                mask(index + 1)
                index += 2
                state = "code"
                continue
            if char not in "\r\n":
                mask(index)
            else:
                line_leading_space = True
        elif state == "line_comment":
            if char in "\r\n":
                state = "code"
                line_leading_space = True
            else:
                mask(index)
        else:
            if char == "\\" and following:
                mask(index)
                mask(index + 1)
                index += 2
                continue
            quote = '"' if state == "string" else "'"
            if char == quote:
                state = "code"
            if char not in "\r\n":
                mask(index)
        index += 1
    return "".join(masked)


def _closing_delimiter(text: str, opening: int, left: str, right: str) -> int:
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == left:
            depth += 1
        elif text[index] == right:
            depth -= 1
            if depth == 0:
                return index
    return -1


def _c_function_candidates(text: str, name: str) -> list[tuple[str, bool]]:
    # Locate definitions in any possibly active branch (for example an include
    # guard). Callers decide separately whether body tokens prove required or
    # forbidden behavior.
    code = _mask_c_comments_and_literals(text, unknown_active=True)
    definite = _mask_c_comments_and_literals(text)
    saw_name = False
    candidates: list[tuple[str, bool]] = []
    for marker in re.finditer(rf"\b{re.escape(name)}\s*\(", code):
        saw_name = True
        opening_paren = code.find("(", marker.start())
        closing_paren = _closing_delimiter(code, opening_paren, "(", ")")
        if closing_paren < 0:
            continue
        cursor = closing_paren + 1
        while cursor < len(code):
            while cursor < len(code) and code[cursor].isspace():
                cursor += 1
            if cursor >= len(code):
                break
            if code[cursor] == "{":
                closing = _closing_delimiter(code, cursor, "{", "}")
                if closing < 0:
                    raise ValueError(f"unterminated C function body {name}")
                candidates.append((
                    text[cursor + 1:closing],
                    name in definite[marker.start():marker.end()],
                ))
                break
            identifier = re.match(r"[A-Za-z_][A-Za-z0-9_]*", code[cursor:])
            if identifier is not None:
                cursor += identifier.end()
                continue
            if code[cursor] == "(":
                attribute_end = _closing_delimiter(code, cursor, "(", ")")
                if attribute_end < 0:
                    break
                cursor = attribute_end + 1
                continue
            break
    if candidates:
        return candidates
    if saw_name:
        raise ValueError(f"missing C function body {name}")
    raise ValueError(f"missing C function {name}")


def c_function_body(text: str, name: str) -> str:
    return _c_function_candidates(text, name)[0][0]


def c_definition_body(text: str, name: str) -> str:
    """Extract a C definition, skipping declarations and call sites."""
    return c_function_body(text, name)


def _c_descriptor_candidates(
    text: str,
    identity: str,
) -> list[tuple[str, bool]]:
    possible = _mask_c_comments_and_literals(text, unknown_active=True)
    definite = _mask_c_comments_and_literals(text)
    marker_pattern = re.compile(
        rf"\.id\s*=\s*{re.escape(identity)}\s*,")
    candidates: list[tuple[str, bool]] = []
    for marker in marker_pattern.finditer(possible):
        opening = possible.rfind("{", 0, marker.start())
        if opening < 0:
            continue
        closing = _closing_delimiter(possible, opening, "{", "}")
        if closing < marker.end():
            continue
        candidates.append((
            text[opening + 1:closing],
            identity in definite[marker.start():marker.end()],
        ))
    if not candidates:
        raise ValueError(f"missing C descriptor {identity}")
    return candidates


def _c_macro_candidates(text: str, name: str) -> list[tuple[str, bool]]:
    possible = _mask_c_comments_and_literals(
        text, unknown_active=True, preserve_directives=True)
    definite = _mask_c_comments_and_literals(
        text, preserve_directives=True)
    pattern = re.compile(
        rf"(?m)^\s*(?:#|%:)\s*define\s+{re.escape(name)}\b([^\r\n]*)")
    candidates: list[tuple[str, bool]] = []
    for match in pattern.finditer(possible):
        value = re.sub(r"/\*.*?\*/|//.*", " ", match.group(1)).strip()
        is_definite = bool(definite[match.start():match.end()].strip())
        candidates.append((value, is_definite))
    return candidates


def _sync_require_macro(
    failures: list[str],
    board_text: str,
    name: str,
    expected: str | None,
) -> None:
    candidates = _c_macro_candidates(board_text, name)
    definite_values = [value for value, definite in candidates if definite]
    if not definite_values:
        failures.append(f"SYNC board profile is missing definite #define {name}")
        return
    accepted = expected if expected is not None else definite_values[0]
    if any(value != accepted for value, _ in candidates):
        failures.append(
            f"SYNC board profile has conflicting {name}; expected {accepted}")


def _sync_require_tokens(
    failures: list[str],
    body: str,
    required: tuple[str, ...],
    forbidden: tuple[str, ...],
    label: str,
) -> None:
    required_body = _mask_c_comments_and_literals(body)
    forbidden_body = _mask_c_comments_and_literals(
        body, unknown_active=True)
    for token in required:
        if token not in required_body:
            failures.append(f"{label} is missing {token}")
    for token in forbidden:
        if token in forbidden_body:
            failures.append(f"{label} contains forbidden {token}")


def _sync_require_candidates(
    failures: list[str],
    candidates: list[tuple[str, bool]],
    required: tuple[str, ...],
    forbidden: tuple[str, ...],
    label: str,
) -> None:
    if not any(definite for _, definite in candidates):
        failures.append(f"{label} has no definitely active definition")
    for body, _ in candidates:
        _sync_require_tokens(failures, body, required, forbidden, label)


def check_sync_io_runtime(
    model_text: str,
    core_text: str,
    persona_text: str,
    board_text: str,
) -> list[str]:
    """Validate the SYNC_IO PIO0 persona handoff and call boundaries."""
    failures: list[str] = []

    for name, expected in (
        ("BOARD_SYNC_PIO_FAST", "pio0"),
        ("BOARD_SYNC_PIO_WAVE", "pio1"),
        ("BOARD_SYNC_PIO0_WAVE_OUTPUT_SM", None),
        ("BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM", None),
        ("BOARD_SYNC_PIO0_LOGIC_ANALYZER_SM", None),
    ):
        _sync_require_macro(failures, board_text, name, expected)

    for token in (
        "SYNC_IO_PERSONA_ID_WAVE_OUTPUT",
        "SYNC_IO_PERSONA_ID_SCHEDULED_TRIGGER",
        "SYNC_IO_PERSONA_ID_LOGIC_ANALYZER",
        "SYNC_IO_PERSONA_FLAG_READ_ONLY_PAD",
        "SYNC_IO_PERSONA_IMPLEMENTATION_MIGRATION_TARGET",
        "SYNC_IO_PERSONA_DMA_CHANNEL_MASK",
        "SYNC_IO_PERSONA_WORKSPACE_CAPTURE_SCHEDULE",
    ):
        if token not in persona_text:
            failures.append(f"SYNC persona catalog is missing {token}")

    try:
        output_arm = _c_function_candidates(
            model_text, "sync_io_output_pulse_schedule_arm")
        output_arm_ns = _c_function_candidates(
            model_text, "sync_io_output_pulse_schedule_arm_ns")
        observer_arm = _c_function_candidates(
            model_text, "sync_io_sma_observer_pulse_schedule_arm_periodic_ns")
        arm_common = _c_function_candidates(
            model_text, "sync_io_pulse_schedule_arm_on_pin_common_owned")
        manager_start = _c_function_candidates(
            model_text, "sync_io_wave_output_manager_start")
        manager_release = _c_function_candidates(
            model_text, "sync_io_wave_output_manager_release")
        capture_start = _c_function_candidates(
            core_text, "sync_io_start_capture_owned")
        debug_output = _c_function_candidates(
            core_text, "sync_io_debug_set_output_mask_owned")
        frequency_start = _c_function_candidates(
            core_text, "sync_io_sma_frequency_tx_start_owned")
    except ValueError as exc:
        failures.append(str(exc))
        return failures

    for label, candidates in (
        ("SYNC WAVE_OUTPUT arm", output_arm),
        ("SYNC WAVE_OUTPUT ns arm", output_arm_ns),
    ):
        _sync_require_candidates(
            failures,
            candidates,
            (
                "BOARD_SYNC_PIO_FAST",
                "BOARD_SYNC_PIO0_WAVE_OUTPUT_SM",
                "DREQ_PIO0_TX0",
                "sync_io_pulse_schedule_arm",
            ),
            ("BOARD_SYNC_PIO_WAVE", "DREQ_PIO1_TX0",
             "BOARD_SYNC_MODEL_SCHED_SM"),
            label,
        )
    _sync_require_candidates(
        failures,
        observer_arm,
        (
            "BOARD_SYNC_PIO_FAST",
            "BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM",
            "DREQ_PIO0_TX0",
            "sync_io_pulse_schedule_arm",
        ),
        ("BOARD_SYNC_PIO_WAVE", "DREQ_PIO1_TX0",
         "BOARD_SYNC_MODEL_SCHED_SM"),
        "SYNC SCHEDULED_TRIGGER arm",
    )

    _sync_require_candidates(
        failures,
        arm_common,
        (
            "sync_io_core_capture_is_running",
            "sync_io_model_pulse_schedule_disarm_owned",
            "s_model_pulse.words = sync_io_shared_workspace",
         "sync_io_wave_output_manager_start",
         ),
        (),
        "SYNC output schedule common arm",
    )
    arm_order_tokens = (
        "sync_io_core_capture_is_running",
        "sync_io_model_pulse_schedule_disarm_owned",
        "s_model_pulse.words = sync_io_shared_workspace",
        "sync_io_wave_output_manager_start",
    )
    for arm_body, _ in arm_common:
        arm_common_code = _mask_c_comments_and_literals(arm_body)
        if ("sync_io_core_capture_is_running() && !observer_capture_overlap"
                not in arm_common_code):
            failures.append(
                "SYNC output schedule common arm is missing "
                "sync_io_core_capture_is_running")
        if all(token in arm_common_code for token in arm_order_tokens):
            arm_order = tuple(
                arm_common_code.index(token) for token in arm_order_tokens)
            if arm_order != tuple(sorted(arm_order)):
                failures.append(
                    "SYNC output schedule arm must reject active capture, disarm "
                    "the old schedule, bind the shared workspace, then start the "
                    "PIO0 persona")

    _sync_require_candidates(
        failures,
        manager_start,
        (
            "sync_io_persona_manager_claim",
            "sync_io_persona_manager_load",
            "sync_io_persona_manager_arm",
            "sync_io_persona_manager_start",
        ),
        (),
        "SYNC output persona start",
    )
    _sync_require_candidates(
        failures,
        manager_release,
        ("sync_io_persona_manager_release",),
        (),
        "SYNC output persona release",
    )

    for forbidden in (
        "sync_io_suspend_for_tdma_flight",
        "sync_io_resume_after_tdma_flight",
        "sync_io_core_tdma_flight_suspended",
        "tdma_flight_suspended",
    ):
        if (forbidden in _mask_c_comments_and_literals(
                core_text, unknown_active=True) or
                forbidden in _mask_c_comments_and_literals(
                    model_text, unknown_active=True)):
            failures.append(
                f"legacy PIO1 TDMA handoff symbol remains in SYNC_IO: {forbidden}")

    for function_name in (
        "sync_io_model_pulse_schedule_arm",
        "sync_io_model_pulse_schedule_arm_ns",
        "sync_io_model_pulse_schedule_arm_periodic_ns",
    ):
        try:
            candidates = _c_function_candidates(model_text, function_name)
        except ValueError as exc:
            failures.append(str(exc))
            continue
        for body, _ in candidates:
            body = _mask_c_comments_and_literals(body, unknown_active=True)
            for forbidden in ("BOARD_SYNC_PIO_WAVE", "DREQ_PIO1_TX0",
                              "BOARD_SYNC_MODEL_SCHED_SM"):
                if forbidden in body:
                    failures.append(
                        f"{function_name} still uses legacy PIO1 handoff path: {forbidden}")

    for label, candidates in (
        ("SYNC capture start", capture_start),
        ("SYNC debug output", debug_output),
        ("SYNC SMA frequency start", frequency_start),
    ):
        if not any(definite for _, definite in candidates):
            failures.append(f"{label} has no definitely active definition")
        for body, _ in candidates:
            if ("sync_io_core_wave_output_persona_active_owned" not in
                    _mask_c_comments_and_literals(body)):
                failures.append(
                    f"{label} must observe WAVE_OUTPUT persona ownership")

    try:
        input_capture = _c_descriptor_candidates(
            persona_text, "SYNC_IO_PERSONA_ID_INPUT_CAPTURE")
        wave = _c_descriptor_candidates(
            persona_text, "SYNC_IO_PERSONA_ID_WAVE_OUTPUT")
        scheduled = _c_descriptor_candidates(
            persona_text, "SYNC_IO_PERSONA_ID_SCHEDULED_TRIGGER")
        analyzer = _c_descriptor_candidates(
            persona_text, "SYNC_IO_PERSONA_ID_LOGIC_ANALYZER")
    except ValueError as exc:
        failures.append(str(exc))
    else:
        _sync_require_candidates(
            failures,
            input_capture,
            ("SYNC_IO_PERSONA_WORKSPACE_CAPTURE_SCHEDULE",),
            (),
            "SYNC INPUT_CAPTURE descriptor",
        )
        for label, candidates, sm in (
            ("SYNC WAVE_OUTPUT descriptor", wave,
             "BOARD_SYNC_PIO0_WAVE_OUTPUT_SM"),
            ("SYNC SCHEDULED_TRIGGER descriptor", scheduled,
             "BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM"),
        ):
            _sync_require_candidates(
                failures,
                candidates,
                (
                    "SYNC_IO_PERSONA_IMPLEMENTATION_MIGRATION_TARGET",
                    "BOARD_TDMA_SMA_PIO_BLOCK_ID",
                    sm,
                    "SYNC_IO_MODEL_PULSE_DMA_CH",
                    "SYNC_IO_PERSONA_WORKSPACE_CAPTURE_SCHEDULE",
                ),
                ("SYNC_IO_PERSONA_FLAG_READ_ONLY_PAD",),
                label,
            )
        _sync_require_candidates(
            failures,
            analyzer,
            (
                "SYNC_IO_PERSONA_FLAG_READ_ONLY_PAD",
                "BOARD_SYNC_PIO0_LOGIC_ANALYZER_SM",
                "SYNC_IO_CAPTURE_DMA_CH",
                ".gpio_read_mask",
            ),
            (".gpio_write_mask", "out pins", "set pins"),
            "SYNC LOGIC_ANALYZER descriptor",
        )

    return failures


def check_rx_endpoint_declaration(text: str) -> list[str]:
    failures: list[str] = []
    required = (
        "tdma_state_machine_fifo_endpoint_t",
        "tdma_state_machine_rx_endpoint_contract_t",
        "tdma_state_machine_rx_endpoint_contract_valid",
        ".rx_endpoints = tdma_state_machine_rx_endpoint_contract()",
    )
    for token in required:
        if token not in text:
            failures.append(f"RX endpoint contract is missing {token}")
    try:
        contract = c_function_body(
            text, "tdma_state_machine_rx_endpoint_contract")
    except ValueError as exc:
        failures.append(str(exc))
        return failures

    if contract.count(".pio = BOARD_TDMA_RX_PIO") != 3:
        failures.append("all RX endpoints must remain on the RX PIO")
    if contract.count(".sm = BOARD_TDMA_RX_DATA_FLIGHT_SM") != 2:
        failures.append("DATA output/unload must share the declared RX PIO SM")
    if contract.count(".sm = BOARD_TDMA_RX_CLOCK_LATCH_SM") != 1:
        failures.append("clock evidence must use its dedicated RX PIO SM")
    if ".business_rx_consumer_count = 1u" not in contract:
        failures.append("RX DATA business FIFO must have exactly one consumer")
    for token, expected, message in (
        (".fifo_direction = TDMA_STATE_MACHINE_FIFO_TX",
         1,
         "DATA output endpoint must declare its TX FIFO"),
        (".fifo_direction = TDMA_STATE_MACHINE_FIFO_RX",
         2,
         "DATA unload/evidence endpoints must declare RX FIFOs"),
        (".dreq_direction = TDMA_STATE_MACHINE_DREQ_TX",
         1,
         "DATA output endpoint must declare the TX DREQ"),
        (".dreq_direction = TDMA_STATE_MACHINE_DREQ_RX",
         1,
         "DATA unload endpoint must declare the RX DREQ"),
        (".dreq_direction = TDMA_STATE_MACHINE_DREQ_NONE",
         1,
         "clock evidence endpoint must not declare a DMA DREQ"),
        (".owner = TDMA_STATE_MACHINE_ENDPOINT_OWNER_DMA",
         2,
         "DATA output/unload endpoints must be owned by DMA"),
        (".owner = TDMA_STATE_MACHINE_ENDPOINT_OWNER_CORE1",
         1,
         "clock evidence FIFO must be owned by core1"),
        (".dma_channel = BOARD_TDMA_RX_DATA_OUT_DMA_CHANNEL",
         1,
         "DATA output endpoint must use the output DMA channel"),
        (".dma_channel = BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL",
         1,
         "DATA unload endpoint must use the capture DMA channel"),
        (".dma_channel = TDMA_STATE_MACHINE_DMA_CHANNEL_NONE",
         1,
         "clock evidence endpoint must not claim a DMA channel"),
    ):
        if contract.count(token) != expected:
            failures.append(message)
    return failures


def check_rx_endpoint_runtime(phys_text: str, flight_io_text: str) -> list[str]:
    failures: list[str] = []
    for token in (
        "flight_resources.rx_endpoints.data_output.pio",
        "flight_resources.rx_endpoints.data_output.sm",
        "flight_resources.rx_endpoints.data_unload.pio",
        "flight_resources.rx_endpoints.data_unload.sm",
        "flight_resources.rx_endpoints.clock_evidence.pio",
        "flight_resources.rx_endpoints.clock_evidence.sm",
        "tdma_state_machine_rx_endpoint_contract_valid",
    ):
        if token not in phys_text:
            failures.append(f"flight runtime does not consume {token}")

    try:
        rx_arm = c_function_body(phys_text, "tdma_pio_spi_phys_rx_arm")
        overlay = c_function_body(
            phys_text, "tdma_pio_spi_phys_overlay_binding")
        clock_evidence = c_function_body(
            phys_text, "tdma_pio_spi_phys_clock_latch_read_and_rearm")
        origin_tx = c_function_body(
            flight_io_text, "tdma_pio_spi_phys_flight_origin_tx")
    except ValueError as exc:
        failures.append(str(exc))
        return failures

    if not re.search(
        r"pio_get_dreq\s*\(\s*capture_pio\s*,\s*capture_sm\s*,\s*false\s*\)",
        rx_arm,
    ):
        failures.append("business RX DMA must use the RX DREQ")
    if not re.search(
        r"&\s*capture_pio->rxf\s*\[\s*capture_sm\s*\]", rx_arm
    ):
        failures.append("business RX DMA must read the declared RX FIFO")
    if len(re.findall(r"->rxf\s*\[", rx_arm)) != 1:
        failures.append("business RX FIFO must have one DMA consumer")

    if not re.search(
        r"pio_get_dreq\s*\(.*?\bdata_pio\b.*?\bdata_sm\b.*?true\s*\)",
        origin_tx,
        re.DOTALL,
    ):
        failures.append("origin DATA output must use the TX DREQ")
    if not re.search(
        r"&\s*data_pio->txf\s*\[\s*data_sm\s*\]", origin_tx
    ):
        failures.append("origin DATA output must write the declared TX FIFO")
    if not re.search(
        r"pio_get_dreq\s*\(\s*tdma_pio_spi_phys_data_pio\s*\(\s*phys\s*\)\s*,"
        r"\s*tdma_pio_spi_phys_data_sm\s*\(\s*phys\s*\)\s*,\s*true\s*\)",
        overlay,
        re.DOTALL,
    ):
        failures.append("follower overlay output must use the TX DREQ")
    if not re.search(
        r"&\s*tdma_pio_spi_phys_data_pio\s*\(\s*phys\s*\)->txf\s*\["
        r"\s*tdma_pio_spi_phys_data_sm\s*\(\s*phys\s*\)\s*\]",
        overlay,
    ):
        failures.append(
            "follower overlay output must write the declared TX FIFO")

    if not re.search(
        r"pio_sm_get\s*\(\s*evidence_pio\s*,\s*latch_sm\s*\)",
        clock_evidence,
    ):
        failures.append("clock evidence must be read from its dedicated RX FIFO")
    return failures


def check(
    board: Path,
    pio: Path,
    *,
    resources: Path = DEFAULT_RESOURCES,
    phys: "Path | tuple[Path, ...]" = DEFAULT_PHYS_UNITS,
    flight_io: Path = DEFAULT_FLIGHT_IO,
    sync_model: Path = DEFAULT_SYNC_MODEL,
    sync_core: "Path | tuple[Path, ...]" = DEFAULT_SYNC_CORE_UNITS,
    sync_personas: Path = DEFAULT_SYNC_PERSONAS,
    sync_board: Path | None = None,
) -> list[str]:
    failures: list[str] = []
    values = macros(board.read_text(encoding="utf-8", errors="ignore"))
    for name, expected in REQUIRED.items():
        if values.get(name) != expected:
            failures.append(f"{name}: expected {expected}, got {values.get(name)!r}")

    for label, names in (
        ("TX PIO persona SM", (
            "BOARD_TDMA_TX_CONTROL_OUT_SM", "BOARD_TDMA_TX_RTT_EVIDENCE_SM",
            "BOARD_TDMA_TX_CLOCK_LATCH_SM", "BOARD_TDMA_TX_DATA_CAPTURE_SM",
        )),
        ("RX PIO persona SM", (
            "BOARD_TDMA_RX_RESERVED_CONTROL_SM", "BOARD_TDMA_RX_RESERVED_EVIDENCE_SM",
            "BOARD_TDMA_RX_DATA_FLIGHT_SM", "BOARD_TDMA_RX_CLOCK_LATCH_SM",
        )),
        ("DMA", (
            "BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL",
            "BOARD_TDMA_RX_DATA_OUT_DMA_CHANNEL",
            "BOARD_TDMA_TX_DATA_IN_FORWARD_DMA_CHANNEL",
            "BOARD_TDMA_TX_SYNC_EDGE_DMA_CHANNEL",
        )),
    ):
        seen: dict[int, str] = {}
        for name in names:
            if name not in values:
                continue
            if values[name] in seen:
                failures.append(f"{label} overlap: {name} == {seen[values[name]]}")
            seen[values[name]] = name

    if values.get("BOARD_TDMA_TX_PIO_BLOCK_ID") == values.get("BOARD_TDMA_RX_PIO_BLOCK_ID"):
        failures.append("TX and RX PIO blocks overlap")
    if values.get("BOARD_TDMA_SMA_PIO_BLOCK_ID") in {
        values.get("BOARD_TDMA_TX_PIO_BLOCK_ID"),
        values.get("BOARD_TDMA_RX_PIO_BLOCK_ID"),
    }:
        failures.append("SMA PIO overlaps TDMA PIO")

    try:
        pio_text = pio.read_text(encoding="utf-8", errors="ignore")
        tx = program_body(pio_text, "tdma_pio_spi_directional_data_tx")
        rx = program_body(pio_text, "tdma_pio_spi_directional_data_rx")
        ctl_tx = program_body(pio_text, "tdma_pio_spi_directional_control_tx")
        ctl_rx = program_body(pio_text, "tdma_pio_spi_directional_control_rx")
        flight_data = program_body(pio_text, "tdma_pio_spi_flight_data_follower")
        process_data = program_body(
            pio_text, "tdma_pio_spi_flight_process_follower")
        origin_data_tx = program_body(
            pio_text, "tdma_pio_spi_flight_origin_data_tx")
        origin_data_capture = program_body(
            pio_text, "tdma_pio_spi_flight_origin_data_capture")
        clock_latch = program_body(
            pio_text, "tdma_pio_spi_flight_clock_latch")
        raw_follower_init = c_function_body(
            pio_text, "tdma_pio_spi_flight_data_follower_program_init")
        process_follower_init = c_function_body(
            pio_text, "tdma_pio_spi_flight_process_follower_program_init")
    except ValueError as exc:
        failures.append(str(exc))
    else:
        if re.search(r"\bin\s+pins\b", tx):
            failures.append("directional DATA TX contains in pins")
        if re.search(r"\bout\s+pins\b", rx):
            failures.append("directional DATA RX contains out pins")
        if re.search(r"\bin\s+pins\b", ctl_tx):
            failures.append("directional control TX contains in pins")
        if re.search(r"\bout\s+pins\b", ctl_rx):
            failures.append("directional control RX contains out pins")
        for name, body in (
            ("flight DATA follower", flight_data),
            ("process-image DATA follower", process_data),
        ):
            if not re.search(r"\bin\s+pins\b", body):
                failures.append(f"{name} does not sample incoming DATA")
            output_action = (r"^\s*mov\s+pins,\s*x\s*$"
                             if name == "process-image DATA follower"
                             else r"^\s*out\s+pins,\s*1\s*$")
            if not re.search(output_action, instruction_text(body), re.MULTILINE):
                failures.append(f"{name} does not forward outgoing DATA")
            if not re.search(
                r"^\s*push\s+noblock\b",
                instruction_text(body),
                re.MULTILINE | re.IGNORECASE,
            ):
                failures.append(
                    f"{name} is missing business RX unload push")
        if re.search(r"\bin\s+pins\b", origin_data_tx):
            failures.append("origin DATA TX contains in pins")
        if not re.search(r"\bout\s+pins\b", origin_data_tx):
            failures.append("origin DATA TX does not drive outgoing DATA")
        if re.search(r"\bout\s+pins\b", origin_data_capture):
            failures.append("origin DATA capture contains out pins")
        if not re.search(r"\bin\s+pins\b", origin_data_capture):
            failures.append("origin DATA capture does not sample returned DATA")
        if "PIO_FIFO_JOIN_RX" not in raw_follower_init:
            failures.append("raw DATA follower must dedicate its FIFO to unload")
        if "sm_config_set_fifo_join" in process_follower_init:
            failures.append(
                "process-image DATA follower must keep independent TX/RX FIFOs")
        clock_instructions = instruction_text(clock_latch)
        if not re.search(r"^\s*push\s+noblock\b", clock_instructions,
                         re.MULTILINE | re.IGNORECASE):
            failures.append("clock evidence latch is missing its RX FIFO push")
        if re.search(r"\b(?:in|out)\s+pins\b", clock_instructions):
            failures.append("clock evidence latch must not consume or drive DATA")

    failures.extend(check_rx_endpoint_declaration(
        resources.read_text(encoding="utf-8", errors="ignore")))
    phys_units = (phys,) if isinstance(phys, Path) else tuple(phys)
    failures.extend(check_rx_endpoint_runtime(
        "\n".join(p.read_text(encoding="utf-8", errors="ignore") for p in phys_units),
        flight_io.read_text(encoding="utf-8", errors="ignore"),
    ))
    sync_board_path = board if sync_board is None else sync_board
    core_units = (sync_core,) if isinstance(sync_core, Path) else tuple(sync_core)
    failures.extend(check_sync_io_runtime(
        sync_model.read_text(encoding="utf-8", errors="ignore"),
        "\n".join(p.read_text(encoding="utf-8", errors="ignore") for p in core_units),
        sync_personas.read_text(encoding="utf-8", errors="ignore"),
        sync_board_path.read_text(encoding="utf-8", errors="ignore"),
    ))
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", type=Path,
                        default=Path("boards/rp2350_trig/inc/board_config.h"))
    parser.add_argument("--pio", type=Path,
                        default=Path("components/tdma/src/tdma_pio_spi.pio"))
    parser.add_argument("--resources", type=Path, default=DEFAULT_RESOURCES)
    parser.add_argument("--phys", type=Path, default=DEFAULT_PHYS)
    parser.add_argument("--flight-io", type=Path, default=DEFAULT_FLIGHT_IO)
    parser.add_argument("--sync-model", type=Path, default=DEFAULT_SYNC_MODEL)
    parser.add_argument("--sync-core", type=Path, default=DEFAULT_SYNC_CORE)
    parser.add_argument("--sync-personas", type=Path, default=DEFAULT_SYNC_PERSONAS)
    parser.add_argument("--sync-board", type=Path, default=None)
    args = parser.parse_args()
    failures = check(args.board, args.pio, resources=args.resources,
                     phys=args.phys, flight_io=args.flight_io,
                     sync_model=args.sync_model, sync_core=args.sync_core,
                     sync_personas=args.sync_personas, sync_board=args.sync_board)
    if failures:
        for failure in failures:
            print(f"FAIL {failure}")
        print(f"SUMMARY FAIL failures={len(failures)}")
        return 1
    print("OK   PIO/SM/DMA flight-persona resource contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
