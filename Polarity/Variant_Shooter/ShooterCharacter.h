// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PolarityCharacter.h"
#include "ShooterWeaponHolder.h"
#include "ApexMovementComponent.h"
#include "TutorialTypes.h"
#include "ShooterCharacter.generated.h"

class AShooterWeapon;
class UInputAction;
class UInputComponent;
class UPawnNoiseEmitterComponent;
class UWeaponRecoilComponent;
class UHitMarkerComponent;
class UMeleeAttackComponent;
class UChargeAnimationComponent;
class UPhysicsHandleComponent;
class UShooterUI;
class UUserWidget;
class UEMFChargeWidget;
class UCaptureReticleWidget;
class UUpgradeManagerComponent;
class UUpgradeRegistry;
class UAbilityComponent;
class UPlayerDeathSequenceComponent;
class UAudioComponent;
class UCurveFloat;
class UCameraShakeBase;
class UMaterialInterface;
class UStaticMeshComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class AEMFPhysicsProp;
class ADroppedRangedWeapon;
class ARiotShield;
struct FCheckpointData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBulletCountUpdatedDelegate, int32, MagazineSize, int32, Bullets);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDamagedDelegate, float, LifePercent, float, ArmorPercent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FHealthChangedDelegate, float, CurrentHP, float, MaxHP, float, LifePercent, float, ArmorPercent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDamageDirectionDelegate, float, AngleDegrees, float, Damage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHeatUpdatedDelegate, float, HeatPercent, float, DamageMultiplier);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FSpeedUpdatedDelegate, float, SpeedPercent, float, CurrentSpeed, float, MaxSpeed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPolarityChangedDelegate, uint8, NewPolarity, float, ChargeValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FChargeUpdatedDelegate, float, ChargeValue, uint8, Polarity);
// Extended charge delegate: TotalCharge, StableCharge, UnstableCharge, MaxStable, MaxUnstable, Polarity
DECLARE_DYNAMIC_MULTICAST_DELEGATE_SixParams(FChargeExtendedDelegate, float, TotalCharge, float, StableCharge, float, UnstableCharge, float, MaxStableCharge, float, MaxUnstableCharge, uint8, Polarity);
// Chromatic aberration intensity delegate (called every tick while effect is active)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDamageChromaticAberrationDelegate, float, Intensity);
// Melee weapon equip/unequip state delegate
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMeleeWeaponEquippedDelegate, bool, bEquipped, int32, RemainingHits, int32, MaxHits);

// Prop capture/launch delegates (fired by ChargeAnimationComponent)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPropCaptured, AActor*, CapturedActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPropLaunched, AActor*, LaunchedActor);

// Prop impact delegate (fired by EMFPhysicsProp when a launched prop damages NPCs)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPropImpact, AEMFPhysicsProp*, Prop, float, TotalDamage, int32, KillCount);

// Boss Finisher delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBossFinisherStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBossFinisherEnded);

/**
 * Per-turret aim telegraph entry. BP iterates ActiveAimingTurrets to compute its own
 * intensity (e.g. dot product of camera forward vs direction-to-turret, mapped through a curve).
 */
USTRUCT(BlueprintType)
struct FTurretAimInfo
{
	GENERATED_BODY()

	/** The turret currently aiming at the player */
	UPROPERTY(BlueprintReadOnly, Category = "Turret Aim")
	TObjectPtr<AActor> Turret = nullptr;

	/** Current aim progress of this turret (0..1) */
	UPROPERTY(BlueprintReadOnly, Category = "Turret Aim")
	float Progress = 0.0f;
};

// Broadcasts the full snapshot of turrets currently aiming at the player whenever it changes
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTargetedByTurretDelegate, const TArray<FTurretAimInfo>&, ActiveTurrets);

/**
 * Settings for Boss Finisher cinematic attack
 * All times are in seconds, distances in cm
 */
USTRUCT(BlueprintType)
struct FBossFinisherSettings
{
	GENERATED_BODY()

	/** Target point in world coordinates (set from Level BP) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	FVector TargetPoint = FVector::ZeroVector;

	/** Total time to travel from current position to target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float TotalTravelTime = 2.0f;

	/** Time before arrival to straighten trajectory (switch from curve to linear) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float StraightenTime = 0.5f;

	/** Time before arrival to start melee animation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float AnimationStartTime = 0.3f;

	/** Time to hang in the air after reaching target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float HangTime = 0.5f;

	/** Approach offset relative to target - defines "from which side" to approach
	 *  Example: (500, 0, 200) means approach from 500cm in front and 200cm above target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	FVector ApproachOffset = FVector(500.0f, 0.0f, 200.0f);
};

// Multicast delegates for melee button hold-detection (used by ChargedPunch and similar upgrades).
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMeleeChargeHoldStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMeleeChargeHoldReleased);

// Broadcast whenever the owned-weapon inventory changes (a weapon is added or removed). Param-less —
// subscribers re-enumerate via GetOwnedWeapons(). Used by the HUD ability bar to keep one persistent
// entry per owned ammo/melee weapon (not just the active one).
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWeaponInventoryChanged);

// Broadcast whenever the ACTIVE (held) weapon changes. NewWeapon == nullptr means the player is now
// unarmed. The HUD crosshair binds to this to swap dot<->crosshair and load the weapon's crosshair config.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActiveWeaponChanged, AShooterWeapon*, NewWeapon);

/**
 *  A player controllable first person shooter character
 *  Manages a weapon inventory through the IShooterWeaponHolder interface
 *  Manages health and death
 */
UCLASS(abstract)
class POLARITY_API AShooterCharacter : public APolarityCharacter, public IShooterWeaponHolder
{
	GENERATED_BODY()

	/** AI Noise emitter component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UPawnNoiseEmitterComponent* PawnNoiseEmitter;

	/** Advanced weapon recoil component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UWeaponRecoilComponent* RecoilComponent;

	/** Hit marker and kill confirm component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UHitMarkerComponent* HitMarkerComponent;

	/** Melee attack component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UMeleeAttackComponent* MeleeAttackComponent;

	/** Charge animation component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UChargeAnimationComponent* ChargeAnimationComponent;

	/** Holds a captured physics prop in front of this character.
	 *
	 *  A held prop used to be pulled by a spring force, which cannot keep it in the hand: a spring
	 *  loses to enough mass, gravity, friction and the player's own speed, so the prop trailed behind
	 *  and dropped. This is a constraint solved by the physics engine instead, so the prop keeps its
	 *  collision (it is stopped by walls and slides around corners) while still being held.
	 *
	 *  Whichever machine is holding the prop runs this, and that machine simulates the prop for real. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPhysicsHandleComponent> PropPhysicsHandle;

	/** Upgrade system manager - tracks and manages all active upgrades */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UUpgradeManagerComponent> UpgradeManager;

	/** Ability inventory + activation orchestrator (Titanfall-style abilities). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAbilityComponent> AbilityComponent;

	/** Configurable terminal death camera / pull / dismemberment presentation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPlayerDeathSequenceComponent> PlayerDeathSequenceComponent;

	// ==================== Melee Weapon FP Mesh ====================

	/** First-person body mesh shown instead of the normal FP mesh when melee weapon is equipped.
	 *  Attached to camera. Has its own AnimBP with access to character data (velocity, etc.).
	 *  Separate from FP mesh due to retargeting issues. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Melee Weapon", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> MeleeWeaponFPMesh;

	/** AnimBP class for MeleeWeaponFPMesh */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Weapon FP Mesh", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UAnimInstance> MeleeWeaponFPAnimClass;

	/** Location offset for MeleeWeaponFPMesh relative to camera */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Weapon FP Mesh", meta = (AllowPrivateAccess = "true"))
	FVector MeleeWeaponFPMeshOffset = FVector::ZeroVector;

	/** Rotation offset for MeleeWeaponFPMesh relative to camera */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee Weapon FP Mesh", meta = (AllowPrivateAccess = "true"))
	FRotator MeleeWeaponFPMeshRotation = FRotator::ZeroRotator;

	/** Cached pointer to the first UStaticMeshComponent child of MeleeWeaponFPMesh (added in Blueprint).
	 *  C++ controls its visibility and first-person rendering settings. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> MeleeWeaponStaticMesh;

protected:

	/** Fire weapon input action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* FireAction;

	/** Switch weapon input action (cycles through weapons) */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* SwitchWeaponAction;

	/** Switch weapon backward input action (cycles through weapons in reverse order). */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* SwitchWeaponBackAction;

	/** Set of weapon-switch input actions to listen for (e.g. IA_Slot1, IA_Slot2). WHICH weapon a key
	 *  selects is declared per-weapon via AShooterWeapon::SwitchAction — so several weapon classes can
	 *  share one key (only one is ever owned at a time). List here every action used by any weapon. */
	UPROPERTY(EditAnywhere, Category = "Input|Weapon Hotkeys")
	TArray<TObjectPtr<UInputAction>> WeaponSwitchActions;

	/** Aim down sights input action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* ADSAction;

	/** Melee attack input action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MeleeAction;

	/** Shield toggle input action (tap = raise/lower, hold = throw). */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* ShieldToggleAction;

	/** Activate the currently selected ability (press = TryActivate, release = OnButtonReleased).
	 *  Press/release split feeds the ability's own Tap-vs-Hold ActivationMode. */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* AbilityAction;

	/** Name of the first person mesh weapon socket */
	UPROPERTY(EditAnywhere, Category = "Weapons")
	FName FirstPersonWeaponSocket = FName("HandGrip_R");

	/** Name of the third person mesh weapon socket */
	UPROPERTY(EditAnywhere, Category = "Weapons")
	FName ThirdPersonWeaponSocket = FName("HandGrip_R");

	// ==================== Left Hand IK ====================

	/** Socket name on weapon mesh for left hand grip */
	UPROPERTY(EditAnywhere, Category = "Weapons|Left Hand IK")
	FName LeftHandGripSocket = FName("GripPoint_002");

	/** Offset applied to the left hand IK target relative to the socket */
	UPROPERTY(EditAnywhere, Category = "Weapons|Left Hand IK")
	FTransform LeftHandIKOffset = FTransform::Identity;

	/** Current left hand IK alpha (0 = no IK/detached, 1 = full IK). Blend this for wallrun etc. */
	float CurrentLeftHandIKAlpha = 1.0f;

	/** Target left hand IK alpha to interpolate towards */
	float TargetLeftHandIKAlpha = 1.0f;

	/** Interpolation speed for left hand IK alpha */
	UPROPERTY(EditAnywhere, Category = "Weapons|Left Hand IK")
	float LeftHandIKAlphaInterpSpeed = 10.0f;

	// ==================== Camera Follow ====================

	/** How much of the camera's own positional offsets (shake, raised shield) the FP mesh keeps.
	 *  The mesh is parented to the camera, so it inherits them at 1.0 automatically; lower values
	 *  give part of the movement back. 0.0 reproduces the pre-camera-attach behaviour. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "First Person View|Camera Follow", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CameraLocationFollowAlpha = 0.5f;

	/** Max distance to use for aim traces */
	UPROPERTY(EditAnywhere, Category = "Aim", meta = (ClampMin = 0, ClampMax = 100000, Units = "cm"))
	float MaxAimDistance = 10000.0f;

	// ==================== ADS State ====================

	/** True when player is holding ADS button */
	bool bWantsToAim = false;

	/** Current ADS alpha (0 = hip fire, 1 = fully aimed) */
	float CurrentADSAlpha = 0.0f;

	/** Base FOV of the camera (stored on BeginPlay) */
	float BaseCameraFOV = 90.0f;

	/** Base First Person FOV (stored on BeginPlay) */
	float BaseFirstPersonFOV = 70.0f;

	FVector BaseCameraLocation = FVector::ZeroVector;
	/** Max HP this character can have */
	UPROPERTY(EditAnywhere, Category = "Health")
	float MaxHP = 500.0f;

	/** Fraction of MaxHP the character starts with (1.0 = full, 0.5 = half). */
	UPROPERTY(EditAnywhere, Category = "Health", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float StartingHPPercent = 1.0f;

	/** Current HP remaining to this character.
	 *  Server-owned: only the authority writes it, everyone else receives it. */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentHP)
	float CurrentHP = 0.0f;

	/** Push the replicated health down to the UI on clients, and run the cosmetic side of dying
	 *  when the authority's HP reaches zero. */
	UFUNCTION()
	void OnRep_CurrentHP();

	/** Client-side latch so a repeated HP replication does not replay the death.
	 *  Never set on the authority, where Die() is driven from TakeDamage instead. */
	bool bHasPlayedLocalDeath = false;

	// ==================== Downed and revive ====================
	// Falling to zero HP no longer ends a player's run on its own. They go down instead: a ragdoll
	// on the floor that a teammate can pick back up. The run only ends when the whole team is down,
	// which is the rule ShouldRunEndOnThisDeath already encodes.

	/** True while this player is down and waiting to be picked up. Server decides, everyone shows
	 *  the ragdoll. Separate from HP so the two can arrive in any order without either side
	 *  guessing: HP hitting zero no longer means "play the death", this flag means "go limp". */
	UPROPERTY(ReplicatedUsing = OnRep_IsDowned)
	bool bIsDowned = false;

	UFUNCTION()
	void OnRep_IsDowned();

	/** Set only when the run really is over for this player, so the death presentation has a signal
	 *  of its own instead of being inferred from HP it cannot tell apart from being downed. */
	UPROPERTY(ReplicatedUsing = OnRep_TerminalDeath)
	bool bTerminalDeath = false;

	UFUNCTION()
	void OnRep_TerminalDeath();

	/** Whether the mesh is currently a ragdoll, so entering and leaving is never done twice. */
	bool bRagdollActive = false;

	/** Where the mesh sits on the capsule when it is not a ragdoll. Captured at BeginPlay, because
	 *  standing back up has to put it back exactly and simulating physics destroys the attachment. */
	FTransform MeshRelativeTransformOnSpawn;

public:

	/** Is this player down and awaiting a pick-up? */
	UFUNCTION(BlueprintPure, Category = "Coop|Downed")
	bool IsDowned() const { return bIsDowned; }

	/** Seconds a teammate must hold the capture button to bring this player back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coop|Downed", meta = (ClampMin = "0.1"))
	float ReviveHoldSeconds = 3.0f;

	/** How far a rescuer can be and still work on this player, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coop|Downed", meta = (ClampMin = "50.0", Units = "cm"))
	float ReviveRange = 250.0f;

	/** Fraction of max HP handed back on standing up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Coop|Downed", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float RevivePercent = 0.35f;

	/** Server: put this player on the floor instead of killing them. */
	void EnterDownedState();

	/** Server: stand them back up with RevivePercent of their HP. */
	void ReviveFromDowned();

	/** Ask the server to pick a downed teammate up. Reliable: dropping it wastes the hold the
	 *  rescuer already paid for. Checked server-side: the target has to actually be down, in range,
	 *  and the rescuer has to be able to act at all. */
	UFUNCTION(Server, Reliable)
	void Server_ReviveTeammate(AShooterCharacter* Target);

	/** Go limp, or get back up. Runs on every machine, driven by bIsDowned. */
	void ApplyDownedPresentation(bool bDowned);

	// Back to the section this block interrupted — everything below was written expecting it.
protected:

	// ==================== Armor (DOOM Eternal-style) ====================

	/** Maximum armor value */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	float MaxArmor = 250.0f;

	/** Current armor remaining (absorbs damage before health). Server-owned, like CurrentHP. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CurrentArmor, Category = "Armor")
	float CurrentArmor = 0.0f;

	UFUNCTION()
	void OnRep_CurrentArmor();

	// ==================== HP Regeneration ====================

	/** If true, HP regenerates over time based on movement speed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Regeneration")
	bool bEnableRegeneration = true;

	/** Base HP regeneration rate when stationary (HP/sec) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Regeneration", meta = (ClampMin = "0.0", EditCondition = "bEnableRegeneration"))
	float BaseRegenRate = 5.0f;

	/** Maximum HP regeneration rate at max speed (HP/sec) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Regeneration", meta = (ClampMin = "0.0", EditCondition = "bEnableRegeneration"))
	float MaxRegenRate = 50.0f;

	/** Speed considered maximum for regeneration scaling (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Regeneration", meta = (ClampMin = "100.0", EditCondition = "bEnableRegeneration"))
	float MaxSpeedForRegen = 1200.0f;

	/** Delay after taking damage before regeneration starts (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Regeneration", meta = (ClampMin = "0.0", EditCondition = "bEnableRegeneration"))
	float RegenDelayAfterDamage = 2.0f;

	/**
	 * Optional curve for speed-to-regen mapping.
	 * X = normalized speed (0-1), Y = regen multiplier (0-1).
	 * If null, linear interpolation is used.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health|Regeneration", meta = (EditCondition = "bEnableRegeneration"))
	TObjectPtr<UCurveFloat> SpeedToRegenCurve;

	/** Time since last damage was taken (for regen delay) */
	float TimeSinceLastDamage = 0.0f;

	/** Team ID for this character*/
	UPROPERTY(EditAnywhere, Category = "Team")
	uint8 TeamByte = 0;

	/** List of weapons picked up by the character.
	 *  Replicated because the server owns weapon creation now: without this a client's inventory
	 *  is empty, so pressing a switch key finds nothing to switch to and no request is ever sent. */
	UPROPERTY(ReplicatedUsing = OnRep_OwnedWeapons)
	TArray<AShooterWeapon*> OwnedWeapons;

	/** The first-person arms are shown only when the character owns something, and that decision
	 *  is made once at BeginPlay. On a client the inventory is still empty at that point, so the
	 *  arms were hidden and nothing ever brought them back. Re-run the check when it arrives. */
	UFUNCTION()
	void OnRep_OwnedWeapons();

	/** Weapon currently equipped and ready to shoot with.
	 *  Replicated so teammates can see which weapon is in your hands and see it change. */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeapon)
	TObjectPtr<AShooterWeapon> CurrentWeapon;

	/** Re-attach the third-person weapon meshes on machines that only observe this character. */
	UFUNCTION()
	void OnRep_CurrentWeapon();

	/** Reserve copies of yanked weapons held by the Bandolier upgrade. Hidden actors with
	 *  bullet state preserved; not in OwnedWeapons, not seen by FindWeaponOfType or the
	 *  switch keys. Promoted into OwnedWeapons when the active copy of the same class is
	 *  discarded (empty-mag throw, hold-throw). */
	UPROPERTY()
	TArray<AShooterWeapon*> ReserveWeapons;

	// ==================== Weapon Switch Animation ====================

	/** True while weapon switch animation is in progress */
	bool bIsWeaponSwitchInProgress = false;

	/** Weapon to switch to after lowering animation completes */
	UPROPERTY()
	TObjectPtr<AShooterWeapon> PendingWeapon = nullptr;

	/** Progress of weapon switch mesh transition (0-1) */
	float WeaponSwitchProgress = 0.0f;

	/** Camera-space Z offset contributed by the weapon switch lower/raise animation.
	 *  0 = weapon at rest, -WeaponSwitchLowerDistance = fully lowered. Consumed as a pose layer. */
	float WeaponSwitchMeshZOffset = 0.0f;

	/** How far down the FP mesh drops during a weapon switch (camera space, cm). */
	UPROPERTY(EditAnywhere, Category = "Weapons|Switch Animation", meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float WeaponSwitchLowerDistance = 100.0f;

	/** Camera-space Z offset driven by external systems. See SetFirstPersonMeshExternalZOffset. */
	float ExternalMeshZOffset = 0.0f;

	/** True during lowering phase, false during raising phase */
	bool bIsWeaponLowering = true;

	/** Time to lower weapon mesh during switch */
	UPROPERTY(EditAnywhere, Category = "Weapons|Switch Animation", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float WeaponSwitchLowerTime = 0.15f;

	/** Time to raise weapon mesh during switch */
	UPROPERTY(EditAnywhere, Category = "Weapons|Switch Animation", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float WeaponSwitchRaiseTime = 0.15f;

	/** True when a weapon switch was started without a target weapon (e.g. yank — pull is in flight).
	 *  Mesh holds at the lowered position until FinishWeaponSwitch(NewWeapon) is called. */
	bool bWeaponSwitchPausedAtBottom = false;

	/** Time before respawn after death */
	UPROPERTY(EditAnywhere, Category = "Death", meta = (ClampMin = 0, ClampMax = 10, Units = "s"))
	float RespawnTime = 2.0f;

	/** Duration of fade to black on death */
	UPROPERTY(EditAnywhere, Category = "Death", meta = (ClampMin = 0.1, ClampMax = 3.0, Units = "s"))
	float DeathFadeOutDuration = 0.5f;

	/** Duration of fade from black on respawn */
	UPROPERTY(EditAnywhere, Category = "Death", meta = (ClampMin = 0.1, ClampMax = 3.0, Units = "s"))
	float RespawnFadeInDuration = 0.3f;

	/** Color of death/respawn fade */
	UPROPERTY(EditAnywhere, Category = "Death")
	FLinearColor DeathFadeColor = FLinearColor::Black;

	FTimerHandle RespawnTimer;

	// ==================== UE4 Mesh System Settings ====================

	// ==================== UI Speed & Polarity ====================

	/** Maximum speed for UI normalization (cm/s) */
	UPROPERTY(EditAnywhere, Category = "UI", meta = (ClampMin = "100.0"))
	float MaxSpeedForUI = 1200.0f;

	/** Previous polarity state for change detection (0=Neutral, 1=Positive, 2=Negative) */
	uint8 PreviousPolarity = 0;

	// ==================== Charge Stability Thresholds ====================

	/** Charge threshold below which state is considered Stable (0-1 absolute) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Charge", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ChargeStableThreshold = 0.3f;

	/** Charge threshold above which state is considered Unstable (0-1 absolute) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI|Charge", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ChargeUnstableThreshold = 0.7f;

	// ==================== Charge Overlay Materials ====================

	/** If true, overlay material will be applied based on charge state */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Charge Overlay")
	bool bUseChargeOverlay = false;

	/** Overlay material to apply when charge is neutral (near zero) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Charge Overlay", meta = (EditCondition = "bUseChargeOverlay"))
	TObjectPtr<UMaterialInterface> NeutralChargeOverlayMaterial;

	/** Overlay material to apply when charge is positive */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Charge Overlay", meta = (EditCondition = "bUseChargeOverlay"))
	TObjectPtr<UMaterialInterface> PositiveChargeOverlayMaterial;

	/** Overlay material to apply when charge is negative */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals|Charge Overlay", meta = (EditCondition = "bUseChargeOverlay"))
	TObjectPtr<UMaterialInterface> NegativeChargeOverlayMaterial;

	// ==================== Mouse Input Tracking ====================

	/** Last frame's mouse delta for recoil sway */
	FVector2D LastMouseDelta = FVector2D::ZeroVector;

	// ==================== SFX|Footsteps ====================

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â® ÃƒÆ’Ã‚Â¸ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Footsteps")
	TObjectPtr<USoundBase> FootstepSound;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¸ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Footsteps", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float FootstepPitchMin = 0.95f;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¸ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Footsteps", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float FootstepPitchMax = 1.05f;

	/** ÃƒÆ’Ã†â€™ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¸ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Footsteps", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float FootstepVolume = 1.0f;

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â¸ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â¥ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Footsteps")
	TObjectPtr<USoundBase> CrouchFootstepSound;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¸ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â¥ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Footsteps", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float CrouchFootstepPitchMin = 0.9f;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¸ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â¥ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Footsteps", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float CrouchFootstepPitchMax = 1.1f;

	/** ÃƒÆ’Ã†â€™ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¸ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â¥ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Footsteps", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float CrouchFootstepVolume = 0.5f;

	// ==================== SFX|Slide ====================

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Slide")
	TObjectPtr<USoundBase> SlideStartSound;

	/** ÃƒÆ’Ã¢â‚¬â€œÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â© ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Slide")
	TObjectPtr<USoundBase> SlideLoopSound;

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Slide")
	TObjectPtr<USoundBase> SlideEndSound;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Slide", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float SlideSoundPitchMin = 0.95f;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Slide", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float SlideSoundPitchMax = 1.05f;

	/** ÃƒÆ’Ã†â€™ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Slide", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float SlideSoundVolume = 1.0f;

	// ==================== SFX|WallRun ====================

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â  wallrun */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|WallRun")
	TObjectPtr<USoundBase> WallRunStartSound;

	/** ÃƒÆ’Ã¢â‚¬â€œÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â© ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª wallrun */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|WallRun")
	TObjectPtr<USoundBase> WallRunLoopSound;

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ wallrun */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|WallRun")
	TObjectPtr<USoundBase> WallRunEndSound;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ wallrun */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|WallRun", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float WallRunSoundPitchMin = 0.95f;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ wallrun */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|WallRun", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float WallRunSoundPitchMax = 1.05f;

	/** ÃƒÆ’Ã†â€™ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ wallrun */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|WallRun", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float WallRunSoundVolume = 1.0f;

	// ==================== SFX|Jump ====================

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â® ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â¦ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Jump")
	TObjectPtr<USoundBase> JumpSound;

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â® ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â¦ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Jump")
	TObjectPtr<USoundBase> DoubleJumpSound;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â¦ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Jump", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float JumpSoundPitchMin = 0.95f;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â¦ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Jump", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float JumpSoundPitchMax = 1.05f;

	/** ÃƒÆ’Ã†â€™ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â¦ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Jump", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float JumpSoundVolume = 1.0f;

	// ==================== SFX|Land ====================

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Land")
	TObjectPtr<USoundBase> LandSound;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Land", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float LandSoundPitchMin = 0.9f;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â© pitch ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Land", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float LandSoundPitchMax = 1.1f;

	/** ÃƒÆ’Ã†â€™ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Land", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float LandSoundVolume = 1.0f;

	/** ÃƒÆ’Ã…â€™ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¼ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  (ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¬/ÃƒÆ’Ã‚Â±) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Land", meta = (ClampMin = "0.0"))
	float LandSoundMinFallSpeed = 300.0f;

	// ==================== SFX Audio Components ====================

	/** Audio component ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â¶ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â® ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	UPROPERTY()
	TObjectPtr<UAudioComponent> SlideLoopAudioComponent;

	/** Audio component ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â¶ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â® ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  wallrun */
	UPROPERTY()
	TObjectPtr<UAudioComponent> WallRunLoopAudioComponent;

	// ==================== SFX|Air Dash ====================

	/** Sound played when performing an air dash */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Air Dash")
	TObjectPtr<USoundBase> AirDashSound;

	/** Minimum pitch for air dash sound */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Air Dash", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float AirDashSoundPitchMin = 0.95f;

	/** Maximum pitch for air dash sound */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Air Dash", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float AirDashSoundPitchMax = 1.05f;

	/** Volume for air dash sound */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Air Dash", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float AirDashSoundVolume = 1.0f;

	// ==================== SFX|Mantle ====================

	/** Sound played when mantling */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Mantle")
	TObjectPtr<USoundBase> MantleSound;

	/** Volume for mantle sound */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Mantle", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MantleSoundVolume = 1.0f;

	// ==================== SFX|Weapon ====================

	/** Sound played when switching weapons */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Weapon")
	TObjectPtr<USoundBase> WeaponSwitchSound;

	/** Volume for weapon switch sound */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Weapon", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float WeaponSwitchSoundVolume = 0.8f;

	// ==================== SFX|Low Health ====================

	/** Warning sound played when health is critically low */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Low Health")
	TObjectPtr<USoundBase> LowHealthWarningSound;

	/** HP threshold (0-1) below which warning plays */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Low Health", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LowHealthThreshold = 0.25f;

	/** Interval between warning sounds (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Low Health", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float LowHealthWarningInterval = 2.0f;

	/** Volume for low health warning sound */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SFX|Low Health", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float LowHealthWarningVolume = 0.7f;

	/** Is currently in low health state */
	bool bIsLowHealth = false;

	/** Timer for low health warning sounds */
	float LowHealthWarningTimer = 0.0f;

	/** Backing flag for IsLowHealthDefenseActive — maintained by UUpgrade_LowHealthDefense.
	 *  Gates the nearby-enemy slow-mo effect. */
	bool bLowHealthDefenseActive = false;

	/** Enemy-bolt speed multiplier (1.0 default; lowered by the Low-Health Defense upgrade). */
	float EnemyBoltSlowMultiplier = 1.0f;

	// ==================== Damage Feedback ====================

	/** Camera shake to play when taking damage. Intensity is scaled by DamageToCameraShakeCurve */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Camera Shake")
	TSubclassOf<UCameraShakeBase> DamageCameraShake;

	/** Curve mapping damage amount to camera shake intensity (X = damage, Y = intensity multiplier 0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Camera Shake")
	TObjectPtr<UCurveFloat> DamageToCameraShakeCurve;

	/** Maximum camera shake scale when curve returns 1.0 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Camera Shake", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float MaxCameraShakeScale = 2.0f;

	// --- Impact Sounds by Damage Type ---

	/** Default impact sound when damage type is unknown */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Impact Sounds")
	TObjectPtr<USoundBase> DefaultImpactSound;

	/** Impact sound for ranged/bullet damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Impact Sounds")
	TObjectPtr<USoundBase> RangedImpactSound;

	/** Impact sound for melee damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Impact Sounds")
	TObjectPtr<USoundBase> MeleeImpactSound;

	/** Impact sound for explosion damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Impact Sounds")
	TObjectPtr<USoundBase> ExplosionImpactSound;

	/** Impact sound for EMF/electric damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Impact Sounds")
	TObjectPtr<USoundBase> EMFImpactSound;

	/** Volume multiplier for damage impact sounds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Impact Sounds", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float DamageImpactSoundVolume = 1.0f;

	// --- Chromatic Aberration ---

	/** Damage amount that results in maximum (1.0) chromatic aberration intensity. Higher damage is clamped to 1.0 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Chromatic Aberration", meta = (ClampMin = "1.0"))
	float MaxDamageForFullChromaticAberration = 100.0f;

	/** Duration of chromatic aberration effect (half sine wave: 0 → peak → 0) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Chromatic Aberration", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float ChromaticAberrationDuration = 0.5f;

	/** Chromatic aberration delegate - broadcasts intensity (0-1) every tick while effect is active */
	UPROPERTY(BlueprintAssignable, Category = "Damage Feedback|Chromatic Aberration")
	FDamageChromaticAberrationDelegate OnDamageChromaticAberration;

	// --- Chromatic Aberration State (internal) ---

	/** Base intensity calculated from damage (before sine modulation) */
	float ChromaticAberrationBaseIntensity = 0.0f;

	/** Elapsed time for current chromatic aberration effect */
	float ChromaticAberrationElapsedTime = 0.0f;

	/** True when chromatic aberration effect is active */
	bool bChromaticAberrationActive = false;

	// --- Melee Knockback (position interpolation, like NPC system) ---

	/** Whether to apply knockback when hit by melee attacks */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Knockback")
	bool bEnableMeleeKnockback = true;

	/** Distance to knock back player on melee hit (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Knockback", meta = (ClampMin = "0.0", EditCondition = "bEnableMeleeKnockback"))
	float MeleeKnockbackDistance = 200.0f;

	/** Duration of knockback interpolation (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Knockback", meta = (ClampMin = "0.05", ClampMax = "1.0", EditCondition = "bEnableMeleeKnockback"))
	float MeleeKnockbackDuration = 0.2f;

	/** If true, knockback can be cancelled by player actions (jump, dash, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Knockback", meta = (EditCondition = "bEnableMeleeKnockback"))
	bool bKnockbackCancellableByPlayer = true;

	// --- Knockback State (internal) ---

	/** True when player is being knocked back */
	bool bIsInKnockback = false;

	/** Start position for knockback interpolation */
	FVector KnockbackStartPosition = FVector::ZeroVector;

	/** Target position for knockback interpolation */
	FVector KnockbackTargetPosition = FVector::ZeroVector;

	/** Total duration of current knockback */
	float KnockbackTotalDuration = 0.0f;

	/** Elapsed time in current knockback */
	float KnockbackElapsedTime = 0.0f;

	// --- Damage Slowdown (ranged hit stacking) ---

	/** Whether to apply speed slowdown when hit by ranged attacks */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Slowdown")
	bool bEnableDamageSlowdown = true;

	/** Slowdown values per consecutive hit. Index 0 = first hit, index 1 = second hit, etc.
	 *  The last element applies to all hits beyond the array size.
	 *  Values are multiplied by DamageSlowdownMultiplier and subtracted from speed as a percentage.
	 *  Example: 0.5 with multiplier 0.6 = 30% speed reduction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Slowdown", meta = (EditCondition = "bEnableDamageSlowdown"))
	TArray<float> DamageSlowdownArray;

	/** Multiplier applied to values from DamageSlowdownArray.
	 *  Final speed reduction = ArrayValue * Multiplier (clamped 0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Slowdown", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableDamageSlowdown"))
	float DamageSlowdownMultiplier = 0.3f;

	/** Time window in seconds. If player receives 0 or 1 hits within this window, the hit counter resets */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage Feedback|Slowdown", meta = (ClampMin = "0.1", ClampMax = "5.0", EditCondition = "bEnableDamageSlowdown"))
	float DamageSlowdownWindow = 1.0f;

	// --- Damage Slowdown State (internal) ---

	/** Current number of ranged hits received within the time window */
	int32 DamageSlowdownHitCount = 0;

	/** Timer handle for resetting hit count */
	FTimerHandle DamageSlowdownResetTimerHandle;

	/** Apply damage slowdown based on current hit count */
	void ApplyDamageSlowdown();

	/** Called by timer - resets hit count if no recent hits */
	void OnDamageSlowdownTimerExpired();

	// ==================== VFX|PostProcess ====================

	/** Post process material instance for low health effect (vignette, desaturation, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|PostProcess")
	TObjectPtr<UMaterialInstanceDynamic> LowHealthPPMaterial;

	/** Post process material instance for high speed effect (motion blur, chromatic aberration, etc.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|PostProcess")
	TObjectPtr<UMaterialInstanceDynamic> HighSpeedPPMaterial;

	/** Parameter name for intensity in post process materials */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|PostProcess")
	FName PPIntensityParameterName = FName("Intensity");

	/** Speed threshold at which high speed effect starts (units/sec) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|PostProcess", meta = (ClampMin = "0.0"))
	float HighSpeedThreshold = 1500.0f;

	/** Speed at which high speed effect reaches maximum intensity (units/sec) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|PostProcess", meta = (ClampMin = "0.0"))
	float HighSpeedMaxThreshold = 3000.0f;

	/** Interpolation speed for post process effects (higher = snappier) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|PostProcess", meta = (ClampMin = "1.0", ClampMax = "20.0"))
	float PPInterpSpeed = 5.0f;

	/** Current low health PP intensity (internal) */
	float CurrentLowHealthPPIntensity = 0.0f;

	/** Current high speed PP intensity (internal) */
	float CurrentHighSpeedPPIntensity = 0.0f;

	/** Snapshot of turrets currently aiming at the player. BP consumes this (directly or via OnTargetedByTurret) to drive PP effects. */
	UPROPERTY(BlueprintReadOnly, Category = "Player|Telegraph")
	TArray<FTurretAimInfo> ActiveAimingTurrets;

	// ==================== VFX|Movement ====================

	/** Niagara system for air dash trail effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Movement")
	TObjectPtr<UNiagaraSystem> AirDashTrailFX;

	/** Niagara system for double jump burst effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Movement")
	TObjectPtr<UNiagaraSystem> DoubleJumpFX;

	/** Scale for double jump VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX|Movement", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float DoubleJumpFXScale = 1.0f;

	/** Active air dash trail component */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ActiveAirDashTrailComponent;

public:

	/** Bullet count updated delegate */
	FBulletCountUpdatedDelegate OnBulletCountUpdated;

	/** Legacy health-percent delegate. Kept for existing upgrade listeners; HUD uses OnHealthChanged. */
	FDamagedDelegate OnDamaged;

	/** Broadcast whenever current HP, max HP, or armor percent changes. */
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FHealthChangedDelegate OnHealthChanged;

	/** Damage direction delegate (angle in degrees relative to player forward, 0 = front, 90 = right, 180 = back, -90 = left) */
	FDamageDirectionDelegate OnDamageDirection;

	/** Heat level updated delegate */
	FHeatUpdatedDelegate OnHeatUpdated;

	/** Speed updated delegate */
	FSpeedUpdatedDelegate OnSpeedUpdated;

	/** Polarity changed delegate (fires only when sign changes) */
	FPolarityChangedDelegate OnPolarityChanged;

	/** Charge updated delegate (fires every tick) */
	FChargeUpdatedDelegate OnChargeUpdated;

	/** Extended charge delegate with stable/unstable breakdown (fires every tick) */
	FChargeExtendedDelegate OnChargeExtended;

	/** Melee weapon equipped/unequipped state (fires on weapon switch and weapon break) */
	FMeleeWeaponEquippedDelegate OnMeleeWeaponEquipped;

	/** Fired when the owned-weapon inventory changes (weapon added/removed). Subscribers re-enumerate
	 *  via GetOwnedWeapons(). Used by the HUD ability bar to maintain one entry per owned weapon. */
	UPROPERTY(BlueprintAssignable, Category = "Player|Events")
	FOnWeaponInventoryChanged OnWeaponInventoryChanged;

	/** Fired whenever the active (held) weapon changes; nullptr = now unarmed. Drives the HUD crosshair
	 *  (dot when unarmed, crosshair when armed) and carries the new weapon so the HUD can load its config. */
	UPROPERTY(BlueprintAssignable, Category = "Player|Events")
	FOnActiveWeaponChanged OnActiveWeaponChanged;

	/** Turret aim telegraph: broadcasts full list of turrets currently aiming at the player on every change.
	 *  BP computes per-turret intensity (e.g. dot product + curve) and drives PP settings accordingly. */
	UPROPERTY(BlueprintAssignable, Category = "Player|Events")
	FOnTargetedByTurretDelegate OnTargetedByTurret;

public:

	/** Constructor */
	AShooterCharacter();

	/** Get the melee mesh component (can return nullptr if not set up) */
	UFUNCTION(BlueprintPure, Category = "Character")
	USkeletalMeshComponent* GetMeleeMesh() const;

	/** Get the melee weapon FP body mesh (can return nullptr if not set up) */
	UFUNCTION(BlueprintPure, Category = "Character")
	USkeletalMeshComponent* GetMeleeWeaponFPMesh() const { return MeleeWeaponFPMesh; }

	/** Get the static mesh child of MeleeWeaponFPMesh (the sword model) */
	UStaticMeshComponent* GetMeleeWeaponStaticMesh() const { return MeleeWeaponStaticMesh; }

	/** Remove a melee weapon from inventory (called when weapon breaks).
	 *  Deactivates, removes from OwnedWeapons, switches to fallback, destroys. */
	UFUNCTION(BlueprintCallable, Category = "Weapons")
	void RemoveMeleeWeapon(AShooterWeapon* WeaponToRemove);

	/** Called by sniper turrets to report aim progress targeting this player.
	 *  bIsActive=false removes the turret from tracking (e.g. state change, turret destroyed). */
	UFUNCTION(BlueprintCallable, Category = "Player|Telegraph")
	void NotifyTurretTargeting(AActor* Turret, float Progress, bool bIsActive);

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Gameplay cleanup */
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;

	/** Per-frame updates */
	virtual void Tick(float DeltaTime) override;

	/** Override to track mouse input for recoil sway */
	virtual void DoAim(float Yaw, float Pitch) override;

public:

	/** Handle incoming damage. Authority only: a client that calls this changes nothing. */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ==================== Coop damage routing ====================

	/** Deal damage on behalf of this character. Applies it directly on the authority and reports
	 *  it to the server otherwise, so a client's shot actually lands instead of only killing its
	 *  own local copy of the target. Every weapon owned by a player must go through here. */
	void DealDamage(AActor* HitActor, float Damage, TSubclassOf<UDamageType> DamageTypeClass,
		AShooterWeapon* Weapon);

	/** Client to server half of DealDamage. Reliable: dropping a hit loses a kill.
	 *
	 *  The client still traces and computes the damage, because re-simulating it on the server would
	 *  cost a round trip of feel, but the server sanity-checks what arrives: the weapon has to belong
	 *  to the shooter, the target has to be within the weapon's reach, and the number is clamped to
	 *  what that weapon can possibly do. See the implementation for what is deliberately not checked
	 *  and why. */
	UFUNCTION(Server, Reliable)
	void Server_ReportDamage(AActor* HitActor, float Damage, TSubclassOf<UDamageType> DamageTypeClass,
		AShooterWeapon* Weapon);

	/** Ask the server for the authoritative projectile, at the transform this client already fired
	 *  its own stand-in from. Reliable: a lost one is a shot that never happened for anybody else.
	 *
	 *  The transform is taken on trust the same way a reported hit is, and checked the same way: the
	 *  weapon has to belong to this character and the muzzle has to be somewhere near it. */
	UFUNCTION(Server, Reliable)
	void Server_FireProjectile(AShooterWeapon* Weapon, const FTransform& ProjectileTransform,
		float ChargeMultiplier);

	/** Tell the server this client's weapon fired, so it can multicast the muzzle flash and sound
	 *  to everyone else. A miss carries no damage, so effects need their own way upstream.
	 *  Unreliable: cosmetic, and a lost one costs a single frame of flash. */
	UFUNCTION(Server, Unreliable)
	void Server_ReportWeaponFired(AShooterWeapon* Weapon);

	/** Same relay for the tracer. Carries the endpoints because only the shooter computed them. */
	UFUNCTION(Server, Unreliable)
	void Server_ReportBeamEffect(AShooterWeapon* Weapon, FVector Start, FVector End,
		float EnergyMultiplier, float OverrideBoltSpeed, float OverrideBoltSpeedVariance,
		float OverrideBoltLength, float OverrideRandomSeed);

	// ==================== Coop HUD ====================
	// A HUD belongs to a screen, and there is one screen per machine. The GameMode used to build a
	// single widget for player zero, which is the host: clients had no HUD at all. Each character
	// builds its own instead, and the only thing that has to cross the network is which class to
	// build — the GameMode blueprint still owns that setting.

	/** HUD class for this character's owner, handed down by the server. Replicated rather than read
	 *  from the GameMode directly because a client has no GameMode to read. */
	UPROPERTY(ReplicatedUsing = OnRep_HUDClass)
	TSubclassOf<UShooterUI> HUDClass;

	UFUNCTION()
	void OnRep_HUDClass();

	/** Build the HUD on this machine, if this machine is the one looking through this character's
	 *  eyes. Idempotent on purpose: the class and the controller arrive in either order and on
	 *  either side, so every one of those arrivals calls this and the first complete pair wins.
	 *  On the server the pawn is possessed AFTER BeginPlay, so BeginPlay alone is not enough. */
	void CreateLocalHUD();

	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_Controller() override;

	/** This machine's HUD widget. Null on every machine that is not driving this character. */
	UPROPERTY(Transient)
	TObjectPtr<UShooterUI> LocalHUD;

	/** Push a score change into this player's own HUD. The score is the team's, so the GameMode
	 *  sends it to every character rather than to a single widget it owns. */
	UFUNCTION(Client, Reliable)
	void Client_UpdateScore(uint8 ScoringTeam, int32 Score);

	/** Cover this player's screen while a run starts, and uncover it. Per player for the same
	 *  reason as the HUD: a client left uncovered watches the frames the cover exists to hide. */
	UFUNCTION(Client, Reliable)
	void Client_ShowLoadingCover(TSubclassOf<UUserWidget> CoverClass);

	UFUNCTION(Client, Reliable)
	void Client_DismissLoadingCover();

	/** Hand this player's machine the widget classes for the charge bars over props and the capture
	 *  reticle.
	 *
	 *  UEMFChargeWidgetSubsystem exists on every machine and registers props on every machine, but
	 *  nothing in C++ ever sets its WidgetClass: the GameMode blueprint does, and a GameMode exists
	 *  only on the server. So a client registered every prop and then held them all in the pending
	 *  queue forever, waiting for a class that was never coming — no charge bars, no grab prompt.
	 *  The server reads its own subsystem and passes the answer down. */
	UFUNCTION(Client, Reliable)
	void Client_ConfigureChargeWidgets(TSubclassOf<UEMFChargeWidget> InWidgetClass,
		TSubclassOf<UCaptureReticleWidget> InReticleClass);

	/** This machine's loading cover, if one is up. */
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> LocalLoadingCover;

	/** Ask the server to pull a dropped weapon toward this character and grant it on arrival.
	 *  Reliable: a lost request is a pickup that never happens and a button that looks broken.
	 *
	 *  The pull itself runs on the server and everyone watches it replicate, rather than the client
	 *  flying its own copy: the weapon only appears in the hand when the server grants it anyway
	 *  (capacity can refuse), so predicting the flight would buy nothing but a chance to disagree
	 *  about where the weapon is. First request wins; a second player asking for the same drop is
	 *  refused and told so in the log.
	 *
	 *  Carries the client's own reach for the same reason Server_CaptureProp does: range is a
	 *  product of the puller's charge, a player's charge is not replicated, and the server would
	 *  otherwise measure a remote player's reach as zero and refuse every pickup. Clamped to that
	 *  client's own search radius. */
	UFUNCTION(Server, Reliable)
	void Server_RequestWeaponPickup(ADroppedRangedWeapon* Drop, float ReportedCaptureRange);

	// ==================== Coop prop hold routing ====================
	// AEMFPhysicsProp::UpdateCaptureForces runs this client's own copy of the capture spring math
	// kinematically (no physics body — the server owns that) purely so a held prop feels held with
	// no round trip. These three RPCs are how the server finds out about it: it never runs the spring
	// math itself for a remote hold, it just believes what arrives here until release.

	/** Ask the server to start a remote hold on Prop. Reliable: a dropped capture request leaves the
	 *  prop looking captured on this screen and untouched everywhere else.
	 *
	 *  Checked the same way a reported hit is: the prop has to be capturable, the distance has to be
	 *  within capture range plus a round-trip margin, and nobody else can already be holding it.
	 *
	 *  The client has to send its own capture range, which it would otherwise never need to: range is
	 *  a product of the puller's charge and the prop's, and a player's charge is not replicated (it
	 *  lives in the EMF plugin's field component). The server therefore reads any remote player's
	 *  charge as zero, computed a range of zero, and rejected every held-transform that client sent
	 *  while also throwing at zero speed. The number is clamped to what that client's own
	 *  CaptureSearchRadius allows, in the same spirit as clamping reported damage. */
	UFUNCTION(Server, Reliable)
	void Server_CaptureProp(AEMFPhysicsProp* Prop, float ReportedCaptureRange);

	/** Ask the server to end this client's remote hold on Prop. Reliable: a dropped release leaves
	 *  the prop stuck kinematic, following nothing, forever. */
	UFUNCTION(Server, Reliable)
	void Server_ReleaseProp(AEMFPhysicsProp* Prop);

	/** Tell the server this client's shot electrified something. Reliable: charge is the whole game
	 *  loop, and a lost one is a shot that did nothing.
	 *
	 *  This needs its own way upstream because ionization carries no damage — the starting weapon
	 *  deals none at all by design — so Server_ReportDamage never fires for it and the server never
	 *  heard about a client charging anything. The client applies it locally too, for the instant
	 *  feedback, and the authority's value replicates back over the top.
	 *
	 *  Deliberately NOT re-checked here: the riot-shield rule, which needs the exact component that
	 *  was hit and is enforced on the shooter's machine, same trust model as a reported hit. */
	UFUNCTION(Server, Reliable)
	void Server_ReportIonization(AActor* Target, AShooterWeapon* Weapon);

	/** Ask the server to throw the prop this client is holding. Unlike the hold, the flight is not
	 *  predicted here — the server takes the prop back and flies it, so the hit, the damage and the
	 *  explosion are all decided in one place. Reliable: a dropped throw is a prop that never flies. */
	UFUNCTION(Server, Reliable)
	void Server_LaunchProp(AEMFPhysicsProp* Prop);

	/** Report where the held prop's local spring simulation put it this tick. Unreliable: this fires
	 *  every tick while holding, so a single lost update is invisible — the next one supersedes it.
	 *  Rejected outright if Prop does not currently list this character as its holder. */
	UFUNCTION(Server, Unreliable)
	void Server_UpdateHeldPropTransform(AEMFPhysicsProp* Prop, FVector Location, FRotator Rotation,
		FVector LinearVelocity);

	/** Where this character is aiming vertically, in degrees, for animation to use.
	 *  Your own pitch comes from the control rotation; a teammate's comes from the pitch the pawn
	 *  already replicates, which nothing in this project read, so remote characters always aimed
	 *  dead level no matter where their owner was looking. Feed this into the aim offset. */
	UFUNCTION(BlueprintPure, Category = "Coop|Aim")
	float GetAimPitchForAnimation() const;

	/** Swap to NewWeapon with no lower/raise animation. The whole equip in one step, so it can be
	 *  run identically on the owning machine and on the authority. */
	void EquipWeaponImmediate(AShooterWeapon* NewWeapon);

	/** Tell the client that fired what its shot actually did. A client applies no damage itself, so
	 *  without this its upgrades never see a kill and never see the real number: "on kill" effects
	 *  would work for the host alone, which is a progression asymmetry rather than a visual one.
	 *  Reliable: a dropped confirmation is a silently lost upgrade trigger. */
	UFUNCTION(Client, Reliable)
	void Client_ConfirmDamageDealt(AShooterWeapon* Weapon, AActor* HitActor, float ActualDamage, bool bKilled);

	/** Ask the server to equip a weapon this character owns. Which weapon is held has to be the
	 *  server's decision: CurrentWeapon and the weapon actor's hidden flag both replicate down, so
	 *  a purely local switch is overwritten and teammates see a stale gun. */
	UFUNCTION(Server, Reliable)
	void Server_RequestEquipWeapon(AShooterWeapon* Weapon);

	/** Broadcast the current health/armor snapshot to listeners. */
	void BroadcastHealthChanged();

	/** Returns true if player is dead (HP <= 0) */
	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const { return CurrentHP <= 0.0f; }

	/** True when this death is the one that ends the run: the server has looked at the whole team
	 *  and found nobody else still standing. Always false away from the server, which is what stops
	 *  a fallen client from travelling to the menu on its own and dropping out of the session.
	 *
	 *  This is the half of the agreed coop death rule that exists today. The other half, lying
	 *  downed until a teammate picks you up, is not built yet: a fallen player currently just stays
	 *  dead and watches until the run really ends. */
	bool ShouldRunEndOnThisDeath() const;

	/** Restore HP by the given amount (clamped to MaxHP). Updates UI. */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void RestoreHealth(float Amount);

	/** Add/remove maximum HP. Positive delta can optionally heal the newly-added capacity. Updates UI. */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void ModifyMaxHP(float DeltaMaxHP, bool bHealAddedMaxHP);

	/** Restore armor by the given amount (clamped to MaxArmor). Updates UI. */
	UFUNCTION(BlueprintCallable, Category = "Armor")
	void RestoreArmor(float Amount);

	/** Returns current armor value */
	UFUNCTION(BlueprintPure, Category = "Armor")
	float GetCurrentArmor() const { return CurrentArmor; }

	/** Returns maximum armor value */
	UFUNCTION(BlueprintPure, Category = "Armor")
	float GetMaxArmor() const { return MaxArmor; }

	/** Returns current HP value */
	UFUNCTION(BlueprintPure, Category = "Health")
	float GetCurrentHP() const { return CurrentHP; }

	/** Returns maximum HP value */
	UFUNCTION(BlueprintPure, Category = "Health")
	float GetMaxHP() const { return MaxHP; }

	/** True while the Low-Health Defense upgrade is owned AND the player is below its threshold.
	 *  Maintained by UUpgrade_LowHealthDefense; read by enemy weapons (AShooterWeapon) to switch
	 *  their hitscan into dodgeable traveling bolts. */
	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsLowHealthDefenseActive() const { return bLowHealthDefenseActive; }

	/** Called by the Low-Health Defense upgrade when its active state changes. */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetLowHealthDefenseActive(bool bActive) { bLowHealthDefenseActive = bActive; }

	/** Speed multiplier enemy bolts use against this player (1.0 = full/default speed). The
	 *  Low-Health Defense upgrade drives this below 1.0 (curve-scaled) so enemy bolts slow down
	 *  and become dodgeable as HP drops. Read by AShooterWeapon when spawning a bolt. */
	UFUNCTION(BlueprintPure, Category = "Health")
	float GetEnemyBoltSlowMultiplier() const { return EnemyBoltSlowMultiplier; }

	/** Set by the Low-Health Defense upgrade (1.0 when inactive / above threshold). */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetEnemyBoltSlowMultiplier(float Multiplier) { EnemyBoltSlowMultiplier = Multiplier; }

	/** Returns true if player is currently being knocked back */
	UFUNCTION(BlueprintPure, Category = "Damage")
	bool IsInKnockback() const { return bIsInKnockback; }

	/** Cancel current knockback (called when player performs action like jump/dash) */
	UFUNCTION(BlueprintCallable, Category = "Damage")
	void CancelKnockback();

protected:

	/** Apply knockback from melee damage */
	void ApplyMeleeKnockback(const FVector& KnockbackDirection, float Distance, float Duration);

	/** Update knockback position interpolation (called from Tick) */
	void UpdateKnockbackInterpolation(float DeltaTime);

	/** Play damage feedback effects (camera shake, sound) based on damage type and amount */
	void PlayDamageFeedback(float Damage, TSubclassOf<UDamageType> DamageTypeClass);

	/** Get impact sound for damage type */
	USoundBase* GetImpactSoundForDamageType(TSubclassOf<UDamageType> DamageTypeClass) const;

	/** Start chromatic aberration effect based on damage amount */
	void StartChromaticAberrationEffect(float Damage);

	/** Update chromatic aberration effect (called from Tick) */
	void UpdateChromaticAberration(float DeltaTime);

public:

	/** Handles start firing input */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoStartFiring();

	/** Handles stop firing input */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoStopFiring();

	/** Handles switch weapon input (cycles through weapons) */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoSwitchWeapon();

	/** Handles reverse switch weapon input (cycles through weapons in reverse order). */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoSwitchWeaponBackward();

	/** Shared weapon-cycle helper. Direction == +1 selects the next weapon, -1 the previous;
	 *  wraps around both ends of OwnedWeapons. Honors the same melee/charge/cast/in-progress guards. */
	void CycleWeapon(int32 Direction);

	/** Press handler for SwitchWeaponAction — starts hold timer for throw-yanked detection. */
	void OnSwitchWeaponPressed();

	/** Release handler for SwitchWeaponAction — if hold threshold not yet fired, performs
	 *  normal tap-swap. If hold already fired, no-op (throw already happened). */
	void OnSwitchWeaponReleased();

	/** Timer callback fired after YankSwapHoldThreshold seconds of holding the swap key.
	 *  Throws the currently-owned yanked weapon forward as an impact-stunning physics actor. */
	UFUNCTION()
	void OnSwapHoldThresholdFired();

	/** Handles a weapon-switch key: equips the OWNED weapon whose SwitchAction matches the pressed
	 *  action (if not already equipped). Several weapon classes may map to one action; only one is owned. */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoWeaponSwitchByAction(UInputAction* Action);

	/** Returns true if weapon switch is currently in progress */
	UFUNCTION(BlueprintPure, Category = "Weapons")
	bool IsWeaponSwitchInProgress() const { return bIsWeaponSwitchInProgress; }

	/** Handles start ADS input */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoStartADS();

	/** Handles stop ADS input */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoStopADS();

	/** Handles melee attack input */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoMeleeAttack();

	/**
	 * Called on melee button press (ETriggerEvent::Started). Broadcasts OnMeleeChargeHoldStarted.
	 * Does NOT trigger the regular melee swing — that still runs from DoMeleeAttack on Triggered.
	 * This hook exists for hold-based upgrades like ChargedPunch.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoMeleePressed();

	/**
	 * Called on melee button release (ETriggerEvent::Completed). Broadcasts OnMeleeChargeHoldReleased.
	 * Used by hold-based upgrades like ChargedPunch to finalize a charged action.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoMeleeReleased();

	/** Called on ability button press (ETriggerEvent::Started). Calls AbilityComponent->TryActivate(). */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoAbilityPressed();

	/** Called on ability button release (ETriggerEvent::Completed). Calls AbilityComponent->OnButtonReleased(). */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoAbilityReleased();

	// ==================== Melee Hold Input Delegates ====================
	// Used by upgrades that need to know when the melee button is pressed/released,
	// independent of the regular tap-to-swing flow. Subscribers do their own
	// hold-time accounting (e.g. ChargedPunch waits for MinHoldTime before charging).
	// Delegate types declared on file scope below — see FOnMeleeChargeHoldStarted /
	// FOnMeleeChargeHoldReleased near the top of this header.

	/** Broadcast when the melee button is pressed (Started). */
	UPROPERTY(BlueprintAssignable, Category = "Melee|Input")
	FOnMeleeChargeHoldStarted OnMeleeChargeHoldStarted;

	/** Broadcast when the melee button is released (Completed). */
	UPROPERTY(BlueprintAssignable, Category = "Melee|Input")
	FOnMeleeChargeHoldReleased OnMeleeChargeHoldReleased;

	/** Returns true if currently aiming down sights */
	UFUNCTION(BlueprintPure, Category = "ADS")
	bool IsAiming() const { return bWantsToAim; }

	/** Returns current ADS alpha (0-1) */
	UFUNCTION(BlueprintPure, Category = "ADS")
	float GetADSAlpha() const { return CurrentADSAlpha; }

	/** Returns the recoil component */
	UFUNCTION(BlueprintPure, Category = "Recoil")
	UWeaponRecoilComponent* GetRecoilComponent() const { return RecoilComponent; }

	/** Returns the hit marker component */
	UFUNCTION(BlueprintPure, Category = "Hit Marker")
	UHitMarkerComponent* GetHitMarkerComponent() const { return HitMarkerComponent; }

	/** Returns the melee attack component */
	UFUNCTION(BlueprintPure, Category = "Melee")
	UMeleeAttackComponent* GetMeleeAttackComponent() const { return MeleeAttackComponent; }

	/** Returns the charge animation component */
	UFUNCTION(BlueprintPure, Category = "Charge")
	UChargeAnimationComponent* GetChargeAnimationComponent() const { return ChargeAnimationComponent; }

	/** Returns the constraint that holds a captured prop. See PropPhysicsHandle. */
	UPhysicsHandleComponent* GetPropPhysicsHandle() const { return PropPhysicsHandle; }

	/** Returns the upgrade manager component */
	UFUNCTION(BlueprintPure, Category = "Upgrades")
	UUpgradeManagerComponent* GetUpgradeManager() const { return UpgradeManager; }

	/** Returns the ability component (multi-slot ability inventory + activation). */
	UFUNCTION(BlueprintPure, Category = "Abilities")
	UAbilityComponent* GetAbilityComponent() const { return AbilityComponent; }

	/** Returns the currently equipped weapon */
	UFUNCTION(BlueprintPure, Category = "Weapons")
	AShooterWeapon* GetCurrentWeapon() const { return CurrentWeapon; }

	/** Returns the input action that activates the ability (for HUD keybind hints; may be null). */
	UFUNCTION(BlueprintPure, Category = "Abilities")
	UInputAction* GetAbilityAction() const { return AbilityAction; }

	/** Read-only access to the owned-weapon inventory (excludes hidden Bandolier reserve copies). */
	const TArray<AShooterWeapon*>& GetOwnedWeapons() const { return OwnedWeapons; }

	/** Returns the input action that switches/equips the given owned weapon: its own SwitchAction if set,
	 *  else the cycle SwitchWeaponAction. Null if neither is set. Used for the HUD "press X to equip" hint. */
	UFUNCTION(BlueprintPure, Category = "Weapons")
	UInputAction* GetSwitchInputActionForWeapon(const AShooterWeapon* Weapon) const;

	/** Sets the target left hand IK alpha (0 = detached, 1 = fully attached). Use for wallrun, melee, etc. */
	UFUNCTION(BlueprintCallable, Category = "Weapons|Left Hand IK")
	void SetLeftHandIKAlpha(float Alpha) { TargetLeftHandIKAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f); }

	/** Camera-space Z offset on the FP mesh contributed by systems outside the character
	 *  (MeleeAttackComponent's weapon-lower). Routed through the pose pipeline so the mesh
	 *  transform keeps a single writer instead of components fighting over it each tick. */
	UFUNCTION(BlueprintCallable, Category = "First Person View")
	void SetFirstPersonMeshExternalZOffset(float ZOffset) { ExternalMeshZOffset = ZOffset; }

	/** Current external FP mesh Z offset. See SetFirstPersonMeshExternalZOffset. */
	UFUNCTION(BlueprintPure, Category = "First Person View")
	float GetFirstPersonMeshExternalZOffset() const { return ExternalMeshZOffset; }

	/** DEPRECATED no-op. Used to disable the FP Control Rig and enable a spine Modify Bone so that
	 *  two-hand montages would follow the camera pitch. The FP mesh is parented to the camera now,
	 *  so montages follow it for free and there is nothing to blend. Kept as a stub while the
	 *  remaining call sites (ChargeAnimationComponent, Blueprints) are cleaned up. */
	UFUNCTION(BlueprintCallable, Category = "Animation|FP Montage Alpha", meta = (DeprecatedFunction, DeprecationMessage = "FP mesh follows the camera directly; this no longer does anything."))
	void SetFPMontageAlpha(float Target, float BlendTime);

	/** DEPRECATED, always 0. See SetFPMontageAlpha. */
	UFUNCTION(BlueprintPure, Category = "Animation|FP Montage Alpha", meta = (DeprecatedFunction, DeprecationMessage = "FP mesh follows the camera directly; this is always 0."))
	float GetFPMontageAlpha() const { return 0.0f; }

	/** Gets the current left hand IK alpha */
	UFUNCTION(BlueprintPure, Category = "Weapons|Left Hand IK")
	float GetLeftHandIKAlpha() const { return CurrentLeftHandIKAlpha; }

protected:

	/** Update ADS state and apply effects */
	void UpdateADS(float DeltaTime);

	/** Start weapon switch to specified weapon (with lowering/raising animation) */
	void StartWeaponSwitch(AShooterWeapon* NewWeapon);

	/** Update weapon switch animation */
	void UpdateWeaponSwitch(float DeltaTime);

	/** Called when weapon lowering completes - performs actual weapon swap */
	void OnWeaponSwitchLowered();

	/** Called when weapon raising completes - ends switch process */
	void OnWeaponSwitchRaised();

	/** Updates first person mesh visibility based on weapon ownership */
	void UpdateFirstPersonMeshVisibility();

	/** Checks if a bone is a child of (or is) any of the specified root bones */
	/** Update HP regeneration based on movement speed */
	void UpdateRegeneration(float DeltaTime);

	/** Update overlay material based on current charge polarity */
	void UpdateChargeOverlay(uint8 NewPolarity);

	/** Adds the shooter-specific FP mesh pose layers: per-weapon base pose, ADS, recoil,
	 *  weapon-switch lower/raise and the camera-follow compensation. */
	virtual void AccumulateFirstPersonPose(float DeltaTime, FVector& Location, FRotator& Rotation) override;

	/** Called when melee attack hits something */
	UFUNCTION()
	void OnMeleeHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage);

	// ==================== SFX Functions ====================

	/** ÃƒÆ’Ã¢â‚¬Å¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â® ÃƒÆ’Ã‚Â¸ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â  */
	UFUNCTION(BlueprintCallable, Category = "SFX")
	void PlayFootstepSound();

	/** ÃƒÆ’Ã¢â‚¬Å¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â¸ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â¢ ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â¥ */
	UFUNCTION(BlueprintCallable, Category = "SFX")
	void PlayCrouchFootstepSound();

	/** ÃƒÆ’Ã¢â‚¬Å¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	void PlaySlideStartSound();

	/** ÃƒÆ’Ã¢â‚¬Å¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	void PlaySlideEndSound();

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â¶ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â© ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	void StartSlideLoopSound();

	/** ÃƒÆ’Ã…Â½ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â¶ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â© ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	void StopSlideLoopSound();

	/** ÃƒÆ’Ã¢â‚¬Å¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â  wallrun */
	void PlayWallRunStartSound();

	/** ÃƒÆ’Ã¢â‚¬Å¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ wallrun */
	void PlayWallRunEndSound();

	/** ÃƒÆ’Ã¢â‚¬Â¡ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â¶ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â© ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª wallrun */
	void StartWallRunLoopSound();

	/** ÃƒÆ’Ã…Â½ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¼ ÃƒÆ’Ã‚Â¶ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â© ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª wallrun */
	void StopWallRunLoopSound();

	/** ÃƒÆ’Ã¢â‚¬Å¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â¦ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  */
	UFUNCTION(BlueprintCallable, Category = "SFX")
	void PlayJumpSound(bool bIsDoubleJump);

	/** ÃƒÆ’Ã¢â‚¬Å¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â³ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ */
	void PlayLandSound(float FallSpeed);

	// ==================== SFX Delegate Handlers ====================

	/** ÃƒÆ’Ã…Â½ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	UFUNCTION()
	void OnSlideStarted_SFX();

	/** ÃƒÆ’Ã…Â½ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â©ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â  */
	UFUNCTION()
	void OnSlideEnded_SFX();

	/** ÃƒÆ’Ã…Â½ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â  wallrun */
	UFUNCTION()
	void OnWallRunStarted_SFX(EWallSide Side);

	/** ÃƒÆ’Ã…Â½ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ wallrun */
	UFUNCTION()
	void OnWallRunEnded_SFX();

	/** ÃƒÆ’Ã…Â½ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â·ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â±ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¡ÃƒÆ’Ã‚Â»ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ ÃƒÆ’Ã‚Â¯ÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â¬ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â­ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¿ */
	UFUNCTION()
	void OnLanded_SFX(const FHitResult& Hit);

	/** ÃƒÆ’Ã‚ÂÃƒÆ’Ã‚Â°ÃƒÆ’Ã‚Â¨ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¿ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Âª ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â¬ ApexMovementComponent */
	void BindMovementSFXDelegates();

	/** ÃƒÆ’Ã…Â½ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â¢ÃƒÆ’Ã‚Â¿ÃƒÆ’Ã‚Â§ÃƒÆ’Ã‚ÂªÃƒÆ’Ã‚Â  ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â² ÃƒÆ’Ã‚Â¤ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â«ÃƒÆ’Ã‚Â¥ÃƒÆ’Ã‚Â£ÃƒÆ’Ã‚Â ÃƒÆ’Ã‚Â²ÃƒÆ’Ã‚Â®ÃƒÆ’Ã‚Â¢ ApexMovementComponent */
	void UnbindMovementSFXDelegates();

	// ==================== New Movement SFX/VFX Handlers ====================

	/** Handler for jump events from ApexMovementComponent */
	UFUNCTION()
	void OnJumpPerformed_Handler(bool bIsDoubleJump);

	/** Handler for mantle start event */
	UFUNCTION()
	void OnMantleStarted_Handler();

	/** Handler for air dash start event - plays sound and starts VFX */
	UFUNCTION()
	void OnAirDashStarted_Handler();

	/** Handler for air dash end event - stops VFX */
	UFUNCTION()
	void OnAirDashEnded_Handler();

	/** Play air dash sound */
	void PlayAirDashSound();

	/** Play mantle sound */
	void PlayMantleSound();

	/** Play weapon switch sound */
	void PlayWeaponSwitchSound();

	/** Update low health warning state and play warning sounds */
	void UpdateLowHealthWarning(float DeltaTime);

	/** Update post process effects based on health and speed */
	void UpdatePostProcessEffects(float DeltaTime);

	/** Spawn double jump VFX at character feet */
	void SpawnDoubleJumpVFX();

	/** Start air dash trail VFX attached to character */
	void StartAirDashTrailVFX();

	/** Stop air dash trail VFX */
	void StopAirDashTrailVFX();

	/** Grants the starting weapon + restores air abilities on the opening launch's first landing. */
	virtual void Landed(const FHitResult& Hit) override;

	/** Air control saved at launch, restored on landing (run-start toss keeps a pure-physics arc). */
	float SavedAirControl = 0.f;

public:

	//~Begin IShooterWeaponHolder interface

	/** Attaches a weapon's meshes to the owner */
	virtual void AttachWeaponMeshes(AShooterWeapon* Weapon) override;

	/** Plays the firing montage for the weapon */
	virtual void PlayFiringMontage(UAnimMontage* Montage) override;

	/** Applies weapon recoil to the owner */
	virtual void AddWeaponRecoil(float Recoil) override;

	/** Updates the weapon's HUD with the current ammo count */
	virtual void UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize) override;

	/** Calculates and returns the aim location for the weapon */
	virtual FVector GetWeaponTargetLocation() override;

	/** Gives a weapon of this class to the owner */
	virtual void AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass) override;

	/** Activates the passed weapon */
	virtual void OnWeaponActivated(AShooterWeapon* Weapon) override;

	/** Deactivates the passed weapon */
	virtual void OnWeaponDeactivated(AShooterWeapon* Weapon) override;

	/** Notifies the owner that the weapon cooldown has expired and it's ready to shoot again */
	virtual void OnSemiWeaponRefire() override;

	/** Notifies the owner that a hit was registered */
	virtual void OnWeaponHit(const FVector& HitLocation, const FVector& HitDirection, float Damage, bool bHeadshot, bool bKilled, AActor* HitActor = nullptr) override;

	//~End IShooterWeaponHolder interface

	/** Find an owned weapon of the given class (or subclass). Returns nullptr if not found. */
	AShooterWeapon* FindWeaponOfType(TSubclassOf<AShooterWeapon> WeaponClass) const;

	/** Count weapons of WeaponClass in OwnedWeapons + ReserveWeapons that are yank-acquired
	 *  (bHasLimitedAmmo). Starter weapons (infinite ammo) are excluded — they don't compete
	 *  for Bandolier capacity. Used by the Bandolier-pickup branch to decide reserve-add vs
	 *  ammo-spill. */
	int32 CountYankedCopiesOfClass(TSubclassOf<AShooterWeapon> WeaponClass) const;

	/** Spawn a hidden, non-activated AShooterWeapon of WeaponClass and put it in ReserveWeapons.
	 *  Tags it as bWasYanked + bHasLimitedAmmo with the provided BulletCount and SourceDropClass.
	 *  Used by DroppedRangedWeapon::CompletePull when Bandolier capacity allows another copy. */
	AShooterWeapon* AddYankedReserveCopy(TSubclassOf<AShooterWeapon> WeaponClass,
		TSubclassOf<class ADroppedRangedWeapon> SourceDropClass,
		int32 BulletCount);

	/** Spread BulletsToSpill across all yanked copies of WeaponClass (CurrentWeapon first if it
	 *  matches, then other yanked in OwnedWeapons, then ReserveWeapons). Stops once Bullets
	 *  reaches 0 or all are full. Excess is dropped on the floor. Used by the Bandolier-overflow
	 *  branch of CompletePull. */
	void SpillBulletsIntoYankedCopiesOfClass(TSubclassOf<AShooterWeapon> WeaponClass, int32 BulletsToSpill);

	/** If a reserve copy of WeaponClass exists, move it from ReserveWeapons → OwnedWeapons,
	 *  un-hide it, and return it as a candidate equip target. Returns nullptr otherwise.
	 *  Called from the throw flow (OnYankThrowLowerNotify / DiscardYankedWeaponShared) so the
	 *  reserve of the same class is preferred over a fallback non-yanked weapon. */
	AShooterWeapon* PromoteReserveCopyOfClass(TSubclassOf<AShooterWeapon> WeaponClass);

	/** Add a weapon of this class with the same animated lower→swap→raise transition that
	 *  Q-switch uses. If the player is unarmed (no CurrentWeapon), falls back to instant equip.
	 *  If a paused-at-bottom switch is already in progress (BeginWeaponLower was called), this
	 *  routes through FinishWeaponSwitch so the swap+raise happens immediately without
	 *  re-lowering. Used by yank pickup (DroppedRangedWeapon::CompletePull). */
	UFUNCTION(BlueprintCallable, Category = "Weapons")
	AShooterWeapon* AddWeaponClassAnimated(const TSubclassOf<AShooterWeapon>& WeaponClass);

	// ==================== Run Start Launch ====================

	/** Weapon granted (with the animated draw) when the player first lands at the start of a run.
	 *  Single weapon for now; hub loadout selection will set this later. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Run Start")
	TSubclassOf<AShooterWeapon> StartingWeaponClass;

	/** True from the opening sea-toss launch until the first landing. */
	UPROPERTY(BlueprintReadOnly, Category = "Run Start")
	bool bRunLaunchInProgress = false;

	/** Tosses the character with the given world velocity and suppresses air abilities + air control
	 *  until landing, so the opening arc is deterministic. */
	UFUNCTION(BlueprintCallable, Category = "Run Start")
	void BeginRunLaunch(const FVector& LaunchVelocity);

	/** Grants StartingWeaponClass and plays the procedural weapon-raise (smooth draw, same motion as the
	 *  melee "show weapon" recovery). Called from Landed() on toss maps and from BP after the boss intro
	 *  cutscene. No-op if StartingWeaponClass is unset. */
	UFUNCTION(BlueprintCallable, Category = "Run Start")
	void EquipStartingWeaponAnimated();

	/** Begin only the lower phase of a weapon switch. Mesh smoothly drops and pauses at the
	 *  bottom waiting for FinishWeaponSwitch(NewWeapon). Used when the new weapon is in flight
	 *  (yank pull) — lets the lower animation run in parallel with the pull so the new weapon
	 *  arrives at an already-empty hand. No-op if a switch is already in progress or if unarmed. */
	UFUNCTION(BlueprintCallable, Category = "Weapons")
	void BeginWeaponLower();

	/** Complete a paused weapon switch: instantly swap to NewWeapon (off-camera at the bottom)
	 *  and play the raise animation. NewWeapon must be in OwnedWeapons. No-op if not paused. */
	UFUNCTION(BlueprintCallable, Category = "Weapons")
	void FinishWeaponSwitch(AShooterWeapon* NewWeapon);

	/** Discards any yanked weapon in OwnedWeapons via the throw animation flow:
	 *  plays ThrowMontage on FP arms; AnimNotifies in the montage drive the actual gameplay
	 *  steps (mesh hide + dropped spawn = mid-throw notify; lower hands + switch to replacement
	 *  = end notify). If no ThrowMontage is configured, falls back to instant discard.
	 *  Strict rule: at most one yanked weapon in inventory at a time. */
	UFUNCTION(BlueprintCallable, Category = "Weapons|Yank")
	void ThrowYankedWeaponIfAny();

	void ThrowYankedWeaponIfEmpty();

private:
	/** Yanked weapon currently being thrown via animation. Set by ThrowYankedWeaponIfAny,
	 *  consumed by OnYankThrowDiscardNotify (hide+spawn) and OnYankThrowLowerNotify (switch). */
	TWeakObjectPtr<AShooterWeapon> PendingYankThrowWeapon;

	/** Timer that destroys the orphaned yanked weapon actor after BeginWeaponLower +
	 *  FinishWeaponSwitch has had time to complete its animation. */
	FTimerHandle YankActorDestroyTimer;

public:
	/** Called by AnimNotify_YankThrowDiscard at the moment in the ThrowMontage where the
	 *  weapon should leave the hand. Spawns dropped version at the FP weapon mesh's exact
	 *  world position and hides the held weapon's meshes. */
	UFUNCTION(BlueprintCallable, Category = "Weapons|Yank")
	void OnYankThrowDiscardNotify();

	/** Called by AnimNotify_YankThrowLower at the moment in the ThrowMontage where the empty
	 *  hands should start lowering for weapon switch. Triggers BeginWeaponLower +
	 *  FinishWeaponSwitch(replacement) and schedules the orphaned yanked actor for destruction. */
	UFUNCTION(BlueprintCallable, Category = "Weapons|Yank")
	void OnYankThrowLowerNotify();

private:
	UFUNCTION()
	void DestroyOrphanedYankActor();

public:

	// ==================== Riot Shield API ====================

	/** True when the player currently owns a shield (regardless of raised/lowered state). */
	UFUNCTION(BlueprintPure, Category = "Shield")
	bool HasShield() const { return EquippedShield != nullptr; }

	/** Returns the currently equipped shield actor (or nullptr). */
	UFUNCTION(BlueprintPure, Category = "Shield")
	ARiotShield* GetEquippedShield() const { return EquippedShield; }

	/** Place a shield in the shield slot, attach it to the camera, and start raised.
	 *  Called by ARiotShieldPickup::OnOverlap. */
	UFUNCTION(BlueprintCallable, Category = "Shield")
	void EquipShield(ARiotShield* Shield);

	/** Shield-toggle key (tap-only): raise ↔ lower. Throw is now bound to the channel/grab key. */
	void OnShieldTogglePressed();

	/** Notification from the shield actor when it's destroyed (break or throw). Clears the slot. */
	UFUNCTION()
	void OnEquippedShieldDestroyed(AActor* DestroyedActor);

	// ==================== Channel button override ====================

	/** While a shield is equipped: throw the shield instead of starting a charge channel.
	 *  Otherwise defers to the base implementation (normal grab/channel). */
	virtual void DoChannelPressed() override;

	/** While a shield is equipped: swallow the release (no channel was started).
	 *  Otherwise defers to the base implementation. */
	virtual void DoChannelReleased() override;

	// ==================== Yank Drop Settings (passive auto-replace) ====================

	/** Spawn offset (relative to player actor space) for the discarded yanked weapon.
	 *  Default: behind+right, hip height — reads as "dropped from the back". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapons|Yank Drop")
	FVector YankDropSpawnOffset = FVector(-40.0f, 20.0f, 30.0f);

	/** Linear impulse (cm/s) applied in player actor space to the discarded weapon mesh.
	 *  Small/negative-X for "drops behind" feel — gravity does the rest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapons|Yank Drop")
	FVector YankDropLinearImpulse = FVector(-80.0f, 30.0f, 0.0f);

	/** Angular impulse (degrees/s) for the discarded weapon — light tumble. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapons|Yank Drop")
	FVector YankDropAngularImpulse = FVector(120.0f, 200.0f, 100.0f);

	// ==================== Yank Throw Settings (active hold-swap launch) ====================

	/** Hold time (seconds) on the swap-weapon button required to trigger a yank throw.
	 *  Tap shorter than this performs a normal weapon switch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapons|Yank Throw", meta = (ClampMin = "0.1", ClampMax = "1.5"))
	float YankSwapHoldThreshold = 0.4f;

	/** Spawn offset (relative to player actor space) for a thrown yanked weapon.
	 *  Default: in front of camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapons|Yank Throw")
	FVector YankThrowSpawnOffset = FVector(60.0f, 0.0f, 30.0f);

	/** Linear impulse (cm/s) for thrown weapon — strong forward+up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapons|Yank Throw")
	FVector YankThrowLinearImpulse = FVector(1500.0f, 0.0f, 200.0f);

	/** Angular impulse (degrees/s) for thrown weapon — heavy tumble. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapons|Yank Throw")
	FVector YankThrowAngularImpulse = FVector(0.0f, 800.0f, 300.0f);

	// ==================== Riot Shield ====================

	/** Currently equipped riot shield (separate slot — not part of OwnedWeapons). */
	UPROPERTY(BlueprintReadOnly, Category = "Shield")
	TObjectPtr<ARiotShield> EquippedShield;

	/** Interpolation speed for the shield-equipped camera offset (higher = snappier). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float ShieldCameraInterpSpeed = 8.0f;

	/** Current shield-equipped camera offset (interpolated each tick toward Shield->CameraOffsetWhenRaised when raised, ZeroVector otherwise). */
	FVector CurrentShieldCameraOffset = FVector::ZeroVector;

protected:

	/** Timer for hold-detection on the swap weapon key */
	FTimerHandle SwapHoldTimer;

	/** True while swap key is held and we're waiting for the hold-threshold timer.
	 *  Cleared on release (timer cancelled — tap path) or on threshold fire (hold path). */
	bool bSwapKeyHeldPending = false;

	/** World seconds when the swap key was pressed (for debug "held for Xs" log). */
	float SwapKeyPressTime = -1.0f;




	/** Updates left hand IK transform from weapon socket and passes it to AnimInstance */
	void UpdateLeftHandIK(float DeltaTime);

	/** Sets the left hand IK transform and alpha in the AnimInstance via reflection */
	void SetAnimInstanceLeftHandIK(const FTransform& Transform, float Alpha);

	// ==================== Upgrade System ====================

	/** Registry of all available upgrades (needed for save/load resolution) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Upgrades")
	TObjectPtr<UUpgradeRegistry> UpgradeRegistry;

	// ==================== Checkpoint System ====================
public:
	/**
	 * Save current character state to checkpoint data.
	 * Called by CheckpointSubsystem when checkpoint is activated.
	 * @param OutData Structure to fill with character state
	 * @return True if save was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Checkpoint")
	bool SaveToCheckpoint(UPARAM(ref) FCheckpointData& OutData);

	/**
	 * Restore character state from checkpoint data.
	 * Called by CheckpointSubsystem on respawn.
	 * @param Data Checkpoint data to restore from
	 * @return True if restore was successful
	 */
	UFUNCTION(BlueprintCallable, Category = "Checkpoint")
	bool RestoreFromCheckpoint(const FCheckpointData& Data);

	/** Blueprint event called after successful respawn at checkpoint */
	UFUNCTION(BlueprintImplementableEvent, Category = "Checkpoint", meta = (DisplayName = "On Respawn At Checkpoint"))
	void BP_OnRespawnAtCheckpoint();

protected:
	/**
	 * Reset character state to defaults (velocity, cooldowns, etc.)
	 * Called during respawn before restoring checkpoint data.
	 */
	void ResetCharacterState();

	/** Called when this character's HP is depleted */
	void Die();

	/** Completes the death fade by leaving the run map for the main menu. */
	void ReturnToMainMenuAfterRunDeath();

	/** Assigned on the player Blueprint; C++ does not own references to game content. */
	UPROPERTY(EditDefaultsOnly, Category = "Shooter|Run Flow", meta = (AllowedClasses = "/Script/Engine.World"))
	TSoftObjectPtr<UWorld> MainMenuLevel;

	/** Called to allow Blueprint code to react to this character's death */
	UFUNCTION(BlueprintImplementableEvent, Category = "Shooter", meta = (DisplayName = "On Death"))
	void BP_OnDeath();

	/** Called from the respawn timer to destroy this character and force the PC to respawn */
	void OnRespawn();

	// ==================== Tutorial Triggers ====================
public:

	/** Debug mode: on BeginPlay, flash all 3 HUD arrows and skip weapon slides */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	bool bTutorialDebugMode = false;

	/** HUD arrow data for first-damage tutorial (points to Health Bar) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	FTutorialHUDArrowData FirstDamageArrowData;

	/** Tutorial ID for first-damage arrow */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	FName FirstDamageTutorialID = FName("Tutorial_FirstDamage");

	/** HUD arrow data for first-charge tutorial (points to Charge Bar) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	FTutorialHUDArrowData FirstChargeArrowData;

	/** Tutorial ID for first-charge arrow */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	FName FirstChargeTutorialID = FName("Tutorial_FirstCharge");

	/** HUD arrow data for first-depletion tutorial (points to Charge Bar) — fires when player's charge first returns to Neutral after being charged */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	FTutorialHUDArrowData FirstDepletionArrowData;

	/** Tutorial ID for first-depletion arrow */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	FName FirstDepletionTutorialID = FName("Tutorial_FirstDepletion");

	/** Tutorial ID for melee charges arrow (must match TriggerVolume config) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	FName MeleeChargesTutorialID = FName("Tutorial_MeleeCharges");

	// --- Health Pickup Objective (shown after first low-health arrow is dismissed) ---

	/** Tutorial ID for the health pickup objective (tracked for completion) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	FName HealthPickupObjectiveTutorialID = FName("Tutorial_HealthPickupObjective");

	/** How many health pickups the player must collect to complete the objective */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial", meta = (ClampMin = "1"))
	int32 RequiredHealthPickups = 2;

	/** Called by HealthPickup when the player collects one */
	void NotifyHealthPickupCollected();

private:

	/** How many pickups collected since objective started */
	int32 HealthPickupsCollected = 0;

	/** True while the persistent HUD objective is active */
	bool bHealthPickupObjectiveActive = false;

public:

	// ==================== Boss Finisher System ====================
public:
	/** Flag indicating boss finisher mode is active - set from Level BP */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss Finisher")
	bool bIsOnBossFinisher = false;

	/** Settings for the boss finisher - configure from Level BP */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss Finisher")
	FBossFinisherSettings BossFinisherSettings;

	/** Called when a prop/NPC is captured by channeling */
	UPROPERTY(BlueprintAssignable, Category = "Events|Channeling")
	FOnPropCaptured OnPropCaptured;

	/** Called when a captured prop/NPC is launched (reverse channeling) */
	UPROPERTY(BlueprintAssignable, Category = "Events|Channeling")
	FOnPropLaunched OnPropLaunched;

	/** Called when a launched prop explodes and damages NPCs (TotalDamage = sum of all NPC damage, KillCount = NPCs killed) */
	UPROPERTY(BlueprintAssignable, Category = "Events|Channeling")
	FOnPropImpact OnPropImpact;

	/** Called when boss finisher starts */
	UPROPERTY(BlueprintAssignable, Category = "Boss Finisher")
	FOnBossFinisherStarted OnBossFinisherStarted;

	/** Called when boss finisher ends */
	UPROPERTY(BlueprintAssignable, Category = "Boss Finisher")
	FOnBossFinisherEnded OnBossFinisherEnded;

	/**
	 * Start the boss finisher sequence.
	 * Call this from Level BP after setting bIsOnBossFinisher and BossFinisherSettings.
	 * Triggered by melee input when bIsOnBossFinisher is true.
	 */
	UFUNCTION(BlueprintCallable, Category = "Boss Finisher")
	void StartBossFinisher();

	/**
	 * Abort the boss finisher and return to normal state.
	 */
	UFUNCTION(BlueprintCallable, Category = "Boss Finisher")
	void StopBossFinisher();

	/** Check if boss finisher is currently executing */
	UFUNCTION(BlueprintPure, Category = "Boss Finisher")
	bool IsBossFinisherActive() const { return bBossFinisherActive; }

	/** Cinematic finisher start: hide the first-person body/weapon and lock input while the boss's
	 *  Level Sequence plays (the cine camera owns the view via a Camera Cuts track). */
	UFUNCTION(BlueprintCallable, Category = "Boss Finisher")
	void BeginFinisherCinematic();

	/** Cinematic finisher end: teleport to ExitLocation, reveal the player, restore input, fade in.
	 *  Called (under the sequence's black fade) from the boss when the sequence finishes. */
	UFUNCTION(BlueprintCallable, Category = "Boss Finisher")
	void EndFinisherCinematic(FVector ExitLocation);

protected:
	// ==================== Boss Finisher State ====================

	/** True while finisher sequence is executing */
	bool bBossFinisherActive = false;

	/** Current phase of boss finisher */
	enum class EBossFinisherPhase : uint8
	{
		None,
		CurveMovement,		// Moving along Bezier curve
		LinearMovement,		// Moving in straight line to target
		Animation,			// Playing attack animation while moving
		Hanging,			// Suspended at target point
		Falling				// Falling down with gravity
	};
	EBossFinisherPhase BossFinisherPhase = EBossFinisherPhase::None;

	/** Time elapsed since finisher started */
	float BossFinisherElapsedTime = 0.0f;

	/** Player position when finisher started */
	FVector BossFinisherStartPosition = FVector::ZeroVector;

	/** Bezier curve control points (P0=Start, P1, P2, P3=Target) */
	FVector BezierP0, BezierP1, BezierP2, BezierP3;

	/** Position when linear movement phase started */
	FVector LinearStartPosition = FVector::ZeroVector;

	/** Time when linear phase started */
	float LinearStartTime = 0.0f;

	/** Update boss finisher movement and state */
	void UpdateBossFinisher(float DeltaTime);

	/** Calculate position on cubic Bezier curve at time t (0-1) */
	FVector EvaluateBezierCurve(float T) const;

	/** Setup Bezier control points based on start position and settings */
	void SetupBezierCurve();

	/** Start the melee animation for boss finisher (uses Air Attack settings) */
	void StartBossFinisherAnimation();

	/** End boss finisher and restore normal state */
	void EndBossFinisher();
};
