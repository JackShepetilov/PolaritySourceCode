# AR_02 test integration — checkpoint 2026-09-27

The requested integration is **not complete**. Do not treat the adapter as production ready.

## Latest continuation after MCP became available

- Native Unreal MCP tools became available again; editor calls work. No user action is required for the previously reported connection blocker.
- Created `/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterNPC_AR02_Test`, a child of the original shooter with `SK_TP_CH_Default` and the test AnimBP. Compiled, saved, and mesh read back. Production NPC classes have not been switched.
- Retargeted `Slide_Full` through `RTG_UE5Manny_UE4Manny`, with source `SKM_Manny_Simple` and target `SK_TP_CH_Default`, to `/Game/Variant_Shooter/Tests/LPSP_AR02/Slide_Full_UE4`. Result skeleton verified as Infima `SKEL_Character`; saved. Not yet blended into AnimBP.
- Captured `Saved/LPSP_AR02/slide_preview.png`. Preview shows a Nanite fallback warning and incomplete body; this does NOT establish visual correctness.
- Changed original BP_AR ThirdPersonMesh from `SK_AR_01` to `SK_AR_02`. Compiled, saved, and read back. FirstPersonMesh verified unchanged as Kinemation `SKM_MX16A4_New`.
- Exact prior mesh saved in `Saved/LPSP_AR02/weapon_mesh_rollback.json`; `rollback_weapon_mesh.py` restores only this property. Full BP backup remains available.
- Weapon-to-adapter graph bridge failed: six nodes created, member_set could not resolve AnimBlueprintGeneratedClass; the action-menu setter also failed. All six experimental nodes were deleted, BP_AR compiled/saved. `connect_weapon.py` is disabled and is NOT a working setup script.
- Still missing: weapon/reference and shot/reload/aim integration; authored attachment alignment; slide blending; lean/crouch and PIE validation. Do not claim LPSP-equivalent behavior yet.

## Saved in the previous editor session

- Backup: `/Game/Variant_Shooter/Tests/LPSP_AR02/Backup/BP_AR_Before_LPSP`.
- Working AnimBP: `/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test`.
- Twelve mismatched spring struct connections fixed in the working copy; it compiled and saved.
- AR_02 settings captured in `Saved/LPSP_AR02/ar02_reference_settings.json`.
- Movement inputs adapted to actor-local velocity; running adapted to ApexMovement.IsSprinting.
- Several LPSP interface outputs replaced with retained adapter variables.
- Original BP_AR and production NPC assets have not yet been changed by this task.

## This continuation

- Added already-adapted connection checks to `setup_adapter.py` movement stages.
- Added a bounded fresh-session stdio client using the configured mcp-remote bridge.
- Python syntax of both scripts checked successfully; no new editor-side validation was possible.
- Editor was initially closed. Launched through the project script without a build; second launch outside sandbox loaded TestLevel. Startup took about 93 seconds and additional plugin initialization followed.
- At 10:12 Moscow, no listener on port 8000. Fresh stdio bridge initialization timed out. Native Unreal MCP tools are absent from this Codex session.
- No C++ or header changes. No commits.

## Resume after MCP is available

1. Re-read saved adapter state; Python globals from the former editor session no longer exist.
2. Create a child of `/Game/Variant_Shooter/Blueprints/AI/BPs/BP_ShooterNPC` in the test folder, with Infima UE4 body and working AnimBP. Inspect original LPSP inventory attachment to obtain the correct socket.
3. Connect actual weapon reference, shot count, reload state and aim pitch to the adapter. Copy authored AR_02 recoil/settings. Inspect remaining interface dependencies.
4. Apply AR_02 third-person mesh and reload/fire animations with a reversible property manifest. Preserve Kinemation first-person setup. Native PlayWeaponMeshAnimation currently sends the same asset to both meshes.
5. Retarget slide onto target UE4 skeleton, and blend its lower-body pose into the working AnimBP. Native PolarityCharacter already pushes SlideAlpha into FP and TP anim instances if they declare it.
6. Verify standing/crouching locomotion, aiming, firing, reload, slide and lean visually in PIE; verify multiplayer implications. No visual success has yet been established.

The `adapter_done.json` marker was never written. Do not assume `main()` previously completed in a single run. The movement stages now skip already-disconnected old interface outputs, but partial graph creation still requires inspection before rerunning.
