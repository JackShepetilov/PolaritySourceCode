// WeaponAttachmentDefinition.h
// One attachment, as data.
//
// The contract lives in Docs/Inventory_Slot_Contract_2026-08-28.md section 4. Two of its rules are
// the reason this asset looks the way it does:
//
//  - Four types per weapon and no more: sight, magazine, muzzle, stock. The type is what makes two
//    attachments mutually exclusive, so it is a hard enum rather than a tag.
//  - Attachments travel with the gun. That is why nothing here knows about the inventory: the cell
//    an attachment costs is the inventory's business, and this asset is only the thing itself.
//
// WHERE THE SOCKET NAME LIVES, and why it is not here.
//
// A socket belongs to the mesh it is authored on, and the mesh is the WEAPON's. Two rifles mount a
// scope on sockets with different names; the same scope fits both. So the weapon owns the map from
// type to socket (AShooterWeapon::AttachmentSockets) and this asset only says which type it is.
// The one thing the attachment mesh must carry itself is SOCKET_Aim, the eye point behind the
// glass, which is what AShooterWeapon::ResolveADSAnchorAttachment looks for -- and that is a socket
// on the attachment, so it travels with it and needs no per-weapon tuning.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Polarity/Upgrades/UpgradeDefinition.h"
#include "WeaponAttachmentDefinition.generated.h"

class UStaticMesh;
class UTexture2D;
class AShooterWeapon;
class UTacticalDeviceDefinition;
class UAnimMontage;
class UMaterialInterface;
class UNiagaraSystem;
class USoundBase;

/** What a muzzle does to the shots, on top of the recoil it changes. Docs/Muzzle_Attachment_Plan_2026-10-01.md.
 *  Append only, like EWeaponAttachmentType. */
UENUM(BlueprintType)
enum class EMuzzleEffect : uint8
{
	/** Recoil only. */
	None,

	/** Chest hits fill a meter on the target; full meter stuns it. A target with no chest bone is
	 *  slowed by hits near its middle instead. */
	Stagger,

	/** A round that hits an enemy jumps on to the nearest other enemy for part of its damage. */
	Ricochet
};

/** What kind of attachment this is. One of each per weapon: mounting a second sight replaces the
 *  first rather than stacking, which is the whole reason this is an enum and not a free-form tag.
 *
 *  Append only. The values are saved in Blueprints and data assets, so inserting in the middle
 *  silently repoints every existing asset at the wrong type. */
UENUM(BlueprintType)
enum class EWeaponAttachmentType : uint8
{
	/** Sight, scope, holo. The one type that changes how the weapon is aimed rather than how it
	 *  shoots, because the eye point moves to the attachment's own SOCKET_Aim. */
	Optic,

	/** Extended or drum magazine. */
	Magazine,

	/** Suppressor, compensator, brake. */
	Muzzle,

	/** Stock, grip. */
	Stock,

	/** Side rail device: flashlight, laser, gun shield, freezer. Runs while the owner aims down
	 *  sights and spends its own charge. What it does is the Device asset below. */
	Tactical
};

/**
 * One attachment. Make a data asset per attachment; the pickup and the inventory cell both point
 * at that asset, and so does the weapon it ends up mounted on.
 */
UCLASS(BlueprintType)
class POLARITY_API UWeaponAttachmentDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	// ==================== Identity ====================

	/** Which of the four slots on a weapon this occupies. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment")
	EWeaponAttachmentType Type = EWeaponAttachmentType::Optic;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment")
	FText DisplayName;

	/** Its own picture: set it for an attachment that is one of a kind (a scope, a flashlight).
	 *  Leave empty for a tier of a family, which then wears the family's or the type's picture.
	 *  A white silhouette: the UI tints it by Rarity. Draw with GetDisplayIcon, not this. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment")
	TObjectPtr<UTexture2D> Icon;

	/** What the UI draws: Icon, else the family's icon, else the type's
	 *  (Project Settings -> Polarity -> Inventory Icons). */
	UFUNCTION(BlueprintPure, Category = "Attachment")
	UTexture2D* GetDisplayIcon() const;

	// ==================== Dispenser ====================
	// Docs/Dispenser_Upgrade_SlotMachine_Spec_2026-09-25.md.

	/** How rare this is at the dispenser. In a family it is also the tier, Apex-style: white, blue,
	 *  purple, gold. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Dispenser")
	EUpgradeRarity Rarity = EUpgradeRarity::Common;

	/** The tiers of one attachment share a family ("Mag.Light": light magazine T1..T4). The dispenser
	 *  then offers a tier only ABOVE the best of the family the player already has. Empty: a
	 *  standalone item (a particular scope), offered regardless. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Dispenser")
	FName Family;

	// ==================== The thing on the gun ====================

	/** Mounted on the weapon's mount socket for this type, on the first person mesh and the third
	 *  person one both.
	 *
	 *  An OPTIC should carry a socket named SOCKET_Aim, sitting behind the glass and pointing down
	 *  the sight line. That socket is the eye point: with it the ADS camera moves to this scope on
	 *  its own and nothing needs tuning per weapon. Without it the weapon falls back to its own
	 *  iron sights, which means the scope will be drawn but will not be looked through. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment")
	TObjectPtr<UStaticMesh> Mesh;

	/** Scale for the mounted mesh, for a part authored at a different size than the weapon. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment")
	FVector MeshScale = FVector(1.0f, 1.0f, 1.0f);

	/** Nudge applied on top of the mount socket, in socket space. The socket is meant to be right;
	 *  this is for the one part that sits a few millimetres proud of the rail. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment")
	FTransform MountOffset = FTransform::Identity;

	// ==================== Which guns it fits ====================
	//
	// A whitelist, and an EMPTY one fits nothing. An attachment goes onto a weapon because somebody
	// said so, never by default: that is what keeps a 4x scope off a pistol, a magazine off a pump
	// gun, and everything off the RPG without anybody having to remember to exclude them.
	//
	// A listed class covers its children, so a parent blueprint can stand for a whole family. The
	// magazine keeps its list in its size table instead, so the two can never disagree about which
	// guns it fits.
	//
	// Checked in AShooterWeapon::InstallAttachment, the one place anything gets mounted.

	/** Guns this attachment can be mounted on. Not used by a magazine: see MagazineSizeByWeapon. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Fits", meta = (EditCondition = "Type != EWeaponAttachmentType::Magazine", EditConditionHides))
	TArray<TSubclassOf<AShooterWeapon>> CompatibleWeapons;

	/** Magazine only: the guns it fits, and what each one's magazine holds with it, in rounds. The
	 *  Apex way: every gun has its own number (Havoc 32, Volt 27 on the same purple mag). A gun
	 *  missing from here cannot take this magazine at all. When a gun and its parent are both
	 *  listed, the closer one wins. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Fits", meta = (EditCondition = "Type == EWeaponAttachmentType::Magazine", EditConditionHides))
	TMap<TSubclassOf<AShooterWeapon>, int32> MagazineSizeByWeapon;

	/** True when this attachment may go on a gun of this class. What the inventory screen asks before
	 *  it lights up a drop target, and what the weapon asks before it mounts anything. */
	UFUNCTION(BlueprintPure, Category = "Attachment")
	bool FitsWeapon(TSubclassOf<AShooterWeapon> WeaponClass) const;

	/** The magazine size this magazine gives a gun of this class, or 0 when it does not fit it. */
	UFUNCTION(BlueprintPure, Category = "Attachment")
	int32 GetMagazineSizeFor(TSubclassOf<AShooterWeapon> WeaponClass) const;

	// ==================== What it changes ====================
	//
	// Only knobs that are actually applied live here. A number that is stored and never read is
	// worse than a missing feature: it reads as configured on the asset and does nothing in the
	// game. Each new modifier goes in together with the one funnel in AShooterWeapon it multiplies,
	// and not before.

	/** Multiplies the weapon's ADSZoom. 1 leaves the magnification alone, which is right for a red
	 *  dot: what such a sight changes is the picture, and the picture comes from SOCKET_Aim on the
	 *  mesh above rather than from any number here.
	 *
	 *  Applied in AShooterWeapon::GetADSZoom, which is the single place the whole game turns zoom
	 *  into a field of view. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Modifiers", meta = (EditCondition = "!bOverrideADSZoom", ClampMin = "0.1", ClampMax = "10.0"))
	float ADSZoomMultiplier = 1.0f;

	/** Own the magnification outright instead of multiplying the weapon's.
	 *
	 *  Multiplying is right for a sight that ADDS to iron sights ("this rifle already aims at 1.5x,
	 *  the scope makes it 3x"). It is wrong for a scope whose whole identity is a number: a 4x is a
	 *  4x on every rifle it is bolted to, and with a multiplier that same asset would be 4x on one
	 *  gun and 6x on the next. Turn this on and ADSZoomOverride below is the magnification, full
	 *  stop -- the weapon's own ADSZoom stops being read while this optic is fitted. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Modifiers")
	bool bOverrideADSZoom = false;

	/** The magnification this optic gives, when bOverrideADSZoom is on. Same meaning as the weapon's
	 *  ADSZoom: how many times closer the picture is than the hip view. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Modifiers", meta = (EditCondition = "bOverrideADSZoom", ClampMin = "1.0", ClampMax = "20.0"))
	float ADSZoomOverride = 2.0f;

	// ---------- Magazine, after the Apex extended energy mag ----------
	//
	// Apex levels are capacity only (reload speed moved to stocks in its season 10), and capacity is
	// MagazineSizeByWeapon above. The gold one keeps the purple capacity and adds the holstered
	// reload below. The energy reserve is counted in magazines, so a bigger magazine makes the
	// reserve deeper and refills faster per round on its own, with no extra number.
	//
	// Applied in AShooterWeapon::ApplyMagazineModifiers.

	/** The gold perk: a gun that has been put away for HolsteredReloadDelay seconds fills its
	 *  magazine from its reserve, with no animation, so it comes out loaded. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Modifiers", meta = (EditCondition = "Type == EWeaponAttachmentType::Magazine"))
	bool bReloadsWhileHolstered = false;

	/** Seconds in the holster before the gold perk reloads. Apex: 4 (was 2 before September 2025). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Modifiers", meta = (EditCondition = "Type == EWeaponAttachmentType::Magazine && bReloadsWhileHolstered", ClampMin = "0.0", Units = "s"))
	float HolsteredReloadDelay = 4.0f;

	// ---------- Tactical ----------
	//
	// Read by UTacticalDeviceComponent on the character holding the gun. The attachment is only the
	// thing on the rail; the device asset is what it does and what it costs, so one device (a
	// flashlight) can be sold as several attachments without its numbers being copied.

	/** What this rail device does while the owner aims. Tactical only. Empty = a dead part. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Tactical", meta = (EditCondition = "Type == EWeaponAttachmentType::Tactical", EditConditionHides))
	TObjectPtr<UTacticalDeviceDefinition> Device;

	// ---------- Muzzle ----------
	//
	// Docs/Muzzle_Attachment_Plan_2026-10-01.md. Recoil: multipliers on the gun's own PRAS asset
	// (RecoilData -> Controller Recoil), applied in AShooterWeapon::ApplyMuzzleModifiers on a private
	// copy, so the shared asset is never written. Only pack (PRAS) guns read them; a gun on our own
	// WeaponRecoilComponent ignores them. The hit effects run in AShooterWeapon::ApplyMuzzleHit.

	/** Multiplies Controller Recoil -> Horizontal Recoil Step (both ends of the range). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle", EditConditionHides, ClampMin = "0.0", ClampMax = "4.0"))
	float HorizontalRecoilMultiplier = 1.0f;

	/** Multiplies Controller Recoil -> Vertical Recoil Step (both ends of the range). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle", EditConditionHides, ClampMin = "0.0", ClampMax = "4.0"))
	float VerticalRecoilMultiplier = 1.0f;

	/** Socket on THIS attachment's mesh where the muzzle flash and the tracer start (the end of the
	 *  flash hider). None or missing = the first socket with "Muzzle" in its name, else the mesh's
	 *  only socket, else the gun's own Muzzle socket. Shots themselves still leave from the gun. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle", EditConditionHides))
	FName FXSocketName;

	/** What the muzzle does to the hits. None = recoil only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle", EditConditionHides))
	EMuzzleEffect MuzzleEffect = EMuzzleEffect::None;

	// --- Stagger: chest ---

	/** Bones that count as the chest. A target whose mesh has none of them is handled by the
	 *  centre rule below instead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Stagger", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Stagger", EditConditionHides))
	TArray<FName> StaggerBoneNames = { FName("spine_03"), FName("spine_04"), FName("spine_05") };

	/** Chest damage that has to pile up on one target before it is stunned. Damage after shields. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Stagger", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Stagger", EditConditionHides, ClampMin = "0.0"))
	float StaggerDamageThreshold = 60.0f;

	/** The meter empties when the target goes this long without a chest hit from this gun. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Stagger", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Stagger", EditConditionHides, ClampMin = "0.1", Units = "s"))
	float StaggerMemory = 2.0f;

	/** How long the stun lasts (AShooterNPC::ApplyExplosionStun). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Stagger", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Stagger", EditConditionHides, ClampMin = "0.1", Units = "s"))
	float StaggerStunDuration = 1.2f;

	/** After a stun the meter stays shut for this long, so a long burst is not a stun lock. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Stagger", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Stagger", EditConditionHides, ClampMin = "0.0", Units = "s"))
	float StaggerImmunityTime = 3.0f;

	/** Played for the stun. Empty = the enemy's own KnockbackMontage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Stagger", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Stagger", EditConditionHides))
	TObjectPtr<UAnimMontage> StaggerStunMontage;

	// --- Stagger: no chest (drones, the carrier, the tank) ---

	/** A hit counts as central when the shot's line passes within this fraction of the hit part's
	 *  half-size (its largest bounding box half-extent) from the part's centre. Measured across the
	 *  line, not to the impact point, so a round sphere is not "never central". 0.5 = the inner half. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Stagger", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Stagger", EditConditionHides, ClampMin = "0.05", ClampMax = "1.0"))
	float CenterRadiusFraction = 0.5f;

	/** Central damage that has to pile up before the slow lands. 0 = every central hit slows. Uses
	 *  StaggerMemory too. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Stagger", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Stagger", EditConditionHides, ClampMin = "0.0"))
	float CenterSlowDamageThreshold = 0.0f;

	/** How long the slow lasts. Another central hit restarts it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Stagger", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Stagger", EditConditionHides, ClampMin = "0.1", Units = "s"))
	float CenterSlowDuration = 1.5f;

	/** Walk speed multiplier on a ground enemy (the tank). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Stagger", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Stagger", EditConditionHides, ClampMin = "0.05", ClampMax = "1.0"))
	float CenterSlowMoveMultiplier = 0.5f;

	/** Turn rate multiplier on a ground enemy. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Stagger", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Stagger", EditConditionHides, ClampMin = "0.05", ClampMax = "1.0"))
	float CenterSlowTurnMultiplier = 0.5f;

	/** Time rate of a flyer (drones, kamikaze, carrier): flight, turns and fire all run this much slower. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Stagger", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Stagger", EditConditionHides, ClampMin = "0.05", ClampMax = "1.0"))
	float CenterSlowFlyerTimeMultiplier = 0.5f;

	/** Overlay on a slowed enemy. Empty = none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Stagger", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Stagger", EditConditionHides))
	TObjectPtr<UMaterialInterface> CenterSlowOverlay;

	// --- Stagger: feedback, on every machine ---

	/** Spawned on the target when a stun or a slow lands. Gets User.Color (red for stun, blue for slow). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Stagger", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Stagger", EditConditionHides))
	TObjectPtr<UNiagaraSystem> StaggerFX;

	/** Played at the target when a stun lands. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Stagger", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Stagger", EditConditionHides))
	TObjectPtr<USoundBase> StaggerSound;

	/** Played at the target when a slow lands. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Stagger", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Stagger", EditConditionHides))
	TObjectPtr<USoundBase> CenterSlowSound;

	// --- Ricochet ---

	/** How many times one round jumps. Each jump goes to the nearest enemy not yet hit by this round. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Ricochet", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Ricochet", EditConditionHides, ClampMin = "1", ClampMax = "8"))
	int32 RicochetCount = 2;

	/** Each jump deals this fraction of the damage the round did to the first target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Ricochet", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Ricochet", EditConditionHides, ClampMin = "0.0", ClampMax = "1.0"))
	float RicochetDamageFraction = 0.5f;

	/** How far a jump can reach from the last hit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Ricochet", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Ricochet", EditConditionHides, ClampMin = "100.0", Units = "cm"))
	float RicochetRange = 1500.0f;

	/** Tracer for a jump. Gets the same parameters as the gun's BeamFX (BeamStart, BeamEnd,
	 *  Distance, BeamColor). Empty = the gun's own tracer. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Ricochet", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Ricochet", EditConditionHides))
	TObjectPtr<UNiagaraSystem> RicochetTracerFX;

	/** Played where a jump leaves from. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment|Muzzle|Ricochet", meta = (EditCondition = "Type == EWeaponAttachmentType::Muzzle && MuzzleEffect == EMuzzleEffect::Ricochet", EditConditionHides))
	TObjectPtr<USoundBase> RicochetSound;
};
