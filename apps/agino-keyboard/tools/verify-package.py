#!/usr/bin/env python3
"""Verify the installed native Qt frontend and bundled Wayland shell payload."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def output(*args):
    return subprocess.check_output(args, text=True)


def verify(package):
    dependencies = output('dpkg-deb', '-f', str(package), 'Depends')
    if re.search(r'(?:layer-shell-qt|liblayershellqt)', dependencies, re.I):
        raise ValueError('package still depends on external LayerShellQt')
    for abi in ('qt6-base-private-abi (= 6.8.0)',
                'qt6-waylandclient-private-abi (= 6.8.0)'):
        if abi not in dependencies:
            raise ValueError(f'missing private Qt ABI constraint: {abi}')
    with tempfile.TemporaryDirectory(prefix='agino-package-check-') as folder:
        subprocess.run(['dpkg-deb', '-x', str(package), folder], check=True)
        root = Path(folder)
        plugin = root / 'usr/lib/x86_64-linux-gnu/qt6/plugins/wayland-shell-integration/libagino-keyboard-shell.so'
        if not plugin.is_file():
            raise ValueError('bundled keyboard shell plugin is missing')
        broker=root / 'usr/bin/agino-keyboard'
        worker=root / 'usr/libexec/agino-keyboard-ui'
        dialog=root / 'usr/libexec/agino-keyboard-dialog'
        if not broker.is_file() or not worker.is_file():
            raise ValueError('resident controller or UI worker is missing')
        if not dialog.is_file():
            raise ValueError('on-demand message dialog helper is missing')
        if re.search(r'NEEDED.*(?:Qt6Qml|Qt6Quick|dtk6declarative)', output('readelf','-d',str(worker)),re.I):
            raise ValueError('native UI worker still links the Qt Quick frontend')
        if re.search(r'NEEDED.*(?:Qt6Gui|Qt6Widgets|Qt6Qml|Qt6Quick|dtk)', output('readelf','-d',str(broker)),re.I):
            raise ValueError('resident controller links a GUI runtime')
        if 'kglobalacceld' not in dependencies:
            raise ValueError('native shortcut service dependency is missing')
        count = 0
        for path in root.rglob('*'):
            if not path.is_file() or path.is_symlink():
                continue
            with path.open('rb') as stream:
                if stream.read(4) != b'\x7fELF':
                    continue
            count += 1
            dynamic = output('readelf', '-d', str(path))
            if re.search(r'NEEDED.*LayerShellQt', dynamic, re.I):
                raise ValueError(f'external LayerShellQt linkage: {path.relative_to(root)}')
        print(f'PASS: native Qt frontend, dialog helper, bundled shell, Qt ABI constraints, {count} ELF files without LayerShellQt dependency')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    try:
        verify(parser.parse_args().package)
    except ValueError as error:
        parser.exit(1, f'FAIL: {error}\n')
