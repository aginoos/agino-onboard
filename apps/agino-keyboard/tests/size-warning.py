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
config=pathlib.Path(os.environ['XDG_CONFIG_HOME'])/'agino/agino-keyboard.conf';config.parent.mkdir(exist_ok=True)
config.write_text('[General]\nmode=2\ncompact=true\n')
gtk=subprocess.Popen(['/build/test-gtk-target'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);app=None
try:
 wait(lambda:call('org.agino.KeyboardTestTarget1','/org/agino/TestTarget','Focus') is not False)
 app=subprocess.Popen(['/build/agino-keyboard','--hidden'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 wait(lambda:status() is not None);ui('Show');time.sleep(.1);assert not status()['viewCreated'],'Off mode must ignore Show before allocating a panel'
 config.write_text('[General]\nmode=1\ncompact=true\n');ui('ReloadPreferences');ui('Show');wait(lambda:status()['visible']);assert status()['panelWidth']==524
 config.write_text('[General]\nmode=1\ncompact=false\n');ui('ReloadPreferences');wait(lambda:not status()['visible'] and not status()['viewCreated'])
 assert app.poll() is None, 'Visible panel crashed while refusing an oversized layout'
 s=status();assert not s['visible'] and not s['viewCreated'],s
 assert s.get('workerPid',app.pid)>0,'The window process must remain alive while its warning awaits ACK'
 # The warning must already have released the heavy panel while awaiting ACK.
 subprocess.run(['xdotool','key','Return'],check=True);time.sleep(.1)
 config.write_text('[General]\nmode=1\ncompact=true\n');ui('ReloadPreferences');ui('Show');wait(lambda:status()['visible'])
 print('PASS: oversized visible layout releases panel, survives warning and reopens compact')
finally:
 for p in (app,gtk):
  if p and p.poll() is None:p.terminate();p.wait(timeout=8)
