#!/usr/bin/env python3
"""Check initial placement and resizing on Hyprland; skip without a desktop."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

if (not os.environ.get('HYPRLAND_INSTANCE_SIGNATURE') or not os.environ.get('WAYLAND_DISPLAY')
        or os.environ.get('QT_QPA_PLATFORM') in ('offscreen', 'minimal') or not shutil.which('hyprctl')):
    print('SKIP: requires a real Hyprland session')
    sys.exit(77)

binary = str(Path(sys.argv[1]).resolve())

def client_for(pid):
    clients = json.loads(subprocess.check_output(['hyprctl', 'clients', '-j']))
    return next((client for client in clients if client.get('pid') == pid), None)

def wait_for(pid, predicate):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        client = client_for(pid)
        if client and predicate(client): return client
        time.sleep(.05)
    raise AssertionError(f'Chooser geometry did not settle: {client_for(pid)}')

with tempfile.TemporaryDirectory(prefix='hyprfm-chooser-') as temporary:
    root = Path(temporary)
    downloads = root / 'Downloads'
    downloads.mkdir()
    for name in ['Documents', 'Images', 'Projects']:
        (downloads / name).mkdir()
    for name in ['Lecture notes.pdf', 'Research.pdf', 'Report.pdf']:
        (downloads / name).write_text('Fixture')
    request = root / 'request.json'
    request.write_text(json.dumps({'mode': 'save', 'current_folder': str(downloads),
                                  'current_name': 'Lecture notes.pdf', 'title': 'HyprFM chooser test',
                                  'filters': [{'name': 'PDF documents', 'rules': [{'type': 0, 'pattern': '*.pdf'}]}]}))
    env = dict(os.environ, XDG_CONFIG_HOME=str(root / 'config'), XDG_CACHE_HOME=str(root / 'cache'))
    process = subprocess.Popen([binary, '--file-picker', str(request)], env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        client = wait_for(process.pid, lambda c: c['floating'] and c['size'] == [980, 640])
        screenshot = os.environ.get('HYPRFM_PICKER_SCREENSHOT')
        if screenshot and shutil.which('grim'):
            time.sleep(.2)
            x, y = client['at']
            w, h = client['size']
            subprocess.run(['grim', '-g', f'{x},{y} {w}x{h}', screenshot], check=True)
        # Use the chooser's PID, never whichever unrelated window is focused.
        selector = f"pid:{process.pid}"
        lua = f'hl.dispatch(hl.dsp.window.resize({{x=700, y=460, window="{selector}"}}))'
        resized = subprocess.run(['hyprctl', 'eval', lua], capture_output=True, text=True)
        if resized.returncode:
            subprocess.run(['hyprctl', 'dispatch', 'resizewindowpixel', f'exact 700 460,{selector}'],
                           check=True, stdout=subprocess.DEVNULL)
        wait_for(process.pid, lambda c: c['floating'] and c['size'] == [700, 460])
        print('PASS: chooser opens floating at 980x640 and can be resized to 700x460')
    finally:
        process.terminate()
        _, error = process.communicate(timeout=5)
        if error: print(error.decode(), file=sys.stderr)
