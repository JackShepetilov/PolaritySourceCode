// RunSubsystem.cpp

#include "RunSubsystem.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Polarity/Upgrades/UpgradeManagerComponent.h"
#include "Polarity/Upgrades/UpgradeDefinition.h"
#include "Save/SaveGameSubsystem.h"
#include "Generation/BiomeRunRegistry.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "MoviePlayer.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/Texture2D.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"

namespace
{
	/** Slate continues ticking inside MoviePlayer even while the game engine is paused. */
	class SRunLoadingSpinner final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SRunLoadingSpinner) {}
			SLATE_ARGUMENT(UTexture2D*, Texture)
			SLATE_ARGUMENT(FVector2D, Size)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			SpinnerBrush = MakeShared<FSlateDynamicImageBrush>(InArgs._Texture, InArgs._Size, TEXT("RunLoadingSpinner"));
			ChildSlot
			[
				SAssignNew(SpinnerImage, SImage)
				.Image(SpinnerBrush.Get())
				.RenderTransformPivot(FVector2D(0.5f, 0.5f))
			];
			SetCanTick(true);
		}

		virtual void Tick(const FGeometry& AllottedGeometry, const double CurrentTime, const float DeltaTime) override
		{
			SCompoundWidget::Tick(AllottedGeometry, CurrentTime, DeltaTime);
			if (SpinnerImage.IsValid())
			{
				const float AngleRadians = FMath::Fmod(static_cast<float>(CurrentTime) * 4.f, 2.f * PI);
				SpinnerImage->SetRenderTransform(FSlateRenderTransform(FQuat2D(AngleRadians)));
			}
		}

	private:
		TSharedPtr<FSlateDynamicImageBrush> SpinnerBrush;
		TSharedPtr<SImage> SpinnerImage;
	};
}

namespace
{
	USaveGameSubsystem* GetSaveSubsystem(const URunSubsystem* Self)
	{
		if (UGameInstance* GI = Self ? Self->GetGameInstance() : nullptr)
		{
			return GI->GetSubsystem<USaveGameSubsystem>();
		}
		return nullptr;
	}
}

void URunSubsystem::StartRun()
{
	UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] StartRun (was %d)"), (int32)RunState);

	RunState = ERunState::Active;
	CurrentArenaIndex = -1;
	Stats = FRunStats();
	ActivatedAntennaCount = 0;
	AcquiredUpgrades.Reset();
	ClearedArenaIndices.Reset();
	if (RunSeed == 0)
	{
		GetOrCreateRunSeed();
	}
	bGenerationPreparedForPendingRun = false;

	if (UWorld* World = GetWorld())
	{
		RunStartTimeSeconds = World->GetTimeSeconds();
	}

	// Reset any "3/5"-style UI back to zero for the new run.
	OnAntennaCountChanged.Broadcast(ActivatedAntennaCount);

	OnRunStarted.Broadcast();

	// New run supersedes any stale mid-run resume; it re-checkpoints on the first EnterArena.
	if (USaveGameSubsystem* Save = GetSaveSubsystem(this))
	{
		Save->ClearRun();
	}
}

int32 URunSubsystem::GetOrCreateRunSeed()
{
	if (RunState == ERunState::Active && RunSeed != 0)
	{
		return RunSeed;
	}
	if (!bGenerationPreparedForPendingRun || RunSeed == 0)
	{
		RunSeed = static_cast<int32>(GetTypeHash(FGuid::NewGuid()));
		if (RunSeed == 0) RunSeed = 1;
		AssembledBiomeId = NAME_None;
		AssembledLayoutId = NAME_None;
		AssembledArenaIds.Reset();
		bGenerationPreparedForPendingRun = true;
		UE_LOG(LogTemp, Log, TEXT("[BIOME_RUN] Prepared new run seed=%d"), RunSeed);
	}
	return RunSeed;
}

bool URunSubsystem::OpenNewRunFromBiome(UBiomeRunRegistry* BiomeRegistry,
	TSubclassOf<UUserWidget> LoadingScreenClass, UTexture2D* LoadingSpinnerTexture)
{
	if (!BiomeRegistry)
	{
		UE_LOG(LogTemp, Error, TEXT("[RUN_FLOW] Cannot start run: biome registry is missing"));
		return false;
	}
	if (IsRunActive()) EndRun(ERunEndReason::Aborted);

	const int32 Seed = GetOrCreateRunSeed();
	FRandomStream Random(HashCombineFast(GetTypeHash(Seed), GetTypeHash(BiomeRegistry->GetPathName())));
	float TotalWeight = 0.f;
	for (const FBiomeLayoutOption& Layout : BiomeRegistry->Layouts)
	{
		if (!Layout.LayoutLevel.IsNull()) TotalWeight += FMath::Max(0.f, Layout.Weight);
	}
	if (TotalWeight <= 0.f)
	{
		UE_LOG(LogTemp, Error, TEXT("[RUN_FLOW] Biome registry %s has no enabled layouts"), *BiomeRegistry->GetPathName());
		return false;
	}

	const float Roll = Random.FRandRange(0.f, TotalWeight);
	float Accumulated = 0.f;
	for (const FBiomeLayoutOption& Layout : BiomeRegistry->Layouts)
	{
		if (Layout.LayoutLevel.IsNull()) continue;
		Accumulated += FMath::Max(0.f, Layout.Weight);
		if (Roll <= Accumulated)
		{
			RunLoadingScreenClass = LoadingScreenClass;
			RunLoadingSpinnerTexture = LoadingSpinnerTexture;
			if (LoadingScreenClass)
			{
				RunLoadingScreenWidget = GetGameInstance()
					? CreateWidget<UUserWidget>(GetGameInstance(), LoadingScreenClass)
					: nullptr;
				if (RunLoadingScreenWidget && IsMoviePlayerEnabled())
				{
					FLoadingScreenAttributes LoadingAttributes;
					// MoviePlayer owns only the blocking OpenLevel period. Holding it manually
					// would freeze GameMode, which then cannot ever reach RunLaunchPoint to stop it.
					LoadingAttributes.bAutoCompleteWhenLoadingCompletes = true;
					LoadingAttributes.bWaitForManualStop = false;
					// Never tick the game engine under MoviePlayer: in standalone with ray tracing
					// it can tick the render-thread geometry manager twice in one frame.
					// UMG animations are therefore intentionally not used for this cross-map screen.
					LoadingAttributes.bAllowEngineTick = false;
					LoadingAttributes.MinimumLoadingScreenDisplayTime = 0.f;
					TSharedRef<SWidget> LoadingScreenContent = RunLoadingScreenWidget->TakeWidget();
					if (RunLoadingSpinnerTexture)
					{
						LoadingScreenContent = SNew(SOverlay)
							+ SOverlay::Slot()[LoadingScreenContent]
							+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(FMargin(48.f))
							[
								SNew(SRunLoadingSpinner)
								.Texture(RunLoadingSpinnerTexture)
								.Size(FVector2D(96.f, 96.f))
							];
					}
					LoadingAttributes.WidgetLoadingScreen = LoadingScreenContent;
					GetMoviePlayer()->SetupLoadingScreen(LoadingAttributes);
					UE_LOG(LogTemp, Log, TEXT("[RUN_FLOW] Cross-map loading screen armed; GameMode cover takes over after map load"));
				}
				else if (RunLoadingScreenWidget)
				{
					// MoviePlayer is deliberately disabled in PIE. A blocking OpenLevel does not render
					// intermediate frames, so cover the old viewport before travel and re-attach the
					// same GameInstance-owned widget in PostLoadMap before the new world renders.
					bUsingViewportLoadingScreenFallback = true;
					RunLoadingScreenWidget->AddToViewport(10000);
					if (!PostLoadMapLoadingScreenHandle.IsValid())
					{
						PostLoadMapLoadingScreenHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
							this, &URunSubsystem::HandlePostLoadMapForRunLoadingScreen);
					}
					UE_LOG(LogTemp, Log, TEXT("[RUN_FLOW] PIE viewport loading-screen fallback armed"));
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[RUN_FLOW] Loading screen unavailable (widget=%s moviePlayer=%d)"),
						*GetNameSafe(RunLoadingScreenWidget), IsMoviePlayerEnabled() ? 1 : 0);
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[RUN_FLOW] No RunLoadingScreenClass assigned in WBP_MainMenu"));
			}

			UE_LOG(LogTemp, Log, TEXT("[RUN_FLOW] seed=%d biome=%s layout=%s"), Seed,
				*BiomeRegistry->GetPathName(), *Layout.LayoutLevel.ToSoftObjectPath().ToString());
			UGameplayStatics::OpenLevelBySoftObjectPtr(this, Layout.LayoutLevel);
			return true;
		}
	}

	UE_LOG(LogTemp, Error, TEXT("[RUN_FLOW] Weighted layout selection failed for %s"), *BiomeRegistry->GetPathName());
	return false;
}

void URunSubsystem::DismissRunLoadingScreen()
{
	// MoviePlayer auto-completes with map loading; this only releases retained UI objects.
	if (RunLoadingScreenWidget)
	{
		RunLoadingScreenWidget->RemoveFromParent();
	}
	if (PostLoadMapLoadingScreenHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapLoadingScreenHandle);
		PostLoadMapLoadingScreenHandle.Reset();
	}
	bUsingViewportLoadingScreenFallback = false;
	RunLoadingScreenWidget = nullptr;
	RunLoadingSpinnerTexture = nullptr;
	RunLoadingScreenClass = nullptr;
}

void URunSubsystem::HandlePostLoadMapForRunLoadingScreen(UWorld* /*LoadedWorld*/)
{
	if (bUsingViewportLoadingScreenFallback && RunLoadingScreenWidget
		&& !RunLoadingScreenWidget->IsInViewport())
	{
		RunLoadingScreenWidget->AddToViewport(10000);
		UE_LOG(LogTemp, Log, TEXT("[RUN_FLOW] PIE viewport loading screen re-attached after map load"));
	}
}

bool URunSubsystem::GetSavedBiomeAssembly(FName BiomeId, FName LayoutId, TArray<FName>& OutArenaIds) const
{
	if (RunSeed != 0 && AssembledBiomeId == BiomeId && AssembledLayoutId == LayoutId && !AssembledArenaIds.IsEmpty())
	{
		OutArenaIds = AssembledArenaIds;
		return true;
	}
	return false;
}

void URunSubsystem::CommitBiomeAssembly(int32 InSeed, FName BiomeId, FName LayoutId, const TArray<FName>& ArenaIds)
{
	RunSeed = InSeed;
	AssembledBiomeId = BiomeId;
	AssembledLayoutId = LayoutId;
	AssembledArenaIds = ArenaIds;
}

void URunSubsystem::EndRun(ERunEndReason Reason)
{
	if (RunState == ERunState::Ended || RunState == ERunState::None)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		Stats.RunDuration = static_cast<float>(World->GetTimeSeconds() - RunStartTimeSeconds);
	}

	RunState = ERunState::Ended;

	UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] EndRun reason=%d duration=%.1fs xp=%d levels=%d"),
		(int32)Reason, Stats.RunDuration, Stats.TotalXPEarned, Stats.LevelsGained);

	OnRunEnded.Broadcast(Reason);

	// Persist banked meta (Stream's HandleRunEnded ran synchronously during the broadcast above),
	// then either keep the run save for resume (quit) or delete it (death / victory / abort).
	if (USaveGameSubsystem* Save = GetSaveSubsystem(this))
	{
		Save->SaveMetaNow();
		if (Reason == ERunEndReason::QuitToMenu)
		{
			Save->SaveRun();
		}
		else
		{
			Save->ClearRun();
		}
	}
}

void URunSubsystem::EnterArena(int32 ArenaIndex)
{
	if (RunState != ERunState::Active)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RUN_DEBUG] EnterArena %d called while RunState=%d (ignored)"),
			ArenaIndex, (int32)RunState);
		return;
	}
	CurrentArenaIndex = ArenaIndex;
	UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] EnterArena %d"), ArenaIndex);
	OnArenaEntered.Broadcast(ArenaIndex);

	// Checkpoint the run so a quit-to-menu from this arena can resume here.
	if (USaveGameSubsystem* Save = GetSaveSubsystem(this))
	{
		Save->SaveRun();
	}
}

void URunSubsystem::ClearArena(int32 ArenaIndex)
{
	if (ArenaIndex < 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RUN_DEBUG] ClearArena ignored invalid index %d"), ArenaIndex);
		return;
	}
	if (ClearedArenaIndices.Contains(ArenaIndex))
	{
		UE_LOG(LogTemp, Verbose, TEXT("[RUN_DEBUG] ClearArena %d already recorded"), ArenaIndex);
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] ClearArena %d recorded"), ArenaIndex);
	ClearedArenaIndices.Add(ArenaIndex);
	OnArenaCleared.Broadcast(ArenaIndex);
	if (USaveGameSubsystem* Save = GetSaveSubsystem(this))
	{
		Save->SaveRun();
	}
}

void URunSubsystem::RegisterAntennaActivated()
{
	++ActivatedAntennaCount;
	UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] RegisterAntennaActivated — count now %d"), ActivatedAntennaCount);
	OnAntennaCountChanged.Broadcast(ActivatedAntennaCount);
}

void URunSubsystem::AddXPEarnedToStats(int32 Amount)
{
	Stats.TotalXPEarned += Amount;
}

void URunSubsystem::AddLevelGainedToStats()
{
	++Stats.LevelsGained;
}

void URunSubsystem::RegisterKillInStats(TSubclassOf<AShooterNPC> EnemyClass)
{
	if (!EnemyClass) return;
	int32& Count = Stats.KillsByEnemy.FindOrAdd(EnemyClass);
	++Count;
}

void URunSubsystem::BindUpgradeManager(UUpgradeManagerComponent* Manager, const UUpgradeRegistry* Registry)
{
	if (!Manager)
	{
		return;
	}

	// 1) Re-apply the persisted ledger onto this fresh character FIRST — before subscribing.
	//    If we subscribed first, GrantUpgrade's broadcasts would re-enter our handlers and
	//    mutate AcquiredUpgrades while RestoreUpgrades iterates it. Only mid-run; outside a run
	//    the ledger may be stale until the next StartRun, so we don't reapply it.
	if (IsRunActive() && Registry)
	{
		UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] BindUpgradeManager: restoring %d upgrade(s) onto new character"),
			AcquiredUpgrades.Num());
		Manager->RestoreUpgrades(AcquiredUpgrades, Registry);
	}

	// 2) Subscribe so future grants/level-ups/removals keep the ledger current.
	//    AddUnique → safe to call once per character spawned during the run.
	Manager->OnUpgradeGranted.AddUniqueDynamic(this, &URunSubsystem::HandleUpgradeGranted);
	Manager->OnUpgradeLeveledUp.AddUniqueDynamic(this, &URunSubsystem::HandleUpgradeLeveledUp);
	Manager->OnUpgradeRemoved.AddUniqueDynamic(this, &URunSubsystem::HandleUpgradeRemoved);
}

void URunSubsystem::HandleUpgradeGranted(UUpgradeDefinition* Definition)
{
	if (Definition && Definition->UpgradeTag.IsValid())
	{
		AcquiredUpgrades.Add(Definition->UpgradeTag, 1);
	}
}

void URunSubsystem::HandleUpgradeLeveledUp(UUpgradeDefinition* Definition, int32 NewLevel)
{
	if (Definition && Definition->UpgradeTag.IsValid())
	{
		AcquiredUpgrades.Add(Definition->UpgradeTag, NewLevel);
	}
}

void URunSubsystem::HandleUpgradeRemoved(UUpgradeDefinition* Definition)
{
	if (Definition && Definition->UpgradeTag.IsValid())
	{
		AcquiredUpgrades.Remove(Definition->UpgradeTag);
	}
}

void URunSubsystem::RestoreFromSave(ERunState InState, int32 InArenaIndex, int32 InActivatedAntennas,
	const TMap<FGameplayTag, int32>& InUpgrades, const FRunStats& InStats,
	int32 InRunSeed, FName InBiomeId, FName InLayoutId, const TArray<FName>& InArenaIds, const TArray<int32>& InClearedArenaIndices)
{
	RunState              = InState;
	CurrentArenaIndex     = InArenaIndex;
	ActivatedAntennaCount = InActivatedAntennas;
	AcquiredUpgrades      = InUpgrades;
	Stats                 = InStats;
	RunSeed               = InRunSeed;
	AssembledBiomeId      = InBiomeId;
	AssembledLayoutId     = InLayoutId;
	AssembledArenaIds     = InArenaIds;
	ClearedArenaIndices.Reset();
	for (const int32 ClearedArenaIndex : InClearedArenaIndices)
	{
		ClearedArenaIndices.Add(ClearedArenaIndex);
	}
	bGenerationPreparedForPendingRun = false;

	// Preserve elapsed run time across the resume (RunDuration was captured at save time).
	if (UWorld* World = GetWorld())
	{
		RunStartTimeSeconds = World->GetTimeSeconds() - Stats.RunDuration;
	}

	UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] RestoreFromSave state=%d arena=%d antennas=%d upgrades=%d"),
		(int32)RunState, CurrentArenaIndex, ActivatedAntennaCount, AcquiredUpgrades.Num());

	// Resync UI once (don't replay N antenna increments).
	OnAntennaCountChanged.Broadcast(ActivatedAntennaCount);
}
