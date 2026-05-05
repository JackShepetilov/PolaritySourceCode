// RiotShieldPickup.h
// World pickup for the RiotShield. Overlap with a player without a shield equips one;
// also used as the thrown-shield form (physics-enabled, configured by SpawnAsThrown).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RiotShieldPickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class ARiotShield;

UCLASS()
class POLARITY_API ARiotShieldPickup : public AActor
{
	GENERATED_BODY()

public:
	ARiotShieldPickup();

	/** Configure this pickup as the result of a throw: enable physics, apply impulses, briefly disable re-pickup. */
	void SpawnAsThrown(const FVector& WorldLinearImpulse, const FVector& AngularImpulseDeg);

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> SphereCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Shield actor class to spawn on the player when picked up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup")
	TSubclassOf<ARiotShield> ShieldClass;

	/** Time after a throw during which this pickup ignores overlaps (so player can't immediately re-pick). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float ReacquireDelay = 0.6f;

	/** Set to true when spawned as a thrown shield (disables overlap until ReacquireDelay elapses). */
	bool bThrownMode = false;

	float TimeUntilReacquireEnabled = 0.0f;

	virtual void Tick(float DeltaTime) override;
};
