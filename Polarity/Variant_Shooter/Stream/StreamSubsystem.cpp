// StreamSubsystem.cpp
// Phase 1: subsystem boots, subscribes to RunSubsystem lifecycle, and is tickable.
// All math (viewer target / smoothing / donation roll) is empty — Phase 3+ fills it
// in via Live Coding without touching this header. Logging tag: [STREAM_DEBUG].

#include "StreamSubsystem.h"

#include "StreamConfig.h"
#include "StreamArenaConfig.h"
#include "StyleComponent.h"

#include "Engine/GameInstance.h"
#include "Stats/Stats.h"

void UStreamSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency(URunSubsystem::StaticClass());

	if (URunSubsystem* Run = GetRunSubsystem())
	{
		Run->OnRunStarted.AddDynamic(this, &UStreamSubsystem::HandleRunStarted);
		Run->OnRunEnded.AddDynamic(this, &UStreamSubsystem::HandleRunEnded);
		Run->OnArenaEntered.AddDynamic(this, &UStreamSubsystem::HandleArenaEntered);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[STREAM_DEBUG] RunSubsystem unavailable in Initialize — Stream will not function"));
	}
}

void UStreamSubsystem::Deinitialize()
{
	if (URunSubsystem* Run = GetRunSubsystem())
	{
		Run->OnRunStarted.RemoveDynamic(this, &UStreamSubsystem::HandleRunStarted);
		Run->OnRunEnded.RemoveDynamic(this, &UStreamSubsystem::HandleRunEnded);
		Run->OnArenaEntered.RemoveDynamic(this, &UStreamSubsystem::HandleArenaEntered);
	}

	Super::Deinitialize();
}

URunSubsystem* UStreamSubsystem::GetRunSubsystem() const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<URunSubsystem>();
	}
	return nullptr;
}

// ==================== FTickableGameObject ====================

void UStreamSubsystem::Tick(float DeltaTime)
{
	// Phase 3: sample LPS, recompute ViewerTarget, smooth Viewers, roll donations.
}

TStatId UStreamSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UStreamSubsystem, STATGROUP_Tickables);
}

bool UStreamSubsystem::IsTickable() const
{
	return bRunActive;
}

// ==================== Config ====================

void UStreamSubsystem::SetConfig(UStreamConfig* InConfig)
{
	Config = InConfig;
	UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Config set: %s"),
		InConfig ? *InConfig->GetName() : TEXT("NULL"));

	if (UStyleComponent* Style = StyleComponent.Get())
	{
		Style->SetConfig(InConfig);
	}
}

void UStreamSubsystem::SetArenaConfig(UStreamArenaConfig* InArenaConfig)
{
	ArenaConfig = InArenaConfig;
	UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] ArenaConfig set: %s"),
		InArenaConfig ? *InArenaConfig->GetName() : TEXT("NULL"));
}

void UStreamSubsystem::RegisterStyleComponent(UStyleComponent* InStyle)
{
	StyleComponent = InStyle;
	UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] StyleComponent registered: %s"),
		InStyle ? *InStyle->GetName() : TEXT("NULL"));

	if (InStyle && Config.IsValid())
	{
		InStyle->SetConfig(Config.Get());
	}
}

// ==================== Read API ====================

float UStreamSubsystem::GetRunElapsedSeconds() const
{
	if (!bRunActive)
	{
		return 0.0f;
	}
	return static_cast<float>(FPlatformTime::Seconds() - RunStartTimeSeconds);
}

// ==================== Meta currency ====================

void UStreamSubsystem::AddMetaCurrency(int64 Amount)
{
	if (Amount <= 0)
	{
		return;
	}
	MetaCurrency += Amount;
	OnMetaCurrencyChanged.Broadcast(MetaCurrency);
	UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] MetaCurrency += %lld -> %lld"), Amount, MetaCurrency);
}

bool UStreamSubsystem::SpendMetaCurrency(int64 Amount)
{
	if (Amount <= 0 || MetaCurrency < Amount)
	{
		return false;
	}
	MetaCurrency -= Amount;
	OnMetaCurrencyChanged.Broadcast(MetaCurrency);
	UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] MetaCurrency -= %lld -> %lld"), Amount, MetaCurrency);
	return true;
}

// ==================== Run lifecycle handlers ====================

void UStreamSubsystem::HandleRunStarted()
{
	bRunActive = true;
	RunStartTimeSeconds = FPlatformTime::Seconds();
	CurrentViewers = 0;
	ViewerTarget = 0;
	DonationRollAccumulator = 0.0f;

	if (UStyleComponent* Style = StyleComponent.Get())
	{
		Style->ResetStyleState();
	}

	UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Run started"));
}

void UStreamSubsystem::HandleRunEnded(ERunEndReason Reason)
{
	bRunActive = false;
	UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Run ended, reason=%d, final viewers=%d"), (int32)Reason, CurrentViewers);
}

void UStreamSubsystem::HandleArenaEntered(int32 ArenaIndex)
{
	UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Arena entered: %d"), ArenaIndex);
	// Phase 3+: arena-specific reset / config swap if needed.
}

// ==================== Tick helpers (skeletons) ====================

float UStreamSubsystem::SampleLikesPerSecond() const
{
	if (UStyleComponent* Style = StyleComponent.Get())
	{
		return Style->GetLikesPerSecond();
	}
	return 0.0f;
}

void UStreamSubsystem::RecomputeViewerTarget(float TimeIntoRunSeconds, float LikesPerSecond)
{
	// Phase 3.
}

void UStreamSubsystem::UpdateViewers(float DeltaTime)
{
	// Phase 3.
}

void UStreamSubsystem::TickDonations(float DeltaTime, float LikesPerSecond)
{
	// Phase 4.
}

FString UStreamSubsystem::PickDonorName() const
{
	if (UStreamConfig* Cfg = Config.Get())
	{
		if (Cfg->DonorNamePool.Num() > 0)
		{
			const int32 Idx = FMath::RandRange(0, Cfg->DonorNamePool.Num() - 1);
			return Cfg->DonorNamePool[Idx];
		}
	}
	return TEXT("anonymous");
}
