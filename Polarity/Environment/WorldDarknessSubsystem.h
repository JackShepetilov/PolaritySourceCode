// WorldDarknessSubsystem.h
// How dark the world is right now, 0 day to 1 night, in one place for everybody that cares.
//
// [author, 2026-10-15] The day and night cycle is the author's; it writes here (SetDarkness), and the
// things that read it (the soldiers noticing a flashlight or a laser from further away at night,
// UTacticalDeviceHandler_Light) only ever ask GetDarkness. Until something writes it, it is day.
// polarity.world.darkness overrides it for testing (-1, the default, means no override).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldDarknessSubsystem.generated.h"

UCLASS()
class POLARITY_API UWorldDarknessSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:

	/** 0 broad daylight, 1 the dark of night. The console override wins when set. */
	UFUNCTION(BlueprintPure, Category = "Environment", meta = (WorldContext = "WorldContext"))
	static float GetDarkness(const UObject* WorldContext);

	/** Written by the day and night cycle. Clamped to 0..1. */
	UFUNCTION(BlueprintCallable, Category = "Environment", meta = (WorldContext = "WorldContext"))
	static void SetDarkness(const UObject* WorldContext, float InDarkness);

private:

	float Darkness = 0.0f;
};
