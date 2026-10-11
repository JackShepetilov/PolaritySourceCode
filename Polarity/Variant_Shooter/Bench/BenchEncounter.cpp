// BenchEncounter.cpp

#include "Variant_Shooter/Bench/BenchEncounter.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "AI/PolarityTeams.h"
#include "AIController.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/Bench/FightRecorder.h"
#include "Variant_Shooter/ShootableButtonComponent.h"

ABenchEncounter::ABenchEncounter()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	// Toggle mode: the same button starts the fight and clears it, with no reset in between.
	Button = CreateDefaultSubobject<UShootableButtonComponent>(TEXT("Button"));
	Button->SetupAttachment(SceneRoot);
	Button->bAutoDisableOnPress = false;
	Button->ActivationCooldown = 0.6f;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded() && Button->GetButtonMesh())
	{
		Button->GetButtonMesh()->SetStaticMesh(Cube.Object);
		Button->GetButtonMesh()->SetRelativeScale3D(FVector(0.15f, 0.5f, 0.5f));
	}

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(SceneRoot);
	Label->SetRelativeLocation(FVector(10.0f, 0.0f, 60.0f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextBottom);
	Label->SetWorldSize(22.0f);
	Label->SetTextRenderColor(FColor(255, 230, 120));
	Label->SetText(FText::FromString(TEXT("ENCOUNTER")));
}

void ABenchEncounter::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && Button)
	{
		Button->OnButtonPressed.AddDynamic(this, &ABenchEncounter::HandleButtonPressed);
	}
	SetStatus(TEXT("shoot to start"));

	// The switch is a console variable and outlives a play session: one that ended in the middle of
	// a watched fight would leave every enemy of the next session blind to the players.
	if (HasAuthority() && bPlayersOnlyWatch)
	{
		SetPlayersIgnored(false);
	}
}

void ABenchEncounter::SetStatus(const FString& Status)
{
	if (Label)
	{
		Label->SetText(FText::FromString(FString::Printf(TEXT("%s\n%s"), *EncounterName, *Status)));
	}
}

int32 ABenchEncounter::GetAliveCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AShooterNPC>& NPC : Alive)
	{
		if (NPC.IsValid() && !NPC->IsDead())
		{
			++Count;
		}
	}
	return Count;
}

void ABenchEncounter::HandleButtonPressed(UShootableButtonComponent* Pressed, AActor* Activator)
{
	Toggle();
}

void ABenchEncounter::Toggle()
{
	if (GetAliveCount() > 0)
	{
		ClearEncounter();
	}
	else
	{
		StartEncounter();
	}
}

int32 ABenchEncounter::SpawnGroup(const FBenchEncounterGroup& Group)
{
	UWorld* const World = GetWorld();
	if (!World)
	{
		return 0;
	}
	if (!Group.EnemyClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BENCH] %s: a group has no enemy class"), *EncounterName);
		return 0;
	}

	// The middle of the group's own points: the ground it holds, when it holds one.
	FVector Middle = FVector::ZeroVector;
	int32 Points = 0;
	for (const AActor* const Point : Group.SpawnPoints)
	{
		if (Point)
		{
			Middle += Point->GetActorLocation();
			++Points;
		}
	}
	Middle = Points > 0 ? Middle / static_cast<float>(Points) : Middle;
	if (Group.HoldCentre)
	{
		Middle = Group.HoldCentre->GetActorLocation();
	}

	// The whole place: a circle round every ground the encounter holds. @see AShooterNPC::SetWideHoldArea
	FVector WideCentre = FVector::ZeroVector;
	float WideRadius = 0.0f;
	{
		TArray<TPair<FVector, float>, TInlineAllocator<8>> Grounds;
		for (const FBenchEncounterGroup& Other : Groups)
		{
			if (Other.Task != ESquadTask::Hold || Other.HoldRadius <= 0.0f)
			{
				continue;
			}
			FVector OtherMiddle = FVector::ZeroVector;
			int32 OtherPoints = 0;
			for (const AActor* const Point : Other.SpawnPoints)
			{
				if (Point)
				{
					OtherMiddle += Point->GetActorLocation();
					++OtherPoints;
				}
			}
			if (Other.HoldCentre)
			{
				OtherMiddle = Other.HoldCentre->GetActorLocation();
			}
			else if (OtherPoints > 0)
			{
				OtherMiddle /= static_cast<float>(OtherPoints);
			}
			else
			{
				continue;
			}
			Grounds.Emplace(OtherMiddle, Other.HoldRadius);
			WideCentre += OtherMiddle;
		}
		if (Grounds.Num() > 1)
		{
			WideCentre /= static_cast<float>(Grounds.Num());
			for (const TPair<FVector, float>& Ground : Grounds)
			{
				WideRadius = FMath::Max(WideRadius, static_cast<float>(FVector::Dist2D(Ground.Key, WideCentre)) + Ground.Value);
			}
		}
	}

	int32 Count = 0;
	for (const AActor* const Point : Group.SpawnPoints)
	{
		if (!Point)
		{
			continue;
		}
		// Deferred, so the side is on the pawn BEFORE it begins play. The sight sense remembers the
		// team of everything it can see at the moment that thing registers, and a side given after
		// the spawn never reaches it: both sides of a versus then read as one team to each other's
		// eyes and stand looking at each other (seen 2026-10-10, ten of them, target None).
		const FTransform SpawnTransform = Point->GetActorTransform();
		AShooterNPC* const NPC = World->SpawnActorDeferred<AShooterNPC>(Group.EnemyClass, SpawnTransform, this, nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!NPC)
		{
			UE_LOG(LogTemp, Error, TEXT("[BENCH] %s: failed to spawn %s"), *EncounterName, *GetNameSafe(Group.EnemyClass));
			continue;
		}
		if (Group.Team != PolarityTeams::Neutral)
		{
			NPC->SetGenericTeamId(FGenericTeamId(Group.Team));
			SidesSpawned.AddUnique(Group.Team);
		}
		NPC->FinishSpawning(SpawnTransform);
		NPC->SetCombatTask(Group.Task);
		if (Group.Task == ESquadTask::Hold && Group.HoldRadius > 0.0f)
		{
			NPC->SetHoldArea(Middle, Group.HoldRadius);
			NPC->SetWideHoldArea(WideCentre, WideRadius);
		}

		if (Group.BodyMaterial)
		{
			TInlineComponentArray<USkeletalMeshComponent*> Bodies(NPC);
			for (USkeletalMeshComponent* const Body : Bodies)
			{
				for (int32 Slot = 0; Slot < Body->GetNumMaterials(); ++Slot)
				{
					Body->SetMaterial(Slot, Group.BodyMaterial);
				}
			}
		}

		if (!NPC->GetController())
		{
			NPC->SpawnDefaultController();
		}

		// The controller takes its side from the pawn when it possesses; said again here so the two
		// cannot disagree whatever order that happened in.
		if (Group.Team != PolarityTeams::Neutral)
		{
			if (IGenericTeamAgentInterface* const Agent = Cast<IGenericTeamAgentInterface>(NPC->GetController()))
			{
				Agent->SetGenericTeamId(FGenericTeamId(Group.Team));
			}
		}

		NPC->OnNPCDeath.AddDynamic(this, &ABenchEncounter::HandleDeath);
		Alive.Add(NPC);
		++Count;
	}
	return Count;
}

void ABenchEncounter::StartEncounter()
{
	UWorld* const World = GetWorld();
	if (!HasAuthority() || !World)
	{
		return;
	}
	ClearEncounter();

	SpawnedCount = 0;
	SidesSpawned.Reset();
	bReinforcementOwed = false;
	bReinforcementCame = false;
	AlarmTime = -1.0f;
	for (const FBenchEncounterGroup& Group : Groups)
	{
		if (Group.bReinforcement)
		{
			bReinforcementOwed = bReinforcementOwed || Group.SpawnPoints.Num() > 0;
			continue;
		}
		SpawnedCount += SpawnGroup(Group);
	}

	bVersus = SidesSpawned.Num() > 1;
	if (bPlayersOnlyWatch && SpawnedCount > 0)
	{
		SetPlayersIgnored(true);
	}
	else if (SpawnedCount > 0)
	{
		// A fight the players are in. The winners of a watched fight stay standing until cleared, and
		// the switch stays on with them: the enemies of this fight would not see the players at all
		// (ALLIES after a VERSUS, author 2026-10-11). So what a watched fight left is cleared first.
		for (TActorIterator<ABenchEncounter> It(World); It; ++It)
		{
			if (*It != this && It->bPlayersOnlyWatch && It->GetAliveCount() > 0)
			{
				It->ClearEncounter();
			}
		}
		SetPlayersIgnored(false);
	}

	bRunning = SpawnedCount > 0;
	bReinforcementOwed = bReinforcementOwed && bRunning;
	StartTime = World->GetTimeSeconds();
	if (bRunning)
	{
		if (UFightRecorderSubsystem* const Recorder = UFightRecorderSubsystem::Get(this))
		{
			Recorder->BeginFight(EncounterName);
		}
	}
	if (bReinforcementOwed)
	{
		World->GetTimerManager().SetTimer(AlarmTimer, this, &ABenchEncounter::WatchAlarm, 0.25f, true);
	}
	SetStatus(bRunning ? FString::Printf(TEXT("%d alive, shoot to clear"), SpawnedCount) : FString(TEXT("nothing to spawn")));
	UE_LOG(LogTemp, Log, TEXT("[BENCH] %s started: %d enemies"), *EncounterName, SpawnedCount);
}

void ABenchEncounter::WatchAlarm()
{
	UWorld* const World = GetWorld();
	if (!World || !bRunning || !bReinforcementOwed)
	{
		if (World)
		{
			World->GetTimerManager().ClearTimer(AlarmTimer);
		}
		return;
	}

	const float Now = World->GetTimeSeconds();
	const int32 Left = GetAliveCount();

	if (AlarmTime < 0.0f)
	{
		// The alarm: somebody of the garrison is hurt, is shooting, or is dead.
		bool bAlarm = Left < SpawnedCount;
		for (const TWeakObjectPtr<AShooterNPC>& Weak : Alive)
		{
			const AShooterNPC* const NPC = Weak.Get();
			if (bAlarm || !NPC || NPC->IsDead())
			{
				continue;
			}
			bAlarm = NPC->IsCurrentlyShooting() || NPC->CurrentHP < NPC->GetMaxHealth() - 0.5f;
		}
		if (!bAlarm)
		{
			return;
		}
		AlarmTime = Now;
		UE_LOG(LogTemp, Log, TEXT("[BENCH] %s: ALARM at %.1f s, reinforcement in %.0f s"), *EncounterName, Now - StartTime, ReinforcementDelay);
		UFightRecorderSubsystem::NoteMark(this, FString::Printf(TEXT("ALARM; reinforcement in %.0f s"), ReinforcementDelay));
	}

	const float Remaining = ReinforcementDelay - (Now - AlarmTime);
	if (Remaining > 0.0f)
	{
		SetStatus(FString::Printf(TEXT("ALARM: %d alive, reinforcement in %.0f s"), Left, Remaining));
		return;
	}

	int32 Came = 0;
	for (const FBenchEncounterGroup& Group : Groups)
	{
		if (Group.bReinforcement)
		{
			Came += SpawnGroup(Group);
		}
	}
	SpawnedCount += Came;
	bReinforcementOwed = false;
	bReinforcementCame = true;
	World->GetTimerManager().ClearTimer(AlarmTimer);

	SetStatus(FString::Printf(TEXT("REINFORCEMENT CAME: %d alive, shoot to clear"), GetAliveCount()));
	UE_LOG(LogTemp, Log, TEXT("[BENCH] %s: reinforcement came, %d of them, %d of the garrison were alive"), *EncounterName, Came, Left);
	UFightRecorderSubsystem::NoteMark(this, FString::Printf(TEXT("REINFORCEMENT CAME; %d of them; %d of the garrison alive"), Came, Left));
}

void ABenchEncounter::ClearEncounter()
{
	if (!HasAuthority())
	{
		return;
	}

	int32 Removed = 0;
	for (const TWeakObjectPtr<AShooterNPC>& Weak : Alive)
	{
		if (AShooterNPC* const NPC = Weak.Get())
		{
			NPC->OnNPCDeath.RemoveDynamic(this, &ABenchEncounter::HandleDeath);
			NPC->Destroy();
			++Removed;
		}
	}
	Alive.Reset();
	GetWorldTimerManager().ClearTimer(AlarmTimer);
	bReinforcementOwed = false;
	if (bRunning)
	{
		UE_LOG(LogTemp, Log, TEXT("[BENCH] %s cleared by hand, %d removed"), *EncounterName, Removed);
		if (UFightRecorderSubsystem* const Recorder = UFightRecorderSubsystem::Get(this))
		{
			Recorder->EndFight(EncounterName, FString::Printf(TEXT("cleared by hand, %d were alive"), Removed));
		}
	}
	if (bPlayersOnlyWatch && Removed > 0)
	{
		SetPlayersIgnored(false);
	}
	bRunning = false;
	SetStatus(TEXT("shoot to start"));
}

void ABenchEncounter::SetPlayersIgnored(bool bIgnored)
{
	if (IConsoleVariable* const Switch = IConsoleManager::Get().FindConsoleVariable(TEXT("polarity.ai.IgnorePlayers")))
	{
		// SetByConsole, not SetByCode: a value typed into the console outranks one set by code, and
		// the engine then drops this write with a warning. Seen 2026-10-12: the switch typed on once,
		// and every fight after it started with a garrison that would not look at the player.
		Switch->Set(bIgnored ? 1 : 0, ECVF_SetByConsole);
		UE_LOG(LogTemp, Log, TEXT("[BENCH] %s: polarity.ai.IgnorePlayers %d"), *EncounterName, bIgnored ? 1 : 0);
	}
}

void ABenchEncounter::HandleDeath(AShooterNPC* DeadNPC)
{
	if (!bRunning)
	{
		return;
	}

	// The dying one may still read as alive inside its own death broadcast.
	int32 Left = 0;
	TArray<uint8, TInlineAllocator<4>> SidesLeft;
	for (const TWeakObjectPtr<AShooterNPC>& NPC : Alive)
	{
		if (NPC.IsValid() && NPC.Get() != DeadNPC && !NPC->IsDead())
		{
			++Left;
			SidesLeft.AddUnique(NPC->GetGenericTeamId().GetId());
		}
	}

	const float Took = GetWorld() ? GetWorld()->GetTimeSeconds() - StartTime : 0.0f;

	// A versus is over when one side is left. The winners stay where they stand (the button clears
	// them), so the end of the fight can be looked at.
	if (bVersus && Left > 0 && SidesLeft.Num() == 1)
	{
		bRunning = false;
		SetStatus(FString::Printf(TEXT("side %d won in %.1f s, %d left, shoot to clear"), SidesLeft[0], Took, Left));
		UE_LOG(LogTemp, Log, TEXT("[BENCH] %s done: side %d won in %.1f s, %d of %d left"),
			*EncounterName, SidesLeft[0], Took, Left, SpawnedCount);
		if (UFightRecorderSubsystem* const Recorder = UFightRecorderSubsystem::Get(this))
		{
			Recorder->EndFight(EncounterName, FString::Printf(TEXT("side %d won, %d of %d left"), SidesLeft[0], Left, SpawnedCount));
		}
		return;
	}

	if (Left > 0)
	{
		SetStatus(FString::Printf(TEXT("%d alive, shoot to clear"), Left));
		return;
	}

	if (bPlayersOnlyWatch)
	{
		SetPlayersIgnored(false);
	}
	bRunning = false;
	Alive.Reset();
	SetStatus(FString::Printf(TEXT("done in %.1f s, shoot to run again"), Took));
	UE_LOG(LogTemp, Log, TEXT("[BENCH] %s done: %d enemies in %.1f s"), *EncounterName, SpawnedCount, Took);

	// An outpost: was it taken before the reinforcement, or after it came?
	FString How = FString::Printf(TEXT("all %d dead"), SpawnedCount);
	if (bReinforcementOwed)
	{
		How += AlarmTime >= 0.0f
			? FString::Printf(TEXT("; taken %.1f s after the alarm; before the reinforcement"), GetWorld()->GetTimeSeconds() - AlarmTime)
			: FString(TEXT("; taken with no alarm"));
		SetStatus(FString::Printf(TEXT("TAKEN in %.1f s, before the reinforcement. Shoot to run again"), Took));
	}
	else if (bReinforcementCame)
	{
		How += TEXT("; the reinforcement came");
	}
	GetWorldTimerManager().ClearTimer(AlarmTimer);
	bReinforcementOwed = false;

	if (UFightRecorderSubsystem* const Recorder = UFightRecorderSubsystem::Get(this))
	{
		Recorder->EndFight(EncounterName, How);
	}
}

// ==================== Console ====================

namespace BenchDebug
{
	static void Run(const TArray<FString>& Args, UWorld* World, bool bStart)
	{
		if (!World)
		{
			return;
		}
		const FString Wanted = Args.Num() > 0 ? FString::Join(Args, TEXT(" ")) : FString();
		int32 Hits = 0;
		for (TActorIterator<ABenchEncounter> It(World); It; ++It)
		{
			if (!bStart && Wanted.IsEmpty())
			{
				It->ClearEncounter();
				++Hits;
			}
			else if (It->EncounterName.Equals(Wanted, ESearchCase::IgnoreCase))
			{
				bStart ? It->StartEncounter() : It->ClearEncounter();
				++Hits;
			}
		}
		UE_LOG(LogTemp, Log, TEXT("[BENCH] %s '%s': %d encounter(s)"), bStart ? TEXT("start") : TEXT("clear"), *Wanted, Hits);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GCmdBenchStart(
	TEXT("polarity.bench.start"),
	TEXT("Start a bench encounter by its name, as its button would. Usage: polarity.bench.start <name>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { BenchDebug::Run(Args, World, true); }));

static FAutoConsoleCommandWithWorldAndArgs GCmdBenchClear(
	TEXT("polarity.bench.clear"),
	TEXT("Clear a bench encounter by name, or every one with no name. Usage: polarity.bench.clear [name]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { BenchDebug::Run(Args, World, false); }));
