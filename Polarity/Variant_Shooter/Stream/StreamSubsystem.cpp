// StreamSubsystem.cpp
// Phases 1-4: subsystem lifecycle, viewer simulation, and donation generation.
// Logging tag: [STREAM_DEBUG].

#include "StreamSubsystem.h"
#include "Coop/CoopPlayers.h"

#include "StreamConfig.h"
#include "StreamArenaConfig.h"
#include "StyleComponent.h"
#include "ChatBroker.h"

#include "Polarity/Arena/ArenaManager.h"
#include "Polarity/ApexMovementComponent.h"
#include "Polarity/EMFPhysicsProp.h"
#include "Polarity/Variant_Shooter/Abilities/AbilityComponent.h"
#include "Polarity/Variant_Shooter/ShooterCharacter.h"
#include "Polarity/Variant_Shooter/Lore/LoreSubsystem.h"
#include "Save/SaveGameSubsystem.h"
#include "Save/PolarityMetaSave.h"

#include "Curves/CurveFloat.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameplayTagContainer.h"
#include "Kismet/GameplayStatics.h"
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

	// Restore persisted meta from the save profile. SaveGameSubsystem depends on nobody, so this
	// dependency is acyclic and guarantees the meta file is loaded before we read it.
	if (USaveGameSubsystem* Save = Cast<USaveGameSubsystem>(
			Collection.InitializeDependency(USaveGameSubsystem::StaticClass())))
	{
		if (const UPolarityMetaSave* M = Save->GetMeta())
		{
			MetaCurrency       = M->MetaCurrency;
			CompletedRuns      = M->CompletedRuns;
			PlayerStreamerName = M->PlayerStreamerName; // may be empty == "not chosen yet"
			UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Restored meta: currency=%lld runs=%d name='%s'"),
				MetaCurrency, CompletedRuns, *PlayerStreamerName);
		}
	}
}

void UStreamSubsystem::Deinitialize()
{
	UnbindLearningTracking();

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
	// Empty stays empty == "not chosen yet" (the desktop prompts for a handle on first launch).
	PlayerStreamerName = InName;
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

	EnsureLearningTrackingBindings();
	PropBindingRefreshAccumulator += DeltaTime;
	if (PropBindingRefreshAccumulator >= 1.0f)
	{
		PropBindingRefreshAccumulator = 0.0f;
		RefreshPropExplosionBindings();
	}
	TickLearningReminders();
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

bool UStreamSubsystem::DebugTriggerLearningReminder(ELearningReminderType ReminderType)
{
	if (!bRunActive)
	{
		UE_LOG(LogTemp, Warning, TEXT("[STREAM_DEBUG] Learning reminder blocked: no active run"));
		return false;
	}

	const UStreamConfig* Cfg = Config.Get();
	const int32 MaxPerType = Cfg ? FMath::Max(0, Cfg->LearningReminderMaxPerTypePerRun) : 2;
	const float RepeatDelay = Cfg ? FMath::Max(5.0f, Cfg->LearningReminderRepeatDelaySec) : 120.0f;
	int32* Count = nullptr;
	float* NextTime = nullptr;

	switch (ReminderType)
	{
	case ELearningReminderType::Dash:
		Count = &DashReminderCount;
		NextTime = &NextDashReminderTime;
		break;
	case ELearningReminderType::Ability:
		Count = &AbilityReminderCount;
		NextTime = &NextAbilityReminderTime;
		break;
	case ELearningReminderType::ChargedPropExplosion:
		Count = &PropExplosionReminderCount;
		NextTime = &NextPropExplosionReminderTime;
		break;
	default:
		return false;
	}

	if (!Count || !NextTime || *Count >= MaxPerType)
	{
		UE_LOG(LogTemp, Warning, TEXT("[STREAM_DEBUG] Learning reminder blocked by per-run limit (%d/%d)"),
			Count ? *Count : 0, MaxPerType);
		return false;
	}

	EmitLearningReminder(ReminderType);
	++(*Count);
	*NextTime = GetRunElapsedSeconds() + RepeatDelay;
	return true;
}

void UStreamSubsystem::DebugResetLearningReminders()
{
	const float Now = GetRunElapsedSeconds();
	DashReminderCount = 0;
	AbilityReminderCount = 0;
	PropExplosionReminderCount = 0;
	NextDashReminderTime = Now + GetLearningReminderDelay(ELearningReminderType::Dash);
	NextAbilityReminderTime = Now + GetLearningReminderDelay(ELearningReminderType::Ability);
	NextPropExplosionReminderTime = Now + GetLearningReminderDelay(ELearningReminderType::ChargedPropExplosion);
	UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Learning reminder counters and timers reset"));
}

FString UStreamSubsystem::GetLearningReminderDebugStatus() const
{
	const UStreamConfig* Cfg = Config.Get();
	const int32 MaxPerType = Cfg ? FMath::Max(0, Cfg->LearningReminderMaxPerTypePerRun) : 2;
	const float Now = GetRunElapsedSeconds();
	return FString::Printf(
		TEXT("run=%s dash=%d/%d next=%.1fs ability=%d/%d available=%s next=%.1fs prop=%d/%d next=%.1fs"),
		bRunActive ? TEXT("active") : TEXT("inactive"),
		DashReminderCount, MaxPerType, FMath::Max(0.0f, NextDashReminderTime - Now),
		AbilityReminderCount, MaxPerType, bAbilityReminderActive ? TEXT("yes") : TEXT("no"),
		FMath::Max(0.0f, NextAbilityReminderTime - Now),
		PropExplosionReminderCount, MaxPerType, FMath::Max(0.0f, NextPropExplosionReminderTime - Now));
}

// ==================== Run lifecycle handlers ====================

void UStreamSubsystem::HandleRunStarted()
{
	bRunActive = true;
	RunStartTimeSeconds = FPlatformTime::Seconds();
	CurrentViewers = 0;
	ViewerTarget = 0;
	DonationRollAccumulator = 0.0f;
	bCurrentRunMilestoneReached = false;
	PropBindingRefreshAccumulator = 1.0f;
	bAbilityReminderActive = false;
	NextDashReminderTime = GetLearningReminderDelay(ELearningReminderType::Dash);
	NextAbilityReminderTime = GetLearningReminderDelay(ELearningReminderType::Ability);
	NextPropExplosionReminderTime = GetLearningReminderDelay(ELearningReminderType::ChargedPropExplosion);
	DashReminderCount = 0;
	AbilityReminderCount = 0;
	PropExplosionReminderCount = 0;

	EnsureLearningTrackingBindings();
	RefreshPropExplosionBindings();

	if (UStyleComponent* Style = StyleComponent.Get())
	{
		Style->ResetStyleState();
	}

	const bool bWasFirst = IsFirstRun();
	if (ChatBroker)
	{
		ChatBroker->BeginRun(bWasFirst);
	}

	UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Run started (first=%d, completedSoFar=%d)"),
		bWasFirst ? 1 : 0, CompletedRuns);
}

void UStreamSubsystem::HandleRunEnded(ERunEndReason Reason)
{
	bRunActive = false;
	UnbindLearningTracking();
	UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Run ended, reason=%d, final viewers=%d"), (int32)Reason, CurrentViewers);

	if (ChatBroker)
	{
		ChatBroker->EndRun();

		if (Reason == ERunEndReason::PlayerDeath)
		{
			const FGameplayTag DeathTag = FGameplayTag::RequestGameplayTag(FName(TEXT("Chat.Event.PlayerDeath")), false);
			ChatBroker->EmitReaction(DeathTag);
		}
	}
}

void UStreamSubsystem::MarkRunMilestoneReached()
{
	if (bCurrentRunMilestoneReached)
	{
		return;
	}
	bCurrentRunMilestoneReached = true;
	++CompletedRuns;
	UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Run milestone reached. CompletedRuns now %d"), CompletedRuns);
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
			if (FoundArena)
			{
				UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Bound ArenaManager: %s"), *FoundArena->GetName());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[STREAM_DEBUG] HandleArenaEntered: no AArenaManager found in world — antenna events won't reach the chat broker"));
			}
			ChatBroker->BindArenaManager(FoundArena);
		}
	}

	EnsureLearningTrackingBindings();
	RefreshPropExplosionBindings();
}

// ==================== Learning reminders ====================

float UStreamSubsystem::GetLearningReminderDelay(ELearningReminderType ReminderType) const
{
	if (const UStreamConfig* Cfg = Config.Get())
	{
		switch (ReminderType)
		{
		case ELearningReminderType::Dash:
			return FMath::Max(5.0f, Cfg->DashReminderDelaySec);
		case ELearningReminderType::Ability:
			return FMath::Max(5.0f, Cfg->AbilityReminderDelaySec);
		case ELearningReminderType::ChargedPropExplosion:
			return FMath::Max(5.0f, Cfg->ChargedPropExplosionReminderDelaySec);
		default:
			break;
		}
	}

	switch (ReminderType)
	{
	case ELearningReminderType::Dash: return 90.0f;
	case ELearningReminderType::Ability: return 90.0f;
	case ELearningReminderType::ChargedPropExplosion: return 120.0f;
	default: return 120.0f;
	}
}

void UStreamSubsystem::EnsureLearningTrackingBindings()
{
	if (!bRunActive)
	{
		return;
	}

	// Stream learning reminders are about the person at this machine (the streamer), so they track
	// the local character rather than the team.
	APlayerController* LocalPC = CoopPlayers::GetLocalController(GetWorld());
	AShooterCharacter* CurrentCharacter = LocalPC ? Cast<AShooterCharacter>(LocalPC->GetPawn()) : nullptr;
	if (TrackedCharacter.Get() != CurrentCharacter)
	{
		if (UApexMovementComponent* OldMovement = TrackedMovement.Get())
		{
			OldMovement->OnGroundDashStarted.RemoveDynamic(this, &UStreamSubsystem::HandleDashUsed);
			OldMovement->OnAirDashStarted.RemoveDynamic(this, &UStreamSubsystem::HandleDashUsed);
		}
		if (UAbilityComponent* OldAbility = TrackedAbility.Get())
		{
			OldAbility->OnAbilityAdded.RemoveDynamic(this, &UStreamSubsystem::HandleAbilityAdded);
			OldAbility->OnAbilityActivated.RemoveDynamic(this, &UStreamSubsystem::HandleAbilityActivated);
		}

		TrackedCharacter = CurrentCharacter;
		TrackedMovement = CurrentCharacter ? CurrentCharacter->GetApexMovement() : nullptr;
		TrackedAbility = CurrentCharacter ? CurrentCharacter->GetAbilityComponent() : nullptr;

		if (UApexMovementComponent* Movement = TrackedMovement.Get())
		{
			Movement->OnGroundDashStarted.AddUniqueDynamic(this, &UStreamSubsystem::HandleDashUsed);
			Movement->OnAirDashStarted.AddUniqueDynamic(this, &UStreamSubsystem::HandleDashUsed);
		}
		if (UAbilityComponent* Ability = TrackedAbility.Get())
		{
			Ability->OnAbilityAdded.AddUniqueDynamic(this, &UStreamSubsystem::HandleAbilityAdded);
			Ability->OnAbilityActivated.AddUniqueDynamic(this, &UStreamSubsystem::HandleAbilityActivated);
		}
	}

	const bool bHasEquippedAbility = TrackedAbility.IsValid() && TrackedAbility->GetSlotCount() > 0;
	if (bHasEquippedAbility && !bAbilityReminderActive)
	{
		NextAbilityReminderTime = GetRunElapsedSeconds() + GetLearningReminderDelay(ELearningReminderType::Ability);
	}
	bAbilityReminderActive = bHasEquippedAbility;
}

void UStreamSubsystem::UnbindLearningTracking()
{
	if (UApexMovementComponent* Movement = TrackedMovement.Get())
	{
		Movement->OnGroundDashStarted.RemoveDynamic(this, &UStreamSubsystem::HandleDashUsed);
		Movement->OnAirDashStarted.RemoveDynamic(this, &UStreamSubsystem::HandleDashUsed);
	}
	if (UAbilityComponent* Ability = TrackedAbility.Get())
	{
		Ability->OnAbilityAdded.RemoveDynamic(this, &UStreamSubsystem::HandleAbilityAdded);
		Ability->OnAbilityActivated.RemoveDynamic(this, &UStreamSubsystem::HandleAbilityActivated);
	}
	for (const TWeakObjectPtr<AEMFPhysicsProp>& PropPtr : TrackedExplosiveProps)
	{
		if (AEMFPhysicsProp* Prop = PropPtr.Get())
		{
			Prop->OnPropExploded.RemoveDynamic(this, &UStreamSubsystem::HandleChargedPropExploded);
		}
	}

	TrackedCharacter.Reset();
	TrackedMovement.Reset();
	TrackedAbility.Reset();
	TrackedExplosiveProps.Reset();
	bAbilityReminderActive = false;
}

void UStreamSubsystem::RefreshPropExplosionBindings()
{
	UWorld* World = GetWorld();
	if (!bRunActive || !World)
	{
		return;
	}

	TrackedExplosiveProps.RemoveAll([](const TWeakObjectPtr<AEMFPhysicsProp>& Prop)
	{
		return !Prop.IsValid();
	});

	for (TActorIterator<AEMFPhysicsProp> It(World); It; ++It)
	{
		AEMFPhysicsProp* Prop = *It;
		if (!Prop || TrackedExplosiveProps.Contains(Prop))
		{
			continue;
		}

		Prop->OnPropExploded.AddUniqueDynamic(this, &UStreamSubsystem::HandleChargedPropExploded);
		TrackedExplosiveProps.Add(Prop);
	}
}

void UStreamSubsystem::HandleDashUsed()
{
	if (bRunActive)
	{
		NextDashReminderTime = GetRunElapsedSeconds() + GetLearningReminderDelay(ELearningReminderType::Dash);
	}
}

void UStreamSubsystem::HandleAbilityAdded(int32 SlotIndex)
{
	if (bRunActive)
	{
		bAbilityReminderActive = true;
		NextAbilityReminderTime = GetRunElapsedSeconds() + GetLearningReminderDelay(ELearningReminderType::Ability);
	}
}

void UStreamSubsystem::HandleAbilityActivated(UAbilityDefinition* Definition)
{
	if (bRunActive)
	{
		NextAbilityReminderTime = GetRunElapsedSeconds() + GetLearningReminderDelay(ELearningReminderType::Ability);
	}
}

void UStreamSubsystem::HandleChargedPropExploded(AEMFPhysicsProp* Prop, FVector Location, float DamageMultiplier)
{
	if (bRunActive)
	{
		NextPropExplosionReminderTime = GetRunElapsedSeconds()
			+ GetLearningReminderDelay(ELearningReminderType::ChargedPropExplosion);
	}
}

void UStreamSubsystem::TickLearningReminders()
{
	const UStreamConfig* Cfg = Config.Get();
	if (!bRunActive || (Cfg && !Cfg->bEnableLearningReminders))
	{
		return;
	}

	const float Now = GetRunElapsedSeconds();
	const float RepeatDelay = Cfg ? FMath::Max(5.0f, Cfg->LearningReminderRepeatDelaySec) : 120.0f;
	const int32 MaxPerType = Cfg ? FMath::Max(0, Cfg->LearningReminderMaxPerTypePerRun) : 2;

	if (DashReminderCount < MaxPerType && Now >= NextDashReminderTime)
	{
		EmitLearningReminder(ELearningReminderType::Dash);
		++DashReminderCount;
		NextDashReminderTime = Now + RepeatDelay;
	}
	if (bAbilityReminderActive && AbilityReminderCount < MaxPerType && Now >= NextAbilityReminderTime)
	{
		EmitLearningReminder(ELearningReminderType::Ability);
		++AbilityReminderCount;
		NextAbilityReminderTime = Now + RepeatDelay;
	}
	if (PropExplosionReminderCount < MaxPerType && Now >= NextPropExplosionReminderTime)
	{
		EmitLearningReminder(ELearningReminderType::ChargedPropExplosion);
		++PropExplosionReminderCount;
		NextPropExplosionReminderTime = Now + RepeatDelay;
	}
}

void UStreamSubsystem::EmitLearningReminder(ELearningReminderType ReminderType)
{
	OnLearningReminderRequested.Broadcast(ReminderType);

	FName EventTagName = NAME_None;
	switch (ReminderType)
	{
	case ELearningReminderType::Dash:
		EventTagName = TEXT("Chat.Event.DashReminder");
		OnDashReminderRequested.Broadcast();
		break;
	case ELearningReminderType::Ability:
		EventTagName = TEXT("Chat.Event.AbilityReminder");
		OnAbilityReminderRequested.Broadcast();
		break;
	case ELearningReminderType::ChargedPropExplosion:
		EventTagName = TEXT("Chat.Event.ChargedPropExplosionReminder");
		break;
	default:
		return;
	}

	const FGameplayTag EventTag = FGameplayTag::RequestGameplayTag(EventTagName, false);
	if (ChatBroker && EventTag.IsValid())
	{
		ChatBroker->EmitReaction(EventTag);
	}

	UE_LOG(LogTemp, Log, TEXT("[STREAM_DEBUG] Learning reminder requested: %s"), *EventTagName.ToString());
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
