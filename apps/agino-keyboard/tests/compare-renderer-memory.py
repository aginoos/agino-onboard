#!/usr/bin/env python3
"""Full applications, real pointer input, active/idle memory and worker retirement.

Run inside private-bus-session.sh/private-session.sh under a 2560x1600 Xvfb.
Both applications use the same standard layout, theme, input target and DPI.
All application descendants count; the common desktop/input service does not.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import time

spec=importlib.util.spec_from_file_location('lifecycle',Path(__file__).with_name('process-lifecycle.py'))
h=importlib.util.module_from_spec(spec);spec.loader.exec_module(h)

def standard_key(name):
    rows=[[('Esc',1),('`',1)]+[(x,1) for x in '1234567890-=']+[('Backspace',1)],
          [('Tab',1)]+[(x,1) for x in 'qwertyuiop[]\\']+[('Del',1)],
          [('Language',1.75)]+[(x,1) for x in "asdfghjkl;'"]+[('Enter',2.25)],
          [('Shift',2.5)]+[(x,1) for x in 'zxcvbnm,./']+[('Shift-R',2.5)]]
    status=h.status();pitch=(status['panelWidth']-24+6)/15
    for row_index,row in enumerate(rows):
        position=0
        for key,units in row:
            if key==name:
                h.click(status['panelX']+12+(position+units/2)*pitch-3,status['panelY']+56+row_index*56+24)
                return
            position+=units
    raise AssertionError('Unknown benchmark key')

def reset(method):
    h.reset_target(method)
    # Changing IM can commit an existing composition. Clear our fixture only
    # after that transition; never carry the previous cohort's preedit forward.
    epoch=h.bridge('GetSnapshot')[0]
    for release in ('false','true'):h.bridge('SendKey','uuubt',0xff1b,0,0,release,epoch)
    h.target('Sensitive','b','false');h.target('Focus');time.sleep(.15)

def snapshot(label):
    row=h.sample(label)
    for metric in ('swapPssKiB','vmSwapKiB'):
        h.require(row['totals'][metric]==0,'Swapped memory invalidates comparison')
    row['geometry']={k:h.status().get(k) for k in ('panelWidth','panelHeight','capsLock','platform','renderer')}
    return row

def run(args):
    for variable in ('XDG_CONFIG_HOME','XDG_RUNTIME_DIR','XDG_DATA_HOME','XDG_CACHE_HOME'):
        h.require(Path(os.environ[variable]).resolve().is_relative_to('/tmp'),'Use disposable session wrappers')
    h.require(bool(os.environ.get('DISPLAY')) and os.environ.get('XDG_SESSION_TYPE')=='x11','Private X11 session required')
    h.REPORT.update(scale=os.environ.get('QT_SCALE_FACTOR','1'),slintScale=os.environ.get('AGINO_KEYBOARD_SCALE','1'),binaries={},order=args.order,cycles=args.cycles,kernel=os.uname().release,platformEnv=os.environ.get('QT_QPA_PLATFORM'))
    config=Path(os.environ['XDG_CONFIG_HOME'])/'agino';config.mkdir(exist_ok=True)
    h.GTK=subprocess.Popen([str(args.gtk)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    h.wait(lambda:h.target('Focus') is not False,'owned GTK fixture')
    for cohort in args.order.split(','):
        binary=getattr(args,cohort).resolve();label=f'{cohort}-{len(h.REPORT["binaries"])}'
        (config/'agino-keyboard.conf').write_text('[General]\nmode=1\ncompact=false\nfunctionRow=false\nopacityPercent=100\ntheme=1\nkeySound=false\n')
        (config/'keyboard-slint.json').write_text(json.dumps(dict(mode=1,compact=False,function_row=False,opacity_percent=100,theme=1,key_sound=False)))
        paths=[binary]
        worker=next((p for p in (binary.parent/'agino-keyboard-ui',binary.parent.parent/'libexec/agino-keyboard-ui') if p.is_file()),None)
        if worker:paths.append(worker)
        h.REPORT['binaries'][label]={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
        h.KNOWN.clear();h.APP=subprocess.Popen([str(binary),'--hidden'],start_new_session=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        h.wait(lambda:h.status() is not None,label+' ready');time.sleep(4);snapshot(label+'-cold-hidden')
        for cycle in range(args.cycles):
            reset('keyboard-us');start=time.monotonic();h.ui('Show')
            h.wait(lambda:h.status()['visible'] and h.status()['firstFrameSerial']==h.status()['showSerial'] and h.status()['firstFrameMs']>=0,'submitted frame')
            latency=(time.monotonic()-start)*1000;time.sleep(1.5)
            row=snapshot(label+f'-plain-{cycle}');row['showWallMs']=round(latency,2);row['submittedFrameMs']=h.status()['firstFrameMs']
            h.require((h.status()['panelWidth'],h.status()['panelHeight'])==(934,344),'Mismatched keyboard geometry')
            h.ui('Hide');time.sleep(4);snapshot(label+f'-closed-{cycle}')
            reset('keyboard-us');h.ui('Show');h.wait(lambda:h.status()['visible'],'active show');time.sleep(1.5)
            for _ in range(3):
                for key in 'nihao':standard_key(key);time.sleep(.06)
            snapshot(label+f'-cycle-{cycle}-active-english');h.require(h.text_equals('nihao'*3),'Real English pointer input failed')
            time.sleep(1.5);snapshot(label+f'-cycle-{cycle}-idle-english')
            reset('pinyin')
            for key in 'nihao':standard_key(key);time.sleep(.06)
            h.wait(lambda:bool(h.bridge('GetSnapshot')[6]),'real Chinese candidates');snapshot(label+f'-cycle-{cycle}-active-candidates')
            time.sleep(1.5);snapshot(label+f'-cycle-{cycle}-idle-candidates')
            status=h.status();h.click(status['panelX']+status['panelWidth']-24,status['panelY']+24)
            h.wait(lambda:h.status()['expanded'],'expanded');time.sleep(.05);snapshot(label+f'-cycle-{cycle}-active-expanded')
            time.sleep(1.5);snapshot(label+f'-cycle-{cycle}-idle-expanded')
            expected=h.bridge('GetSnapshot')[6][0];status=h.status();h.click(status['panelX']+62,status['panelY']+24)
            h.wait(lambda:h.text_equals(expected),'Real pointer candidate commit to GTK')
            h.REPORT['checks'].append(label+f'-cycle-{cycle}: real English and Pinyin candidate pointer input')
            h.ui('Settings');h.wait(lambda:h.status()['settingsVisible'],'settings');time.sleep(.05);snapshot(label+f'-cycle-{cycle}-active-settings')
            time.sleep(1.5);snapshot(label+f'-cycle-{cycle}-idle-settings');h.ui('Hide');time.sleep(4);snapshot(label+f'-cycle-{cycle}-post-input-closed')
        h.APP.terminate();h.APP.wait(timeout=6);time.sleep(.5)
    h.REPORT['result']='pass'

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('qt','slint','gtk','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--order',default='slint,qt,qt,slint');parser.add_argument('--cycles',type=int,default=3)
    args=parser.parse_args()
    try:
        h.require(all(c in ('qt','slint') for c in args.order.split(',')),'Invalid cohort order')
        h.require(args.cycles>0,'At least one reopen cycle required')
        run(args)
    except Exception as error:
        h.REPORT['result']='fail';h.REPORT['error']=str(error)
        raise
    finally:
        if h.APP and h.APP.poll() is None:os.killpg(h.APP.pid,signal.SIGTERM);h.APP.wait(timeout=6)
        if h.GTK:h.GTK.terminate();h.GTK.wait(timeout=5)
        args.output.write_text(json.dumps(h.REPORT,ensure_ascii=False,indent=2)+'\n')
