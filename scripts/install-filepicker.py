#!/usr/bin/env python3
"""Install the HyprFM chooser for this user. Restart portals after installation."""
from datetime import datetime
from pathlib import Path
import os
import re
import shutil
import sys

project = Path(__file__).resolve().parent.parent
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else project / 'build'
binaries = ['hyprfm', 'xdg-desktop-portal-hyprfm']
for binary in binaries:
    if not (build / 'src' / binary).is_file():
        raise SystemExit(f'Build {binary} first: cmake --build {build} --target hyprfm xdg-desktop-portal-hyprfm')
home = Path.home()
config = Path(os.environ.get('XDG_CONFIG_HOME', home / '.config'))
data = Path(os.environ.get('XDG_DATA_HOME', home / '.local/share'))
state = Path(os.environ.get('XDG_STATE_HOME', home / '.local/state'))
bin_dir = home / '.local/bin'
backup = state / 'hyprfm/filepicker-backups' / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
backup.mkdir(parents=True)

def preserve(path):
    if path.exists():
        destination = backup / path.relative_to(home)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)

def write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    preserve(path)
    path.write_text(content)

for binary in binaries:
    destination = bin_dir / binary
    bin_dir.mkdir(parents=True, exist_ok=True)
    preserve(destination)
    temporary = destination.with_name(binary + '.installing')
    shutil.copy2(build / 'src' / binary, temporary)
    temporary.replace(destination)  # Existing windows keep their running executable.

for theme in (project / 'themes').glob('*.toml'):
    destination = data / 'hyprfm/themes' / theme.name
    destination.parent.mkdir(parents=True, exist_ok=True)
    preserve(destination)
    shutil.copy2(theme, destination)

service_name = 'org.freedesktop.impl.portal.desktop.hyprfm'
write(data / 'xdg-desktop-portal/portals/hyprfm.portal',
      f'[portal]\nDBusName={service_name}\nInterfaces=org.freedesktop.impl.portal.FileChooser;\nUseIn=Hyprland;\n')
# D-Bus Exec uses shell-style quoting, while systemd also expands percent signs.
executable = str(bin_dir / 'xdg-desktop-portal-hyprfm')
quoted = '"' + executable.replace('\\', '\\\\').replace('"', '\\"') + '"'
write(data / f'dbus-1/services/{service_name}.service',
      f'[D-BUS Service]\nName={service_name}\nExec={quoted}\nSystemdService=xdg-desktop-portal-hyprfm.service\n')
write(config / 'systemd/user/xdg-desktop-portal-hyprfm.service',
      f'[Unit]\nDescription=HyprFM file chooser portal\n\n[Service]\nType=dbus\nBusName={service_name}\nExecStart={quoted.replace("%", "%%")}\nRestart=on-failure\n')
portal_config = config / 'xdg-desktop-portal/portals.conf'
content = portal_config.read_text() if portal_config.exists() else '[preferred]\ndefault=hyprland;gtk\n'
key = 'org.freedesktop.impl.portal.FileChooser'
line = key + '=hyprfm;gtk\n'
# Limit the change to the preferred section, keeping other portal routing intact.
match = re.search(r'(?m)^\[preferred\][ \t]*\n', content)
if not match:
    content += '\n[preferred]\n' + line
else:
    start = match.end()
    following = re.search(r'(?m)^\[', content[start:])
    end = start + following.start() if following else len(content)
    section = content[start:end]
    if re.search(r'(?m)^' + re.escape(key) + r'\s*=', section):
        section = re.sub(r'(?m)^' + re.escape(key) + r'\s*=.*(?:\n|$)', line, section)
    else:
        section = section.rstrip('\n') + '\n' + line
    content = content[:start] + section + content[end:]
write(portal_config, content)
print(f'Installed HyprFM file chooser. Previous files: {backup}')
print('Activate: systemctl --user daemon-reload && systemctl --user restart xdg-desktop-portal-hyprfm xdg-desktop-portal')
