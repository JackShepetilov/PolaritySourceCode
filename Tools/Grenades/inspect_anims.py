# inspect_anims.py
# Read-only: length, root motion travel, and which way the body lies at the start and end of the
# Frank knockdown, get-up and stun sequences. Used to pair a fall with its get-up and to split
# the falls into "from the front" (goes over backwards) and "from behind" (goes over forwards).
# Docs/Grenades_Plan_2026-10-14.md
import unreal

FOLDERS = ["/Game/Variant_Shooter/Anims/Knockdown", "/Game/Variant_Shooter/Anims/Stuns"]
MESH = "/Game/Variant_Shooter/Tests/LPSP_AR02/SK_TP_CH_AR02_Test"


def pose_info(anim, t, mesh):
    opts = unreal.AnimPoseEvaluationOptions()
    opts.set_editor_property("optional_skeletal_mesh", mesh)
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts)
    pelvis = unreal.AnimPoseExtensions.get_bone_pose(pose, "pelvis", unreal.AnimPoseSpaces.WORLD)
    head = unreal.AnimPoseExtensions.get_bone_pose(pose, "head", unreal.AnimPoseSpaces.WORLD)
    p = pelvis.translation
    h = head.translation
    # The chest's facing: pelvis local axes in mesh space. Which axis is "forward" for this skeleton
    # is read off the standing first frame, so both are printed.
    q = pelvis.rotation
    fx = q.rotate_vector(unreal.Vector(1, 0, 0))
    fy = q.rotate_vector(unreal.Vector(0, 1, 0))
    fz = q.rotate_vector(unreal.Vector(0, 0, 1))
    return p, h, fx, fy, fz


def main():
    mesh = unreal.load_asset(MESH)
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    for folder in FOLDERS:
        for ad in reg.get_assets_by_path(folder, recursive=False):
            anim = ad.get_asset()
            if not isinstance(anim, unreal.AnimSequence):
                continue
            length = anim.get_play_length()
            rm = anim.get_editor_property("enable_root_motion")
            line = "%s len=%.2f rootmotion=%s" % (anim.get_name(), length, rm)
            for tag, t in (("start", 0.0), ("end", max(0.0, length - 0.02))):
                p, h, fx, fy, fz = pose_info(anim, t, mesh)
                line += " | %s pelvis=(%.0f,%.0f,%.0f) head=(%.0f,%.0f,%.0f) X=(%.2f,%.2f,%.2f) Y=(%.2f,%.2f,%.2f) Z=(%.2f,%.2f,%.2f)" % (
                    tag, p.x, p.y, p.z, h.x, h.y, h.z, fx.x, fx.y, fx.z, fy.x, fy.y, fy.z, fz.x, fz.y, fz.z)
            print(line)



