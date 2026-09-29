"""Print the blocks of Saved/LPSP_AR02/graph_now.txt whose node name or title contains any of the
given tokens (case sensitive). Blocks start at a line that has no leading spaces and contains ' | '.

    python show_graph.py LayeredBoneBlend SequencePlayer BlendListByBool_5 BlendListByBool_6
"""
import io
import os
import sys

PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                    '..', '..', '..', 'Saved', 'LPSP_AR02', 'graph_now.txt')
tokens = sys.argv[1:]
blocks = []
for line in io.open(PATH, encoding='utf-8'):
    line = line.rstrip('\n').rstrip('\r')
    if line and not line.startswith(' ') and ' | ' in line:
        blocks.append([line])
    elif blocks:
        blocks[-1].append(line)
for b in blocks:
    if any(t in b[0] for t in tokens):
        print('\n'.join(b).rstrip())
        print()
