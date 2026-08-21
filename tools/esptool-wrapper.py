"""Project-local wrapper for ESP-IDF's CMake esptool command."""

import runpy
import sys


if len(sys.argv) < 2:
    raise SystemExit("esptool wrapper: script path is missing")

esptool_script = sys.argv[1]
sys.argv = sys.argv[1:]
runpy.run_path(esptool_script, run_name="__main__")
