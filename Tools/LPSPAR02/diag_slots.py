import unreal, json, time

# 1) Which slots the test weapon's montages target vs which slots the LPSP graph actually has.
# 2) Arm a sampler that logs the player's anim variables, so every complaint can be traced to an input.
AB_PKG = '/Game/Variant_Shooter/Tests/LPSP_AR02'
ABP = AB_PKG + '/ABP_AR02_LPSP_Test'
WEAPON = AB_PKG + '/BP_AR02_Integration_Test'
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/diag.txt'
L = []


def log(m):
    L.append(str(m))
    print(m)


# --- graph slots ---
pkg = unreal.load_asset(ABP).get_outermost()
slots = set()
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() != pkg:
        continue
    if 'AnimGraphNode_Slot' in n.get_class().get_name():
        try:
            slots.add(str(n.get_editor_property('node').get_editor_property('slot_name')))
        except Exception:
            t = str(n.get_node_title()).replace('\n', ' ')
            slots.add(t)
log('graph slots: %s' % sorted(slots))

# a few expected LPSP slot names for comparison
for s in ('Action Aiming', 'Overlay Aiming', 'Action Left Arm Aiming', 'Action Standing'):
    log('   has %-24s : %s' % (s, s in slots))

# --- weapon montages ---
cls = unreal.load_class(None, WEAPON + '.' + WEAPON.split('/')[-1] + '_C')
cdo = unreal.get_default_object(cls) if cls else None
log('weapon cdo: %s' % (cdo is not None))
if cdo:
    for prop in ('firing_montage', 'reload_montage', 'secondary_reload_montage',
                 'weapon_mesh_fire_animation', 'weapon_mesh_reload_animation'):
        try:
            v = cdo.get_editor_property(prop)
        except Exception as e:
            log('   %-32s : err %s' % (prop, e))
            continue
        if not v:
            log('   %-32s : NONE' % prop)
            continue
        extra = ''
        try:
            tracks = v.get_editor_property('slot_anim_tracks')
            extra = ' slots=%s' % [str(t.get_editor_property('slot_name')) for t in tracks]
        except Exception:
            pass
        log('   %-32s : %s%s' % (prop, v.get_name(), extra))
