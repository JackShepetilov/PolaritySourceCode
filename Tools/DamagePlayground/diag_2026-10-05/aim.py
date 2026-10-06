import unreal, math
TARGET = "DP_GRUNT_NORMAL"; DZ = 40.0
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
tgt = None
for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Pawn):
    if a.get_actor_label() == TARGET: tgt = a
cm = unreal.GameplayStatics.get_player_camera_manager(w, 0)
eye = cm.get_camera_location()
p = tgt.get_actor_location() + unreal.Vector(0, 0, DZ)
d = p - eye
yaw = math.degrees(math.atan2(d.y, d.x)); pitch = math.degrees(math.atan2(d.z, math.hypot(d.x, d.y)))
unreal.GameplayStatics.get_player_controller(w, 0).set_control_rotation(unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw))
print("AIM", TARGET, tgt.get_name(), "hp", tgt.get_editor_property("current_hp"), "dist", d.length())
