# Prepares Musashi's m68k_in.c for src/core/m68k/m68kops.c (see src/core/m68k/README.txt): drops what a
# plain 68000 does not have - the opcode table rows whose first CPU mode column is '.' and the handler
# bodies (M68KMAKE_OP(name, size, proc, ea)) no remaining row uses. On a 68000 those opcodes were illegal
# instructions anyway; leaving them out keeps about 270 handlers out of the program.
#   python tools/musashi_68000_only.py <Musashi>/m68k_in.c <out>/m68k_in.c
#   then in <out>: m68kmake . m68k_in.c
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
lines = open(src, 'rb').read().decode('latin-1').split('\n')
section, kept, out_head, dropped = 'head', set(), [], 0
body_start = None
for i, ln in enumerate(lines):
    s = ln.strip()
    if s.startswith('M68KMAKE_TABLE_START'):
        section = 'table'
    elif s.startswith('M68KMAKE_OPCODE_HANDLER_BODY'):
        out_head.append(ln)
        body_start = i + 1
        break
    elif section == 'table' and s and not s.startswith('/*') and not s.startswith('*'):
        f = s.split()
        if len(f) >= 11 and re.match(r'^\d+$', f[1]):
            if f[6] == '.':
                dropped += 1
                continue
            kept.add((f[0], f[1], f[2], f[3]))
    out_head.append(ln)
# bodies: each starts at a line M68KMAKE_OP(...) and runs to the next one (or the closing XXXX line)
out_body, cur, keep, dropped_bodies = [], [], True, 0
op = re.compile(r'^M68KMAKE_OP\(\s*([^,\s]+)\s*,\s*([^,\s]+)\s*,\s*([^,\s]+)\s*,\s*([^)\s]+)\s*\)')
for ln in lines[body_start:]:
    m = op.match(ln.strip())
    if m or ln.startswith('XXXXXXXX'):
        if keep:
            out_body += cur
        else:
            dropped_bodies += 1
        cur, keep = [], True
        if m:
            keep = (m.group(1), m.group(2), m.group(3), m.group(4)) in kept
    cur.append(ln)
out_body += cur
open(dst, 'wb').write('\n'.join(out_head + out_body).encode('latin-1'))
print('dropped table rows', dropped, 'handler bodies', dropped_bodies)
