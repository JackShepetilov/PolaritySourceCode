// StyleComponent.cpp
// Phase 2: style accumulation, freshness tracking, LPS computation.
// Logging tag: [STREAM_DEBUG].

#include "StyleComponent.h"

#include "StreamConfig.h"

#include "Curves/CurveFloat.h"
#include "Engine/World.h"

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
	if (Action.Category == EStyleCategory::None)
	{
		return;
	}

	UStreamConfig* Cfg = Config.Get();
	if (!Cfg)
	{
		return;
	}

	const float* SpectaclePtr = Cfg->SpectacleScores.Find(Action.Category);
	if (!SpectaclePtr)
	{
		// Category has no configured score yet — silent no-op (designer hasn't filled the table).
		return;
	}
	const float Spectacle = *SpectaclePtr;
	const float Freshness = ComputeFreshness(Action.Category);
	const float StylePoints = Spectacle * Freshness * Action.InstanceMultiplier;

	const float MaxStyle = (Cfg->MaxStyle > 0.0f) ? Cfg->MaxStyle : 10000.0f;
	CurrentStyle = FMath::Clamp(CurrentStyle + StylePoints, 0.0f, MaxStyle);
	TimeSinceLastAction = 0.0f;

	const float Now = (GetWorld() != nullptr) ? GetWorld()->GetTimeSeconds() : 0.0f;
	FCategoryHistory& History = CategoryHistories.FindOrAdd(Action.Category);
	History.Timestamps.Add(Now);

	RecomputeLikesPerSecond();

	const int32 HeartCount = FMath::Max(1, FMath::RoundToInt(StylePoints / 10.0f));
	OnLikesGenerated.Broadcast(HeartCount, Action.WorldLocation);

	UE_LOG(LogTemp, Log,
		TEXT("[STREAM_DEBUG] Action=%d Spectacle=%.0f Fresh=%.2f StyleAdded=%.0f Style=%.0f LPS=%.1f Hearts=%d"),
		static_cast<int32>(Action.Category), Spectacle, Freshness, StylePoints,
		CurrentStyle, CurrentLikesPerSecond, HeartCount);
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

	UStreamConfig* Cfg = Config.Get();
	if (!Cfg)
	{
		return;
	}

	TimeSinceLastAction += DeltaTime;

	if (TimeSinceLastAction > Cfg->StyleDecayGracePeriod && CurrentStyle > 0.0f)
	{
		CurrentStyle = FMath::Max(0.0f, CurrentStyle - Cfg->StyleDecayPerSecond * DeltaTime);
	}

	PruneCategoryHistories();
	RecomputeLikesPerSecond();
}

float UStyleComponent::ComputeFreshness(EStyleCategory Category) const
{
	UStreamConfig* Cfg = Config.Get();
	if (!Cfg)
	{
		return 1.0f;
	}

	UCurveFloat* Curve = Cfg->Freshness.RepetitionMultiplier;
	if (!Curve)
	{
		return 1.0f;
	}

	const float WindowSeconds = FMath::Max(0.1f, Cfg->Freshness.WindowSeconds);
	const float Now = (GetWorld() != nullptr) ? GetWorld()->GetTimeSeconds() : 0.0f;

	int32 Repetitions = 0;
	if (const FCategoryHistory* History = CategoryHistories.Find(Category))
	{
		for (float Timestamp : History->Timestamps)
		{
			if (Now - Timestamp <= WindowSeconds)
			{
				++Repetitions;
			}
		}
	}

	return Curve->GetFloatValue(static_cast<float>(Repetitions));
}

void UStyleComponent::RecomputeLikesPerSecond()
{
	UStreamConfig* Cfg = Config.Get();
	if (!Cfg)
	{
		CurrentLikesPerSecond = 0.0f;
		return;
	}

	if (UCurveFloat* Curve = Cfg->LikesPerSecondCurve)
	{
		CurrentLikesPerSecond = Curve->GetFloatValue(CurrentStyle);
	}
	else
	{
		CurrentLikesPerSecond = 0.0f;
	}
}

void UStyleComponent::PruneCategoryHistories()
{
	UStreamConfig* Cfg = Config.Get();
	if (!Cfg)
	{
		return;
	}

	const float WindowSeconds = FMath::Max(0.1f, Cfg->Freshness.WindowSeconds);
	const float Now = (GetWorld() != nullptr) ? GetWorld()->GetTimeSeconds() : 0.0f;

	for (auto& Pair : CategoryHistories)
	{
		FCategoryHistory& History = Pair.Value;
		History.Timestamps.RemoveAll([Now, WindowSeconds](float Timestamp)
		{
			return (Now - Timestamp) > WindowSeconds;
		});
	}
}
