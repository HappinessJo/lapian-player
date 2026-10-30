#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path

app = Path(sys.argv[1]).resolve()
problems = []
checked = 0
for binary in sorted(app.rglob("*")):
    if not binary.is_file() or binary.is_symlink():
        continue
    if binary.suffix != ".dylib" and binary.parent.name not in {"MacOS", "A"}:
        continue
    result = subprocess.run(["otool", "-L", str(binary)], capture_output=True, text=True)
    if result.returncode != 0:
        continue
    checked += 1
    dependencies = [line.strip().split(" ", 1)[0]
                    for line in result.stdout.splitlines()[1:] if line.strip()]
    for index, dependency in enumerate(dependencies):
        if index == 0 and Path(dependency).name == binary.name:
            continue
        if dependency.startswith(("/opt/homebrew/", "/usr/local/", "/Users/")):
            problems.append(f"{binary.relative_to(app)}: nonportable dependency")
        elif dependency.startswith("@executable_path/"):
            target = app / "Contents" / "MacOS" / dependency.removeprefix("@executable_path/")
            if not target.resolve().exists():
                problems.append(f"{binary.relative_to(app)}: missing executable dependency")
        elif dependency.startswith("@loader_path/"):
            target = binary.parent / dependency.removeprefix("@loader_path/")
            if not target.resolve().exists():
                problems.append(f"{binary.relative_to(app)}: missing loader dependency")
        elif dependency.startswith("@rpath/"):
            tail = dependency.removeprefix("@rpath/")
            targets = [app / "Contents" / "Frameworks" / tail, binary.parent / tail]
            if not any(target.resolve().exists() for target in targets):
                problems.append(f"{binary.relative_to(app)}: missing rpath dependency")
        elif not dependency.startswith(("/System/Library/", "/usr/lib/")):
            problems.append(f"{binary.relative_to(app)}: unexpected dependency")
    load_result = subprocess.run(["otool", "-l", str(binary)], capture_output=True, text=True)
    lines = load_result.stdout.splitlines()
    for index, line in enumerate(lines[:-1]):
        if line.strip() == "LC_RPATH":
            rpath = lines[index + 1].strip().removeprefix("path ").removesuffix(" (offset 12)")
            if not rpath.startswith("@"):
                problems.append(f"{binary.relative_to(app)}: absolute rpath")
print(f"Mach-O files checked: {checked}")
print(f"Dependency problems: {len(problems)}")
for problem in problems:
    print(problem)
raise SystemExit(bool(problems))
