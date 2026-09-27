"""Drive vexel.exe !vex_lsp over stdio: init, open broken+good docs, ask around."""
import json
import subprocess
import sys

EXE = r'C:\Users\kotaq\Desktop\Vexel\vexel.exe'


def send(proc, obj):
    body = json.dumps(obj).encode('utf-8')
    proc.stdin.write(b'Content-Length: %d\r\n\r\n' % len(body) + body)
    proc.stdin.flush()


def read_msg(proc):
    headers = {}
    while True:
        line = proc.stdout.readline().decode('utf-8')
        if not line or line in ('\r\n', '\n'):
            break
        k, _, v = line.partition(':')
        headers[k.strip().lower()] = v.strip()
    n = int(headers['content-length'])
    data = b''
    while len(data) < n:
        chunk = proc.stdout.read(n - len(data))
        if not chunk:
            break
        data += chunk
    return json.loads(data.decode('utf-8'))


def main():
    proc = subprocess.Popen(
        [EXE, '!vex_lsp'],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, cwd=r'C:\Users\kotaq\Desktop\Vexel')
    fails = 0

    def check(label, cond, extra=''):
        nonlocal fails
        print(('ok ' if cond else 'FAIL ') + label, extra)
        if not cond:
            fails += 1

    send(proc, {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {}})
    r = read_msg(proc)
    check('initialize', r.get('id') == 1 and 'capabilities' in r.get('result', {}), str(r)[:120])
    send(proc, {'jsonrpc': '2.0', 'method': 'initialized', 'params': {}})

    bad = '@ x : 1\n> nope + 1\n& add a b :\n  = a + b\n.\n'
    send(proc, {'jsonrpc': '2.0', 'method': 'textDocument/didOpen',
                'params': {'textDocument': {'uri': 'file:///t.vx', 'text': bad}}})
    r = read_msg(proc)
    diags = r.get('params', {}).get('diagnostics', [])
    check('diag published', r.get('method') == 'textDocument/publishDiagnostics' and len(diags) == 1, str(diags)[:200])

    send(proc, {'jsonrpc': '2.0', 'id': 2, 'method': 'textDocument/hover',
                'params': {'textDocument': {'uri': 'file:///t.vx'},
                           'position': {'line': 1, 'character': 3}}})
    r = read_msg(proc)
    check('hover got nothing useful (unknown word)', r.get('result') is None)

    send(proc, {'jsonrpc': '2.0', 'id': 3, 'method': 'textDocument/completion',
                'params': {'textDocument': {'uri': 'file:///t.vx'}, 'position': {'line': 0, 'character': 0}}})
    r = read_msg(proc)
    items = r.get('result', {}).get('items', [])
    names = [i['label'] for i in items]
    check('completion has add+builtins', 'add' in names and 'len' in names, str(names)[:200])

    send(proc, {'jsonrpc': '2.0', 'id': 4, 'method': 'textDocument/definition',
                'params': {'textDocument': {'uri': 'file:///t.vx'}, 'position': {'line': 1, 'character': 3}}})
    r = read_msg(proc)
    check('definition of unknown is null', r.get('result') is None)

    send(proc, {'jsonrpc': '2.0', 'id': 5, 'method': 'textDocument/documentSymbol',
                'params': {'textDocument': {'uri': 'file:///t.vx'}}})
    r = read_msg(proc)
    syms = [(s['name'], s['kind']) for s in r.get('result', [])]
    check('symbols x+add', ('x', 13) in syms and ('add', 12) in syms, str(syms))

    good = '@ VexGUI\n> version #\n'
    send(proc, {'jsonrpc': '2.0', 'method': 'textDocument/didOpen',
                'params': {'textDocument': {'uri': 'file:///g.vx', 'text': good}}})
    r = read_msg(proc)
    check('good file clean', r.get('params', {}).get('diagnostics', []) == [])

    send(proc, {'jsonrpc': '2.0', 'id': 6, 'method': 'textDocument/hover',
                'params': {'textDocument': {'uri': 'file:///g.vx'},
                           'position': {'line': 1, 'character': 3}}})
    r = read_msg(proc)
    val = ((r.get('result') or {}).get('contents') or {}).get('value', '')
    check('hover version verb', 'version # (0)' in val, val[:120])

    send(proc, {'jsonrpc': '2.0', 'id': 7, 'method': 'shutdown', 'params': {}})
    r = read_msg(proc)
    check('shutdown', r.get('id') == 7)
    send(proc, {'jsonrpc': '2.0', 'method': 'exit', 'params': {}})
    proc.wait(timeout=10)
    print('FAILS:', fails)
    return 1 if fails else 0


sys.exit(main())
