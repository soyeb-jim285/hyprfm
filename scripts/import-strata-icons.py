#!/usr/bin/env python3
"""Import Strata's bundled SVGs, file associations and compatible QML icon adapters."""
import argparse
import json
from pathlib import Path
import re
import shutil
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def table(source, name):
    return source.split(f'static {name}:', 1)[1].split('= &[', 1)[1].split('\n];', 1)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('strata', type=Path)
    args = parser.parse_args()
    strata = args.strata.expanduser()
    dest = ROOT / 'src/icons/strata'
    dest.mkdir(parents=True, exist_ok=True)
    assets = (strata / 'src/assets.rs').read_text()
    entry = (strata / 'src/ui/browser/entry.rs').read_text()
    ctp = (strata / 'src/ui/browser/entry/catppuccin.rs').read_text()
    constants = dict(re.findall(r'pub const (\w+): &str = "([^"]+)";', assets + ctp))
    def pairs(body):
        return {name: constants[key] for name, key in re.findall(r'\("([^"]+)", (?:catppuccin::|icons::)?(\w+)\)', body)}
    names = pairs(table(entry, 'FILENAME_ICONS'))
    names.update(pairs(table(ctp, 'NAMES')))
    extensions = pairs(table(ctp, 'EXTENSIONS'))
    # Strata's category fallbacks supplement the upstream Catppuccin mappings.
    fallback = entry.split('    match extension {', 1)[1].split('\n    }', 1)[0]
    for suffixes, constant in re.findall(r'Some\((.*?)\)\s*=>\s*\{?\s*(?:crate::assets::icons::|icons::|catppuccin::)(\w+)', fallback, re.S):
        for suffix in re.findall(r'"([^"]+)"', suffixes):
            extensions.setdefault(suffix, constants[constant])
    patterns = [[prefix, suffix, constants[key]] for prefix, suffix, key in re.findall(
        r'\("([^"]*)", "([^"]*)", (?:catppuccin::|icons::)(\w+)\)', table(entry, 'FILENAME_AFFIX_PATTERNS'))]
    for directory, source in [('actions', 'data/icons/scalable/actions'), ('catppuccin', 'data/icons/catppuccin')]:
        (dest / directory).mkdir(exist_ok=True)
        for file in (strata / source).glob('*.svg'):
            ET.fromstring(file.read_bytes())
            shutil.copyfile(file, dest / directory / file.name)
    all_icons = {p.stem for p in dest.rglob('*.svg')}
    assert set(names.values()) | set(extensions.values()) | {p[2] for p in patterns} <= all_icons
    (dest / 'associations.json').write_text(json.dumps({'names': names, 'extensions': extensions, 'patterns': patterns}, indent=2, sort_keys=True) + '\n')
    palettes = {}
    for flavor in ('MOCHA', 'LATTE'):
        body = assets.split(f'const CATPPUCCIN_{flavor}:', 1)[1].split('\n];', 1)[0]
        palettes[flavor.lower()] = dict(re.findall(r'\("([^"]+)", "(#[0-9a-fA-F]+)"\)', body))
    (dest / 'palettes.json').write_text(json.dumps(palettes, indent=2, sort_keys=True) + '\n')
    licenses = dest / 'licenses'
    licenses.mkdir(exist_ok=True)
    shutil.copyfile(strata / 'LICENSE', licenses / 'Strata-MIT.txt')
    for license in ['Lucide-ISC.txt', 'Catppuccin-VSCode-Icons-MIT.txt']:
        shutil.copyfile(strata / 'data/licenses' / license, licenses / license)
    overrides = {
        'Grid': 'icons', 'AlignJustify': 'list', 'Columns': 'columns', 'Columns3': 'columns',
        'Home': 'house', 'Music': 'music-2', 'Settings': 'settings', 'RefreshCw': 'refresh',
        'FileText': 'file-text', 'File': 'file-text', 'Globe': 'globe', 'Image': 'image',
        'Info': 'info', 'Usb': 'hard-drive', 'HardDriveOff': 'unplug', 'Scissors': 'scissors',
        'Search': 'search', 'AlertCircle': 'triangle-alert', 'FolderPen': 'folder',
        'ChevronLeft': 'arrow-left', 'ChevronUp': 'arrow-up', 'PanelLeft': 'panel-left-symbolic',
        'SquareSplitHorizontal': 'columns', 'Undo': 'undo-2', 'Files': 'copy',
        'Volume': 'volume-2', 'Volume1': 'volume-2', 'Volume2': 'volume-2',
        'Filter': 'funnel', 'CopyPath': 'route', 'Pen': 'pencil', 'LayoutGrid': 'icons',
        'BookOpen': 'library', 'Clipboard': 'clipboard-paste', 'Bookmark': 'pin',
        'Type': 'file-type',
        'SlidersH': 'sliders-horizontal', 'VolumeHigh': 'volume-2',
    }
    adapters = ROOT / 'src/qml/strataicons'
    adapters.mkdir(exist_ok=True)
    mapped = []
    for file in sorted((ROOT / 'src/qml/icons').glob('Icon*.qml')):
        if set(re.findall(r'property \w+ (\w+)', file.read_text())) != {'size', 'color', 'strokeWidth'}:
            continue
        name = file.stem[4:]
        slug = overrides.get(name, re.sub(r'(?<!^)(?=[A-Z])', '-', name).lower())
        icon = f'strata-{slug}'
        if icon not in all_icons:
            continue
        (adapters / file.name).write_text('import QtQuick\nimport HyprFM\n\nStrataIcon { iconName: "' + icon + '" }\n')
        mapped.append(file.name)
    (adapters / 'IconLayoutList.qml').write_text('import QtQuick\nimport HyprFM\n\nStrataIcon { iconName: "strata-layout-list" }\n')
    mapped.append('IconLayoutList.qml')
    (adapters / 'IconMapping.js').write_text('var types = ' + json.dumps({Path(name).stem: True for name in mapped}, sort_keys=True) + ';\n')
    (ROOT / 'src/icons/strata-qml.cmake').write_text('# Generated by scripts/import-strata-icons.py\nset(HYPRFM_STRATA_QML_OVERRIDES\n' + ''.join(f'    {name}\n' for name in mapped) + ')\n')
    resource = ET.Element('RCC')
    group = ET.SubElement(resource, 'qresource', prefix='/HyprFM/icons/strata')
    for file in sorted(dest.rglob('*')):
        if file.is_file():
            ET.SubElement(group, 'file', alias=str(file.relative_to(dest))).text = 'strata/' + str(file.relative_to(dest))
    ET.indent(resource)
    (ROOT / 'src/icons/strata.qrc').write_text(ET.tostring(resource, encoding='unicode') + '\n')
    print(f'Imported {len(all_icons)} SVGs, {len(names)} filenames, {len(extensions)} extensions and {len(mapped)} QML adapters.')


if __name__ == '__main__':
    main()
