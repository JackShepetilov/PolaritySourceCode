// TacticalDeviceDefinition.h
// What a side rail device does and what it costs, as data. Docs/TacticalAttachment_Plan_2026-10-01.md.
//
// One asset per device, pointed at by UWeaponAttachmentDefinition::Device. The asset is stateless:
// everything that changes while the device runs (the charge, who has been lit for how long, who is
// slowed) lives in UTacticalDeviceComponent and its handler, so the same asset can sit on four
// players' guns at once.
//
// Each subclass names the handler that runs it (CreateHandler). A new kind of device is a new pair:
// a definition subclass here and a handler in TacticalDeviceHandlers.h.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TacticalDeviceDefinition.generated.h"

class UCurveFloat;
class UMaterialInterface;
class UNiagaraSystem;
class USoundBase;
class UStaticMesh;
class UTacticalDeviceComponent;
class UTacticalDeviceHandler;

/**
 * Base: the battery and the parts every device shares (sounds, a looping effect on the rail, the
 * overlay put on whatever it affects). Abstract: the subclass is what the device does.
 */
UCLASS(Abstract, BlueprintType)
class POLARITY_API UTacticalDeviceDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:

	/** Build the runtime object that runs this device for one owner. */
	virtual UTacticalDeviceHandler* CreateHandler(UTacticalDeviceComponent* Owner) const PURE_VIRTUAL(UTacticalDeviceDefinition::CreateHandler, return nullptr;);

	// ==================== Battery ====================
	//
	// The charge is a fraction, 0..1. It drains while the device runs and refills while it does not,
	// after a pause. A device switches on by itself while the owner aims down sights (author's call,
	// 2026-10-01), so the only thing stopping a player from running it for ever is this.

	/** Seconds a full charge lasts while running. 0 = running costs nothing by itself (the shield,
	 *  whose charge is spent by the damage it eats instead). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Battery", meta = (ClampMin = "0.0", Units = "s"))
	float ActiveDuration = 6.0f;

	/** Seconds from empty to full once refilling has started. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Battery", meta = (ClampMin = "0.1", Units = "s"))
	float RechargeDuration = 4.0f;

	/** Seconds after switching off before the charge starts coming back. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Battery", meta = (ClampMin = "0.0", Units = "s"))
	float RechargeDelay = 1.0f;

	/** The charge a device needs to switch on. Above zero so an emptied device does not flicker on
	 *  and off for a frame each time a sliver comes back under a held aim. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Battery", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinChargeToActivate = 0.2f;

	// ==================== Visual (prototype, to be finished by an art pass) ====================

	/** Tint of everything the prototype draws for this device. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	FLinearColor Color = FLinearColor(1.0f, 0.95f, 0.8f, 1.0f);

	/** Looping effect on the device while it runs, attached at the rail (or the muzzle when the
	 *  weapon has no SOCKET_Tactical). Optional. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TObjectPtr<UNiagaraSystem> ActiveFX;

	/** Put on every enemy the device is affecting right now (lit, frozen). Optional. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TObjectPtr<UMaterialInterface> AffectedOverlayMaterial;

	/** Played once on each enemy when the device starts affecting it, attached to it. Optional. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TObjectPtr<UNiagaraSystem> AffectedFX;

	// ==================== Audio ====================

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> ActivateSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> DeactivateSound;

	/** Loops while the device runs, attached to the owner. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> ActiveLoopSound;

	/** The charge ran out under a held aim. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> DepletedSound;
};

/**
 * Gun shield, after Gibraltar's: while aiming, a pool of damage in front of the owner. Hits from the
 * front come off the pool first; an empty pool breaks the shield until the charge comes back.
 * The charge IS the pool, in fractions of ShieldCapacity.
 */
UCLASS(BlueprintType)
class POLARITY_API UTacticalDevice_Shield : public UTacticalDeviceDefinition
{
	GENERATED_BODY()

public:

	UTacticalDevice_Shield();

	virtual UTacticalDeviceHandler* CreateHandler(UTacticalDeviceComponent* Owner) const override;

	/** Damage a full shield takes before it breaks. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shield", meta = (ClampMin = "1.0"))
	float ShieldCapacity = 75.0f;

	/** Half angle (degrees) around the aim, in the horizontal plane, that the shield covers. A hit
	 *  from outside it goes straight to armour and health. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Shield", meta = (ClampMin = "1.0", ClampMax = "180.0", Units = "deg"))
	float CoverHalfAngle = 70.0f;

	// ---------- Visual ----------

	/** Prototype: the shape drawn in front of the owner. An engine sphere squashed into a dish works. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TObjectPtr<UStaticMesh> ShieldMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TObjectPtr<UMaterialInterface> ShieldMaterial;

	/** World size of the shield mesh, cm: X depth, Y width, Z height. The mesh is scaled to it from
	 *  its own bounds, so any mesh fits. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	FVector ShieldSize = FVector(20.0f, 140.0f, 160.0f);

	/** How far in front of the owner's eyes the shield stands, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual", meta = (Units = "cm"))
	float ShieldDistance = 90.0f;

	/** Played where a hit landed on the shield. Optional. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TObjectPtr<UNiagaraSystem> ShieldHitFX;

	/** Played when the pool empties. Optional. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TObjectPtr<UNiagaraSystem> ShieldBreakFX;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> ShieldHitSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> ShieldBreakSound;
};

/**
 * Flashlight and laser: one device, two sets of numbers. Lights every enemy in a cone. A person is
 * only lit through the face (FaceBoneNames must be in the cone and in sight); something without
 * those bones (a drone) is lit through its body.
 *
 * What being lit does is the enemy's business (AShooterNPC::ApplyDazzle): a soldier fires blind and
 * backs off to cover, a kamikaze cannot start a strike, and one already striking flies on blind.
 * How long it lasts grows with how long the light was held on it.
 */
UCLASS(BlueprintType)
class POLARITY_API UTacticalDevice_Light : public UTacticalDeviceDefinition
{
	GENERATED_BODY()

public:

	UTacticalDevice_Light();

	virtual UTacticalDeviceHandler* CreateHandler(UTacticalDeviceComponent* Owner) const override;

	/** Reach, cm. Flashlight about 15 m, laser further. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Light", meta = (ClampMin = "100.0", Units = "cm"))
	float Range = 1500.0f;

	/** Half angle of the lit cone, degrees. Flashlight wide, laser narrow. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Light", meta = (ClampMin = "0.1", ClampMax = "60.0", Units = "deg"))
	float ConeHalfAngle = 12.0f;

	/** Bones that count as the face. The first one the mesh has is enough, any of them lit is a hit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Light")
	TArray<FName> FaceBoneNames;

	/** Seconds of effect for seconds the light has been held on the enemy (X: seconds lit, Y:
	 *  seconds dazzled). Read again every time the light lands, so holding it longer pushes the end
	 *  out. Empty = linear, DazzleSecondsPerLitSecond up to MaxDazzleDuration. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Light")
	TObjectPtr<UCurveFloat> DazzleDurationByLitTime;

	/** Without the curve: seconds of effect per second lit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Light", meta = (ClampMin = "0.0"))
	float DazzleSecondsPerLitSecond = 3.0f;

	/** Without the curve: the longest the effect gets. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Light", meta = (ClampMin = "0.0", Units = "s"))
	float MaxDazzleDuration = 6.0f;

	/** Light held on an enemy for less than this does nothing yet: a sweep across a room should not
	 *  dazzle everybody it brushes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Light", meta = (ClampMin = "0.0", Units = "s"))
	float MinLitTimeToDazzle = 0.15f;

	/** Light taken off an enemy for this long forgets the time it had been held there. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Light", meta = (ClampMin = "0.0", Units = "s"))
	float LitTimeMemory = 1.0f;

	/** A dazzled soldier's spread is multiplied by this. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Light", meta = (ClampMin = "1.0"))
	float DazzleSpreadMultiplier = 3.0f;

	// ---------- Visual ----------

	/** Real light, in candelas, so the prototype reads as a flashlight without any art. 0 = no light. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual", meta = (ClampMin = "0.0"))
	float LightIntensity = 3000.0f;

	/** Draw a beam (laser). The flashlight is the light alone. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	bool bDrawBeam = false;

	/** Prototype beam: a mesh stretched from the device to where the aim lands. An engine cylinder. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual", meta = (EditCondition = "bDrawBeam"))
	TObjectPtr<UStaticMesh> BeamMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual", meta = (EditCondition = "bDrawBeam"))
	TObjectPtr<UMaterialInterface> BeamMaterial;

	/** Beam thickness, cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual", meta = (EditCondition = "bDrawBeam", ClampMin = "0.05", Units = "cm"))
	float BeamWidth = 0.6f;

	/** Effect at the end of the beam, where the light lands. Optional. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TObjectPtr<UNiagaraSystem> EndPointFX;
};

/**
 * Freezer: a cone in front of the gun where enemies move and turn slower and every projectile
 * (theirs and the players') flies slower. Players are not slowed (author's call, 2026-10-01).
 */
UCLASS(BlueprintType)
class POLARITY_API UTacticalDevice_Freeze : public UTacticalDeviceDefinition
{
	GENERATED_BODY()

public:

	UTacticalDevice_Freeze();

	virtual UTacticalDeviceHandler* CreateHandler(UTacticalDeviceComponent* Owner) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Freeze", meta = (ClampMin = "100.0", Units = "cm"))
	float Range = 1200.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Freeze", meta = (ClampMin = "1.0", ClampMax = "80.0", Units = "deg"))
	float ConeHalfAngle = 25.0f;

	/** Walking speed of an enemy in the cone, as a share of its own. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Freeze", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float EnemyMoveMultiplier = 0.4f;

	/** Turn rate of an enemy in the cone, as a share of its own. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Freeze", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float EnemyTurnMultiplier = 0.4f;

	/** Time rate of flying enemies (drones drive their own flight, so the walk slow does not reach
	 *  them): the whole drone runs at this share of normal time while in the cone. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Freeze", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float FlyerTimeMultiplier = 0.4f;

	/** Speed of any projectile or bolt inside the cone, as a share of its own. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Freeze", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float ProjectileSpeedMultiplier = 0.25f;

	// ---------- Visual ----------

	/** Prototype: the cone itself. An engine cone, apex at the device, scaled to Range and the angle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TObjectPtr<UStaticMesh> ConeMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TObjectPtr<UMaterialInterface> ConeMaterial;
};
