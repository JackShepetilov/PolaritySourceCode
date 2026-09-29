import unreal

# Cheap state probe: is PIE running, is anything dirty, which test NPC is on the level.
# Run: Tools/mcp.sh py Source/Tools/LPSPAR02/probe_state.py

es = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
w = es.get_editor_world()
print('editor world:', w.get_name() if w else None)
print('dirty maps:', [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
print('dirty content:', [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()])
gw = es.get_game_world()
print('game world:', gw.get_name() if gw else None, '(None = PIE not running)')


def look(world, tag):
    cls = unreal.load_class(None, '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterNPC_AR02_Test.BP_ShooterNPC_AR02_Test_C')
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, cls)
    print(tag, 'test NPCs:', len(actors))
    for a in actors:
        m = a.get_editor_property('mesh')
        ai = m.get_anim_instance()
        loc = a.get_actor_location()
        print('  loc=(%.0f,%.0f,%.0f) yaw=%.0f spd=%.0f mesh=%s anim=%s' % (
            loc.x, loc.y, loc.z, a.get_actor_rotation().yaw, a.get_velocity().length(),
            m.get_skinned_asset().get_name() if m.get_skinned_asset() else None,
            ai.__class__.__name__ if ai else None))


look(w, 'editor:')
if gw:
    look(gw, 'game:')
