import unreal
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
base='/Game/Variant_Shooter/Tests/LPSP_AR02'
source='/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/ARs/AM_TP_CH_AR_02_Reload_Empty'
path=base+'/AM_AR02_TP_Reload_Empty'
if not unreal.EditorAssetLibrary.does_asset_exist(path):
    assert unreal.EditorAssetLibrary.duplicate_asset(source,path)
    print('CREATED',path)
m=unreal.load_asset(path)
tracks=list(m.get_editor_property('slot_anim_tracks'))
if [str(t.get_editor_property('slot_name')) for t in tracks]!=['Action Standing','Action Aiming']:
    assert len(tracks)==1 and str(tracks[0].get_editor_property('slot_name'))=='DefaultSlot'
    standing=tracks[0]
    aiming=unreal.SlotAnimationTrack()
    aiming.import_text(standing.export_text())
    standing.set_editor_property('slot_name','Action Standing')
    aiming.set_editor_property('slot_name','Action Aiming')
    m.set_editor_property('slot_anim_tracks',[standing,aiming])
    print('MODIFIED',path,'standing and aiming slots')
assert unreal.EditorAssetLibrary.save_asset(path,False)
bp=unreal.load_asset(base+'/BP_AR02_Integration_Test')
assert unreal.BlueprintService.set_property(base+'/BP_AR02_Integration_Test','ReloadMontageTP',m.get_path_name())
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp,False)
print('SAVED reload montage and weapon assignment')
