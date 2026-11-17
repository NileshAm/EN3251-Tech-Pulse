"""Optional legacy collector/dashboard; not used by the Home Assistant firmware path.

See README_HOME_ASSISTANT.md for normal operation. Python 3.11+.
"""
import argparse
from contextlib import contextmanager
import csv
import io
import json
import logging
import math
import os
from pathlib import Path
import re
import sqlite3
import threading
import time
import uuid
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

FIELDS = ('voltage_v', 'current_a', 'power_w', 'power_factor', 'frequency_hz')
LIMITS = ((0, 500), (0, 200), (-100000, 100000), (-1, 1), (0, 100))
STATIC = Path(__file__).parent / 'static'

def validate(data):
    if not isinstance(data, dict):
        raise ValueError('Expected object')
    for key in ('device_id', 'boot_id'):
        if not isinstance(data.get(key), str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,64}', data[key]):
            raise ValueError('Invalid ' + key)
    if type(data.get('seq')) is not int or not 0 <= data['seq'] <= 4294967295:
        raise ValueError('Invalid sequence')
    if type(data.get('simulated')) is not bool:
        raise ValueError('simulated must be boolean')
    for key, (low, high) in zip(FIELDS, LIMITS):
        value = data.get(key)
        if type(value) not in (int, float) or not math.isfinite(value) or not low <= value <= high:
            raise ValueError('Invalid ' + key)
    return data

class Store:
    def __init__(self, path):
        self.path = str(path)
        with self.connect() as db:
            db.execute('PRAGMA journal_mode=WAL')
            db.execute('CREATE TABLE IF NOT EXISTS readings (id INTEGER PRIMARY KEY, device_id TEXT, boot_id TEXT, seq INTEGER, ts REAL, voltage_v REAL, current_a REAL, power_w REAL, power_factor REAL, frequency_hz REAL, simulated INTEGER, UNIQUE(device_id,boot_id,seq))')
            db.execute('CREATE INDEX IF NOT EXISTS readings_device_time ON readings(device_id,ts)')

    @contextmanager
    def connect(self):
        db = sqlite3.connect(self.path, timeout=10)
        db.row_factory = sqlite3.Row
        try:
            with db:
                yield db
        finally:
            db.close()

    def insert(self, data, received=None):
        data = validate(data)
        with self.connect() as db:
            cursor = db.execute('INSERT OR IGNORE INTO readings(device_id,boot_id,seq,ts,voltage_v,current_a,power_w,power_factor,frequency_hz,simulated) VALUES (?,?,?,?,?,?,?,?,?,?)',
                (data['device_id'], data['boot_id'], data['seq'], time.time() if received is None else received,
                 *(data[k] for k in FIELDS), int(data['simulated'])))
            return cursor.rowcount == 1

    def devices(self):
        with self.connect() as db:
            return [dict(r) for r in db.execute('SELECT r.* FROM readings r JOIN (SELECT device_id,MAX(id) id FROM readings GROUP BY device_id) l ON r.id=l.id ORDER BY r.device_id')]

    def readings(self, device, hours):
        with self.connect() as db:
            return [dict(r) for r in db.execute('SELECT * FROM readings WHERE device_id=? AND ts>=? ORDER BY ts,id', (device,time.time()-hours*3600))]

def summarize(rows):
    energy = covered = 0.0
    for a, b in zip(rows, rows[1:]):
        gap = b['ts']-a['ts']
        # Never interpolate outages, reboots, or changes between real/demo data.
        if 0 < gap <= 10 and a['boot_id']==b['boot_id'] and a['simulated']==b['simulated']:
            energy += (max(0,a['power_w'])+max(0,b['power_w']))/2*gap/3600000
            covered += gap
    return {'energy_kwh':energy, 'covered_seconds':covered, 'samples':len(rows),
            'average_w':sum(r['power_w'] for r in rows)/len(rows) if rows else None,
            'peak_w':max((r['power_w'] for r in rows), default=None)}

def start_mqtt(store):
    import paho.mqtt.client as mqtt
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id='techpulse-server-'+uuid.uuid4().hex[:8])
    if os.getenv('MQTT_USER'):
        client.username_pw_set(os.environ['MQTT_USER'], os.getenv('MQTT_PASSWORD',''))
    if os.getenv('MQTT_TLS') == '1':
        client.tls_set(ca_certs=os.getenv('MQTT_CA') or None)
    def connected(c, userdata, flags, reason, props):
        if reason == 0:
            c.subscribe('techpulse/+/telemetry', qos=1)
            logging.info('MQTT connected')
        else:
            logging.error('MQTT connection refused: %s', reason)
    def message(c, userdata, msg):
        try:
            if msg.retain or len(msg.payload)>4096:
                return
            data = validate(json.loads(msg.payload))
            if msg.topic != 'techpulse/'+data['device_id']+'/telemetry':
                raise ValueError('Topic/device mismatch')
            store.insert(data)
        except (ValueError, TypeError, UnicodeError):
            logging.warning('Rejected invalid MQTT telemetry')
        except sqlite3.Error:
            logging.exception('Telemetry storage failed')
    client.on_connect, client.on_message = connected, message
    client.reconnect_delay_set(1,30)
    client.connect_async(os.getenv('MQTT_HOST','localhost'),int(os.getenv('MQTT_PORT','1883')),60)
    client.loop_start()
    return client

def demo(store, stop):
    boot = uuid.uuid4().hex
    seq = 0
    while not stop.is_set():
        phase=time.time()/25
        voltage=230+2*math.sin(phase/3)
        power=480+190*math.sin(phase)+80*math.sin(phase*3)
        store.insert(dict(device_id='demo-meter',boot_id=boot,seq=seq,voltage_v=voltage,
                          current_a=power/(voltage*.94),power_w=power,power_factor=.94,
                          frequency_hz=50+.03*math.sin(phase),simulated=True))
        seq+=1
        stop.wait(2)

def handler(store):
    class Handler(SimpleHTTPRequestHandler):
        def __init__(self,*args,**kwargs):
            super().__init__(*args,directory=str(STATIC),**kwargs)
        def do_GET(self):
            parsed=urlparse(self.path)
            if not parsed.path.startswith('/api/'):
                if parsed.path not in ('/','/index.html','/app.js','/style.css','/telemetry.css'):
                    self.send_error(404); return
                return super().do_GET()
            try:
                query=parse_qs(parsed.query)
                if parsed.path=='/api/devices':
                    result=store.devices()
                elif parsed.path in ('/api/readings','/api/export'):
                    hours=float(query.get('hours',['24'])[0])
                    if not math.isfinite(hours) or not 0<hours<=168:
                        raise ValueError('hours must be between 0 and 168')
                    device=query.get('device',[''])[0]
                    rows=store.readings(device,hours)
                    if parsed.path=='/api/export':
                        out=io.StringIO(); writer=csv.DictWriter(out,fieldnames=['ts','device_id',*FIELDS,'simulated'])
                        writer.writeheader(); writer.writerows({k:r[k] for k in writer.fieldnames} for r in rows)
                        self.reply(out.getvalue().encode(),'text/csv',True); return
                    # Aggregate chart points while computing statistics on full resolution.
                    step=max(1,math.ceil(len(rows)/600))
                    points=[]
                    for n in range(0,len(rows),step):
                        bucket=rows[n:n+step]; point=dict(bucket[-1])
                        for k in FIELDS: point[k]=sum(r[k] for r in bucket)/len(bucket)
                        points.append(point)
                    result={'summary':summarize(rows),'points':points,'latest':rows[-1] if rows else None}
                else:
                    self.send_error(404);return
                self.reply(json.dumps(result,allow_nan=False).encode(),'application/json')
            except (ValueError, OverflowError) as exc:
                self.send_error(400,str(exc))
        def reply(self,body,kind,download=False):
            self.send_response(200);self.send_header('Content-Type',kind+'; charset=utf-8')
            self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff')
            if download:self.send_header('Content-Disposition','attachment; filename="techpulse.csv"')
            self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    return Handler

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--demo',action='store_true')
    parser.add_argument('--host',default='127.0.0.1')
    parser.add_argument('--port',type=int,default=8000)
    parser.add_argument('--db',default=str(Path(__file__).parent/'telemetry.sqlite3'))
    args=parser.parse_args();logging.basicConfig(level=logging.INFO)
    store=Store(args.db);stop=threading.Event();client=None
    if args.demo:threading.Thread(target=demo,args=(store,stop),daemon=True).start()
    else:client=start_mqtt(store)
    server=ThreadingHTTPServer((args.host,args.port),handler(store))
    print(f'Tech Pulse: http://{args.host}:{args.port}',flush=True)
    try:server.serve_forever()
    except KeyboardInterrupt:pass
    finally:
        stop.set();server.server_close()
        if client:client.disconnect();client.loop_stop()

if __name__=='__main__':main()
