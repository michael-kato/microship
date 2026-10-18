#!/usr/bin/env python3
"""Run the Windows application in an initialized Proton prefix for E2E checks."""

import os
from pathlib import Path
import subprocess
import sys
import threading
from gpu_environment import check_gpu_environment


def windows_path(path):
    return "Z:" + str(Path(path).resolve()).replace("/", "\\")


def main():
    check_gpu_environment()
    repo = Path(__file__).resolve().parents[1]
    steam = Path(os.environ.get("STEAM_COMPAT_CLIENT_INSTALL_PATH", Path.home() / ".local/share/Steam"))
    proton = Path(os.environ.get("NANOVA_PROTON", steam / "steamapps/common/Proton 11.0/proton"))
    prefix = Path(os.environ.get("STEAM_COMPAT_DATA_PATH", repo / "build/proton11-prefix"))
    if not proton.is_file() or not (prefix / "pfx/system.reg").is_file():
        print("Proton installation or initialized prefix missing. Set NANOVA_PROTON "
              "and STEAM_COMPAT_DATA_PATH; see README.md for initialization.", file=sys.stderr)
        return 2
    if len(sys.argv) < 2:
        return 2
    arguments = [windows_path(sys.argv[1])]
    path_argument = False
    for argument in sys.argv[2:]:
        arguments.append(windows_path(argument) if path_argument else argument)
        path_argument = argument in ("--world", "--config", "--capture", "--shader", "--state", "--scene-state", "--replay", "--audio-capture", "--audio")
    environment = os.environ.copy()
    environment["STEAM_COMPAT_CLIENT_INSTALL_PATH"] = str(steam.resolve())
    environment["STEAM_COMPAT_DATA_PATH"] = str(prefix.resolve())
    environment.setdefault("PROTON_USE_XALIA", "0")
    environment["PROTON_LOG"] = "0"  # Keep application stdout/stderr observable.
    validation = os.environ.get("NANOVA_VULKAN_VALIDATION", "0")
    if validation not in ("0", "1"):
        raise RuntimeError('NANOVA_VULKAN_VALIDATION must be 0 or 1')
    if validation == "1":
        layers = repo / "build/toolchains/validation/usr/share/vulkan/explicit_layer.d"
        if not (layers / "VkLayer_khronos_validation.json").is_file():
            raise RuntimeError('Local Vulkan validation layer missing; see README.md')
        environment["VK_LAYER_PATH"] = str(layers)
        environment["VK_INSTANCE_LAYERS"] = "VK_LAYER_KHRONOS_validation"
    process = subprocess.Popen([proton, "runinprefix", *arguments], env=environment,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
    validation_seen = [False]

    def forward(pipe, stream):
        # Stream live diagnostics instead of retaining an entire game session.
        for line in pipe:
            if stream is sys.stdout and 'Vulkan validation: enabled.' in line:
                validation_seen[0] = True
            stream.write(line.replace("Z:\\", "/").replace("\\", "/"))
            stream.flush()

    readers = [threading.Thread(target=forward,args=(process.stdout,sys.stdout)),
               threading.Thread(target=forward,args=(process.stderr,sys.stderr))]
    for reader in readers: reader.start()
    returncode = process.wait()
    for reader in readers: reader.join()
    if validation == "1" and not any(flag in sys.argv[2:] for flag in ("--smoke", "--help")) and returncode == 0:
        if not validation_seen[0]:
            print('Requested validation was not reported active by the application.', file=sys.stderr)
            return 1
    return returncode


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
