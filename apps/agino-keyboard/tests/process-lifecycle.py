#!/usr/bin/env python3
"""Resident/worker regression in an explicitly disposable X11/Fcitx session.

Run under private-bus-session.sh + private-session.sh in xvfb-run (2560x1600).
AGINO_KEYBOARD_BINARY and AGINO_GTK_TARGET override the /build fixture paths.
AGINO_MEMORY_REPORT writes structured evidence, including on failure. Optional
AGINO_HIDDEN_MAX_{RSS,USS,PSS}_KIB gates permit like-for-like SDK/host runs;
no-GUI/no-worker/no-swap gates always apply. All memory covers the entire owned
process group and descendants, including children that outlive their parent.
No host settings, host input context, process command lines or typed contents
are included in the report.
"""
import hashlib
import json
import os
import pathlib
import re
import signal
import subprocess
import sys
import time

UI = 'org.agino.KeyboardUi1'
BRIDGE = 'org.agino.Keyboard1'
TARGET = 'org.agino.KeyboardTestTarget1'
REPORT = {'schema': 1, 'samples': [], 'checks': [], 'result': 'running'}
APP = None
GTK = None
KNOWN = {}  # pid -> /proc starttime, so PID reuse cannot affect other processes
GUI_MAP = re.compile(r'libQt6(?:Gui|Qml[^/ ]*|Quick[^/ ]*|Widgets)|libdtk|/org[./]deepin[./]dtk', re.I)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def call(service, path, method, signature='', *args):
    result = subprocess.run(
        ['busctl', '--user', '--auto-start=no', '--json=short', '--', 'call',
         service, path, service, method] +
        ([signature] + list(map(str, args)) if signature else []),
        capture_output=True, text=True, timeout=5, check=True)
    return json.loads(result.stdout)['data'] if result.stdout.strip() else None


def ui(method, signature='', *args):
    return call(UI, '/Keyboard', method, signature, *args)


def status():
    return {k: v['data'] for k, v in ui('Status')[0].items()}


def target(method, signature='', *args):
    return call(TARGET, '/org/agino/TestTarget', method, signature, *args)


def bridge(method, signature='', *args):
    return call(BRIDGE, '/org/agino/Keyboard1', method, signature, *args)


def wait(predicate, description, timeout=12):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            value = predicate()
            if value:
                return value
        except subprocess.CalledProcessError:
            pass
        time.sleep(.04)
    raise AssertionError('Timed out: ' + description)


def proc_stat(pid):
    # comm can contain spaces or parentheses; fields after its closing ')' are stable.
    fields = (pathlib.Path('/proc') / str(pid) / 'stat').read_text().rsplit(')', 1)[1].split()
    return {'state': fields[0], 'ppid': int(fields[1]), 'pgrp': int(fields[2]),
            'ticks': int(fields[11]) + int(fields[12]), 'start': int(fields[19])}


def owned_processes():
    table = {}
    for path in pathlib.Path('/proc').iterdir():
        if path.name.isdigit():
            try:
                table[int(path.name)] = proc_stat(int(path.name))
            except (FileNotFoundError, ProcessLookupError):
                pass
    owned = {pid for pid, s in table.items()
             if s['pgrp'] == APP.pid or KNOWN.get(pid) == s['start']}
    owned.add(APP.pid)
    while True:
        children = {pid for pid, s in table.items() if s['ppid'] in owned}
        if children <= owned:
            break
        owned |= children
    result = {pid: table[pid] for pid in owned if pid in table and table[pid]['state'] != 'Z'}
    KNOWN.update({pid: s['start'] for pid, s in result.items()})
    return result


def sample(label):
    processes = []
    for pid, stat in sorted(owned_processes().items()):
        path = pathlib.Path('/proc') / str(pid)
        # A process disappearing during a sample is retried rather than silently omitted.
        rollup = dict((k, int(v)) for k, v in re.findall(
            r'^(\w+):\s+(\d+) kB$', (path / 'smaps_rollup').read_text(), re.M))
        stats = dict(re.findall(r'^(\w+):\s+(\d+)', (path / 'status').read_text(), re.M))
        maps = (path / 'maps').read_text().splitlines()
        gui = sorted({line.split()[-1].rsplit('/', 1)[-1]
                      for line in maps if GUI_MAP.search(line)})
        processes.append({'pid': pid, 'start': stat['start'], 'cpuTicks': stat['ticks'],
                          'rssKiB': rollup['Rss'], 'pssKiB': rollup['Pss'],
                          'ussKiB': sum(rollup.get(k, 0) for k in
                                        ('Private_Clean', 'Private_Dirty', 'Private_Hugetlb')),
                          'swapKiB': rollup.get('Swap', 0),
                          'swapPssKiB': rollup.get('SwapPss', 0),
                          'vmSwapKiB': int(stats.get('VmSwap', 0)),
                          'threads': int(stats['Threads']),
                          'fds': len(list((path / 'fd').iterdir())),
                          'maps': len(maps), 'guiLibraries': gui})
    require(any(p['pid'] == APP.pid for p in processes), 'resident broker exited')
    totals = {key: sum(p[key] for p in processes) for key in
              ('rssKiB', 'pssKiB', 'ussKiB', 'swapKiB', 'swapPssKiB', 'vmSwapKiB',
               'threads', 'fds', 'maps', 'cpuTicks')}
    result = {'label': label, 'processes': processes, 'totals': totals}
    REPORT['samples'].append(result)
    return result


def hidden(label):
    wait(lambda: not (s := status())['visible'] and not s['viewCreated'] and not s.get('workerPids', []), label + ' unload')
    # The old monolith already unloads its view, so library inspection is an
    # independent architectural regression check, even without new Status keys.
    time.sleep(.15)
    snapshot = sample(label)
    require(not any(p['guiLibraries'] for p in snapshot['processes']),
            label + ': hidden process still maps Qt GUI/QML/Quick/Widgets/DTK')
    require(len(snapshot['processes']) == 1, label + ': worker/descendant still alive')
    s = status()
    require('workerPid' in s and 'workerPids' in s, 'Status lacks worker lifecycle accounting')
    require(s['workerPid'] == 0 and not s['workerPids'], label + ': stale worker accounting')
    for key in ('swapKiB', 'swapPssKiB', 'vmSwapKiB'):
        require(snapshot['totals'][key] == 0, label + ': swapped-out memory invalidates idle result')
    for metric in ('RSS', 'USS', 'PSS'):
        ceiling = os.environ.get('AGINO_HIDDEN_MAX_' + metric + '_KIB')
        if ceiling:
            require(snapshot['totals'][metric.lower() + 'KiB'] <= int(ceiling),
                    label + ': hidden ' + metric + ' exceeds configured ceiling')
    return snapshot


def show():
    ui('Show')
    s = wait(lambda: (s if s['visible'] and s['panelLoaded'] and
                     s.get('workerPid', 0) else False) if (s := status()) else False,
             'worker visible')
    require(s['workerPid'] in owned_processes(), 'worker PID not in owned application process tree')
    require(set(s['workerPids']) == set(owned_processes()) - {APP.pid},
            'Status omitted a live worker process')
    wait(lambda: status()['firstFrameSerial'] == status()['showSerial'] and
         status()['firstFrameMs'] >= 0, 'first visible frame')
    time.sleep(.1)
    return s


def click(x, y):
    scale = float(os.environ.get('QT_SCALE_FACTOR', '1'))
    subprocess.run(['xdotool', 'mousemove', str(round(x * scale)), str(round(y * scale)),
                    'click', '1'], check=True, timeout=5)


def key(name):
    # Real pointer taps, using the public compact layout's rows/units. No test
    # key-injection endpoint can conceal broken worker-to-Fcitx input routing.
    rows = [[('Esc', 1), ('`', 1)] + [(c, 1) for c in '1234567890-='] + [('Backspace', 1)],
            [('Tab', 1)] + [(c, 1) for c in 'qwertyuiop[]\\'] + [('Del', 1)],
            [('Language', 1.75)] + [(c, 1) for c in "asdfghjkl;'"] + [('Enter', 2.25)],
            [('Shift', 2.5)] + [(c, 1) for c in 'zxcvbnm,./'] + [('Shift-R', 2.5)]]
    s = status()
    pitch = (s['panelWidth'] - 16 + 2) / 15
    for row_index, row in enumerate(rows):
        position = 0
        for label, units in row:
            if label == name:
                click(s['panelX'] + 8 + (position + units / 2) * pitch - 1,
                      s['panelY'] + 32 + 4 + row_index * 34 + 16)
                return
            position += units
    raise AssertionError('Unknown synthetic key')


def reset_target(method):
    target('Sensitive', 'b', 'false')  # clears only this fixture's own entry
    target('Focus')
    wait(lambda: bridge('GetSnapshot')[2], 'GTK editable context')
    snapshot = bridge('GetSnapshot')
    require(bridge('SetInputMethod', 'st', method, snapshot[0])[0], 'set fixture input method')
    wait(lambda: bridge('GetSnapshot')[5] == method, 'fixture input method ready')
    time.sleep(.1)


def text_equals(expected):
    # Do not include actual entry text in errors or JSON, even for this fixture.
    return target('GetText')[0] == expected


def input_roundtrip():
    reset_target('keyboard-us')
    key('a')
    wait(lambda: text_equals('a'), 'pointer key committed to GTK')
    # Two shift taps engage caps lock. Both output and retained floating geometry
    # are checked across an actual worker exit, rather than a hidden window.
    key('Shift'); key('Shift')
    key('a')
    wait(lambda: text_equals('aA'), 'caps lock engaged')
    ui('MoveBy', 'dd', 91, -137)
    before = status()
    location = (before['panelX'], before['panelY'])
    REPORT['capsBeforeRetire']={k:before.get(k) for k in ('capsLock','modifiers','editable','epoch','generation')}
    old_worker = before['workerPid']
    ui('Hide'); hidden('input-cycle-hidden')
    after = show()
    require(after['workerPid'] != old_worker, 'reopen reused worker instead of exiting')
    require((after['panelX'], after['panelY']) == location, 'floating position lost on worker exit')
    REPORT['capsAfterReopen']={k:after.get(k) for k in ('capsLock','modifiers','editable','epoch','generation')}
    time.sleep(.15); key('b')
    REPORT['capsAfterInput']={k:status().get(k) for k in ('capsLock','modifiers','editable','epoch','generation')}
    REPORT['syntheticTextLengthAfterB']=len(target('GetText')[0])
    wait(lambda: text_equals('aAB'), 'caps lock retained across worker exit')
    key('Shift'); key('Shift')
    reset_target('pinyin')
    for character in 'nihao':
        key(character)
        time.sleep(.06)
    wait(lambda: bool(bridge('GetSnapshot')[6]), 'real Pinyin candidates')
    expected = bridge('GetSnapshot')[6][0]
    s = status()
    click(s['panelX'] + 62, s['panelY'] + 16)
    wait(lambda: text_equals(expected), 'pointer candidate selected into GTK')
    REPORT['checks'].append('GTK pointer typing, caps retention, floating position and candidate commit')


def main():
    global APP, GTK
    require(os.environ.get('AGINO_TEST_ALLOW_RESTART') == '1', 'private fixture opt-in required')
    require(os.environ.get('DBUS_SESSION_BUS_ADDRESS') and os.environ.get('DISPLAY'),
            'private session bus and X11 display required')
    # The wrappers create all these outside HOME before starting their bus.
    for variable in ('XDG_CONFIG_HOME', 'XDG_RUNTIME_DIR', 'XDG_DATA_HOME', 'XDG_CACHE_HOME'):
        path = pathlib.Path(os.environ[variable]).resolve()
        require(str(path).startswith('/tmp/'), 'use private-bus-session.sh for ' + variable)
    binary = os.environ.get('AGINO_KEYBOARD_BINARY', '/build/agino-keyboard')
    REPORT['binarySha256'] = hashlib.sha256(pathlib.Path(binary).read_bytes()).hexdigest()
    REPORT['clockTicksPerSecond'] = os.sysconf('SC_CLK_TCK')
    REPORT['hiddenCeilingsKiB'] = {
        metric.lower(): int(os.environ['AGINO_HIDDEN_MAX_' + metric + '_KIB'])
        for metric in ('RSS', 'USS', 'PSS')
        if 'AGINO_HIDDEN_MAX_' + metric + '_KIB' in os.environ}
    REPORT['cycles'] = int(os.environ.get('AGINO_MEMORY_CYCLES', '5'))
    config = pathlib.Path(os.environ['XDG_CONFIG_HOME']) / 'agino/agino-keyboard.conf'
    config.parent.mkdir(exist_ok=True)
    config.write_text('[General]\nmode=1\ncompact=true\nkeySound=false\n')
    GTK = subprocess.Popen([os.environ.get('AGINO_GTK_TARGET', '/build/test-gtk-target')],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    wait(lambda: target('Focus') is not False, 'owned GTK fixture')
    APP = subprocess.Popen([binary, '--hidden'],
                           start_new_session=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    wait(lambda: status() is not None, 'resident broker')
    initial = hidden('initial-hidden')
    time.sleep(1)
    standby = sample('one-second-standby')
    REPORT['standbyCpuTicks'] = standby['totals']['cpuTicks'] - initial['totals']['cpuTicks']
    REPORT['checks'].append('cold standby has no GUI mappings, descendants or swapped memory')

    # Calls return while a cold worker is still starting; latest intent must win.
    for sequence, desired in [(('Show', 'Hide'), 'hidden'),
                              (('Show', 'Hide', 'Show'), 'visible'),
                              (('Settings', 'Show'), 'settings'),
                              (('Settings', 'ReloadPreferences', 'Show'), 'settings'),
                              (('Settings', 'Hide'), 'hidden')]:
        for method in sequence:
            ui(method)
        if desired == 'hidden':
            hidden('race-' + '-'.join(sequence))
        else:
            wait(lambda: status()['visible'] and status()['panelLoaded'] and
                 (desired != 'settings' or status()['settingsVisible']), 'startup race final state')
            if desired == 'settings':
                s = status(); click(s['panelX'] + s['panelWidth'] - 68, s['panelY'] + 16)
                wait(lambda: not status()['settingsVisible'], 'return from settings')
            ui('Hide'); hidden('race-cleanup-' + '-'.join(sequence))
    REPORT['checks'].append('cold Show/Hide/Show and Settings startup races')

    show(); sample('first-visible')
    # A small embedded PNG icon must not bring in SVG/video/raw-image codecs.
    # Check actual mappings, since a source-level format hint alone is not proof.
    extra_codecs = re.compile(r'librsvg|libheif|libavif|libjxl|libraw\.|libx265')
    for pid in owned_processes():
        maps = (pathlib.Path('/proc') / str(pid) / 'maps').read_text()
        require(not extra_codecs.search(maps), 'unneeded image codecs loaded for keyboard icon')
    REPORT['checks'].append('visible keyboard has no optional image-codec mappings')
    input_roundtrip()
    # Kill only a verified owned worker. Broker must survive and reopen freshly.
    worker = status()['workerPid']
    require(worker != APP.pid and worker in owned_processes(), 'unsafe worker crash target')
    os.kill(worker, signal.SIGKILL)
    wait(lambda: not status().get('workerPid'), 'worker crash observed')
    show(); reset_target('keyboard-us'); key('c')
    wait(lambda: text_equals('c'), 'GTK input after worker crash')
    ui('Hide'); hidden('post-crash-hidden')
    REPORT['checks'].append('worker crash recovery with real GTK input')

    for index in range(REPORT['cycles']):
        show(); ui('Hide'); hidden('repeat-hidden-' + str(index))
    final = hidden('final-hidden')
    # Modest fixed allowances cover connection caches, not repeated QML engines.
    for metric, allowance in [('ussKiB', 2048), ('pssKiB', 2048), ('fds', 4), ('threads', 1), ('maps', 16)]:
        require(final['totals'][metric] <= initial['totals'][metric] + allowance,
                'repeated reopen leaked ' + metric)
    REPORT['checks'].append('repeated full worker retirement with bounded memory/fd/thread/map drift')
    # A successfully spawned but broken GUI must fail once, without a respawn
    # storm; a later explicit Show must still recover using a working worker.
    APP.terminate(); APP.wait(timeout=5)
    import tempfile, shutil
    with tempfile.TemporaryDirectory(prefix='agino-start-failure-') as folder:
        directory=pathlib.Path(folder)
        shutil.copy2(binary,directory/'agino-keyboard')
        worker=directory/'agino-keyboard-ui'; counter=directory/'attempts'
        worker.write_text('#!/bin/sh\nprintf x >> "$AGINO_TEST_START_COUNT"\nexit 7\n')
        worker.chmod(0o755)
        env=dict(os.environ,AGINO_TEST_START_COUNT=str(counter))
        APP=subprocess.Popen([str(directory/'agino-keyboard'),'--hidden'],env=env,start_new_session=True,
                             stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        KNOWN.clear();wait(lambda:status() is not None,'startup failure broker')
        ui('Show');wait(lambda:status().get('workerError'),'startup failure reported')
        time.sleep(.4);require(counter.read_text()=='x','failed GUI respawned automatically')
        ui('Show');wait(lambda:counter.read_text()=='xx','explicit startup retry')
        wait(lambda:not status()['workerPid'],'failed retry exited')
        # Both build-tree siblings and installed bin/../libexec layouts are valid.
        prefix=pathlib.Path(binary).resolve().parent
        original=next((p for p in (prefix/'agino-keyboard-ui',prefix.parent/'libexec/agino-keyboard-ui') if p.is_file()),None)
        require(original is not None,'original UI worker unavailable for recovery')
        worker.unlink();worker.symlink_to(original.resolve())
        show();ui('Hide');hidden('startup-failure-recovered')
    REPORT['checks'].append('failed GUI startup is bounded and explicit retry recovers')



if __name__ == "__main__":
    try:
        main()
        REPORT['result'] = 'pass'
        print('PASS: resident/worker process lifecycle, aggregate memory, races and GTK input')
    except Exception as error:
        REPORT['result'] = 'fail'
        # CalledProcessError contains only a fixed synthetic command, never entry contents.
        REPORT['error'] = str(error)
        print('FAIL: ' + str(error), file=sys.stderr)
    finally:
        if APP:
            for pid, info in owned_processes().items():
                try:
                    if proc_stat(pid)['start'] == info['start']:
                        os.kill(pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
            try:
                APP.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(APP.pid, signal.SIGKILL)
                APP.wait(timeout=5)
        if GTK:
            GTK.terminate(); GTK.wait(timeout=5)
        if os.environ.get('AGINO_MEMORY_REPORT'):
            pathlib.Path(os.environ['AGINO_MEMORY_REPORT']).write_text(
                json.dumps(REPORT, ensure_ascii=False, indent=2) + '\n')
    sys.exit(0 if REPORT['result'] == 'pass' else 1)
