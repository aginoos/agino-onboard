#!/usr/bin/env python3
"""Real click playback and stalled-server responsiveness; no host audio access."""
import array,os,pathlib,socket,subprocess,sys,tempfile,threading,time
binary=sys.argv[1]
with tempfile.TemporaryDirectory(prefix='agino-keyboard-audio-') as folder:
 path=pathlib.Path(folder);server=path/'pulse.sock'
 env={**os.environ,'PULSE_SERVER':f'unix:{server}','CANBERRA_DRIVER':'pulse','XDG_RUNTIME_DIR':folder,'AGINO_TEST_AUDIO_STALL':'1'}
 with socket.socket(socket.AF_UNIX) as listener:
  listener.bind(str(server));listener.listen();listener.settimeout(8)
  def stall():
   conn,_=listener.accept()
   with conn:time.sleep(1.5)
  worker=threading.Thread(target=stall);worker.start()
  subprocess.run([binary,'disablingDuringUnresponsiveAudioDoesNotBlock'],env=env,check=True,timeout=12)
  worker.join(timeout=8)
 server.unlink(missing_ok=True)
 env.pop('AGINO_TEST_AUDIO_STALL');env['AGINO_TEST_AUDIO']='1'
 pulse=recorder=None
 try:
  with open(path/'pulse.log','w') as log:
   pulse=subprocess.Popen(['pulseaudio','-n','--daemonize=no','--exit-idle-time=-1',f'--load=module-native-protocol-unix socket={server} auth-anonymous=1','--load=module-null-sink sink_name=keyboard-test'],env=env,stdout=log,stderr=log)
   for _ in range(100):
    if server.exists():break
    time.sleep(.05)
   assert server.exists(),(path/'pulse.log').read_text()
   raw=path/'recording.raw'
   with open(raw,'wb') as recording:
    recorder=subprocess.Popen(['parec','--raw','--format=s16le','--rate=44100','--channels=1','--latency-msec=10','--device=keyboard-test.monitor'],env=env,stdout=recording,stderr=subprocess.PIPE)
    time.sleep(.25)
    subprocess.run([binary,'enabledPlaysBundledClick'],env=env,check=True,timeout=15)
    peak=0
    for _ in range(100):
     samples=array.array('h');data=raw.read_bytes();samples.frombytes(data[:len(data)//2*2]);peak=max((abs(s) for s in samples),default=0)
     if peak:break
     time.sleep(.05)
    assert peak>0,'No audible samples reached disposable sink'
    print(f'PASS: real keyboard click recorded, peak={peak}; no host audio connection')
 finally:
  for process in (recorder,pulse):
   if process:process.terminate();process.wait(timeout=8)
