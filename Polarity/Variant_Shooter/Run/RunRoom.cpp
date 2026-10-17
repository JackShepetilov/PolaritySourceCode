// RunRoom.cpp

#include "Variant_Shooter/Run/RunRoom.h"

#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Coop/CoopPlayers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "Variant_Shooter/Bench/BenchEncounter.h"
#include "Variant_Shooter/Bench/FightRecorder.h"
#include "Variant_Shooter/ShooterCharacter.h"

ARunRoom::ARunRoom()
{
	PrimaryActorTick.bCanEverTick = false;

	// Only the door state travels, and it has to reach a player wherever they stand.
	bReplicates = true;
	bAlwaysRelevant = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	EntryZone = CreateDefaultSubobject<UBoxComponent>(TEXT("EntryZone"));
	EntryZone->SetupAttachment(SceneRoot);
	EntryZone->SetBoxExtent(EntryExtent);
	EntryZone->SetCollisionProfileName(TEXT("Trigger"));
	EntryZone->SetGenerateOverlapEvents(true);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(SceneRoot);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextBottom);
	Label->SetWorldSize(40.0f);
	Label->SetTextRenderColor(FColor(140, 255, 160));
	Label->SetText(FText::FromString(TEXT("ROOM")));
}

void ARunRoom::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (EntryZone)
	{
		EntryZone->SetBoxExtent(EntryExtent);
	}
	if (Label)
	{
		Label->SetRelativeLocation(FVector(0.0f, 0.0f, EntryExtent.Z + 40.0f));
	}
	SetStatus(Encounter ? TEXT("waiting") : TEXT("no fight, open"));
}

void ARunRoom::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		EntryZone->OnComponentBeginOverlap.AddDynamic(this, &ARunRoom::HandleEntryOverlap);
		if (Encounter)
		{
			Encounter->OnEncounterDone.AddDynamic(this, &ARunRoom::HandleEncounterDone);
		}
		bExitOpen = Encounter == nullptr;
	}
	ApplyDoors();
	SetStatus(bExitOpen ? TEXT("no fight, open") : TEXT("waiting"));
}

void ARunRoom::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARunRoom, bExitOpen);
}

void ARunRoom::OnRep_ExitOpen()
{
	ApplyDoors();
}

void ARunRoom::ApplyDoors()
{
	// Each machine hides its own copy of the blocks: they are plain level geometry and do not
	// replicate, only this flag does.
	for (AActor* const Door : ExitDoors)
	{
		if (Door)
		{
			Door->SetActorHiddenInGame(bExitOpen);
			Door->SetActorEnableCollision(!bExitOpen);
		}
	}
}

void ARunRoom::SetStatus(const FString& Status)
{
	if (Label)
	{
		Label->SetText(FText::FromString(FString::Printf(TEXT("ROOM %d %s\n%s"), RoomIndex, *RoomName, *Status)));
	}
}

bool ARunRoom::IsFighting() const
{
	return bFightStarted && !bExitOpen && Encounter && Encounter->IsRunning();
}

void ARunRoom::ResetRoom()
{
	if (!HasAuthority())
	{
		return;
	}
	if (Encounter)
	{
		Encounter->ClearEncounter();
	}
	bFightStarted = false;
	bExitOpen = Encounter == nullptr;
	ApplyDoors();
	SetStatus(bExitOpen ? TEXT("no fight, open") : TEXT("waiting"));
}

void ARunRoom::StartFight()
{
	if (!HasAuthority() || bFightStarted || !Encounter)
	{
		return;
	}
	bFightStarted = true;
	Encounter->StartEncounter();
	SetStatus(TEXT("FIGHT"));
	UE_LOG(LogTemp, Log, TEXT("[RUN] room %d %s: fight, %d enemies"), RoomIndex, *RoomName, Encounter->GetAliveCount());
	UFightRecorderSubsystem::NoteMark(this, FString::Printf(TEXT("RUN room %d %s entered"), RoomIndex, *RoomName));
}

void ARunRoom::OpenExit()
{
	if (!HasAuthority())
	{
		return;
	}
	bExitOpen = true;
	ApplyDoors();

	UWorld* const World = GetWorld();
	if (bLastRoom && RunStartTime >= 0.0f && World)
	{
		const float Took = World->GetTimeSeconds() - RunStartTime;
		SetStatus(FString::Printf(TEXT("RUN COMPLETE in %.0f s"), Took));
		UE_LOG(LogTemp, Log, TEXT("[RUN] COMPLETE: room %d %s cleared, run took %.1f s"), RoomIndex, *RoomName, Took);
		UFightRecorderSubsystem::NoteMark(this, FString::Printf(TEXT("RUN COMPLETE in %.1f s"), Took));
		RunStartTime = -1.0f;
		return;
	}
	SetStatus(TEXT("CLEARED, door open"));
	UE_LOG(LogTemp, Log, TEXT("[RUN] room %d %s: cleared, door open"), RoomIndex, *RoomName);
}

void ARunRoom::HandleEntryOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (CoopPlayers::IsPlayer(OtherActor))
	{
		StartFight();
	}
}

void ARunRoom::HandleEncounterDone(ABenchEncounter* Done)
{
	if (bFightStarted && !bExitOpen)
	{
		OpenExit();
	}
}

bool ARunRoom::StartRunAt(UWorld* World, int32 Index)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return false;
	}

	ARunRoom* Target = nullptr;
	for (TActorIterator<ARunRoom> It(World); It; ++It)
	{
		if (It->RoomIndex == Index)
		{
			Target = *It;
		}
	}
	if (!Target || !Target->StartPoint)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RUN] no room %d with a start point"), Index);
		return false;
	}

	const float Now = World->GetTimeSeconds();
	for (TActorIterator<ARunRoom> It(World); It; ++It)
	{
		It->ResetRoom();
		It->RunStartTime = Now;
		if (It->RoomIndex < Index)
		{
			It->bFightStarted = true;
			It->bExitOpen = true;
			It->ApplyDoors();
			It->SetStatus(TEXT("skipped, door open"));
		}
	}

	// The whole team, in a row across the way the point faces, 1.2 m apart. A dead one is left to
	// the bench's own respawn.
	TArray<APawn*> Players;
	CoopPlayers::GetAll(World, Players);
	const FRotator Facing = Target->StartPoint->GetActorRotation();
	const FVector Across = FRotationMatrix(Facing).GetUnitAxis(EAxis::Y);
	int32 Placed = 0;
	for (APawn* const Pawn : Players)
	{
		const AShooterCharacter* const Character = Cast<AShooterCharacter>(Pawn);
		if (!Pawn || (Character && Character->IsDead()))
		{
			continue;
		}
		const float Offset = (Placed - (Players.Num() - 1) * 0.5f) * 120.0f;
		Pawn->TeleportTo(Target->StartPoint->GetActorLocation() + Across * Offset, Facing, /*bIsATest*/ false, /*bNoCheck*/ true);
		if (AController* const Controller = Pawn->GetController())
		{
			Controller->SetControlRotation(Facing);
		}
		++Placed;
	}

	UE_LOG(LogTemp, Log, TEXT("[RUN] start at room %d %s: %d players"), Index, *Target->RoomName, Placed);
	UFightRecorderSubsystem::NoteMark(Target, FString::Printf(TEXT("RUN START at room %d"), Index));
	Target->StartFight();
	return true;
}

// ==================== Console ====================

static FAutoConsoleCommandWithWorldAndArgs GCmdRunRoom(
	TEXT("polarity.run.room"),
	TEXT("Start the run skeleton at room N: rooms reset, the ones before N open, the team put down at N. Usage: polarity.run.room <n>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		ARunRoom::StartRunAt(World, Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 1);
	}));

static FAutoConsoleCommandWithWorldAndArgs GCmdRunOpen(
	TEXT("polarity.run.open"),
	TEXT("Open the door of every run room whose fight is on, and remove what is left of the fight. For an enemy that got stuck."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			return;
		}
		for (TActorIterator<ARunRoom> It(World); It; ++It)
		{
			if (It->IsFighting())
			{
				if (It->Encounter)
				{
					It->Encounter->ClearEncounter();
				}
				It->OpenExit();
			}
		}
	}));
