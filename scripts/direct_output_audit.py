"""Reject direct actuator writes outside their authoritative control owners.

The command gateway is mandatory for every application-level caller, including
the control loop and its mode handlers.  The allowlist has exactly one entry:
the gateway itself.  Adding a file requires an explicit architecture review
rather than silently creating another output authority.

Configuration and telemetry calls (for example Pump::setCalibration() and
Valves::configureFractionator()) are out of scope: they do not energize or
move an output immediately.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


AUTHORITATIVE_WRITERS = {
    "src/control/power_command_gateway.cpp",
}

DRIVER_IMPLEMENTATIONS = {
    "src/drivers/heater.cpp",
    "src/drivers/pump.cpp",
    "src/drivers/stirrer.cpp",
    "src/drivers/valves.cpp",
}

DIRECT_OUTPUT_CALL = re.compile(
    r"\b(?:"
    r"Heater::(?:setPower|setPowerWatts|setBoosterEnabled|setTriacDelay|"
    r"emergencyStop|rampTo)|"
    r"Pump::(?:start|stop|setSpeed)|"
    r"Stirrer::(?:setSpeed|start|stop)|"
    r"Valves::(?:setWater|setHeads|setBody|setTails|setUno|setProductValve|"
    r"closeProductValves|pulse|setStartStop|closeAll|setFraction|"
    r"setFractionAngle)"
    r")\s*\("
)


def strip_comments_and_literals(source: str) -> str:
    """Preserve line positions while removing comments and string literals."""
    output: list[str] = []
    index = 0
    state = "code"
    while index < len(source):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""

        if state == "code":
            if char == "/" and following == "/":
                output.extend((" ", " "))
                index += 2
                state = "line_comment"
                continue
            if char == "/" and following == "*":
                output.extend((" ", " "))
                index += 2
                state = "block_comment"
                continue
            if char == '"':
                output.append(" ")
                index += 1
                state = "string"
                continue
            if char == "'":
                output.append(" ")
                index += 1
                state = "char"
                continue
            output.append(char)
        elif state == "line_comment":
            output.append("\n" if char == "\n" else " ")
            if char == "\n":
                state = "code"
        elif state == "block_comment":
            if char == "*" and following == "/":
                output.extend((" ", " "))
                index += 2
                state = "code"
                continue
            output.append("\n" if char == "\n" else " ")
        else:
            quote = '"' if state == "string" else "'"
            if char == "\\" and following:
                output.extend((" ", "\n" if following == "\n" else " "))
                index += 2
                continue
            output.append("\n" if char == "\n" else " ")
            if char == quote:
                state = "code"
        index += 1
    return "".join(output)


def audit(project_dir: Path) -> list[str]:
    source_dir = project_dir / "src"
    violations: list[str] = []
    for path in sorted(
        candidate
        for pattern in ("*.cpp", "*.h")
        for candidate in source_dir.rglob(pattern)
    ):
        relative = path.relative_to(project_dir).as_posix()
        if relative in DRIVER_IMPLEMENTATIONS:
            continue
        sanitized = strip_comments_and_literals(path.read_text(encoding="utf-8"))
        for match in DIRECT_OUTPUT_CALL.finditer(sanitized):
            if relative in AUTHORITATIVE_WRITERS:
                continue
            line = sanitized.count("\n", 0, match.start()) + 1
            boundary = "transport/UI" if relative.startswith("src/interface/") else "non-authoritative"
            call = match.group(0).rstrip("(").strip()
            violations.append(
                f"{relative}:{line}: {call} bypasses the {boundary} output boundary"
            )
    return violations


def main(project_dir: Path) -> int:
    violations = audit(project_dir)
    if violations:
        print("[DIRECT-OUTPUT-AUDIT] failed:", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        print(
            "Route transport/UI and service actions through PowerCommandGateway; "
            "add a control owner to AUTHORITATIVE_WRITERS only after review.",
            file=sys.stderr,
        )
        return 1
    print("[DIRECT-OUTPUT-AUDIT] passed: direct output writes are authoritative")
    return 0


if __name__ == "__main__":
    sys.exit(main(Path(__file__).resolve().parents[1]))
else:
    from SCons.Script import Import

    Import("env")
    if main(Path(env["PROJECT_DIR"])):
        raise RuntimeError("Direct output authority audit failed")
