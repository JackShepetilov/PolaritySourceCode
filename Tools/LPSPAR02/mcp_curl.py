"""Same flow as Tools/mcp.sh (which the project agents used successfully), but driven from Python
because there is no bash here. Calls curl.exe and keeps every payload in files, never in a shell
variable (that is what made PowerShell eat 2 GB on the SSE stream).

    python mcp_curl.py <python_file_in_editor>
    python mcp_curl.py tools
"""
import json
import os
import subprocess
import sys
import tempfile

URL = 'http://127.0.0.1:8000/mcp'
D = os.path.join(tempfile.gettempdir(), 'mcp_py')
os.makedirs(D, exist_ok=True)
ACCEPT = 'Accept: application/json, text/event-stream'
CT = 'Content-Type: application/json'


def curl(args):
    p = subprocess.run(['curl.exe', '-s'] + args, capture_output=True)
    return p.stdout.decode('utf-8', 'replace'), p.returncode


def write(path, text):
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)


def body(method_id, method, params=None):
    o = {'jsonrpc': '2.0', 'id': method_id, 'method': method}
    if params is not None:
        o['params'] = params
    return json.dumps(o)


def main():
    hdr, _ = curl(['-D', '-', '-o', os.devnull, '-X', 'POST', URL, '-H', ACCEPT, '-H', CT,
                   '--data-binary', body(1, 'initialize', {'protocolVersion': '2024-11-05',
                                                           'capabilities': {},
                                                           'clientInfo': {'name': 'py', 'version': '1'}}),
                   '--max-time', '15'])
    sid = None
    for line in hdr.replace('\r', '').splitlines():
        if line.lower().startswith('mcp-session-id:'):
            sid = line.split(':', 1)[1].strip()
    if not sid:
        print('MCP INIT FAILED, headers were:\n%s' % hdr[:600])
        return 1
    curl(['-o', os.devnull, '-X', 'POST', URL, '-H', ACCEPT, '-H', CT, '-H', 'Mcp-Session-Id: ' + sid,
          '--data-binary', json.dumps({'jsonrpc': '2.0', 'method': 'notifications/initialized'}),
          '--max-time', '10'])

    mode = sys.argv[1] if len(sys.argv) > 1 else 'tools'
    if mode == 'tools':
        payload = body(2, 'tools/list')
    else:
        code = open(mode, encoding='utf-8').read()
        payload = body(2, 'tools/call', {'name': 'execute_python_code', 'arguments': {'code': code}})
    write(os.path.join(D, 'call.json'), payload)

    out_path = os.path.join(D, 'call.out')
    if os.path.exists(out_path):
        os.remove(out_path)
    _, rc = curl(['-o', out_path, '-X', 'POST', URL, '-H', ACCEPT, '-H', CT, '-H', 'Mcp-Session-Id: ' + sid,
                  '--data-binary', '@' + os.path.join(D, 'call.json'), '--max-time', '300'])
    raw = open(out_path, encoding='utf-8', errors='replace').read() if os.path.exists(out_path) else ''
    obj = None
    for line in raw.splitlines():
        if line.startswith('data: '):
            try:
                obj = json.loads(line[6:])
                break
            except Exception:
                pass
    if obj is None:
        try:
            obj = json.loads(raw)
        except Exception:
            print('NO RESPONSE (rc=%s). Raw head:\n%s' % (rc, raw[:1500]))
            return 1
    if 'error' in obj:
        print('RPC ERROR:', json.dumps(obj['error'])[:800])
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


if __name__ == '__main__':
    sys.exit(main())
