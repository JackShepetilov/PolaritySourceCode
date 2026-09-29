import unreal, json

# Roll the NPC movement settings back to the values recorded before the temporary test.
BACKUP = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_settings_backup.json'
data = json.load(open(BACKUP, encoding='utf-8'))
st = unreal.load_asset(data['asset'])
print('restoring into', data['asset'])
for prop, value in data['values'].items():
    st.set_editor_property(prop, value)
    print('   %s = %s' % (prop, value))
print('saved:', unreal.EditorAssetLibrary.save_asset(data['asset'], only_if_is_dirty=False))
back = unreal.load_asset(data['asset'])
for prop in data['values']:
    print('   verify %s = %s' % (prop, back.get_editor_property(prop)))
