#!/usr/bin/env python3
"""Real nested-KWin Wayland full-app memory and pointer-input comparison.

Run with wayland-memory-session.sh. Both clients share private Fcitx/GTK/KWin;
those shared services are excluded, while every application-owned PID counts.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import signal
import re
import subprocess
import time

spec=importlib.util.spec_from_file_location('comparison',Path(__file__).with_name('compare-renderer-memory.py'))
c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
h=c.h
EXPECTED_PLATFORM='wayland'
SAMPLE_CONTEXT={}

def mapping_profile(pid):
    groups={};current=None
    for line in Path(f'/proc/{pid}/smaps').read_text().splitlines():
        header=re.match(r'^[0-9a-f]+-[0-9a-f]+\s+\S+\s+\S+\s+(\S+)\s+(\S+)\s*(.*)$',line)
        if header:
            path=header.group(3) or '[anonymous]'
            kind=('deleted-tmp-file' if re.match(r'/tmp/#\d+ \(deleted\)$',path) else 'memfd' if 'memfd:' in path else 'dev-shm' if '/dev/shm/' in path else
                  'heap' if path=='[heap]' else 'stack' if path.startswith('[stack') else
                  'anonymous' if path=='[anonymous]' or path.startswith('[anon') else
                  'library' if '.so' in path else 'special' if path.startswith('[') else 'file')
            current=groups.setdefault(path,dict(path=path,kind=kind,fileIdentities=[],mappings=0,Size=0,Rss=0,Pss=0,Private_Clean=0,Private_Dirty=0,Shared_Clean=0,Shared_Dirty=0,Anonymous=0,Swap=0))
            identity=header.group(1)+':'+header.group(2)
            if identity not in current['fileIdentities']:current['fileIdentities'].append(identity)
            current['mappings']+=1
        elif current:
            metric=re.match(r'^(\w+):\s+(\d+) kB$',line)
            if metric and metric.group(1) in current:current[metric.group(1)]+=int(metric.group(2))
    ordered=sorted(groups.values(),key=lambda item:item['Pss'],reverse=True);categories={}
    for item in ordered:
        category=categories.setdefault(item['kind'],dict(Size=0,Rss=0,Pss=0,USS=0,mappings=0))
        for metric in ('Size','Rss','Pss','mappings'):category[metric]+=item[metric]
        category['USS']+=item['Private_Clean']+item['Private_Dirty']
    return {'pid':pid,'categoriesKiB':categories,'topPssMappingsKiB':ordered[:35],
            'topRssMappingsKiB':sorted(ordered,key=lambda item:item['Rss'],reverse=True)[:35],
            'bufferMappingsKiB':[item for item in ordered if item['kind'] in ('memfd','dev-shm','deleted-tmp-file')],
            'graphicsLibrariesKiB':[item for item in ordered if re.search(r'libEGL|libGL(?:X|dispatch|\.so)|libgbm|libdrm|libvulkan|_dri\.so',item['path'])],
            'runtimeLibraries':[path for path in groups if re.search(r'libQt6|/qt6/plugins/|libEGL|libGL(?:X|dispatch|\.so)|libgbm|libdrm|libwayland|libdtk',path)]}


def protocol_summary(path):
    """Record only geometry messages from an optional WAYLAND_DEBUG trace."""
    if not path.is_file():return {}
    data=path.read_text(errors='replace')
    patterns={
        'outputScale':r'wl_output[#@]\d+\.scale(?:,\s*)?\((\d+)\)',
        'surfaceBufferScale':r'wl_surface[#@]\d+\.set_buffer_scale\((\d+)\)',
        'shmBuffers':r'wl_shm_pool[#@]\d+\.create_buffer\([^,]+,\s*\d+,\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\)',
    }
    return {key:sorted(set(re.findall(pattern,data))) for key,pattern in patterns.items()}


def host_window(width,height):
    # KWin's native X11 backend exposes NET_WM_NAME but no legacy WM_NAME;
    # xdotool's name search misses it. Enumerate actual X11 child IDs instead.
    tree=subprocess.run(['xwininfo','-root','-tree'],capture_output=True,text=True,timeout=5,check=True)
    for identifier in re.findall(r'^\s+(0x[0-9a-fA-F]+)\s',tree.stdout,re.M):
        result=subprocess.run(['xdotool','getwindowgeometry','--shell',identifier],capture_output=True,text=True,timeout=5)
        data=dict(line.split('=',1) for line in result.stdout.splitlines() if '=' in line)
        if int(data.get('WIDTH',0))==width and int(data.get('HEIGHT',0))==height:
            return {'id':int(identifier,0),'x':int(data['X']),'y':int(data['Y']),'width':width,'height':height}
    return None


def sample(label):
    row=h.sample(label)
    row.update(SAMPLE_CONTEXT)
    row['sampleMonotonicSeconds']=round(time.monotonic(),6)
    row['state']=re.sub(r'-\d+$','',re.sub(r'^(?:qt|slint)-\d+-','',label))
    for key in ('swapKiB','swapPssKiB','vmSwapKiB'):h.require(row['totals'][key]==0,'Swapped memory invalidates comparison')
    status=h.status()
    h.require(status['platform'].startswith('wayland') if EXPECTED_PLATFORM=='wayland' else status['platform'] in ('xcb','x11'),'Unexpected client platform')
    if SAMPLE_CONTEXT.get('cohort')=='qt' and row['state'] in ('closed','post-input-closed'):
        h.require(not status.get('panelLoaded'),'Native worker did not unload after hidden grace period')
    if SAMPLE_CONTEXT.get('cohort')=='qt' and status.get('panelLoaded'):
        h.require(status.get('renderer')=='qtgui-raster','Expected final native QtGui renderer')
        h.require(not any('Qt6Quick' in library or 'Qt6Qml' in library for process in row['processes'] for library in process['guiLibraries']),
                  'Unexpected QtQuick/Qml mapping in native-owned application process')
    row['geometry']={key:value for key,value in status.items() if re.search('panel|physical|screen|scale|renderer|graphics|platform',key,re.I)}
    profiles=None
    if CAPTURE_MAPS or (VERIFY_NO_ATLAS and SAMPLE_CONTEXT.get('cohort')=='qt'):
        profiles=[mapping_profile(process['pid']) for process in row['processes']]
    if CAPTURE_MAPS:row['mappingProfiles']=profiles
    if VERIFY_NO_ATLAS and SAMPLE_CONTEXT.get('cohort')=='qt':
        buffers=[dict(pid=profile['pid'],mappings=profile['bufferMappingsKiB']) for profile in profiles]
        row['bufferMappingsKiB']=buffers
        h.require(not any('wayland-cursor' in mapping['path'] for process in buffers for mapping in process['mappings']),
                  'Native cursor atlas unexpectedly loaded in uniform-scale Wayland session')
    print(label,json.dumps(row['totals']),flush=True)
    return row


def stop_app():
    if h.APP is None:return
    owned=h.owned_processes()
    for pid,info in owned.items():
        try:
            if h.proc_stat(pid)['start']==info['start']:os.kill(pid,signal.SIGTERM)
        except ProcessLookupError:pass
    try:h.APP.wait(timeout=6)
    except subprocess.TimeoutExpired:os.killpg(h.APP.pid,signal.SIGKILL);h.APP.wait(timeout=5)
    h.wait(lambda:not h.owned_processes(),'all owned application PIDs exit',timeout=6)


def run(args):
    global EXPECTED_PLATFORM,CAPTURE_MAPS,VERIFY_NO_ATLAS
    EXPECTED_PLATFORM='x11' if args.x11_baseline else 'wayland';CAPTURE_MAPS=args.capture_maps;VERIFY_NO_ATLAS=args.verify_no_cursor_atlas
    if VERIFY_NO_ATLAS:h.require(EXPECTED_PLATFORM=='wayland','Cursor atlas assertion requires real Wayland')
    for variable in ('XDG_CONFIG_HOME','XDG_RUNTIME_DIR','XDG_DATA_HOME','XDG_CACHE_HOME'):
        h.require(Path(os.environ[variable]).resolve().is_relative_to('/tmp'),'Disposable environment required')
    h.require(os.environ.get('XDG_SESSION_TYPE')==EXPECTED_PLATFORM,'Wrong private session type')
    if EXPECTED_PLATFORM=='wayland':h.require(os.environ.get('WAYLAND_DISPLAY'),'Real Wayland socket required')
    h.require(os.environ.get('DISPLAY'),'Outer Xvfb display is required for real pointer delivery')
    client_scale=float(os.environ.get('QT_SCALE_FACTOR','1'))
    h.require(client_scale==float(os.environ.get('AGINO_KEYBOARD_SCALE','1')),'Qt/Slint scale mismatch')
    scale=float(os.environ.get('AGINO_MEMORY_SCALE','1')) if EXPECTED_PLATFORM=='wayland' else client_scale
    if EXPECTED_PLATFORM=='wayland':h.require(client_scale==1,'Use compositor output scale, not client-only forcing')
    width,height=(1280,800) if scale==1 else (2560,1600)
    origin=h.wait(lambda:host_window(width,height),'visible nested KWin host window') if EXPECTED_PLATFORM=='wayland' else dict(x=0,y=0,width=2560,height=1600)
    def click(x,y):
        subprocess.run(['xdotool','mousemove',str(round(origin['x']+x*scale)),str(round(origin['y']+y*scale)),'click','1'],check=True,timeout=5)
    h.click=click
    h.REPORT.update(schema=2,session='real-private-KWin-Wayland' if EXPECTED_PLATFORM=='wayland' else 'private-SDK-X11-control',scale=scale,
                    slintScale=os.environ.get('AGINO_KEYBOARD_SCALE'),hostWindow=origin,
                    binaries={},order=args.order,kernel=os.uname().release,runtimeLabel=args.runtime_label,
                    cycles=args.cycles,verifyNoCursorAtlas=args.verify_no_cursor_atlas,expectedLogicalGeometry=[934,344],expectedPhysicalGeometry=[round(934*scale),round(344*scale)],sharedServicesExcluded=['private KWin/Xwayland (Wayland only)','private Fcitx5','owned GTK input fixture'],clientEnv={'qt':args.qt_env,'slint':args.slint_env},qtClientScaleOverride=client_scale,diagnosticPlainOnly=args.plain_only)
    config=Path(os.environ['XDG_CONFIG_HOME'])/'agino';config.mkdir(exist_ok=True)
    h.GTK=subprocess.Popen([str(args.gtk)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    h.wait(lambda:h.target('Focus') is not False,'real GTK Wayland fixture')
    for index,cohort in enumerate(args.order.split(',')):
        SAMPLE_CONTEXT.update(cohort=cohort,cohortIndex=index,cycle=-1)
        label=f'{cohort}-{index}';binary=getattr(args,cohort).resolve()
        (config/'agino-keyboard.conf').write_text('[General]\nmode=1\ncompact=false\nfunctionRow=false\nopacityPercent=100\ntheme=1\nkeySound=false\n')
        (config/'keyboard-slint.json').write_text(json.dumps(dict(mode=1,compact=False,function_row=False,opacity_percent=100,theme=1,key_sound=False)))
        binaries=[binary]
        worker=next((p for p in (binary.parent/'agino-keyboard-ui',binary.parent.parent/'libexec/agino-keyboard-ui') if p.is_file()),None)
        if worker:binaries.append(worker)
        h.REPORT['binaries'][label]={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in binaries}
        h.KNOWN.clear()
        log=(args.output.parent/f'{label}.log').open('w')
        try:
            env=dict(os.environ)
            for assignment in getattr(args,cohort+'_env'):
                key,value=assignment.split('=',1)
                h.require(key in ('QT_WAYLAND_CLIENT_BUFFER_INTEGRATION','QT_WAYLAND_DECORATION','WAYLAND_DEBUG','SLINT_SCALE_FACTOR'),'Unapproved benchmark env key')
                env[key]=value
            h.APP=subprocess.Popen([str(binary),'--hidden'],env=env,start_new_session=True,stdout=log,stderr=log)
            h.wait(lambda:h.status() is not None,label+' ready');time.sleep(4);sample(label+'-cold-hidden')
            for cycle in range(args.cycles):
                SAMPLE_CONTEXT['cycle']=cycle
                c.reset('keyboard-us');start=time.monotonic();h.ui('Show')
                h.wait(lambda:(s:=h.status())['visible'] and s['firstFrameSerial']==s['showSerial'] and s['firstFrameMs']>=0,'submitted Wayland frame')
                elapsed=(time.monotonic()-start)*1000;time.sleep(1.5)
                row=sample(label+f'-plain-{cycle}');row['showWallMs']=round(elapsed,2);row['submittedFrameMs']=h.status()['firstFrameMs']
                status=h.status()
                h.require((status['panelWidth'],status['panelHeight'])==(934,344),'Mismatched standard layout geometry')
                if cohort=='slint' and EXPECTED_PLATFORM=='wayland':
                    h.require((status['physicalWidth'],status['physicalHeight'])==(round(934*scale),round(344*scale)),
                              'Slint physical geometry does not match compositor scale')
                h.ui('Hide');time.sleep(4);sample(label+f'-closed-{cycle}')
                if args.plain_only:continue
                c.reset('keyboard-us');h.ui('Show');h.wait(lambda:h.status()['visible'],'active show');time.sleep(1.5)
                for _ in range(3):
                    for key in 'nihao':c.standard_key(key);time.sleep(.06)
                sample(label+'-active-english');h.require(h.text_equals('nihao'*3),'Real Wayland English pointer input failed')
                time.sleep(1.5);sample(label+'-idle-english')
                h.REPORT['checks'].append(label+': real pointer English committed to GTK')
                c.reset('pinyin')
                for key in 'nihao':c.standard_key(key);time.sleep(.06)
                h.wait(lambda:bool(h.bridge('GetSnapshot')[6]),'real Chinese candidates');sample(label+'-active-candidates')
                time.sleep(1.5);sample(label+'-idle-candidates')
                status=h.status();h.click(status['panelX']+status['panelWidth']-24,status['panelY']+24)
                h.wait(lambda:h.status()['expanded'],'expanded');time.sleep(.05);sample(label+'-active-expanded')
                time.sleep(1.5);sample(label+'-idle-expanded')
                expected=h.bridge('GetSnapshot')[6][0];status=h.status();h.click(status['panelX']+62,status['panelY']+24)
                h.wait(lambda:h.text_equals(expected),'real pointer candidate commit to GTK')
                h.REPORT['checks'].append(label+': real Pinyin candidates and pointer candidate selection')
                h.ui('Settings');h.wait(lambda:h.status()['settingsVisible'],'settings');time.sleep(.05);sample(label+'-active-settings')
                time.sleep(1.5);sample(label+'-idle-settings');h.ui('Hide');time.sleep(4);sample(label+'-post-input-closed')
        finally:
            stop_app();log.close()
            h.REPORT.setdefault('protocolGeometry',{})[label]=protocol_summary(args.output.parent/f'{label}.log')
    h.REPORT['result']='pass'


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('qt','slint','gtk','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--runtime-label',default='unspecified',help='For example host-shared-DDE or SDK-diagnostic')
    parser.add_argument('--order',default='slint,qt,qt,slint');parser.add_argument('--cycles',type=int,default=2)
    parser.add_argument('--capture-maps',action='store_true');parser.add_argument('--x11-baseline',action='store_true')
    parser.add_argument('--verify-no-cursor-atlas',action='store_true',help='Legacy selective-cursor assertion for 0.1.19/0.1.20 only; current versions use standard Qt cursors')
    parser.add_argument('--plain-only',action='store_true',help='Diagnostic memory/DSO capture only; skips input-parity assertions')
    parser.add_argument('--qt-env',action='append',default=[]);parser.add_argument('--slint-env',action='append',default=[])
    args=parser.parse_args();args.output.parent.mkdir(parents=True,exist_ok=True)
    try:
        h.require(all(part in ('qt','slint') for part in args.order.split(',')),'Invalid cohort order');h.require(args.cycles>0,'At least one cycle required')
        run(args)
    except Exception as error:
        h.REPORT['result']='fail';h.REPORT['error']=str(error)
        raise
    finally:
        if h.APP and h.APP.poll() is None:stop_app()
        if h.GTK:h.GTK.terminate();h.GTK.wait(timeout=5)
        args.output.write_text(json.dumps(h.REPORT,ensure_ascii=False,indent=2)+'\n')
