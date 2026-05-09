// RiotShieldPickup.h
// World pickup for the RiotShield. Overlap with a player without a shield equips one;
// also used as the thrown-shield form (physics-enabled, configured by SpawnAsThrown).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RiotShieldPickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UEMF_FieldComponent;
class ARiotShield;

UCLASS()
class POLARITY_API ARiotShieldPickup : public AActor
{
	GENERATED_BODY()

public:
	ARiotShieldPickup();

	/** Configure this pickup as the result of a throw: enable physics, apply impulses, briefly disable re-pickup. */
	void SpawnAsThrown(const FVector& WorldLinearImpulse, const FVector& AngularImpulseDeg);

	/** Start a scripted pull toward Puller (canonical yank-flow). Disables physics+collision,
	 *  interpolates to a camera-relative target over PullDuration, then equips on arrival.
	 *  Use this instead of SpawnAsThrown for NPC-yank — guaranteed to reach the player. */
	void StartPull(class AShooterCharacter* InPuller);

	UFUNCTION(BlueprintPure, Category = "Pickup")
	bool IsBeingPulled() const { return bIsBeingPulled; }

	/** Arm/disarm impact-stun. Called by ARiotShield::ThrowAway when the player actively throws. Default false
	 *  so passively-spawned pickups don't stun NPCs that walk into them. */
	void SetCanStunOnImpact(bool bEnable) { bCanStunOnImpact = bEnable; }

	/** Get the pickup's stored EMF charge. */
	UFUNCTION(BlueprintPure, Category = "EMF")
	float GetCharge() const;

	/** Set the pickup's stored EMF charge. (Re)registers the floating charge widget when non-zero. */
	UFUNCTION(BlueprintCallable, Category = "EMF")
	void SetCharge(float NewCharge);

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> SphereCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** EMF field component: source of truth for GetCharge/SetCharge. Drives the floating charge widget. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UEMF_FieldComponent> FieldComponent;

	/** Shield actor class to spawn on the player when picked up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup")
	TSubclassOf<ARiotShield> ShieldClass;

	// ==================== Capture (channel/grab key acquisition) ====================
public:
	/** Can the channel/grab key initiate a pull on this pickup? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Capture")
	bool bCanBeCaptured = true;

	/** Fixed capture range (cm). Unlike DroppedRangedWeapon this does not scale with charge — the
	 *  shield can be captured regardless of charge value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Capture", meta = (ClampMin = "50.0", Units = "cm"))
	float CaptureRange = 500.0f;
protected:

	// ==================== Stun on Impact (mirrors ADroppedRangedWeapon) ====================

	/** When true, this thrown pickup stuns AShooterNPCs it collides with above the velocity threshold.
	 *  Set externally by the throwing path (ARiotShield::ThrowAway). Default false so passively-placed
	 *  pickups don't stun NPCs that walk into them. Pull-flow pickups never set this — collision is off then. */
	UPROPERTY(BlueprintReadWrite, Category = "Stun")
	bool bCanStunOnImpact = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stun", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float StunDuration = 2.0f;

	/** Minimum impact velocity (cm/s) required to trigger a stun. Lower velocities ignored
	 *  (settling/rolling pickup shouldn't stun NPCs that brush against it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stun", meta = (ClampMin = "0.0"))
	float StunImpactVelocityThreshold = 400.0f;

	/** Cooldown (seconds) between stun events on a single bounce. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stun", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float StunCooldown = 0.5f;

	/** Optional montage played on stunned NPC. Null falls back to the NPC's KnockbackMontage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stun")
	TObjectPtr<class UAnimMontage> StunMontage;

	/** Time after a throw during which this pickup ignores overlaps (so player can't immediately re-pick). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float ReacquireDelay = 0.6f;

	/** Set to true when spawned as a thrown shield (disables overlap until ReacquireDelay elapses). */
	bool bThrownMode = false;

	float TimeUntilReacquireEnabled = 0.0f;

	// ==================== Scripted Pull (NPC-yank flow) ====================

	/** Total scripted-pull duration in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Pull", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float PullDuration = 0.4f;

	/** Camera-relative final location offset (forward / right / up) at pull arrival. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup|Pull")
	FVector PullTargetOffset = FVector(60.0f, 0.0f, -10.0f);

	virtual void Tick(float DeltaTime) override;

private:
	bool bIsBeingPulled = false;
	float PullElapsed = 0.0f;
	FVector PullStartLocation = FVector::ZeroVector;
	FRotator PullStartRotation = FRotator::ZeroRotator;

	UPROPERTY()
	TWeakObjectPtr<AShooterCharacter> PullingCharacter;

	/** Drive lerp each tick during pull. */
	void UpdatePull(float DeltaTime);

	/** Pull arrival: spawn ARiotShield on PullingCharacter and Destroy. Same equip-path as OnOverlap. */
	void TryEquipOnPullingCharacter();

	// ==================== Stun ====================

	/** Last time this pickup successfully stunned an NPC (world seconds). Cooldown gate uses this. */
	float LastStunTime = -1.0f;

	UFUNCTION()
	void OnMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);
};
