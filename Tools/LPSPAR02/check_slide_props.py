import unreal

PATHS = [
    '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Neutral_Slide_FootOut_Into_Lfoot',
    '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Neutral_Slide_FootOut_Into_Rfoot',
    '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Neutral_Slide_FootOut_Loop',
    '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Neutral_Slide_FootOut_Out_Idle_Crouch',
    '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Neutral_Slide_FootOut_Out_Idle_Stand',
    '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Neutral_Slide_FootOut_Out_Moving_Crouch',
    '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Neutral_Slide_FootOut_Out_Moving_Run',
    '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Neutral_Slide_FootOut_Out_Moving_Walk',
    '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Relaxed_Slide_FootOut_Into_Lfoot',
    '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Relaxed_Slide_FootOut_Into_Rfoot',
    '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Relaxed_Slide_FootOut_Into_Sprint_Lfoot',
    '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Relaxed_Slide_FootOut_Into_Sprint_Rfoot',
    '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Relaxed_Slide_FootOut_Loop',
    '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Relaxed_Slide_FootOut_Out_Idle_Crouch',
    '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Relaxed_Slide_FootOut_Out_Idle_Stand',
    '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Relaxed_Slide_FootOut_Out_Moving_Crouch',
    '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Relaxed_Slide_FootOut_Out_Moving_Run',
    '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Relaxed_Slide_FootOut_Out_Moving_Sprint',
    '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Relaxed_Slide_FootOut_Out_Moving_Walk',
]

print('=== INSPECTING FOOTOUT SLIDE ANIMATIONS ===')
for p in PATHS:
    anim = unreal.load_asset(p)
    if not anim:
        print('FAILED TO LOAD:', p)
        continue
    skel = anim.get_editor_property('skeleton')
    add_type = anim.get_editor_property('additive_anim_type')
    rm = anim.get_editor_property('enable_root_motion')
    seq_len = anim.get_editor_property('sequence_length')
    print('%s | Skel=%s | Add=%s | RM=%s | Len=%.2fs' % (
        anim.get_name(), skel.get_name() if skel else 'None', add_type, rm, seq_len))
