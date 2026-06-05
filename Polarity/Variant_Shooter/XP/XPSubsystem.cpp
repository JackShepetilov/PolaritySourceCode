// XPSubsystem.cpp

#include "XPSubsystem.h"

#include "XPConfig.h"
#include "ShooterNPC.h"

#include "UpgradeManagerComponent.h"
#include "UpgradeRegistry.h"
#include "UpgradeDefinition.h"
#include "GameplayTagContainer.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectIterator.h"

void UXPSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency(URunSubsystem::StaticClass());

	if (URunSubsystem* Run = GetRunSubsystem())
	{
		Run->OnRunStarted.AddDynamic(this, &UXPSubsystem::HandleRunStarted);
		Run->OnRunEnded.AddDynamic(this, &UXPSubsystem::HandleRunEnded);
		Run->OnArenaEntered.AddDynamic(this, &UXPSubsystem::HandleArenaEntered);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[XP_DEBUG] RunSubsystem unavailable in Initialize — XP will not function"));
	}
}

void UXPSubsystem::Deinitialize()
{
	UnbindNPCEventsForCurrentWorld();

	if (URunSubsystem* Run = GetRunSubsystem())
	{
		Run->OnRunStarted.RemoveDynamic(this, &UXPSubsystem::HandleRunStarted);
		Run->OnRunEnded.RemoveDynamic(this, &UXPSubsystem::HandleRunEnded);
		Run->OnArenaEntered.RemoveDynamic(this, &UXPSubsystem::HandleArenaEntered);
	}

	Super::Deinitialize();
}

URunSubsystem* UXPSubsystem::GetRunSubsystem() const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<URunSubsystem>();
	}
	return nullptr;
}

void UXPSubsystem::SetConfig(UXPConfig* InConfig)
{
	Config = InConfig;
	UE_LOG(LogTemp, Log, TEXT("[XP_DEBUG] Config set: %s"),
		InConfig ? *InConfig->GetName() : TEXT("NULL"));
}

// ==================== Read API ====================

int32 UXPSubsystem::GetCurrentXP() const
{
	return Progress.CurrentXP;
}

int32 UXPSubsystem::GetCurrentLevel() const
{
	return Progress.CurrentLevel;
}

int32 UXPSubsystem::GetXPToNextLevel() const
{
	if (!Config.IsValid()) return INT32_MAX;
	const int32 Next = Progress.CurrentLevel + 1;
	if (Next > Config->GetMaxLevel()) return INT32_MAX;
	return Config->GetThresholdForLevel(Next) - Progress.CurrentXP;
}

int32 UXPSubsystem::GetXPIntoCurrentLevel() const
{
	if (!Config.IsValid()) return Progress.CurrentXP;
	const int32 Level = Progress.CurrentLevel;
	const int32 Prev = (Level >= 1) ? Config->GetThresholdForLevel(Level) : 0;
	return Progress.CurrentXP - Prev;
}

int32 UXPSubsystem::GetCurrentLevelSpan() const
{
	if (!Config.IsValid()) return 0;
	const int32 Level = Progress.CurrentLevel;
	const int32 Next = Level + 1;
	if (Next > Config->GetMaxLevel()) return 0;
	const int32 Prev = (Level >= 1) ? Config->GetThresholdForLevel(Level) : 0;
	return Config->GetThresholdForLevel(Next) - Prev;
}

float UXPSubsystem::GetProgressToNextLevel01() const
{
	const int32 Span = GetCurrentLevelSpan();
	if (Span <= 0) return 1.f;
	return FMath::Clamp(static_cast<float>(GetXPIntoCurrentLevel()) / static_cast<float>(Span), 0.f, 1.f);
}

// ==================== Award API ====================

void UXPSubsystem::AddXP(int32 Amount)
{
	if (Amount <= 0) return;

	URunSubsystem* Run = GetRunSubsystem();
	if (!Run || !Run->IsRunActive())
	{
		// Run inactive — ignore (covers Гл.1 / cinematics / post-death).
		return;
	}

	Progress.CurrentXP += Amount;

	Run->AddXPEarnedToStats(Amount);

	OnXPGained.Broadcast(Amount, Progress.CurrentXP);
	CheckLevelUp();
}

void UXPSubsystem::AwardKillXP(TSubclassOf<AShooterNPC> EnemyClass)
{
	if (!Config.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[XP_DEBUG] AwardKillXP called with no config"));
		return;
	}

	const int32 Base = Config->GetBaseXPPerKill();
	if (Base <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[XP_DEBUG] BaseXPPerKill=0 (LevelCurve missing?)"));
		return;
	}

	const float Multiplier = Config->GetEnemyMultiplier(EnemyClass);
	const int32 Award = FMath::RoundToInt(static_cast<float>(Base) * Multiplier);

	AddXP(Award);
}

// ==================== Internals ====================

void UXPSubsystem::CheckLevelUp()
{
	if (!Config.IsValid()) return;
	const int32 MaxLevel = Config->GetMaxLevel();

	while (Progress.CurrentLevel < MaxLevel)
	{
		const int32 Next = Progress.CurrentLevel + 1;
		const int32 Threshold = Config->GetThresholdForLevel(Next);
		if (Progress.CurrentXP < Threshold) break;

		Progress.CurrentLevel = Next;

		if (URunSubsystem* Run = GetRunSubsystem())
		{
			Run->AddLevelGainedToStats();
		}

		UE_LOG(LogTemp, Log, TEXT("[XP_DEBUG] LevelUp -> %d"), Progress.CurrentLevel);
		OnLevelUp.Broadcast(Progress.CurrentLevel);
	}
}

// ==================== Run lifecycle handlers ====================

void UXPSubsystem::HandleRunStarted()
{
	UE_LOG(LogTemp, Log, TEXT("[XP_DEBUG] HandleRunStarted — resetting XP track"));
	Progress = FSkillState();
}

void UXPSubsystem::HandleRunEnded(ERunEndReason Reason)
{
	UE_LOG(LogTemp, Log, TEXT("[XP_DEBUG] HandleRunEnded reason=%d"), (int32)Reason);
	UnbindNPCEventsForCurrentWorld();
}

void UXPSubsystem::HandleArenaEntered(int32 ArenaIndex)
{
	UE_LOG(LogTemp, Log, TEXT("[XP_DEBUG] HandleArenaEntered %d — rebinding NPC death events"), ArenaIndex);
	UnbindNPCEventsForCurrentWorld();
	BindNPCEventsForCurrentWorld();
}

// ==================== NPC tracking ====================

void UXPSubsystem::BindNPCEventsForCurrentWorld()
{
	UWorld* World = GetWorld();
	if (!World) return;
	BoundWorld = World;

	ActorSpawnedHandle = World->AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateUObject(this, &UXPSubsystem::OnAnyActorSpawned));

	int32 ExistingCount = 0;
	for (TActorIterator<AShooterNPC> It(World); It; ++It)
	{
		BindToNPC(*It);
		++ExistingCount;
	}

	UE_LOG(LogTemp, Log, TEXT("[XP_DEBUG] Bound to %d existing NPCs + spawn handler"), ExistingCount);
}

void UXPSubsystem::UnbindNPCEventsForCurrentWorld()
{
	if (UWorld* World = BoundWorld.Get())
	{
		if (ActorSpawnedHandle.IsValid())
		{
			World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
			ActorSpawnedHandle.Reset();
		}
	}

	for (const TWeakObjectPtr<AShooterNPC>& WeakNPC : BoundNPCs)
	{
		if (AShooterNPC* NPC = WeakNPC.Get())
		{
			NPC->OnNPCDeathDetailed.RemoveDynamic(this, &UXPSubsystem::HandleNPCDeath);
		}
	}
	BoundNPCs.Reset();
	BoundWorld.Reset();
}

void UXPSubsystem::OnAnyActorSpawned(AActor* Actor)
{
	if (AShooterNPC* NPC = Cast<AShooterNPC>(Actor))
	{
		BindToNPC(NPC);
		UE_LOG(LogTemp, Log, TEXT("[XP_DEBUG] Bound late-spawn NPC: %s (class %s)"),
			*NPC->GetName(), *NPC->GetClass()->GetName());
	}
}

void UXPSubsystem::BindToNPC(AShooterNPC* NPC)
{
	if (!NPC) return;
	NPC->OnNPCDeathDetailed.AddDynamic(this, &UXPSubsystem::HandleNPCDeath);
	BoundNPCs.Add(NPC);
}

void UXPSubsystem::HandleNPCDeath(AShooterNPC* DeadNPC, TSubclassOf<UDamageType> KillingDamageType, AActor* KillingDamageCauser)
{
	if (!DeadNPC) return;

	UE_LOG(LogTemp, Log, TEXT("[XP_DEBUG] HandleNPCDeath: NPC=%s DT=%s Causer=%s"),
		*DeadNPC->GetName(),
		KillingDamageType ? *KillingDamageType->GetName() : TEXT("NULL"),
		KillingDamageCauser ? *KillingDamageCauser->GetName() : TEXT("NULL"));

	URunSubsystem* Run = GetRunSubsystem();
	if (!Run || !Run->IsRunActive())
	{
		UE_LOG(LogTemp, Verbose, TEXT("[XP_DEBUG] Kill %s ignored: run not active"), *DeadNPC->GetName());
		return;
	}

	// Some DamageTypes are gameplay-implied player kills even when the engine's Causer/Instigator
	// chain doesn't lead back to the PlayerPawn (e.g. thrown props, wallslam from yank-throws).
	// Designer opts these in via XPConfig.AlwaysAttributeToPlayer.
	const bool bAlwaysAttribute = Config.IsValid() && Config->ShouldAlwaysAttributeToPlayer(KillingDamageType);

	if (!bAlwaysAttribute && !WasKillCausedByPlayer(KillingDamageCauser))
	{
		// Most common reason for "missing" XP from props / thrown NPCs: their Instigator chain
		// doesn't lead back to the player pawn. If you expect the kill to count, the gameplay
		// code that spawns/kicks the prop must set its Instigator to the player.
		const FString CauserStr   = KillingDamageCauser ? KillingDamageCauser->GetName() : FString(TEXT("NULL"));
		const FString OwnerStr    = (KillingDamageCauser && KillingDamageCauser->GetOwner())
			? KillingDamageCauser->GetOwner()->GetName() : FString(TEXT("NULL"));
		const FString InstigStr   = (KillingDamageCauser && KillingDamageCauser->GetInstigator())
			? KillingDamageCauser->GetInstigator()->GetName() : FString(TEXT("NULL"));
		const FString DTStr       = KillingDamageType ? KillingDamageType->GetName() : FString(TEXT("NULL"));

		UE_LOG(LogTemp, Warning,
			TEXT("[XP_DEBUG] Kill %s ignored: not attributed to player. DamageType=%s Causer=%s Owner=%s Instigator=%s"),
			*DeadNPC->GetName(), *DTStr, *CauserStr, *OwnerStr, *InstigStr);
		return;
	}

	const TSubclassOf<AShooterNPC> EnemyClass = DeadNPC->GetClass();
	Run->RegisterKillInStats(EnemyClass);

	if (!Config.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[XP_DEBUG] No XPConfig — kill stats registered but no XP awarded"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[XP_DEBUG] Kill %s by %s -> +XP"),
		*DeadNPC->GetName(),
		KillingDamageType ? *KillingDamageType->GetName() : TEXT("NULL-DamageType"));
	AwardKillXP(EnemyClass);
}

bool UXPSubsystem::WasKillCausedByPlayer(AActor* DamageCauser) const
{
	if (!DamageCauser) return false;

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PlayerPawn) return false;

	if (DamageCauser == PlayerPawn) return true;
	if (DamageCauser->GetInstigator() == PlayerPawn) return true;
	if (DamageCauser->GetOwner() == PlayerPawn) return true;
	return false;
}

// ==================== Debug Console Commands ====================
//
// All commands are registered via FAutoConsoleCommandWithWorldAndArgs (static, global).
// Open the in-game console (` key) and type the command name. Tab-completion works.
//
// Examples:
//   polarity.xp.add 500
//   polarity.xp.levelup
//   polarity.xp.show
//   polarity.run.start
//   polarity.run.enterarena 0

namespace XPDebugCommands
{
	static UXPSubsystem* GetXP(UWorld* World)
	{
		if (!World) return nullptr;
		UGameInstance* GI = World->GetGameInstance();
		return GI ? GI->GetSubsystem<UXPSubsystem>() : nullptr;
	}

	static URunSubsystem* GetRun(UWorld* World)
	{
		if (!World) return nullptr;
		UGameInstance* GI = World->GetGameInstance();
		return GI ? GI->GetSubsystem<URunSubsystem>() : nullptr;
	}

	static void CmdAddXP(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: polarity.xp.add <amount>"));
			return;
		}
		const int32 Amount = FCString::Atoi(*Args[0]);
		if (Amount <= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Amount must be positive, got %d"), Amount);
			return;
		}
		UXPSubsystem* XP = GetXP(World);
		if (!XP) { UE_LOG(LogTemp, Warning, TEXT("XPSubsystem not found")); return; }

		URunSubsystem* Run = GetRun(World);
		if (Run && !Run->IsRunActive())
		{
			UE_LOG(LogTemp, Warning, TEXT("[XP_DEBUG] Run not active — call polarity.run.start first (or AddXP will be a no-op)"));
		}

		XP->AddXP(Amount);
		UE_LOG(LogTemp, Log, TEXT("[XP_DEBUG] cmd: +%d XP (total %d, level %d)"),
			Amount, XP->GetCurrentXP(), XP->GetCurrentLevel());
	}

	static void CmdLevelUp(const TArray<FString>& /*Args*/, UWorld* World)
	{
		UXPSubsystem* XP = GetXP(World);
		if (!XP) return;
		const int32 ToNext = XP->GetXPToNextLevel();
		if (ToNext == INT32_MAX)
		{
			UE_LOG(LogTemp, Warning, TEXT("[XP_DEBUG] Already at max level"));
			return;
		}
		XP->AddXP(FMath::Max(1, ToNext));
	}

	static void CmdShow(const TArray<FString>& /*Args*/, UWorld* World)
	{
		UXPSubsystem* XP = GetXP(World);
		if (!XP) { UE_LOG(LogTemp, Warning, TEXT("XPSubsystem not found")); return; }

		URunSubsystem* Run = GetRun(World);
		const int32 ToNext = XP->GetXPToNextLevel();
		UE_LOG(LogTemp, Log, TEXT("[XP_DEBUG] === XP Status (run %s) ===  XP=%d  Lv=%d  ToNext=%s"),
			(Run && Run->IsRunActive()) ? TEXT("ACTIVE") : TEXT("INACTIVE"),
			XP->GetCurrentXP(),
			XP->GetCurrentLevel(),
			(ToNext == INT32_MAX) ? TEXT("MAX") : *FString::FromInt(ToNext));
	}

	static void CmdRunStart(const TArray<FString>& /*Args*/, UWorld* World)
	{
		URunSubsystem* Run = GetRun(World);
		if (!Run) { UE_LOG(LogTemp, Warning, TEXT("RunSubsystem not found")); return; }
		Run->StartRun();
	}

	static void CmdRunEnd(const TArray<FString>& Args, UWorld* World)
	{
		URunSubsystem* Run = GetRun(World);
		if (!Run) return;
		ERunEndReason Reason = ERunEndReason::Aborted;
		if (Args.Num() > 0)
		{
			if      (Args[0].Equals(TEXT("Death"),   ESearchCase::IgnoreCase)) Reason = ERunEndReason::PlayerDeath;
			else if (Args[0].Equals(TEXT("Victory"), ESearchCase::IgnoreCase)) Reason = ERunEndReason::Victory;
			else if (Args[0].Equals(TEXT("Quit"),    ESearchCase::IgnoreCase)) Reason = ERunEndReason::QuitToMenu;
		}
		Run->EndRun(Reason);
	}

	static void CmdRunEnterArena(const TArray<FString>& Args, UWorld* World)
	{
		URunSubsystem* Run = GetRun(World);
		if (!Run) return;
		const int32 Idx = (Args.Num() > 0) ? FCString::Atoi(*Args[0]) : 0;
		Run->EnterArena(Idx);
	}

	// ==================== Upgrade commands ====================

	static UUpgradeManagerComponent* GetUpgradeManagerComp(UWorld* World)
	{
		if (!World) return nullptr;
		APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);
		return Pawn ? Pawn->FindComponentByClass<UUpgradeManagerComponent>() : nullptr;
	}

	/** Returns the first loaded UUpgradeRegistry instance in memory (skipping CDOs). */
	static UUpgradeRegistry* FindFirstLoadedRegistry()
	{
		for (TObjectIterator<UUpgradeRegistry> It; It; ++It)
		{
			UUpgradeRegistry* R = *It;
			if (!R || R->IsTemplate() || R->HasAnyFlags(RF_ClassDefaultObject)) continue;
			return R;
		}
		return nullptr;
	}

	static void CmdUpgradeGrant(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: polarity.upgrade.grant <Upgrade.Tag>"));
			return;
		}

		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Args[0]), /*bErrorIfNotFound*/ false);
		if (!Tag.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] Gameplay tag '%s' is not registered (check Project Settings > GameplayTags)"), *Args[0]);
			return;
		}

		UUpgradeRegistry* Registry = FindFirstLoadedRegistry();
		if (!Registry)
		{
			UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] No UpgradeRegistry loaded — open WBP_UpgradeChoice or start a run so the registry is referenced"));
			return;
		}

		UUpgradeDefinition* Def = Registry->FindByTag(Tag);
		if (!Def)
		{
			UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] Tag '%s' not present in UpgradeRegistry.AllUpgrades"), *Tag.ToString());
			return;
		}

		UUpgradeManagerComponent* Manager = GetUpgradeManagerComp(World);
		if (!Manager)
		{
			UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] Player pawn has no UUpgradeManagerComponent"));
			return;
		}

		const bool bOk = Manager->GrantUpgrade(Def);
		UE_LOG(LogTemp, Log, TEXT("[UPGRADE_DEBUG] cmd grant '%s' -> %s (now Lv %d/%d)"),
			*Tag.ToString(),
			bOk ? TEXT("OK") : TEXT("no-op (likely at MaxLevel)"),
			Manager->GetUpgradeLevel(Tag),
			Def->MaxLevel);
	}

	static void CmdUpgradeList(const TArray<FString>& /*Args*/, UWorld* /*World*/)
	{
		UUpgradeRegistry* Registry = FindFirstLoadedRegistry();
		if (!Registry)
		{
			UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] No UpgradeRegistry loaded"));
			return;
		}
		UE_LOG(LogTemp, Log, TEXT("[UPGRADE_DEBUG] === Registry (%d upgrades) ==="), Registry->AllUpgrades.Num());
		for (UUpgradeDefinition* Def : Registry->AllUpgrades)
		{
			if (!Def) continue;
			UE_LOG(LogTemp, Log, TEXT("[UPGRADE_DEBUG]   '%s'  cat=%d  maxLv=%d  tag='%s'"),
				*Def->DisplayName.ToString(),
				(int32)Def->Category,
				Def->MaxLevel,
				*Def->UpgradeTag.ToString());
		}
	}

	static void CmdUpgradeShow(const TArray<FString>& /*Args*/, UWorld* World)
	{
		UUpgradeManagerComponent* Manager = GetUpgradeManagerComp(World);
		if (!Manager)
		{
			UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] No UpgradeManagerComponent on player pawn"));
			return;
		}
		const TArray<UUpgradeDefinition*> Acquired = Manager->GetAcquiredUpgrades();
		UE_LOG(LogTemp, Log, TEXT("[UPGRADE_DEBUG] === Owned (%d) ==="), Acquired.Num());
		for (UUpgradeDefinition* Def : Acquired)
		{
			if (!Def) continue;
			const int32 Lv = Manager->GetUpgradeLevel(Def->UpgradeTag);
			UE_LOG(LogTemp, Log, TEXT("[UPGRADE_DEBUG]   '%s'  Lv %d/%d  tag='%s'"),
				*Def->DisplayName.ToString(), Lv, Def->MaxLevel, *Def->UpgradeTag.ToString());
		}
	}

	static void CmdUpgradeRemove(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("Usage: polarity.upgrade.remove <Upgrade.Tag>"));
			return;
		}
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Args[0]), false);
		if (!Tag.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] Unknown gameplay tag '%s'"), *Args[0]);
			return;
		}
		UUpgradeManagerComponent* Manager = GetUpgradeManagerComp(World);
		if (!Manager) return;
		const bool bOk = Manager->RemoveUpgrade(Tag);
		UE_LOG(LogTemp, Log, TEXT("[UPGRADE_DEBUG] cmd remove '%s' -> %s"),
			*Tag.ToString(), bOk ? TEXT("OK") : TEXT("not owned"));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GCmdPolarityXPAdd(
	TEXT("polarity.xp.add"),
	TEXT("Add XP to the run's single track. Usage: polarity.xp.add <amount>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&XPDebugCommands::CmdAddXP));

static FAutoConsoleCommandWithWorldAndArgs GCmdPolarityXPLevelUp(
	TEXT("polarity.xp.levelup"),
	TEXT("Adds just enough XP to trigger one level-up. Usage: polarity.xp.levelup"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&XPDebugCommands::CmdLevelUp));

static FAutoConsoleCommandWithWorldAndArgs GCmdPolarityXPShow(
	TEXT("polarity.xp.show"),
	TEXT("Logs current XP / level / XP-to-next."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&XPDebugCommands::CmdShow));

static FAutoConsoleCommandWithWorldAndArgs GCmdPolarityRunStart(
	TEXT("polarity.run.start"),
	TEXT("Start a roguelite run (resets all skills, fires OnRunStarted)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&XPDebugCommands::CmdRunStart));

static FAutoConsoleCommandWithWorldAndArgs GCmdPolarityRunEnd(
	TEXT("polarity.run.end"),
	TEXT("End the current run. Usage: polarity.run.end [Death|Victory|Quit|Aborted]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&XPDebugCommands::CmdRunEnd));

static FAutoConsoleCommandWithWorldAndArgs GCmdPolarityRunEnterArena(
	TEXT("polarity.run.enterarena"),
	TEXT("Enter arena by index (re-binds NPC death handlers). Usage: polarity.run.enterarena [idx=0]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&XPDebugCommands::CmdRunEnterArena));

static FAutoConsoleCommandWithWorldAndArgs GCmdPolarityUpgradeGrant(
	TEXT("polarity.upgrade.grant"),
	TEXT("Grant an upgrade to the player by gameplay tag (or level it up if already owned). Usage: polarity.upgrade.grant <Upgrade.Tag>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&XPDebugCommands::CmdUpgradeGrant));

static FAutoConsoleCommandWithWorldAndArgs GCmdPolarityUpgradeRemove(
	TEXT("polarity.upgrade.remove"),
	TEXT("Remove an upgrade from the player by gameplay tag. Usage: polarity.upgrade.remove <Upgrade.Tag>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&XPDebugCommands::CmdUpgradeRemove));

static FAutoConsoleCommandWithWorldAndArgs GCmdPolarityUpgradeList(
	TEXT("polarity.upgrade.list"),
	TEXT("List all upgrades present in the loaded UpgradeRegistry."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&XPDebugCommands::CmdUpgradeList));

static FAutoConsoleCommandWithWorldAndArgs GCmdPolarityUpgradeShow(
	TEXT("polarity.upgrade.show"),
	TEXT("List upgrades the player currently owns and their levels."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&XPDebugCommands::CmdUpgradeShow));
