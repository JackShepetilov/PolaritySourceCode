"""Bounded, fresh stdio MCP session using the project's configured bridge."""
import json
import os
import queue
import subprocess
import sys
import threading
import time

env = os.environ.copy()
env['PATH'] = r'C:\Program Files\nodejs;' + env.get('PATH', '')
p = subprocess.Popen(['cmd', '/c', 'npx', '-y', 'mcp-remote@latest',
                      'http://127.0.0.1:8000/mcp', '--transport', 'http-only'],
                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                     text=True, encoding='utf-8', env=env,
                     creationflags=subprocess.CREATE_NO_WINDOW)
lines = queue.Queue()
def read_stream(stream, is_error):
    for line in stream:
        lines.put((is_error, line))
for stream, err in [(p.stdout, False), (p.stderr, True)]:
    threading.Thread(target=read_stream, args=(stream, err), daemon=True).start()
def send(data):
    p.stdin.write(json.dumps(data) + '\n')
    p.stdin.flush()
def receive(request_id):
    deadline = time.monotonic() + 50
    while time.monotonic() < deadline:
        try:
            err, line = lines.get(timeout=1)
        except queue.Empty:
            continue
        if err:
            print(line.strip(), file=sys.stderr)
            continue
        try:
            obj = json.loads(line)
        except ValueError:
            continue
        if obj.get('id') == request_id:
            return obj
    raise TimeoutError('MCP bridge did not answer within 50 seconds')
try:
    send({'jsonrpc':'2.0','id':1,'method':'initialize','params':{
        'protocolVersion':'2024-11-05','capabilities':{},
        'clientInfo':{'name':'polarity-ar02','version':'1'}}})
    result = receive(1)
    if 'error' in result:
        raise RuntimeError(result)
    send({'jsonrpc':'2.0','method':'notifications/initialized'})
    with open(sys.argv[1], encoding='utf-8-sig') as f:
        request = json.load(f)
    send({'jsonrpc':'2.0','id':2,**request})
    print(json.dumps(receive(2), ensure_ascii=False))
finally:
    p.terminate()
