"""Accelerated live-PIE selection test; never changes the saved pool or machine defaults."""
import json
from pathlib import Path
import unreal

def begin_casino_check(count=30):
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    controller = next(p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerController)
                      if p.get_controlled_pawn())
    player = controller.get_controlled_pawn()
    player.equip_starting_weapon_animated()
    machine = next(a.get_component_by_class(unreal.DispenserSlotMachineComponent)
                   for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.BuildableActor)
                   if "Dispenser" in a.get_class().get_name())
    pool = machine.get_editor_property("upgrade_pool")
    inventory = player.get_component_by_class(unreal.InventoryComponent)
    observations = []
    output = Path(r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Saved/Casino_Runtime_2026-10-04.json")
    handle = [None]
    next_time = [0.0]
    original_dilation = unreal.GameplayStatics.get_global_time_dilation(world)
    unreal.SystemLibrary.execute_console_command(world, "polarity.debug.timescale 20")
    unreal.SystemLibrary.execute_console_command(world, "polarity.player.god 1")

    def tick(_delta):
        if not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
            unreal.unregister_slate_post_tick_callback(handle[0])
            return
        now = unreal.GameplayStatics.get_time_seconds(world)
        if now < next_time[0] or machine.get_spin().phase != unreal.DispenserSpinPhase.IDLE:
            return
        if len(observations) >= count:
            unreal.SystemLibrary.execute_console_command(world, "polarity.debug.timescale %g" % original_dilation)
            unreal.unregister_slate_post_tick_callback(handle[0])
            print("[CASINO_VERIFY] complete", len(observations))
            return
        # Only discard rewards created by this isolated test so empty slots remain comparable.
        for index, slot in enumerate(inventory.get_slots()):
            if slot.kind == unreal.InventorySlotKind.ATTACHMENT:
                inventory.clear_slot(index)
        weapon = player.get_current_weapon()
        if not weapon:
            return
        unreal.SystemLibrary.execute_console_command(world, "polarity.casino.offer 5")
        spin = machine.get_spin()
        cards = []
        for card in spin.cards:
            row = {"outcome": str(card.outcome), "pickup_index": card.pickup_index}
            if card.outcome == unreal.DispenserCardOutcome.ATTACHMENT:
                attachment = pool.attachments[card.pickup_index].attachment
                row.update(attachment=attachment.get_name(), fits=attachment.fits_weapon(weapon.get_class()))
                assert row["fits"], "Casino offered an attachment incompatible with the held gun"
            cards.append(row)
        observations.append({"serial": spin.serial, "weapon": weapon.get_class().get_name(), "cards": cards})
        output.write_text(json.dumps(observations, indent=2), encoding="utf-8")
        next_time[0] = now + 1

    handle[0] = unreal.register_slate_post_tick_callback(tick)
    print("[CASINO_VERIFY] scheduled", count)
