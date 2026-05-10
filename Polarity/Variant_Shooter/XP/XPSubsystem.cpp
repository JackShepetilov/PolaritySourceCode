// XPSubsystem.cpp

#include "XPSubsystem.h"

#include "XPConfig.h"
#include "ShooterNPC.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

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

FSkillState& UXPSubsystem::GetOrCreateState(ESkillCategory Category)
{
	return Skills.FindOrAdd(Category);
}

// ==================== Read API ====================

int32 UXPSubsystem::GetCurrentXP(ESkillCategory Category) const
{
	const FSkillState* S = Skills.Find(Category);
	return S ? S->CurrentXP : 0;
}

int32 UXPSubsystem::GetCurrentLevel(ESkillCategory Category) const
{
	const FSkillState* S = Skills.Find(Category);
	return S ? S->CurrentLevel : 0;
}

int32 UXPSubsystem::GetXPToNextLevel(ESkillCategory Category) const
{
	if (!Config.IsValid()) return INT32_MAX;
	const int32 Level = GetCurrentLevel(Category);
	const int32 Next = Level + 1;
	if (Next > Config->GetMaxLevel(Category)) return INT32_MAX;
	return Config->GetThresholdForLevel(Category, Next) - GetCurrentXP(Category);
}

int32 UXPSubsystem::GetXPIntoCurrentLevel(ESkillCategory Category) const
{
	if (!Config.IsValid()) return GetCurrentXP(Category);
	const int32 Level = GetCurrentLevel(Category);
	const int32 Prev = (Level >= 1) ? Config->GetThresholdForLevel(Category, Level) : 0;
	return GetCurrentXP(Category) - Prev;
}

int32 UXPSubsystem::GetCurrentLevelSpan(ESkillCategory Category) const
{
	if (!Config.IsValid()) return 0;
	const int32 Level = GetCurrentLevel(Category);
	const int32 Next = Level + 1;
	if (Next > Config->GetMaxLevel(Category)) return 0;
	const int32 Prev = (Level >= 1) ? Config->GetThresholdForLevel(Category, Level) : 0;
	return Config->GetThresholdForLevel(Category, Next) - Prev;
}

float UXPSubsystem::GetProgressToNextLevel01(ESkillCategory Category) const
{
	const int32 Span = GetCurrentLevelSpan(Category);
	if (Span <= 0) return 1.f;
	return FMath::Clamp(static_cast<float>(GetXPIntoCurrentLevel(Category)) / static_cast<float>(Span), 0.f, 1.f);
}

// ==================== Award API ====================

void UXPSubsystem::AddSkillXP(ESkillCategory Category, int32 Amount)
{
	if (Amount <= 0) return;

	URunSubsystem* Run = GetRunSubsystem();
	if (!Run || !Run->IsRunActive())
	{
		// Run inactive — ignore (covers Гл.1 / cinematics / post-death).
		return;
	}

	FSkillState& State = GetOrCreateState(Category);
	State.CurrentXP += Amount;

	Run->AddXPEarnedToStats(Amount);

	OnSkillXPGained.Broadcast(Category, Amount, State.CurrentXP);
	CheckLevelUpForCategory(Category);
}

void UXPSubsystem::AwardKillXP(ESkillCategory Category, TSubclassOf<AShooterNPC> EnemyClass)
{
	if (!Config.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[XP_DEBUG] AwardKillXP called with no config"));
		return;
	}

	const int32 Base = Config->GetBaseXPPerKill(Category);
	if (Base <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[XP_DEBUG] BaseXPPerKill=0 for skill %d (curve missing?)"), (int32)Category);
		return;
	}

	const float Multiplier = Config->GetEnemyMultiplier(EnemyClass);
	const int32 Award = FMath::RoundToInt(static_cast<float>(Base) * Multiplier);

	AddSkillXP(Category, Award);
}

// ==================== Internals ====================

void UXPSubsystem::CheckLevelUpForCategory(ESkillCategory Category)
{
	if (!Config.IsValid()) return;
	const int32 MaxLevel = Config->GetMaxLevel(Category);

	FSkillState& State = GetOrCreateState(Category);

	while (State.CurrentLevel < MaxLevel)
	{
		const int32 Next = State.CurrentLevel + 1;
		const int32 Threshold = Config->GetThresholdForLevel(Category, Next);
		if (State.CurrentXP < Threshold) break;

		State.CurrentLevel = Next;

		if (URunSubsystem* Run = GetRunSubsystem())
		{
			Run->AddLevelGainedToStats();
		}

		UE_LOG(LogTemp, Log, TEXT("[XP_DEBUG] LevelUp skill=%d -> %d"), (int32)Category, State.CurrentLevel);
		OnSkillLevelUp.Broadcast(Category, State.CurrentLevel);
	}
}

// ==================== Run lifecycle handlers ====================

void UXPSubsystem::HandleRunStarted()
{
	UE_LOG(LogTemp, Log, TEXT("[XP_DEBUG] HandleRunStarted — resetting all skills"));
	Skills.Reset();
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

	ESkillCategory Cat;
	if (!Config->GetSkillForDamageType(KillingDamageType, Cat))
	{
		UE_LOG(LogTemp, Warning, TEXT("[XP_DEBUG] DamageType %s not in KillXPRouting — kill awards no XP (add the entry to DA_XPConfig.KillXPRouting)"),
			KillingDamageType ? *KillingDamageType->GetName() : TEXT("NULL"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[XP_DEBUG] Kill %s by %s -> skill %d"),
		*DeadNPC->GetName(),
		KillingDamageType ? *KillingDamageType->GetName() : TEXT("NULL-DamageType"),
		(int32)Cat);
	AwardKillXP(Cat, EnemyClass);
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
