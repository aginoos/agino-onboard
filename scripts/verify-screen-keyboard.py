#!/usr/bin/env python3
"""Run real session tests and inspect the package without certifying absent hardware."""
import argparse
import json
from pathlib import Path
import subprocess

HARDWARE_CHECKS = ('multitouch', 'pen', 'rotation', 'output_hotplug', 'sleep_resume')
def report_for(automated_passed, hardware, memory=None):
    verified = isinstance(hardware, dict) and bool(hardware.get('device')) and all(hardware.get(k) is True for k in HARDWARE_CHECKS)
    measured = isinstance(memory, dict) and memory.get('cold_boots') == 3 and memory.get('settle_seconds', 0) >= 120 and memory.get('samples_per_boot', 0) >= 60
    peaks = memory.get('peaks') if isinstance(memory, dict) else None
    measured = measured and isinstance(peaks, list) and len(peaks) == 3 and all(type(p) is int and p > 0 for p in peaks)
    memory_passed = measured and max(peaks) <= 1_000_000_000
    return {'automated_passed': bool(automated_passed),
            'hardware': {'status': 'passed' if verified else 'unverified', 'evidence': hardware},
            'memory': {'status': 'passed' if memory_passed else 'failed' if measured else 'unverified', 'evidence': memory},
            'dde_wayland_session': 'passed' if isinstance(hardware, dict) and hardware.get('dde_wayland_session') is True else 'unverified',
            'release_ready': bool(automated_passed) and verified and hardware.get('dde_wayland_session') is True and memory_passed,
            'scope': 'Preview for matching Deepin runtime; full DDE Wayland session requires separate acceptance'}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--container', required=True)
    parser.add_argument('--package', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--hardware-evidence', type=Path)
    parser.add_argument('--memory-evidence', type=Path)
    parser.add_argument('--require-hardware', action='store_true')
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    errors = []
    test = subprocess.run(['docker', 'exec', args.container, 'ctest', '--test-dir', '/build', '--output-on-failure'], capture_output=True, text=True)
    args.output.with_suffix('.tests.log').write_text(test.stdout + test.stderr)
    if test.returncode:
        errors.append('Real session CTest failed')
    package = subprocess.run(['dpkg-deb', '-c', str(args.package)], capture_output=True, text=True)
    required = ('/usr/bin/agino-keyboard', '/usr/bin/agino-keyboard-fcitx-x11', '/usr/lib/agino-keyboard/fcitx5/libxcb.so', '/etc/xdg/autostart/agino-keyboard.desktop',
                '/fcitx5/libagino-keyboard-bridge.so', '/fcitx5/libagino-waylandim.so',
                '/kwin/plugins/libagino-keyboard-device.so', '/doc/agino-keyboard/COPYING',
                '/doc/agino-keyboard/fcitx5-copyright', '/doc/agino-keyboard/kwin-copyright')
    if package.returncode or any(name not in package.stdout for name in required):
        errors.append('Package is missing required runtime/entry/license files')
    if '/uinput' in package.stdout or '/systemd/system/' in package.stdout:
        errors.append('Unexpected privileged input service in package')
    args.output.with_suffix('.package.log').write_text(package.stdout + package.stderr)
    hardware = json.loads(args.hardware_evidence.read_text()) if args.hardware_evidence else None
    memory = json.loads(args.memory_evidence.read_text()) if args.memory_evidence else None
    report = {**report_for(not errors, hardware, memory), 'errors': errors, 'package': str(args.package.resolve()), 'test_exit_code': test.returncode}
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: report[k] for k in ('automated_passed', 'hardware', 'release_ready')}, ensure_ascii=False))
    return 1 if errors or (args.require_hardware and report['hardware']['status'] != 'passed') else 0

if __name__ == '__main__':
    raise SystemExit(main())
