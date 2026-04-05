// NoFlyZone.h
// Volume that prevents flying drones from entering confined spaces (buildings, tunnels, etc.)
// Place over buildings in the editor. Drones will reject flight points inside these volumes.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NoFlyZone.generated.h"

class UBoxComponent;

UCLASS()
class POLARITY_API ANoFlyZone : public AActor
{
	GENERATED_BODY()

public:

	ANoFlyZone();

	/** Check if a world-space point is inside any NoFlyZone in the given world */
	UFUNCTION(BlueprintPure, Category = "NoFlyZone", meta = (WorldContext = "WorldContextObject"))
	static bool IsPointInNoFlyZone(const UObject* WorldContextObject, const FVector& Point);

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Box volume defining the no-fly area */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> ZoneBox;

private:

	/** All active no-fly zones (for fast static lookup) */
	static TArray<TWeakObjectPtr<ANoFlyZone>> ActiveZones;
};
