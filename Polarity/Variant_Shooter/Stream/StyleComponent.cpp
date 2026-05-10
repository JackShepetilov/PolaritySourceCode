// StyleComponent.cpp
// Phase 1: skeleton — tick is enabled, public API is in place, but scoring/freshness
// logic is empty (Phase 2). Logging tag: [STREAM_DEBUG].

#include "StyleComponent.h"

#include "StreamConfig.h"

UStyleComponent::UStyleComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UStyleComponent::SetConfig(UStreamConfig* InConfig)
{
	Config = InConfig;
	UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] StyleComponent config set: %s"),
		InConfig ? *InConfig->GetName() : TEXT("NULL"));
}

void UStyleComponent::RegisterAction(const FStyleAction& Action)
{
	// Phase 2 will compute Spectacle * Freshness * InstanceMultiplier and accumulate into CurrentStyle.
}

void UStyleComponent::ResetStyleState()
{
	CategoryHistories.Reset();
	CurrentStyle = 0.0f;
	CurrentLikesPerSecond = 0.0f;
	TimeSinceLastAction = 0.0f;
}

void UStyleComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UStyleComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Phase 2: decay CurrentStyle after grace period, prune category histories,
	// recompute LikesPerSecond every frame.
}

float UStyleComponent::ComputeFreshness(EStyleCategory Category) const
{
	// Phase 2: count entries within window in CategoryHistories[Category],
	// then sample Config->Freshness.RepetitionMultiplier curve.
	return 1.0f;
}

void UStyleComponent::RecomputeLikesPerSecond()
{
	// Phase 2: sample Config->LikesPerSecondCurve at CurrentStyle.
}

void UStyleComponent::PruneCategoryHistories()
{
	// Phase 2.
}
