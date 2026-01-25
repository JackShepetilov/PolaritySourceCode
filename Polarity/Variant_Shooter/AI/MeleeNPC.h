// MeleeNPC.h
// Melee combat NPC that attacks players at close range

#pragma once

#include "CoreMinimal.h"
#include "ShooterNPC.h"
#include "MeleeNPC.generated.h"

/**
 * A melee-focused NPC that inherits from ShooterNPC but fights in close combat.
 * Uses sphere trace in front of the character to detect hits.
 * Supports optional melee weapon actor attachment.
 */
UCLASS()
class POLARITY_API AMeleeNPC : public AShooterNPC
{
	GENERATED_BODY()

public:

	AMeleeNPC(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:

	// ==================== Attack Animation ====================

	/** Array of attack animation montages (randomly selected) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Animation")
	TArray<TObjectPtr<UAnimMontage>> AttackMontages;

	// ==================== Attack Parameters ====================

	/** Damage dealt per melee hit */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Damage", meta = (ClampMin = "0"))
	float AttackDamage = 25.0f;

	/** Range at which NPC will start attack (distance to target) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Range", meta = (ClampMin = "0"))
	float AttackRange = 150.0f;

	/** Cooldown between attacks in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Timing", meta = (ClampMin = "0.1"))
	float AttackCooldown = 1.0f;

	// ==================== Damage Window (Timer-based) ====================

	/** Time after attack start when damage window begins (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Damage Window", meta = (ClampMin = "0"))
	float DamageWindowStartTime = 0.2f;

	/** Duration of the damage window (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Damage Window", meta = (ClampMin = "0.01"))
	float DamageWindowDuration = 0.3f;

	/** If true, use timer-based damage window. If false, rely on AnimNotify only */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Damage Window")
	bool bUseTimerDamageWindow = true;

	// ==================== Trace Parameters ====================

	/** Radius of the damage sphere trace */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Trace", meta = (ClampMin = "1"))
	float TraceRadius = 40.0f;

	/** Distance in front of character to perform trace */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Trace", meta = (ClampMin = "1"))
	float TraceDistance = 120.0f;

	/** Height offset from character origin for trace start */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Trace")
	float TraceHeightOffset = 50.0f;

	// ==================== Optional Melee Weapon ====================

	/** Optional melee weapon actor class to spawn and attach */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Weapon")
	TSubclassOf<AActor> MeleeWeaponClass;

	/** Socket name to attach melee weapon to */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Weapon")
	FName MeleeWeaponSocket = FName("hand_r");

	/** Spawned melee weapon actor (if MeleeWeaponClass is set) */
	UPROPERTY(BlueprintReadOnly, Category = "Melee|Weapon")
	TObjectPtr<AActor> MeleeWeaponActor;

	// ==================== Dash Parameters ====================

	/** Длительность рывка в секундах */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Dash", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float DashDuration = 0.3f;

	/** Кулдаун между рывками в секундах */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Dash", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float DashCooldown = 2.0f;

	/** Опциональная анимация рывка (может быть null - рывок работает без анимации) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Dash")
	TObjectPtr<UAnimMontage> DashMontage = nullptr;

	/** Запас расстояния от AttackRange для dash-to-attack (остановится на AttackRange - Buffer) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Dash", meta = (ClampMin = "0", ClampMax = "100"))
	float DashAttackRangeBuffer = 50.0f;

	/** Множитель импульса knockback если NPC получает удар во время dash (парирование) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Dash", meta = (ClampMin = "1.0", ClampMax = "5.0"))
	float DashKnockbackMultiplier = 2.5f;

	/** Если true, dash следит за движением цели. Если false, dash идёт к статичной точке */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Dash")
	bool bDashTracksTarget = true;

	// ==================== Debug ====================

	/** If true, draw debug spheres for melee traces */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Debug")
	bool bDebugMeleeTraces = false;

	/** Duration to show debug traces (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Debug", meta = (ClampMin = "0.1", EditCondition = "bDebugMeleeTraces"))
	float DebugTraceDuration = 0.5f;

	// ==================== Runtime State ====================

	/** True while attack animation is playing */
	bool bIsAttacking = false;

	/** True while damage window is active (can deal damage) */
	bool bDamageWindowActive = false;

	/** True if attack is on cooldown */
	bool bAttackOnCooldown = false;

	/** Last time an attack was performed */
	float LastAttackTime = -1.0f;

	/** Actors already hit during current attack (prevents multi-hit) */
	TSet<AActor*> HitActorsThisAttack;

	/** Current attack target */
	TWeakObjectPtr<AActor> CurrentMeleeTarget;

	// ==================== Dash Runtime State ====================

	/** True когда NPC выполняет рывок */
	bool bIsDashing = false;

	/** Время последнего рывка (для кулдауна) */
	float LastDashTime = -1.0f;

	/** Начальная позиция рывка */
	FVector DashStartPosition = FVector::ZeroVector;

	/** Целевая позиция рывка */
	FVector DashTargetPosition = FVector::ZeroVector;

	/** Направление рывка (нормализованное) */
	FVector DashDirection = FVector::ZeroVector;

	/** Прошедшее время рывка */
	float DashElapsedTime = 0.0f;

	/** Общая длительность текущего рывка */
	float DashTotalDuration = 0.0f;

	/** Цель, за которой следит dash (если bDashTracksTarget = true) */
	TWeakObjectPtr<AActor> DashTargetActor;

	// ==================== Timers ====================

	/** Timer for damage window start */
	FTimerHandle DamageWindowStartTimer;

	/** Timer for damage window end */
	FTimerHandle DamageWindowEndTimer;

	/** Timer for attack cooldown */
	FTimerHandle AttackCooldownTimer;

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Per-frame updates */
	virtual void Tick(float DeltaTime) override;

	/** Cleanup on end play */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void ApplyKnockback(const FVector& InKnockbackDirection, float Distance, float Duration, const FVector& AttackerLocation = FVector::ZeroVector, bool bKeepEMFEnabled = false) override;

public:

	// ==================== Attack Interface ====================

	/** Start a melee attack against the target */
	UFUNCTION(BlueprintCallable, Category = "Melee")
	void StartMeleeAttack(AActor* Target);

	/** Returns true if NPC can currently attack (not attacking, not on cooldown) */
	UFUNCTION(BlueprintPure, Category = "Melee")
	bool CanAttack() const;

	/** Returns true if target is within attack range */
	UFUNCTION(BlueprintPure, Category = "Melee")
	bool IsTargetInAttackRange(AActor* Target) const;

	/** Returns the attack range */
	UFUNCTION(BlueprintPure, Category = "Melee")
	float GetAttackRange() const { return AttackRange; }

	/** Returns true if currently performing an attack */
	UFUNCTION(BlueprintPure, Category = "Melee")
	bool IsAttacking() const { return bIsAttacking; }

	// ==================== Dash Interface ====================

	/** Начать рывок в указанном направлении на указанную дистанцию
	 *  @param Direction Направление рывка (будет нормализовано)
	 *  @param Distance Дистанция рывка в см
	 *  @param TargetActor Опциональная цель для tracking (если bDashTracksTarget = true)
	 *  @return true если рывок успешно начат, false если невозможен
	 */
	UFUNCTION(BlueprintCallable, Category = "Melee|Dash")
	bool StartDash(const FVector& Direction, float Distance, AActor* TargetActor = nullptr);

	/** Returns true если NPC сейчас выполняет рывок */
	UFUNCTION(BlueprintPure, Category = "Melee|Dash")
	bool IsDashing() const { return bIsDashing; }

	/** Returns true если NPC может начать рывок (не в кулдауне, не мёртв, не в knockback, не в dash) */
	UFUNCTION(BlueprintPure, Category = "Melee|Dash")
	bool CanDash() const;

	// ==================== AnimNotify Support ====================

	/** Call from AnimNotify to start damage window (when not using timer) */
	UFUNCTION(BlueprintCallable, Category = "Melee|AnimNotify")
	void NotifyDamageWindowStart();

	/** Call from AnimNotify to end damage window (when not using timer) */
	UFUNCTION(BlueprintCallable, Category = "Melee|AnimNotify")
	void NotifyDamageWindowEnd();

protected:

	// ==================== Internal Attack Logic ====================

	/** Called when damage window should start */
	void OnDamageWindowStart();

	/** Called when damage window should end */
	void OnDamageWindowEnd();

	/** Perform sphere trace and apply damage to hit actors */
	void PerformMeleeTrace();

	/** Called when attack animation ends (via montage blend out) */
	UFUNCTION()
	void OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	/** Called when attack cooldown ends */
	void OnAttackCooldownEnd();

	/** Apply damage to a hit actor */
	void ApplyMeleeDamage(AActor* HitActor, const FHitResult& HitResult);

	/** Spawn and attach melee weapon if class is set */
	void SpawnMeleeWeapon();

	// ==================== Internal Dash Logic ====================

	/** Обновление интерполяции позиции во время рывка (вызывается из Tick) */
	void UpdateDashInterpolation(float DeltaTime);

	/** Завершить рывок и восстановить состояние */
	void EndDash();

	/** Валидация пути рывка - проверка NavMesh и коллизий
	 *  @param StartPos Начальная позиция
	 *  @param EndPos Конечная позиция
	 *  @return true если путь валиден
	 */
	bool ValidateDashPath(const FVector& StartPos, const FVector& EndPos) const;
};
