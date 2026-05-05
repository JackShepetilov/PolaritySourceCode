// RiotShield.h
// Standalone shield actor — not a weapon, attached to player camera.
// Lifecycle: pickup spawns shield on character → Raised/Lowered toggle → broken (HP=0) or thrown (returns to a pickup).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RiotShield.generated.h"

class UStaticMeshComponent;
class UBoxComponent;
class UCurveVector;
class USoundBase;
class UDamageType;
class UNiagaraSystem;
class UGeometryCollection;
class AShooterCharacter;
class ARiotShieldPickup;

UENUM(BlueprintType)
enum class ERiotShieldState : uint8
{
	Lowered,
	Raised,
	Transitioning,
	Bashing
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnShieldHealthChanged, float, CurrentHealth, float, MaxHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnShieldStateChanged, ERiotShieldState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnShieldBroken);

UCLASS()
class POLARITY_API ARiotShield : public AActor
{
	GENERATED_BODY()

public:
	ARiotShield();

	/** Equip on character — attach to camera, set state to Raised, apply walk-speed/melee/ADS gating. */
	void EquipToCharacter(AShooterCharacter* Character);

	/** Toggle Raised → Lowered (or vice versa). No-op while bashing. */
	void Toggle();

	/** Force raise the shield (called on equip). */
	void Raise();

	/** Force lower the shield. */
	void Lower();

	/** Begin a bash attack. Plays programmatic forward thrust over BashDuration. */
	void StartBash();

	/** Throw the shield: spawn pickup with impulses, destroy this actor. */
	void ThrowAway();

	/** Internal: HP reached 0 — spawn break GC, destroy. */
	void BreakShield();

	UFUNCTION(BlueprintPure, Category = "Shield")
	ERiotShieldState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Shield")
	bool IsRaised() const { return State == ERiotShieldState::Raised || State == ERiotShieldState::Bashing; }

	UFUNCTION(BlueprintPure, Category = "Shield")
	bool IsLowered() const { return State == ERiotShieldState::Lowered; }

	UFUNCTION(BlueprintPure, Category = "Shield")
	bool IsBashing() const { return State == ERiotShieldState::Bashing; }

	UFUNCTION(BlueprintPure, Category = "Shield")
	float GetCurrentHealth() const { return CurrentHealth; }

	UFUNCTION(BlueprintPure, Category = "Shield")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Shield")
	AShooterCharacter* GetOwningCharacter() const { return OwnerCharacter; }

	/** Override TakeDamage so projectiles/hitscan that hit ShieldMesh consume HP instead of hurting the player. */
	virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Health change broadcast (for HUD). */
	UPROPERTY(BlueprintAssignable, Category = "Shield")
	FOnShieldHealthChanged OnHealthChanged;

	/** State change broadcast. */
	UPROPERTY(BlueprintAssignable, Category = "Shield")
	FOnShieldStateChanged OnStateChanged;

	/** Fired right before the actor is destroyed by break (HP=0). */
	UPROPERTY(BlueprintAssignable, Category = "Shield")
	FOnShieldBroken OnBroken;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// ==================== Components ====================

	/** Visible shield model. Block channels are configured at runtime based on Raised/Lowered. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> ShieldMesh;

	// ==================== Health / Durability ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Health", meta = (ClampMin = "1.0"))
	float MaxHealth = 200.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Shield|Health")
	float CurrentHealth = 200.0f;

	/** Damage type classes that consume shield HP. Damage to the shield is always blocked from the player.
	 *  Types not in this list still get blocked (no damage forwarded) but don't drain HP. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Health")
	TArray<TSubclassOf<UDamageType>> HealthDrainingDamageTypes;

	// ==================== Movement Penalty ====================

	/** Multiplier applied to MaxWalkSpeed while shield is Raised. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Movement", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float SpeedMultiplierWhenRaised = 0.6f;

	// ==================== Mesh Transforms ====================

	/** Mesh transform relative to the shield actor (which is attached to camera) when shield is Raised. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Pose|Raised")
	FVector RaisedRelativeLocation = FVector(60.0f, 0.0f, -10.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Pose|Raised")
	FRotator RaisedRelativeRotation = FRotator::ZeroRotator;

	/** Mesh transform when shield is Lowered (hip — out of view, no blocking). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Pose|Lowered")
	FVector LoweredRelativeLocation = FVector(50.0f, 30.0f, -60.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Pose|Lowered")
	FRotator LoweredRelativeRotation = FRotator(-30.0f, 0.0f, 0.0f);

	/** Time to interpolate between Raised and Lowered poses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Pose", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float StateTransitionTime = 0.18f;

	// ==================== Sway ====================

	/** Multiplier for the character's procedural run-sway applied to the shield mesh.
	 *  0 = no sway, 1 = full match with the FP weapon mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Sway", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float RunSwayInfluence = 1.0f;

	/** Multiplier for WeaponRecoilComponent's mouse/recoil sway applied to the shield mesh.
	 *  Reads the live recoil settings of the currently equipped weapon (if any). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Sway", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float RecoilSwayInfluence = 1.0f;

	// ==================== Camera Offset ====================

	/** Local offset (in actor/camera-relative space) applied to the player's camera while the shield is Raised.
	 *  Use this to shift the camera so the weapon visually moves to the side and the shield's transparent
	 *  area centers in the screen. Designer fills this from BP. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Camera")
	FVector CameraOffsetWhenRaised = FVector::ZeroVector;

public:
	UFUNCTION(BlueprintPure, Category = "Shield|Camera")
	FVector GetCameraOffsetWhenRaised() const { return CameraOffsetWhenRaised; }

protected:

	// ==================== Bash ====================

	/** Forward thrust distance used by the FALLBACK animation when BashLocationCurve is null (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Bash", meta = (ClampMin = "0.0", ClampMax = "300.0"))
	float BashDistance = 80.0f;

	/** Total duration of bash motion (out + back). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Bash", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float BashDuration = 0.3f;

	/** Cooldown between bashes. Bash button presses during cooldown are ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Bash", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float BashCooldown = 0.4f;

	/** Optional vector curve mapping normalized bash time [0..1] → local LOCATION offset (cm) added
	 *  to RaisedRelativeLocation. X = forward, Y = right, Z = up (relative to the raised pose).
	 *  If null, falls back to sin(πt) × BashDistance along forward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Bash")
	TObjectPtr<UCurveVector> BashLocationCurve;

	/** Optional vector curve mapping normalized bash time [0..1] → local ROTATION offset (degrees)
	 *  added to RaisedRelativeRotation. X = Roll, Y = Pitch, Z = Yaw. Null = no rotation animation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Bash")
	TObjectPtr<UCurveVector> BashRotationCurve;

	/** Damage dealt to enemies hit during the bash window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Bash", meta = (ClampMin = "0.0"))
	float BashDamage = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Bash")
	TSubclassOf<UDamageType> BashDamageType;

	/** Forward range of the bash sphere trace. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Bash", meta = (ClampMin = "50.0", ClampMax = "400.0"))
	float BashRange = 150.0f;

	/** Sphere trace radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Bash", meta = (ClampMin = "10.0", ClampMax = "100.0"))
	float BashRadius = 50.0f;

	/** Damage window opens at this normalized bash time (alpha 0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Bash", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BashDamageWindowOpen = 0.25f;

	/** Damage window closes at this normalized bash time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Bash", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BashDamageWindowClose = 0.6f;

	/** Linear impulse applied to hit physics characters during bash (along camera forward). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Bash", meta = (ClampMin = "0.0"))
	float BashImpulse = 800.0f;

	// ==================== Throw ====================

	/** Pickup class to spawn when the shield is thrown (and used by the world to spawn shields). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Throw")
	TSubclassOf<ARiotShieldPickup> PickupClass;

	/** Local-to-camera offset for spawning the thrown pickup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Throw")
	FVector ThrowSpawnOffset = FVector(80.0f, 0.0f, -10.0f);

	/** Local-to-camera linear impulse applied to the thrown pickup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Throw")
	FVector ThrowLinearImpulse = FVector(1500.0f, 0.0f, 200.0f);

	/** Angular impulse in degrees applied to the thrown pickup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Throw")
	FVector ThrowAngularImpulse = FVector(0.0f, 600.0f, 0.0f);

	// ==================== Break (Geometry Collection) ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Break")
	TObjectPtr<UGeometryCollection> BreakGeometryCollection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Break", meta = (ClampMin = "0.0"))
	float BreakImpulse = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Break", meta = (ClampMin = "0.0"))
	float BreakAngularImpulse = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Break", meta = (ClampMin = "0.5", ClampMax = "30.0"))
	float BreakGibLifetime = 3.0f;

	// ==================== SFX/VFX ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|SFX")
	TObjectPtr<USoundBase> RaiseSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|SFX")
	TObjectPtr<USoundBase> LowerSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|SFX")
	TObjectPtr<USoundBase> BashSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|SFX")
	TObjectPtr<USoundBase> BashHitSound;

	/** Sound played when the shield absorbs an incoming hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|SFX")
	TObjectPtr<USoundBase> AbsorbHitSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|SFX")
	TObjectPtr<USoundBase> BreakSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|VFX")
	TObjectPtr<UNiagaraSystem> AbsorbHitVFX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|VFX")
	TObjectPtr<UNiagaraSystem> BashImpactVFX;

private:

	// ==================== State ====================

	UPROPERTY()
	ERiotShieldState State = ERiotShieldState::Lowered;

	/** Pre-transition state (used to know what we were before Transitioning). */
	UPROPERTY()
	ERiotShieldState PostTransitionState = ERiotShieldState::Raised;

	/** Mesh pose at the start of the current pose-transition. */
	FVector TransitionStartLocation = FVector::ZeroVector;
	FRotator TransitionStartRotation = FRotator::ZeroRotator;

	/** Elapsed time within the current pose-transition. */
	float TransitionElapsed = 0.0f;

	/** Elapsed time within the current bash. */
	float BashElapsed = 0.0f;

	/** Cooldown timer for bash. */
	float BashCooldownRemaining = 0.0f;

	/** Actors already damaged during the current bash (prevents multi-hit). */
	UPROPERTY()
	TSet<TObjectPtr<AActor>> BashHitActorsThisSwing;

	/** Cached owning character. */
	UPROPERTY()
	TObjectPtr<AShooterCharacter> OwnerCharacter;

	/** Original walk speed before applying SpeedMultiplierWhenRaised. */
	float CachedMaxWalkSpeed = 0.0f;

	bool bWalkSpeedModified = false;

	/** Set once BreakShield/ThrowAway runs — guards TakeDamage from re-entering during the
	 *  same frame and prevents stale-pointer crashes in upstream hitscan callers. */
	bool bPendingDestroy = false;

	/** Common cleanup used by both BreakShield and ThrowAway: hide mesh, disable collision,
	 *  restore walk speed, mark pending-destroy, schedule deferred destruction. */
	void TearDownAndScheduleDestroy();

	/** Apply collision profile to mesh based on current Raised/Lowered/Transitioning state. */
	void UpdateMeshCollision();

	/** Apply pose interpolation each tick. */
	void TickPose(float DeltaTime);

	/** Drive bash forward thrust + sphere trace damage window. */
	void TickBash(float DeltaTime);

	/** Read run-sway and recoil-sway from the owning character and add them to the current mesh pose. */
	void ApplyExternalSway();

	/** Sphere trace + apply damage to actors in the bash damage window. */
	void PerformBashTrace();

	/** Apply walk-speed reduction; cache base speed. */
	void ApplyWalkSpeedPenalty();

	/** Restore cached walk-speed. */
	void RestoreWalkSpeedPenalty();

	/** Spawn the GC shatter at the shield mesh location. */
	void SpawnBreakDestructionGC();
};
