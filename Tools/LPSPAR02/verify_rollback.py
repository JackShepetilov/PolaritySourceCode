import unreal, json

# Make sure the rolled-back value is really on disk, not only in memory.
SET = '/Game/Variant_Shooter/Blueprints/MovementSettings/MovementSettings_ShooterNPC'
print('in memory:', unreal.load_asset(SET).get_editor_property('SlideMinStartSpeed'))
print('reload:', unreal.EditorAssetLibrary.reload_asset(SET))
print('after reload from disk:', unreal.load_asset(SET).get_editor_property('SlideMinStartSpeed'))
print('dirty:', unreal.EditorAssetLibrary.does_asset_exist(SET),
      unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages() != [])
