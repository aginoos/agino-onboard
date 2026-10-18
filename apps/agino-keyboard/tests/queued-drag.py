#!/usr/bin/env python3
"""Real queued pointer input against only an owned private GTK/keyboard fixture."""
import json,os,pathlib,signal,subprocess,time
assert os.environ.get('AGINO_TEST_ALLOW_RESTART')=='1'
def call(service,path,method,sig='',*args):
 r=subprocess.run(['busctl','--user','--auto-start=no','--json=short','--','call',service,path,service,method]+([sig]+list(map(str,args)) if sig else []),capture_output=True,text=True,check=True)
 return json.loads(r.stdout)['data'] if r.stdout.strip() else None
def ui(method,sig='',*args):return call('org.agino.KeyboardUi1','/Keyboard',method,sig,*args)
def status():return {k:v['data'] for k,v in ui('Status')[0].items()}
def wait(fn):
 until=time.monotonic()+15
 while time.monotonic()<until:
  try:
   r=fn()
   if r:return r
  except subprocess.CalledProcessError:pass
  time.sleep(.04)
 raise AssertionError('Private fixture readiness timeout')
config=pathlib.Path(os.environ['XDG_CONFIG_HOME'])/'agino/agino-keyboard.conf';config.parent.mkdir(exist_ok=True);config.write_text('[General]\nmode=1\n')
gtk=subprocess.Popen(['/build/test-gtk-target'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);app=None
try:
 wait(lambda:call('org.agino.KeyboardTestTarget1','/org/agino/TestTarget','Focus') is not False)
 app=subprocess.Popen(['/build/agino-keyboard','--hidden'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 wait(lambda:status() is not None);ui('Settings');wait(lambda:status()['settingsVisible']);time.sleep(.3)
 scale=float(os.environ.get('QT_SCALE_FACTOR','1'));initial=status();gx=round((initial['panelX']+80)*scale);gy=round((initial['panelY']+24)*scale)
 subprocess.run(['xdotool','mousemove',str(gx),str(gy),'mousedown','1'],check=True);time.sleep(.12);drag_pid=int(initial.get("workerPid",app.pid));assert drag_pid>0;os.kill(drag_pid,signal.SIGSTOP)
 try:
  args=['xdotool']
  for dx,dy in [(20,-20),(40,-30),(60,-40),(80,-50),(100,-60),(120,-80)]:args+=['mousemove',str(gx+round(dx*scale)),str(gy+round(dy*scale)),'sleep','.02']
  subprocess.run(args,check=True)
 finally:os.kill(drag_pid,signal.SIGCONT)
 try:
  time.sleep(.5);after=status();expected=(initial['panelX']+120,initial['panelY']-80)
  assert (after['panelX'],after['panelY'])==expected,{'expected':expected,'actual':(after['panelX'],after['panelY']),'scale':scale}
  subprocess.run(['xdotool','mousemove',str(gx+round(120*scale)),str(gy-round(80*scale))],check=True);time.sleep(.2);still=status();assert (still['panelX'],still['panelY'])==expected
  print('PASS: actual queued drag and stationary pointer at scale',scale,expected)
 finally:subprocess.run(['xdotool','mouseup','1'],check=True)
finally:
 for p in (app,gtk):
  if p:p.terminate();p.wait(timeout=8)
