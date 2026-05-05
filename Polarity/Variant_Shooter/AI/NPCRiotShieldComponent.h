// NPCRiotShieldComponent.h
// Optional shield held by a HumanoidNPC. No-op when ShieldMeshAsset or PickupClass is unset
// (so the same component can sit on every BP_HumanoidNPC variant without affecting unshielded ones).
// On Yank: spawns ARiotShieldPickup at the mesh pose with an impulse toward the puller, then
// hides itself. The pickup's existing overlap flow equips it on the player.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCRiotShieldComponent.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class ARiotShieldPickup;
class AShooterCharacter;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class POLARITY_API UNPCRiotShieldComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNPCRiotShieldComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** True only when ShieldMeshAsset + PickupClass were valid at activation AND the shield has not been yanked yet. */
	UFUNCTION(BlueprintPure, Category = "Shield")
	bool HasActiveShield() const { return bShieldActive; }

	/** Yank gate: same logic as AHumanoidNPC::CanBeYanked but for the shield slot. */
	UFUNCTION(BlueprintPure, Category = "Shield")
	bool CanBeYanked() const;

	/** Effective yank range using the same q_npc * q_player formula as DroppedRangedWeapon. */
	UFUNCTION(BlueprintPure, Category = "Shield")
	float CalculateYankRange() const;

	/** Spawn ARiotShieldPickup with an impulse toward Puller and deactivate the local mesh. */
	UFUNCTION(BlueprintCallable, Category = "Shield")
	bool TryYank(AShooterCharacter* Puller);

	/** Reset for pool recycling. Re-attaches/re-shows the mesh if asset is still valid. */
	UFUNCTION(BlueprintCallable, Category = "Shield")
	void ResetForPool();

	/** Pickup class to spawn on yank. Public read for the capture scanner gating. */
	UFUNCTION(BlueprintPure, Category = "Shield")
	TSubclassOf<ARiotShieldPickup> GetPickupClass() const { return PickupClass; }

protected:
	// ==================== Shield Asset ====================

	/** Visual mesh of the shield. Null = component is a no-op for this NPC. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield")
	TObjectPtr<UStaticMesh> ShieldMeshAsset;

	/** Pickup class spawned on yank. Null = component is a no-op (no point activating without a takeable form). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield")
	TSubclassOf<ARiotShieldPickup> PickupClass;

	// ==================== Attachment ====================

	/** Owner skeletal-mesh socket the shield mesh is attached to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Attach")
	FName AttachSocketName = FName("HandGrip_R");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Attach")
	FVector RelativeLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Attach")
	FRotator RelativeRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Attach")
	FVector RelativeScale = FVector::OneVector;

	// ==================== Aim ====================

	/** When true, shield mesh is rotated each tick so its world-yaw faces the player camera. Pitch/roll left at zero. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Aim")
	bool bAimAtPlayer = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Aim", meta = (ClampMin = "0.1"))
	float AimYawInterpSpeed = 8.0f;

	/** Extra yaw offset (deg) applied on top of look-at-player yaw. Use to align mesh forward with the shield's blocking face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Aim")
	float AimYawOffsetDeg = 0.0f;

	// ==================== Yank ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Yank", meta = (ClampMin = "50.0"))
	float YankBaseRange = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Yank", meta = (ClampMin = "0.01"))
	float YankNormCoeff = 50.0f;

	/** Linear impulse magnitude (cm/s * mass) applied to the spawned pickup, directed from shield toward Puller. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Yank", meta = (ClampMin = "0.0"))
	float YankLinearImpulseMagnitude = 1500.0f;

	/** Angular impulse (deg) applied to the spawned pickup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield|Yank")
	FVector YankAngularImpulseDeg = FVector(0.0f, 600.0f, 0.0f);

private:
	/** Runtime visual mesh. Created and registered when activation succeeds. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> ShieldMesh;

	bool bShieldActive = false;

	/** Build ShieldMesh, attach to owner mesh socket, register, mark active.
	 *  No-op if ShieldMeshAsset or PickupClass is unset. */
	void TryActivate();

	/** Destroy ShieldMesh and clear active flag. */
	void Deactivate();

	USkeletalMeshComponent* GetOwnerSkeletalMesh() const;
};
