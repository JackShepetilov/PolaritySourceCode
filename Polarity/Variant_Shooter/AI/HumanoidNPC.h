// HumanoidNPC.h
// Organic humanoid enemy — holds ranged weapons that the player can yank out.
// Body accumulates charge like a robot, but EMF forces (knockback, capture, launch) are immune.
// After all weapons are yanked, switches to melee mode and stops accepting charge.

#pragma once

#include "CoreMinimal.h"
#include "MeleeNPC.h"
#include "HumanoidNPC.generated.h"

class ADroppedRangedWeapon;
class AShooterCharacter;
class UAnimInstance;
class UNPCRiotShieldComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHumanoidEnteredMeleeMode, AHumanoidNPC*, Humanoid);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHumanoidExitedMeleeMode,  AHumanoidNPC*, Humanoid);

UCLASS()
class POLARITY_API AHumanoidNPC : public AMeleeNPC
{
	GENERATED_BODY()

public:

	AHumanoidNPC(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Fired when humanoid transitions to melee mode (last weapon yanked).
	 *  Bind in BP to show melee weapon mesh, swap AnimBP, etc. */
	UPROPERTY(BlueprintAssignable, Category = "Humanoid|Events")
	FOnHumanoidEnteredMeleeMode OnEnteredMeleeMode;

	/** Fired when humanoid transitions back to ranged mode (pool respawn refilled inventory).
	 *  Bind in BP to hide melee weapon mesh, restore ranged AnimBP, etc. */
	UPROPERTY(BlueprintAssignable, Category = "Humanoid|Events")
	FOnHumanoidExitedMeleeMode OnExitedMeleeMode;

	// ==================== Inventory ====================

	/** Weapons held in sequence. Index 0 = first weapon. After each yank, next index becomes active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Humanoid|Inventory")
	TArray<TSubclassOf<AShooterWeapon>> WeaponInventory;

	/** 1:1 mapping with WeaponInventory — which ADroppedRangedWeapon to spawn on yank.
	 *  If arrays differ in length, missing entries result in yank without spawn (weapon just despawns). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Humanoid|Inventory")
	TArray<TSubclassOf<ADroppedRangedWeapon>> WeaponDropMapping;

	// ==================== Yank Animation ====================

	/** Animation played when player yanks from in front */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Humanoid|Yank")
	TObjectPtr<UAnimMontage> YankFrontMontage;

	/** Animation played when player yanks from behind */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Humanoid|Yank")
	TObjectPtr<UAnimMontage> YankBackMontage;

	/** Animation played when player yanks from the left */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Humanoid|Yank")
	TObjectPtr<UAnimMontage> YankLeftMontage;

	/** Animation played when player yanks from the right */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Humanoid|Yank")
	TObjectPtr<UAnimMontage> YankRightMontage;

	/** Delay between yank and next weapon spawn — should match yank animation length */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Humanoid|Yank", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float WeaponSwitchDelay = 0.6f;

	// ==================== Mode State ====================

	/** True after last weapon yanked — humanoid fights in melee and no longer accepts charge */
	UPROPERTY(BlueprintReadOnly, Category = "Humanoid|State")
	bool bIsInMeleeMode = false;

	/** True while humanoid can receive charge from the player. False in melee mode. */
	UPROPERTY(BlueprintReadOnly, Category = "Humanoid|State")
	bool bChargeReceptive = true;

	// ==================== Melee Mode Visuals ====================

	/** AnimBP applied on entry to melee mode. Leave null to keep the ranged AnimBP. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Humanoid|Melee Mode")
	TSubclassOf<UAnimInstance> MeleeAnimClass;

	/** MaxWalkSpeed applied on entry to melee mode. Leave 0 to keep the ranged speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Humanoid|Melee Mode", meta = (ClampMin = "0"))
	float MeleeMaxWalkSpeed = 0.0f;

	// ==================== Public API ====================

	/** Player yanks the current weapon from the humanoid's hands.
	 *  Spawns ADroppedRangedWeapon, starts its pull to player, zeroes body charge,
	 *  plays directional animation, and schedules SpawnNextWeapon() after WeaponSwitchDelay.
	 *  @return false if CanBeYanked() == false or Puller is null. */
	UFUNCTION(BlueprintCallable, Category = "Humanoid")
	bool YankCurrentWeapon(AShooterCharacter* Puller);

	/** Effective yank range for current state.
	 *  Formula: BaseRange * max(1, 1 + ln(|q_npc * q_player| / NormCoeff))
	 *  Returns 0 if CanBeYanked() == false or either charge is zero. */
	UFUNCTION(BlueprintPure, Category = "Humanoid")
	float CalculateWeaponYankRange() const;

	/** True if a yank can start: ranged mode, weapon valid, no yank in progress, not dead.
	 *  Returns false while a shield is still active — shield must be yanked first. */
	UFUNCTION(BlueprintPure, Category = "Humanoid")
	bool CanBeYanked() const;

	// ==================== Shield API ====================

	/** Optional shield component. Always present, but inactive unless its ShieldMeshAsset+PickupClass are set
	 *  (so vanilla BP_HumanoidNPC keeps working unchanged). */
	UFUNCTION(BlueprintPure, Category = "Humanoid|Shield")
	UNPCRiotShieldComponent* GetShieldComponent() const { return ShieldComponent; }

	/** True if a shield yank can start: shield active, not dead, not in melee mode. */
	UFUNCTION(BlueprintPure, Category = "Humanoid|Shield")
	bool CanShieldBeYanked() const;

	/** Player yanks the shield from this NPC. Spawns ARiotShieldPickup with impulse toward Puller.
	 *  @return false if CanShieldBeYanked() == false or Puller is null. */
	UFUNCTION(BlueprintCallable, Category = "Humanoid|Shield")
	bool YankShield(AShooterCharacter* Puller);

	/** Effective shield-yank range; same charge formula as weapon yank. 0 when shield can't be yanked. */
	UFUNCTION(BlueprintPure, Category = "Humanoid|Shield")
	float CalculateShieldYankRange() const;

	/** Reset state for pool recycling. Clears weapon index, mode flags, re-spawns first weapon.
	 *  override is critical: ArenaManager calls ResetForPool through AShooterNPC*, so without
	 *  virtual dispatch this method would be hidden and the parent's version would run instead. */
	virtual void ResetForPool(const FVector& NewLocation, const FRotator& NewRotation) override;

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Saves charge before Super call, restores it when !bChargeReceptive (melee mode). */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	// ==================== EMF / Capture Immunity ====================

	virtual void ApplyKnockback(const FVector& KnockbackDir, float Distance, float Duration,
		const FVector& AttackerLocation = FVector::ZeroVector, bool bKeepEMFEnabled = false,
		EKnockbackStyle Style = EKnockbackStyle::Standard) override;

	virtual void ApplyKnockbackVelocity(const FVector& KnockbackVelocity,
		float StunDuration = 0.3f) override;

	virtual void EnterCapturedState(UAnimMontage* OverrideMontage = nullptr) override;
	virtual void EnterLaunchedState() override;

	// ==================== Internal Weapon Management ====================

	/** Increments CurrentWeaponIndex and spawns the next weapon, or calls EnterMeleeMode(). */
	void SpawnNextWeapon();

	/** Destroys Weapon actor and nulls the pointer. */
	void DespawnCurrentWeapon();

	/** Transitions to melee-only mode: sets flags, zeroes charge, locks it, removes weapon. */
	void EnterMeleeMode();

	/** Fired by OnChargeUpdated while in melee mode — forces charge back to 0. */
	UFUNCTION()
	void OnChargeUpdatedInMeleeMode(float ChargeValue, uint8 Polarity);

	/** Returns the yank montage for the angle between NPC forward and direction to Puller.
	 *  Front <45°, Back >135°, Right 45-135°, Left -135 to -45°. */
	UAnimMontage* SelectYankMontageForDirection(const FVector& PullerLocation) const;

private:

	/** Thin UFUNCTION wrapper — AddDynamic cannot bind to inherited protected methods directly. */
	UFUNCTION()
	void OnWeaponShotFiredForward();

	/** Current index in WeaponInventory */
	int32 CurrentWeaponIndex = 0;

	/** Timer between yank and next weapon spawn */
	FTimerHandle WeaponSwitchTimer;

	/** Currently playing yank montage (tracked to avoid double-play) */
	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveYankMontage;

	/** AnimBP class active at BeginPlay (before any melee swap). Restored on pool reset. */
	UPROPERTY()
	TSubclassOf<UAnimInstance> CachedRangedAnimClass;

	/** MaxWalkSpeed value at BeginPlay. Restored on pool reset. */
	float CachedRangedMaxWalkSpeed = 0.0f;

	/** Optional shield slot. Subobject is always created so designers can opt-in via UPROPERTY in BP children. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Humanoid|Shield", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UNPCRiotShieldComponent> ShieldComponent;
};
