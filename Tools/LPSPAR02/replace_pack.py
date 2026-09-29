"""Replace our Low Poly Shooter Pack copy with the fresh one, keeping the project's own edits.

Run from the project root with the EDITOR CLOSED:  python Source/Tools/LPSPAR02/replace_pack.py [--go]
Without --go it only prints what it would do.

What it does:
  1. Backs up Content/InfimaGames/{AnimatedLowPolyWeapons,LowPolyShooterPack,Utilities} to
     Saved/LPSP_AR02/InfimaGames_backup_<stamp>/ (plain copy, restore = copy back).
  2. Copies every file of the fresh pack over ours, EXCEPT:
     - every SK_* / SKEL_* file (skeletons and meshes carry our sockets and virtual bones);
     - files we edited by hand (Saved/LPSP_AR02/pack_hand_edits.txt), except the retarget-damaged
       walk animations listed in REPLACE_ANYWAY, which is exactly what the fresh copy is for.
  3. Never deletes anything: files that exist only in our copy (34 project additions) stay.
"""
import os
import shutil
import sys
import time

OUR = 'Content/InfimaGames'
NEW = 'C:/Users/Professional/Documents/assets/LowPolyShooterPack/Content/InfimaGames'
PACK = ['AnimatedLowPolyWeapons', 'LowPolyShooterPack', 'Utilities']
HAND = 'Saved/LPSP_AR02/pack_hand_edits.txt'
_WALK = 'AnimatedLowPolyWeapons/Art/Characters/Animations/_Common/'
REPLACE_ANYWAY = {_WALK + n + '.uasset' for n in
                  ['A_FP_PCH_Walk_F', 'A_FP_PCH_Walk_B', 'A_FP_PCH_Walk_L', 'A_FP_PCH_Walk_R', 'BS_FP_PCH_Walking']}


def main():
    go = '--go' in sys.argv
    hand = set()
    for line in open(HAND, encoding='utf-8'):
        parts = line.split()
        if len(parts) == 2:
            hand.add(parts[1])
    keep, copy = [], []
    for top in PACK:
        for dp, _, fs in os.walk(os.path.join(NEW, top)):
            for f in fs:
                src = os.path.join(dp, f)
                rel = os.path.relpath(src, NEW).replace(os.sep, '/')
                dst = os.path.join(OUR, rel)
                ours_exists = os.path.exists(dst)
                if ours_exists and rel not in REPLACE_ANYWAY and (
                        f.startswith('SK_') or f.startswith('SKEL_') or rel in hand):
                    keep.append(rel)
                else:
                    copy.append((src, dst, rel))
    print('keep ours:', len(keep))
    for k in keep:
        print('  KEEP', k)
    print('copy from fresh pack:', len(copy))
    if not go:
        print('dry run; add --go to do it')
        return
    stamp = time.strftime('%Y%m%d_%H%M')
    bak = 'Saved/LPSP_AR02/InfimaGames_backup_' + stamp
    for top in PACK:
        shutil.copytree(os.path.join(OUR, top), os.path.join(bak, top))
    print('BACKUP:', bak)
    for src, dst, rel in copy:
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
    print('COPIED:', len(copy))


if __name__ == '__main__':
    main()
