// MeleeAttackComponent.h
// Quick melee attack system - works independently of equipped weapon
// Features mesh-switching animation system with camera-aligned attacks

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MeleeAttackComponent.generated.h"

class UAnimMontage;
class USoundBase;
class UDamageType;
class UCameraShakeBase;
class UCameraComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class USkeletalMeshComponent;
class UCurveFloat;

/**
 * Type of melee attack based on movement state
 */
UENUM(BlueprintType)
enum class EMeleeAttackType : uint8
{
	Ground,		// Standing, walking, running
	Airborne,	// In air (jumping, falling)
	Sliding		// During slide
};

/**
 * Melee attack state
 */
UENUM(BlueprintType)
enum class EMeleeAttackState : uint8
{
	Ready,			// Can attack
	HidingWeapon,	// UNUSED since the weapon stows instantly. Kept so saved values and readers still load.
	InputDelay,		// UNUSED, as above.
	Windup,			// Montage playing, damage window not opened yet (the notify opens it)
	Active,			// Damage window, opened and closed by AnimNotifyState_MeleeDamageWindow
	Recovery,		// Rest of the montage after the damage window; ends when the montage does
	ShowingWeapon,	// Swing over, the weapon's own draw is running on the character; no new swing until it ends
	Cooldown		// On cooldown
};

/**
 * Animation data for a specific melee attack type
 */
USTRUCT(BlueprintType)
struct FMeleeAnimationData
{
	GENERATED_BODY()

	/** Selection weight for random animation choice (probability = weight / sum of all weights) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Selection", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/** Animation montage for this attack type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> AttackMontage;

	/** Play rate curve (X = normalized time 0-1, Y = play rate multiplier) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UCurveFloat> PlayRateCurve;

	/** Camera shake for swing (create in Editor as Blueprint) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	TSubclassOf<UCameraShakeBase> SwingCameraShake;

	/** Camera shake scale for this attack */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float SwingShakeScale = 1.0f;

	/** Base play rate multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float BasePlayRate = 1.0f;

	/** Location offset for MeleeMesh during this attack (relative to camera) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Offset")
	FVector MeshLocationOffset = FVector::ZeroVector;

	/** Rotation offset for MeleeMesh during this attack (added to camera rotation) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh Offset")
	FRotator MeshRotationOffset = FRotator::ZeroRotator;

	/** Bones to hide for this specific attack (e.g., hide arms for kick, hide right arm for left punch) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh")
	TArray<FName> HiddenBones;
};

/**
 * Melee attack settings
 */
USTRUCT(BlueprintType)
struct FMeleeAttackSettings
{
	GENERATED_BODY()

	// ==================== Damage ====================

	/** Base damage per hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0"))
	float BaseDamage = 50.0f;

	/** Damage multiplier for headshots */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "1.0"))
	float HeadshotMultiplier = 1.5f;

	/** Damage type class */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	TSubclassOf<UDamageType> DamageType;

	/** Headroom the server allows over BaseDamage * HeadshotMultiplier when a client reports a melee
	 *  hit. Upgrade multipliers (Backstab is 3x) live on the swinger's machine and are not replicated,
	 *  so the authority cannot re-derive the real number and clamps to this instead. Mirrors
	 *  AShooterWeapon::MaxReportedDamageMultiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "1.0"))
	float MaxReportedDamageMultiplier = 4.0f;

	/** Impulse applied to hit physics objects */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage", meta = (ClampMin = "0"))
	float HitImpulse = 500.0f;

	// ==================== Momentum Damage ====================

	/** Additional damage per 100 units of player velocity towards target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Momentum Damage", meta = (ClampMin = "0"))
	float MomentumDamagePerSpeed = 10.0f;

	/** Maximum bonus damage from momentum */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Momentum Damage", meta = (ClampMin = "0"))
	float MaxMomentumDamage = 50.0f;

	/** Additional impulse multiplier based on player speed (impulse = HitImpulse * (1 + speed * MomentumImpulseMultiplier)) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Momentum Damage", meta = (ClampMin = "0"))
	float MomentumImpulseMultiplier = 0.002f;

	// ==================== Distance-Based Knockback ====================

	/** Base knockback distance in cm (applied regardless of player speed) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance Knockback", meta = (ClampMin = "0"))
	float BaseKnockbackDistance = 200.0f;

	/** Additional knockback distance per unit of player velocity (cm per cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance Knockback", meta = (ClampMin = "0"))
	float KnockbackDistancePerVelocity = 0.15f;

	/** Base duration for knockback interpolation in seconds (scales with distance) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance Knockback", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float KnockbackBaseDuration = 0.3f;

	/** Duration multiplier per 100cm of knockback distance */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distance Knockback", meta = (ClampMin = "0"))
	float KnockbackDurationPerDistance = 0.001f;

	// ==================== Lunge (TF2-style) ====================
	// Player flies TO the enemy on attack. Target acquired via cone trace from camera.
	// Position is recalculated each frame (full XY+Z homing during the lunge).

	/** Enable lunge toward target enemy in cone */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lunge")
	bool bEnableLunge = true;

	/** Max range for target acquisition (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lunge", meta = (ClampMin = "0", EditCondition = "bEnableLunge"))
	float LungeRange = 250.0f;

	/** Half-angle of detection cone in degrees (TF2 pilot: 30°) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lunge", meta = (ClampMin = "5", ClampMax = "60", EditCondition = "bEnableLunge"))
	float LungeConeHalfAngle = 30.0f;

	/** How close (cm) to stop in front of the target. ~50 = nearly touching. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lunge", meta = (ClampMin = "0", ClampMax = "200", EditCondition = "bEnableLunge"))
	float LungeStopDistance = 50.0f;

	/** Hard cap on lunge velocity (cm/s) — prevents teleport-feel when very close */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lunge", meta = (ClampMin = "500", ClampMax = "10000", EditCondition = "bEnableLunge"))
	float LungeMaxSpeed = 3000.0f;

	/** Minimum player speed required to trigger lunge (0 = works from standstill, TF2 default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lunge", meta = (ClampMin = "0", EditCondition = "bEnableLunge"))
	float MinSpeedForLunge = 0.0f;

	/** Disable gravity during the lunge for clean horizontal flight */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lunge", meta = (EditCondition = "bEnableLunge"))
	bool bDisableGravityDuringLunge = true;

	/** Soft aim-assist: gently steer camera toward the lunge target each frame. Off by default — opt in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lunge", meta = (EditCondition = "bEnableLunge"))
	bool bSoftAimAssistDuringLunge = false;

	/** Strength of the aim assist (0 = none, 1 = snap). 0.3 ≈ subtle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lunge", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bSoftAimAssistDuringLunge"))
	float SoftAimAssistStrength = 0.3f;

	/** One-shot forward boost (cm/s) added to velocity on Active phase entry when there is no lunge target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lunge", meta = (ClampMin = "0", ClampMax = "2000"))
	float NoTargetBoostSpeed = 600.0f;

	// ==================== Titanfall 2 Momentum System ====================

	/** Enable Titanfall 2 style momentum preservation - player keeps velocity during melee */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Titanfall Momentum")
	bool bPreserveMomentum = true;

	/** How much of the original velocity to preserve during melee (1.0 = 100%) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Titanfall Momentum", meta = (ClampMin = "0", ClampMax = "1.0", EditCondition = "bPreserveMomentum"))
	float MomentumPreservationRatio = 1.0f;

	/** Transfer player momentum to target on hit (Titanfall 2 flying kick feel) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Titanfall Momentum")
	bool bTransferMomentumOnHit = true;

	/** Multiplier for momentum transferred to target (1.0 = full velocity transfer) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Titanfall Momentum", meta = (ClampMin = "0", ClampMax = "2.0", EditCondition = "bTransferMomentumOnHit"))
	float MomentumTransferMultiplier = 1.0f;

	// ==================== Cool Kick ====================

	/** Duration of the cool kick period (applied when hitting enemy in air without lunge) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cool Kick", meta = (ClampMin = "0", ClampMax = "2.0"))
	float CoolKickDuration = 0.3f;

	/** Speed boost added gradually over the cool kick period (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cool Kick", meta = (ClampMin = "0", ClampMax = "2000.0"))
	float CoolKickSpeedBoost = 400.0f;

	// ==================== Drop Kick ====================

	/** Enable drop kick - airborne attack when looking down, player dives toward enemy */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Kick")
	bool bEnableDropKick = true;

	/** Minimum height difference (cm) - drop kick only triggers when player is at least this much higher than target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Kick", meta = (ClampMin = "0", ClampMax = "500", EditCondition = "bEnableDropKick"))
	float DropKickMinHeightDifference = 100.0f;

	/** Camera pitch threshold (degrees) - drop kick triggers when looking down more than this */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Kick", meta = (ClampMin = "10", ClampMax = "80", EditCondition = "bEnableDropKick"))
	float DropKickPitchThreshold = 45.0f;

	/** Cone angle for drop kick detection (half-angle in degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Kick", meta = (ClampMin = "5", ClampMax = "45", EditCondition = "bEnableDropKick"))
	float DropKickConeAngle = 30.0f;

	/** Maximum range for drop kick cone trace (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Kick", meta = (ClampMin = "100", ClampMax = "2000", EditCondition = "bEnableDropKick"))
	float DropKickMaxRange = 1000.0f;

	/** Bonus damage per 100cm of height difference */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Kick", meta = (ClampMin = "0", EditCondition = "bEnableDropKick"))
	float DropKickDamagePerHeight = 10.0f;

	/** Maximum bonus damage from height */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Kick", meta = (ClampMin = "0", EditCondition = "bEnableDropKick"))
	float DropKickMaxBonusDamage = 100.0f;

	/** Speed at which player dives toward drop kick target (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Kick", meta = (ClampMin = "500", ClampMax = "5000", EditCondition = "bEnableDropKick"))
	float DropKickDiveSpeed = 2500.0f;

	/** Cooldown duration for drop kick ability (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Kick", meta = (ClampMin = "0", ClampMax = "30", EditCondition = "bEnableDropKick"))
	float DropKickCooldown = 5.0f;

	/** Charge multiplier for successful drop kick hits (multiplied with ChargePerMeleeHit) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Kick", meta = (ClampMin = "1.0", ClampMax = "10.0", EditCondition = "bEnableDropKick"))
	float DropKickChargeMultiplier = 2.0f;

	// ==================== Range & Detection ====================

	/** Maximum range of the melee attack (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Range", meta = (ClampMin = "50", ClampMax = "500"))
	float AttackRange = 150.0f;

	/** Radius of the sphere trace (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Range", meta = (ClampMin = "10", ClampMax = "100"))
	float AttackRadius = 30.0f;

	/** Forward offset from camera for trace start (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Range", meta = (ClampMin = "0", ClampMax = "100"))
	float TraceForwardOffset = 20.0f;

	/** Angle for cone-based hit detection (degrees, 0 = line trace only) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Range", meta = (ClampMin = "0", ClampMax = "45"))
	float AttackAngle = 15.0f;

	// ==================== Timing ====================

	/** The swing has no timers of its own: it lasts as long as its montage, and the damage window is
	 *  wherever AnimNotifyState_MeleeDamageWindow sits in it. This is only the damage window for a
	 *  swing with NO montage to carry the notify, and the minimum window of a drop kick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float ActiveTime = 0.15f;

	/** Cooldown before next attack. Only applied after a swing that hit an enemy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0", ClampMax = "2"))
	float Cooldown = 0.5f;

	// ==================== Movement ====================

	/** Lunge duration (seconds) — how long the player flies toward the lunge target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float LungeDuration = 0.15f;

	/** Can attack while airborne */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	bool bCanAttackInAir = true;

	/** Can attack while sliding */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	bool bCanAttackWhileSliding = true;
};

// Delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMeleeAttackStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnMeleeHit, AActor*, HitActor, const FVector&, HitLocation, bool, bHeadshot, float, Damage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDropKickStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnDropKickHit, AActor*, HitActor, const FVector&, HitLocation, float, Damage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMeleeAttackEnded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDropKickCooldownStarted, float, CooldownDuration);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDropKickCooldownEnded);

/**
 * Component that provides quick melee attack capability.
 * Works independently of the weapon system - can be used at any time.
 * Supports sphere trace hit detection, lunge movement, and animation montages.
 */
UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class POLARITY_API UMeleeAttackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMeleeAttackComponent();

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ==================== Settings ====================

	/** Melee attack settings */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FMeleeAttackSettings Settings;

	/** How fast the weapon comes back out after a swing, as a multiplier on the weapon's OWN draw
	 *  animation (2 = twice as fast). Scales rather than replaces, so a heavy gun stays slower than a
	 *  light one. This is the knob upgrades turn; the draw animation itself stays the weapon's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float DrawSpeedMultiplier = 1.0f;

	// ==================== Animation ====================

	/** Animation variants for ground attacks (standing, walking, running) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Attack Types")
	TArray<FMeleeAnimationData> GroundAttacks;

	/** Animation variants for airborne attacks (jumping, falling) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Attack Types")
	TArray<FMeleeAnimationData> AirborneAttacks;

	/** Animation variants for sliding attacks */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Attack Types")
	TArray<FMeleeAnimationData> SlidingAttacks;

	/** Third person attack montage (optional, plays on character mesh) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UAnimMontage> ThirdPersonMontage;

	// ==================== Mesh References ====================

	/** Reference to the melee mesh component (set at runtime or in Blueprint) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh")
	TObjectPtr<USkeletalMeshComponent> MeleeMesh;

	/** Reference to the first person mesh component (auto-detected or set manually) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh")
	TObjectPtr<USkeletalMeshComponent> FirstPersonMesh;

	/** Rotation offset to align MeleeMesh with camera direction (adjust if mesh faces wrong way) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mesh")
	FRotator MeleeMeshRotationOffset = FRotator(0.0f, -90.0f, 0.0f);

	// ==================== Audio ====================

	/** Swing sound (plays on attack start) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> SwingSound;

	/** Hit sound (plays on successful hit) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> HitSound;

	/** Miss sound (plays if no hit during active window) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TObjectPtr<USoundBase> MissSound;

	// ==================== Camera ====================

	/** Camera shake on hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	TSubclassOf<UCameraShakeBase> HitCameraShake;

	/** Camera shake intensity */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0", ClampMax = "2"))
	float CameraShakeScale = 1.0f;

	/** Enable camera focus on lunge target (rotates camera toward enemy when lunge starts) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	bool bEnableCameraFocusOnLunge = true;

	/** Duration of camera focus rotation (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.05", ClampMax = "1.0", EditCondition = "bEnableCameraFocusOnLunge"))
	float CameraFocusDuration = 0.2f;

	/** Strength of camera focus (1.0 = instant snap, 0.5 = gentle rotation) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.1", ClampMax = "1.0", EditCondition = "bEnableCameraFocusOnLunge"))
	float CameraFocusStrength = 0.7f;

	// ==================== Debug ====================

	/** Enable debug visualization for all melee traces */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bEnableDebugVisualization = false;

	/** Duration for debug shapes (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (ClampMin = "0.1", ClampMax = "10.0", EditCondition = "bEnableDebugVisualization"))
	float DebugShapeDuration = 2.0f;

	// ==================== VFX ====================

	/** Niagara effect for swing trail (spawned at attack start) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	TObjectPtr<UNiagaraSystem> SwingTrailFX;

	/** Niagara effect for impact (spawned on hit) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	TObjectPtr<UNiagaraSystem> ImpactFX;

	/** Socket name on first person mesh for trail attachment */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	FName TrailSocketName = FName("hand_r");

	/** Offset from socket for trail effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	FVector TrailOffset = FVector(0.0f, 0.0f, 0.0f);

	/** Rotation offset from socket for trail effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	FRotator TrailRotationOffset = FRotator::ZeroRotator;

	/** Scale for impact effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float ImpactFXScale = 1.0f;

	// ==================== Events ====================

	/** Called when melee attack begins */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnMeleeAttackStarted OnMeleeAttackStarted;

	/** Called when melee attack hits something */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnMeleeHit OnMeleeHit;

	/** Called when drop kick starts (player begins diving toward target) */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnDropKickStarted OnDropKickStarted;

	/** Called when drop kick hits an enemy */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnDropKickHit OnDropKickHit;

	/** Called when melee attack ends (regardless of hit) */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnMeleeAttackEnded OnMeleeAttackEnded;

	/** Called when drop kick cooldown starts */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnDropKickCooldownStarted OnDropKickCooldownStarted;

	/** Called when drop kick cooldown ends */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnDropKickCooldownEnded OnDropKickCooldownEnded;

	// ==================== Tag-Based Damage Multipliers ====================

	/** Damage multipliers based on target actor tags (AActor::Tags). Multiple matching tags multiply together.
	 *  Mirrors AShooterWeapon::TagDamageMultipliers so melee and gunplay can be tuned per-target uniformly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage")
	TMap<FName, float> TagDamageMultipliers;

	/** Calculate combined damage multiplier from this component's TagDamageMultipliers against Target. */
	UFUNCTION(BlueprintPure, Category = "Damage")
	float GetTagDamageMultiplier(AActor* Target) const;

	// ==================== Delegated lunge (melee weapon) ====================
	// AShooterWeapon_Melee used to carry its own copy of target acquisition and lunge flight, with
	// its own settings, and it disabled this component while equipped. That copy wrote Velocity from
	// the weapon's Tick, which is not part of the move the server replays -- so it was host-only by
	// construction -- and it read a separate MinSpeedForLunge that defaulted to 300, so a swing from
	// standstill produced no lunge at all while the bare-handed one worked fine.
	//
	// It is gone. The weapon now borrows this lunge, the same way it already borrows the dropkick
	// through TryStartDelegatedDropKick, so there is one implementation, one set of numbers (on the
	// character's component, editable in BP_MeleeCharacter) and one that flies inside the simulation.
	//
	// What is deliberately NOT borrowed: the state machine, the animation, the damage window and the
	// charges. The weapon keeps doing all of those itself. This lunge flies and nothing else, which
	// is why it rides its own flag rather than putting the component into an attack state -- that
	// would run this component's hit detection alongside the weapon's and hit everything twice.

	/** Acquire a target and start flying at it, without starting an attack. Returns false when
	 *  nothing qualified, in which case the swing simply does not lunge. */
	UFUNCTION(BlueprintCallable, Category = "Melee|Lunge")
	bool TryStartDelegatedLunge();

	/** End a delegated lunge. Safe to call when none is running. */
	UFUNCTION(BlueprintCallable, Category = "Melee|Lunge")
	void EndDelegatedLunge();

	/** Whatever the lunge is currently flying at, or null. The weapon asks because two of its own
	 *  decisions used to read its own copy of this: whether an airborne swing becomes a cool kick,
	 *  and which NPC to stop driving into once it is already being knocked back. */
	UFUNCTION(BlueprintPure, Category = "Melee|Lunge")
	AActor* GetLungeTargetActor() const { return MagnetismTarget.Get(); }

	// ==================== Focus lock (hold aim) ====================
	//
	// The swing does NOT pick a victim from wherever the camera happens to be pointing any more.
	// The player holds the aim button, that locks the best target inside the reach the passive
	// grants, the view then follows that target, and the lunge flies at it and at nothing else.
	//
	// Local to the machine that pressed the button, all of it. The view is the client's own (the
	// server receives it as the pawn's rotation like any other frame), and the lunge target already
	// travels inside the saved move, so nothing new goes on the wire.
	// @see FCharacterNetworkMoveData_Polarity::MeleeLungeTarget

	/** Lock the best candidate under the crosshair. False when nothing qualifies, in which case the
	 *  button did nothing and the caller should treat the press as unused.
	 *
	 *  Failing does NOT mean the button is finished: the hold is remembered (@see SetFocusHeld) and
	 *  the lock takes the first target that walks into reach, without a second press. */
	UFUNCTION(BlueprintCallable, Category = "Melee|Focus")
	bool TryStartFocus();

	/** Remember that the aim button is DOWN, separately from whether anything is locked.
	 *
	 *  This is the difference between "the press missed" and "the player is still asking". A press
	 *  with nothing in reach used to be thrown away, so a player holding the button through an
	 *  approach had to let go and press again the moment an enemy became lockable -- which is not
	 *  something anybody does mid-fight, so the lock read as broken. */
	UFUNCTION(BlueprintCallable, Category = "Melee|Focus")
	void SetFocusHeld(bool bHeld);

	/** Let go. Safe to call when nothing is locked. */
	UFUNCTION(BlueprintCallable, Category = "Melee|Focus")
	void StopFocus();

	UFUNCTION(BlueprintPure, Category = "Melee|Focus")
	bool IsFocusing() const { return FocusTarget.IsValid(); }

	/** What the view is being held on, or null. Read by the HUD to draw the brackets. */
	UFUNCTION(BlueprintPure, Category = "Melee|Focus")
	AActor* GetFocusTarget() const { return FocusTarget.Get(); }

	/** WHERE on the target the lock points, and where the brackets get drawn.
	 *
	 *  One function for both on purpose. Until this existed each of the two asked the target for
	 *  GetActorLocation() separately, which is the actor's pivot -- on a Character that is the middle
	 *  of the capsule, i.e. the belt. Nothing was ever attached to the enemy; the lock was staring at
	 *  a pivot. Two call sites answering the question independently is also how the brackets and the
	 *  camera end up disagreeing, and a reticle that is not where the camera is going is a lie. */
	UFUNCTION(BlueprintPure, Category = "Melee|Focus")
	FVector GetFocusAimPoint(const AActor* Target) const;

	/** Bone on the target to aim at. `spine_04` is the upper chest on the UE5 mannequin the NPCs use;
	 *  `head` and `spine_05` are the other two that read as deliberate.
	 *
	 *  A bone is the only answer that follows the animation, so a target that ducks, staggers or goes
	 *  to ragdoll drags the lock with it instead of leaving it pointed at where the capsule still is.
	 *  Empty, or a name this particular target's skeleton does not have, falls through to the height
	 *  fraction below: the tank and the drones have no humanoid rig, and a lock that only worked on
	 *  infantry would be worse than one that aims a little roughly at everything. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Focus")
	FName FocusAimBone = TEXT("spine_04");

	/** Fallback height as a fraction of the target's own bounds: 0 is the feet, 0.5 the middle (which
	 *  is exactly what the pivot gave and why the old lock looked at the belt), 1 the top of the head.
	 *  A fraction rather than a distance so a tank and a grunt are both aimed at the same PROPORTION
	 *  of themselves without a number authored per enemy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Focus", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FocusAimHeightFraction = 0.72f;

	/** Final nudge in world Z on top of whichever of the two above answered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Focus", meta = (ClampMin = "-200.0", ClampMax = "200.0", Units = "cm"))
	float FocusAimZOffset = 0.0f;

	/** How fast the view swings onto the locked target. A pull, not a pin: the mouse keeps working
	 *  and this keeps winning while the button is held, which is what makes it feel like a lock
	 *  rather than like losing control of the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Focus", meta = (ClampMin = "0.5", ClampMax = "60.0"))
	float FocusTrackingSpeed = 18.0f;

	/** Inside this many degrees the view is put EXACTLY on the target instead of being interpolated
	 *  the rest of the way.
	 *
	 *  An interpolation alone never arrives: it halves the error every frame, so a moving target sits
	 *  permanently a few degrees off centre and the lock reads as sloppy no matter how high the speed
	 *  above is. The snap is what makes it a lock. Small on purpose -- it is the last step of a pull
	 *  the player can still fight, not a magnet that grabs the camera from across the screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Focus", meta = (ClampMin = "0.0", ClampMax = "20.0", Units = "deg"))
	float FocusHardSnapDegrees = 4.0f;

	/** How often a held button that has nothing to lock looks again, in seconds. The search is a
	 *  sphere overlap, so it is not free enough to run every frame for a button that is being held
	 *  down for a whole approach. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Focus", meta = (ClampMin = "0.02", ClampMax = "1.0", Units = "s"))
	float FocusRetryInterval = 0.1f;

	/** Slack before a lock breaks on range, as a multiplier of the target's own allowed reach. A
	 *  lock that broke at the exact centimetre the lunge stops being legal would flicker on and off
	 *  while the two of you move. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Focus", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float FocusBreakRangeSlack = 1.25f;

	// ==================== Lunge reach ====================
	// Settings.LungeRange is what a swing is worth on its own. A class passive is allowed to extend
	// it per target -- the Melee's reaches further into an enemy whose shield the team has already
	// broken -- so nothing decides reach by reading Settings.LungeRange directly any more.

	/** How far the lunge may reach for this particular candidate, after the owner's passive has had
	 *  its say. Returns 0 when lunging is switched off, which is also "no candidate qualifies". */
	UFUNCTION(BlueprintPure, Category = "Melee|Lunge")
	float GetLungeRangeFor(const AActor* Target) const;

	/** The furthest any candidate could possibly be allowed to be. Sizes the acquisition sphere and
	 *  bounds the reach the authority will accept for a reported hit. */
	UFUNCTION(BlueprintPure, Category = "Melee|Lunge")
	float GetMaxLungeRange() const;

	// ==================== Coop damage validation ====================
	// Read by AShooterCharacter::Server_ReportMeleeDamage off the SERVER's own copy of this component,
	// never off numbers the client sent. Both machines have it with the same Blueprint defaults.

	/** Ceiling the authority clamps a reported melee hit to. */
	UFUNCTION(BlueprintPure, Category = "Melee|Validation")
	float GetMaxReportedSingleHitDamage() const;

	/** Furthest a melee hit can legitimately land from the swinger: the swing itself plus whatever
	 *  the lunge or the dive could have closed on the way in. */
	UFUNCTION(BlueprintPure, Category = "Melee|Validation")
	float GetMaxReportedReach() const;

	/** Ceiling the authority clamps a reported shove distance to. */
	UFUNCTION(BlueprintPure, Category = "Melee|Validation")
	float GetMaxReportedKnockbackDistance() const;

	/** Actually shove Target, on the machine that owns the decision.
	 *
	 *  Static and public because the server reaches it from AShooterCharacter::Server_ReportMeleeKnockback
	 *  when a client's punch arrives, where there is no swinging component in scope — the swing
	 *  happened on the other machine. Everything it needs is already in the arguments.
	 *
	 *  An NPC is shoved outright: its movement is the server's, so the server's word is final. A
	 *  player is shoved on BOTH ends, here and through Client_ApplyKnockback, because a player's
	 *  movement is predicted by their own client and a one-sided launch just starts an argument. */
	static void ApplyKnockbackOnAuthority(AActor* Target, const FVector& Direction, float Distance,
		float Duration, const FVector& AttackerLocation);

	// ==================== External Control ====================

	/** When true, CanAttack() returns false. Set by ShooterCharacter when a melee weapon is equipped. */
	bool bExternallyDisabled = false;

	/** Enable or disable this component externally (e.g., when a melee weapon is equipped) */
	void SetExternallyDisabled(bool bDisabled) { bExternallyDisabled = bDisabled; }

	/** Check if this component is externally disabled */
	UFUNCTION(BlueprintPure, Category = "Melee")
	bool IsExternallyDisabled() const { return bExternallyDisabled; }

	// ==================== API ====================

	/**
	 * Attempt to start a melee attack
	 * @return true if attack started successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	bool StartAttack();

	/**
	 * Start a delegated dropkick (called by ShooterWeapon_Melee).
	 * Handles only movement and hit detection — skips animations, mesh transitions, damage.
	 * The calling weapon handles its own animation and damage via OnDropKickHit delegate.
	 * @return true if dropkick started successfully
	 */
	bool StartDelegatedDropKick();

	/**
	 * Cancel current attack (if in windup phase)
	 * @return true if attack was cancelled
	 */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	bool CancelAttack();

	/**
	 * Check if an attack can be started
	 */
	UFUNCTION(BlueprintPure, Category = "Melee")
	bool CanAttack() const;

	/**
	 * Get current attack state
	 */
	UFUNCTION(BlueprintPure, Category = "Melee")
	EMeleeAttackState GetAttackState() const { return CurrentState; }

	/**
	 * Check if currently attacking (any phase)
	 */
	UFUNCTION(BlueprintPure, Category = "Melee")
	bool IsAttacking() const;

	/**
	 * Get current attack type (Ground, Airborne, or Sliding)
	 */
	UFUNCTION(BlueprintPure, Category = "Melee")
	EMeleeAttackType GetCurrentAttackType() const { return CurrentAttackType; }

	/**
	 * Get cooldown progress (0 = just started, 1 = ready)
	 */
	UFUNCTION(BlueprintPure, Category = "Melee")
	float GetCooldownProgress() const;

	/**
	 * Check if input is currently locked (attack in progress)
	 */
	UFUNCTION(BlueprintPure, Category = "Melee")
	bool IsInputLocked() const { return bInputLocked; }

	/**
	 * Check if current attack is a drop kick
	 */
	UFUNCTION(BlueprintPure, Category = "Melee")
	bool IsDropKick() const { return bIsDropKick; }

	/**
	 * Check if current dropkick is delegated from ShooterWeapon_Melee
	 */
	bool IsDelegatedDropKick() const { return bDelegatedDropKick; }

	/**
	 * Get height difference at dropkick start (for external bonus damage calculation)
	 */
	float GetDropKickHeightDifference() const { return DropKickHeightDifference; }

	/**
	 * Get the current dropkick/magnetism target actor (for external calculations like play rate sync)
	 */
	AActor* GetDropKickTarget() const { return MagnetismTarget.Get(); }

	/**
	 * Check if drop kick is on cooldown
	 */
	UFUNCTION(BlueprintPure, Category = "Melee")
	bool IsDropKickOnCooldown() const { return DropKickCooldownRemaining > 0.0f; }

	/**
	 * Runtime gate for the drop kick mechanic. Default false — granted by the "Drop Kick" upgrade pickup.
	 * ShouldPerformDropKick() returns false while this is false, regardless of Settings.bEnableDropKick.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Melee|Drop Kick")
	bool bDropKickUnlocked = false;

	/**
	 * Get drop kick cooldown progress (0 = just started, 1 = ready)
	 */
	UFUNCTION(BlueprintPure, Category = "Melee")
	float GetDropKickCooldownProgress() const;

	/**
	 * Get drop kick cooldown remaining time (seconds)
	 */
	UFUNCTION(BlueprintPure, Category = "Melee")
	float GetDropKickCooldownRemaining() const { return DropKickCooldownRemaining; }

	/**
	 * Take the weapon out of the hands now, without starting an attack. Used by the boss finisher's
	 * approach: the finisher's own swing then finds the hands empty, and its end draws the weapon.
	 */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	void LowerWeapon();

	/**
	 * Check if the weapon was taken away by LowerWeapon (boss finisher) and the finisher's swing
	 * has not ended yet
	 */
	UFUNCTION(BlueprintPure, Category = "Melee")
	bool IsWeaponLowered() const { return bIsWeaponLowered; }

	/**
	 * Enable or disable debug visualization for melee traces
	 */
	UFUNCTION(BlueprintCallable, Category = "Melee|Debug")
	void SetDebugVisualizationEnabled(bool bEnabled) { bEnableDebugVisualization = bEnabled; }

	/**
	 * Check if debug visualization is enabled
	 */
	UFUNCTION(BlueprintPure, Category = "Melee|Debug")
	bool IsDebugVisualizationEnabled() const { return bEnableDebugVisualization; }

	// ==================== Charged Punch / Air-Lunge API ====================
	// External hook for upgrades like ChargedPunch that want to take over the swing
	// pipeline temporarily: hide the weapon, swap to MeleeMesh, play a custom montage,
	// then restore. Wrap the protected mesh-switch helpers so external callers can
	// drive their own flow without touching the rest of the state machine.

	/** Hide FirstPersonMesh + current weapon, show MeleeMesh attached to camera. */
	UFUNCTION(BlueprintCallable, Category = "Melee|External")
	void EnterMeleeMeshView();

	/** Hide MeleeMesh, restore FirstPersonMesh + weapon visibility. */
	UFUNCTION(BlueprintCallable, Category = "Melee|External")
	void ExitMeleeMeshView();

	/**
	 * Play a single montage on the MeleeMesh's AnimInstance. Intended for upgrades
	 * (ChargedPunch) to play their custom swing animation in the first-person view.
	 * Caller is responsible for EnterMeleeMeshView before and ExitMeleeMeshView after.
	 */
	UFUNCTION(BlueprintCallable, Category = "Melee|External")
	void PlayMontageOnMeleeMesh(UAnimMontage* Montage, float PlayRate = 1.0f);

	// ==================== Animation Notify API ====================
	// All three notify entry points are idempotent within a single attack — calling
	// them twice in the same phase is a no-op. Fallback state-machine timers honour
	// the per-phase "consumed" flags so the notify always wins if it fires.

	/**
	 * Activate damage window from animation notify (called by AnimNotify_MeleeActiveStart).
	 * Transitions Windup -> Active (consumes Windup timer).
	 */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	void ActivateDamageWindowFromNotify();

	/**
	 * Deactivate damage window from animation notify (called by AnimNotify_MeleeActiveEnd).
	 * Transitions Active -> Recovery (consumes Active timer).
	 */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	void DeactivateDamageWindowFromNotify();

	/**
	 * End recovery from animation notify (called by AnimNotify_MeleeRecoveryEnd).
	 * Transitions Recovery -> ShowingWeapon (consumes Recovery timer).
	 */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	void EndRecoveryFromNotify();

	/** Marks the authored next-melee-ready point. The next swing may interrupt recovery/equip. */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	void NotifyMeleeReadyFromNotify();

	// ==================== Combo Speed Multiplier ====================

	/**
	 * Apply a multiplier to all phase timings and montage play rates.
	 * 1.0 = normal speed. 2.0 = twice as fast.
	 * Called by the Combo upgrade as the combo counter rises.
	 * Takes effect from the NEXT phase entry (current phase keeps its current timer).
	 */
	UFUNCTION(BlueprintCallable, Category = "Melee|Combo")
	void ApplyComboSpeedMultiplier(float NewMultiplier);

	/** Current combo speed multiplier (1.0 if no combo modifier active). */
	UFUNCTION(BlueprintPure, Category = "Melee|Combo")
	float GetComboSpeedMultiplier() const { return ComboSpeedMultiplier; }

protected:
	// ==================== State ====================

	/** Current attack state */
	EMeleeAttackState CurrentState = EMeleeAttackState::Ready;
	/** Set by the authored ready notify; consumed when the next swing starts. */
	bool bReadyForNextAttackFromNotify = false;

	/** Time remaining in current state */
	float StateTimeRemaining = 0.0f;

	/** Has hit something during current attack */
	bool bHasHitThisAttack = false;

	/** Has hit an enemy (AShooterNPC) during current attack — used to decide if cooldown applies */
	bool bHitEnemyThisAttack = false;

	/** Input is locked - prevents starting new attack until current one fully completes */
	bool bInputLocked = false;

	/** Actors already hit during this attack (prevents multi-hit) */
	UPROPERTY()
	TArray<TObjectPtr<AActor>> HitActorsThisAttack;

	/** Lunge direction (stored at attack start) */
	FVector LungeDirection = FVector::ZeroVector;

	/** Lunge progress (0-1) */
	float LungeProgress = 0.0f;

	/** Cached owner character */
	UPROPERTY()
	TObjectPtr<ACharacter> OwnerCharacter;

	/** Cached owner controller */
	UPROPERTY()
	TObjectPtr<APlayerController> OwnerController;

	/** Active trail effect component */
	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ActiveTrailFX;

	/** Target being pulled by magnetism */
	UPROPERTY()
	TWeakObjectPtr<AActor> MagnetismTarget;

	/** What the player is holding the view on. Set by TryStartFocus, cleared when the button comes
	 *  up or the target stops qualifying. Weak: it can die mid-lock, which is the common case. */
	TWeakObjectPtr<AActor> FocusTarget;

	/** The aim button is down. Outlives a failed press and a lock that broke, which is what lets the
	 *  next valid target be taken without another press. @see SetFocusHeld */
	bool bFocusHeld = false;

	/** Counts down to the next look for a target while the button is held and nothing is locked. */
	float FocusRetryTimer = 0.0f;

	/** Target position for lunge (calculated during magnetism start with path validation) */
	FVector LungeTargetPosition = FVector::ZeroVector;

	/** Cached owner velocity at attack start (for momentum calculations) */
	FVector OwnerVelocityAtAttackStart = FVector::ZeroVector;

	// ==================== Cool Kick State ====================

	/** Time remaining in cool kick period */
	float CoolKickTimeRemaining = 0.0f;

	/** Direction for cool kick boost (movement direction at hit time) */
	FVector CoolKickDirection = FVector::ZeroVector;

	// ==================== Combo Speed State ====================

	/** Current combo speed multiplier (set by ApplyComboSpeedMultiplier). */
	float ComboSpeedMultiplier = 1.0f;

	// ==================== Drop Kick State ====================

	/** True if current attack is a drop kick */
	bool bIsDropKick = false;

	/** Height difference at drop kick start (for bonus damage calculation) */
	float DropKickHeightDifference = 0.0f;

	/** Target position for drop kick dive */
	FVector DropKickTargetPosition = FVector::ZeroVector;

	/** Velocity during drop kick (updated each tick, used for exit momentum) */
	FVector DropKickVelocity = FVector::ZeroVector;

	/** Remaining drop kick cooldown time (seconds) */
	float DropKickCooldownRemaining = 0.0f;

	/** When true, dropkick is delegated from ShooterWeapon_Melee — skip mesh/animation/damage */
	bool bDelegatedDropKick = false;

	/** When true, the equipped melee weapon is borrowing the lunge for its own swing. The component
	 *  stays out of its own attack state throughout: this flag is the ONLY thing that puts UpdateLunge
	 *  into a flying phase, and nothing else about an attack is running. */
	bool bDelegatedLunge = false;

	// ==================== Mesh Transition State ====================

	/** Current attack type (determined at attack start) */
	EMeleeAttackType CurrentAttackType = EMeleeAttackType::Ground;

	/** The weapon was taken away by LowerWeapon (boss finisher approach). Cleared when a swing ends.
	 *  Read by the hit code to skip the hit camera shake during the finisher. */
	bool bIsWeaponLowered = false;

	/** Target rotation for MeleeMesh (camera-aligned) */
	FRotator MeleeMeshTargetRotation = FRotator::ZeroRotator;

	/** Current montage being played on MeleeMesh */
	UPROPERTY()
	TObjectPtr<UAnimMontage> CurrentMeleeMontage;

	/** Bones currently hidden on MeleeMesh (for restoration) */
	TArray<FName> CurrentlyHiddenBones;

	/** Time elapsed in current montage (for play rate curve sampling) */
	float MontageTimeElapsed = 0.0f;

	/** Total duration of current montage at base rate */
	float MontageTotalDuration = 0.0f;

	// ==================== Camera Focus State ====================

	/** Target actor for camera focus */
	UPROPERTY()
	TWeakObjectPtr<AActor> CameraFocusTarget;

	/** Time remaining for camera focus */
	float CameraFocusTimeRemaining = 0.0f;

	/** Initial rotation when focus started */
	FRotator CameraFocusStartRotation = FRotator::ZeroRotator;

	/** Target rotation for camera focus */
	FRotator CameraFocusTargetRotation = FRotator::ZeroRotator;

	// ==================== Internal ====================

	/** Transition to a new state */
	void SetState(EMeleeAttackState NewState);

	/** Update current state */
	void UpdateState(float DeltaTime);

	/** Perform hit detection */
	void PerformHitDetection();

	/** Apply damage to hit actor, returns final damage dealt */
	float ApplyDamage(AActor* HitActor, const FHitResult& HitResult);

	/** One portion of a melee hit, routed to whoever is allowed to apply it.
	 *
	 *  On the authority this is a plain TakeDamage. On a client it is a report to the server, because
	 *  health is the server's and a client calling TakeDamage locally changes nothing anywhere — which
	 *  is exactly why a client's punches did nothing at all before this existed. Mirrors
	 *  AShooterCharacter::DealDamage, which does the same job for gunfire. */
	void DealMeleeDamage(AActor* HitActor, float Damage, TSubclassOf<UDamageType> DamageTypeClass,
		const FHitResult& HitResult, const FVector& ShotDirection);

	/** Check if hit is a headshot */
	bool IsHeadshot(const FHitResult& HitResult) const;

	/** Check if actor is a valid melee target (Pawn/Character, not world geometry) */
	bool IsValidMeleeTarget(AActor* HitActor) const;

	/** Apply lunge movement */
	void UpdateLunge(float DeltaTime);

	/** Play attack animation */
	void PlayAttackAnimation();

	/** Stop attack animation */
	void StopAttackAnimation();

	/** Play sound effect */
	void PlaySound(USoundBase* Sound);

	/** Play camera shake */
	void PlayCameraShake();

	/** Spawn swing trail effect */
	void SpawnSwingTrailFX();

	/** Stop and destroy swing trail effect */
	void StopSwingTrailFX();

	/** Spawn impact effect at hit location */
	void SpawnImpactFX(const FVector& Location, const FVector& Normal);

	/** Get trace start location */
	FVector GetTraceStart() const;

	/** Get trace end location */
	FVector GetTraceEnd() const;

	/** Get trace direction */
	FVector GetTraceDirection() const;

	/** Get lunge direction based on current movement velocity */
	FVector GetLungeDirection() const;

	/** Find and start pulling magnetism target */
	void StartMagnetism();

	/** Update magnetism pull */
	void UpdateMagnetism(float DeltaTime);

	/** Stop magnetism and clear target */
	void StopMagnetism();

	/** Apply impulse to character (works with ShooterNPC) */
	void ApplyCharacterImpulse(AActor* HitActor, const FVector& ImpulseDirection, float ImpulseStrength);

	/** Calculate momentum-based bonus damage */
	float CalculateMomentumDamage(AActor* HitActor) const;

	/** Calculate momentum-based impulse multiplier */
	float CalculateMomentumImpulseMultiplier() const;

	/** Get impact center for magnetism pull */
	FVector GetImpactCenter() const;

	/** Start cool kick period (called on airborne hit without lunge) */
	void StartCoolKick();

	/** Update cool kick boost */
	void UpdateCoolKick(float DeltaTime);

	// ==================== Drop Kick ====================

	/** Check if drop kick conditions are met (airborne + looking down) */
	bool ShouldPerformDropKick() const;

	/** Perform cone trace for drop kick and find target */
	bool TryStartDropKick();

	/** Update drop kick dive movement */
	void UpdateDropKick(float DeltaTime);

	/** Calculate drop kick bonus damage based on height difference */
	float CalculateDropKickBonusDamage() const;

	/** Check if there's a valid dropkick target in the cone (read-only, no side effects) */
	bool HasDropKickTarget() const;

	// ==================== Mesh Transition ====================

	/** Determine attack type based on current movement state */
	EMeleeAttackType DetermineAttackType() const;

	/** Select random animation from array based on weights */
	const FMeleeAnimationData* SelectWeightedAnimation(const TArray<FMeleeAnimationData>& Animations);

	/** Get animation data for the current attack type (selected at attack start) */
	const FMeleeAnimationData& GetCurrentAnimationData() const;

	/** Currently selected animation data (chosen at attack start) */
	const FMeleeAnimationData* SelectedAnimationData = nullptr;

	/** Default empty animation data for fallback */
	FMeleeAnimationData DefaultAnimationData;

	/** Empty the hands for the swing, instantly (AShooterCharacter::StowWeaponForMelee). */
	void HideWeaponForSwing();

	/** Bring the weapon back through its draw, at DrawSpeedMultiplier (AShooterCharacter::DrawWeaponAfterMelee). */
	void DrawWeaponBack();

	/** The montage is over: melee mesh away, weapon drawn back, then Cooldown or Ready. The one
	 *  place a swing ends, whichever way it got there. */
	void EndSwing();

	/** Attach the MeleeMesh to the camera and show it. The hands are the character's business. */
	void SwitchToMeleeMesh();

	/** Detach and hide the MeleeMesh. The hands are the character's business. */
	void SwitchToFirstPersonMesh();

	/** Update MeleeMesh rotation to match camera */
	void UpdateMeleeMeshRotation();

	/** Play swing camera shake */
	void PlaySwingCameraShake();

	/** Update montage play rate based on curve */
	void UpdateMontagePlayRate(float DeltaTime);

	/** Called when melee montage ends */
	UFUNCTION()
	void OnMeleeMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	/** Called when the melee montage STARTS blending out. This, not the end, is where the swing
	 *  ends: the melee mesh's graph has nothing under the slot, so the blend-out tail is a blend
	 *  toward the reference pose and must not be seen. */
	UFUNCTION()
	void OnMeleeMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);

	/** Auto-detect mesh references if not set */
	void AutoDetectMeshReferences();

	// ==================== Camera Focus ====================

	/** Start camera focus on target */
	/** The best target under the crosshair by the lunge's own rules: inside the cone, inside the
	 *  reach that candidate specifically is allowed, alive, and not a downed teammate. Shared by the
	 *  focus lock and by nothing else now -- the swing reads the lock instead of searching. */
	AActor* FindBestLungeCandidate() const;

	/** Does this target still deserve the lock: alive, still standing, still inside its own reach
	 *  plus the slack. */
	bool IsFocusTargetStillValid(const AActor* Target) const;

	/** Holds the view on the locked target, and drops the lock when it stops qualifying. Runs on the
	 *  locking machine only. */
	void UpdateFocus(float DeltaTime);

	void StartCameraFocus(AActor* Target);

	/** Update camera focus interpolation */
	void UpdateCameraFocus(float DeltaTime);

	/** Stop camera focus */
	void StopCameraFocus();
};
