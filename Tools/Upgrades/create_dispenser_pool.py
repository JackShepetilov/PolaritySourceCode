import unreal

# Creates or completes DA_DispenserUpgradePool, the dispenser slot machine's pool
# (Docs/Dispenser_Upgrade_SlotMachine_Spec_2026-09-25.md, Docs/Handoff_SlotMachine_2026-09-25.md).
# Everything in the pool is a data asset (upgrade, attachment, buff); the pool entry only holds the
# weights. Every section is filled ONLY while it is empty, so what the author set by hand is never
# overwritten: run it again after a code change resets a section, and only that section gets its
# starting values. The contents below are a STARTING PROPOSAL for the author to edit in the asset.
#
# Rarity per level of an upgrade is not set here: it lives on each upgrade (LevelDisplays[i].Rarity).
# Rarity and family of an attachment are not set here either: they live in the attachment's own
# asset (Rarity, Family). The script only prints them, so a wrong tier is easy to spot.

FOLDER = "/Game/Variant_Shooter/Blueprints/Upgrades"
NAME = "DA_DispenserUpgradePool"
UP = FOLDER + "/"
CARTRIDGE = "/Game/Buildables/Dispenser/SlotMachine/SM_UpgradeCartridge"
UPGRADE_PICKUP_FOLDER = FOLDER + "/Pickups"
BUFF_FOLDER = FOLDER + "/Buffs"

# (slot name, exclusive, [upgrade assets])
SLOTS = [
    ("Jump", True, ["Boring/DA_Upgrade_AirDash", "Boring/DA_Upgrade_ExtraJump", "Boring/DA_Upgrade_ChargedJump"]),
    ("Aim", True, []),
    ("Slide", True, ["WeaponUpgrades/DA_SwordSlide"]),
    ("Sprint", True, []),
    ("Grapple", True, []),
    ("Melee", True, ["Fun/DA_Upgrade_DropKick", "Fun/DA_UpgradeDefinition_ChargedPunch",
                     "Fun/DA_UpgradeDefinition_Backstab", "Fun/DA_UpgradeDefinition_Combo",
                     "WeaponUpgrades/DA_MeleeCharge"]),
    ("Ability", True, []),
    ("Passive", False, ["Boring/DA_MaxHealth", "Boring/DA_FullHealthBonus", "Boring/DA_LowHealthDefense",
                        "Boring/DA_VerticalCooling", "Boring/DA_DropChargeOverride", "Fun/DA_PlotArmor",
                        "Fun/DA_TestosteroneBoost", "Fun/DA_HealthBlast", "Fun/DA_Bandolier",
                        "Fun/DA_UD_360Shot", "Fun/DA_AirMail", "Fun/DA_UD_ChargeFlip",
                        "Fun/DA_UpgradeDefinition_Microstun", "Fun/DA_UpgradeDefinition_TractorBeam",
                        "WeaponUpgrades/DA_RocketSwap", "WeaponUpgrades/DA_SMG_AmmoRefund"]),
]

ATTACH = "/Game/Variant_Shooter/Blueprints/Pickups/Attachments/Scopes/"
# (attachment asset, weight, override weight while already owned or None)
ATTACHMENTS = [
    (ATTACH + "DA_Attach_Magazine_Common", 1.0, None),
    (ATTACH + "DA_Attach_Scope1xPistols", 1.0, 0.2),
    (ATTACH + "DA_Attach_Scope1x", 1.0, 0.2),
    (ATTACH + "DA_Attach_Scope4x", 1.0, 0.2),
]

K = unreal.DispenserBuffKind
# (asset name, display name, kind, amount, weight). Amount: health, armour, or magazines.
BUFFS = [
    ("DA_Buff_Heal", "Health", K.HEAL, 50.0, 1.0),
    ("DA_Buff_Armor", "Armor", K.ARMOR, 50.0, 1.0),
    ("DA_Buff_Ammo", "Ammo", K.AMMO, 2.0, 1.0),
]


def set_bool(obj, name, value):
    # A bool bFoo is foo in Python, but not always: try both spellings.
    for prop in (name, "b_" + name):
        try:
            obj.set_editor_property(prop, value)
            return True
        except Exception:
            pass
    print("NO PROPERTY:", name, "on", type(obj).__name__)
    return False


def upgrade_entries(assets):
    out = []
    for rel in assets:
        a = unreal.EditorAssetLibrary.load_asset(UP + rel)
        if not a:
            print("MISSING upgrade:", UP + rel)
            continue
        e = unreal.DispenserUpgradeEntry()
        e.set_editor_property("upgrade", a)
        e.set_editor_property("weight", 1.0)
        out.append(e)
    return out


def attachment_entries():
    out = []
    for path, weight, owned_weight in ATTACHMENTS:
        a = unreal.EditorAssetLibrary.load_asset(path)
        if not a:
            print("MISSING attachment:", path)
            continue
        e = unreal.DispenserAttachmentEntry()
        e.set_editor_property("attachment", a)
        e.set_editor_property("weight", weight)
        if owned_weight is not None:
            set_bool(e, "override_chance_when_already_equipped", True)
            e.set_editor_property("weight_when_equipped", owned_weight)
        out.append(e)
        print("  attachment", a.get_name(), "rarity", a.get_editor_property("rarity"),
              "family", a.get_editor_property("family"))
    return out


def buff_entries():
    out = []
    for name, display, kind, amount, weight in BUFFS:
        path = BUFF_FOLDER + "/" + name
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            buff = unreal.EditorAssetLibrary.load_asset(path)
        else:
            tools = unreal.AssetToolsHelpers.get_asset_tools()
            buff = tools.create_asset(name, BUFF_FOLDER, unreal.DispenserBuffDefinition, unreal.DataAssetFactory())
            buff.set_editor_property("display_name", display)
            buff.set_editor_property("kind", kind)
            buff.set_editor_property("amount", amount)
            unreal.EditorAssetLibrary.save_loaded_asset(buff)
            print("CREATED:", path)
        if not buff:
            print("MISSING buff:", path)
            continue
        e = unreal.DispenserBuffEntry()
        e.set_editor_property("buff", buff)
        e.set_editor_property("weight", weight)
        out.append(e)
    return out


def find_upgrade_pickup_class():
    # The first Blueprint in the upgrade pickups folder that IS an AUpgradePickup: its hologram
    # widget is authored there, the C++ class alone has none.
    for path in sorted(unreal.EditorAssetLibrary.list_assets(UPGRADE_PICKUP_FOLDER, recursive=False)):
        cls = unreal.EditorAssetLibrary.load_blueprint_class(path.split(".")[0])
        if cls and isinstance(unreal.get_default_object(cls), unreal.UpgradePickup):
            return cls, path
    return None, None


def has_any(entries, prop):
    return any(e.get_editor_property(prop) for e in entries)


path = FOLDER + "/" + NAME
pool = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
if not pool:
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    pool = tools.create_asset(NAME, FOLDER, unreal.DispenserUpgradePool, unreal.DataAssetFactory())
    print("CREATED:", path)

changed = False
slots = list(pool.get_editor_property("slots"))
if len(slots) == 0:
    for name, exclusive, assets in SLOTS:
        slot = unreal.DispenserUpgradeSlot()
        slot.set_editor_property("display_name", name)
        set_bool(slot, "exclusive", exclusive)
        slot.set_editor_property("upgrades", upgrade_entries(assets))
        slots.append(slot)
    pool.set_editor_property("slots", slots)
    print("FILLED slots:", len(slots))
    changed = True
elif not any(has_any(s.get_editor_property("upgrades"), "upgrade") for s in slots):
    # The slots survived a struct change but their upgrades did not: refill each slot the author
    # kept under a known name, leave the name, the exclusive flag and unknown slots as they are.
    by_name = {name: assets for name, _, assets in SLOTS}
    for slot in slots:
        name = str(slot.get_editor_property("display_name"))
        if name in by_name:
            slot.set_editor_property("upgrades", upgrade_entries(by_name[name]))
            print("REFILLED slot upgrades:", name)
    pool.set_editor_property("slots", slots)
    changed = True

if not pool.get_editor_property("upgrade_pickup_class"):
    cls, found = find_upgrade_pickup_class()
    if cls:
        pool.set_editor_property("upgrade_pickup_class", cls)
        print("FILLED upgrade_pickup_class:", found)
        changed = True
    else:
        print("NO AUpgradePickup Blueprint found in", UPGRADE_PICKUP_FOLDER, "- the plain C++ pickup will be used")

cart = pool.get_editor_property("upgrade_cartridge_mesh")
if cart is None or (hasattr(cart, "is_null") and cart.is_null()):
    mesh = unreal.EditorAssetLibrary.load_asset(CARTRIDGE)
    if mesh:
        pool.set_editor_property("upgrade_cartridge_mesh", mesh)
        print("FILLED upgrade_cartridge_mesh:", CARTRIDGE)
        changed = True

if not has_any(pool.get_editor_property("attachments"), "attachment"):
    rows = attachment_entries()
    pool.set_editor_property("attachments", rows)
    print("FILLED attachments", len(rows))
    changed = True

if not has_any(pool.get_editor_property("buffs"), "buff"):
    rows = buff_entries()
    pool.set_editor_property("buffs", rows)
    print("FILLED buffs", len(rows))
    changed = True

if changed:
    unreal.EditorAssetLibrary.save_loaded_asset(pool)
    print("SAVED:", path)
else:
    print("NOTHING TO FILL:", path)
