// SiegeCoreBuildable.h
// A siege core placed straight into a level.
//
// The core's rule and its finders live on ABuildableActor (bSiegeCore, DefendRadius, IsDefended,
// FindUndefendedCore / FindNearestCore), because on a real siege map the core is the first
// dispenser a player builds (ASiegeDirector::NotifyBuildablePlaced). This class is what a bench
// level uses instead: a building that is the core from the start, with a lot of health.
//
// Placed straight into the level (no builder, no owner), so it starts Active at full health like
// any level-placed ABuildableActor. Without a UBuildableDefinition the wrench does nothing to it.

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/Buildables/BuildableActor.h"
#include "SiegeCoreBuildable.generated.h"

UCLASS(Blueprintable)
class POLARITY_API ASiegeCoreBuildable : public ABuildableActor
{
	GENERATED_BODY()

public:

	ASiegeCoreBuildable();
};
