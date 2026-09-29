import json
import re

import unreal

# What the transferred Settings Animation actually carries on the live M1911 body: the sequence and
# blendspace tables are fields inside that struct.

OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/live_settings.jsonl'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world, 'PIE required'
subject = next(p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ShooterCharacter)
               if p.is_locally_controlled())
body = subject.get_editor_property('mesh').get_anim_instance()
settings = body.get_editor_property('Settings Animation')
text = settings.export_text()
row = {'weapon': str(subject.get_current_weapon().get_name())}
for token in ('DataTableSequences', 'DataTableBlendspaces', 'DataTablePoses', 'LookUps', 'Unarmed'):
    hits = sorted({m.group(0) for m in re.finditer(r'%s[^,)]{0,120}' % token, text)})
    row[token] = hits[:4]
row['SequenceLoopJog'] = [h for h in row.pop('LookUps', [])][:2]
with open(OUT, 'w', encoding='utf-8') as handle:
    handle.write(json.dumps(row, default=str, indent=1) + '\n')
print('LIVE SETTINGS', json.dumps(row, default=str)[:1200])
