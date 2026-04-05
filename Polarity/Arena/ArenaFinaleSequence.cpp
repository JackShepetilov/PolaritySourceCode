// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "ArenaFinaleSequence.h"
#include "ArenaManager.h"
#include "Polarity/Variant_Shooter/ShooterCharacter.h"
#include "Polarity/Variant_Shooter/AI/ShooterNPC.h"
#include "Polarity/PolarityCharacter.h"
#include "Polarity/ChargeAnimationComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "GameFramework/PlayerController.h"
#include "Curves/CurveFloat.h"

AArenaFinaleSequence::AArenaFinaleSequence()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

// ==================== API ====================

void AArenaFinaleSequence::StartFinaleSequence()
{
	if (bSequenceActive)
	{
		UE_LOG(LogTemp, Warning, TEXT("ArenaFinaleSequence: Already active, ignoring StartFinaleSequence"));
		return;
	}

	// Resolve arena
	AArenaManager* Arena = LinkedArena.Get();
	if (!Arena)
	{
		UE_LOG(LogTemp, Error, TEXT("ArenaFinaleSequence: LinkedArena is null!"));
		return;
	}

	// Stop sustain from spawning replacements during the sequence
	Arena->PauseSustainSpawning();

	// Get alive NPCs and shuffle
	TArray<AShooterNPC*> AliveNPCs = Arena->GetAliveNPCs();
	if (AliveNPCs.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ArenaFinaleSequence: No alive NPCs — skipping sequence"));
		return;
	}

	// Fisher-Yates shuffle
	for (int32 i = AliveNPCs.Num() - 1; i > 0; --i)
	{
		const int32 j = FMath::RandRange(0, i);
		AliveNPCs.Swap(i, j);
	}

	PendingKillNPCs.Empty(AliveNPCs.Num());
	for (AShooterNPC* NPC : AliveNPCs)
	{
		PendingKillNPCs.Add(NPC);
	}

	// Find player
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !PC->GetPawn())
	{
		UE_LOG(LogTemp, Error, TEXT("ArenaFinaleSequence: No player controller/pawn!"));
		return;
	}

	AShooterCharacter* Player = Cast<AShooterCharacter>(PC->GetPawn());
	if (!Player)
	{
		UE_LOG(LogTemp, Error, TEXT("ArenaFinaleSequence: Pawn is not AShooterCharacter!"));
		return;
	}

	UChargeAnimationComponent* ChargeComp = Player->FindComponentByClass<UChargeAnimationComponent>();
	if (!ChargeComp)
	{
		UE_LOG(LogTemp, Error, TEXT("ArenaFinaleSequence: No ChargeAnimationComponent on player!"));
		return;
	}

	// Cache references
	CachedPlayer = Player;
	CachedPC = PC;
	CachedChargeComp = ChargeComp;

	// Lock normal grab
	ChargeComp->SetInputLocked(true);

	// Resolve camera target
	if (AActor* CamTarget = CameraLockTarget.Get())
	{
		ResolvedCameraTarget = CamTarget->GetActorLocation();
	}
	else
	{
		ResolvedCameraTarget = GetActorLocation();
		UE_LOG(LogTemp, Warning, TEXT("ArenaFinaleSequence: CameraLockTarget is null — using own location"));
	}

	bSequenceActive = true;
	bWaitingForInput = false;
	bInKillPhase = false;
	bCameraLocked = false;

	// Enable tick
	SetActorTickEnabled(true);

	OnFinaleStarted.Broadcast();

	UE_LOG(LogTemp, Warning, TEXT("ArenaFinaleSequence: Started — %d NPCs queued, triggering cinematic phase"),
		PendingKillNPCs.Num());

	OnPlayerActivated();
}

// ==================== Input ====================

void AArenaFinaleSequence::BindPlayerInput()
{
	APolarityCharacter* PolarityChar = Cast<APolarityCharacter>(CachedPlayer.Get());
	if (!PolarityChar)
	{
		return;
	}

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PolarityChar->InputComponent);
	if (!EIC)
	{
		UE_LOG(LogTemp, Error, TEXT("ArenaFinaleSequence: No EnhancedInputComponent on player!"));
		return;
	}

	InputBindingHandles.Empty();

	// Bind to ToggleChargeAction (tap)
	if (UInputAction* ToggleAction = PolarityChar->GetToggleChargeAction())
	{
		auto& Binding = EIC->BindAction(ToggleAction, ETriggerEvent::Started, this, &AArenaFinaleSequence::OnGrabInputTriggered);
		InputBindingHandles.Add(Binding.GetHandle());
	}

	// Bind to ChannelAction (hold)
	if (UInputAction* ChanAction = PolarityChar->GetChannelAction())
	{
		auto& Binding = EIC->BindAction(ChanAction, ETriggerEvent::Started, this, &AArenaFinaleSequence::OnGrabInputTriggered);
		InputBindingHandles.Add(Binding.GetHandle());
	}
}

void AArenaFinaleSequence::UnbindPlayerInput()
{
	APolarityCharacter* PolarityChar = Cast<APolarityCharacter>(CachedPlayer.Get());
	if (!PolarityChar)
	{
		return;
	}

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PolarityChar->InputComponent);
	if (!EIC)
	{
		return;
	}

	for (uint32 Handle : InputBindingHandles)
	{
		EIC->RemoveBindingByHandle(Handle);
	}
	InputBindingHandles.Empty();
}

void AArenaFinaleSequence::OnGrabInputTriggered()
{
	if (!bWaitingForInput)
	{
		return;
	}

	bWaitingForInput = false;
	UnbindPlayerInput();
	OnPlayerActivated();
}

// ==================== Activation ====================

void AArenaFinaleSequence::OnPlayerActivated()
{
	UE_LOG(LogTemp, Warning, TEXT("ArenaFinaleSequence: Player activated — starting cinematic phase"));

	// Play the charge animation through ChargeAnimationComponent (normal montage, normal play rate)
	UChargeAnimationComponent* ChargeComp = CachedChargeComp.Get();
	if (ChargeComp)
	{
		ChargeComp->SetInputLocked(false);
		ChargeComp->OnChargeButtonPressed();
		// Component re-locks itself via state machine during animation — no need to re-lock manually
	}

	// Lock camera
	if (APlayerController* PC = CachedPC.Get())
	{
		PC->SetIgnoreLookInput(true);
		PC->SetIgnoreMoveInput(true);
		bCameraLocked = true;
	}

	// Stun all NPCs
	for (const TWeakObjectPtr<AShooterNPC>& NPCPtr : PendingKillNPCs)
	{
		if (AShooterNPC* NPC = NPCPtr.Get())
		{
			if (!NPC->IsDead())
			{
				NPC->ForceResetCombatState();
				NPC->ApplyExplosionStun(NPCStunDuration, NPCStunMontage);
			}
		}
	}

	// Schedule kill phase (real-time)
	NextKillRealTime = GetWorld()->GetRealTimeSeconds() + DelayBeforeKills;
	bInKillPhase = false;

	OnFinalePlayerActivated.Broadcast();
}

// ==================== Tick ====================

void AArenaFinaleSequence::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bSequenceActive)
	{
		return;
	}

	// Compute real (unscaled) delta time
	const float TimeDilation = FMath::Max(UGameplayStatics::GetGlobalTimeDilation(this), 0.01f);
	const float RealDelta = DeltaTime / TimeDilation;

	// Update camera
	if (bCameraLocked)
	{
		UpdateCameraLock(RealDelta);
	}

	// Kill phase scheduling
	if (!bWaitingForInput)
	{
		const double RealTime = GetWorld()->GetRealTimeSeconds();
		if (RealTime >= NextKillRealTime)
		{
			bInKillPhase = true;
			KillNextNPC();
		}
	}
}

// ==================== Camera ====================

void AArenaFinaleSequence::UpdateCameraLock(float RealDelta)
{
	APlayerController* PC = CachedPC.Get();
	if (!PC)
	{
		return;
	}

	const FVector PlayerEye = PC->PlayerCameraManager
		? PC->PlayerCameraManager->GetCameraLocation()
		: PC->GetPawn()->GetActorLocation();

	const FRotator CurrentRot = PC->GetControlRotation();
	const FRotator TargetRot = (ResolvedCameraTarget - PlayerEye).Rotation();
	const float InterpSpeed = 1.0f / FMath::Max(CameraBlendTime, 0.01f);

	const FRotator NewRot = FMath::RInterpTo(CurrentRot, TargetRot, RealDelta, InterpSpeed);
	PC->SetControlRotation(NewRot);
}

// ==================== Kill Phase ====================

void AArenaFinaleSequence::KillNextNPC()
{
	// Find next valid NPC
	while (PendingKillNPCs.Num() > 0)
	{
		TWeakObjectPtr<AShooterNPC> NPCPtr = PendingKillNPCs[0];
		PendingKillNPCs.RemoveAt(0);

		AShooterNPC* NPC = NPCPtr.Get();
		if (NPC && !NPC->IsDead())
		{
			FinaleKillNPC(NPC);

			// Schedule next kill
			NextKillRealTime = GetWorld()->GetRealTimeSeconds() + DelayBetweenKills;
			return;
		}
	}

	// All NPCs processed
	EndFinaleSequence();
}

void AArenaFinaleSequence::FinaleKillNPC(AShooterNPC* NPC)
{
	if (!NPC)
	{
		return;
	}

	const FVector NPCLocation = NPC->GetActorLocation();

	// Spawn death VFX
	if (FinaleDeathVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), FinaleDeathVFX, NPCLocation,
			FRotator::ZeroRotator, FinaleDeathVFXScale);
	}

	// Suppress loot drops and apply lethal damage
	NPC->bSuppressDeathDrops = true;
	UGameplayStatics::ApplyDamage(NPC, NPC->CurrentHP + 100.0f, nullptr, this, nullptr);

	UE_LOG(LogTemp, Log, TEXT("ArenaFinaleSequence: Killed %s at %s"), *NPC->GetName(), *NPCLocation.ToString());
}

// ==================== Completion ====================

void AArenaFinaleSequence::EndFinaleSequence()
{
	UE_LOG(LogTemp, Warning, TEXT("ArenaFinaleSequence: Sequence complete"));

	// Unlock camera
	if (APlayerController* PC = CachedPC.Get())
	{
		PC->ResetIgnoreLookInput();
		PC->ResetIgnoreMoveInput();
	}
	bCameraLocked = false;

	// Restore grab input
	if (UChargeAnimationComponent* ChargeComp = CachedChargeComp.Get())
	{
		ChargeComp->SetInputLocked(false);
	}

	// Cleanup
	bSequenceActive = false;
	bInKillPhase = false;
	bWaitingForInput = false;
	PendingKillNPCs.Empty();
	SetActorTickEnabled(false);

	// Cleanup any remaining input bindings
	UnbindPlayerInput();

	OnFinaleCompleted.Broadcast();
}
