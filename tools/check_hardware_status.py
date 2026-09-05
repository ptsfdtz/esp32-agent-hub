"""COM7 hardware status/frame capture. --controlled publishes labelled test states.
Stop the production Bridge before --controlled and restore it afterwards.
"""
import argparse, json, time
from pathlib import Path
import serial
import paho.mqtt.client as mqtt
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument('--controlled', action='store_true')
args = parser.parse_args()
out = Path('build/hardware-status'); out.mkdir(parents=True, exist_ok=True)
port = serial.Serial(); port.port='COM7'; port.baudrate=115200; port.timeout=.5
port.dtr=False; port.rts=False; port.open()

def capture(name):
    port.reset_input_buffer(); port.write(b'qf')
    state=None; frame=None; deadline=time.monotonic()+5
    while time.monotonic()<deadline and (state is None or frame is None):
        line=port.readline().decode(errors='replace').strip()
        if line.startswith('STATE '): state=dict((k,int(v)) for k,v in (p.split('=') for p in line[6:].split()))
        if line.startswith('FRAME '): frame=bytes.fromhex(line[6:])
    assert state is not None and frame is not None, 'No complete device diagnostic response'
    im=Image.new('1',(128,64))
    im.putdata([255 if frame[(y//8)*128+x] & (1<<(y%8)) else 0 for y in range(64) for x in range(128)])
    im.resize((768,384),Image.Resampling.NEAREST).save(out/f'{name}.png')
    (out/f'{name}.json').write_text(json.dumps(state,indent=2))
    print(name,state,flush=True)
    return state

try:
    if not args.controlled:
        s=capture('live')
        assert s['mqtt'] and s['online'] and s['working'] and s['usage'] and not s['idle'], s
    else:
        usage=json.loads((out/'real-usage.json').read_text(encoding='utf-8-sig'))
        client=mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,client_id='hardware-status-verification')
        client.connect('127.0.0.1',1884); client.loop_start()
        def phase(name,working,seconds,publish=True):
            end=time.monotonic()+seconds
            while time.monotonic()<end:
                if publish:
                    stamp=int(time.time())
                    client.publish('agent/codex/status',json.dumps(dict(online=True,working=working,model='HW TEST',task='Controlled verification',ts=stamp)))
                    client.publish('agent/codex/usage',json.dumps(dict(usage,ts=stamp)))
                time.sleep(1)
            return capture(name)
        try:
            s=phase('test-working',True,22)
            assert s['working'] and s['usage'] and not s['idle'], s
            s=phase('test-idle-cache',False,22)
            assert not s['working'] and s['usage'] and s['idle'], s
            s=phase('test-wake',True,3)
            assert s['working'] and s['usage'] and not s['idle'], s
            s=phase('test-heartbeat-loss',False,17,False)
            assert not s['online'] and not s['working'], s
            s=phase('test-reconnected',True,3)
            assert s['online'] and s['working'] and s['usage'] and not s['idle'], s
        finally:
            client.disconnect();client.loop_stop()
finally:
    port.close()
