#!/usr/bin/env python3
"""Run under dbus-run-session: exercise the real backend without desktop changes."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
from gi.repository import Gio, GLib

backend = str(Path(sys.argv[1]).resolve())
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus',
              'RequestName', GLib.Variant('(su)', ('org.freedesktop.portal.Desktop', 0)), None, 0, 5000, None)
service = 'org.freedesktop.impl.portal.desktop.hyprfm'
interface = 'org.freedesktop.impl.portal.FileChooser'
path = '/org/freedesktop/portal/desktop'
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    scenario = root / 'scenario'
    captured = root / 'captured.json'
    fake = root / 'hyprfm'
    fake.write_text('''#!/usr/bin/env python3
import json, os, sys, time
from pathlib import Path
request = json.loads(Path(sys.argv[2]).read_text())
Path(os.environ['PICKER_CAPTURE']).write_text(json.dumps(request))
mode = Path(os.environ['PICKER_SCENARIO']).read_text()
if mode == 'cancel': sys.exit(0)
if mode == 'fail': sys.exit(2)
if mode == 'wait': time.sleep(30)
if mode == 'malformed': print('not json'); sys.exit(0)
print(json.dumps({'uris': [os.environ['PICKER_URI']], 'filter_index': 1,
                  'choices': {'encoding': 'utf8', 'readonly': 'false'}}))
''')
    fake.chmod(0o755)
    uri = (root / 'lezione è #1.pdf').as_uri()
    env = dict(os.environ, HYPRFM_PICKER_EXECUTABLE=str(fake), PICKER_CAPTURE=str(captured),
               PICKER_SCENARIO=str(scenario), PICKER_URI=uri)
    portal = subprocess.Popen([backend], env=env, stderr=subprocess.PIPE, text=True)
    try:
        for _ in range(100):
            owner = bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus',
                                   'NameHasOwner', GLib.Variant('(s)', (service,)), None, 0, 1000, None).unpack()[0]
            if owner: break
            time.sleep(.02)
        assert owner, 'backend did not register on D-Bus'
        filters = [('All files', [(0, '*')]), ('PDF', [(1, 'application/pdf')])]
        opts = {
            'current_folder': GLib.Variant('ay', os.fsencode(root) + b'\0'),
            'current_name': GLib.Variant('s', 'lezione è #1.pdf'),
            'filters': GLib.Variant('a(sa(us))', filters),
            'current_filter': GLib.Variant('(sa(us))', filters[1]),
            'choices': GLib.Variant('a(ssa(ss)s)', [('encoding', 'Encoding', [('utf8', 'UTF-8')], 'utf8'),
                                                  ('readonly', 'Read only', [], 'false')]),
        }
        count = 0
        def invoke(method, options=None):
            global count
            count += 1
            handle = path + '/request/test/r' + str(count)
            args = GLib.Variant('(osssa{sv})', (handle, 'test.app', '', 'Salva PDF', options or opts))
            return bus.call_sync(service, path, interface, method, args, GLib.VariantType.new('(ua{sv})'),
                                  Gio.DBusCallFlags.NONE, 10000, None).unpack()
        scenario.write_text('ok')
        for method, mode in [('SaveFile', 'save'), ('OpenFile', 'open'), ('SaveFiles', 'save_files')]:
            options = dict(opts)
            if method == 'SaveFiles': options['files'] = GLib.Variant('aay', [b'one.pdf\0', b'two.pdf\0'])
            response, result = invoke(method, options)
            assert response == 0, (method, response, result)
            assert result['uris'] == [uri]
            assert result['current_filter'] == filters[1]
            assert dict(result['choices'])['encoding'] == 'utf8'
            request = json.loads(captured.read_text())
            assert request['mode'] == mode
            assert request['current_folder'] == str(root)
            assert request['filter_index'] == 1
            assert request['filters'][1]['rules'][0] == {'type': 1, 'pattern': 'application/pdf'}
            assert request['choices'][0]['items'][0]['id'] == 'utf8'
            if method == 'SaveFiles': assert request['files'] == ['one.pdf', 'two.pdf']
        for mode, expected in [('cancel', 1), ('fail', 2), ('malformed', 2)]:
            scenario.write_text(mode)
            response, result = invoke('SaveFile')
            assert response == expected and result == {}, (mode, response, result)
        scenario.write_text('wait')
        captured.unlink()
        outcome = []
        worker = threading.Thread(target=lambda: outcome.append(invoke('OpenFile')))
        worker.start()
        for _ in range(100):
            if captured.exists(): break
            time.sleep(.02)
        assert captured.exists(), 'picker did not start'
        bus.call_sync(service, path + '/request/test/r' + str(count), 'org.freedesktop.impl.portal.Request',
                      'Close', None, None, 0, 3000, None)
        worker.join(5)
        assert not worker.is_alive() and outcome[0] == (1, {}), outcome
        print('PASS: OpenFile, SaveFile, SaveFiles, filters, choices, Unicode, cancellation, errors, Close')
    finally:
        portal.terminate()
        _, errors = portal.communicate(timeout=5)
        if errors: print(errors, file=sys.stderr)
