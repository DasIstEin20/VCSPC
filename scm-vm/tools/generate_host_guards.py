#!/usr/bin/env python3
"""Generate typed, fail-closed callbacks for the pinned reVCS host contract."""

import argparse
import hashlib
from pathlib import Path
import re


HOST_SHA256 = "baea4470747017ddee9218b8a950e175710d70a694a9b21f3d8212b43dcbdb45"
CALLBACK = re.compile(r"^\s*(.*?)\s*\(\*(\w+)\)\((.*?)\);\s*$", re.MULTILINE)
# This observer selects debug objectives and never owns gameplay or cleanup.
OPTIONAL_CALLBACKS = {"missionMarkerObserved"}


def generate(header: Path) -> str:
    content = header.read_bytes()
    if hashlib.sha256(content).hexdigest() != HOST_SHA256:
        raise ValueError("ReVcsScriptVm.h differs from the reviewed host ABI")
    body = content.decode("utf-8").split("struct ReVcsScriptHost {", 1)[1].split("};", 1)[0]
    callbacks = CALLBACK.findall(body)
    if len(callbacks) != 584:
        raise ValueError("The reviewed host contract must contain exactly 584 callbacks")

    lines = ["// Generated from the pinned ReVcsScriptVm.h. Do not edit.",
             "static ReVcsScriptHost engineHost;", ""]
    for result, name, parameters in callbacks:
        # The reviewed header uses named scalar/pointer parameters exclusively.
        names = []
        for parameter in parameters.split(","):
            parameter = parameter.strip()
            if parameter == "void":
                continue
            match = re.fullmatch(r"(.*?)\b[A-Za-z_]\w*", parameter)
            if not match or not match.group(1).strip():
                raise ValueError("Unsupported callback parameter in " + name)
            names.append(parameter[len(match.group(1)):].strip())
        lines += [
            f"static {result.strip()} Guarded_{name}({parameters if parameters != 'void' else ''}) {{",
            "    CheckHostError();",
            f'    if (!engineHost.{name}) throw MissingHostCallback("{name}");',
        ]
        invocation = f"engineHost.{name}({', '.join(names)})"
        if result.strip() == "void":
            lines += [f"    {invocation};", "    CheckHostError();"]
        else:
            lines += [f"    {result.strip()} callbackResult = {invocation};",
                      "    CheckHostError();", "    return callbackResult;"]
        lines += ["}"]

    lines += ["", "static void InstallMissingHostGuards(ReVcsScriptHost& host) {"]
    lines += ["    engineHost = host;"]
    for _, name, _ in callbacks:
        prefix = f"if (host.{name}) " if name in OPTIONAL_CALLBACKS else ""
        lines += [f"    {prefix}host.{name} = &Guarded_{name};"]
    lines += ["}", ""]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--header", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = generate(args.header)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as output:
        output.write(result)


if __name__ == "__main__":
    main()
