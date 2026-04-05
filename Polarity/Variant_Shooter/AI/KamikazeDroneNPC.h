// KamikazeDroneNPC.h
// FPV kamikaze drone — orbits in Middle Ring, dives at player with prediction, explodes on contact.
// Supports swarms (20+), retaliation on damage, and Polarity system integration.

#pragma once

#include "CoreMinimal.h"
#include "ShooterNPC.h"
#include "KamikazeDroneNPC.generated.h"

class UFlyingAIMovementComponent;
class USphereComponent;
class UFPVTiltComponent;
class UNiagaraSystem;
class UAudioComponent;

/** State machine for kamikaze drone behavior */
UENUM(BlueprintType)
enum class EKamikazeState : uint8
{
	Launching,      // Post-spawn: FPV PID stabilization from launch impulse
	Orbiting,       // Circling in Middle Ring
	Telegraphing,   // 0.3-0.4s warning before attack
	Positioning,    // Flying to frontal attack point before committing
	Attacking,      // Diving toward predicted position
	PostAttack,     // Inertia after missing target
	Recovery,       // Braking, turning, returning to orbit
	Parried,        // Melee-parried: spiraling toward target/ground, will explode with stun
	Dead
};

/**
 * FPV Kamikaze Drone NPC.
 *
 * Inherits from AShooterNPC (NOT AFlyingDrone) — different movement model:
 * - No weapon (attacks by collision)
 * - Constant motion (never hovers)
 * - FPV-style visual tilt instead of stabilization springs
 * - Orbit-based approach instead of waypoint patrol
 *
 * Uses UFlyingAIMovementComponent for base 3D flight,
 * but overrides movement logic with custom state machine.
 */
UCLASS()
class POLARITY_API AKamikazeDroneNPC : public AShooterNPC
{
	GENERATED_BODY()

public:

	AKamikazeDroneNPC(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:

	// ==================== Components ====================

	/** Flying AI movement component for 3D navigation */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UFlyingAIMovementComponent> FlyingMovement;

	/** Sphere collision for the drone body */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> DroneCollision;

	/** Visual mesh for the drone */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> DroneMesh;

	/** FPV visual tilt component (pitch/roll/yaw/wobble) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UFPVTiltComponent> FPVTilt;

	/** Looping flight sound component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UAudioComponent> FlightAudioComponent;

	// ==================== Collision Settings ====================

	/** Radius of the drone's collision sphere (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Collision")
	float CollisionRadius = 35.0f;

	// ==================== Combat Settings ====================

	/** Damage dealt on direct collision with player */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Combat")
	float CollisionDamage = 40.0f;

	/** Explosion radius on direct player hit (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Combat")
	float ExplosionRadius = 300.0f;

	/** Explosion radius on crash into geometry or air death (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Combat")
	float CrashExplosionRadius = 200.0f;

	/** Damage dealt by crash explosion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Combat")
	float CrashDamage = 25.0f;

	/** Duration of explosion stun applied to nearby NPCs (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Combat", meta = (ClampMin = "0.1"))
	float ExplosionStunDuration = 2.0f;

	/** Anim montage to play on stunned NPCs (null = fallback to NPC's KnockbackMontage) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Combat")
	TObjectPtr<UAnimMontage> ExplosionStunMontage;

	// ==================== Attack Settings ====================

	/** Speed during attack dive (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Attack")
	float AttackSpeed = 1200.0f;

	/** Maximum turn rate during attack dive (degrees/s) — very limited correction */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Attack", meta = (ClampMin = "5", ClampMax = "40"))
	float AttackTurnRate = 17.5f;

	/** Duration of telegraph phase before attack (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Attack", meta = (ClampMin = "0.1"))
	float TelegraphDuration = 0.35f;

	/** Duration of post-attack inertia before crash chance or recovery */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Attack", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float PostAttackInertiaTime = 0.5f;

	/** Steering jitter amplitude during attack dive — simulates FPV imperfections (0.06 ≈ 3.4°) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Attack", meta = (ClampMin = "0", ClampMax = "0.3"))
	float AttackJitterAmplitude = 0.06f;

	/** Speed oscillation during attack (fraction of AttackSpeed, ±4% default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Attack", meta = (ClampMin = "0", ClampMax = "0.2"))
	float AttackSpeedJitter = 0.04f;

	/** Distance from target within which killing the attacking drone triggers air explosion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Attack")
	float AttackDeathDistanceThreshold = 400.0f;

	// ==================== Orbit Settings ====================

	/** Cruising speed on orbit (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Orbit")
	float CruiseSpeed = 800.0f;

	/** Maximum turn rate while orbiting (degrees/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Orbit")
	float OrbitMaxTurnRate = 90.0f;

	/** Starting orbit radius (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Orbit")
	float OrbitStartRadius = 1100.0f;

	/** Minimum orbit radius — attack readiness zone (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Orbit")
	float OrbitMinRadius = 650.0f;

	/** Radius reduction per completed lap (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Orbit")
	float OrbitShrinkPerLap = 75.0f;

	/** Ellipse eccentricity (0 = circle, 1 = flat) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Orbit", meta = (ClampMin = "0", ClampMax = "0.8"))
	float OrbitEccentricity = 0.3f;

	/** Base height above ground for orbit (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Orbit")
	float OrbitBaseHeight = 250.0f;

	/** Vertical oscillation amplitude (cm) — height varies ±Amplitude around BaseHeight */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Orbit")
	float OrbitHeightAmplitude = 100.0f;

	/** How often orbit center updates to track player (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Orbit", meta = (ClampMin = "0.1"))
	float OrbitCenterUpdateInterval = 0.5f;

	/** How often geometry checks are performed (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Orbit", meta = (ClampMin = "0.1"))
	float GeometryCheckInterval = 0.25f;

	/** Speed noise amplitude (fraction of CruiseSpeed) for per-instance variation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Orbit", meta = (ClampMin = "0", ClampMax = "0.3"))
	float SpeedNoiseAmplitude = 0.12f;

	/** Minimum orbit space threshold — forced attack if can't maintain this (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Orbit")
	float MinOrbitSpaceThreshold = 400.0f;

	/** Duration of positioning Bezier transition (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Positioning", meta = (ClampMin = "0.3", ClampMax = "3.0"))
	float PositioningDuration = 0.8f;

	// ==================== Strafe Settings ====================

	/** Lateral oscillation amplitude (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Strafe", meta = (ClampMin = "50", ClampMax = "800"))
	float StrafeAmplitude = 300.0f;

	/** Oscillation frequency (radians/s). 2.5 ≈ one full cycle every 2.5 seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Strafe", meta = (ClampMin = "0.5", ClampMax = "8.0"))
	float StrafeFrequency = 2.5f;

	/** Number of blocked rays (out of 8) to consider orbit "bad" and switch to strafe */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Strafe", meta = (ClampMin = "1", ClampMax = "8"))
	int32 BadOrbitThreshold = 5;

	// ==================== Crash Settings ====================

	/** Base crash chance even in open space after a miss */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Crash", meta = (ClampMin = "0", ClampMax = "1"))
	float BaseCrashChance = 0.1f;

	/** Additional crash chance from speed factor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Crash", meta = (ClampMin = "0", ClampMax = "1"))
	float SpeedCrashFactor = 0.2f;

	/** Additional crash chance from pitch angle factor */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Crash", meta = (ClampMin = "0", ClampMax = "1"))
	float AngleCrashFactor = 0.2f;

	// ==================== Launch Stabilization ====================

	/** Max time for launch stabilization before forcing orbit transition (seconds).
	 *  Actual transition often happens sooner when speed settles near CruiseSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Launch")
	float MaxLaunchStabilizationTime = 1.5f;

	/** Speed decay rate during launch (higher = faster deceleration).
	 *  Simulates FPV PID-controller settling: exponential decay toward CruiseSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Launch", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float LaunchDecayRate = 3.0f;

	/** How aggressively the drone steers toward orbit path during launch (degrees/s).
	 *  Low = gentle arc from launch tube, high = sharp snap to orbit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Launch", meta = (ClampMin = "5.0", ClampMax = "120.0"))
	float LaunchSteerRate = 35.0f;

	// ==================== Prediction Settings ====================

	/** Prediction order: 0 = zero order (aim at current pos), 1 = first order (pos + vel * t) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Prediction", meta = (ClampMin = "0", ClampMax = "1"))
	int32 PredictionOrder = 1;

	/** Turn rate multiplier for difficulty scaling during attack */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Prediction", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float AttackTurnRateMultiplier = 1.0f;

	// ==================== Orbit-to-Attack Timing ====================

	/** Minimum time the drone must orbit before being eligible for token-based attack (seconds).
	 *  Set to 0 for immediate attack eligibility (e.g. for solo drones).
	 *  Does NOT affect retaliation or forced attacks — those always trigger immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Triggers", meta = (ClampMin = "0.0"))
	float MinOrbitTimeBeforeAttack = 2.0f;

	// ==================== Proximity / Retaliation ====================

	/** Radius at which proximity attack triggers if no token for too long */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Triggers")
	float ProximityAttackRadius = 400.0f;

	/** Time without token at proximity radius before emergency attack (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Triggers", meta = (ClampMin = "1.0"))
	float ProximityAttackDelay = 3.0f;

	/** If true, drone retaliates immediately when hit during orbit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Triggers")
	bool bRetaliateOnDamage = true;

	// ==================== Parry Settings ====================

	/** Speed along the parry direction (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Parry")
	float ParrySpiralForwardSpeed = 900.0f;

	/** Spiral rotation speed (radians/s) — how fast it circles */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Parry")
	float ParrySpiralAngularSpeed = 12.0f;

	/** Starting radius of the spiral (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Parry")
	float ParrySpiralStartRadius = 120.0f;

	/** How fast the spiral radius shrinks (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Parry")
	float ParrySpiralRadiusShrinkRate = 40.0f;

	/** Downward bias when no enemy target — makes spiral go toward ground (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Parry")
	float ParryGravityBias = 200.0f;

	/** Mesh spin speed when parried (degrees/s) — visual tumble */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Parry")
	float ParryMeshSpinSpeed = 1080.0f;

	/** Max time before forced explosion if nothing is hit (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Parry", meta = (ClampMin = "0.5"))
	float ParryMaxFlightTime = 3.0f;

	/** Half-angle of the cone in front of player to search for redirect targets (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Parry", meta = (ClampMin = "5", ClampMax = "90"))
	float ParryConeLookHalfAngle = 45.0f;

	/** Max distance to search for redirect targets (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Parry")
	float ParryConeLookDistance = 2000.0f;

	/** Explosion radius for parried drone (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Parry")
	float ParryExplosionRadius = 400.0f;

	/** Damage dealt by parried explosion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Parry")
	float ParryExplosionDamage = 50.0f;

	// ==================== VFX / SFX ====================

	/** Niagara system for full explosion (player collision) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|VFX")
	TObjectPtr<UNiagaraSystem> ExplosionFX;

	/** Niagara system for crash explosion (geometry collision) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|VFX")
	TObjectPtr<UNiagaraSystem> CrashExplosionFX;

	/** Sound for full explosion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|SFX")
	TObjectPtr<USoundBase> ExplosionSound;

	/** Sound for crash explosion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|SFX")
	TObjectPtr<USoundBase> CrashSound;

	/** Sound for telegraph (high-pitched whine) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|SFX")
	TObjectPtr<USoundBase> TelegraphSound;

	/** Looping flight sound (assigned in Blueprint) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|SFX")
	TObjectPtr<USoundBase> FlightSound;

	/** Minimum pitch multiplier (at FlightPitchMinSpeed or below) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|SFX", meta = (ClampMin = "0.3", ClampMax = "2.0"))
	float FlightPitchMin = 0.8f;

	/** Maximum pitch multiplier (at FlightPitchMaxSpeed or above) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|SFX", meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float FlightPitchMax = 1.4f;

	/** Speed at which pitch is at minimum */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|SFX", meta = (ClampMin = "0"))
	float FlightPitchMinSpeed = 200.0f;

	/** Speed at which pitch is at maximum */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|SFX", meta = (ClampMin = "0"))
	float FlightPitchMaxSpeed = 1200.0f;

protected:

	// ==================== Lifecycle ====================

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// ==================== Overrides from ShooterNPC ====================

	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual UMeshComponent* GetHitFlashMeshComponent() const override;
	virtual void ApplyKnockback(const FVector& KnockbackDirection, float Distance, float Duration, const FVector& AttackerLocation = FVector::ZeroVector, bool bKeepEMFEnabled = false) override;
	virtual void EndKnockbackStun() override;
	virtual void OnCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit) override;
	virtual void SpawnDeathGeometryCollection(const FDeathModeConfig& Config) override;
	virtual void EnterCapturedState(UAnimMontage* OverrideMontage = nullptr) override;
	virtual void ExitCapturedState() override;

public:

	// ==================== Public Interface ====================

	/** Get current state */
	UFUNCTION(BlueprintPure, Category = "Kamikaze")
	EKamikazeState GetKamikazeState() const { return CurrentState; }

	/** Get flying movement component */
	UFUNCTION(BlueprintPure, Category = "Kamikaze")
	UFlyingAIMovementComponent* GetFlyingMovement() const { return FlyingMovement; }

	/** Check if drone took damage recently (for StateTree retaliation condition) */
	UFUNCTION(BlueprintPure, Category = "Kamikaze")
	bool TookDamageRecently(float GracePeriod = 0.5f) const;

	/** Clear damage flag (called by StateTree after handling) */
	UFUNCTION(BlueprintCallable, Category = "Kamikaze")
	void ClearDamageTakenFlag() { bTookDamageThisFrame = false; }

	/** Returns true if drone is in an attacking sequence (Telegraph/Attack/PostAttack) */
	UFUNCTION(BlueprintPure, Category = "Kamikaze")
	bool IsInAttackSequence() const;

	/** Returns true if orbit can't be maintained (for forced attack) */
	UFUNCTION(BlueprintPure, Category = "Kamikaze")
	bool IsOrbitForced() const { return bOrbitForced; }

	/** Returns true if this is a retaliation attack */
	UFUNCTION(BlueprintPure, Category = "Kamikaze")
	bool IsRetaliating() const { return bIsRetaliating; }

	// ==================== State Machine Control (for StateTree) ====================

	/** Begin telegraph → attack sequence. Called by StateTree when attack is authorized. */
	UFUNCTION(BlueprintCallable, Category = "Kamikaze")
	void BeginTelegraph(bool bRetaliation = false);

	/** Initialize launch from spawner: sets velocity and enters Launching state.
	 *  Drone will stabilize from launch impulse (FPV PID settling) before entering orbit. */
	UFUNCTION(BlueprintCallable, Category = "Kamikaze")
	void InitiateLaunch(const FVector& LaunchVelocity);

protected:

	// ==================== State Machine ====================

	EKamikazeState CurrentState = EKamikazeState::Orbiting;
	EKamikazeState StateBeforeCapture = EKamikazeState::Orbiting;

	void SetState(EKamikazeState NewState);
	void UpdateLaunching(float DeltaTime);
	void UpdateOrbiting(float DeltaTime);
	void UpdateStrafing(float DeltaTime);
	void UpdatePositioning(float DeltaTime);
	void UpdateTelegraphing(float DeltaTime);
	void UpdateAttacking(float DeltaTime);
	void UpdatePostAttack(float DeltaTime);

	/** Evaluate orbit quality: 8 rays, switch to strafe if too many blocked */
	void EvaluateOrbitQuality();
	void UpdateRecovery(float DeltaTime);
	void UpdateParried(float DeltaTime);

	// ==================== Attack Methods ====================

	/** Commit to attack: calculate predicted position, begin dive */
	void CommitAttack();

	/** Calculate predicted target position using prediction order */
	FVector CalculatePredictedPosition() const;

	// ==================== Parry Methods ====================

	/** Initiate parry: find target in cone or use player look direction, enter Parried state */
	void InitiateParry(AController* AttackerController);

	/** Find best enemy target in cone in front of player. Returns nullptr if none found. */
	AShooterNPC* FindParryTarget(const FVector& PlayerLocation, const FVector& PlayerForward) const;

	// ==================== Death Methods ====================

	/** Master death handler — delegates to specific death type based on state */
	void KamikazeDie();

	/** Debris fall (orbit death — no explosion) */
	void TriggerDebrisFall();

	/** Air explosion (killed during attack near target) */
	void TriggerAirExplosion();

	/** Crash explosion (hit geometry after miss) */
	void TriggerCrashExplosion();

	/** Full collision explosion (direct hit on player) */
	void TriggerCollisionExplosion();

	/** Shared explosion logic: damage, stun, VFX, SFX
	 *  @param Radius Explosion radius
	 *  @param Damage Max damage at center
	 *  @param DamageTypeClass Damage type for the explosion
	 *  @param bDropHealthPickup Whether to drop HP pickups
	 */
	void DoExplosion(float Radius, float Damage, TSubclassOf<UDamageType> DamageTypeClass, bool bDropHealthPickup);

	/** Deferred destruction */
	void DeathDestroy();

	/** Aggressive deactivation of all systems (performance — copied from FlyingDrone pattern) */
	void DeactivateAllSystems();

	// ==================== Orbit State ====================

	/** Current orbit angle (radians) — synced to drone's actual angular position */
	float OrbitAngle = 0.0f;

	/** Cumulative angle traveled for lap counting (resets every 2PI) */
	float OrbitCumulativeAngle = 0.0f;

	/** Current orbit radius */
	float CurrentOrbitRadius = 1100.0f;

	/** Center of the orbit (tracks player with smoothing) */
	FVector OrbitCenter = FVector::ZeroVector;

	/** Time since last orbit center update */
	float TimeSinceOrbitCenterUpdate = 0.0f;

	/** Time since last geometry check */
	float TimeSinceGeometryCheck = 0.0f;

	/** Cumulative time unable to maintain minimum orbit radius */
	float OrbitForcedTimer = 0.0f;

	/** If true, orbit can't be maintained and attack is forced */
	bool bOrbitForced = false;

	/** Per-instance phase offset for vertical sinusoid */
	float OrbitHeightPhaseOffset = 0.0f;

	/** Per-instance speed noise offset */
	float SpeedNoiseTimeOffset = 0.0f;

	/** Time at proximity radius without token */
	float ProximityTimer = 0.0f;

	// ==================== Strafe State ====================

	/** If true, drone is in lateral strafe mode instead of orbiting */
	bool bIsStrafing = false;

	/** Center position for strafe oscillation (assigned by coordinator) */
	FVector StrafeCenter = FVector::ZeroVector;

	/** Lateral axis for strafe oscillation (perpendicular to player-drone direction) */
	FVector StrafeAxis = FVector::ZeroVector;

	/** Current phase of strafe sinusoid */
	float StrafePhase = 0.0f;

	/** Cumulative phase for lap counting (one full sin period = one "lap") */
	float StrafeCumulativePhase = 0.0f;

	/** Timer for orbit quality evaluation (every 1.0s) */
	float OrbitEvaluationTimer = 0.0f;

	/** Number of blocked rays in last orbit evaluation (out of 8) */
	int32 LastBlockedRayCount = 0;

	/** How long the drone has been in orbit (for MinOrbitTimeBeforeAttack) */
	float OrbitElapsedTime = 0.0f;

	// ==================== Attack State ====================

	/** Target position for attack (calculated at commit) */
	FVector AttackTargetPosition = FVector::ZeroVector;

	/** Attack direction (normalized) */
	FVector AttackDirection = FVector::ForwardVector;

	/** Timer for telegraph/post-attack phases */
	float StateTimer = 0.0f;

	/** If true, attack was triggered by retaliation (bypasses token) */
	bool bIsRetaliating = false;

	// ==================== Positioning Bezier State ====================
	// Cubic Bezier from orbit to frontal attack point.
	// P0, P1 are fixed at start. P3 (target) updates dynamically each frame.

	/** Position at start of positioning */
	FVector PositioningStartPos = FVector::ZeroVector;

	/** P1 control point (orbit tangent pull, fixed at start) */
	FVector PositioningP1 = FVector::ZeroVector;

	/** Previous frame position for virtual velocity calculation */
	FVector PositioningPrevPos = FVector::ZeroVector;

	/** Orbit tangent direction at positioning start (for P1 calculation) */
	FVector PositioningOrbitTangent = FVector::ForwardVector;

	// ==================== Telegraph Phantom Lerp ====================
	// Two "ghost" drones are simulated: one continues orbiting, one snaps to attack.
	// The real drone's position is lerped between them over TelegraphDuration.

	/** Drone position at telegraph start — both phantoms originate here */
	FVector TelegraphStartPos = FVector::ZeroVector;

	/** Attack phantom direction (toward predicted target at telegraph start) */
	FVector TelegraphAttackDir = FVector::ForwardVector;

	/** Saved orbit angle for phantom orbit simulation */
	float TelegraphPhantomOrbitAngle = 0.0f;

	/** Saved effective speed for phantom orbit */
	float TelegraphPhantomOrbitSpeed = 0.0f;

	/** Saved orbit center for phantom orbit */
	FVector TelegraphPhantomOrbitCenter = FVector::ZeroVector;

	/** Previous frame's interpolated position (for computing virtual velocity) */
	FVector TelegraphPrevPos = FVector::ZeroVector;

	// ==================== Sweep Collision ====================

	/** Previous frame position — used for sphere sweep hit detection to prevent tunneling */
	FVector PreviousFrameLocation = FVector::ZeroVector;

	/** Sphere sweep from previous to current location. Returns true if hit player → triggers explosion + death. */
	bool CheckPlayerCollisionSweep();

	// ==================== Damage State (for StateTree) ====================

	/** True if damage taken this frame */
	bool bTookDamageThisFrame = false;

	/** Time when last damage was taken */
	float LastDamageTakenTime = -100.0f;

	// ==================== Launch State ====================

	/** Initial launch speed (set by spawner via InitiateLaunch) */
	float LaunchInitialSpeed = 0.0f;

	/** Accumulated time with near-zero position delta (for stuck detection) */
	float StuckAccumulator = 0.0f;

	// ==================== Parry State ====================

	/** Direction the parried drone flies (toward enemy or player look dir) */
	FVector ParryDirection = FVector::ForwardVector;

	/** Right axis perpendicular to ParryDirection (for spiral) */
	FVector ParrySpiralRight = FVector::RightVector;

	/** Up axis perpendicular to ParryDirection (for spiral) */
	FVector ParrySpiralUp = FVector::UpVector;

	/** Current spiral angle (radians) */
	float ParrySpiralAngle = 0.0f;

	/** Current spiral radius */
	float ParryCurrentRadius = 0.0f;

	/** Whether this drone was parried (used to enable stun on explosion) */
	bool bIsParried = false;

	/** Target NPC for redirected parry (can be null — spiral toward ground) */
	TWeakObjectPtr<AActor> ParryTarget;

	// ==================== Internal ====================

	/** Per-instance random stream */
	FRandomStream InstanceRandom;

	/** Death sequence started flag */
	bool bDeathSequenceStarted = false;

	/** Timer for death destruction */
	FTimerHandle DeathSequenceTimer;

	/** Actor to ignore collision with during knockback */
	TWeakObjectPtr<AActor> KnockbackIgnoreActor;

	/** Get the player pawn (cached helper) */
	APawn* GetPlayerPawn() const;
};
