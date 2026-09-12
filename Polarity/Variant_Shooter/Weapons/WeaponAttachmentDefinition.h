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
#include "WeaponAttachmentDefinition.generated.h"

class UStaticMesh;
class UTexture2D;
class AShooterWeapon;

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
	Stock
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

	/** The square in the grid and the square beside the weapon draw this. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment")
	TObjectPtr<UTexture2D> Icon;

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
};
