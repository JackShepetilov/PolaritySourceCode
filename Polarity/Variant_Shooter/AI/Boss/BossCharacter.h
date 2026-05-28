// BossCharacter.h
// Ground-phase boss: shoot-and-strafe (ranged) + melee (lunge / in-place), Sekiro-style.
//
// Built on AHumanoidNPC, which already provides: ranged weapon + burst fire (AShooterNPC),
// player weapon-yank, cyclic weapon management, melee montages + attack magnetism +
// AnimNotify-driven damage window (AMeleeNPC), and EMF capture immunity.
//
// This class adds the boss-specific layer on top:
//   - Posture model: CurrentHP is Posture (Sekiro-style). When it would hit 1, the boss enters
//     the invulnerable Finisher phase and waits for a melee finisher. Posture regenerates at a
//     rate that scales with the datacenter (arena prop %), the true health pool (ArenaManager).
//   - Prop impacts apply a temporary slowdown instead of a stun.
//   - Counter: a player hit landing during the boss's melee windup (before the damage notify
//     opens) interrupts + slows the boss; only in that window is the boss knocked back.
//   - Cyclic disarm: when the player yanks the weapon, the boss fights melee-only for a short
//     window, then re-arms and resumes the ranged/melee choice.

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/AI/HumanoidNPC.h"
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

/** Action the boss commits to after a strafe (chosen by ChooseNextAction). */
UENUM(BlueprintType)
enum class EBossAction : uint8
{
	Shoot	UMETA(DisplayName = "Shoot (ranged burst)"),
	Melee	UMETA(DisplayName = "Melee (approach + strike)")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBossPhaseChanged, EBossPhase, OldPhase, EBossPhase, NewPhase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBossFinisherReady);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBossDefeated);

UCLASS()
class POLARITY_API ABossCharacter : public AHumanoidNPC
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

	// ==================== Melee: Lunge vs In-Place ====================
	// Drives the inherited AMeleeNPC machinery (magnetism / damage window / trace). The melee
	// start range, pull-to distance, pull speed, damage, and trace size are the inherited
	// AMeleeNPC properties: AttackRange (start), MagnetismStopDistance (pull-to),
	// MagnetismSpeed (pull speed), AttackDamage, TraceRadius/TraceDistance.

	/** Attack montages used when the target is farther than InPlaceMeleeRange — boss lunges in
	 *  (attack magnetism pulls it toward the player, stopping at MagnetismStopDistance). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Melee")
	TArray<TObjectPtr<UAnimMontage>> LungeMeleeMontages;

	/** Attack montages used when the target is already within InPlaceMeleeRange — boss strikes
	 *  on the spot with NO magnet pull. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Melee")
	TArray<TObjectPtr<UAnimMontage>> InPlaceMeleeMontages;

	/** Distance threshold: target closer than this → in-place attack (no pull); farther → lunge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Melee", meta = (ClampMin = "0.0"))
	float InPlaceMeleeRange = 180.0f;

	// ==================== Ranged: Fire-Montage Fork (crossfaded) ====================

	/** Crossfaded in from locomotion on the FIRST shot of a burst. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ranged|Fire Montage")
	TObjectPtr<UAnimMontage> FirstFireMontage;

	/** Crossfaded on the 2nd+ shots of a burst; on the last shot it crossfades back to locomotion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ranged|Fire Montage")
	TObjectPtr<UAnimMontage> ContinuousFireMontage;

	/** Crossfade time for all fire-montage transitions (in, between, out). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Ranged|Fire Montage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FireMontageBlendTime = 0.12f;

	// ==================== Yank Gate ====================

	/** The boss's weapon can only be yanked once |body charge| reaches this threshold.
	 *  Layered on top of AHumanoidNPC::CanBeYanked() via the override below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Yank", meta = (ClampMin = "0.0"))
	float YankChargeThreshold = 50.0f;

	// ==================== Behavior: Action Choice ====================
	// The boss cycle is: strafe → ChooseNextAction (weighted) → Shoot or Melee → back to strafe.
	// When disarmed (weapon just yanked) the choice is forced to Melee.

	/** Relative weight of choosing a ranged burst (vs melee) when armed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Behavior", meta = (ClampMin = "0.0"))
	float ShootActionWeight = 1.0f;

	/** Relative weight of choosing a melee approach (vs shoot) when armed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Behavior", meta = (ClampMin = "0.0"))
	float MeleeActionWeight = 1.0f;

	// ==================== Behavior: Strafe (Sekiro/DS orbit) ====================

	/** Preferred distance the boss keeps from the player while strafing (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Behavior|Strafe", meta = (ClampMin = "0.0"))
	float StrafeRadius = 700.0f;

	/** How long a normal strafe lasts before the boss picks an action (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Behavior|Strafe", meta = (ClampMin = "0.0"))
	float StrafeDuration = 2.5f;

	/** Shorter strafe time used while disarmed (boss is more aggressive without its gun). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Behavior|Strafe", meta = (ClampMin = "0.0"))
	float StrafeDurationDisarmed = 1.0f;

	/** How often the strafe issues a new orbit MoveTo (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Behavior|Strafe", meta = (ClampMin = "0.05"))
	float StrafeRepathInterval = 0.5f;

	/** Angle stepped around the player per repath (deg) — sets the orbit speed/curve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Behavior|Strafe", meta = (ClampMin = "1.0", ClampMax = "120.0"))
	float StrafeStepAngleDeg = 35.0f;

	/** MoveTo acceptance radius for orbit points (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Behavior|Strafe", meta = (ClampMin = "1.0"))
	float StrafeAcceptanceRadius = 80.0f;

	// ==================== Animation Blending (melee) ====================

	/** Blend time when crossfading from locomotion into an attack montage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Animation Blending", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MeleeStartBlendTime = 0.1f;

	/** Blend time when crossfading out of an attack montage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boss|Animation Blending", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AttackEndBlendTime = 0.2f;

	// ==================== Counter ====================

	/** Slowdown duration applied to the boss after a successful counter (player hit during windup). */
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

	/** Curve mapping datacenter RemainingPercent (X, 0..1, 1 = full) → Posture/HP per second (Y).
	 *  Falls back to FallbackPostureRegenBase * Percent² if null. */
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

	/** Cached max Posture (== starting CurrentHP). Posture threshold math uses this. */
	float MaxHP = 1000.0f;

	/** Slowdown state and cached default MaxWalkSpeed for restoration. */
	bool bIsSlowed = false;
	float DefaultMaxWalkSpeed = 0.0f;
	FTimerHandle SlowdownRecoveryTimer;

	/** World time of the last registered windup counter. Lets ApplyKnockback allow the counter
	 *  knockback regardless of whether the player's TakeDamage or ApplyKnockback fires first. */
	float LastCounterRegisteredTime = -10.0f;

	/** Action chosen by the last ChooseNextAction(); the StateTree branches on it. */
	EBossAction PendingAction = EBossAction::Shoot;

	/** Last broadcast datacenter prop percent, remapped to 1.0 = full, 0.0 = destroyed-at-threshold. */
	float CachedArenaPropPercent = 1.0f;

	/** Currently-playing montage tracked by CrossfadeToMontage; used to blend out on the next call. */
	TWeakObjectPtr<UAnimMontage> ActiveCrossfadeMontage;

	/** Current combat target (set by the boss StateTree). */
	TWeakObjectPtr<AActor> CurrentTarget;

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

	// ==================== Damage / Stun / Knockback ====================

	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Boss does not stun on prop impact while Posture is alive; instead, it slows. */
	virtual void ApplyExplosionStun(float Duration, UAnimMontage* StunMontage = nullptr) override;

	/** Boss knockback rule: suppressed in general (HumanoidNPC body immunity), allowed ONLY during
	 *  the boss's own melee windup (the counter window) and never in the finisher phase. */
	virtual void ApplyKnockback(const FVector& InKnockbackDirection, float Distance, float Duration, const FVector& AttackerLocation = FVector::ZeroVector, bool bKeepEMFEnabled = false, EKnockbackStyle Style = EKnockbackStyle::Standard) override;

public:
	// ==================== Phase Control ====================

	UFUNCTION(BlueprintPure, Category = "Boss|Phase")
	EBossPhase GetCurrentPhase() const { return CurrentPhase; }

	UFUNCTION(BlueprintCallable, Category = "Boss|Phase")
	void SetPhase(EBossPhase NewPhase);

	UFUNCTION(BlueprintPure, Category = "Boss|Phase")
	bool IsTransitioning() const { return bIsTransitioning; }

	// ==================== Melee Interface ====================

	/** Boss melee entry point (called by the boss StateTree melee task). Picks a lunge or in-place
	 *  montage based on distance to Target, toggles attack magnetism accordingly, and drives the
	 *  inherited AMeleeNPC damage-window / trace / magnet machinery. The damage window is
	 *  AnimNotify-driven (bUseTimerDamageWindow = false) — place the "Melee: Damage Window State"
	 *  notify in the attack montages. */
	UFUNCTION(BlueprintCallable, Category = "Boss|Melee")
	void StartBossMeleeAttack(AActor* Target);

	/** True during the read-and-react counter window: the boss's own attack has started but the
	 *  damage window has not opened yet (derived from inherited AMeleeNPC flags). */
	UFUNCTION(BlueprintPure, Category = "Boss|Melee")
	bool IsInMeleeWindup() const;

	// ==================== Ranged / Disarm ====================

	/** True when the boss currently holds no ranged weapon (just yanked, waiting to re-arm). */
	UFUNCTION(BlueprintPure, Category = "Boss|Ranged")
	bool IsDisarmed() const;

	/** Start a single ranged burst at Target (StateTree shoot task). Bypasses the squad
	 *  coordinator (solo boss). The burst ends via the inherited burst-cooldown logic. */
	UFUNCTION(BlueprintCallable, Category = "Boss|Ranged")
	void StartShootBurst(AActor* Target);

	// ==================== Behavior (StateTree-driven) ====================

	/** Pick the next action with weights; forced to Melee while disarmed. Stores PendingAction. */
	UFUNCTION(BlueprintCallable, Category = "Boss|Behavior")
	void ChooseNextAction();

	UFUNCTION(BlueprintPure, Category = "Boss|Behavior")
	EBossAction GetPendingAction() const { return PendingAction; }

	/** Strafe duration for the current state (shorter while disarmed). */
	UFUNCTION(BlueprintPure, Category = "Boss|Behavior")
	float GetStrafeDurationForState() const;

	/** How often the strafe task should issue a new orbit MoveTo (s). */
	UFUNCTION(BlueprintPure, Category = "Boss|Behavior")
	float GetStrafeRepathInterval() const { return StrafeRepathInterval; }

	/** Begin a strafe: focus the controller on Target so the boss faces it while orbiting. */
	UFUNCTION(BlueprintCallable, Category = "Boss|Behavior")
	void BeginStrafe(AActor* Target);

	/** Issue one orbit MoveTo around Target. Direction +1 = CCW, -1 = CW. */
	UFUNCTION(BlueprintCallable, Category = "Boss|Behavior")
	void StrafeStep(AActor* Target, float Direction);

	/** Stop the strafe movement. */
	UFUNCTION(BlueprintCallable, Category = "Boss|Behavior")
	void StopStrafe();

	// ==================== Counter ====================

	/** Returns true if a hit from Attacker right now should count as a windup counter. */
	UFUNCTION(BlueprintPure, Category = "Boss|Counter")
	bool IsBeingCountered(AActor* Attacker) const;

	// ==================== Animation Blending ====================

	/**
	 * Stop the currently-tracked montage with BlendOut = CrossfadeTime, then play NewMontage with
	 * BlendIn = CrossfadeTime. Use this when chaining boss montages (melee, fire fork) to get a
	 * clean crossfade in code. Passing nullptr just blends the tracked montage out to locomotion.
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

	// ==================== Yank Override ====================

	/** Adds the YankChargeThreshold gate on top of AHumanoidNPC::CanBeYanked(). */
	virtual bool CanBeYanked() const override;

	// ==================== Target Management ====================

	UFUNCTION(BlueprintCallable, Category = "Boss|Target")
	void SetTarget(AActor* NewTarget);

	UFUNCTION(BlueprintPure, Category = "Boss|Target")
	AActor* GetTarget() const { return CurrentTarget.Get(); }

	/** Arena this boss is bound to (datacenter prop %). Returns nullptr if not loaded.
	 *  Defined in .cpp because TSoftObjectPtr::Get() resolves via the full AArenaManager type. */
	UFUNCTION(BlueprintPure, Category = "Boss")
	AArenaManager* GetLinkedArena() const;

protected:
	// ==================== Cyclic Re-arm ====================

	/** Override AHumanoidNPC: instead of exhausting the inventory into permanent melee mode, the
	 *  boss re-spawns its (single) weapon and resumes the ranged/melee choice. The disarmed window
	 *  is the WeaponSwitchDelay between the yank and this call. */
	virtual void SpawnNextWeapon() override;

	// ==================== Internal: Melee ====================

	UFUNCTION()
	void OnBossAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// ==================== Internal: Fire-Montage Fork ====================

	/** Bound to the INITIAL weapon's OnShotFired (burst counting handled by the inherited
	 *  AHumanoidNPC binding). Drives the crossfaded fire montage only. */
	UFUNCTION()
	void OnBossWeaponShotFired();

	/** Bound to a RE-ARMED weapon's OnShotFired. Drives burst counting (the inherited binding is
	 *  gone on a fresh weapon) AND the fire montage. */
	UFUNCTION()
	void OnBossWeaponShotFiredReArmed();

	/** Shared fire-montage logic: first shot → FirstFireMontage, 2nd+ → ContinuousFireMontage,
	 *  last shot of the burst → blend back to locomotion. All via CrossfadeToMontage. */
	void DriveFireMontageForShot();

	// ==================== Internal: Phase ====================

	void ExecutePhaseTransition(EBossPhase NewPhase);
	void OnPhaseTransitionComplete();

	// ==================== Internal: Finisher ====================

	void StartFinisherKnockback();
	void UpdateFinisherKnockback(float DeltaTime);
	void OnFinisherKnockbackComplete();

	// ==================== Internal: Posture Regen ====================

	/** Posture-recovery tick. Samples PostureRegenByArenaPropCurve at CachedArenaPropPercent. */
	void UpdatePostureRegen(float DeltaTime);

	/** Subscribed to LinkedArena->OnPropPercentChanged. */
	UFUNCTION()
	void OnArenaPropPercentChanged(float RemainingPercent, int32 AliveCount);

	// ==================== Internal: Slowdown ====================

	/** Restore MaxWalkSpeed at the end of a slowdown window. */
	void EndSlowdown();
};
