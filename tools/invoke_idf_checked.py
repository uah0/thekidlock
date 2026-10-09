"""Expose the child exit status even when EIM does not propagate it."""
import base64
import os
import shlex
import subprocess
import sys


def main():
    code = 1
    try:
        command = base64.b64decode(sys.argv[1]).decode("utf-8")
        args = shlex.split(command)
        if args[0] == "idf.py":
            args[:1] = [sys.executable, os.path.join(os.environ["IDF_PATH"], "tools", "idf.py")]
        elif args[0] == "python":
            args[0] = sys.executable
        code = subprocess.run(args, check=False).returncode
    finally:
        print(f"CODEX_IDF_EXIT_CODE={code}", flush=True)
    return code


if __name__ == "__main__":
    sys.exit(main())
