#include "BiomeRunAssembler.h"
#include "Arena/ArenaManager.h"
#include "IslandBridgeActor.h"
#include "ArenaGrassExclusionVolume.h"
#include "BiomeArenaAnchor.h"
#include "BiomeRunRegistry.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LandscapeProxy.h"
#include "NavigationSystem.h"
#include "RunSubsystem.h"
#include "TimerManager.h"

namespace
{
	FName GetLevelId(const TSoftObjectPtr<UWorld>& Level)
	{
		return FName(*Level.ToSoftObjectPath().GetLongPackageName());
	}

	const FBiomeArenaOption* ChooseWeightedArena(const FBiomeArenaPool& Pool, FRandomStream& Random, int32& OutIndex)
	{
		float TotalWeight = 0.f;
		for (const FBiomeArenaOption& Option : Pool.Arenas)
		{
			if (!Option.ArenaLevel.IsNull())
			{
				TotalWeight += FMath::Max(0.f, Option.Weight);
			}
		}
		if (TotalWeight <= 0.f) return nullptr;

		const float Roll = Random.FRandRange(0.f, TotalWeight);
		float Accumulated = 0.f;
		for (int32 Index = 0; Index < Pool.Arenas.Num(); ++Index)
		{
			const FBiomeArenaOption& Option = Pool.Arenas[Index];
			if (Option.ArenaLevel.IsNull()) continue;
			Accumulated += FMath::Max(0.f, Option.Weight);
			if (Roll <= Accumulated)
			{
				OutIndex = Index;
				return &Option;
			}
		}
		return nullptr;
	}
}

ABiomeRunAssembler::ABiomeRunAssembler()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ABiomeRunAssembler::BeginPlay()
{
	Super::BeginPlay();
	Assemble();
}

void ABiomeRunAssembler::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(AssemblyPollTimer);
	for (UBiomeGrassExclusionHandle* Handle : GrassExclusionHandles)
	{
		if (Handle) ALandscapeProxy::RemoveExclusionBox(FWeakObjectPtr(Handle));
	}
	GrassExclusionHandles.Reset();
	Super::EndPlay(EndPlayReason);
}

FName ABiomeRunAssembler::ResolveBiomeId() const
{
	return Registry ? FName(*Registry->GetPathName()) : NAME_None;
}

FName ABiomeRunAssembler::ResolveLayoutId() const
{
	return GetWorld() ? FName(*GetWorld()->GetOutermost()->GetName()) : NAME_None;
}

void ABiomeRunAssembler::Assemble()
{
	if (!Registry)
	{
		FinishAssembly(false, TEXT("Registry is missing"));
		return;
	}

	TMap<FName, ABiomeArenaAnchor*> AnchorsBySlot;
	for (TActorIterator<ABiomeArenaAnchor> It(GetWorld()); It; ++It)
	{
		ABiomeArenaAnchor* Anchor = *It;
		if (!Anchor || Anchor->SlotId.IsNone()) continue;
		if (AnchorsBySlot.Contains(Anchor->SlotId))
		{
			FinishAssembly(false, FString::Printf(TEXT("Duplicate anchor SlotId '%s'"), *Anchor->SlotId.ToString()));
			return;
		}
		AnchorsBySlot.Add(Anchor->SlotId, Anchor);
	}

	URunSubsystem* Run = GetGameInstance() ? GetGameInstance()->GetSubsystem<URunSubsystem>() : nullptr;
	ResolvedSeed = SeedOverride != 0 ? SeedOverride : (Run ? Run->GetOrCreateRunSeed() : FMath::Rand());
	const FName ResolvedBiomeId = ResolveBiomeId();
	const FName ResolvedLayoutId = ResolveLayoutId();
	TArray<FName> SavedArenaIds;
	const bool bUseSavedSelection = Run && Run->GetSavedBiomeAssembly(
		ResolvedBiomeId, ResolvedLayoutId, SavedArenaIds) && SavedArenaIds.Num() == Registry->ArenaPools.Num();
	FRandomStream Random(HashCombineFast(GetTypeHash(ResolvedSeed), GetTypeHash(ResolvedBiomeId)));

	SelectedArenaIds.Reset();
	SelectedOptionIndices.Reset();
	OrderedAnchors.Reset();
	for (int32 PoolIndex = 0; PoolIndex < Registry->ArenaPools.Num(); ++PoolIndex)
	{
		const FBiomeArenaPool& Pool = Registry->ArenaPools[PoolIndex];
		ABiomeArenaAnchor* const* AnchorPtr = AnchorsBySlot.Find(Pool.SlotId);
		if (!AnchorPtr || !*AnchorPtr)
		{
			FinishAssembly(false, FString::Printf(TEXT("No anchor for slot '%s'"), *Pool.SlotId.ToString()));
			return;
		}

		int32 OptionIndex = INDEX_NONE;
		const FBiomeArenaOption* Chosen = nullptr;
		if (bUseSavedSelection)
		{
			OptionIndex = Pool.Arenas.IndexOfByPredicate([&SavedArenaIds, PoolIndex](const FBiomeArenaOption& Option)
			{
				return GetLevelId(Option.ArenaLevel) == SavedArenaIds[PoolIndex];
			});
			Chosen = Pool.Arenas.IsValidIndex(OptionIndex) ? &Pool.Arenas[OptionIndex] : nullptr;
		}
		if (!Chosen) Chosen = ChooseWeightedArena(Pool, Random, OptionIndex);
		if (!Chosen)
		{
			FinishAssembly(false, FString::Printf(TEXT("Slot '%s' has no enabled arena"), *Pool.SlotId.ToString()));
			return;
		}

		bool bLoaded = false;
		const FString InstanceName = FString::Printf(TEXT("Run_%s_%s_%d"),
			*Registry->GetName(), *Pool.SlotId.ToString(), ResolvedSeed);
		ULevelStreamingDynamic* Streaming = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(
			this, Chosen->ArenaLevel, (*AnchorPtr)->GetActorTransform(), bLoaded, InstanceName);
		if (!bLoaded || !Streaming)
		{
			FinishAssembly(false, FString::Printf(TEXT("Failed to stream arena '%s'"),
				*Chosen->ArenaLevel.ToSoftObjectPath().ToString()));
			return;
		}

		SpawnedArenaLevels.Add(Streaming);
		OrderedAnchors.Add(*AnchorPtr);
		const FName ChosenArenaId = GetLevelId(Chosen->ArenaLevel);
		SelectedArenaIds.Add(ChosenArenaId);
		SelectedOptionIndices.Add(OptionIndex);
		UE_LOG(LogTemp, Log, TEXT("[BIOME_RUN] slot=%s arena=%s level=%s"),
			*Pool.SlotId.ToString(), *ChosenArenaId.ToString(), *Chosen->ArenaLevel.ToSoftObjectPath().ToString());
	}

	if (Run)
	{
		Run->CommitBiomeAssembly(ResolvedSeed, ResolvedBiomeId, ResolvedLayoutId, SelectedArenaIds);
	}
	GetWorldTimerManager().SetTimer(AssemblyPollTimer, this, &ABiomeRunAssembler::PollArenaStreaming, 0.05f, true);
}

void ABiomeRunAssembler::PollArenaStreaming()
{
	for (const ULevelStreamingDynamic* Streaming : SpawnedArenaLevels)
	{
		if (!Streaming || !Streaming->IsLevelLoaded() || !Streaming->IsLevelVisible()) return;
	}
	GetWorldTimerManager().ClearTimer(AssemblyPollTimer);
	BindBridgeProgression();
	RegisterGrassExclusions();

	if (bRebuildNavigation)
	{
		if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		{
			NavigationWaitStartedAt = FPlatformTime::Seconds();
			Nav->Build();
			GetWorldTimerManager().SetTimer(AssemblyPollTimer, this, &ABiomeRunAssembler::PollNavigation, 0.1f, true);
			return;
		}
	}
	FinishAssembly(true);
}


void ABiomeRunAssembler::BindBridgeProgression()
{
	int32 BoundBridgeCount = 0;
	for (TActorIterator<AIslandBridgeActor> It(GetWorld()); It; ++It)
	{
		AIslandBridgeActor* Bridge = *It;
		if (!Bridge || Bridge->GetLevel() != GetWorld()->PersistentLevel)
		{
			continue;
		}
		if (!Bridge->bStartsLocked)
		{
			Bridge->SetBridgeLocked(false);
			continue;
		}
		ABiomeArenaAnchor* Anchor = Bridge->UnlockAfterAnchor;
		const int32 PoolIndex = OrderedAnchors.IndexOfByKey(Anchor);
		if (!Anchor || PoolIndex == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning, TEXT("[BIOME_BRIDGE] locked bridge=%s has no valid UnlockAfterAnchor"), *Bridge->GetActorNameOrLabel());
			continue;
		}
		AArenaManager* ArenaManager = nullptr;
		if (SpawnedArenaLevels.IsValidIndex(PoolIndex))
		{
			if (const ULevel* LoadedLevel = SpawnedArenaLevels[PoolIndex]->GetLoadedLevel())
			{
				for (AActor* Actor : LoadedLevel->Actors)
				{
					ArenaManager = Cast<AArenaManager>(Actor);
					if (ArenaManager)
					{
						break;
					}
				}
			}
		}
		if (!ArenaManager)
		{
			UE_LOG(LogTemp, Warning, TEXT("[BIOME_BRIDGE] no ArenaManager for bridge=%s pool=%d"), *Bridge->GetActorNameOrLabel(), PoolIndex);
			continue;
		}
		Bridge->BindUnlockSource(ArenaManager, PoolIndex);
		++BoundBridgeCount;
	}
	UE_LOG(LogTemp, Log, TEXT("[BIOME_BRIDGE] bound %d progression bridge(s)"), BoundBridgeCount);
}
void ABiomeRunAssembler::RegisterGrassExclusions()
{
	for (int32 PoolIndex = 0; PoolIndex < Registry->ArenaPools.Num(); ++PoolIndex)
	{
		const FBiomeArenaPool& Pool = Registry->ArenaPools[PoolIndex];
		if (!SelectedOptionIndices.IsValidIndex(PoolIndex) || !OrderedAnchors.IsValidIndex(PoolIndex)
			|| !Pool.Arenas.IsValidIndex(SelectedOptionIndices[PoolIndex])) continue;
		const FBiomeArenaOption& Option = Pool.Arenas[SelectedOptionIndices[PoolIndex]];
		int32 AuthoredVolumeCount = 0;
		if (SpawnedArenaLevels.IsValidIndex(PoolIndex))
		{
			if (const ULevel* LoadedLevel = SpawnedArenaLevels[PoolIndex]->GetLoadedLevel())
			{
				for (AActor* Actor : LoadedLevel->Actors)
				{
					const AArenaGrassExclusionVolume* Volume = Cast<AArenaGrassExclusionVolume>(Actor);
					if (!Volume) continue;
					const FBox WorldBox = Volume->GetWorldExclusionBox();
					if (!WorldBox.IsValid) continue;
					UBiomeGrassExclusionHandle* Handle = NewObject<UBiomeGrassExclusionHandle>(this);
					GrassExclusionHandles.Add(Handle);
					ALandscapeProxy::AddExclusionBox(FWeakObjectPtr(Handle), WorldBox);
					++AuthoredVolumeCount;
					UE_LOG(LogTemp, Log, TEXT("[BIOME_RUN] viewport grass volume slot=%s actor=%s box=%s"),
						*Pool.SlotId.ToString(), *Volume->GetActorNameOrLabel(), *WorldBox.ToString());
				}
			}
		}

		if (AuthoredVolumeCount == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[BIOME_RUN] slot=%s arena has no grass exclusion volumes"),
				*Pool.SlotId.ToString());
		}
	}
}

void ABiomeRunAssembler::PollNavigation()
{
	if (!UNavigationSystemV1::IsNavigationBeingBuilt(GetWorld()))
	{
		GetWorldTimerManager().ClearTimer(AssemblyPollTimer);
		FinishAssembly(true);
		return;
	}
	if (FPlatformTime::Seconds() - NavigationWaitStartedAt >= NavigationWaitTimeout)
	{
		GetWorldTimerManager().ClearTimer(AssemblyPollTimer);
		UE_LOG(LogTemp, Warning, TEXT("[BIOME_RUN] Navigation build exceeded %.1fs; continuing"), NavigationWaitTimeout);
		FinishAssembly(true);
	}
}

void ABiomeRunAssembler::FinishAssembly(bool bSuccess, const FString& Reason)
{
	GetWorldTimerManager().ClearTimer(AssemblyPollTimer);
	bAssemblyComplete = true;
	bAssemblySucceeded = bSuccess;
	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("[BIOME_RUN] READY biome=%s seed=%d arenas=%s"),
			*ResolveBiomeId().ToString(), ResolvedSeed,
			*FString::JoinBy(SelectedArenaIds, TEXT(","), [](FName Id) { return Id.ToString(); }));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[BIOME_RUN] FAILED: %s"), *Reason);
	}
}
