lines = open('vex/lex.vx', encoding='utf-8').read().split('\n')
depth = 0
for idx, raw in enumerate(lines, 1):
    ln = raw
    # strip comment (respect strings)
    code = []
    instr = False
    i = 0
    while i < len(ln):
        c = ln[i]
        if instr:
            if c == '\\' and i + 1 < len(ln):
                code.append('x')
                code.append('x')
                i += 2
                continue
            if c == '"':
                instr = False
            code.append('x')
        else:
            if c == '"':
                instr = True
                code.append('x')
            elif c == ';':
                break
            else:
                code.append(c)
        i += 1
    code = ''.join(code)
    s = code.strip()
    if not s:
        continue
    first = s[0]
    opens = s.endswith(':') and first in '?&*'
    closes = (s == '.')
    sep = (s == '!')
    if closes:
        depth -= 1
    tag = ''
    if opens:
        tag = 'OPEN'
        depth += 1
    elif closes:
        tag = 'CLOSE'
    elif sep:
        tag = 'SEP'
    if tag or depth < 0:
        print(idx, repr(s[:40]), tag, 'depth->', depth)
print('final depth:', depth)
