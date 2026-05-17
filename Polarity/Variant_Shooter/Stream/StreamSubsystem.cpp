// StreamSubsystem.cpp
// Phases 1-4: subsystem lifecycle, viewer simulation, and donation generation.
// Logging tag: [STREAM_DEBUG].

#include "StreamSubsystem.h"

#include "StreamConfig.h"
#include "StreamArenaConfig.h"
#include "StyleComponent.h"
#include "ChatBroker.h"

#include "Polarity/Arena/ArenaManager.h"
#include "Polarity/Variant_Shooter/Lore/LoreSubsystem.h"

#include "Curves/CurveFloat.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameplayTagContainer.h"
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

	ChatBroker = NewObject<UChatBroker>(this);
	ChatBroker->Init(this);
}

void UStreamSubsystem::Deinitialize()
{
	if (ChatBroker)
	{
		ChatBroker->Shutdown();
		ChatBroker = nullptr;
	}

	if (URunSubsystem* Run = GetRunSubsystem())
	{
		Run->OnRunStarted.RemoveDynamic(this, &UStreamSubsystem::HandleRunStarted);
		Run->OnRunEnded.RemoveDynamic(this, &UStreamSubsystem::HandleRunEnded);
		Run->OnArenaEntered.RemoveDynamic(this, &UStreamSubsystem::HandleArenaEntered);
	}

	Super::Deinitialize();
}

void UStreamSubsystem::SetPlayerStreamerName(const FString& InName)
{
	PlayerStreamerName = InName.IsEmpty() ? TEXT("@ramless_") : InName;
	UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] PlayerStreamerName set: %s"), *PlayerStreamerName);
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
	if (!bRunActive)
	{
		return;
	}

	const float LPS = SampleLikesPerSecond();
	const float TimeIntoRun = GetRunElapsedSeconds();

	RecomputeViewerTarget(TimeIntoRun, LPS);
	UpdateViewers(DeltaTime);
	TickDonations(DeltaTime, LPS);
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

	if (ChatBroker)
	{
		ChatBroker->ApplyConfig(InConfig);
	}

	// Forward lore tables to LoreSubsystem.
	if (InConfig)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (ULoreSubsystem* Lore = GI->GetSubsystem<ULoreSubsystem>())
			{
				TArray<UDataTable*> Tables;
				for (const TObjectPtr<UDataTable>& T : InConfig->LoreTables)
				{
					if (T) { Tables.Add(T.Get()); }
				}
				Lore->SetLoreTables(Tables);
			}
		}
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

	if (ChatBroker)
	{
		ChatBroker->BindStyleComponent(InStyle);
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

	if (ChatBroker && Reason == ERunEndReason::PlayerDeath)
	{
		const FGameplayTag DeathTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Chat.Event.PlayerDeath")), false);
		ChatBroker->EmitReaction(DeathTag);
	}
}

void UStreamSubsystem::HandleArenaEntered(int32 ArenaIndex)
{
	UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Arena entered: %d"), ArenaIndex);

	// Auto-bind chat broker to the arena manager so antenna events flow into chat reactions.
	if (ChatBroker)
	{
		UWorld* World = nullptr;
		if (UGameInstance* GI = GetGameInstance())
		{
			World = GI->GetWorld();
		}
		if (World)
		{
			AArenaManager* FoundArena = nullptr;
			for (TActorIterator<AArenaManager> It(World); It; ++It)
			{
				FoundArena = *It;
				break;
			}
			ChatBroker->BindArenaManager(FoundArena);
		}
	}
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
	UStreamConfig* Cfg = Config.Get();
	if (!Cfg)
	{
		ViewerTarget = 0;
		return;
	}

	float TimeShape = 1.0f;
	if (UCurveFloat* Curve = Cfg->BaselineCurve)
	{
		TimeShape = Curve->GetFloatValue(TimeIntoRunSeconds);
	}

	float RankMul = 1.0f;
	if (UCurveFloat* Curve = Cfg->RankMultiplierCurve)
	{
		RankMul = Curve->GetFloatValue(LikesPerSecond);
	}

	float ArenaMul = 1.0f;
	if (UStreamArenaConfig* AC = ArenaConfig.Get())
	{
		ArenaMul = AC->ArenaMultiplier;
	}

	const float TargetFloat = static_cast<float>(Cfg->BasePopulation) * TimeShape * RankMul * ArenaMul;
	ViewerTarget = FMath::Max(0, FMath::RoundToInt(TargetFloat));
}

void UStreamSubsystem::UpdateViewers(float DeltaTime)
{
	UStreamConfig* Cfg = Config.Get();
	if (!Cfg)
	{
		return;
	}

	const float ApproachSpeed = FMath::Max(0.0f, Cfg->ViewerApproachSpeed);

	const float CurrentF = static_cast<float>(CurrentViewers);
	const float TargetF = static_cast<float>(ViewerTarget);
	const float NewF = FMath::FInterpTo(CurrentF, TargetF, DeltaTime, ApproachSpeed);
	const int32 NewViewers = FMath::RoundToInt(NewF);

	if (NewViewers != CurrentViewers)
	{
		CurrentViewers = NewViewers;
		OnViewersChanged.Broadcast(CurrentViewers);
	}
}

void UStreamSubsystem::TickDonations(float DeltaTime, float LikesPerSecond)
{
	UStreamConfig* Cfg = Config.Get();
	if (!Cfg || CurrentViewers <= 0)
	{
		return;
	}

	const float Divisor = FMath::Max(1.0f, Cfg->DonationDivisor);

	float ChanceMul = 1.0f;
	if (UCurveFloat* Curve = Cfg->DonationChanceCurve)
	{
		ChanceMul = Curve->GetFloatValue(LikesPerSecond);
	}

	const float DonationsPerSecond = (static_cast<float>(CurrentViewers) / Divisor) * ChanceMul;
	DonationRollAccumulator += DonationsPerSecond * DeltaTime;

	while (DonationRollAccumulator >= 1.0f)
	{
		DonationRollAccumulator -= 1.0f;

		FDonation Donation;
		Donation.DonorName = PickDonorName();

		if (UCurveFloat* AmtCurve = Cfg->DonationAmountCurve)
		{
			const float Roll = FMath::FRand();
			const float RawAmount = AmtCurve->GetFloatValue(Roll);
			const float ViewerScale = FMath::Pow(static_cast<float>(CurrentViewers), 0.3f);
			Donation.Amount = FMath::Max(1, FMath::RoundToInt(RawAmount * ViewerScale));
		}
		else
		{
			Donation.Amount = 1;
		}

		Donation.Message = FText::GetEmpty();

		OnDonationGenerated.Broadcast(Donation);
		AddMetaCurrency(static_cast<int64>(Donation.Amount));

		UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Donation: %s -> %d (Viewers=%d, LPS=%.1f)"),
			*Donation.DonorName, Donation.Amount, CurrentViewers, LikesPerSecond);
	}
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
