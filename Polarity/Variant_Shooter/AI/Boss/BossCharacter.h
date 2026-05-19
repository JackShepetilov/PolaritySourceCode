// BossCharacter.h
// Ground-phase melee boss with finisher

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "BossCharacter.generated.h"

class UAnimMontage;
class UNiagaraSystem;

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
 * The CurrentHP field is conceptually Posture (Sekiro-style): all player damage drains it,
 * and when it hits 1 the boss enters Finisher phase (invulnerable, waiting for a melee finisher).
 * The actual health pool is the datacenter (arena prop %), tracked by ArenaManager separately.
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

	// ==================== Finisher Phase ====================
	// Teleport/hover fields kept here only because they're still referenced in EnterFinisherPhase()
	// implementation. They will be removed alongside the teleport logic in the next task.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Finisher Phase", meta = (ClampMin = "200.0"))
	float FinisherHoverHeight = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Finisher Phase")
	FVector FinisherHoverOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Finisher Phase|VFX")
	TObjectPtr<UNiagaraSystem> FinisherVulnerabilityVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Finisher Phase|VFX", meta = (ClampMin = "0.1"))
	float FinisherVFXScale = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Finisher Phase|Teleport")
	FVector FinisherTeleportPosition = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Finisher Phase|Teleport")
	TObjectPtr<UNiagaraSystem> TeleportDisappearVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Finisher Phase|Teleport")
	TObjectPtr<UNiagaraSystem> TeleportAppearVFX;

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
	void TeleportToFinisherPosition();
	void StartFinisherKnockback();
	void UpdateFinisherKnockback(float DeltaTime);
	void OnFinisherKnockbackComplete();

public:
	// ==================== Target Management ====================

	UFUNCTION(BlueprintCallable, Category = "Boss|Target")
	void SetTarget(AActor* NewTarget);

	UFUNCTION(BlueprintPure, Category = "Boss|Target")
	AActor* GetTarget() const { return CurrentTarget.Get(); }

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
};
