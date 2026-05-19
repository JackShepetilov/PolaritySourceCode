// BossCharacter.h
// Ground-phase melee boss with finisher

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "BossCharacter.generated.h"

class UAnimMontage;
class UNiagaraSystem;
class UCurveFloat;
class AArenaManager;

/** Boss phase enumeration */
UENUM(BlueprintType)
enum class EBossPhase : uint8
{
	Ground		UMETA(DisplayName = "Ground Phase"),
	Finisher	UMETA(DisplayName = "Finisher Phase")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBossPhaseChanged, EBossPhase, OldPhase, EBossPhase, NewPhase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBossFinisherReady);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBossDefeated);

/**
 * Ground-phase melee boss with finisher.
 *
 * The CurrentHP field is conceptually Posture (Sekiro-style): every player damage source drains it,
 * and when it hits 1 the boss enters Finisher phase (invulnerable, waiting for a melee finisher).
 * The actual health pool is the datacenter (arena prop %), tracked by ArenaManager separately.
 *
 * Prop impacts that would normally stun a regular NPC instead apply a temporary slowdown — only a
 * fully drained Posture (Finisher phase) actually stuns the boss.
 *
 * Posture regenerates passively, but at a rate that scales with datacenter health: the more the
 * datacenter is destroyed, the slower the boss recovers.
 *
 * Melee swings have a windup phase; a player who attacks the boss head-on during windup performs
 * a counter that bypasses damage and instead slows the boss.
 */
UCLASS()
class POLARITY_API ABossCharacter : public AShooterNPC
{
	GENERATED_BODY()

public:
	ABossCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	// ==================== Phase ====================

	UPROPERTY(BlueprintReadOnly, Category = "Boss|Phase")
	EBossPhase CurrentPhase = EBossPhase::Ground;

	UPROPERTY(BlueprintReadOnly, Category = "Boss|Phase")
	bool bIsTransitioning = false;

	// ==================== Ground Phase: Dash ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Dash", meta = (ClampMin = "100.0"))
	float MaxDashDistance = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Dash", meta = (ClampMin = "500.0"))
	float DashSpeed = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Dash", meta = (ClampMin = "0.5"))
	float DashCooldown = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Dash", meta = (ClampMin = "30.0", ClampMax = "150.0"))
	float MinDashAngleOffset = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Dash", meta = (ClampMin = "30.0", ClampMax = "180.0"))
	float MaxDashAngleOffset = 135.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Dash", meta = (ClampMin = "50.0"))
	float DashTargetDistanceFromPlayer = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Dash|Animation")
	TObjectPtr<UAnimMontage> ApproachDashMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Dash|Animation")
	TObjectPtr<UAnimMontage> CircleDashMontage;

	// ==================== Ground Phase: Melee ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Melee")
	TArray<TObjectPtr<UAnimMontage>> MeleeAttackMontages;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Melee", meta = (ClampMin = "0"))
	float MeleeAttackDamage = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Melee", meta = (ClampMin = "50.0"))
	float MeleeAttackRange = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Melee", meta = (ClampMin = "0.1"))
	float MeleeAttackCooldown = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Melee", meta = (ClampMin = "0.0"))
	float MeleeAttackPullSpeed = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Melee", meta = (ClampMin = "10.0"))
	float MeleeTraceRadius = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ground Phase|Melee", meta = (ClampMin = "50.0"))
	float MeleeTraceDistance = 150.0f;

	// ==================== Crossfade (Dash <-> Attack) ====================

	/** Blend time when crossfading from a dash montage into an attack montage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Animation Blending", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DashToAttackBlendTime = 0.15f;

	/** Blend time when first starting a dash montage. 0 = snap in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Animation Blending", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DashStartBlendTime = 0.05f;

	/** Blend time when crossfading out of an attack montage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Animation Blending", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AttackEndBlendTime = 0.2f;

	// ==================== Counter Detection ====================

	/** Max distance (cm) from boss within which an attacker is considered a counter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Counter", meta = (ClampMin = "50.0"))
	float CounterDistance = 200.0f;

	/** Minimum dot product of attacker forward × direction-to-boss to count as a head-on counter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Counter", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CounterDotThreshold = 0.7f;

	/** Slowdown duration applied to the boss after a successful counter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Counter", meta = (ClampMin = "0.1"))
	float CounterSlowdownDuration = 1.5f;

	// ==================== Slowdown (replaces prop-impact stun) ====================

	/** MaxWalkSpeed multiplier while slowed (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Slowdown", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SlowdownStrength = 0.4f;

	/** Multiplier applied to the incoming stun duration to derive slowdown duration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Slowdown", meta = (ClampMin = "0.0"))
	float StunToSlowdownTimeScale = 1.0f;

	// ==================== Posture Regen ====================

	/**
	 * Curve mapping datacenter RemainingPercent (X, 0..1, 1 = full) → Posture/HP per second (Y).
	 * Falls back to FallbackPostureRegenBase * Percent² if null.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Posture Regen")
	TObjectPtr<UCurveFloat> PostureRegenByArenaPropCurve;

	/** Base regen (HP/sec) used when no curve is supplied; scaled by datacenter percent². */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Posture Regen", meta = (ClampMin = "0.0"))
	float FallbackPostureRegenBase = 20.0f;

	/** Arena that hosts the boss fight. Posture regen samples its OnPropPercentChanged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Posture Regen")
	TSoftObjectPtr<AArenaManager> LinkedArena;

	// ==================== Finisher Phase ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Finisher Phase|VFX")
	TObjectPtr<UNiagaraSystem> FinisherVulnerabilityVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Finisher Phase|VFX", meta = (ClampMin = "0.1"))
	float FinisherVFXScale = 1.5f;

	/** Animation montage played the moment the boss enters the finisher phase (in place). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Finisher Phase|Animation")
	TObjectPtr<UAnimMontage> FinisherEnterStunMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Finisher Phase|Knockback")
	FVector FinisherKnockbackDirection = FVector(1.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Finisher Phase|Knockback", meta = (ClampMin = "100.0"))
	float FinisherKnockbackDistance = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Finisher Phase|Knockback", meta = (ClampMin = "0.1"))
	float FinisherKnockbackDuration = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Finisher Phase|Knockback")
	TObjectPtr<UAnimMontage> FinisherKnockbackMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Finisher Phase|Death")
	TObjectPtr<UNiagaraSystem> FinisherDeathVFX;

	// ==================== Runtime State ====================

	bool bIsInFinisherPhase = false;
	bool bIsFinisherKnockback = false;

	FVector FinisherKnockbackStartPos = FVector::ZeroVector;
	FVector FinisherKnockbackEndPos = FVector::ZeroVector;
	float FinisherKnockbackElapsed = 0.0f;

	bool bIsDashing = false;
	bool bIsAttacking = false;
	bool bMeleeOnCooldown = false;
	bool bDashOnCooldown = false;
	float LastDashTime = -10.0f;
	float LastMeleeAttackTime = -10.0f;
	float MaxHP = 1000.0f;

	/** True from StartMeleeAttack until the damage window opens — the read-and-react counter window. */
	bool bIsInMeleeWindup = false;

	/** Slowdown state and cached default MaxWalkSpeed for restoration. */
	bool bIsSlowed = false;
	float DefaultMaxWalkSpeed = 0.0f;
	FTimerHandle SlowdownRecoveryTimer;

	/** Last broadcasted datacenter prop percent (1.0 = all alive, 0.0 = all destroyed). */
	float CachedArenaPropPercent = 1.0f;

	/** Currently-playing montage tracked by CrossfadeToMontage; used to blend out on the next call. */
	TWeakObjectPtr<UAnimMontage> ActiveCrossfadeMontage;

	// ==================== Dash State ====================

	bool bIsApproachDash = false;
	FVector DashStartPosition = FVector::ZeroVector;
	float DashStartAngle = 0.0f;
	float DashTargetAngle = 0.0f;
	float DashStartRadius = 0.0f;
	float DashTargetRadius = 0.0f;
	float DashArcDirection = 1.0f;
	float DashElapsedTime = 0.0f;
	float DashTotalDuration = 0.0f;

	TWeakObjectPtr<AActor> CurrentTarget;

	// ==================== Melee State ====================

	TSet<AActor*> HitActorsThisAttack;
	bool bDamageWindowActive = false;

	// ==================== Timers ====================

	FTimerHandle DashCooldownTimer;
	FTimerHandle MeleeCooldownTimer;
	FTimerHandle DamageWindowStartTimer;
	FTimerHandle DamageWindowEndTimer;
	FTimerHandle PhaseTransitionTimer;

public:
	// ==================== Events ====================

	UPROPERTY(BlueprintAssignable, Category = "Boss|Events")
	FOnBossPhaseChanged OnPhaseChanged;

	UPROPERTY(BlueprintAssignable, Category = "Boss|Events")
	FOnBossFinisherReady OnFinisherReady;

	UPROPERTY(BlueprintAssignable, Category = "Boss|Events")
	FOnBossDefeated OnBossDefeated;

protected:
	// ==================== Lifecycle ====================

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ==================== Damage Handling ====================

	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	// ==================== Stun / Slowdown ====================

	/** Boss does not stun on prop impact while Posture is alive; instead, it slows. */
	virtual void ApplyExplosionStun(float Duration, UAnimMontage* StunMontage = nullptr) override;

public:
	// ==================== Phase Control ====================

	UFUNCTION(BlueprintPure, Category = "Boss|Phase")
	EBossPhase GetCurrentPhase() const { return CurrentPhase; }

	UFUNCTION(BlueprintCallable, Category = "Boss|Phase")
	void SetPhase(EBossPhase NewPhase);

	UFUNCTION(BlueprintPure, Category = "Boss|Phase")
	bool IsTransitioning() const { return bIsTransitioning; }

	// ==================== Ground Phase Interface ====================

	UFUNCTION(BlueprintCallable, Category = "Boss|Ground Phase")
	bool StartApproachDash(AActor* Target);

	UFUNCTION(BlueprintCallable, Category = "Boss|Ground Phase")
	bool StartCircleDash(AActor* Target);

	UFUNCTION(BlueprintPure, Category = "Boss|Ground Phase")
	bool CanDash() const;

	UFUNCTION(BlueprintPure, Category = "Boss|Ground Phase")
	bool IsDashing() const { return bIsDashing; }

	UFUNCTION(BlueprintPure, Category = "Boss|Ground Phase")
	bool IsTargetFar(AActor* Target) const;

	UFUNCTION(BlueprintCallable, Category = "Boss|Ground Phase")
	void StartMeleeAttack(AActor* Target);

	UFUNCTION(BlueprintPure, Category = "Boss|Ground Phase")
	bool CanMeleeAttack() const;

	UFUNCTION(BlueprintPure, Category = "Boss|Ground Phase")
	bool IsAttacking() const { return bIsAttacking; }

	UFUNCTION(BlueprintPure, Category = "Boss|Ground Phase")
	bool IsTargetInMeleeRange(AActor* Target) const;

	// ==================== Counter ====================

	/** Returns true if Attacker satisfies all conditions for countering a windup attack. */
	UFUNCTION(BlueprintPure, Category = "Boss|Counter")
	bool IsBeingCountered(AActor* Attacker) const;

	// ==================== Animation Blending ====================

	/**
	 * Stop the currently-tracked montage with BlendOut = CrossfadeTime, then play NewMontage
	 * with BlendIn = CrossfadeTime. Use this when chaining boss montages (dash → attack, etc.)
	 * to get a clean architectural crossfade in code instead of relying on per-asset blends.
	 */
	UFUNCTION(BlueprintCallable, Category = "Boss|Animation")
	void CrossfadeToMontage(UAnimMontage* NewMontage, float CrossfadeTime = 0.15f, float PlayRate = 1.0f);

	// ==================== Finisher Phase Interface ====================

	UFUNCTION(BlueprintCallable, Category = "Boss|Finisher Phase")
	void EnterFinisherPhase();

	UFUNCTION(BlueprintCallable, Category = "Boss|Finisher Phase")
	void ExecuteFinisher(AActor* Attacker);

	UFUNCTION(BlueprintPure, Category = "Boss|Finisher Phase")
	bool IsInFinisherPhase() const { return bIsInFinisherPhase; }

	UFUNCTION(BlueprintPure, Category = "Boss|Finisher Phase")
	bool IsInFinisherKnockback() const { return bIsFinisherKnockback; }

	// ==================== Knockback Override ====================

	virtual void ApplyKnockback(const FVector& InKnockbackDirection, float Distance, float Duration, const FVector& AttackerLocation = FVector::ZeroVector, bool bKeepEMFEnabled = false, EKnockbackStyle Style = EKnockbackStyle::Standard) override;

protected:
	void StartFinisherKnockback();
	void UpdateFinisherKnockback(float DeltaTime);
	void OnFinisherKnockbackComplete();

public:
	// ==================== Target Management ====================

	UFUNCTION(BlueprintCallable, Category = "Boss|Target")
	void SetTarget(AActor* NewTarget);

	UFUNCTION(BlueprintPure, Category = "Boss|Target")
	AActor* GetTarget() const { return CurrentTarget.Get(); }

	/** Arena this boss is bound to (datacenter prop %). Returns nullptr if not loaded.
	 *  Defined in .cpp because TSoftObjectPtr::Get() resolves via dynamic_cast and needs
	 *  the full AArenaManager type — keeping it inline here forces every TU that includes
	 *  BossCharacter.h to also include ArenaManager.h, which is bad include hygiene. */
	UFUNCTION(BlueprintPure, Category = "Boss")
	AArenaManager* GetLinkedArena() const;

protected:
	// ==================== Internal Methods ====================

	FVector CalculateArcDashTarget(AActor* Target) const;
	FVector CalculateArcControlPoint(const FVector& Start, const FVector& End, AActor* Target) const;
	void UpdateArcDash(float DeltaTime);
	void EndDash();
	void OnDashCooldownEnd();
	void OnMeleeCooldownEnd();
	void PerformMeleeTrace();
	void OnDamageWindowStart();
	void OnDamageWindowEnd();

	UFUNCTION()
	void OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	void ApplyMeleeDamage(AActor* HitActor, const FHitResult& HitResult);
	void UpdateMeleeAttackPull(float DeltaTime);
	void ExecutePhaseTransition(EBossPhase NewPhase);
	void OnPhaseTransitionComplete();
	FVector EvaluateBezier(const FVector& P0, const FVector& P1, const FVector& P2, float T) const;

	// ==================== Posture Regen ====================

	/** Posture-recovery tick. Samples PostureRegenByArenaPropCurve at CachedArenaPropPercent. */
	void UpdatePostureRegen(float DeltaTime);

	/** Subscribed to LinkedArena->OnPropPercentChanged. */
	UFUNCTION()
	void OnArenaPropPercentChanged(float RemainingPercent, int32 AliveCount);

	// ==================== Slowdown ====================

	/** Restore MaxWalkSpeed at the end of a slowdown window. */
	void EndSlowdown();
};
