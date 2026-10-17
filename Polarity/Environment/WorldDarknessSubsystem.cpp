// WorldDarknessSubsystem.cpp

#include "Environment/WorldDarknessSubsystem.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<float> CVarWorldDarkness(
	TEXT("polarity.world.darkness"),
	-1.0f,
	TEXT("Override how dark the world is, 0 day .. 1 night. -1 (default): whatever the day and night cycle wrote."));

float UWorldDarknessSubsystem::GetDarkness(const UObject* WorldContext)
{
	const float Override = CVarWorldDarkness.GetValueOnGameThread();
	if (Override >= 0.0f)
	{
		return FMath::Clamp(Override, 0.0f, 1.0f);
	}
	const UWorld* const World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UWorldDarknessSubsystem* const Self = World ? World->GetSubsystem<UWorldDarknessSubsystem>() : nullptr;
	return Self ? Self->Darkness : 0.0f;
}

void UWorldDarknessSubsystem::SetDarkness(const UObject* WorldContext, float InDarkness)
{
	const UWorld* const World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (UWorldDarknessSubsystem* const Self = World ? World->GetSubsystem<UWorldDarknessSubsystem>() : nullptr)
	{
		Self->Darkness = FMath::Clamp(InDarkness, 0.0f, 1.0f);
	}
}
