import re, sys

# Local analysis of Saved/LPSP_AR02/flip_samples.txt (no editor needed).
PATH = r'C:\Users\Professional\Documents\Unreal Projects\Polarity_Main5_8\Saved\LPSP_AR02\flip_samples.txt'
pat = re.compile(r'([\d.]+) act_yaw=(-?[\d.]+) body=p(-?[\d.]+) y(-?[\d.]+) r(-?[\d.]+) root=p(-?[\d.]+) y(-?[\d.]+) r(-?[\d.]+) '
                 r'ikC=p(-?[\d.]+) y(-?[\d.]+) r(-?[\d.]+) ikW=p(-?[\d.]+) y(-?[\d.]+) r(-?[\d.]+) '
                 r'wep=p(-?[\d.]+) y(-?[\d.]+) r(-?[\d.]+) shots=(\d+) recl=(-?[\d.]+),(-?[\d.]+),(-?[\d.]+)')
rows = []
for line in open(PATH, encoding='utf-8'):
    m = pat.search(line)
    if m:
        g = m.groups()
        rows.append(dict(t=float(g[0]), act=float(g[1]), body=float(g[3]), root=float(g[6]),
                         ikCy=float(g[9]), ikWy=float(g[12]), wepy=float(g[15]), shots=int(g[17]),
                         recl=(float(g[18]), float(g[19]), float(g[20]))))
print('parsed rows:', len(rows), 'from', len(open(PATH, encoding="utf-8").readlines()), 'lines')

print('root yaw != 0:', sum(1 for r in rows if abs(r['root']) > 0.5))
worst = sorted(((abs(rows[i]['wepy'] - rows[i - 1]['wepy']), rows[i - 1]['t'], rows[i]['t'],
                 rows[i - 1]['wepy'], rows[i]['wepy']) for i in range(1, len(rows))), reverse=True)[:6]
print('biggest wep yaw jumps (deg):')
for w in worst:
    print('   d%.0f  t %.1f->%.1f  %.0f -> %.0f' % w)
worst = sorted(((abs(rows[i]['ikCy'] - rows[i - 1]['ikCy']), rows[i - 1]['t'], rows[i]['t'],
                 rows[i - 1]['ikCy'], rows[i]['ikCy']) for i in range(1, len(rows))), reverse=True)[:6]
print('biggest ik_hand_gun component yaw jumps (deg):')
for w in worst:
    print('   d%.0f  t %.1f->%.1f  %.0f -> %.0f' % w)
mism = [(r['t'], round(r['wepy'] - r['ikWy'], 1)) for r in rows if abs(r['wepy'] - r['ikWy']) > 3]
print('frames where weapon mesh world yaw != ik_hand_gun world yaw (>3 deg):', len(mism), mism[:8])
print('recoil magnitude max:', max(max(abs(x) for x in r['recl']) for r in rows))
print('max shots seen:', max(r['shots'] for r in rows))
print('body yaw range: %.0f .. %.0f ; root yaw range: %.0f .. %.0f' % (
    min(r['body'] for r in rows), max(r['body'] for r in rows),
    min(r['root'] for r in rows), max(r['root'] for r in rows)))
