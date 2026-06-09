// SaveGameSubsystem.cpp
// Logging tag: [SAVE_DEBUG].

#include "SaveGameSubsystem.h"

#include "PolarityMetaSave.h"
#include "PolarityRunSave.h"

#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/CoreDelegates.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"

// Project subsystems we read/restore:
#include "RunSubsystem.h"
#include "XPSubsystem.h"
#include "StreamSubsystem.h"
#include "LoreSubsystem.h"
#include "ShooterNPC.h"

// ============================================================================
// Slot names + paths (PC/desktop file-based; swap to ISaveGameSystem for console)
// ============================================================================
FString USaveGameSubsystem::MetaSlotName()
{
#if WITH_EDITOR
	return TEXT("Polarity_Meta_PIE");
#else
	return TEXT("Polarity_Meta");
#endif
}

FString USaveGameSubsystem::RunSlotName()
{
#if WITH_EDITOR
	return TEXT("Polarity_Run_PIE");
#else
	return TEXT("Polarity_Run");
#endif
}

FString USaveGameSubsystem::SlotFilePath(const FString& Slot)
{
	return FPaths::ProjectSavedDir() / TEXT("SaveGames") / (Slot + TEXT(".sav"));
}

// ============================================================================
// Lifecycle
// ============================================================================
void USaveGameSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Chat grouping is content (FChatMessage::Group); the unread badge uses per-group sizes the
	// desktop/chat widget registers at runtime via RegisterChatGroupSizes(). No row indices here.

	LoadMetaSynchronous();

	// Cache run-slot presence + resume arena once, so the menu never re-reads disk per frame.
	if (USaveGame* RunLoaded = LoadSaveFromDisk(RunSlotName()))
	{
		if (const UPolarityRunSave* RS = Cast<UPolarityRunSave>(RunLoaded))
		{
			bRunSaveExists = true;
			CachedResumeArenaIndex = RS->CurrentArenaIndex;
		}
	}

	BindAppLifecycleDelegates();
	OnSaveLoaded.Broadcast();
}

void USaveGameSubsystem::Deinitialize()
{
	if (FlushTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(FlushTickerHandle);
		FlushTickerHandle.Reset();
	}
	if (DeactivateHandle.IsValid())
	{
		FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(DeactivateHandle);
	}
	if (TerminateHandle.IsValid())
	{
		FCoreDelegates::GetApplicationWillTerminateDelegate().Remove(TerminateHandle);
	}

	FlushMetaIfDirty(); // backstop only; the reliable PC path is the menu Quit button calling SaveMetaNow.
	Super::Deinitialize();
}

void USaveGameSubsystem::BindAppLifecycleDelegates()
{
	// Backstops. On desktop, ApplicationWillDeactivate fires on focus loss (not a clean quit), and
	// the terminate delegate is unreliable — the menu Quit button MUST call SaveMetaNow() explicitly.
	DeactivateHandle = FCoreDelegates::ApplicationWillDeactivateDelegate.AddWeakLambda(this, [this]()
	{
		if (bMetaDirty) { SaveMetaNow(); }
	});
	TerminateHandle = FCoreDelegates::GetApplicationWillTerminateDelegate().AddWeakLambda(this, [this]()
	{
		if (bMetaDirty) { SaveMetaNow(); }
	});
}

// ============================================================================
// Meta load
// ============================================================================
UPolarityMetaSave* USaveGameSubsystem::GetMeta() const
{
	return Meta;
}

void USaveGameSubsystem::InitializeFreshMeta()
{
	if (!Meta) { return; }
	// intro is unlocked from the start and plays in real time on the first visit.
	Meta->Chat.UnlockedGroups.Add(FName(TEXT("intro")));
	// Desktop starts with chat + settings; "raid" appears only after NotifyTutorialCompleted.
	Meta->UnlockedApps.Add(FName(TEXT("chat")));
	Meta->UnlockedApps.Add(FName(TEXT("settings")));
	// PlayerStreamerName intentionally left empty -> desktop prompts for a handle on first launch.
}

void USaveGameSubsystem::LoadMetaSynchronous()
{
	if (USaveGame* Loaded = LoadSaveFromDisk(MetaSlotName()))
	{
		if (UPolarityMetaSave* M = Cast<UPolarityMetaSave>(Loaded))
		{
			if (M->SaveVersion > PolaritySaveVersion::MetaLatest)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[SAVE_DEBUG] Meta save v%d is newer than supported v%d -> ignoring (fresh start)."),
					M->SaveVersion, PolaritySaveVersion::MetaLatest);
			}
			else if (MigrateMeta(*M))
			{
				Meta = M;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[SAVE_DEBUG] Meta migration failed -> fresh start."));
			}
		}
	}

	const bool bFresh = (Meta == nullptr);
	if (bFresh)
	{
		Meta = Cast<UPolarityMetaSave>(UGameplayStatics::CreateSaveGameObject(UPolarityMetaSave::StaticClass()));
		InitializeFreshMeta();
	}

	// Range sanity.
	Meta->MetaCurrency = FMath::Max<int64>(0, Meta->MetaCurrency);

	bMetaLoaded = true;
	UE_LOG(LogTemp, Log, TEXT("[SAVE_DEBUG] Meta %s (currency=%lld runs=%d tutorial=%d apps=%d)"),
		bFresh ? TEXT("created fresh") : TEXT("loaded"),
		Meta->MetaCurrency, Meta->CompletedRuns, Meta->bTutorialCompleted ? 1 : 0, Meta->UnlockedApps.Num());
}

// ============================================================================
// Meta save (gather -> disk)
// ============================================================================
void USaveGameSubsystem::GatherMeta()
{
	if (!Meta) { return; }
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UStreamSubsystem* Stream = GI->GetSubsystem<UStreamSubsystem>())
		{
			Meta->MetaCurrency  = Stream->GetMetaCurrency();
			Meta->CompletedRuns = Stream->GetCompletedRuns();
			const FString StreamName = Stream->GetPlayerStreamerName();
			if (!StreamName.IsEmpty()) { Meta->PlayerStreamerName = StreamName; }
		}
		if (ULoreSubsystem* Lore = GI->GetSubsystem<ULoreSubsystem>())
		{
			Meta->ConsumedLoreIDs = Lore->GetConsumedLoreIDs();
		}
	}
	// bTutorialCompleted / UnlockedApps / Chat / ability-bank already live on Meta.
}

void USaveGameSubsystem::SaveMetaNow()
{
	if (!bMetaLoaded || !Meta) { return; }
	GatherMeta();
	Meta->SaveVersion = PolaritySaveVersion::MetaLatest;
	Meta->SavedAtUtc  = FDateTime::UtcNow();
	WriteSaveToDisk(Meta, MetaSlotName());
	bMetaDirty = false;
	UE_LOG(LogTemp, Log, TEXT("[SAVE_DEBUG] Meta saved (currency=%lld runs=%d unread=%d)"),
		Meta->MetaCurrency, Meta->CompletedRuns, GetUnreadChatCount());
}

void USaveGameSubsystem::RequestMetaSave()
{
	if (!bMetaLoaded) { return; }
	bMetaDirty = true;
	if (!FlushTickerHandle.IsValid())
	{
		// FTSTicker is GameInstance-independent: it survives OpenLevel/world travel (a World timer
		// would be destroyed mid-travel and silently drop the queued save).
		FlushTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &USaveGameSubsystem::OnFlushTick), 3.0f);
	}
}

bool USaveGameSubsystem::OnFlushTick(float /*DeltaTime*/)
{
	FlushTickerHandle.Reset();
	FlushMetaIfDirty();
	return false; // one-shot
}

void USaveGameSubsystem::FlushMetaIfDirty()
{
	if (bMetaDirty && bMetaLoaded && Meta)
	{
		SaveMetaNow();
	}
}

void USaveGameSubsystem::WipeMeta()
{
	const FString Path = SlotFilePath(MetaSlotName());
	IFileManager& FM = IFileManager::Get();
	FM.Delete(*Path, false, true, true);
	FM.Delete(*(Path + TEXT(".bak")), false, true, true);
	FM.Delete(*(Path + TEXT(".tmp")), false, true, true);

	Meta = Cast<UPolarityMetaSave>(UGameplayStatics::CreateSaveGameObject(UPolarityMetaSave::StaticClass()));
	InitializeFreshMeta();
	bMetaDirty = false;
	bMetaLoaded = true;
	OnDesktopStateChanged.Broadcast();
	UE_LOG(LogTemp, Warning, TEXT("[SAVE_DEBUG] Meta wiped to fresh (live subsystems refresh on next boot)."));
}

// ============================================================================
// Player stream handle
// ============================================================================
bool USaveGameSubsystem::HasPlayerStreamerName() const
{
	return Meta && !Meta->PlayerStreamerName.IsEmpty();
}

FString USaveGameSubsystem::GetPlayerStreamerName() const
{
	return Meta ? Meta->PlayerStreamerName : FString();
}

void USaveGameSubsystem::SetPlayerStreamerName(const FString& InName)
{
	if (!Meta) { return; }
	Meta->PlayerStreamerName = InName;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UStreamSubsystem* Stream = GI->GetSubsystem<UStreamSubsystem>())
		{
			Stream->SetPlayerStreamerName(InName);
		}
	}
	RequestMetaSave();
}

// ============================================================================
// Tutorial (set by the TUTORIAL LEVEL event, idempotent)
// ============================================================================
void USaveGameSubsystem::NotifyTutorialCompleted()
{
	if (!Meta || Meta->bTutorialCompleted)
	{
		return; // at most one disk write on the first transition
	}

	Meta->bTutorialCompleted = true;
	Meta->UnlockedApps.Add(FName(TEXT("raid")));
	Meta->Chat.UnlockedGroups.Add(FName(TEXT("post_tutorial")));

	SaveMetaNow(); // high-value transition -> immediate sync flush
	OnDesktopStateChanged.Broadcast();
	UE_LOG(LogTemp, Log, TEXT("[SAVE_DEBUG] Tutorial completed -> 'raid' app + 'post_tutorial' chat unlocked."));
}

bool USaveGameSubsystem::IsTutorialCompleted() const
{
	return Meta && Meta->bTutorialCompleted;
}

// ============================================================================
// Desktop apps
// ============================================================================
bool USaveGameSubsystem::IsAppUnlocked(FName AppId) const
{
	return Meta && Meta->UnlockedApps.Contains(AppId);
}

void USaveGameSubsystem::UnlockApp(FName AppId)
{
	if (Meta && !Meta->UnlockedApps.Contains(AppId))
	{
		Meta->UnlockedApps.Add(AppId);
		RequestMetaSave();
		OnDesktopStateChanged.Broadcast();
	}
}

TSet<FName> USaveGameSubsystem::GetUnlockedApps() const
{
	return Meta ? Meta->UnlockedApps : TSet<FName>();
}

// ============================================================================
// Gamer-chat read-state
// ============================================================================
TSet<FName> USaveGameSubsystem::GetUnlockedChatGroups() const
{
	return Meta ? Meta->Chat.UnlockedGroups : TSet<FName>();
}

bool USaveGameSubsystem::ShouldPlayChatRealtime(FName GroupId) const
{
	return Meta
		&& Meta->Chat.UnlockedGroups.Contains(GroupId)
		&& !Meta->Chat.SeenGroups.Contains(GroupId);
}

bool USaveGameSubsystem::IsChatGroupSeen(FName GroupId) const
{
	return Meta && Meta->Chat.SeenGroups.Contains(GroupId);
}

void USaveGameSubsystem::RegisterChatGroupSizes(const TMap<FName, int32>& Sizes)
{
	ChatGroupSizes = Sizes;
	OnDesktopStateChanged.Broadcast(); // badge can now be computed with real numbers
}

int32 USaveGameSubsystem::GetUnreadChatCount() const
{
	if (!Meta) { return 0; }
	int32 Unread = 0;
	for (const FName& G : Meta->Chat.UnlockedGroups)
	{
		if (!Meta->Chat.SeenGroups.Contains(G))
		{
			if (const int32* Size = ChatGroupSizes.Find(G))
			{
				Unread += *Size;
			}
		}
	}
	return Unread;
}

void USaveGameSubsystem::UnlockChatGroup(FName GroupId)
{
	if (Meta && !Meta->Chat.UnlockedGroups.Contains(GroupId))
	{
		Meta->Chat.UnlockedGroups.Add(GroupId);
		RequestMetaSave();
		OnDesktopStateChanged.Broadcast();
	}
}

void USaveGameSubsystem::MarkChatGroupSeen(FName GroupId)
{
	if (Meta && !Meta->Chat.SeenGroups.Contains(GroupId))
	{
		Meta->Chat.SeenGroups.Add(GroupId);
		RequestMetaSave();
		OnDesktopStateChanged.Broadcast(); // unread badge changed
	}
}

// ============================================================================
// Run slot
// ============================================================================
void USaveGameSubsystem::SaveRun()
{
	UPolarityRunSave* RS = Cast<UPolarityRunSave>(
		UGameplayStatics::CreateSaveGameObject(UPolarityRunSave::StaticClass()));
	if (!RS) { return; }

	GatherRun(*RS);
	RS->SaveVersion = PolaritySaveVersion::RunLatest;
	if (WriteSaveToDisk(RS, RunSlotName()))
	{
		bRunSaveExists = true;
		CachedResumeArenaIndex = RS->CurrentArenaIndex;
	}
}

bool USaveGameSubsystem::ResumeRun()
{
	USaveGame* Loaded = LoadSaveFromDisk(RunSlotName());
	UPolarityRunSave* RS = Cast<UPolarityRunSave>(Loaded);
	if (!RS) { return false; }

	if (RS->SaveVersion > PolaritySaveVersion::RunLatest || !MigrateRun(*RS))
	{
		// Run slot is expendable: discard a corrupt/incompatible resume and offer a fresh run.
		ClearRun();
		return false;
	}

	ApplyRun(*RS);
	return true;
}

void USaveGameSubsystem::ClearRun()
{
	const FString Path = SlotFilePath(RunSlotName());
	IFileManager& FM = IFileManager::Get();
	FM.Delete(*Path, false, true, true);
	FM.Delete(*(Path + TEXT(".bak")), false, true, true);
	FM.Delete(*(Path + TEXT(".tmp")), false, true, true);
	bRunSaveExists = false;
	CachedResumeArenaIndex = -1;
}

void USaveGameSubsystem::GatherRun(UPolarityRunSave& Out) const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (URunSubsystem* Run = GI->GetSubsystem<URunSubsystem>())
		{
			Out.RunState              = static_cast<uint8>(Run->GetRunState());
			Out.CurrentArenaIndex     = Run->GetCurrentArenaIndex();
			Out.ActivatedAntennaCount = Run->GetActivatedAntennaCount();
			Out.AcquiredUpgrades      = Run->GetUpgradeLedger();

			const FRunStats& Stats = Run->GetStats();
			Out.TotalXPEarned = Stats.TotalXPEarned;
			Out.LevelsGained  = Stats.LevelsGained;
			Out.RunDuration   = Stats.RunDuration;

			Out.KillsByEnemyClassPath.Reset();
			for (const TPair<TSubclassOf<AShooterNPC>, int32>& Kill : Stats.KillsByEnemy)
			{
				if (*Kill.Key)
				{
					Out.KillsByEnemyClassPath.Add(FSoftObjectPath(*Kill.Key).ToString(), Kill.Value);
				}
			}
		}
		if (UXPSubsystem* XP = GI->GetSubsystem<UXPSubsystem>())
		{
			Out.XP_CurrentXP    = XP->GetCurrentXP();
			Out.XP_CurrentLevel = XP->GetCurrentLevel();
		}
	}
}

void USaveGameSubsystem::ApplyRun(const UPolarityRunSave& In)
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (URunSubsystem* Run = GI->GetSubsystem<URunSubsystem>())
		{
			FRunStats Stats;
			Stats.TotalXPEarned = In.TotalXPEarned;
			Stats.LevelsGained  = In.LevelsGained;
			Stats.RunDuration   = In.RunDuration;
			for (const TPair<FString, int32>& Kv : In.KillsByEnemyClassPath)
			{
				TSoftClassPtr<AShooterNPC> SoftClass(FSoftObjectPath(Kv.Key));
				if (UClass* Resolved = SoftClass.LoadSynchronous())
				{
					Stats.KillsByEnemy.Add(Resolved, Kv.Value);
				}
				// Unresolved (renamed/deleted BP) -> dropped. Acceptable for run-scoped stats.
			}

			Run->RestoreFromSave(
				static_cast<ERunState>(In.RunState),
				In.CurrentArenaIndex,
				In.ActivatedAntennaCount,
				In.AcquiredUpgrades,
				Stats);
		}
		if (UXPSubsystem* XP = GI->GetSubsystem<UXPSubsystem>())
		{
			XP->RestoreProgress(In.XP_CurrentXP, In.XP_CurrentLevel);
		}
	}
}

// ============================================================================
// Versioning (v1 only today; chain-migrate older saves forward on load)
// ============================================================================
bool USaveGameSubsystem::MigrateMeta(UPolarityMetaSave& S) const
{
	while (S.SaveVersion < PolaritySaveVersion::MetaLatest)
	{
		// Add per-version migration steps here as the schema evolves, e.g.:
		//   if (S.SaveVersion == 1) { Migrate_1_to_2(S); S.SaveVersion = 2; continue; }
		return false; // no migration path for this version -> reject (caller starts fresh)
	}
	return true;
}

bool USaveGameSubsystem::MigrateRun(UPolarityRunSave& S) const
{
	while (S.SaveVersion < PolaritySaveVersion::RunLatest)
	{
		// Add per-version migration steps here as the run schema evolves.
		return false;
	}
	return true;
}

// ============================================================================
// Disk plumbing — atomic temp->rename + .bak fallback. PC/desktop file-based.
// ============================================================================
bool USaveGameSubsystem::WriteSaveToDisk(USaveGame* Obj, const FString& Slot)
{
	if (!Obj) { return false; }

	TArray<uint8> Bytes;
	if (!UGameplayStatics::SaveGameToMemory(Obj, Bytes))
	{
		UE_LOG(LogTemp, Warning, TEXT("[SAVE_DEBUG] SaveGameToMemory failed for slot '%s'"), *Slot);
		return false;
	}

	const FString Live = SlotFilePath(Slot);
	const FString Tmp  = Live + TEXT(".tmp");
	const FString Bak  = Live + TEXT(".bak");

	IFileManager& FM = IFileManager::Get();
	FM.MakeDirectory(*FPaths::GetPath(Live), /*Tree*/ true);

	if (!FFileHelper::SaveArrayToFile(Bytes, *Tmp))
	{
		UE_LOG(LogTemp, Warning, TEXT("[SAVE_DEBUG] Failed writing temp for slot '%s'"), *Slot);
		return false;
	}

	// IFileManager::Move(Dest, Src) moves Src -> Dest.
	if (FM.FileExists(*Live))
	{
		FM.Move(*Bak, *Live, /*Replace*/ true, /*EvenIfReadOnly*/ true); // rotate live -> .bak
	}
	FM.Move(*Live, *Tmp, /*Replace*/ true, /*EvenIfReadOnly*/ true);       // promote tmp -> live (atomic rename)
	return true;
}

USaveGame* USaveGameSubsystem::LoadSaveFromDisk(const FString& Slot)
{
	auto TryLoad = [](const FString& Path) -> USaveGame*
	{
		TArray<uint8> Bytes;
		if (FFileHelper::LoadFileToArray(Bytes, *Path) && Bytes.Num() > 0)
		{
			return UGameplayStatics::LoadGameFromMemory(Bytes);
		}
		return nullptr;
	};

	const FString Live = SlotFilePath(Slot);
	if (USaveGame* SG = TryLoad(Live))
	{
		return SG;
	}
	if (USaveGame* SG = TryLoad(Live + TEXT(".bak")))
	{
		UE_LOG(LogTemp, Warning, TEXT("[SAVE_DEBUG] Slot '%s' main file unreadable; recovered from .bak."), *Slot);
		return SG;
	}
	return nullptr;
}

// ============================================================================
// Debug console commands
// ============================================================================
static USaveGameSubsystem* FindSaveSubsystem(UWorld* World)
{
	if (World)
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<USaveGameSubsystem>();
		}
	}
	return nullptr;
}

static FAutoConsoleCommandWithWorld GPolaritySaveWipeCmd(
	TEXT("polarity.save.wipe"),
	TEXT("Delete the meta save and reset progress to fresh."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		if (USaveGameSubsystem* Save = FindSaveSubsystem(World)) { Save->WipeMeta(); }
	}));

static FAutoConsoleCommandWithWorld GPolaritySaveDumpCmd(
	TEXT("polarity.save.dump"),
	TEXT("Log the current meta save state."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		if (USaveGameSubsystem* Save = FindSaveSubsystem(World))
		{
			if (const UPolarityMetaSave* M = Save->GetMeta())
			{
				UE_LOG(LogTemp, Log, TEXT("[SAVE_DEBUG] DUMP name='%s' currency=%lld runs=%d tutorial=%d apps=%d unread=%d resumeArena=%d"),
					*M->PlayerStreamerName, M->MetaCurrency, M->CompletedRuns, M->bTutorialCompleted ? 1 : 0,
					M->UnlockedApps.Num(), Save->GetUnreadChatCount(), Save->GetResumeArenaIndex());
			}
		}
	}));
