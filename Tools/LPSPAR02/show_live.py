"""Compact table from Saved/LPSP_AR02/slide_live.txt: speed, alpha, pelvis height, leg pose and the
gun-to-hand distance, which is what a broken grip shows up as."""
import io
import os
import sys

PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'Saved', 'LPSP_AR02',
                    sys.argv[1] if len(sys.argv) > 1 else 'slide_live.txt')
KEY = ('Pelvis', 'Thigh_L', 'Foot_L', 'Hand_R', 'ik_hand_gun', 'Hand_L', 'ik_hand_l', 'gun-HandR',
       'ikhandL-HandL', 'SlideAlpha', 'Crouching', 'Jumping')


def val(parts, key):
    for p in parts:
        if p.startswith(key + '=') or p.startswith(key + '('):
            return p[len(key):].lstrip('=')
    return ''


for line in io.open(PATH, encoding='utf-8'):
    line = line.strip()
    if not line:
        continue
    if 'WORLD UP' in line or 'DRIVE' in line or 'END' in line:
        print(line)
        continue
    parts = line.split()
    if len(parts) < 6:
        continue
    print('t=%-6s spd=%-5s z=%-6s m_alpha=%-5s m_dur=%-5s alpha=%-5s crouch=%-5s jump=%-5s pelZ=%-6s thighL=%-18s footL=%-18s gunHandR=%-6s ikhandLhandL=%-6s'
          % (parts[0], val(parts, 'spd'), val(parts, 'z'), val(parts, 'malpha'), val(parts, 'mdur'),
             val(parts, 'SlideAlpha'), val(parts, 'Crouching'), val(parts, 'Jumping'),
             val(parts, 'Pelvis'), val(parts, 'Thigh_L'), val(parts, 'Foot_L'),
             val(parts, 'gun-HandR'), val(parts, 'ikhandL-HandL')))
