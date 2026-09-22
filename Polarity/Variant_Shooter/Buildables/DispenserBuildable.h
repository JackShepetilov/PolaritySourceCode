#pragma once

#include "CoreMinimal.h"
#include "BuildableActor.h"
#include "DispenserBuildable.generated.h"

/**
 * The dispenser BUILDING, as a class: a marker and a place for per-weapon tuning of the melt
 * value. The heal/ammunition/fuel behaviour itself lives on ABuildableActor and switches on for
 * any buildable whose DEFINITION carries the Buildable.Dispenser tag, because the project's
 * dispenser Blueprint (BP_Buildable_Dispenser) predates this class and is a DIRECT child of
 * ABuildableActor.
 */
UCLASS(Blueprintable)
class POLARITY_API ADispenserBuildable : public ABuildableActor
{
	GENERATED_BODY()

public:
	ADispenserBuildable();
};