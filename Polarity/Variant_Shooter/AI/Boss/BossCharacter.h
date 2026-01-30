// BossCharacter.h
// Hybrid boss character that switches between ground and aerial phases

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "BossCharacter.generated.h"

class UFlyingAIMovementComponent;
class UAnimMontage;
class UNiagaraSystem;
class AShooterProjectile;
class ABossProjectile;

/** Boss phase enumeration */
UENUM(BlueprintType)
enum class EBossPhase : uint8
{
	Ground		UMETA(DisplayName = "Ground Phase"),
	Aerial		UMETA(DisplayName = "Aerial Phase"),
	Finisher	UMETA(DisplayName = "Finisher Phase")
};

/** Delegate for phase change events */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBossPhaseChanged, EBossPhase, OldPhase, EBossPhase, NewPhase);

/** Delegate for finisher ready event */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBossFinisherReady);

/** Delegate for boss defeated event */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBossDefeated);

/**
 * Hybrid boss character that switches between ground melee and aerial ranged phases.
 * Ground phase: Arc dashes around player with single melee attacks
 * Aerial phase: Hovers and shoots EMF projectiles that can be parried
 * Finisher phase: Invulnerable, waiting for melee finisher from player
 */
UCLASS()
class POLARITY_API ABossCharacter : public AShooterNPC
{
	GENERATED_BODY()

public:

	ABossCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:

	// ==================== Components ====================

	/** Flying AI movement component for aerial phase */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss|Components")
	TObjectPtr<UFlyingAIMovementComponent> FlyingMovement;

	// ==================== Phase Settings ====================

	/** Current boss phase */
	UPROPERTY(BlueprintReadOnly, Category = "Boss|Phase")
	EBossPhase CurrentPhase = EBossPhase::Ground;

	/** HP threshold (percentage 0-1) to transition from Ground to Aerial phase */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Phase Transitions", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AerialPhaseHPThreshold = 0.7f;

	/** Number of dash attacks before transitioning to Aerial phase (if HP threshold not reached) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Phase Transitions", meta = (ClampMin = "1"))
	int32 DashAttacksBeforeAerialPhase = 5;

	/** Number of successful parries before transitioning back to Ground phase */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Phase Transitions", meta = (ClampMin = "1"))
	int32 ParriesBeforeGroundPhase = 3;

	/** Maximum time in Aerial phase before forced transition to Ground (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Phase Transitions", meta = (ClampMin = "5.0"))
	float MaxAerialPhaseDuration = 20.0f;

	/** Time it takes to transition from Ground to Aerial (take off) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Phase Transitions", meta = (ClampMin = "0.5"))
	float TakeOffDuration = 1.5f;

	/** Time it takes to transition from Aerial to Ground (landing) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Phase Transitions", meta = (ClampMin = "0.5"))
	float LandingDuration = 1.0f;

	/** Is boss currently transitioning between phases (cannot attack during transition) */
	UPROPERTY(BlueprintReadOnly, Category = "Boss|Phase")
	bool bIsTransitioning = false;

	// ==================== Ground Phase (Melee) Settings ====================

	/** Maximum dash distance in cm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Dash", meta = (ClampMin = "100.0"))
	float MaxDashDistance = 2000.0f;

	/** Dash speed in cm/s */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Dash", meta = (ClampMin = "500.0"))
	float DashSpeed = 2500.0f;

	/** Cooldown between dashes in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Dash", meta = (ClampMin = "0.5"))
	float DashCooldown = 1.5f;

	/** Minimum angle offset when selecting dash target point (degrees from direct line to player) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Dash", meta = (ClampMin = "30.0", ClampMax = "150.0"))
	float MinDashAngleOffset = 45.0f;

	/** Maximum angle offset when selecting dash target point */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Dash", meta = (ClampMin = "30.0", ClampMax = "180.0"))
	float MaxDashAngleOffset = 135.0f;

	/** Distance from player where dash will end (attack range) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Dash", meta = (ClampMin = "50.0"))
	float DashTargetDistanceFromPlayer = 150.0f;

	/** Array of melee attack montages */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Melee")
	TArray<TObjectPtr<UAnimMontage>> MeleeAttackMontages;

	/** Melee attack damage */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Melee", meta = (ClampMin = "0"))
	float MeleeAttackDamage = 50.0f;

	/** Melee attack range */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Melee", meta = (ClampMin = "50.0"))
	float MeleeAttackRange = 200.0f;

	/** Cooldown between melee attacks */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Melee", meta = (ClampMin = "0.1"))
	float MeleeAttackCooldown = 0.5f;

	/** Speed at which boss is pulled towards player during melee attack (cm/s).
	 *  This punishes slow players - if you stand still, boss will track you.
	 *  Fast moving players can dodge by outrunning the pull. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Melee", meta = (ClampMin = "0.0"))
	float MeleeAttackPullSpeed = 500.0f;

	/** Radius of melee trace sphere */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Melee", meta = (ClampMin = "10.0"))
	float MeleeTraceRadius = 50.0f;

	/** Distance of melee trace */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Melee", meta = (ClampMin = "50.0"))
	float MeleeTraceDistance = 150.0f;

	// ==================== Aerial Phase (Ranged) Settings ====================

	/** Hover height above ground during aerial phase (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Aerial Phase", meta = (ClampMin = "300.0"))
	float AerialHoverHeight = 900.0f;

	/** Strafe speed during aerial phase (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Aerial Phase", meta = (ClampMin = "100.0"))
	float AerialStrafeSpeed = 400.0f;

	/** Interval between projectile shots (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Aerial Phase", meta = (ClampMin = "0.5"))
	float ShootInterval = 1.75f;

	/** Dash distance before/after shooting (for evasion from rifle) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Aerial Phase", meta = (ClampMin = "100.0"))
	float AerialDashDistance = 400.0f;

	/** If true, perform dash before shooting */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Aerial Phase")
	bool bDashBeforeShooting = true;

	/** If true, perform dash after parry detected */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Aerial Phase")
	bool bDashAfterParry = true;

	/** Projectile class to spawn (should be BossProjectile or subclass) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Aerial Phase")
	TSubclassOf<ABossProjectile> BossProjectileClass;

	/** Speed of boss projectiles */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Aerial Phase", meta = (ClampMin = "100.0"))
	float ProjectileSpeed = 2000.0f;

	// ==================== Projectile Tracking Settings ====================

	/** Distance threshold to consider projectile "close" to boss for parry detection */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Aerial Phase|Parry Detection", meta = (ClampMin = "100.0"))
	float ParryDetectionRadius = 500.0f;

	/** Minimum angle between projectile velocity and direction to boss to consider it "returning" (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Aerial Phase|Parry Detection", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float ParryReturnAngleThreshold = 45.0f;

	/** Interval for checking parry status of tracked projectiles (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Aerial Phase|Parry Detection", meta = (ClampMin = "0.01"))
	float ParryCheckInterval = 0.1f;

	// ==================== Finisher Phase Settings ====================

	/** Height to hover at during finisher phase (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Finisher Phase", meta = (ClampMin = "200.0"))
	float FinisherHoverHeight = 700.0f;

	/** Location offset from arena center for finisher hover (set in Blueprint or dynamically) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Finisher Phase")
	FVector FinisherHoverOffset = FVector::ZeroVector;

	/** VFX to play during finisher vulnerability state */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Finisher Phase|VFX")
	TObjectPtr<UNiagaraSystem> FinisherVulnerabilityVFX;

	/** Scale for finisher vulnerability VFX */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Finisher Phase|VFX", meta = (ClampMin = "0.1"))
	float FinisherVFXScale = 1.5f;

	// ==================== Runtime State ====================

	/** Counter for dash attacks in current ground phase */
	int32 CurrentDashAttackCount = 0;

	/** Counter for parries in current aerial phase */
	int32 CurrentParryCount = 0;

	/** Time when aerial phase started */
	float AerialPhaseStartTime = 0.0f;

	/** True if boss is in finisher phase and invulnerable */
	bool bIsInFinisherPhase = false;

	/** True if boss is currently dashing */
	bool bIsDashing = false;

	/** True if boss is currently performing melee attack */
	bool bIsAttacking = false;

	/** True if melee attack cooldown is active */
	bool bMeleeOnCooldown = false;

	/** True if dash cooldown is active */
	bool bDashOnCooldown = false;

	/** Last time dash was performed */
	float LastDashTime = -10.0f;

	/** Last time melee attack was performed */
	float LastMeleeAttackTime = -10.0f;

	/** Maximum HP (cached on BeginPlay for threshold calculations) */
	float MaxHP = 1000.0f;

	// ==================== Dash State ====================

	/** True if current dash is approach (to player), false if circle (around player) */
	bool bIsApproachDash = false;

	/** Start position of current dash (world space, for Z height reference) */
	FVector DashStartPosition = FVector::ZeroVector;

	/** Starting angle around player (radians) */
	float DashStartAngle = 0.0f;

	/** Target angle around player (radians) */
	float DashTargetAngle = 0.0f;

	/** Starting distance from player */
	float DashStartRadius = 0.0f;

	/** Target distance from player (approach dash ends at melee range) */
	float DashTargetRadius = 0.0f;

	/** Arc direction: 1 = counter-clockwise, -1 = clockwise */
	float DashArcDirection = 1.0f;

	/** Elapsed time in current dash */
	float DashElapsedTime = 0.0f;

	/** Total duration of current dash */
	float DashTotalDuration = 0.0f;

	/** Current target actor for attacks */
	TWeakObjectPtr<AActor> CurrentTarget;

	// ==================== Melee State ====================

	/** Actors hit during current melee attack (to prevent multi-hit) */
	TSet<AActor*> HitActorsThisAttack;

	/** True if damage window is currently active */
	bool bDamageWindowActive = false;

	// ==================== Projectile Tracking State ====================

	/** Projectiles fired by this boss (for parry detection) */
	UPROPERTY()
	TArray<TWeakObjectPtr<AShooterProjectile>> TrackedProjectiles;

	/** Last polarity sign of projectile at spawn time (to detect player polarity change) */
	TMap<TWeakObjectPtr<AShooterProjectile>, int32> ProjectileOriginalTargetPolarity;

	/** Time of last parry check */
	float LastParryCheckTime = 0.0f;

	// ==================== Timers ====================

	FTimerHandle DashCooldownTimer;
	FTimerHandle MeleeCooldownTimer;
	FTimerHandle DamageWindowStartTimer;
	FTimerHandle DamageWindowEndTimer;
	FTimerHandle AerialPhaseTimer;
	FTimerHandle ParryCheckTimer;
	FTimerHandle PhaseTransitionTimer;

public:

	// ==================== Events ====================

	/** Called when boss phase changes */
	UPROPERTY(BlueprintAssignable, Category = "Boss|Events")
	FOnBossPhaseChanged OnPhaseChanged;

	/** Called when boss enters finisher phase */
	UPROPERTY(BlueprintAssignable, Category = "Boss|Events")
	FOnBossFinisherReady OnFinisherReady;

	/** Called when boss is defeated (finisher executed) */
	UPROPERTY(BlueprintAssignable, Category = "Boss|Events")
	FOnBossDefeated OnBossDefeated;

protected:

	// ==================== Lifecycle ====================

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ==================== Damage Handling ====================

	/** Override damage handling to implement finisher phase */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

public:

	// ==================== Phase Control ====================

	/** Get current boss phase */
	UFUNCTION(BlueprintPure, Category = "Boss|Phase")
	EBossPhase GetCurrentPhase() const { return CurrentPhase; }

	/** Manually trigger phase transition */
	UFUNCTION(BlueprintCallable, Category = "Boss|Phase")
	void SetPhase(EBossPhase NewPhase);

	/** Check if conditions are met to transition to aerial phase */
	UFUNCTION(BlueprintPure, Category = "Boss|Phase")
	bool ShouldTransitionToAerial() const;

	/** Check if conditions are met to transition to ground phase */
	UFUNCTION(BlueprintPure, Category = "Boss|Phase")
	bool ShouldTransitionToGround() const;

	/** Check if boss is currently transitioning between phases */
	UFUNCTION(BlueprintPure, Category = "Boss|Phase")
	bool IsTransitioning() const { return bIsTransitioning; }

	// ==================== Ground Phase Interface ====================

	/** Start approach dash - moves TOWARDS player to close distance */
	UFUNCTION(BlueprintCallable, Category = "Boss|Ground Phase")
	bool StartApproachDash(AActor* Target);

	/** Start circle dash - moves AROUND player at current distance */
	UFUNCTION(BlueprintCallable, Category = "Boss|Ground Phase")
	bool StartCircleDash(AActor* Target);

	/** Returns true if boss can currently dash */
	UFUNCTION(BlueprintPure, Category = "Boss|Ground Phase")
	bool CanDash() const;

	/** Returns true if boss is currently dashing */
	UFUNCTION(BlueprintPure, Category = "Boss|Ground Phase")
	bool IsDashing() const { return bIsDashing; }

	/** Returns true if target is far (needs approach dash) */
	UFUNCTION(BlueprintPure, Category = "Boss|Ground Phase")
	bool IsTargetFar(AActor* Target) const;

	/** Start a melee attack against the target */
	UFUNCTION(BlueprintCallable, Category = "Boss|Ground Phase")
	void StartMeleeAttack(AActor* Target);

	/** Returns true if boss can currently perform melee attack */
	UFUNCTION(BlueprintPure, Category = "Boss|Ground Phase")
	bool CanMeleeAttack() const;

	/** Returns true if boss is currently attacking */
	UFUNCTION(BlueprintPure, Category = "Boss|Ground Phase")
	bool IsAttacking() const { return bIsAttacking; }

	/** Returns true if target is within melee attack range */
	UFUNCTION(BlueprintPure, Category = "Boss|Ground Phase")
	bool IsTargetInMeleeRange(AActor* Target) const;

	// ==================== Aerial Phase Interface ====================

	/** Transition to hovering state */
	UFUNCTION(BlueprintCallable, Category = "Boss|Aerial Phase")
	void StartHovering();

	/** Land from hovering state */
	UFUNCTION(BlueprintCallable, Category = "Boss|Aerial Phase")
	void StopHovering();

	/** Perform aerial strafe movement */
	UFUNCTION(BlueprintCallable, Category = "Boss|Aerial Phase")
	void AerialStrafe(const FVector& Direction);

	/** Perform aerial dash (for evasion) */
	UFUNCTION(BlueprintCallable, Category = "Boss|Aerial Phase")
	bool PerformAerialDash();

	/** Change polarity to opposite of target's */
	UFUNCTION(BlueprintCallable, Category = "Boss|Aerial Phase")
	void MatchOppositePolarity(AActor* Target);

	/** Register a successful parry (projectile returned) */
	UFUNCTION(BlueprintCallable, Category = "Boss|Aerial Phase")
	void RegisterParry();

	/** Fire EMF projectile at target and track it for parry detection */
	UFUNCTION(BlueprintCallable, Category = "Boss|Aerial Phase")
	void FireEMFProjectile(AActor* Target);

	/** Track a projectile for parry detection */
	UFUNCTION(BlueprintCallable, Category = "Boss|Aerial Phase")
	void TrackProjectile(AShooterProjectile* Projectile);

	/** Called by BossProjectile when parry is detected */
	UFUNCTION(BlueprintCallable, Category = "Boss|Aerial Phase")
	void OnProjectileParried(ABossProjectile* Projectile);

	// ==================== Finisher Phase Interface ====================

	/** Enter finisher phase (invulnerable, waiting for melee hit) */
	UFUNCTION(BlueprintCallable, Category = "Boss|Finisher Phase")
	void EnterFinisherPhase();

	/** Execute finisher (called when player lands melee hit in finisher phase) */
	UFUNCTION(BlueprintCallable, Category = "Boss|Finisher Phase")
	void ExecuteFinisher(AActor* Attacker);

	/** Returns true if boss is in finisher phase */
	UFUNCTION(BlueprintPure, Category = "Boss|Finisher Phase")
	bool IsInFinisherPhase() const { return bIsInFinisherPhase; }

	// ==================== Target Management ====================

	/** Set current target */
	UFUNCTION(BlueprintCallable, Category = "Boss|Target")
	void SetTarget(AActor* NewTarget);

	/** Get current target */
	UFUNCTION(BlueprintPure, Category = "Boss|Target")
	AActor* GetTarget() const { return CurrentTarget.Get(); }

	// ==================== Component Getters ====================

	/** Get flying movement component */
	UFUNCTION(BlueprintPure, Category = "Boss|Components")
	UFlyingAIMovementComponent* GetFlyingMovement() const { return FlyingMovement; }

protected:

	// ==================== Internal Methods ====================

	/** Calculate arc dash target position around the player */
	FVector CalculateArcDashTarget(AActor* Target) const;

	/** Calculate control point for bezier arc dash */
	FVector CalculateArcControlPoint(const FVector& Start, const FVector& End, AActor* Target) const;

	/** Update dash interpolation along arc */
	void UpdateArcDash(float DeltaTime);

	/** End current dash */
	void EndDash();

	/** Called when dash cooldown ends */
	void OnDashCooldownEnd();

	/** Called when melee cooldown ends */
	void OnMeleeCooldownEnd();

	/** Perform melee trace and apply damage */
	void PerformMeleeTrace();

	/** Called when damage window starts */
	void OnDamageWindowStart();

	/** Called when damage window ends */
	void OnDamageWindowEnd();

	/** Called when attack montage ends */
	UFUNCTION()
	void OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	/** Apply melee damage to hit actor */
	void ApplyMeleeDamage(AActor* HitActor, const FHitResult& HitResult);

	/** Pull boss towards player during melee attack */
	void UpdateMeleeAttackPull(float DeltaTime);

	/** Execute phase transition */
	void ExecutePhaseTransition(EBossPhase NewPhase);

	/** Called when phase transition animation/movement completes */
	void OnPhaseTransitionComplete();

	/** Check aerial phase duration timeout */
	void CheckAerialPhaseTimeout();

	/** Evaluate a quadratic bezier curve (for arc dash) */
	FVector EvaluateBezier(const FVector& P0, const FVector& P1, const FVector& P2, float T) const;

	// ==================== Parry Detection Methods ====================

	/** Check tracked projectiles for parry (projectile returning to boss via EMF) */
	void CheckProjectilesForParry();

	/** Check if a specific projectile is being parried (returning to boss) */
	bool IsProjectileReturning(AShooterProjectile* Projectile) const;

	/** Clean up destroyed/invalid projectiles from tracking list */
	void CleanupTrackedProjectiles();

	/** Called when parry check timer fires */
	void OnParryCheckTimer();

	/** Start parry detection timer */
	void StartParryDetection();

	/** Stop parry detection timer */
	void StopParryDetection();
};
