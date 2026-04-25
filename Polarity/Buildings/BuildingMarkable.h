// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "BuildingMarkable.generated.h"

UINTERFACE(MinimalAPI, Blueprintable, meta = (DisplayName = "Building Markable"))
class UBuildingMarkable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implemented by anything that can be "marked" by a player ability (e.g. 360 Shot).
 * The marker (a VFX) is spawned by the ability itself; the implementer reacts to being marked.
 *
 * Used by ATurretBuilding to spawn a kamikaze drone targeting the marked location.
 */
class POLARITY_API IBuildingMarkable
{
	GENERATED_BODY()

public:

	/**
	 * Called when this actor was marked by a player ability.
	 * @param HitLocation  World-space hit point
	 * @param HitNormal    Surface normal at the hit point (points away from the marked surface)
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Marking")
	void OnMarked(FVector HitLocation, FVector HitNormal);
};
