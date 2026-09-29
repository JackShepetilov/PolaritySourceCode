"""Talk to the editor's MCP server from Python (there is no bash on this machine, so Tools/mcp.sh
cannot be used). Same protocol it uses: initialize, notifications/initialized, tools/call.

    python mcp_client.py <python_file_in_editor>      # runs execute_python_code
    python mcp_client.py tool <name> <args.json>      # any MCP tool
    python mcp_client.py tools                        # list tool names

The server answers as SSE and holds the stream open, so reads are bounded by a deadline and a
socket timeout; whatever arrived is parsed.
"""
import json
import socket
import sys
import time
import urllib.request

URL = 'http://127.0.0.1:8000/mcp'
HEADERS = {'Accept': 'application/json, text/event-stream', 'Content-Type': 'application/json'}
ARGS = [a for a in sys.argv[1:] if not a.startswith('--')]
DEBUG = '--debug' in sys.argv
DEADLINE = 240.0
for a in sys.argv[1:]:
    if a.startswith('--timeout='):
        DEADLINE = float(a.split('=', 1)[1])


def post(payload, sid=None, timeout=30):
    data = json.dumps(payload).encode('utf-8')
    req = urllib.request.Request(URL, data=data, method='POST')
    for k, v in HEADERS.items():
        req.add_header(k, v)
    if sid:
        req.add_header('Mcp-Session-Id', sid)
    return urllib.request.urlopen(req, timeout=timeout)


def parse_sse(buf):
    txt = buf.decode('utf-8', 'replace')
    for line in txt.splitlines():
        if line.startswith('data: '):
            try:
                return json.loads(line[6:])
            except Exception:
                return None
    try:
        return json.loads(txt)
    except Exception:
        return None


def read_to_object(resp, deadline):
    buf = b''
    t0 = time.time()
    while time.time() < deadline:
        try:
            chunk = resp.read(65536)
        except (socket.timeout, TimeoutError):
            if DEBUG:
                print('[dbg] read timed out after %.1fs, %d bytes' % (time.time() - t0, len(buf)))
            break
        if not chunk:
            if DEBUG:
                print('[dbg] stream closed after %.1fs, %d bytes' % (time.time() - t0, len(buf)))
            break
        buf += chunk
        obj = parse_sse(buf)
        if obj is not None:
            return obj
    if DEBUG:
        print('[dbg] raw %r' % buf[:600])
    return parse_sse(buf)


def finish(obj):
    if obj is None:
        print('NO RESPONSE')
        return 1
    if 'error' in obj:
        print('RPC ERROR:', json.dumps(obj['error'])[:1000])
        return 1
    res = obj.get('result', {})
    if 'tools' in res:
        for t in res['tools']:
            print(t['name'])
        return 0
    for c in res.get('content', []):
        t = c.get('text', '')
        try:
            inner = json.loads(t)
            if isinstance(inner, dict) and 'output' in inner:
                print(inner.get('output', ''))
                if not inner.get('success', True):
                    print('SUCCESS=False', inner.get('error', ''))
                continue
        except Exception:
            pass
        print(t)
    return 0


def main():
    init = {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize',
            'params': {'protocolVersion': '2024-11-05', 'capabilities': {}, 'clientInfo': {'name': 'py', 'version': '1'}}}
    r = post(init, timeout=20)
    sid = r.headers.get('Mcp-Session-Id')
    try:
        r.read(1)
    except Exception:
        pass
    if not sid:
        print('MCP INIT FAILED on port 8000 - is the editor open?')
        return 1
    post({'jsonrpc': '2.0', 'method': 'notifications/initialized'}, sid=sid, timeout=20).read(1)

    mode = ARGS[0] if ARGS else 'tools'
    if mode == 'tools':
        call = {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'}
    elif mode == 'tool':
        args = json.load(open(ARGS[2], encoding='utf-8')) if len(ARGS) > 2 else {}
        call = {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/call',
                'params': {'name': ARGS[1], 'arguments': args}}
    else:
        code = open(mode, encoding='utf-8').read()
        call = {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/call',
                'params': {'name': 'execute_python_code', 'arguments': {'code': code}}}
    resp = post(call, sid=sid, timeout=90)
    if DEBUG:
        print('[dbg] status %s headers %s' % (resp.status, dict(resp.headers)))
    return finish(read_to_object(resp, time.time() + DEADLINE))


if __name__ == '__main__':
    sys.exit(main())
