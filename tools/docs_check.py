"""Validate html docs: structure, links, nav markers."""
import glob
import os
import re
from html.parser import HTMLParser

for f in sorted(glob.glob('docs/html/*.html')):
    t = open(f, encoding='utf-8').read()
    n = len(re.findall(r'<a href="vexfs.html">VexFS</a>', t))
    m = len(re.findall(r'<a href="vexec.html">VexExec</a>', t))
    print('navpairs', f, n, m)
print('---')

VOID = {'meta', 'link', 'br', 'hr', 'img', 'input'}


class P(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.stack = []
        self.errs = []

    def handle_starttag(self, t, a):
        if t not in VOID:
            self.stack.append(t)

    def handle_endtag(self, t):
        if t in VOID:
            return
        if self.stack and self.stack[-1] == t:
            self.stack.pop()
        else:
            self.errs.append(t)


bad = 0
for f in sorted(glob.glob('docs/html/*.html')):
    p = P()
    p.feed(open(f, encoding='utf-8').read())
    if p.errs or p.stack:
        bad += 1
        print('BAD', f, p.errs, p.stack)
print('pages-bad:', bad)

errs = 0
files = sorted(glob.glob('docs/html/*.html'))
for f in files:
    t = open(f, encoding='utf-8').read()
    for m in re.finditer(r'href="([^"#]+)"', t):
        h = m.group(1)
        if h.startswith('http'):
            continue
        if not os.path.exists(os.path.join('docs/html', h)):
            print('BROKEN', f, h)
            errs += 1
print('links-broken:', errs)

for f in files:
    t = open(f, encoding='utf-8').read()
    n = len(re.findall(r'class="here"', t))
    if n != 1:
        print('NAV-HERE', f, n)
        errs += 1
print('nav-bad:', errs)
print('DONE')
