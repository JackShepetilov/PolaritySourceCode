// AICombatCoordinator.cpp

#include "AICombatCoordinator.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "ShooterNPC.h"
#include "MeleeNPC.h"
#include "FlyingDrone.h"
#include "ShooterCharacter.h"

// ==================== FTokenPool ====================

bool FTokenPool::HasToken(APawn* NPC) const
{
	for (const auto& Ref : HeldBy)
	{
		if (Ref.Get() == NPC)
		{
			return true;
		}
	}
	return false;
}

bool FTokenPool::TryAcquire(APawn* NPC)
{
	if (!NPC) return false;
	if (HasToken(NPC)) return true;
	if (HeldBy.Num() >= MaxTokens) return false;

	HeldBy.Add(NPC);
	return true;
}

void FTokenPool::Release(APawn* NPC)
{
	HeldBy.RemoveAll([NPC](const TWeakObjectPtr<APawn>& Ref)
	{
		return Ref.Get() == NPC;
	});
}

void FTokenPool::CleanupInvalid()
{
	HeldBy.RemoveAll([](const TWeakObjectPtr<APawn>& Ref)
	{
		return !Ref.IsValid();
	});
}

// ==================== Coordinator ====================

TWeakObjectPtr<AAICombatCoordinator> AAICombatCoordinator::Instance = nullptr;

AAICombatCoordinator::AAICombatCoordinator()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f; // 10Hz
}

void AAICombatCoordinator::BeginPlay()
{
	Super::BeginPlay();
	Instance = this;

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

	// Re-find player if lost
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

	// Cleanup
	CleanupInvalidNPCs();

	// Token pools
	UpdateTokenPools();
	UpdateProximityOverrides();
	RangedTokenPool.CleanupInvalid();
	MeleeTokenPool.CleanupInvalid();
	SpecialTokenPool.CleanupInvalid();

	// Scores
	UpdateAttackScores();

	// Permission timeouts
	UpdatePermissionTimeouts(DeltaTime);

	// Wait times
	for (FRegisteredNPCData& Data : RegisteredNPCs)
	{
		if (!Data.bHasAttackPermission && !Data.bIsCurrentlyAttacking)
		{
			Data.WaitTime += DeltaTime;
		}
	}

	// Player state cache
	UpdatePlayerStateCache();

	// Role assignment
	AssignRoles();

	// Battle Circle
	if (bUseBattleCircle)
	{
		TimeSinceLastSlotRecalc += DeltaTime;

		const int32 ActiveNPCCount = RegisteredNPCs.Num();
		if (ActiveNPCCount != LastSlotNPCCount || BattleSlots.Num() == 0)
		{
			GenerateBattleSlots();
			AssignNPCsToSlots();
			LastSlotNPCCount = ActiveNPCCount;
			TimeSinceLastSlotRecalc = 0.0f;
		}
		else if (TimeSinceLastSlotRecalc >= SlotRecalculationInterval)
		{
			RecalculateSlotPositions();
			AssignNPCsToSlots();
			TimeSinceLastSlotRecalc = 0.0f;
		}
	}

	// Debug drawing
	if (bDrawDebug)
	{
		DrawDebugInfo();
	}
	if (bDrawBattleCircle)
	{
		DrawBattleCircleDebug();
	}
	if (bDrawRoleDebug)
	{
		DrawRoleDebug();
	}
}

// ==================== Singleton ====================

AAICombatCoordinator* AAICombatCoordinator::GetCoordinator(const UObject* WorldContext)
{
	if (Instance.IsValid())
	{
		return Instance.Get();
	}

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

// ==================== Registration ====================

void AAICombatCoordinator::RegisterNPC(APawn* NPC)
{
	if (!NPC) return;
	if (FindNPCData(NPC)) return;

	FRegisteredNPCData NewData;
	NewData.NPC = NPC;
	NewData.Role = EAICombatRole::Supporter;
	NewData.TokenType = DetermineTokenType(NPC);

	RegisteredNPCs.Add(NewData);
}

void AAICombatCoordinator::UnregisterNPC(APawn* NPC)
{
	if (!NPC) return;

	// Release any held tokens
	RangedTokenPool.Release(NPC);
	MeleeTokenPool.Release(NPC);
	SpecialTokenPool.Release(NPC);

	RegisteredNPCs.RemoveAll([NPC](const FRegisteredNPCData& Data)
	{
		return Data.NPC.Get() == NPC;
	});
}

// ==================== Attack Permission (bridges to tokens) ====================

bool AAICombatCoordinator::RequestAttackPermission(APawn* Requester)
{
	if (!Requester) return false;

	// Outside engagement range — free attack
	if (!IsNPCInEngagementRange(Requester))
	{
		return bAllowFreeAttackOutsideRange;
	}

	FRegisteredNPCData* Data = FindNPCData(Requester);
	if (!Data)
	{
		RegisterNPC(Requester);
		Data = FindNPCData(Requester);
		if (!Data) return false;
	}

	if (Data->bHasAttackPermission) return true;

	// Try token acquisition
	EAttackTokenType Type = DetermineTokenType(Requester);
	if (RequestAttackToken(Requester, Type))
	{
		Data->bHasAttackPermission = true;
		Data->bHasToken = true;
		Data->PermissionTime = 0.0f;
		Data->WaitTime = 0.0f;
		Data->Role = EAICombatRole::Aggressor;
		TimeSinceLastAttackGrant = 0.0f;
		return true;
	}
	return false;
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
		Data->AttackingTime = 0.0f;
	}
}

void AAICombatCoordinator::NotifyAttackComplete(APawn* Attacker)
{
	if (FRegisteredNPCData* Data = FindNPCData(Attacker))
	{
		if (Data->bHasToken)
		{
			ReleaseAttackToken(Attacker);
			Data->bHasToken = false;
		}
		Data->bHasAttackPermission = false;
		Data->bIsCurrentlyAttacking = false;
		Data->bProximityOverride = false;
		Data->PermissionTime = 0.0f;
		Data->AttackingTime = 0.0f;
		Data->Role = EAICombatRole::Supporter;
	}
}

void AAICombatCoordinator::GrantRetaliationPermission(APawn* NPC)
{
	if (!NPC) return;

	FRegisteredNPCData* Data = FindNPCData(NPC);
	if (!Data)
	{
		RegisterNPC(NPC);
		Data = FindNPCData(NPC);
		if (!Data) return;
	}

	// Already attacking — extend timer
	if (Data->bIsCurrentlyAttacking)
	{
		Data->AttackingTime = 0.0f;
		return;
	}

	// Grant immediate permission (bypasses tokens)
	Data->bHasAttackPermission = true;
	Data->PermissionTime = 0.0f;
	Data->AttackingTime = 0.0f;
	Data->WaitTime = 0.0f;
	Data->Role = EAICombatRole::Aggressor;
}

// ==================== Token System ====================

EAttackTokenType AAICombatCoordinator::DetermineTokenType(APawn* NPC) const
{
	if (Cast<AMeleeNPC>(NPC)) return EAttackTokenType::Melee;
	// FlyingDrone and ShooterNPC both use ranged
	return EAttackTokenType::Ranged;
}

FTokenPool& AAICombatCoordinator::GetPoolForType(EAttackTokenType Type)
{
	switch (Type)
	{
	case EAttackTokenType::Melee: return MeleeTokenPool;
	case EAttackTokenType::Special: return SpecialTokenPool;
	default: return RangedTokenPool;
	}
}

const FTokenPool& AAICombatCoordinator::GetPoolForType(EAttackTokenType Type) const
{
	switch (Type)
	{
	case EAttackTokenType::Melee: return MeleeTokenPool;
	case EAttackTokenType::Special: return SpecialTokenPool;
	default: return RangedTokenPool;
	}
}

bool AAICombatCoordinator::RequestAttackToken(APawn* Requester, EAttackTokenType TokenType)
{
	if (!Requester) return false;

	FRegisteredNPCData* Data = FindNPCData(Requester);
	if (!Data)
	{
		RegisterNPC(Requester);
		Data = FindNPCData(Requester);
		if (!Data) return false;
	}

	// Proximity override — attack without consuming a token
	if (Data->bProximityOverride)
	{
		return true;
	}

	FTokenPool& Pool = GetPoolForType(TokenType);

	// Already holds token
	if (Pool.HasToken(Requester))
	{
		return true;
	}

	// Check minimum time between grants
	if (TimeSinceLastAttackGrant < MinTimeBetweenAttacks)
	{
		return false;
	}

	// Try to acquire from pool
	if (Pool.TryAcquire(Requester))
	{
		return true;
	}

	// Pool full — try stealing
	if (bAllowTokenStealing)
	{
		return TryStealToken(Requester, Pool);
	}

	return false;
}

void AAICombatCoordinator::ReleaseAttackToken(APawn* Attacker)
{
	if (!Attacker) return;

	RangedTokenPool.Release(Attacker);
	MeleeTokenPool.Release(Attacker);
	SpecialTokenPool.Release(Attacker);
}

bool AAICombatCoordinator::HasAttackToken(APawn* NPC) const
{
	if (!NPC) return false;

	const FRegisteredNPCData* Data = FindNPCData(NPC);
	if (Data && Data->bProximityOverride) return true;

	return RangedTokenPool.HasToken(NPC)
		|| MeleeTokenPool.HasToken(NPC)
		|| SpecialTokenPool.HasToken(NPC);
}

bool AAICombatCoordinator::TryStealToken(APawn* Requester, FTokenPool& Pool)
{
	if (!PrimaryTarget.IsValid() || !Requester) return false;

	const bool bRequesterHasLOS = HasLineOfSightToTarget(Requester);
	if (!bRequesterHasLOS) return false;

	const float RequesterDist = GetDistanceToTarget(Requester);

	APawn* WorstHolder = nullptr;
	float WorstScore = MAX_FLT;

	for (const auto& HeldRef : Pool.HeldBy)
	{
		APawn* Holder = Cast<APawn>(HeldRef.Get());
		if (!Holder) continue;

		const bool bHolderHasLOS = HasLineOfSightToTarget(Holder);
		if (!bHolderHasLOS)
		{
			const float HolderDist = GetDistanceToTarget(Holder);
			if (RequesterDist < HolderDist)
			{
				float Score = -HolderDist; // More negative = farther = worse
				if (Score < WorstScore)
				{
					WorstScore = Score;
					WorstHolder = Holder;
				}
			}
		}
	}

	if (WorstHolder)
	{
		Pool.Release(WorstHolder);
		if (FRegisteredNPCData* StolenData = FindNPCData(WorstHolder))
		{
			StolenData->bHasToken = false;
			StolenData->bHasAttackPermission = false;
			StolenData->bIsCurrentlyAttacking = false;
			StolenData->Role = EAICombatRole::Supporter;
		}
		Pool.TryAcquire(Requester);
		return true;
	}
	return false;
}

void AAICombatCoordinator::UpdateProximityOverrides()
{
	if (!PrimaryTarget.IsValid()) return;

	for (FRegisteredNPCData& Data : RegisteredNPCs)
	{
		if (!Data.NPC.IsValid()) continue;
		const float Dist = GetDistanceToTarget(Data.NPC.Get());
		Data.bProximityOverride = (Dist <= ProximityOverrideDistance);
	}
}

void AAICombatCoordinator::UpdateTokenPools()
{
	RangedTokenPool.MaxTokens = MaxRangedTokens;
	MeleeTokenPool.MaxTokens = MaxMeleeTokens;
	SpecialTokenPool.MaxTokens = MaxSpecialTokens;
}

// ==================== Battle Circle ====================

void AAICombatCoordinator::GenerateBattleSlots()
{
	BattleSlots.Empty();

	// Count NPCs per preferred ring
	int32 InnerCount = 0, MiddleCount = 0, OuterCount = 0;
	for (const FRegisteredNPCData& Data : RegisteredNPCs)
	{
		if (!Data.NPC.IsValid()) continue;
		EBattleRing Ring = GetPreferredRing(Data);
		switch (Ring)
		{
		case EBattleRing::Inner: InnerCount++; break;
		case EBattleRing::Middle: MiddleCount++; break;
		case EBattleRing::Outer: OuterCount++; break;
		}
	}

	auto CreateSlotsForRing = [this](EBattleRing Ring, int32 Count)
	{
		if (Count <= 0) return;
		const float AngleStep = 360.0f / Count;
		const float RandomOffset = FMath::FRandRange(0.0f, AngleStep);
		for (int32 i = 0; i < Count; ++i)
		{
			FBattleSlot Slot;
			Slot.Ring = Ring;
			Slot.AngleDeg = FMath::Fmod(RandomOffset + i * AngleStep, 360.0f);
			BattleSlots.Add(Slot);
		}
	};

	CreateSlotsForRing(EBattleRing::Inner, InnerCount);
	CreateSlotsForRing(EBattleRing::Middle, MiddleCount);
	CreateSlotsForRing(EBattleRing::Outer, OuterCount);

	RecalculateSlotPositions();
}

void AAICombatCoordinator::RecalculateSlotPositions()
{
	if (!PrimaryTarget.IsValid()) return;

	const FVector PlayerPos = PrimaryTarget->GetActorLocation();
	LastSlotCalcPlayerPosition = PlayerPos;

	for (FBattleSlot& Slot : BattleSlots)
	{
		const float Radius = GetRingMidRadius(Slot.Ring);
		const float AngleRad = FMath::DegreesToRadians(Slot.AngleDeg);
		Slot.WorldPosition = PlayerPos + FVector(
			FMath::Cos(AngleRad) * Radius,
			FMath::Sin(AngleRad) * Radius,
			0.0f
		);
	}
}

float AAICombatCoordinator::GetRingMidRadius(EBattleRing Ring) const
{
	switch (Ring)
	{
	case EBattleRing::Inner: return (InnerRingMinRadius + InnerRingMaxRadius) * 0.5f;
	case EBattleRing::Middle: return (MiddleRingMinRadius + MiddleRingMaxRadius) * 0.5f;
	case EBattleRing::Outer: return (OuterRingMinRadius + OuterRingMaxRadius) * 0.5f;
	}
	return MiddleRingMinRadius;
}

EBattleRing AAICombatCoordinator::GetPreferredRing(const FRegisteredNPCData& Data) const
{
	if (!Data.NPC.IsValid()) return EBattleRing::Middle;

	// Role overrides for pressure system
	if (Data.Role == EAICombatRole::Pressurer)
	{
		if (CachedPlayerState.bIsValid && CachedPlayerState.HPPercent <= LowHPThreshold)
		{
			if (Cast<AMeleeNPC>(Data.NPC.Get())) return EBattleRing::Inner;
		}
		if (CachedPlayerState.bIsValid && CachedPlayerState.ArmorPercent <= LowArmorThreshold)
		{
			return EBattleRing::Middle;
		}
	}

	if (Data.Role == EAICombatRole::Aggressor)
	{
		if (Cast<AMeleeNPC>(Data.NPC.Get())) return EBattleRing::Inner;
		return EBattleRing::Middle;
	}

	// Type-based defaults
	if (Cast<AMeleeNPC>(Data.NPC.Get())) return EBattleRing::Inner;
	if (Cast<AFlyingDrone>(Data.NPC.Get())) return EBattleRing::Outer;
	return EBattleRing::Middle;
}

void AAICombatCoordinator::AssignNPCsToSlots()
{
	// Clear all assignments
	for (FBattleSlot& Slot : BattleSlots)
	{
		Slot.AssignedNPC = nullptr;
	}
	for (FRegisteredNPCData& Data : RegisteredNPCs)
	{
		Data.AssignedSlotIndex = -1;
		Data.AssignedSlotPosition = FVector::ZeroVector;
	}

	// Build list of unassigned NPCs
	TArray<int32> UnassignedNPCIndices;
	for (int32 i = 0; i < RegisteredNPCs.Num(); ++i)
	{
		if (RegisteredNPCs[i].NPC.IsValid())
		{
			UnassignedNPCIndices.Add(i);
		}
	}

	// Two-pass: pass 0 = preferred ring only, pass 1 = any ring
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 SlotIdx = 0; SlotIdx < BattleSlots.Num(); ++SlotIdx)
		{
			FBattleSlot& Slot = BattleSlots[SlotIdx];
			if (Slot.IsOccupied()) continue;

			int32 BestNPCArrayIdx = -1;
			float BestDist = MAX_FLT;

			for (int32 k = 0; k < UnassignedNPCIndices.Num(); ++k)
			{
				int32 NPCIdx = UnassignedNPCIndices[k];
				const FRegisteredNPCData& Data = RegisteredNPCs[NPCIdx];

				if (Pass == 0 && GetPreferredRing(Data) != Slot.Ring) continue;

				const float Dist = FVector::Dist(Data.NPC->GetActorLocation(), Slot.WorldPosition);
				if (Dist < BestDist)
				{
					BestDist = Dist;
					BestNPCArrayIdx = k;
				}
			}

			if (BestNPCArrayIdx >= 0)
			{
				int32 NPCIdx = UnassignedNPCIndices[BestNPCArrayIdx];
				Slot.AssignedNPC = RegisteredNPCs[NPCIdx].NPC;
				RegisteredNPCs[NPCIdx].AssignedSlotIndex = SlotIdx;
				RegisteredNPCs[NPCIdx].AssignedSlotPosition = Slot.WorldPosition;
				UnassignedNPCIndices.RemoveAtSwap(BestNPCArrayIdx);
			}
		}
	}
}

bool AAICombatCoordinator::GetAssignedSlotPosition(APawn* NPC, FVector& OutPosition) const
{
	if (!bUseBattleCircle) return false;

	const FRegisteredNPCData* Data = FindNPCData(NPC);
	if (!Data || Data->AssignedSlotIndex < 0) return false;

	OutPosition = Data->AssignedSlotPosition;
	return true;
}

EBattleRing AAICombatCoordinator::GetNPCRing(APawn* NPC) const
{
	const FRegisteredNPCData* Data = FindNPCData(NPC);
	if (!Data || Data->AssignedSlotIndex < 0) return EBattleRing::Middle;
	if (Data->AssignedSlotIndex < BattleSlots.Num())
	{
		return BattleSlots[Data->AssignedSlotIndex].Ring;
	}
	return EBattleRing::Middle;
}

// ==================== Role & Pressure ====================

void AAICombatCoordinator::UpdatePlayerStateCache()
{
	CachedPlayerState.bIsValid = false;

	if (!PrimaryTarget.IsValid()) return;

	AShooterCharacter* Player = Cast<AShooterCharacter>(PrimaryTarget.Get());
	if (!Player) return;

	CachedPlayerState.HPPercent = Player->GetCurrentHP() / FMath::Max(1.0f, Player->GetMaxHP());
	CachedPlayerState.ArmorPercent = Player->GetCurrentArmor() / FMath::Max(1.0f, Player->GetMaxArmor());
	CachedPlayerState.Speed = Player->GetVelocity().Size();
	CachedPlayerState.Position = Player->GetActorLocation();

	if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
	{
		CachedPlayerState.FacingDirection = PC->GetControlRotation().Vector();
	}

	CachedPlayerState.bIsValid = true;
}

void AAICombatCoordinator::AssignRoles()
{
	if (!CachedPlayerState.bIsValid) return;

	bool bHasAggressor = false;

	// Calculate angles
	for (FRegisteredNPCData& Data : RegisteredNPCs)
	{
		if (!Data.NPC.IsValid()) continue;
		Data.AngleToPlayerFacing = CalculateAngleFromPlayerFacing(Data.NPC.Get());
	}

	// Sort by distance (closest first) using index array
	TArray<int32> SortedIndices;
	for (int32 i = 0; i < RegisteredNPCs.Num(); ++i)
	{
		if (RegisteredNPCs[i].NPC.IsValid())
		{
			SortedIndices.Add(i);
		}
	}
	SortedIndices.Sort([this](int32 A, int32 B)
	{
		return GetDistanceToTarget(RegisteredNPCs[A].NPC.Get()) <
			   GetDistanceToTarget(RegisteredNPCs[B].NPC.Get());
	});

	for (int32 Idx : SortedIndices)
	{
		FRegisteredNPCData& Data = RegisteredNPCs[Idx];

		// Currently attacking → Aggressor
		if (Data.bIsCurrentlyAttacking || Data.bHasAttackPermission)
		{
			Data.Role = EAICombatRole::Aggressor;
			bHasAggressor = true;
			continue;
		}

		// Flanker: angle > threshold from player facing
		if (Data.AngleToPlayerFacing >= FlankerMinAngle)
		{
			Data.Role = EAICombatRole::Flanker;
			continue;
		}

		// Pressurer: low HP + melee → push for health drops
		if (CachedPlayerState.HPPercent <= LowHPThreshold && Cast<AMeleeNPC>(Data.NPC.Get()))
		{
			Data.Role = EAICombatRole::Pressurer;
			continue;
		}

		// Pressurer: no armor → group up for channeling kills
		if (CachedPlayerState.ArmorPercent <= LowArmorThreshold)
		{
			Data.Role = EAICombatRole::Pressurer;
			continue;
		}

		// Default
		Data.Role = EAICombatRole::Supporter;
	}

	// Guarantee at least 1 Aggressor
	if (!bHasAggressor && SortedIndices.Num() > 0)
	{
		for (int32 Idx : SortedIndices)
		{
			FRegisteredNPCData& Data = RegisteredNPCs[Idx];
			if (Data.Role != EAICombatRole::Flanker)
			{
				Data.Role = EAICombatRole::Aggressor;
				bHasAggressor = true;
				break;
			}
		}
		// If all are flankers, force closest
		if (!bHasAggressor)
		{
			RegisteredNPCs[SortedIndices[0]].Role = EAICombatRole::Aggressor;
		}
	}
}

float AAICombatCoordinator::CalculateAngleFromPlayerFacing(APawn* NPC) const
{
	if (!NPC || !CachedPlayerState.bIsValid) return 0.0f;

	const FVector ToNPC = (NPC->GetActorLocation() - CachedPlayerState.Position).GetSafeNormal2D();
	const FVector PlayerFwd = CachedPlayerState.FacingDirection.GetSafeNormal2D();

	const float DotProduct = FVector::DotProduct(PlayerFwd, ToNPC);
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(DotProduct, -1.0f, 1.0f)));
}

// ==================== Roles API ====================

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

// ==================== Core Helpers ====================

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
		if (IsNPCInEngagementRange(Data.NPC.Get()))
		{
			Data.AttackScore = CalculateAttackScore(Data);
		}
		else
		{
			Data.AttackScore = 0.0f;
		}
	}
}

float AAICombatCoordinator::CalculateAttackScore(const FRegisteredNPCData& Data) const
{
	if (!Data.NPC.IsValid() || !PrimaryTarget.IsValid()) return 0.0f;

	float Score = 0.0f;

	const float Distance = FVector::Dist(Data.NPC->GetActorLocation(), PrimaryTarget->GetActorLocation());
	const float NormalizedDistance = 1.0f - FMath::Clamp(Distance / MaxScoringDistance, 0.0f, 1.0f);
	Score += NormalizedDistance * DistanceWeight;

	if (HasLineOfSightToTarget(Data.NPC.Get()))
	{
		Score += LineOfSightWeight;
	}

	Score += Data.WaitTime * WaitTimeWeight;

	return Score;
}

bool AAICombatCoordinator::HasLineOfSightToTarget(APawn* NPC) const
{
	if (!NPC || !PrimaryTarget.IsValid()) return false;

	FHitResult HitResult;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(NPC);

	const FVector Start = NPC->GetPawnViewLocation();
	const FVector End = PrimaryTarget->GetActorLocation();

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		HitResult, Start, End, ECC_Visibility, QueryParams
	);

	return !bHit || HitResult.GetActor() == PrimaryTarget.Get();
}

void AAICombatCoordinator::CleanupInvalidNPCs()
{
	RegisteredNPCs.RemoveAll([](const FRegisteredNPCData& Data)
	{
		if (!Data.NPC.IsValid()) return true;

		if (AShooterNPC* ShooterNPC = Cast<AShooterNPC>(Data.NPC.Get()))
		{
			if (ShooterNPC->IsDead()) return true;
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
				Data.AttackingTime += DeltaTime;

				bool bStillShooting = false;
				if (AShooterNPC* ShooterNPC = Cast<AShooterNPC>(Data.NPC.Get()))
				{
					bStillShooting = ShooterNPC->IsCurrentlyShooting();
				}

				if (!bStillShooting || Data.AttackingTime >= MaxAttackingTime)
				{
					if (Data.bHasToken)
					{
						ReleaseAttackToken(Data.NPC.Get());
						Data.bHasToken = false;
					}
					Data.bHasAttackPermission = false;
					Data.bIsCurrentlyAttacking = false;
					Data.AttackingTime = 0.0f;
					Data.PermissionTime = 0.0f;
					Data.Role = EAICombatRole::Supporter;
				}
			}
			else
			{
				Data.PermissionTime += DeltaTime;

				if (Data.PermissionTime >= AttackPermissionTimeout)
				{
					if (Data.bHasToken)
					{
						ReleaseAttackToken(Data.NPC.Get());
						Data.bHasToken = false;
					}
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
		if ((Data.bHasAttackPermission || Data.bIsCurrentlyAttacking || Data.bProximityOverride)
			&& IsNPCInEngagementRange(Data.NPC.Get()))
		{
			++Count;
		}
	}
	return Count;
}

bool AAICombatCoordinator::IsNPCInEngagementRange(APawn* NPC) const
{
	if (MaxEngagementDistance <= 0.0f) return true;
	if (!PrimaryTarget.IsValid()) return true;

	const float Distance = GetDistanceToTarget(NPC);
	return Distance <= MaxEngagementDistance;
}

float AAICombatCoordinator::GetDistanceToTarget(APawn* NPC) const
{
	if (!NPC || !PrimaryTarget.IsValid()) return MAX_FLT;
	return FVector::Dist(NPC->GetActorLocation(), PrimaryTarget->GetActorLocation());
}

// ==================== Debug Drawing ====================

void AAICombatCoordinator::DrawDebugInfo()
{
	if (!GetWorld()) return;

	const float DebugDuration = 0.0f;

	// Engagement range
	if (PrimaryTarget.IsValid() && MaxEngagementDistance > 0.0f)
	{
		DrawDebugSphere(GetWorld(), PrimaryTarget->GetActorLocation(), MaxEngagementDistance,
			24, FColor::Green, false, DebugDuration, 0, 5.0f);
	}

	// Per-NPC status
	for (const FRegisteredNPCData& Data : RegisteredNPCs)
	{
		if (!Data.NPC.IsValid()) continue;

		const FVector NPCLocation = Data.NPC->GetActorLocation();
		const FVector HeadLocation = NPCLocation + FVector(0, 0, 100.0f);

		FColor StatusColor;
		FString StatusText;

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
		else if (Data.bProximityOverride)
		{
			StatusColor = FColor::White;
			StatusText = TEXT("PROX");
		}
		else if (Data.bIsCurrentlyAttacking)
		{
			StatusColor = DebugColorAttacking;
			StatusText = FString::Printf(TEXT("ATTACKING (%.1fs)"), Data.AttackingTime);
		}
		else if (Data.bHasAttackPermission)
		{
			StatusColor = FColor::Orange;
			StatusText = FString::Printf(TEXT("PERMISSION (%.1fs)"), Data.PermissionTime);
		}
		else
		{
			StatusColor = DebugColorWaiting;
			StatusText = FString::Printf(TEXT("WAITING (%.1fs)"), Data.WaitTime);
		}

		// Token info
		FString TokenText;
		if (Data.bHasToken)
		{
			switch (Data.TokenType)
			{
			case EAttackTokenType::Ranged: TokenText = TEXT("TOKEN:R"); break;
			case EAttackTokenType::Melee: TokenText = TEXT("TOKEN:M"); break;
			case EAttackTokenType::Special: TokenText = TEXT("TOKEN:S"); break;
			}
		}
		else if (Data.bProximityOverride)
		{
			TokenText = TEXT("PROX");
		}
		else
		{
			TokenText = TEXT("NO TOKEN");
		}

		DrawDebugSphere(GetWorld(), HeadLocation, 25.0f, 8, StatusColor, false, DebugDuration, 0, 2.0f);

		if (Data.bIsCurrentlyAttacking && PrimaryTarget.IsValid())
		{
			DrawDebugLine(GetWorld(), NPCLocation, PrimaryTarget->GetActorLocation(),
				DebugColorAttacking, false, DebugDuration, 0, 3.0f);
		}

		DrawDebugString(GetWorld(), HeadLocation + FVector(0, 0, 30.0f),
			FString::Printf(TEXT("%s\n%s\nScore: %.1f"), *StatusText, *TokenText, Data.AttackScore),
			nullptr, StatusColor, DebugDuration, true, 1.0f);
	}

	// Token pool summary
	if (PrimaryTarget.IsValid())
	{
		const FVector StatsLocation = PrimaryTarget->GetActorLocation() + FVector(0, 0, 200.0f);
		DrawDebugString(GetWorld(), StatsLocation,
			FString::Printf(TEXT("Attackers: %d / %d\nTokens R:%d/%d M:%d/%d S:%d/%d\nRegistered: %d"),
				CountCurrentAttackers(), MaxSimultaneousAttackers,
				RangedTokenPool.MaxTokens - RangedTokenPool.GetAvailableCount(), RangedTokenPool.MaxTokens,
				MeleeTokenPool.MaxTokens - MeleeTokenPool.GetAvailableCount(), MeleeTokenPool.MaxTokens,
				SpecialTokenPool.MaxTokens - SpecialTokenPool.GetAvailableCount(), SpecialTokenPool.MaxTokens,
				RegisteredNPCs.Num()),
			nullptr, FColor::White, DebugDuration, true, 1.2f);
	}
}

void AAICombatCoordinator::DrawBattleCircleDebug()
{
	if (!PrimaryTarget.IsValid() || !GetWorld()) return;

	const FVector PlayerPos = PrimaryTarget->GetActorLocation();
	const float DebugDuration = 0.0f;

	// Draw ring circles
	auto DrawRingCircle = [this, &PlayerPos, DebugDuration](float Radius, FColor Color)
	{
		DrawDebugCircle(GetWorld(), PlayerPos, Radius, 48, Color, false, DebugDuration, 0, 3.0f,
			FVector(1, 0, 0), FVector(0, 1, 0), false);
	};

	DrawRingCircle(InnerRingMinRadius, DebugColorInnerRing);
	DrawRingCircle(InnerRingMaxRadius, DebugColorInnerRing);
	DrawRingCircle(MiddleRingMinRadius, DebugColorMiddleRing);
	DrawRingCircle(MiddleRingMaxRadius, DebugColorMiddleRing);
	DrawRingCircle(OuterRingMinRadius, DebugColorOuterRing);
	DrawRingCircle(OuterRingMaxRadius, DebugColorOuterRing);

	// Draw each slot
	for (const FBattleSlot& Slot : BattleSlots)
	{
		FColor SlotColor;
		switch (Slot.Ring)
		{
		case EBattleRing::Inner: SlotColor = DebugColorInnerRing; break;
		case EBattleRing::Middle: SlotColor = DebugColorMiddleRing; break;
		case EBattleRing::Outer: SlotColor = DebugColorOuterRing; break;
		default: SlotColor = FColor::White; break;
		}

		if (!Slot.IsOccupied())
		{
			SlotColor = FColor(SlotColor.R / 2, SlotColor.G / 2, SlotColor.B / 2);
		}

		DrawDebugSphere(GetWorld(), Slot.WorldPosition, 40.0f, 8, SlotColor, false, DebugDuration, 0, 2.0f);

		if (Slot.IsOccupied())
		{
			DrawDebugLine(GetWorld(), Slot.WorldPosition, Slot.AssignedNPC->GetActorLocation(),
				SlotColor, false, DebugDuration, 0, 1.5f);
		}
	}
}

void AAICombatCoordinator::DrawRoleDebug()
{
	if (!GetWorld()) return;

	const float DebugDuration = 0.0f;

	// Player state overlay
	if (PrimaryTarget.IsValid() && CachedPlayerState.bIsValid)
	{
		const FVector PlayerLoc = PrimaryTarget->GetActorLocation();

		// Facing direction arrow
		DrawDebugDirectionalArrow(GetWorld(), PlayerLoc,
			PlayerLoc + CachedPlayerState.FacingDirection * 300.0f,
			50.0f, FColor::White, false, DebugDuration, 0, 3.0f);

		// Flanker angle cone boundaries
		const FVector FacingDir2D = CachedPlayerState.FacingDirection.GetSafeNormal2D();
		const float AngleRad = FMath::DegreesToRadians(FlankerMinAngle);
		const float ConeLen = 500.0f;

		// Rotate facing direction by +/- FlankerMinAngle
		const FVector LeftBound = FacingDir2D.RotateAngleAxis(FlankerMinAngle, FVector::UpVector) * ConeLen;
		const FVector RightBound = FacingDir2D.RotateAngleAxis(-FlankerMinAngle, FVector::UpVector) * ConeLen;

		DrawDebugLine(GetWorld(), PlayerLoc, PlayerLoc + LeftBound,
			FColor(128, 0, 128), false, DebugDuration, 0, 2.0f);
		DrawDebugLine(GetWorld(), PlayerLoc, PlayerLoc + RightBound,
			FColor(128, 0, 128), false, DebugDuration, 0, 2.0f);

		// Pressure status
		FString PressureText = FString::Printf(TEXT("HP: %.0f%%  Armor: %.0f%%"),
			CachedPlayerState.HPPercent * 100.0f, CachedPlayerState.ArmorPercent * 100.0f);
		if (CachedPlayerState.HPPercent <= LowHPThreshold)
			PressureText += TEXT(" [LOW HP - PUSH MELEE]");
		if (CachedPlayerState.ArmorPercent <= LowArmorThreshold)
			PressureText += TEXT(" [NO ARMOR - GROUP UP]");

		DrawDebugString(GetWorld(), PlayerLoc + FVector(0, 0, 350.0f),
			PressureText, nullptr, FColor::White, DebugDuration, true, 1.0f);
	}

	// Per-NPC role display
	for (const FRegisteredNPCData& Data : RegisteredNPCs)
	{
		if (!Data.NPC.IsValid()) continue;

		FColor RoleColor;
		FString RoleName;
		switch (Data.Role)
		{
		case EAICombatRole::Aggressor:  RoleColor = DebugColorAggressor;  RoleName = TEXT("AGGRESSOR"); break;
		case EAICombatRole::Supporter:  RoleColor = DebugColorWaiting;    RoleName = TEXT("SUPPORTER"); break;
		case EAICombatRole::Flanker:    RoleColor = DebugColorFlanker;    RoleName = TEXT("FLANKER"); break;
		case EAICombatRole::Pressurer:  RoleColor = DebugColorPressurer;  RoleName = TEXT("PRESSURER"); break;
		default: RoleColor = FColor::White; RoleName = TEXT("UNKNOWN"); break;
		}

		const FVector NPCLoc = Data.NPC->GetActorLocation() + FVector(0, 0, 160.0f);
		DrawDebugString(GetWorld(), NPCLoc,
			FString::Printf(TEXT("%s (%.0f deg)"), *RoleName, Data.AngleToPlayerFacing),
			nullptr, RoleColor, DebugDuration, true, 0.8f);
	}
}
