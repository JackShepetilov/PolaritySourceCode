// ShooterDoor.cpp

#include "ShooterDoor.h"
#include "Components/BoxComponent.h"
#include "ShooterKey.h"
#include "ShooterCharacter.h"
#include "Checkpoint/CheckpointSubsystem.h"

AShooterDoor::AShooterDoor()
{
	PrimaryActorTick.bCanEverTick = false;

	// Create root component
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Create key detection box
	KeyDetectionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("KeyDetectionBox"));
	KeyDetectionBox->SetupAttachment(Root);
	KeyDetectionBox->SetBoxExtent(KeyBoxExtent);
	KeyDetectionBox->SetRelativeLocation(KeyBoxOffset);
	KeyDetectionBox->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	KeyDetectionBox->SetGenerateOverlapEvents(true);
	KeyDetectionBox->SetHiddenInGame(true);
	KeyDetectionBox->ShapeColor = FColor::Yellow;

	// Create player detection box
	PlayerDetectionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("PlayerDetectionBox"));
	PlayerDetectionBox->SetupAttachment(Root);
	PlayerDetectionBox->SetBoxExtent(PlayerBoxExtent);
	PlayerDetectionBox->SetRelativeLocation(PlayerBoxOffset);
	PlayerDetectionBox->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	PlayerDetectionBox->SetGenerateOverlapEvents(true);
	PlayerDetectionBox->SetHiddenInGame(true);
	PlayerDetectionBox->ShapeColor = FColor::Cyan;
}

void AShooterDoor::BeginPlay()
{
	Super::BeginPlay();

	// Cache checkpoint subsystem
	CheckpointSubsystem = GetWorld()->GetSubsystem<UCheckpointSubsystem>();

	// Bind to checkpoint events
	if (CheckpointSubsystem)
	{
		CheckpointSubsystem->OnCheckpointActivated.AddDynamic(this, &AShooterDoor::OnCheckpointActivated);
		CheckpointSubsystem->OnPlayerRespawned.AddDynamic(this, &AShooterDoor::OnPlayerRespawned);
	}

	// Bind key detection box overlaps
	KeyDetectionBox->OnComponentBeginOverlap.AddDynamic(this, &AShooterDoor::OnKeyBoxBeginOverlap);
	KeyDetectionBox->OnComponentEndOverlap.AddDynamic(this, &AShooterDoor::OnKeyBoxEndOverlap);

	// Bind player detection box overlaps
	PlayerDetectionBox->OnComponentBeginOverlap.AddDynamic(this, &AShooterDoor::OnPlayerBoxBeginOverlap);
	PlayerDetectionBox->OnComponentEndOverlap.AddDynamic(this, &AShooterDoor::OnPlayerBoxEndOverlap);

	// Save initial state as checkpoint state
	bStateAtCheckpoint = bIsOpen;

	// Do initial scan for keys after a short delay
	FTimerHandle InitialScanTimer;
	GetWorld()->GetTimerManager().SetTimer(InitialScanTimer, this, &AShooterDoor::RescanForKeys, 0.1f, false);
}

void AShooterDoor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unbind from checkpoint subsystem
	if (CheckpointSubsystem)
	{
		CheckpointSubsystem->OnCheckpointActivated.RemoveDynamic(this, &AShooterDoor::OnCheckpointActivated);
		CheckpointSubsystem->OnPlayerRespawned.RemoveDynamic(this, &AShooterDoor::OnPlayerRespawned);
	}

	// Unbind from tracked key
	if (AShooterKey* Key = TrackedKey.Get())
	{
		Key->OnDummyDeath.RemoveDynamic(this, &AShooterDoor::HandleKeyDeath);
	}

	Super::EndPlay(EndPlayReason);
}

void AShooterDoor::OpenDoor()
{
	if (!bIsOpen)
	{
		bIsOpen = true;
		OnDoorOpened.Broadcast();
	}
}

void AShooterDoor::CloseDoor()
{
	if (bIsOpen)
	{
		bIsOpen = false;
		OnDoorClosed.Broadcast();
	}
}

void AShooterDoor::ToggleDoor()
{
	if (bIsOpen)
	{
		CloseDoor();
	}
	else
	{
		OpenDoor();
	}
}

bool AShooterDoor::IsKeyAlive() const
{
	AShooterKey* Key = TrackedKey.Get();
	return Key && !Key->IsDead();
}

int32 AShooterDoor::GetAliveKeyCount() const
{
	// Currently single key, but architecture supports multiple
	return IsKeyAlive() ? 1 : 0;
}

void AShooterDoor::UpdateKeyDetectionBox()
{
	if (KeyDetectionBox)
	{
		KeyDetectionBox->SetBoxExtent(KeyBoxExtent);
		KeyDetectionBox->SetRelativeLocation(KeyBoxOffset);
		RescanForKeys();
	}
}

void AShooterDoor::UpdatePlayerDetectionBox()
{
	if (PlayerDetectionBox)
	{
		PlayerDetectionBox->SetBoxExtent(PlayerBoxExtent);
		PlayerDetectionBox->SetRelativeLocation(PlayerBoxOffset);
	}
}

// ==================== Key Detection Callbacks ====================

void AShooterDoor::OnKeyBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	AShooterKey* Key = Cast<AShooterKey>(OtherActor);
	if (!Key)
	{
		return;
	}

	StartTrackingKey(Key);
}

void AShooterDoor::OnKeyBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	AShooterKey* Key = Cast<AShooterKey>(OtherActor);
	if (!Key)
	{
		return;
	}

	// Don't unbind if key is dead - we still want to receive the death event
	// The EndOverlap fires when collision is disabled in Die(), before OnDummyDeath broadcasts
	if (Key->IsDead())
	{
		UE_LOG(LogTemp, Log, TEXT("ShooterDoor::OnKeyBoxEndOverlap - Key %s is dead, keeping binding"), *Key->GetName());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("ShooterDoor::OnKeyBoxEndOverlap - Key %s left detection box"), *Key->GetName());
	StopTrackingKey(Key);
}

// ==================== Player Detection Callbacks ====================

void AShooterDoor::OnPlayerBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	AShooterCharacter* Player = Cast<AShooterCharacter>(OtherActor);
	if (!Player)
	{
		return;
	}

	bIsPlayerInside = true;
	OnPlayerEntered.Broadcast(Player);
}

void AShooterDoor::OnPlayerBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	AShooterCharacter* Player = Cast<AShooterCharacter>(OtherActor);
	if (!Player)
	{
		return;
	}

	bIsPlayerInside = false;
	OnPlayerExited.Broadcast(Player);
}

// ==================== Key Event Handlers ====================

void AShooterDoor::HandleKeyDeath(AShooterDummy* Dummy, AActor* Killer)
{
	UE_LOG(LogTemp, Log, TEXT("ShooterDoor::HandleKeyDeath - Called! Dummy: %s, Killer: %s"),
		Dummy ? *Dummy->GetName() : TEXT("null"),
		Killer ? *Killer->GetName() : TEXT("null"));

	AShooterKey* Key = Cast<AShooterKey>(Dummy);
	if (!Key)
	{
		UE_LOG(LogTemp, Warning, TEXT("ShooterDoor::HandleKeyDeath - Cast to AShooterKey failed!"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("ShooterDoor::HandleKeyDeath - Broadcasting OnKeyDeath for: %s"), *Key->GetName());
	// Broadcast key death event
	OnKeyDeath.Broadcast(Key);
}

// ==================== Checkpoint Handlers ====================

void AShooterDoor::OnCheckpointActivated(const FCheckpointData& CheckpointData)
{
	// Save current door state for respawn
	bStateAtCheckpoint = bIsOpen;
}

void AShooterDoor::OnPlayerRespawned()
{
	// Restore door state from checkpoint
	const bool bPreviousState = bIsOpen;
	bIsOpen = bStateAtCheckpoint;

	// Broadcast state change if it changed
	if (bPreviousState != bIsOpen)
	{
		if (bIsOpen)
		{
			OnDoorOpened.Broadcast();
		}
		else
		{
			OnDoorClosed.Broadcast();
		}
	}

	// Clear tracked key - it will be respawned
	if (AShooterKey* Key = TrackedKey.Get())
	{
		Key->OnDummyDeath.RemoveDynamic(this, &AShooterDoor::HandleKeyDeath);
	}
	TrackedKey.Reset();

	// Rescan for keys after NPCs respawn
	FTimerHandle RescanTimer;
	GetWorld()->GetTimerManager().SetTimer(RescanTimer, this, &AShooterDoor::RescanForKeys, 0.3f, false);
}

void AShooterDoor::RescanForKeys()
{
	if (!KeyDetectionBox)
	{
		UE_LOG(LogTemp, Warning, TEXT("ShooterDoor::RescanForKeys - KeyDetectionBox is null!"));
		return;
	}

	// Get all overlapping actors
	TArray<AActor*> OverlappingActors;
	KeyDetectionBox->GetOverlappingActors(OverlappingActors, AShooterKey::StaticClass());

	UE_LOG(LogTemp, Log, TEXT("ShooterDoor::RescanForKeys - Found %d overlapping keys"), OverlappingActors.Num());

	for (AActor* Actor : OverlappingActors)
	{
		AShooterKey* Key = Cast<AShooterKey>(Actor);
		if (Key && !Key->IsDead())
		{
			UE_LOG(LogTemp, Log, TEXT("ShooterDoor::RescanForKeys - Tracking key: %s"), *Key->GetName());
			StartTrackingKey(Key);

			// Check if this is a known key that respawned
			TWeakObjectPtr<AShooterKey> WeakKey(Key);
			if (KnownKeys.Contains(WeakKey))
			{
				OnKeyRespawned.Broadcast(Key);
			}
		}
	}
}

void AShooterDoor::StartTrackingKey(AShooterKey* Key)
{
	if (!Key)
	{
		UE_LOG(LogTemp, Warning, TEXT("ShooterDoor::StartTrackingKey - Key is null!"));
		return;
	}

	// Skip if already tracking this key
	if (TrackedKey.Get() == Key)
	{
		UE_LOG(LogTemp, Log, TEXT("ShooterDoor::StartTrackingKey - Already tracking key: %s"), *Key->GetName());
		return;
	}

	// Unbind from previous key if any
	if (AShooterKey* OldKey = TrackedKey.Get())
	{
		UE_LOG(LogTemp, Log, TEXT("ShooterDoor::StartTrackingKey - Unbinding from old key: %s"), *OldKey->GetName());
		OldKey->OnDummyDeath.RemoveDynamic(this, &AShooterDoor::HandleKeyDeath);
	}

	// Track new key
	TrackedKey = Key;
	KnownKeys.Add(Key);

	// Bind to death event
	Key->OnDummyDeath.AddDynamic(this, &AShooterDoor::HandleKeyDeath);
	UE_LOG(LogTemp, Log, TEXT("ShooterDoor::StartTrackingKey - Now tracking key: %s, bound to OnDummyDeath"), *Key->GetName());
}

void AShooterDoor::StopTrackingKey(AShooterKey* Key)
{
	if (!Key)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("ShooterDoor::StopTrackingKey - Called for key: %s"), *Key->GetName());

	if (TrackedKey.Get() != Key)
	{
		UE_LOG(LogTemp, Log, TEXT("ShooterDoor::StopTrackingKey - Key not tracked, ignoring"));
		return;
	}

	// Unbind death event
	UE_LOG(LogTemp, Warning, TEXT("ShooterDoor::StopTrackingKey - UNBINDING from key: %s"), *Key->GetName());
	Key->OnDummyDeath.RemoveDynamic(this, &AShooterDoor::HandleKeyDeath);

	// Clear tracked key (but keep in KnownKeys for respawn detection)
	TrackedKey.Reset();
}
