import unreal,json
from pathlib import Path
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
s=unreal.load_object(None,'/Script/UnrealEd.Default__LevelEditorPlaySettings')
print('SETTINGS',s)
for n in ['PlayNumberOfClients','RunUnderOneProcess']:
    print(n,s.get_editor_property(n))
s.set_editor_property('PlayNumberOfClients',2)
s.set_editor_property('RunUnderOneProcess',True)
exec(Path(unreal.Paths.project_dir(),'Source/Tools/LPSPAR02/round4_start.py').read_text())
