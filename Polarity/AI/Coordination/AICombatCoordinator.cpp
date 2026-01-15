// AICombatCoordinator.cpp

#include "AICombatCoordinator.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "ShooterNPC.h"

TWeakObjectPtr<AAICombatCoordinator> AAICombatCoordinator::Instance = nullptr;

AAICombatCoordinator::AAICombatCoordinator()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f; // Update 10 times per second
}

void AAICombatCoordinator::BeginPlay()
{
	Super::BeginPlay();

	// Register as singleton
	Instance = this;

	// Find player as default target
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (APawn* PlayerPawn = PC->GetPawn())
		{
			PrimaryTarget = PlayerPawn;
		}
	}
}

void AAICombatCoordinator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	TimeSinceLastAttackGrant += DeltaTime;

	// Try to find player if we lost them (respawn, etc.)
	if (!PrimaryTarget.IsValid())
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
		{
			if (APawn* PlayerPawn = PC->GetPawn())
			{
				PrimaryTarget = PlayerPawn;
			}
		}
	}

	// Cleanup invalid references
	CleanupInvalidNPCs();

	// Update scores
	UpdateAttackScores();

	// Update permission timeouts
	UpdatePermissionTimeouts(DeltaTime);

	// Update wait times for NPCs without permission
	for (FRegisteredNPCData& Data : RegisteredNPCs)
	{
		if (!Data.bHasAttackPermission && !Data.bIsCurrentlyAttacking)
		{
			Data.WaitTime += DeltaTime;
		}
	}

	// Draw debug visualization
	if (bDrawDebug)
	{
		DrawDebugInfo();
	}
}

AAICombatCoordinator* AAICombatCoordinator::GetCoordinator(const UObject* WorldContext)
{
	if (Instance.IsValid())
	{
		return Instance.Get();
	}

	// Spawn new coordinator
	if (WorldContext)
	{
		UWorld* World = WorldContext->GetWorld();
		if (World)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			AAICombatCoordinator* NewCoordinator = World->SpawnActor<AAICombatCoordinator>(
				AAICombatCoordinator::StaticClass(),
				FVector::ZeroVector,
				FRotator::ZeroRotator,
				SpawnParams
			);

			Instance = NewCoordinator;
			return NewCoordinator;
		}
	}

	return nullptr;
}

void AAICombatCoordinator::RegisterNPC(APawn* NPC)
{
	if (!NPC)
	{
		return;
	}

	// Check if already registered
	if (FindNPCData(NPC))
	{
		return;
	}

	FRegisteredNPCData NewData;
	NewData.NPC = NPC;
	NewData.Role = EAICombatRole::Supporter;
	NewData.AttackScore = 0.0f;
	NewData.WaitTime = 0.0f;
	NewData.PermissionTime = 0.0f;
	NewData.AttackingTime = 0.0f;
	NewData.bHasAttackPermission = false;
	NewData.bIsCurrentlyAttacking = false;

	RegisteredNPCs.Add(NewData);
}

void AAICombatCoordinator::UnregisterNPC(APawn* NPC)
{
	if (!NPC)
	{
		return;
	}

	RegisteredNPCs.RemoveAll([NPC](const FRegisteredNPCData& Data)
		{
			return Data.NPC.Get() == NPC;
		});
}

bool AAICombatCoordinator::RequestAttackPermission(APawn* Requester)
{
	if (!Requester)
	{
		return false;
	}

	// Check if NPC is outside engagement range
	if (!IsNPCInEngagementRange(Requester))
	{
		// Outside range - allow free attack if enabled
		return bAllowFreeAttackOutsideRange;
	}

	FRegisteredNPCData* Data = FindNPCData(Requester);
	if (!Data)
	{
		// Auto-register
		RegisterNPC(Requester);
		Data = FindNPCData(Requester);
		if (!Data)
		{
			return false;
		}
	}

	// Already has permission
	if (Data->bHasAttackPermission)
	{
		return true;
	}

	// Check if we can grant new permission (only count NPCs in range)
	const int32 CurrentAttackers = CountCurrentAttackers();
	if (CurrentAttackers >= MaxSimultaneousAttackers)
	{
		return false;
	}

	// Check minimum time between grants
	if (TimeSinceLastAttackGrant < MinTimeBetweenAttacks)
	{
		return false;
	}

	// Check if this NPC has highest score among waiting NPCs (only in range)
	float HighestWaitingScore = 0.0f;
	for (const FRegisteredNPCData& OtherData : RegisteredNPCs)
	{
		if (!OtherData.bHasAttackPermission && !OtherData.bIsCurrentlyAttacking)
		{
			// Only consider NPCs in engagement range
			if (IsNPCInEngagementRange(OtherData.NPC.Get()))
			{
				HighestWaitingScore = FMath::Max(HighestWaitingScore, OtherData.AttackScore);
			}
		}
	}

	// Only grant if this NPC has highest or near-highest score
	if (Data->AttackScore < HighestWaitingScore * 0.8f)
	{
		return false;
	}

	// Grant permission
	Data->bHasAttackPermission = true;
	Data->PermissionTime = 0.0f;  // Reset permission timeout
	Data->WaitTime = 0.0f;        // Reset wait time (got permission)
	Data->Role = EAICombatRole::Attacker;
	TimeSinceLastAttackGrant = 0.0f;

	return true;
}

bool AAICombatCoordinator::HasAttackPermission(APawn* NPC) const
{
	const FRegisteredNPCData* Data = FindNPCData(NPC);
	return Data && Data->bHasAttackPermission;
}

void AAICombatCoordinator::NotifyAttackStarted(APawn* Attacker)
{
	if (FRegisteredNPCData* Data = FindNPCData(Attacker))
	{
		Data->bIsCurrentlyAttacking = true;
		Data->PermissionTime = 0.0f;
		Data->AttackingTime = 0.0f;  // Reset attacking timer
	}
}

void AAICombatCoordinator::NotifyAttackComplete(APawn* Attacker)
{
	if (FRegisteredNPCData* Data = FindNPCData(Attacker))
	{
		Data->bHasAttackPermission = false;
		Data->bIsCurrentlyAttacking = false;
		Data->PermissionTime = 0.0f;
		Data->AttackingTime = 0.0f;
		Data->Role = EAICombatRole::Supporter;
	}
}

void AAICombatCoordinator::GrantRetaliationPermission(APawn* NPC)
{
	if (!NPC)
	{
		return;
	}

	FRegisteredNPCData* Data = FindNPCData(NPC);
	if (!Data)
	{
		// Auto-register
		RegisterNPC(NPC);
		Data = FindNPCData(NPC);
		if (!Data)
		{
			return;
		}
	}

	// Already attacking - just reset the timer to extend
	if (Data->bIsCurrentlyAttacking)
	{
		Data->AttackingTime = 0.0f;
		return;
	}

	// Grant immediate permission (bypasses limits)
	Data->bHasAttackPermission = true;
	Data->PermissionTime = 0.0f;
	Data->AttackingTime = 0.0f;
	Data->WaitTime = 0.0f;
	Data->Role = EAICombatRole::Attacker;
}

EAICombatRole AAICombatCoordinator::GetNPCRole(APawn* NPC) const
{
	const FRegisteredNPCData* Data = FindNPCData(NPC);
	return Data ? Data->Role : EAICombatRole::Supporter;
}

void AAICombatCoordinator::SetNPCRole(APawn* NPC, EAICombatRole NewRole)
{
	if (FRegisteredNPCData* Data = FindNPCData(NPC))
	{
		Data->Role = NewRole;
	}
}

int32 AAICombatCoordinator::GetActiveAttackerCount() const
{
	return CountCurrentAttackers();
}

void AAICombatCoordinator::SetPrimaryTarget(AActor* Target)
{
	PrimaryTarget = Target;
}

FRegisteredNPCData* AAICombatCoordinator::FindNPCData(APawn* NPC)
{
	for (FRegisteredNPCData& Data : RegisteredNPCs)
	{
		if (Data.NPC.Get() == NPC)
		{
			return &Data;
		}
	}
	return nullptr;
}

const FRegisteredNPCData* AAICombatCoordinator::FindNPCData(APawn* NPC) const
{
	for (const FRegisteredNPCData& Data : RegisteredNPCs)
	{
		if (Data.NPC.Get() == NPC)
		{
			return &Data;
		}
	}
	return nullptr;
}

void AAICombatCoordinator::UpdateAttackScores()
{
	for (FRegisteredNPCData& Data : RegisteredNPCs)
	{
		// Only calculate scores for NPCs in engagement range
		if (IsNPCInEngagementRange(Data.NPC.Get()))
		{
			Data.AttackScore = CalculateAttackScore(Data);
		}
		else
		{
			// NPCs outside range get zero score (they attack freely anyway)
			Data.AttackScore = 0.0f;
		}
	}
}

float AAICombatCoordinator::CalculateAttackScore(const FRegisteredNPCData& Data) const
{
	if (!Data.NPC.IsValid() || !PrimaryTarget.IsValid())
	{
		return 0.0f;
	}

	float Score = 0.0f;

	// Distance score (closer = higher)
	const float Distance = FVector::Dist(Data.NPC->GetActorLocation(), PrimaryTarget->GetActorLocation());
	const float NormalizedDistance = 1.0f - FMath::Clamp(Distance / MaxScoringDistance, 0.0f, 1.0f);
	Score += NormalizedDistance * DistanceWeight;

	// Line of sight score
	if (HasLineOfSightToTarget(Data.NPC.Get()))
	{
		Score += LineOfSightWeight;
	}

	// Wait time score (prevents starvation)
	Score += Data.WaitTime * WaitTimeWeight;

	return Score;
}

bool AAICombatCoordinator::HasLineOfSightToTarget(APawn* NPC) const
{
	if (!NPC || !PrimaryTarget.IsValid())
	{
		return false;
	}

	FHitResult HitResult;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(NPC);

	const FVector Start = NPC->GetPawnViewLocation();
	const FVector End = PrimaryTarget->GetActorLocation();

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		HitResult,
		Start,
		End,
		ECC_Visibility,
		QueryParams
	);

	// No hit means clear line of sight, or hit the target
	return !bHit || HitResult.GetActor() == PrimaryTarget.Get();
}

void AAICombatCoordinator::CleanupInvalidNPCs()
{
	RegisteredNPCs.RemoveAll([](const FRegisteredNPCData& Data)
		{
			if (!Data.NPC.IsValid())
			{
				return true;
			}
			
			// Also remove dead NPCs
			if (AShooterNPC* ShooterNPC = Cast<AShooterNPC>(Data.NPC.Get()))
			{
				if (ShooterNPC->IsDead())
				{
					return true;
				}
			}
			
			return false;
		});
}

void AAICombatCoordinator::UpdatePermissionTimeouts(float DeltaTime)
{
	for (FRegisteredNPCData& Data : RegisteredNPCs)
	{
		if (Data.bHasAttackPermission)
		{
			if (Data.bIsCurrentlyAttacking)
			{
				// Track how long NPC has been "attacking"
				Data.AttackingTime += DeltaTime;

				// Check if NPC is actually still shooting
				bool bStillShooting = false;
				if (AShooterNPC* ShooterNPC = Cast<AShooterNPC>(Data.NPC.Get()))
				{
					// If NPC says it's not shooting anymore, release the slot
					bStillShooting = ShooterNPC->IsCurrentlyShooting();
				}

				// Reset if NPC stopped shooting or stuck in attacking state too long
				if (!bStillShooting || Data.AttackingTime >= MaxAttackingTime)
				{
					Data.bHasAttackPermission = false;
					Data.bIsCurrentlyAttacking = false;
					Data.AttackingTime = 0.0f;
					Data.PermissionTime = 0.0f;
					Data.Role = EAICombatRole::Supporter;
				}
			}
			else
			{
				// Has permission but not attacking yet
				Data.PermissionTime += DeltaTime;

				// Timeout: revoke permission if not used
				if (Data.PermissionTime >= AttackPermissionTimeout)
				{
					Data.bHasAttackPermission = false;
					Data.PermissionTime = 0.0f;
					Data.Role = EAICombatRole::Supporter;
				}
			}
		}
	}
}

int32 AAICombatCoordinator::CountCurrentAttackers() const
{
	int32 Count = 0;
	for (const FRegisteredNPCData& Data : RegisteredNPCs)
	{
		if (Data.bHasAttackPermission || Data.bIsCurrentlyAttacking)
		{
			// Only count NPCs within engagement range
			if (IsNPCInEngagementRange(Data.NPC.Get()))
			{
				++Count;
			}
		}
	}
	return Count;
}

bool AAICombatCoordinator::IsNPCInEngagementRange(APawn* NPC) const
{
	// If MaxEngagementDistance is 0, all NPCs are in range
	if (MaxEngagementDistance <= 0.0f)
	{
		return true;
	}

	// If no valid target, consider all NPCs in range (so coordination still works)
	if (!PrimaryTarget.IsValid())
	{
		return true;
	}

	const float Distance = GetDistanceToTarget(NPC);
	return Distance <= MaxEngagementDistance;
}

float AAICombatCoordinator::GetDistanceToTarget(APawn* NPC) const
{
	if (!NPC || !PrimaryTarget.IsValid())
	{
		return MAX_FLT;
	}

	return FVector::Dist(NPC->GetActorLocation(), PrimaryTarget->GetActorLocation());
}

void AAICombatCoordinator::DrawDebugInfo()
{
	if (!GetWorld())
	{
		return;
	}

	const float DebugDuration = 0.0f; // Single frame

	// Draw engagement range around player
	if (PrimaryTarget.IsValid() && MaxEngagementDistance > 0.0f)
	{
		DrawDebugSphere(
			GetWorld(),
			PrimaryTarget->GetActorLocation(),
			MaxEngagementDistance,
			24,
			FColor::Green,
			false,
			DebugDuration,
			0,
			5.0f
		);
	}

	// Draw status for each NPC
	for (const FRegisteredNPCData& Data : RegisteredNPCs)
	{
		if (!Data.NPC.IsValid())
		{
			continue;
		}

		const FVector NPCLocation = Data.NPC->GetActorLocation();
		const FVector HeadLocation = NPCLocation + FVector(0, 0, 100.0f);

		// Determine color based on state
		FColor StatusColor;
		FString StatusText;

		// Check if NPC is dead first
		bool bNPCIsDead = false;
		if (AShooterNPC* ShooterNPC = Cast<AShooterNPC>(Data.NPC.Get()))
		{
			bNPCIsDead = ShooterNPC->IsDead();
		}

		const bool bInRange = IsNPCInEngagementRange(Data.NPC.Get());

		if (bNPCIsDead)
		{
			StatusColor = FColor::Black;
			StatusText = TEXT("DEAD");
		}
		else if (!bInRange)
		{
			StatusColor = DebugColorOutOfRange;
			StatusText = TEXT("OUT OF RANGE");
		}
		else if (Data.bIsCurrentlyAttacking)
		{
			StatusColor = DebugColorAttacking;
			StatusText = FString::Printf(TEXT("ATTACKING (%.1fs)"), Data.AttackingTime);
		}
		else if (Data.bHasAttackPermission)
		{
			StatusColor = FColor::Orange;  // Has permission but not shooting yet
			StatusText = FString::Printf(TEXT("PERMISSION (%.1fs)"), Data.PermissionTime);
		}
		else
		{
			StatusColor = DebugColorWaiting;
			StatusText = FString::Printf(TEXT("WAITING (%.1fs)"), Data.WaitTime);
		}

		// Draw sphere above NPC head
		DrawDebugSphere(
			GetWorld(),
			HeadLocation,
			25.0f,
			8,
			StatusColor,
			false,
			DebugDuration,
			0,
			2.0f
		);

		// Draw line to target if actually attacking
		if (Data.bIsCurrentlyAttacking && PrimaryTarget.IsValid())
		{
			DrawDebugLine(
				GetWorld(),
				NPCLocation,
				PrimaryTarget->GetActorLocation(),
				DebugColorAttacking,
				false,
				DebugDuration,
				0,
				3.0f
			);
		}

		// Draw debug string
		DrawDebugString(
			GetWorld(),
			HeadLocation + FVector(0, 0, 30.0f),
			FString::Printf(TEXT("%s\nScore: %.1f"), *StatusText, Data.AttackScore),
			nullptr,
			StatusColor,
			DebugDuration,
			true,
			1.0f
		);
	}

	// Draw overall stats
	if (PrimaryTarget.IsValid())
	{
		const FVector StatsLocation = PrimaryTarget->GetActorLocation() + FVector(0, 0, 200.0f);
		DrawDebugString(
			GetWorld(),
			StatsLocation,
			FString::Printf(TEXT("Attackers: %d / %d\nRegistered: %d"),
				CountCurrentAttackers(),
				MaxSimultaneousAttackers,
				RegisteredNPCs.Num()),
			nullptr,
			FColor::White,
			DebugDuration,
			true,
			1.2f
		);
	}
}
