// ShooterKey.cpp

#include "ShooterKey.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "AI/ShooterNPC.h"
#include "Checkpoint/CheckpointSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"

AShooterKey::AShooterKey()
{
	// Create primary detection box
	PrimaryDetectionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("PrimaryDetectionBox"));
	PrimaryDetectionBox->SetupAttachment(RootComponent);
	PrimaryDetectionBox->SetBoxExtent(DetectionBoxExtent);
	PrimaryDetectionBox->SetRelativeLocation(DetectionBoxOffset);

	// Only detect overlaps, don't block anything or respond to hits
	PrimaryDetectionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PrimaryDetectionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	PrimaryDetectionBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	PrimaryDetectionBox->SetGenerateOverlapEvents(true);

	// Make detection box visible in editor for easier setup
	PrimaryDetectionBox->SetHiddenInGame(true);
	PrimaryDetectionBox->ShapeColor = FColor::Yellow;
}

void AShooterKey::BeginPlay()
{
	Super::BeginPlay();

	// Cache checkpoint subsystem reference
	CheckpointSubsystem = GetWorld()->GetSubsystem<UCheckpointSubsystem>();

	// Bind to player respawn event
	if (CheckpointSubsystem)
	{
		CheckpointSubsystem->OnPlayerRespawned.AddDynamic(this, &AShooterKey::OnPlayerRespawned);
	}

	// Register primary detection box
	RegisterAdditionalDetectionBox(PrimaryDetectionBox);

	// Initial state - assume invulnerable until we check
	bIsInvulnerable = true;

	// Apply initial material
	ApplyOverlayMaterial();

	// Do initial scan after a short delay to let NPCs spawn
	FTimerHandle InitialScanTimer;
	GetWorld()->GetTimerManager().SetTimer(InitialScanTimer, this, &AShooterKey::RefreshEnemyDetection, 0.1f, false);
}

void AShooterKey::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unbind from checkpoint subsystem
	if (CheckpointSubsystem)
	{
		CheckpointSubsystem->OnPlayerRespawned.RemoveDynamic(this, &AShooterKey::OnPlayerRespawned);
	}

	// Unbind from all tracked NPCs
	for (const TWeakObjectPtr<AShooterNPC>& NPCPtr : TrackedEnemies)
	{
		if (AShooterNPC* NPC = NPCPtr.Get())
		{
			NPC->OnNPCDeath.RemoveDynamic(this, &AShooterKey::OnTrackedNPCDeath);
		}
	}
	TrackedEnemies.Empty();

	// Clear detection boxes
	DetectionBoxes.Empty();

	Super::EndPlay(EndPlayReason);
}

float AShooterKey::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// Detailed debug info for damage source
	FString CauserInfo = TEXT("null");
	FString InstigatorInfo = TEXT("null");
	FString DamageTypeInfo = TEXT("null");

	if (DamageCauser)
	{
		CauserInfo = FString::Printf(TEXT("%s [%s]"), *DamageCauser->GetName(), *DamageCauser->GetClass()->GetName());
		if (AActor* CauserOwner = DamageCauser->GetOwner())
		{
			CauserInfo += FString::Printf(TEXT(" Owner: %s [%s]"), *CauserOwner->GetName(), *CauserOwner->GetClass()->GetName());
		}
	}

	if (EventInstigator)
	{
		InstigatorInfo = FString::Printf(TEXT("%s [%s]"), *EventInstigator->GetName(), *EventInstigator->GetClass()->GetName());
		if (APawn* Pawn = EventInstigator->GetPawn())
		{
			InstigatorInfo += FString::Printf(TEXT(" Pawn: %s [%s]"), *Pawn->GetName(), *Pawn->GetClass()->GetName());
		}
	}

	if (DamageEvent.DamageTypeClass)
	{
		DamageTypeInfo = DamageEvent.DamageTypeClass->GetName();
	}

	UE_LOG(LogTemp, Warning, TEXT("========== ShooterKey::TakeDamage =========="));
	UE_LOG(LogTemp, Warning, TEXT("  Key: %s"), *GetName());
	UE_LOG(LogTemp, Warning, TEXT("  Damage: %.1f"), Damage);
	UE_LOG(LogTemp, Warning, TEXT("  Invulnerable: %s"), bIsInvulnerable ? TEXT("YES") : TEXT("NO"));
	UE_LOG(LogTemp, Warning, TEXT("  IsDead: %s"), IsDead() ? TEXT("YES") : TEXT("NO"));
	UE_LOG(LogTemp, Warning, TEXT("  HP: %.1f / %.1f"), CurrentHP, MaxHP);
	UE_LOG(LogTemp, Warning, TEXT("  EnemyCount: %d (Threshold: %d)"), TrackedEnemies.Num(), EnemyThreshold);
	UE_LOG(LogTemp, Warning, TEXT("  DamageCauser: %s"), *CauserInfo);
	UE_LOG(LogTemp, Warning, TEXT("  EventInstigator: %s"), *InstigatorInfo);
	UE_LOG(LogTemp, Warning, TEXT("  DamageType: %s"), *DamageTypeInfo);
	UE_LOG(LogTemp, Warning, TEXT("  IsPlayerController: %s"), (EventInstigator && EventInstigator->IsPlayerController()) ? TEXT("YES") : TEXT("NO"));

	// Block damage if not from player
	if (!EventInstigator || !EventInstigator->IsPlayerController())
	{
		UE_LOG(LogTemp, Warning, TEXT("  >>> BLOCKED (not from player)"));
		UE_LOG(LogTemp, Warning, TEXT("============================================="));
		return 0.0f;
	}

	// Block damage if invulnerable
	if (bIsInvulnerable)
	{
		UE_LOG(LogTemp, Warning, TEXT("  >>> BLOCKED (invulnerable)"));
		UE_LOG(LogTemp, Warning, TEXT("============================================="));
		return 0.0f;
	}

	UE_LOG(LogTemp, Warning, TEXT("  >>> ALLOWED - Calling Super::TakeDamage"));

	// Print callstack to find where damage comes from
	FString StackTrace = FFrame::GetScriptCallstack();
	if (!StackTrace.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("  Blueprint Callstack:\n%s"), *StackTrace);
	}

	// C++ callstack
	FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);

	// Let parent handle damage
	float ActualDamage = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	UE_LOG(LogTemp, Warning, TEXT("  >>> Applied: %.1f, HP remaining: %.1f"), ActualDamage, CurrentHP);
	UE_LOG(LogTemp, Warning, TEXT("============================================="));

	return ActualDamage;
}

void AShooterKey::RefreshEnemyDetection()
{
	RebuildTrackedEnemies();
}

void AShooterKey::RegisterAdditionalDetectionBox(UBoxComponent* BoxComponent)
{
	if (!BoxComponent)
	{
		return;
	}

	// Check if already registered
	for (const TWeakObjectPtr<UBoxComponent>& ExistingBox : DetectionBoxes)
	{
		if (ExistingBox.Get() == BoxComponent)
		{
			return;
		}
	}

	// Setup overlap callbacks
	SetupBoxOverlapCallbacks(BoxComponent);

	// Add to list
	DetectionBoxes.Add(BoxComponent);

	// Refresh detection to pick up any actors already overlapping
	RefreshEnemyDetection();
}

void AShooterKey::UnregisterDetectionBox(UBoxComponent* BoxComponent)
{
	if (!BoxComponent)
	{
		return;
	}

	// Unbind callbacks
	BoxComponent->OnComponentBeginOverlap.RemoveDynamic(this, &AShooterKey::OnDetectionBoxBeginOverlap);
	BoxComponent->OnComponentEndOverlap.RemoveDynamic(this, &AShooterKey::OnDetectionBoxEndOverlap);

	// Remove from list
	DetectionBoxes.RemoveAll([BoxComponent](const TWeakObjectPtr<UBoxComponent>& Ptr)
	{
		return !Ptr.IsValid() || Ptr.Get() == BoxComponent;
	});

	// Refresh detection - some NPCs might no longer be in any box
	RefreshEnemyDetection();
}

void AShooterKey::UpdatePrimaryDetectionBox()
{
	if (PrimaryDetectionBox)
	{
		PrimaryDetectionBox->SetBoxExtent(DetectionBoxExtent);
		PrimaryDetectionBox->SetRelativeLocation(DetectionBoxOffset);

		// Refresh detection after box resize
		RefreshEnemyDetection();
	}
}

void AShooterKey::SetupBoxOverlapCallbacks(UBoxComponent* BoxComponent)
{
	if (!BoxComponent)
	{
		return;
	}

	BoxComponent->OnComponentBeginOverlap.AddDynamic(this, &AShooterKey::OnDetectionBoxBeginOverlap);
	BoxComponent->OnComponentEndOverlap.AddDynamic(this, &AShooterKey::OnDetectionBoxEndOverlap);
}

void AShooterKey::OnDetectionBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Check if it's a ShooterNPC
	AShooterNPC* NPC = Cast<AShooterNPC>(OtherActor);
	if (!NPC)
	{
		return;
	}

	// Skip dead NPCs
	if (NPC->IsDead())
	{
		return;
	}

	// Start tracking (TSet handles duplicates automatically)
	StartTrackingNPC(NPC);
}

void AShooterKey::OnDetectionBoxEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	AShooterNPC* NPC = Cast<AShooterNPC>(OtherActor);
	if (!NPC)
	{
		return;
	}

	// Check if NPC is still in ANY other detection box
	bool bStillInAnyBox = false;
	for (const TWeakObjectPtr<UBoxComponent>& BoxPtr : DetectionBoxes)
	{
		UBoxComponent* Box = BoxPtr.Get();
		if (!Box || Box == OverlappedComponent)
		{
			continue;
		}

		TArray<AActor*> OverlappingActors;
		Box->GetOverlappingActors(OverlappingActors, AShooterNPC::StaticClass());
		if (OverlappingActors.Contains(NPC))
		{
			bStillInAnyBox = true;
			break;
		}
	}

	// Only stop tracking if NPC is not in any box
	if (!bStillInAnyBox)
	{
		StopTrackingNPC(NPC);
	}
}

void AShooterKey::OnTrackedNPCDeath(AShooterNPC* DeadNPC)
{
	StopTrackingNPC(DeadNPC);
}

void AShooterKey::OnPlayerRespawned()
{
	// Reset key to alive state
	ResetHealth();

	// Re-enable collision (was disabled on death)
	if (HitboxComponent)
	{
		HitboxComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	// Clear all tracking - NPCs are being respawned
	for (const TWeakObjectPtr<AShooterNPC>& NPCPtr : TrackedEnemies)
	{
		if (AShooterNPC* NPC = NPCPtr.Get())
		{
			NPC->OnNPCDeath.RemoveDynamic(this, &AShooterKey::OnTrackedNPCDeath);
		}
	}
	TrackedEnemies.Empty();

	// Reset to invulnerable state until NPCs are scanned
	bIsInvulnerable = true;
	ApplyOverlayMaterial();

	// Wait a frame for NPCs to spawn, then rescan
	FTimerHandle RescanTimer;
	GetWorld()->GetTimerManager().SetTimer(RescanTimer, this, &AShooterKey::RefreshEnemyDetection, 0.2f, false);
}

void AShooterKey::StartTrackingNPC(AShooterNPC* NPC)
{
	if (!NPC)
	{
		return;
	}

	// Check if already tracking
	TWeakObjectPtr<AShooterNPC> WeakNPC(NPC);
	if (TrackedEnemies.Contains(WeakNPC))
	{
		return;
	}

	// Add to set
	TrackedEnemies.Add(WeakNPC);

	// Bind to death delegate
	NPC->OnNPCDeath.AddDynamic(this, &AShooterKey::OnTrackedNPCDeath);

	// Update state
	UpdateInvulnerabilityState();
}

void AShooterKey::StopTrackingNPC(AShooterNPC* NPC)
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

	// Unbind death delegate
	NPC->OnNPCDeath.RemoveDynamic(this, &AShooterKey::OnTrackedNPCDeath);

	// Remove from set
	TrackedEnemies.Remove(WeakNPC);

	// Update state
	UpdateInvulnerabilityState();
}

void AShooterKey::SetInvulnerable(bool bNewInvulnerable)
{
	UE_LOG(LogTemp, Warning, TEXT("ShooterKey::SetInvulnerable - Called with %s, ManualMode: %s, Current: %s"),
		bNewInvulnerable ? TEXT("TRUE") : TEXT("FALSE"),
		bManualMode ? TEXT("YES") : TEXT("NO"),
		bIsInvulnerable ? TEXT("INVULNERABLE") : TEXT("VULNERABLE"));

	// Only works in manual mode
	if (!bManualMode)
	{
		UE_LOG(LogTemp, Warning, TEXT("ShooterKey::SetInvulnerable - Ignored because bManualMode is false. Enable bManualMode to control invulnerability manually."));
		return;
	}

	bIsInvulnerable = bNewInvulnerable;

	UE_LOG(LogTemp, Warning, TEXT("ShooterKey::SetInvulnerable - State is now: %s"),
		bIsInvulnerable ? TEXT("INVULNERABLE") : TEXT("VULNERABLE"));
}

void AShooterKey::UpdateInvulnerabilityState()
{
	// Clean up any invalid weak pointers
	for (auto It = TrackedEnemies.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent();
		}
	}

	const int32 CurrentCount = TrackedEnemies.Num();

	// Broadcast event regardless of mode (enemy tracking still works)
	OnEnemyCountChanged.Broadcast(CurrentCount, EnemyThreshold);

	// In manual mode, don't automatically change invulnerability based on enemies
	if (bManualMode)
	{
		return;
	}

	const bool bShouldBeInvulnerable = CurrentCount > EnemyThreshold;

	// Check if state changed
	if (bIsInvulnerable != bShouldBeInvulnerable)
	{
		bIsInvulnerable = bShouldBeInvulnerable;
		ApplyOverlayMaterial();
	}
}

void AShooterKey::RebuildTrackedEnemies()
{
	// Unbind from all current NPCs
	for (const TWeakObjectPtr<AShooterNPC>& NPCPtr : TrackedEnemies)
	{
		if (AShooterNPC* NPC = NPCPtr.Get())
		{
			NPC->OnNPCDeath.RemoveDynamic(this, &AShooterKey::OnTrackedNPCDeath);
		}
	}
	TrackedEnemies.Empty();

	// Scan all detection boxes
	for (const TWeakObjectPtr<UBoxComponent>& BoxPtr : DetectionBoxes)
	{
		UBoxComponent* Box = BoxPtr.Get();
		if (!Box)
		{
			continue;
		}

		TArray<AActor*> OverlappingActors;
		Box->GetOverlappingActors(OverlappingActors, AShooterNPC::StaticClass());

		for (AActor* Actor : OverlappingActors)
		{
			AShooterNPC* NPC = Cast<AShooterNPC>(Actor);
			if (NPC && !NPC->IsDead())
			{
				TWeakObjectPtr<AShooterNPC> WeakNPC(NPC);
				if (!TrackedEnemies.Contains(WeakNPC))
				{
					TrackedEnemies.Add(WeakNPC);
					NPC->OnNPCDeath.AddDynamic(this, &AShooterKey::OnTrackedNPCDeath);
				}
			}
		}
	}

	// Update state
	UpdateInvulnerabilityState();
}

void AShooterKey::ApplyOverlayMaterial()
{
	if (!DummyMesh)
	{
		return;
	}

	UMaterialInterface* MaterialToApply = bIsInvulnerable ? InvulnerableMaterial : VulnerableMaterial;

	if (MaterialToApply)
	{
		DummyMesh->SetOverlayMaterial(MaterialToApply);
	}
	else
	{
		// Clear overlay if no material set
		DummyMesh->SetOverlayMaterial(nullptr);
	}
}
