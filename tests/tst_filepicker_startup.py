#!/usr/bin/env python3
"""The real chooser must load on headless CI, including the release Qt version."""
import json
import os
import signal
from pathlib import Path
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='hyprfm-chooser-startup-') as temporary:
    root = Path(temporary)
    request = root / 'request.json'
    request.write_text(json.dumps({'mode': 'save', 'current_folder': temporary,
                                   'current_name': 'test.txt'}))
    environment = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
                       XDG_CONFIG_HOME=str(root / 'config'), XDG_CACHE_HOME=str(root / 'cache'),
                       XDG_RUNTIME_DIR=temporary)
    process = subprocess.Popen([binary, '--file-picker', str(request)], env=environment,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
    try:
        try:
            output, error = process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            print('PASS: file chooser loaded and stayed running')
        else:
            raise AssertionError(f'Chooser exited with {process.returncode}: {error.decode()}')
    finally:
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.communicate()
