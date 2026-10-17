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

/** [author, 2026-10-15] The flashlight and the laser have three: off, at work, and the active use
 *  (the strobe, the eye burner). One key cycles them. Devices without modes run while aiming. */
UENUM(BlueprintType)
enum class ETacticalDeviceMode : uint8
{
	Off,
	Normal,
	Active
};

/** Which of the two light devices this is. */
UENUM(BlueprintType)
enum class ETacticalLightKind : uint8
{
	Flashlight,
	Laser
};

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

	/** Switched by its own key through Off, Normal, Active instead of running while aiming. */
	virtual bool UsesModes() const { return false; }

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
	virtual bool UsesModes() const override { return true; }

	/** Flashlight or laser: the same light, two ways of using it (see the modes below). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Light")
	ETacticalLightKind Kind = ETacticalLightKind::Flashlight;

	// ==================== Modes [author, 2026-10-15] ====================
	//
	// Normal: a light to work by. The face it lands on is put off a little (the reaction starts over,
	// the aim is a little worse, a little stress), nobody is driven off. The soldiers who SEE the
	// light (the lamp itself, or the laser's beam) are alerted and turn to it; at night from much
	// further than by day (UWorldDarknessSubsystem).
	//
	// Active, flashlight: the strobe. Seen from further still, it piles on stress, and whoever it
	// holds in the face may not come closer: it backs off or goes to a corner away from the light,
	// firing at it badly (AShooterNPC::ApplyDazzle with the drive-off, ApplyKeepAway).
	// Active, laser: the eye burner. Up close, on the head bone exactly, it is the strobe. At any
	// range the beam shows everybody who sees it where the shooter is, and the soldier it is held on
	// (or by) is pinned: it stays down behind its cover and wants another corner
	// (UAIAccuracyComponent::NotePainted, FSTTask_ShooterPeek).

	/** Seconds a full charge lasts in Normal. 0: Normal costs nothing (and the charge refills). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modes", meta = (ClampMin = "0.0", Units = "s"))
	float NormalDuration = 0.0f;

	/** Normal: a face held in the light this long is put off (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modes|Normal", meta = (ClampMin = "0.0", Units = "s"))
	float NormalGlareSeconds = 1.5f;

	/** Normal: the put-off soldier's aim is this much worse while it lasts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modes|Normal", meta = (ClampMin = "1.0"))
	float NormalSpreadMultiplier = 1.5f;

	/** Normal: stress per second of light in the face. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modes|Normal", meta = (ClampMin = "0.0"))
	float NormalStressPerSecond = 0.1f;

	/** How far a soldier sees the light by day (cm)... */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modes|Seen", meta = (ClampMin = "0.0", Units = "cm"))
	float SeenRangeDay = 600.0f;

	/** ...and at night (cm). Lerped by the darkness. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modes|Seen", meta = (ClampMin = "0.0", Units = "cm"))
	float SeenRangeNight = 4000.0f;

	/** Active: the seen range times this. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modes|Seen", meta = (ClampMin = "1.0"))
	float ActiveSeenScale = 2.0f;

	/** Active: stress per second of light in the face (the strobe, the burner up close). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modes|Active", meta = (ClampMin = "0.0"))
	float ActiveStressPerSecond = 0.8f;

	/** Active: a soldier held in it may not come closer than it was, for this long after (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modes|Active", meta = (ClampMin = "0.0", Units = "s"))
	float ActiveKeepAwaySeconds = 2.0f;

	/** Active flashlight: flashes per second. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modes|Active", meta = (ClampMin = "1.0"))
	float StrobeRate = 12.0f;

	/** Active laser: closer than this, the head bone exactly, it burns like the strobe (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modes|Laser", meta = (ClampMin = "0.0", Units = "cm"))
	float BurnRange = 1000.0f;

	/** Active laser: the head bone it has to be on. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modes|Laser")
	FName BurnBone = FName("head");

	/** Active laser: the soldier the dot is within this of is pinned (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modes|Laser", meta = (ClampMin = "0.0", Units = "cm"))
	float PaintRadius = 150.0f;

	/** Active laser: stress per second for the pinned one. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modes|Laser", meta = (ClampMin = "0.0"))
	float PaintStressPerSecond = 0.5f;

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
