// BenchGrenadeRack.h
// One place of the grenade stand on the bench level: grenades lie here to be picked up, and once they
// have been taken more appear, as on the weapon racks [author, 2026-10-15].
//
// Hand-placed (or by Source/Tools/CombatBench/build_bench_armory.py, step_grenades), one actor per
// pickup, the type set in the Details panel. What lies on it is an ordinary AGrenadePickup, so taking
// it is the game's own pick-up. Server only, like ABenchWeaponRack.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BenchGrenadeRack.generated.h"

class AGrenadePickup;
class UGrenadeDefinition;
class UStaticMesh;
class UTextRenderComponent;

UCLASS()
class POLARITY_API ABenchGrenadeRack : public AActor
{
	GENERATED_BODY()

public:

	ABenchGrenadeRack();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rack")
	TObjectPtr<UGrenadeDefinition> GrenadeType;

	/** How many in one pickup. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rack", meta = (ClampMin = "1"))
	int32 Count = 3;

	/** What the pickup looks like on the stand. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rack")
	TObjectPtr<UStaticMesh> PickupMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rack")
	FVector PickupMeshScale = FVector(1.6f);

	/** How often the rack looks whether its grenades are still there (s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rack", meta = (ClampMin = "0.2", Units = "s"))
	float RefillInterval = 1.5f;

protected:

	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rack")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rack")
	TObjectPtr<UTextRenderComponent> Label;

private:

	void Refill();

	TWeakObjectPtr<AGrenadePickup> Pickup;
	FTimerHandle RefillTimer;
};
