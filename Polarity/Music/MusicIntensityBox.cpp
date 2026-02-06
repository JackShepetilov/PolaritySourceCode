// MusicIntensityBox.cpp

#include "MusicIntensityBox.h"
#include "MusicPlayerSubsystem.h"
#include "MusicTrackDataAsset.h"
#include "Components/BoxComponent.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY(LogMusicIntensityBox);

AMusicIntensityBox::AMusicIntensityBox()
{
	PrimaryActorTick.bCanEverTick = false;

	// Create trigger box
	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	RootComponent = TriggerBox;

	TriggerBox->SetBoxExtent(BoxExtent);
	TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerBox->SetGenerateOverlapEvents(true);

	// Editor visualization
	TriggerBox->SetHiddenInGame(true);
	TriggerBox->ShapeColor = FColor::Orange;
}

void AMusicIntensityBox::BeginPlay()
{
	Super::BeginPlay();

	// Cache music subsystem
	UGameInstance* GI = GetGameInstance();
	if (GI)
	{
		MusicSubsystem = GI->GetSubsystem<UMusicPlayerSubsystem>();
	}

	if (!MusicSubsystem)
	{
		LogWarning(TEXT("MusicPlayerSubsystem not found!"));
	}

	// Validate track
	if (!MusicTrack)
	{
		LogWarning(FString::Printf(TEXT("No MusicTrack assigned to %s"), *GetName()));
	}
	else if (!MusicTrack->IsValid())
	{
		LogWarning(FString::Printf(TEXT("MusicTrack '%s' is invalid"), *MusicTrack->TrackName));
	}

	// Bind overlap events
	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &AMusicIntensityBox::OnBoxBeginOverlap);
	TriggerBox->OnComponentEndOverlap.AddDynamic(this, &AMusicIntensityBox::OnBoxEndOverlap);

	// Do initial enemy scan after a short delay
	FTimerHandle InitialScanTimer;
	GetWorld()->GetTimerManager().SetTimer(
		InitialScanTimer,
		this,
		&AMusicIntensityBox::RefreshEnemyDetection,
		0.1f,
		false
	);

	LogDebug(FString::Printf(TEXT("MusicIntensityBox '%s' initialized (Track: %s)"),
		*GetName(),
		MusicTrack ? *MusicTrack->TrackName : TEXT("NONE")));
}

void AMusicIntensityBox::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unbind from all tracked NPCs
	for (const TWeakObjectPtr<AShooterNPC>& NPCPtr : TrackedEnemies)
	{
		if (AShooterNPC* NPC = NPCPtr.Get())
		{
			NPC->OnNPCDeath.RemoveDynamic(this, &AMusicIntensityBox::OnTrackedNPCDeath);
		}
	}
	TrackedEnemies.Empty();

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void AMusicIntensityBox::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Update box extent if changed in editor
	if (PropertyChangedEvent.Property &&
		PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AMusicIntensityBox, BoxExtent))
	{
		if (TriggerBox)
		{
			TriggerBox->SetBoxExtent(BoxExtent);
		}
	}
}
#endif

void AMusicIntensityBox::Reactivate()
{
	LogDebug(FString::Printf(TEXT("Reactivating MusicIntensityBox '%s'"), *GetName()));

	bIsActive = true;
	bIsFirstMusicEntry = true;

	// Rebuild enemy tracking
	RefreshEnemyDetection();
}

void AMusicIntensityBox::RefreshEnemyDetection()
{
	RebuildTrackedEnemies();
}

// ==================== Overlap Handlers ====================

void AMusicIntensityBox::OnBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Check for player
	if (AShooterCharacter* Player = Cast<AShooterCharacter>(OtherActor))
	{
		OnPlayerEntered();
		return;
	}

	// Check for NPC
	if (AShooterNPC* NPC = Cast<AShooterNPC>(OtherActor))
	{
		if (!NPC->IsDead())
		{
			StartTrackingNPC(NPC);
		}
	}
}

void AMusicIntensityBox::OnBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	// Check for player
	if (AShooterCharacter* Player = Cast<AShooterCharacter>(OtherActor))
	{
		OnPlayerExited();
		return;
	}

	// Check for NPC
	if (AShooterNPC* NPC = Cast<AShooterNPC>(OtherActor))
	{
		StopTrackingNPC(NPC);
	}
}

// ==================== Player Handling ====================

void AMusicIntensityBox::OnPlayerEntered()
{
	if (!bIsActive)
	{
		LogDebug(FString::Printf(TEXT("Player entered inactive MIB '%s' - ignoring"), *GetName()));
		return;
	}

	bPlayerInside = true;

	LogDebug(FString::Printf(TEXT("=== Player ENTERED MIB '%s' ==="), *GetName()));
	LogDebug(FString::Printf(TEXT("  FirstEntry: %s"), bIsFirstMusicEntry ? TEXT("YES") : TEXT("NO")));
	LogDebug(FString::Printf(TEXT("  TrackedEnemies: %d"), TrackedEnemies.Num()));

	if (!MusicSubsystem)
	{
		LogWarning(TEXT("No MusicSubsystem - cannot start music"));
		return;
	}

	if (!MusicTrack)
	{
		LogWarning(TEXT("No MusicTrack assigned - cannot start music"));
		return;
	}

	// Check if music is already playing (from another MIB)
	if (MusicSubsystem->IsPlaying())
	{
		// Just set intense zone, music continues
		LogDebug(TEXT("Music already playing - just setting intense zone"));
		MusicSubsystem->SetIntenseZone(true);
	}
	else
	{
		// Start new track
		bool bShouldFadeIn = bIsFirstMusicEntry;
		LogDebug(FString::Printf(TEXT("Starting track '%s' (FadeIn: %s)"),
			*MusicTrack->TrackName,
			bShouldFadeIn ? TEXT("YES") : TEXT("NO")));

		MusicSubsystem->StartTrack(MusicTrack, bShouldFadeIn);
		bIsFirstMusicEntry = false;
	}
}

void AMusicIntensityBox::OnPlayerExited()
{
	if (!bPlayerInside)
	{
		return;
	}

	bPlayerInside = false;

	LogDebug(FString::Printf(TEXT("=== Player EXITED MIB '%s' ==="), *GetName()));

	if (MusicSubsystem && bIsActive)
	{
		// Switch to calm mode (music continues at lower volume)
		MusicSubsystem->SetIntenseZone(false);
	}
}

// ==================== Enemy Tracking ====================

void AMusicIntensityBox::StartTrackingNPC(AShooterNPC* NPC)
{
	if (!NPC)
	{
		return;
	}

	TWeakObjectPtr<AShooterNPC> WeakNPC(NPC);
	if (TrackedEnemies.Contains(WeakNPC))
	{
		return;
	}

	TrackedEnemies.Add(WeakNPC);
	NPC->OnNPCDeath.AddDynamic(this, &AMusicIntensityBox::OnTrackedNPCDeath);

	LogDebug(FString::Printf(TEXT("Started tracking NPC '%s' (Total: %d)"),
		*NPC->GetName(), TrackedEnemies.Num()));

	UpdateActiveState();
}

void AMusicIntensityBox::StopTrackingNPC(AShooterNPC* NPC)
{
	if (!NPC)
	{
		return;
	}

	TWeakObjectPtr<AShooterNPC> WeakNPC(NPC);
	if (!TrackedEnemies.Contains(WeakNPC))
	{
		return;
	}

	NPC->OnNPCDeath.RemoveDynamic(this, &AMusicIntensityBox::OnTrackedNPCDeath);
	TrackedEnemies.Remove(WeakNPC);

	LogDebug(FString::Printf(TEXT("Stopped tracking NPC '%s' (Total: %d)"),
		*NPC->GetName(), TrackedEnemies.Num()));

	UpdateActiveState();
}

void AMusicIntensityBox::OnTrackedNPCDeath(AShooterNPC* DeadNPC)
{
	LogDebug(FString::Printf(TEXT("Tracked NPC '%s' died"), DeadNPC ? *DeadNPC->GetName() : TEXT("null")));
	StopTrackingNPC(DeadNPC);
}

void AMusicIntensityBox::RebuildTrackedEnemies()
{
	// Unbind from all current NPCs
	for (const TWeakObjectPtr<AShooterNPC>& NPCPtr : TrackedEnemies)
	{
		if (AShooterNPC* NPC = NPCPtr.Get())
		{
			NPC->OnNPCDeath.RemoveDynamic(this, &AMusicIntensityBox::OnTrackedNPCDeath);
		}
	}
	TrackedEnemies.Empty();

	// Scan for overlapping NPCs
	TArray<AActor*> OverlappingActors;
	TriggerBox->GetOverlappingActors(OverlappingActors, AShooterNPC::StaticClass());

	for (AActor* Actor : OverlappingActors)
	{
		AShooterNPC* NPC = Cast<AShooterNPC>(Actor);
		if (NPC && !NPC->IsDead())
		{
			TWeakObjectPtr<AShooterNPC> WeakNPC(NPC);
			TrackedEnemies.Add(WeakNPC);
			NPC->OnNPCDeath.AddDynamic(this, &AMusicIntensityBox::OnTrackedNPCDeath);
		}
	}

	LogDebug(FString::Printf(TEXT("RebuildTrackedEnemies: Found %d enemies"), TrackedEnemies.Num()));

	UpdateActiveState();
}

void AMusicIntensityBox::UpdateActiveState()
{
	// Clean up invalid weak pointers
	for (auto It = TrackedEnemies.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent();
		}
	}

	const int32 EnemyCount = TrackedEnemies.Num();

	// Deactivate if no enemies left AND player was inside
	if (EnemyCount == 0 && bIsActive && bPlayerInside)
	{
		LogDebug(FString::Printf(TEXT("All enemies cleared in MIB '%s' - deactivating"), *GetName()));
		bIsActive = false;

		if (MusicSubsystem)
		{
			MusicSubsystem->OnEnemiesCleared();
		}
	}
}

// ==================== Debug ====================

void AMusicIntensityBox::LogDebug(const FString& Message) const
{
	UE_LOG(LogMusicIntensityBox, Log, TEXT("[MIB:%s] %s"), *GetName(), *Message);
}

void AMusicIntensityBox::LogWarning(const FString& Message) const
{
	UE_LOG(LogMusicIntensityBox, Warning, TEXT("[MIB:%s] %s"), *GetName(), *Message);
}
