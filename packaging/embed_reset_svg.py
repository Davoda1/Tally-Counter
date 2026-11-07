#!/usr/bin/env python3
"""Compile the bundled reset SVG to native vector commands without a runtime dependency."""
from pathlib import Path
import hashlib
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/reset.svg"
OUTPUT = ROOT / "src/reset_svg.hpp"


def commands_for(data):
    pattern = r"[A-Za-z]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?"
    if re.sub(pattern, "", data).strip(" ,\t\r\n"):
        raise ValueError("Invalid SVG path data")
    tokens = re.findall(pattern, data)
    result = []
    index = 0
    command = None
    current = (0.0, 0.0)
    start = current
    last_control = None
    previous = None
    sizes = {"M": 2, "L": 2, "H": 1, "V": 1, "C": 6, "S": 4}
    while index < len(tokens):
        if tokens[index].isalpha():
            command = tokens[index]
            index += 1
        if command is None:
            raise ValueError("Path coordinates need a command")
        kind = command.upper()
        relative = command.islower()
        if kind == "Z":
            result.append(("Close",))
            current = start
            previous = kind
            last_control = None
            command = None
            continue
        if kind not in sizes:
            raise ValueError("Unsupported reset SVG command: " + command)
        count = sizes[kind]
        values = tokens[index:index + count]
        if len(values) != count or any(value.isalpha() for value in values):
            raise ValueError("Incomplete reset SVG command")
        numbers = list(map(float, values))
        index += count
        x, y = current

        def point(a, b):
            return (a + x, b + y) if relative else (a, b)

        if kind in ("M", "L"):
            current = point(*numbers)
            result.append(("Move" if kind == "M" else "Line", *current))
            if kind == "M":
                start = current
                command = "l" if relative else "L"
            last_control = None
        elif kind == "H":
            current = (numbers[0] + x if relative else numbers[0], y)
            result.append(("Line", *current))
            last_control = None
        elif kind == "V":
            current = (x, numbers[0] + y if relative else numbers[0])
            result.append(("Line", *current))
            last_control = None
        elif kind == "C":
            control1 = point(*numbers[:2])
            control2 = point(*numbers[2:4])
            current = point(*numbers[4:6])
            result.append(("Curve", *control1, *control2, *current))
            last_control = control2
        elif kind == "S":
            control1 = (2*x-last_control[0], 2*y-last_control[1]) if previous in ("C", "S") else (x, y)
            control2 = point(*numbers[:2])
            current = point(*numbers[2:4])
            result.append(("Curve", *control1, *control2, *current))
            last_control = control2
        previous = kind
    if result and result[-1][0] != "Close":
        result.append(("Close",))
    return result


def main():
    contents = SOURCE.read_bytes()
    svg = ET.fromstring(contents)
    viewbox = list(map(float, svg.attrib["viewBox"].split()))
    if len(viewbox) != 4 or viewbox[:2] != [0, 0] or min(viewbox[2:]) <= 0:
        raise ValueError("Expected a positive viewBox starting at 0,0")
    paths = svg.findall("{http://www.w3.org/2000/svg}path")
    if len(paths) != 1 or "transform" in svg.attrib or "transform" in paths[0].attrib:
        raise ValueError("Expected one untransformed reset path")
    commands = commands_for(paths[0].attrib["d"])
    lines = [
        "// Generated from assets/reset.svg by packaging/embed_reset_svg.py.",
        "// Reset path: Copyright (c) 2015 Instructure, Inc. (MIT).",
        "// See licenses/INSTRUCTURE-UI-MIT.txt.",
        "// SVG SHA256: " + hashlib.sha256(contents).hexdigest(),
        "#pragma once", '#include "paint.hpp"', "#include <array>",
        "namespace tally::reset_svg {",
        f"constexpr double width={viewbox[2]:.12g},height={viewbox[3]:.12g};",
        f"constexpr std::array<PathCommand,{len(commands)}> commands={{{{",
    ]
    for kind, *coordinates in commands:
        values = ",".join(f"{number:.12g}" for number in coordinates)
        lines.append("    {PathVerb::" + kind + ("," + values if values else "") + "},")
    lines += ["}};", "}", ""]
    OUTPUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"Embedded {len(commands)} native SVG path commands")


if __name__ == "__main__":
    main()
