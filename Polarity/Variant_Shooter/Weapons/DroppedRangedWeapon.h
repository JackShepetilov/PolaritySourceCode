// DroppedRangedWeapon.h
// World actor for a ranged weapon dropped by an NPC on death.
// Player captures it via EMF channeling (scripted pull to camera-relative point),
// then it equips as a permanent ShooterWeapon.
// Works identically to DroppedMeleeWeapon but grants regular weapons.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DroppedRangedWeapon.generated.h"

class UStaticMeshComponent;
class UEMF_FieldComponent;
class AShooterWeapon;
class AShooterCharacter;
class USoundBase;
class UAnimMontage;
class UPrimitiveComponent;
class UCurveFloat;
struct FHitResult;

UCLASS(Blueprintable)
class POLARITY_API ADroppedRangedWeapon : public AActor
{
	GENERATED_BODY()

public:
	ADroppedRangedWeapon();

	// ==================== Components ====================

	/** Visible weapon mesh — root, physics-simulated */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;

	/** EMF field component (charge storage for capture detection) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EMF")
	TObjectPtr<UEMF_FieldComponent> FieldComponent;

	// ==================== Weapon Data ====================

	/** Weapon class to grant when player captures this */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TSubclassOf<AShooterWeapon> WeaponClass;

	// ==================== Ammo (yanked-source weapons only) ====================

	/** Distribution curve sampled at yank-spawn time (RollSpawnedBulletCount) to determine
	 *  starting bullet count via inverse-transform sampling: X = random [0..1], Y = bullet
	 *  fraction [0..1] of MagazineSize. If null, falls back to uniform random [1, MagazineSize].
	 *  Only consulted when HumanoidNPC explicitly calls RollSpawnedBulletCount() — death-drop
	 *  path leaves SpawnedBulletCount = -1 and the weapon is granted at full mag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammo")
	TObjectPtr<UCurveFloat> AmmoDistributionCurve;

	/** Bullet count this drop will grant on pickup. -1 = unrolled (default — full magazine on
	 *  pickup, equivalent to current death-drop behavior). Set to a positive value by
	 *  RollSpawnedBulletCount() — guaranteed >= 1 when set. */
	UPROPERTY(BlueprintReadOnly, Category = "Ammo")
	int32 SpawnedBulletCount = -1;

	/** When true, this drop auto-rolls SpawnedBulletCount in BeginPlay so the granted weapon
	 *  behaves like a yanked one: limited bullet pool on pickup, auto-discarded when empty.
	 *  Lets death-drop blueprints opt into the yank-style single-use behavior without code
	 *  changes on the spawn site. Ignored if RollSpawnedBulletCount() already ran (yank path). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammo")
	bool bForceLimitedAmmo = false;

	// ==================== Capture Settings ====================

	/** Can be captured by channeling? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture")
	bool bCanBeCaptured = true;

	/** Base capture range (cm). Actual range = BaseRange * max(1, 1 + ln(|q_player * q_weapon| / NormCoeff)) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = "50.0", Units = "cm"))
	float CaptureBaseRange = 500.0f;

	/** Charge normalization coefficient for capture range formula */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Capture", meta = (ClampMin = "1.0", ClampMax = "1000.0"))
	float CaptureChargeNormCoeff = 50.0f;

	// ==================== Pull Settings ====================

	/** Camera-relative offset where the weapon flies to during pull (X=forward, Y=right, Z=up) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pull")
	FVector PullTargetOffset = FVector(60.0f, 10.0f, -15.0f);

	/** Target rotation at pull end (relative to camera) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pull")
	FRotator PullTargetRotation = FRotator::ZeroRotator;

	/** Duration of the pull interpolation (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pull", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float PullDuration = 0.4f;

	// ==================== Effects ====================

	/** Sound played when weapon is picked up */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	TObjectPtr<USoundBase> PickupSound;

	// ==================== Stun on Impact (thrown weapon) ====================

	/** When true, this dropped weapon stuns AShooterNPCs it collides with above the velocity
	 *  threshold. Set externally (e.g. by ShooterCharacter::ThrowYankedWeaponIfAny). Default false
	 *  so passively-discarded weapons don't stun NPCs that walk into them. */
	UPROPERTY(BlueprintReadWrite, Category = "Stun")
	bool bCanStunOnImpact = false;

	/** Duration (seconds) of the stun applied to NPCs hit by this weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stun", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float StunDuration = 2.0f;

	/** Minimum impact velocity (cm/s) required to trigger a stun. Lower velocities are ignored
	 *  (prevents stun-spam from a settling/rolling weapon). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stun", meta = (ClampMin = "0.0"))
	float StunImpactVelocityThreshold = 400.0f;

	/** Cooldown (seconds) between stun events. Prevents multi-stun from a single bounce. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stun", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float StunCooldown = 0.5f;

	/** Optional montage to play on the stunned NPC. If null, ApplyExplosionStun falls back to
	 *  the NPC's KnockbackMontage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stun")
	TObjectPtr<UAnimMontage> StunMontage;

	// ==================== Public API ====================

	/** Get current charge */
	UFUNCTION(BlueprintPure, Category = "EMF")
	float GetCharge() const;

	/** Set charge directly */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void SetCharge(float NewCharge);

	/** Calculate effective capture range (logarithmic, same formula as DroppedMeleeWeapon) */
	float CalculateCaptureRange() const;

	/** Begin scripted pull toward camera-relative target */
	void StartPull(AShooterCharacter* PullingPlayer);

	/** Class of the weapon the player was holding when StartPull fired. Captured so the
	 *  Bandolier-pickup check at CompletePull uses the class at pull-START — the player may
	 *  have switched weapons during the pull and we want pull-time intent, not current state.
	 *  Null if pull never started or player was unarmed. */
	UPROPERTY()
	TSubclassOf<AShooterWeapon> PullingClientCurrentWeaponClass;

	/** Is currently being pulled toward player? */
	bool IsBeingPulled() const { return bIsBeingPulled; }

	/** Has pull completed (weapon granted)? */
	bool IsPullComplete() const { return bPullComplete; }

	/** Roll SpawnedBulletCount from AmmoDistributionCurve via inverse-transform sampling, or
	 *  uniform random [1, MagazineSize] when curve is null. Result is clamped to [1, MagazineSize].
	 *  Called by HumanoidNPC::YankCurrentWeapon — death-drop path does NOT call this, so the
	 *  granted weapon stays at full mag with infinite refills (current behavior preserved). */
	UFUNCTION(BlueprintCallable, Category = "Ammo")
	void RollSpawnedBulletCount();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** OnComponentHit callback for WeaponMesh — applies stun to AShooterNPC targets when
	 *  bCanStunOnImpact is true and impact velocity exceeds StunImpactVelocityThreshold. */
	UFUNCTION()
	void OnWeaponMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

private:
	/** Time of last stun event for cooldown checking (world seconds). */
	float LastStunTime = -10.0f;

	// ==================== Pull State ====================

	bool bIsBeingPulled = false;
	bool bPullComplete = false;
	float PullElapsed = 0.0f;
	FVector PullStartLocation = FVector::ZeroVector;
	FRotator PullStartRotation = FRotator::ZeroRotator;

	UPROPERTY()
	TWeakObjectPtr<AShooterCharacter> PullingCharacter;

	/** Update pull interpolation each tick */
	void UpdatePull(float DeltaTime);

	/** Called when pull interpolation completes: hide self, grant weapon */
	void CompletePull();
};
