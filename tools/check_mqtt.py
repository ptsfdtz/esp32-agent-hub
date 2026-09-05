"""Black-box TCP MQTT integration test for the Rust bridge (amqtt/paho are test-only)."""
import asyncio, json, os, queue, socket, subprocess, sys, tempfile, time
from pathlib import Path
import paho.mqtt.client as mqtt
from amqtt.broker import Broker

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)

async def broker_process(port):
    broker = Broker({"listeners":{"default":{"type":"tcp","bind":f"127.0.0.1:{port}"}},"plugins":{"amqtt.plugins.authentication.AnonymousAuthPlugin":{"allow_anonymous":True}}})
    await broker.start(); await asyncio.Event().wait()

def wait_for(predicate, seconds=12):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        value=predicate()
        if value:return value
        time.sleep(.05)
    raise AssertionError("Timed out waiting for integration condition")

class LocalBroker:
    def __init__(self,port):self.port,self.process=port,None
    def start(self):
        self.process=subprocess.Popen([sys.executable,__file__,"--broker",str(self.port)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);wait_for(self.listening)
    def listening(self):
        if self.process.poll() is not None:raise AssertionError("Test broker exited")
        try:
            with socket.create_connection(("127.0.0.1",self.port),timeout=.2):return True
        except OSError:return False
    def down(self):
        if self.process and self.process.poll() is None:self.process.terminate();self.process.wait(timeout=5)

def main():
    subprocess.run(["cargo","build","--manifest-path","bridge/Cargo.toml"],check=True)
    executable=ROOT/"bridge/target/debug"/("agentdeck-bridge.exe" if os.name=="nt" else "agentdeck-bridge")
    with socket.socket() as sock:sock.bind(("127.0.0.1",0));port=sock.getsockname()[1]
    broker=LocalBroker(port);broker.start();messages=queue.Queue()
    subscriber=mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,client_id="integration-deck")
    subscriber.on_connect=lambda client,*_:client.subscribe("#",1)
    subscriber.on_message=lambda client,userdata,msg:messages.put((msg.topic,bytes(msg.payload)))
    subscriber.reconnect_delay_set(1,2);subscriber.connect("127.0.0.1",port,5);subscriber.loop_start();wait_for(subscriber.is_connected)
    parser=subprocess.Popen([str(ROOT/"build/host"/("network.exe" if os.name=="nt" else "network")),"--stdin"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,encoding="utf-8")
    temporary=tempfile.TemporaryDirectory();directory=Path(temporary.name);marker=directory/"calls.txt";handler=directory/"handler.py"
    handler.write_text("import sys,json\nfrom pathlib import Path\nd=json.load(sys.stdin)\nwith Path(sys.argv[1]).open('a') as f:f.write(d['id']+'\\n')\n",encoding="utf-8")
    control=[str(executable),"control","--state-dir",str(directory)]
    cfg={"host":"127.0.0.1","port":port,"devices":["deck"],"interval":1,"state_dir":str(directory),"codex_usage_command":None,"handlers":{"codex":{"confirm":[sys.executable,str(handler),str(marker)],"stop":control}}}
    config=directory/"config.json";config.write_text(json.dumps(cfg),encoding="utf-8")
    stamp=int(time.time());retained={"id":"retained","device_id":"deck","action":"confirm","ts":stamp,"expires_at":stamp+10}
    subscriber.publish("agent/codex/command",json.dumps(retained),qos=1,retain=True).wait_for_publish(2)
    bridge=subprocess.Popen([str(executable),"service","--config",str(config)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);runner=None
    def message(topic,condition=lambda value:True,seconds=12):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            try:current,payload=messages.get(timeout=.2)
            except queue.Empty:continue
            if current==topic:
                value=json.loads(payload)
                if condition(value):return value
        raise AssertionError("No expected message on "+topic)
    try:
        pc=message("pc/status",lambda value:value.get("online"));parser.stdin.write("pc/status\n"+json.dumps(pc)+"\n");parser.stdin.flush();assert parser.stdout.readline().strip()=="ok";assert not marker.exists()
        subscriber.publish("agent/codex/command",b"",retain=True).wait_for_publish(2)
        runner=subprocess.Popen([str(executable),"run","--agent","codex","--task","integration process","--state-dir",str(directory),"--",sys.executable,"-c","import time;time.sleep(45)"])
        status=message("agent/codex/status",lambda value:value.get("working"));parser.stdin.write("agent/codex/status\n"+json.dumps(status)+"\n");parser.stdin.flush();assert parser.stdout.readline().strip()=="ok"
        def command(identifier,action):
            now=int(time.time());data={"id":identifier,"device_id":"deck","action":action,"ts":now,"expires_at":now+10};subscriber.publish("agent/codex/command",json.dumps(data),qos=1).wait_for_publish(2);return message("agentdeck/deck/ack",lambda value:value.get("id")==identifier)
        assert command("confirm-1","confirm")["status"]=="completed";assert command("confirm-1","confirm")["status"]=="completed";assert marker.read_text().splitlines()==["confirm-1"]
        assert command("unsupported","approve")["status"]=="rejected";broker.down();time.sleep(2);broker.start();wait_for(subscriber.is_connected,15);message("pc/status",lambda value:value.get("online"),15)
        assert command("confirm-1","confirm")["status"]=="completed";assert marker.read_text().splitlines()==["confirm-1"];assert command("stop-1","stop")["status"]=="completed";runner.wait(timeout=6);message("agent/codex/status",lambda value:not value.get("online"))
        print("PASS: Rust bridge telemetry, parser, retained rejection, ACK, dedup, reconnect, and runner stop")
    finally:
        if runner and runner.poll() is None:runner.kill();runner.wait(timeout=3)
        bridge.terminate();bridge.wait(timeout=5);parser.stdin.close();parser.terminate();parser.wait(timeout=3);parser.stdout.close();subscriber.disconnect();subscriber.loop_stop();broker.down();temporary.cleanup()

if __name__=="__main__":
    if len(sys.argv)>1 and sys.argv[1]=="--broker":asyncio.run(broker_process(int(sys.argv[2])))
    else:main()
