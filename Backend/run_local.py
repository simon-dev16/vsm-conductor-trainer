"""Run the standard-library backend with an embedded Python runtime."""

from pathlib import Path
import runpy
import sys

backend = Path(__file__).resolve().parent
sys.path.insert(0, str(backend))
runpy.run_path(str(backend / "server.py"), run_name="__main__")
