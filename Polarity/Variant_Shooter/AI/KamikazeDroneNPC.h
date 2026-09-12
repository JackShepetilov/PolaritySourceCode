// KamikazeDroneNPC.h
// FPV kamikaze drone: hangs in the air out of melee reach, then strikes at the player in one committed
// run with live lead, and explodes on contact. Retaliation on damage, parry by melee, Polarity charge.

#pragma once

#include "CoreMinimal.h"
#include "ShooterNPC.h"
#include "KamikazeDroneNPC.generated.h"

class UFlyingAIMovementComponent;
class USphereComponent;
class UFPVTiltComponent;
class UNiagaraSystem;
class UAudioComponent;

/** State machine for kamikaze drone behavior.
 *  The names predate the current flight model and are kept because StateTree assets and tasks
 *  switch on them:
 *    Orbiting   = HOLD: hangs at a point above and beside the target, out of melee reach
 *    Attacking  = STRIKE: one committed run at the target with live lead
 *    Recovery   = after a miss: turns back and climbs to the hold point
 *    PostAttack = unused by the current flight model, kept for asset compatibility */
UENUM(BlueprintType)
enum class EKamikazeState : uint8
{
	Launching,      // Post-spawn: ejected from a spawner or a carrier, settling before it flies on its own
	Orbiting,       // Hold (see above)
	Attacking,      // Strike (see above)
	PostAttack,     // Unused, kept for asset compatibility
	Recovery,       // Back to the hold point after a miss
	Parried,        // Melee-parried: spiraling toward target/ground, will explode with stun
	Dead
};

/** How the drone reaches its target. */
UENUM(BlueprintType)
enum class EAttackPattern : uint8
{
	/** Default: hold, then strike at the target pawn with lead. */
	Orbit,
	/** Skip the hold and fly straight at BuildingTarget's location. */
	Direct
};

/**
 * FPV Kamikaze Drone NPC.
 *
 * Inherits from AShooterNPC (NOT AFlyingDrone): no weapon, attacks by collision.
 *
 * Flight is driven by hand on the authority, not through the CharacterMovementComponent's own
 * physics: the drone computes its velocity itself every frame and moves with a swept, sliding move.
 * That is what makes the path readable (no hidden acceleration model between the numbers below and
 * what the player sees) and what keeps it out of geometry (every move is a sweep that slides along
 * whatever it touches). The CMC stays as velocity storage, collision owner and replication source,
 * and gets its own tick back only while something else drives the pawn (knockback).
 *
 * Why the strike is fair: the drone is out of reach of the player's hands except during its own
 * committed strike, and the strike is a constant-speed run with a readable turn rate. Straight
 * running cannot escape it (the lead catches a constant velocity), a velocity the drone cannot turn
 * with can (a well-timed grapple). The parry window is simply the time the drone spends inside
 * melee reach before contact, set by StrikeSpeed.
 */
UCLASS()
class POLARITY_API AKamikazeDroneNPC : public AShooterNPC
{
	GENERATED_BODY()

	/** Reads the Schedule* tuning of the drone it is scheduling. */
	friend class UKamikazeStrikeSubsystem;

public:

	AKamikazeDroneNPC(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:

	// ==================== Components ====================

	/** Flying AI movement component, kept for the CMC setup in its BeginPlay; its tick is off */
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

	/** Radius of the drone's collision sphere at construction (cm). Every runtime check reads the
	 *  actual capsule instead, so a Blueprint that resizes the capsule stays consistent. */
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

	/** Killed mid-strike closer than this to the target: it blows up in the air instead of falling (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Combat")
	float AttackDeathDistanceThreshold = 400.0f;

	// ==================== Hold ====================

	/** Horizontal distance from the target while holding (cm). Keep it well past melee reach plus the
	 *  melee lunge, or the player can hit the drone whenever they like instead of when it strikes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Hold", meta = (ClampMin = "300", Units = "cm"))
	float HoldDistance = 1200.0f;

	/** Height of the hold point above the target's feet (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Hold", meta = (ClampMin = "0", Units = "cm"))
	float HoldHeight = 600.0f;

	/** Top speed while moving to the hold point and back to it after a miss (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Hold", meta = (ClampMin = "100", Units = "cm/s"))
	float HoldSpeed = 1500.0f;

	/** How fast the velocity may change while holding or recovering, in cm/s^2. Sets how wide the
	 *  turn-around arc after a miss is. (No Units: the engine has no unit for acceleration.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Hold", meta = (ClampMin = "100"))
	float HoldAcceleration = 2500.0f;

	/** Seconds spent at the hold point, with line of sight, before the drone is ready to strike. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Hold", meta = (ClampMin = "0", Units = "s"))
	float HoldTimeBeforeStrike = 2.0f;

	/** Once the hold time is up, ask the strike queue by itself and strike when granted. Off = only
	 *  the StateTree starts strikes (it sees the same readiness through IsProximityTimedOut, and its
	 *  BeginAttack passes the same queue). Both on is safe: a strike starts once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Hold")
	bool bSelfStrike = true;

	// ==================== Hold: staying alive in the air ====================
	// A real FPV in acro mode does not hang still: nothing levels it, and with the camera tilted up a
	// still drone looks at the sky. The pilot keeps it drifting and turning. These make the hold do
	// the same, smoothly, so it stays a trackable target.

	/** Sideways reach of the figure-eight drift around the hold point (cm). Zero = holds still. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Hold|Drift", meta = (ClampMin = "0", Units = "cm"))
	float DriftRadius = 200.0f;

	/** Up and down reach of the drift (cm): throttle never quite holds a height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Hold|Drift", meta = (ClampMin = "0", Units = "cm"))
	float DriftVertical = 40.0f;

	/** Seconds for one full figure-eight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Hold|Drift", meta = (ClampMin = "0.5", Units = "s"))
	float DriftPeriod = 5.0f;

	/** Every this many seconds the drone slides to the other side of its sector. Zero = never. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Hold|Drift", meta = (ClampMin = "0", Units = "s"))
	float RepositionInterval = 3.0f;

	/** How far it slides around the player, from one side of its sector to the other (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Hold|Drift", meta = (ClampMin = "0", ClampMax = "90"))
	float RepositionAngle = 40.0f;

	/** Seconds the slide takes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Hold|Drift", meta = (ClampMin = "0.1", Units = "s"))
	float RepositionTime = 1.5f;

	// ==================== Strike ====================

	/** Constant speed of the strike run (cm/s). Must beat the target's top ground speed or straight
	 *  running escapes it; the parry window is roughly (melee reach) / StrikeSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Strike", meta = (ClampMin = "100", Units = "cm/s"))
	float StrikeSpeed = 1200.0f;

	/** How fast it gets up to StrikeSpeed from the hold, in cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Strike", meta = (ClampMin = "100"))
	float StrikeAcceleration = 4000.0f;

	/** How fast the strike can turn (degrees/s). The whole dodge knob: what the drone can turn with,
	 *  it catches; what it cannot, it misses. Never switched off before impact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Strike", meta = (ClampMin = "10", ClampMax = "720"))
	float StrikeTurnRate = 240.0f;

	/** The strike aims this far above the target's feet (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Strike", meta = (ClampMin = "0", Units = "cm"))
	float AimHeightAboveFeet = 50.0f;

	/** Longest lead the strike takes, as seconds of the target's current velocity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Strike", meta = (ClampMin = "0", Units = "s"))
	float MaxLeadTime = 1.0f;

	/** Contact means the drone's sphere, inflated by this much, touches a hostile pawn (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Strike", meta = (ClampMin = "0", Units = "cm"))
	float ContactFuseRadius = 20.0f;

	/** A strike that has not connected after this long is called a miss (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Strike", meta = (ClampMin = "0.5", Units = "s"))
	float StrikeMaxTime = 3.0f;

	/** Before the run, the drone backs off and up for this long and dips its nose: the pilot lining
	 *  up the dive, and the player's warning that this one goes next (seconds). Zero = no wind-up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Strike", meta = (ClampMin = "0", Units = "s"))
	float WindUpTime = 0.3f;

	/** How far the wind-up backs off (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Strike", meta = (ClampMin = "0", Units = "cm"))
	float WindUpDistance = 100.0f;

	/** After a miss the drone punches out: full throttle up for this long before turning back (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Strike", meta = (ClampMin = "0", Units = "s"))
	float PunchOutTime = 0.5f;

	/** Climb speed of the punch-out (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Strike", meta = (ClampMin = "0", Units = "cm/s"))
	float PunchOutSpeed = 900.0f;

	/** How much the run curves instead of coming straight (0 = straight, 1 = a wide arc to one side).
	 *  The curve tightens as the drone closes, so it still lands. Meant to rise as a run goes on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Strike", meta = (ClampMin = "0", ClampMax = "1"))
	float StrikeArc = 0.0f;

	// ==================== Strike schedule ====================
	// What the strike schedule assumes about the player when it spaces this drone's strike from the
	// one before it. Deliberately constants on the drone, not read off the player's weapon: the
	// schedule is a promise about what a player can do, not about what this one happens to carry.

	/** Seconds the player needs to kill this drone once it is in the crosshair. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Schedule", meta = (ClampMin = "0", Units = "s"))
	float ScheduleKillTime = 0.3f;

	/** How fast the player is assumed to turn from one drone to the next (degrees/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Schedule", meta = (ClampMin = "1"))
	float ScheduleTurnSpeed = 400.0f;

	/** Seconds from the strike's sound cue to the player reacting, for the first strike of a run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Schedule", meta = (ClampMin = "0", Units = "s"))
	float ScheduleReaction = 0.25f;

	/** Multiplier on the gap between strikes: 1 = just enough for a perfect player, higher is kinder. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Schedule", meta = (ClampMin = "0.1"))
	float ScheduleSlack = 1.2f;

	/** Rounds a kill takes. Fewer than this in the player's magazine and the gap before this strike
	 *  includes the weapon's reload time, once per empty magazine. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Schedule", meta = (ClampMin = "1"))
	int32 ScheduleKillBullets = 3;

	/** How the drone reaches its target. Direct skips the hold and flies at BuildingTarget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Strike")
	EAttackPattern AttackPattern = EAttackPattern::Orbit;

	/** Fixed actor target used when AttackPattern == Direct (e.g. ATurretBuilding). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Strike")
	TObjectPtr<AActor> BuildingTarget;

	/** Exact world-space point the drone aims for in Direct mode. Set by InitiateDirectAttack. */
	UPROPERTY(BlueprintReadOnly, Category = "Kamikaze|Strike")
	FVector DirectAttackTargetLocation = FVector::ZeroVector;

	// ==================== Targeting ====================

	/** Pick a target on our own instead of asking the combat coordinator, and pick a new one when
	 *  the current one dies. A carrier's munitions are not registered with the coordinator, so
	 *  without this they would inherit the carrier's opinion or nothing at all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Targeting")
	bool bSelfDesignateTarget = false;

	/** Minimum seconds between self-designation sweeps. The sweep walks pawns, so it is not free. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Targeting", meta = (ClampMin = "0.1"))
	float TargetReacquireInterval = 0.5f;

	// ==================== Launch Stabilization ====================

	/** Max time for launch stabilization before forcing the hand-off (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Launch")
	float MaxLaunchStabilizationTime = 1.5f;

	/** Speed decay rate during launch (higher = faster settling toward HoldSpeed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Launch", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float LaunchDecayRate = 3.0f;

	/** How fast the launched drone turns toward where it is going (degrees/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|Launch", meta = (ClampMin = "5.0", ClampMax = "360.0"))
	float LaunchSteerRate = 35.0f;

	// ==================== Retaliation ====================

	/** If true, the drone is flagged to strike immediately when hit while holding */
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

	/** Sound played when a strike starts: the audible telegraph */
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

	/** Acceleration that alone drives the motor pitch to maximum, in cm/s^2: the motors scream when
	 *  the drone changes speed, not only when it is fast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kamikaze|SFX", meta = (ClampMin = "1"))
	float FlightPitchMaxAccel = 3000.0f;

protected:

	// ==================== Lifecycle ====================

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	// ==================== Overrides from ShooterNPC ====================

	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual UMeshComponent* GetHitFlashMeshComponent() const override;
	virtual void ApplyKnockback(const FVector& KnockbackDirection, float Distance, float Duration, const FVector& AttackerLocation = FVector::ZeroVector, bool bKeepEMFEnabled = false, EKnockbackStyle Style = EKnockbackStyle::Standard) override;
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

	/** Returns true while a strike, its recovery or a parry is running */
	UFUNCTION(BlueprintPure, Category = "Kamikaze")
	bool IsInAttackSequence() const;

	/** True once the drone has held its point long enough to strike (read by the StateTree
	 *  condition). The name is historical: it used to mean "loitered too close for too long". */
	UFUNCTION(BlueprintPure, Category = "Kamikaze")
	bool IsProximityTimedOut() const { return bProximityTimedOut; }

	/** Returns true if this is a retaliation attack */
	UFUNCTION(BlueprintPure, Category = "Kamikaze")
	bool IsRetaliating() const { return bIsRetaliating; }

	// ==================== State Machine Control ====================

	/** Start a strike from the hold or the recovery. Called by the StateTree, or by the drone itself
	 *  when bSelfStrike is on and the hold time is up. Does nothing in any other state. */
	UFUNCTION(BlueprintCallable, Category = "Kamikaze")
	void BeginAttack(bool bRetaliation = false);

	/** Initialize launch from spawner: sets velocity and enters Launching state. The drone settles
	 *  from the launch impulse and then goes to its hold point. */
	UFUNCTION(BlueprintCallable, Category = "Kamikaze")
	void InitiateLaunch(const FVector& LaunchVelocity);

	/** Launch this drone as a carrier-dropped munition: eject along LaunchVelocity, settle, then hold
	 *  and wait for the strike schedule like any other drone (the author's call: a carrier delivers,
	 *  the schedule decides). It picks its own target when no player is available. */
	UFUNCTION(BlueprintCallable, Category = "Kamikaze")
	void LaunchAsHomingMunition(const FVector& LaunchVelocity);

	/** Configure this drone as a direct attacker against a fixed actor target: no hold, no lead,
	 *  straight at TargetWorldLocation. Used by ATurretBuilding.
	 *  @param Target             Actor the drone is attacking (stored as BuildingTarget)
	 *  @param TargetWorldLocation Exact world-space point the drone will dive toward */
	UFUNCTION(BlueprintCallable, Category = "Kamikaze")
	void InitiateDirectAttack(AActor* Target, FVector TargetWorldLocation);

protected:

	// ==================== State Machine ====================

	EKamikazeState CurrentState = EKamikazeState::Orbiting;
	EKamikazeState StateBeforeCapture = EKamikazeState::Orbiting;

	void SetState(EKamikazeState NewState);
	void UpdateLaunching(float DeltaTime);
	void UpdateHold(float DeltaTime, bool bCountTowardStrike);
	void UpdateStrike(float DeltaTime);
	void UpdateParried(float DeltaTime);

	// ==================== Flight ====================

	/** The one mover for every hand-driven state: store Velocity, sweep, slide along whatever is hit.
	 *  Returns the first blocking hit of the move (not the slide), for callers that react to walls. */
	FHitResult FlyMove(const FVector& NewVelocity, float DeltaTime);

	/** Hand the pawn to our own flight (true) or back to the CMC's own tick (false). */
	void SetHandDrivenFlight(bool bHandDriven);

	/** Where the target's feet are. */
	FVector GetTargetFeet(const APawn* Target) const;

	/** Point above and beside the target, keeping the bearing the drone is already on. */
	FVector ComputeHoldPoint(const APawn* Target) const;

	/** Where the strike aims this frame: above the feet, led by the target's velocity. */
	FVector ComputeStrikeAimPoint() const;

	/** Velocity that arrives at Point and stops there, without overshoot, within SpeedCap. */
	FVector ArriveVelocity(const FVector& Point, float SpeedCap, float Acceleration) const;

	/** Hostile pawn contact along this frame's path. Returns true if the drone detonated. */
	bool CheckContact();

	// ==================== Parry Methods ====================

	/** Initiate parry: find target in cone or use attacker look direction, enter Parried state */
	void InitiateParry(AController* AttackerController);

	/** Find best hostile-of-attacker pawn in cone in front of the parrier. Returns nullptr if none found. */
	APawn* FindParryTarget(const APawn* AttackerPawn, const FVector& PlayerLocation, const FVector& PlayerForward) const;

	// ==================== Death Methods ====================

	/** Master death handler — delegates to specific death type based on state */
	void KamikazeDie();

	/** Debris fall (hold death — no explosion) */
	void TriggerDebrisFall();

	/** Air explosion (killed during strike near target) */
	void TriggerAirExplosion();

	/** Crash explosion (hit geometry) */
	void TriggerCrashExplosion();

	/** Full collision explosion (direct hit on player) */
	void TriggerCollisionExplosion();

	/** Shared explosion logic: damage, stun, VFX, SFX */
	void DoExplosion(float Radius, float Damage, TSubclassOf<UDamageType> DamageTypeClass, bool bDropHealthPickup);

	/** Deferred destruction */
	void DeathDestroy();

	/** Aggressive deactivation of all systems (performance — copied from FlyingDrone pattern) */
	void DeactivateAllSystems();

	// ==================== Hold / Strike State ====================

	/** Seconds held at the hold point with line of sight, toward HoldTimeBeforeStrike. */
	float HoldTimer = 0.0f;

	/** Set once HoldTimer reaches HoldTimeBeforeStrike (read by the StateTree condition). */
	bool bProximityTimedOut = false;

	/** Aim point of the current strike, for debug drawing. */
	FVector AttackTargetPosition = FVector::ZeroVector;

	/** Timer for the current state */
	float StateTimer = 0.0f;

	/** If true, the drone was shot while holding: it skips the rest of the wait and goes to the front
	 *  of the strike queue (still one strike at a time per player) */
	bool bIsRetaliating = false;

	/** The strike queue said yes; consumed by BeginAttack. */
	bool bStrikeGranted = false;

	/** The player the strike queue handed this drone, refreshed every frame on the authority. */
	TWeakObjectPtr<APawn> QueueTarget;

	/** Per-drone phase of the drift, so a group of drones does not sway in step. */
	float DriftPhase = 0.0f;

	/** The side it came in from, around its target (degrees), fixed the first time it holds. */
	float HoldBearingDeg = 0.0f;
	bool bHasHoldBearing = false;

	/** Which way the current strike curves (+1 or -1), picked when it starts. */
	float StrikeArcSide = 1.0f;

	/** Current slide around the sector (degrees) and the side it is heading for (+1 or -1). */
	float RepositionOffsetDeg = 0.0f;
	float RepositionSide = 1.0f;
	float RepositionTimer = 0.0f;

	/** Velocity last frame and a smoothed acceleration, for the tilt and the motor pitch. */
	FVector LastVelocity = FVector::ZeroVector;
	FVector SmoothedAcceleration = FVector::ZeroVector;

	/** The server's strike queue, or null on a client. */
	class UKamikazeStrikeSubsystem* GetStrikeQueue() const;

	// ==================== Detonation Sweep ====================

	/** Previous frame position — used for sphere sweep hit detection to prevent tunneling */
	FVector PreviousFrameLocation = FVector::ZeroVector;

	// ==================== Damage State (for StateTree) ====================

	/** True if damage taken this frame */
	bool bTookDamageThisFrame = false;

	/** Time when last damage was taken */
	float LastDamageTakenTime = -100.0f;

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

	/** Death sequence started flag */
	bool bDeathSequenceStarted = false;

	/** Timer for death destruction */
	FTimerHandle DeathSequenceTimer;

	/** Actor to ignore collision with during knockback */
	TWeakObjectPtr<AActor> KnockbackIgnoreActor;

	/** Whoever this drone is acting against. Not necessarily a player: with factions the drone is a
	 *  squad's expendable and can be sent at another faction's pawn.
	 *
	 *  The coordinator owns the choice; this only reads it, and caches the answer so a frame where
	 *  the coordinator is missing does not make the drone flip mid-strike. Nearest hostile is the
	 *  fallback for a drone nobody has registered. */
	APawn* GetTargetPawn() const;

	/** Backing store for GetTargetPawn. Mutable because the accessor is const and every
	 *  caller is a read. Cleared implicitly when the pawn dies (weak pointer), which re-picks. */
	mutable TWeakObjectPtr<APawn> CachedTarget;

	/** World time of the last self-designation sweep. Mutable for the same reason as CachedTarget. */
	mutable float LastReacquireTime = -100.0f;
};
