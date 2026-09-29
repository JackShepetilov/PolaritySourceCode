import unreal, json

# Where is the player pawn class set? Needed so PIE can spawn the new test pawn.
LOG = unreal.Paths.project_saved_dir() + 'LPSP_AR02/player_pawn.log'
L = []


def log(m):
    L.append(str(m))
    print(m)


settings = unreal.load_class(None, '/Script/EngineSettings.GameMapsSettings')
cdo = unreal.get_default_object(settings) if settings else None
if cdo:
    for prop in ('global_default_game_mode', 'game_default_map', 'editor_start_map'):
        try:
            v = cdo.get_editor_property(prop)
            log('GameMapsSettings.%s = %s' % (prop, v))
        except Exception as e:
            log('GameMapsSettings.%s err %s' % (prop, e))
    gm = None
    try:
        gm = cdo.get_editor_property('global_default_game_mode').try_load_class()
    except Exception as e:
        log('gm load err %s' % e)
    log('game mode class: %s' % gm)
    if gm:
        gmcdo = unreal.get_default_object(gm)
        for prop in ('default_pawn_class', 'player_controller_class', 'hud_class'):
            try:
                log('   %s = %s' % (prop, gmcdo.get_editor_property(prop)))
            except Exception as e:
                log('   %s err %s' % (prop, e))

log('test pawn exists: %s' % unreal.EditorAssetLibrary.does_asset_exist(
    '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterCharacter_AR02_Test'))
open(LOG, 'w', encoding='utf-8').write('\n'.join(L))
