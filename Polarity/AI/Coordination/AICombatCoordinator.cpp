// AICombatCoordinator.cpp

#include "AICombatCoordinator.h"
#include "Coop/CoopPlayers.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "ShooterNPC.h"
#include "MeleeNPC.h"
#include "FlyingDrone.h"
#include "KamikazeDroneNPC.h"
#include "ShooterCharacter.h"
#include "ShooterAIController.h"
#include "ThreatComponent.h"

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

	// Debug drawing on by default while the group work is being looked at. This actor is spawned
	// from C++ rather than placed, so there is no Blueprint to tick these on in, and turning them
	// off means editing here again. Turn them off when the formations stop being interesting.
	bDrawDebug = true;
	bDrawBattleCircle = true;
	bDrawRoleDebug = true;
}

void AAICombatCoordinator::BeginPlay()
{
	Super::BeginPlay();
	Instance = this;

	// Each NPC now carries its own remembered target (FRegisteredNPCData::Target), and PrimaryTarget
	// is derived from those every tick: whoever the most enemies are actually fighting.
	//
	// TODO(COOP): what is still single-target is the ARRANGEMENT, not the choice. Battle slots, the
	// player state cache and the strafe rings all hang off PrimaryTarget, so with four players the
	// enemies pick their own opponents correctly but their formations are all laid out around the
	// busiest one. The shape of the fix is known — group the NPCs by their target and give each group
	// its own slot ring and its own token pools, with GetEffectiveMaxAttackers as the ceiling over
	// all of them — and it is the next piece of work here.
	//
	// TODO(COOP): threat. UpdateNPCTargets scores candidates by raw distance; the design calls for
	// distance scaled by a per-player threat value that loud actions spike and that decays in a few
	// seconds. There is exactly one place to add it now, which is why this was done first.
	PrimaryTarget = CoopPlayers::GetNearest(GetWorld(), GetActorLocation());
}

void AAICombatCoordinator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	TimeSinceLastAttackGrant += DeltaTime;

	// Cleanup first: targeting below walks the registered list and should not walk corpses.
	CleanupInvalidNPCs();

	// Who each NPC is fighting, remembered between frames. This also derives PrimaryTarget, so the
	// old "re-find the nearest player to the coordinator actor" is gone — that answer had nothing to
	// do with where the fighting was.
	UpdateNPCTargets(DeltaTime);

	if (!PrimaryTarget.IsValid())
	{
		PrimaryTarget = CoopPlayers::GetNearest(GetWorld(), GetActorLocation());
	}

	// Groups follow from the targets decided just above.
	RebuildTargetGroups();

	// Token pools
	UpdateTokenPools();
	UpdateKamikazeTokenPoolSize();
	UpdateProximityOverrides();
	for (FTargetGroup& Group : Groups)
	{
		Group.Ranged.CleanupInvalid();
		Group.Melee.CleanupInvalid();
		Group.Special.CleanupInvalid();
	}
	KamikazeTokenPool.CleanupInvalid();

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

	// Player state cache, one per group
	for (FTargetGroup& Group : Groups)
	{
		UpdatePlayerStateCacheForGroup(Group);
	}

	// Role assignment
	AssignRoles();

	// Battle Circle, one ring per group. Each group keeps its own clock and its own member count, so
	// an enemy joining the fight around one player does not rebuild the formation around another.
	if (bUseBattleCircle)
	{
		for (FTargetGroup& Group : Groups)
		{
			Group.TimeSinceLastSlotRecalc += DeltaTime;

			const int32 ActiveNPCCount = Group.Members.Num();
			if (ActiveNPCCount != Group.LastSlotNPCCount || Group.BattleSlots.Num() == 0)
			{
				GenerateBattleSlotsForGroup(Group);
				AssignNPCsToSlotsForGroup(Group);
				Group.LastSlotNPCCount = ActiveNPCCount;
				Group.TimeSinceLastSlotRecalc = 0.0f;
			}
			else if (Group.TimeSinceLastSlotRecalc >= SlotRecalculationInterval)
			{
				RecalculateSlotPositionsForGroup(Group);
				AssignNPCsToSlotsForGroup(Group);
				Group.TimeSinceLastSlotRecalc = 0.0f;
			}
		}
	}

	// Enemy cluster direction (for kamikaze orbit bias)
	TimeSinceLastClusterCalc += DeltaTime;
	if (TimeSinceLastClusterCalc >= 0.5f)
	{
		UpdateEnemyClusterDirection();
		TimeSinceLastClusterCalc = 0.0f;
	}

	// State snapshot to the log, so this can be read after the fact and compared between machines
	// instead of depending on somebody watching the right screen at the right moment.
	if (bLogStateSnapshot)
	{
		TimeSinceLastSnapshot += DeltaTime;
		if (TimeSinceLastSnapshot >= StateSnapshotInterval)
		{
			LogStateSnapshot();
			TimeSinceLastSnapshot = 0.0f;
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
	for (FTargetGroup& Group : Groups)
	{
		Group.Ranged.Release(NPC);
		Group.Melee.Release(NPC);
		Group.Special.Release(NPC);
	}
	KamikazeTokenPool.Release(NPC);

	// Release strafe slot
	ReleaseStrafeSlot(NPC);

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
	if (Cast<AKamikazeDroneNPC>(NPC)) return EAttackTokenType::Kamikaze;
	if (Cast<AMeleeNPC>(NPC)) return EAttackTokenType::Melee;
	// FlyingDrone and ShooterNPC both use ranged
	return EAttackTokenType::Ranged;
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

	FTokenPool* PoolPtr = GetPoolFor(Requester, TokenType);
	if (!PoolPtr)
	{
		// No group yet: this NPC has not been given a target, which happens on the first tick after
		// it registers. Said out loud rather than returning a silent false — a permanently groupless
		// NPC would never attack anybody and nothing else would report it.
		UE_LOG(LogTemp, Verbose, TEXT("[COOP_DEBUG] %s asked for a token with no target group yet"),
			*GetNameSafe(Requester));
		return false;
	}
	FTokenPool& Pool = *PoolPtr;

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

	// Kamikaze stagger: enforce delay between kamikaze token grants
	if (TokenType == EAttackTokenType::Kamikaze)
	{
		const float TimeSinceLastKamikaze = GetWorld()->GetTimeSeconds() - LastKamikazeTokenGrantTime;
		const float RequiredDelay = KamikazeStaggerDelay + FMath::FRandRange(0.0f, KamikazeStaggerRandom);
		if (TimeSinceLastKamikaze < RequiredDelay)
		{
			return false;
		}
	}

	// Try to acquire from pool
	if (Pool.TryAcquire(Requester))
	{
		if (TokenType == EAttackTokenType::Kamikaze)
		{
			LastKamikazeTokenGrantTime = GetWorld()->GetTimeSeconds();
		}
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

	// Released from every group, not just the one it belongs to now: an NPC that switched target
	// while holding a token would otherwise leave it behind in its old group forever.
	for (FTargetGroup& Group : Groups)
	{
		Group.Ranged.Release(Attacker);
		Group.Melee.Release(Attacker);
		Group.Special.Release(Attacker);
	}
}

bool AAICombatCoordinator::HasAttackToken(APawn* NPC) const
{
	if (!NPC) return false;

	const FRegisteredNPCData* Data = FindNPCData(NPC);
	if (Data && Data->bProximityOverride) return true;

	const FTargetGroup* Group = FindGroupFor(NPC);
	if (!Group)
	{
		return false;
	}

	return Group->Ranged.HasToken(NPC)
		|| Group->Melee.HasToken(NPC)
		|| Group->Special.HasToken(NPC);
}

bool AAICombatCoordinator::TryStealToken(APawn* Requester, FTokenPool& Pool)
{
	// No PrimaryTarget check any more: stealing happens within one group, so the only target that
	// matters is the requester's own, and the gates below already ask about it.
	if (!Requester) return false;

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
	for (FRegisteredNPCData& Data : RegisteredNPCs)
	{
		if (!Data.NPC.IsValid()) continue;
		// Distance to THIS NPC's own target. @see ResolveTargetFor
		const float Dist = GetDistanceToTarget(Data.NPC.Get());
		Data.bProximityOverride = (Dist <= ProximityOverrideDistance);
	}
}

void AAICombatCoordinator::UpdateTokenPools()
{
	// Every group gets the same per-player allowance. The team-size scaling lives in the global
	// ceiling instead, so adding a player adds a group rather than crowding an existing one.
	for (FTargetGroup& Group : Groups)
	{
		Group.Ranged.MaxTokens  = MaxRangedTokens;
		Group.Melee.MaxTokens   = MaxMeleeTokens;
		Group.Special.MaxTokens = MaxSpecialTokens;
	}
}

// ==================== Battle Circle ====================

void AAICombatCoordinator::GenerateBattleSlotsForGroup(FTargetGroup& Group)
{
	Group.BattleSlots.Empty();

	// Count only THIS group's NPCs per preferred ring. The ring around a player is sized by the
	// enemies fighting that player, not by everyone alive on the level.
	int32 InnerCount = 0, MiddleCount = 0, OuterCount = 0;
	for (int32 Index : Group.Members)
	{
		if (!RegisteredNPCs.IsValidIndex(Index)) continue;
		const FRegisteredNPCData& Data = RegisteredNPCs[Index];
		if (!Data.NPC.IsValid()) continue;
		EBattleRing Ring = GetPreferredRing(Data);
		switch (Ring)
		{
		case EBattleRing::Inner: InnerCount++; break;
		case EBattleRing::Middle: MiddleCount++; break;
		case EBattleRing::Outer: OuterCount++; break;
		}
	}

	auto CreateSlotsForRing = [&Group](EBattleRing Ring, int32 Count)
	{
		if (Count <= 0) return;
		const float AngleStep = 360.0f / Count;
		const float RandomOffset = FMath::FRandRange(0.0f, AngleStep);
		for (int32 i = 0; i < Count; ++i)
		{
			FBattleSlot Slot;
			Slot.Ring = Ring;
			Slot.AngleDeg = FMath::Fmod(RandomOffset + i * AngleStep, 360.0f);
			Group.BattleSlots.Add(Slot);
		}
	};

	CreateSlotsForRing(EBattleRing::Inner, InnerCount);
	CreateSlotsForRing(EBattleRing::Middle, MiddleCount);
	CreateSlotsForRing(EBattleRing::Outer, OuterCount);

	RecalculateSlotPositionsForGroup(Group);
}

void AAICombatCoordinator::RecalculateSlotPositionsForGroup(FTargetGroup& Group)
{
	AActor* Target = Group.Target.Get();
	if (!Target) return;

	const FVector PlayerPos = Target->GetActorLocation();

	for (FBattleSlot& Slot : Group.BattleSlots)
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

	// Role overrides for pressure system. Read from the group this NPC belongs to: pressure is a
	// response to how the player IT is fighting is doing, not to whoever is worst off on the level.
	const FTargetGroup* Group = Groups.IsValidIndex(Data.GroupIndex) ? &Groups[Data.GroupIndex] : nullptr;
	if (Data.Role == EAICombatRole::Pressurer && Group && Group->State.bIsValid)
	{
		if (Group->State.HPPercent <= LowHPThreshold)
		{
			if (Cast<AMeleeNPC>(Data.NPC.Get())) return EBattleRing::Inner;
		}
		if (Group->State.ArmorPercent <= LowArmorThreshold)
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
	if (Cast<AKamikazeDroneNPC>(Data.NPC.Get())) return EBattleRing::Middle;
	if (Cast<AFlyingDrone>(Data.NPC.Get())) return EBattleRing::Outer;
	return EBattleRing::Middle;
}

void AAICombatCoordinator::AssignNPCsToSlotsForGroup(FTargetGroup& Group)
{
	// Clear this group's assignments only. Touching every registered NPC here would wipe the slot
	// another group just handed out.
	for (FBattleSlot& Slot : Group.BattleSlots)
	{
		Slot.AssignedNPC = nullptr;
	}
	for (int32 Index : Group.Members)
	{
		if (!RegisteredNPCs.IsValidIndex(Index)) continue;
		FRegisteredNPCData& Data = RegisteredNPCs[Index];
		Data.AssignedSlotIndex = -1;
		Data.AssignedSlotPosition = FVector::ZeroVector;
	}

	// Only this group's NPCs compete for this group's slots.
	TArray<int32> UnassignedNPCIndices;
	for (int32 Index : Group.Members)
	{
		if (RegisteredNPCs.IsValidIndex(Index) && RegisteredNPCs[Index].NPC.IsValid())
		{
			UnassignedNPCIndices.Add(Index);
		}
	}

	// Two-pass: pass 0 = preferred ring only, pass 1 = any ring
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 SlotIdx = 0; SlotIdx < Group.BattleSlots.Num(); ++SlotIdx)
		{
			FBattleSlot& Slot = Group.BattleSlots[SlotIdx];
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
	const FTargetGroup* Group = FindGroupFor(NPC);
	if (Group && Group->BattleSlots.IsValidIndex(Data->AssignedSlotIndex))
	{
		return Group->BattleSlots[Data->AssignedSlotIndex].Ring;
	}
	return EBattleRing::Middle;
}

// ==================== Role & Pressure ====================

void AAICombatCoordinator::UpdateNPCTargets(float DeltaTime)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	PruneDecoys();

	TArray<APawn*> Players;
	CoopPlayers::GetAll(World, Players);
	if (Players.Num() == 0)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	for (FRegisteredNPCData& Data : RegisteredNPCs)
	{
		APawn* NPC = Data.NPC.Get();
		if (!NPC)
		{
			continue;
		}

		const FVector NPCLocation = NPC->GetActorLocation();

		// A decoy beats every player inside its radius, and it takes effect on the frame it lands.
		// Neither the switch margin nor the switch delay applies: those two exist to stop an enemy
		// flickering between two teammates whose distances keep swapping, and a decoy is a single
		// loud event at a fixed point. Something that takes three quarters of a second to notice a
		// bang going off next to it does not read as being lured, it reads as being slow.
		if (AActor* Decoy = FindDecoyFor(NPCLocation))
		{
			const FActiveDecoy* Entry = ActiveDecoys.FindByPredicate(
				[Decoy](const FActiveDecoy& D) { return D.Actor.Get() == Decoy; });
			const float Remaining = Entry ? FMath::Max(0.0f, Entry->ExpiryTime - Now) : 0.0f;

			if (Data.Target.Get() != Decoy)
			{
				UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] %s pulled off %s by decoy %s (%.1fs left)"),
					*NPC->GetName(), *GetNameSafe(Data.Target.Get()), *Decoy->GetName(), Remaining);
			}

			Data.Target = Decoy;
			Data.TargetSwitchPressure = 0.0f;
			ApplyDistraction(NPC, Decoy, Remaining);
			continue;
		}

		// Was fighting a decoy that has just stopped being one — expired, or shot to pieces. Nothing
		// to be loyal to, so it falls through to the ordinary pick below with a clean slate.
		//
		// Recognised by "not a player" rather than by looking the decoy up: PruneDecoys has already
		// removed it by the time this runs, and nothing else ever puts a non-player in here.
		if (AActor* Held = Data.Target.Get(); Held && !CoopPlayers::IsPlayer(Held))
		{
			ClearDistraction(NPC);
			Data.Target.Reset();
			Data.TargetSwitchPressure = 0.0f;
		}

		// Whoever is most worth attacking right now. This is the candidate, NOT the answer — the
		// answer is below, and it is mostly "keep doing what you were doing".
		//
		// Threat divides the distance rather than being added to it, so it reads as "this player
		// looks closer than they are". A threat of 1.0 halves the apparent distance. Scoring and the
		// switch rules are kept apart on purpose: making somebody look closer must not also let them
		// skip the margin and the delay, or provocation becomes a way to reintroduce the flicker
		// those two exist to stop.
		APawn* Nearest = nullptr;
		float NearestDist = TNumericLimits<float>::Max();
		for (APawn* Player : Players)
		{
			const float Dist = GetApparentDistance(NPCLocation, Player);
			if (Dist < NearestDist)
			{
				NearestDist = Dist;
				Nearest = Player;
			}
		}

		AActor* Current = Data.Target.Get();

		// No target, or the one it had is gone: take the nearest with no ceremony. Nothing to be
		// loyal to.
		if (!Current)
		{
			Data.Target = Nearest;
			Data.TargetSwitchPressure = 0.0f;
			continue;
		}

		if (Current == Nearest)
		{
			Data.TargetSwitchPressure = 0.0f;
			continue;
		}

		// Somebody else is closer. Two gates before the enemy turns around, and they exist for
		// different reasons: the margin rejects a tie (two teammates side by side, distances
		// swapping every frame), the delay rejects a pass-through (a teammate sprinting past on the
		// way somewhere else).
		// Measured the same way as the candidate, or the margin would compare a threat-weighted
		// number against a raw one and the enemy would switch on arithmetic rather than on events.
		const float CurrentDist = GetApparentDistance(NPCLocation, Cast<APawn>(Current));
		if (CurrentDist - NearestDist > TargetSwitchMargin)
		{
			Data.TargetSwitchPressure += DeltaTime;
			if (Data.TargetSwitchPressure >= TargetSwitchDelay)
			{
				// Warning, not Verbose: LogTemp Verbose does not print by default, and a switch that
				// leaves no trace is a mechanic nobody can ever confirm on the bench. Hysteresis caps
				// this at one line per NPC per TargetSwitchDelay, so it cannot become spam.
				UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] %s switches from %s to %s (%.0f cm closer for %.2fs)"),
					*NPC->GetName(), *GetNameSafe(Current), *GetNameSafe(Nearest),
					CurrentDist - NearestDist, Data.TargetSwitchPressure);

				Data.Target = Nearest;
				Data.TargetSwitchPressure = 0.0f;
			}
		}
		else
		{
			// Stopped leading. Start over rather than decay: a contender that keeps drifting in and
			// out of the margin should never accumulate its way to a switch.
			Data.TargetSwitchPressure = 0.0f;
		}
	}

	// PrimaryTarget is now derived rather than chosen: whoever the most enemies are actually fighting.
	// Everything still built around a single target (battle slots, the player state cache, the strafe
	// rings) keeps working and now at least points at the busiest player instead of an arbitrary one.
	// Those are the next thing to make per-target; see the TODO(COOP) in BeginPlay.
	TMap<AActor*, int32> Tally;
	for (const FRegisteredNPCData& Data : RegisteredNPCs)
	{
		if (AActor* T = Data.Target.Get())
		{
			Tally.FindOrAdd(T)++;
		}
	}

	AActor* Busiest = nullptr;
	int32 BusiestCount = 0;
	for (const TPair<AActor*, int32>& Pair : Tally)
	{
		if (Pair.Value > BusiestCount)
		{
			BusiestCount = Pair.Value;
			Busiest = Pair.Key;
		}
	}

	if (Busiest)
	{
		PrimaryTarget = Busiest;
	}
}

AActor* AAICombatCoordinator::GetTargetFor(APawn* NPC) const
{
	const FRegisteredNPCData* Data = FindNPCData(NPC);
	return Data ? Data->Target.Get() : nullptr;
}

// ==================== Decoys ====================

void AAICombatCoordinator::RegisterDecoy(AActor* Decoy, float Radius, float Duration)
{
	const UWorld* World = GetWorld();
	if (!Decoy || !World || Radius <= 0.0f || Duration <= 0.0f)
	{
		return;
	}

	const float Expiry = World->GetTimeSeconds() + Duration;

	if (FActiveDecoy* Existing = ActiveDecoys.FindByPredicate(
		[Decoy](const FActiveDecoy& D) { return D.Actor.Get() == Decoy; }))
	{
		Existing->Radius = Radius;
		Existing->ExpiryTime = Expiry;
		return;
	}

	FActiveDecoy Entry;
	Entry.Actor = Decoy;
	Entry.Radius = Radius;
	Entry.ExpiryTime = Expiry;
	ActiveDecoys.Add(Entry);

	UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] Decoy %s armed: radius=%.0f duration=%.1fs"),
		*Decoy->GetName(), Radius, Duration);
}

void AAICombatCoordinator::UnregisterDecoy(AActor* Decoy)
{
	if (!Decoy)
	{
		return;
	}

	const int32 Removed = ActiveDecoys.RemoveAll(
		[Decoy](const FActiveDecoy& D) { return D.Actor.Get() == Decoy; });

	if (Removed == 0)
	{
		return;
	}

	// Let go of everyone holding it in the same call. Waiting for the next tick would be at most a
	// tenth of a second, but the release also has to happen when the prop is destroyed, and by the
	// next tick the pointer is null and there is no way left to tell which NPCs were on it.
	for (FRegisteredNPCData& Data : RegisteredNPCs)
	{
		APawn* NPC = Data.NPC.Get();
		if (!NPC || Data.Target.Get() != Decoy)
		{
			continue;
		}

		ClearDistraction(NPC);
		Data.Target.Reset();
		Data.TargetSwitchPressure = 0.0f;
	}

	UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] Decoy %s is done"), *Decoy->GetName());
}

bool AAICombatCoordinator::IsActiveDecoy(const AActor* Actor) const
{
	const UWorld* World = GetWorld();
	if (!Actor || !World)
	{
		return false;
	}

	const float Now = World->GetTimeSeconds();
	return ActiveDecoys.ContainsByPredicate([Actor, Now](const FActiveDecoy& D)
	{
		return D.Actor.Get() == Actor && Now < D.ExpiryTime;
	});
}

void AAICombatCoordinator::PruneDecoys()
{
	const UWorld* World = GetWorld();
	if (!World || ActiveDecoys.Num() == 0)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	// Collected first and unregistered afterwards, because UnregisterDecoy walks the NPC list and
	// releases whoever was holding it — that is the whole reason a decoy ending has to go through
	// one function rather than being dropped from the array here.
	TArray<AActor*> Finished;
	for (const FActiveDecoy& Entry : ActiveDecoys)
	{
		if (!Entry.Actor.IsValid())
		{
			continue;   // destroyed: nothing to release it from, the pointer is already gone
		}
		if (Now >= Entry.ExpiryTime)
		{
			Finished.Add(Entry.Actor.Get());
		}
	}

	ActiveDecoys.RemoveAll([](const FActiveDecoy& D) { return !D.Actor.IsValid(); });

	for (AActor* Expired : Finished)
	{
		UnregisterDecoy(Expired);
	}
}

AActor* AAICombatCoordinator::FindDecoyFor(const FVector& NPCLocation) const
{
	const UWorld* World = GetWorld();
	if (!World || ActiveDecoys.Num() == 0)
	{
		return nullptr;
	}

	const float Now = World->GetTimeSeconds();

	AActor* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();

	for (const FActiveDecoy& Entry : ActiveDecoys)
	{
		AActor* Actor = Entry.Actor.Get();
		if (!Actor || Now >= Entry.ExpiryTime)
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(NPCLocation, Actor->GetActorLocation());
		if (DistSq > Entry.Radius * Entry.Radius || DistSq >= BestDistSq)
		{
			continue;
		}

		BestDistSq = DistSq;
		Best = Actor;
	}

	return Best;
}

void AAICombatCoordinator::ApplyDistraction(APawn* NPC, AActor* Decoy, float SecondsRemaining)
{
	if (AShooterAIController* AIController = Cast<AShooterAIController>(NPC ? NPC->GetController() : nullptr))
	{
		AIController->DistractTo(Decoy, SecondsRemaining);
	}

	// And say so on the enemy itself, where it can be seen. The controller knows, but a controller
	// has no mesh and does not replicate to the machines that need to draw this.
	if (AShooterNPC* ShooterNPC = Cast<AShooterNPC>(NPC))
	{
		ShooterNPC->SetDistracted(true);
	}
}

void AAICombatCoordinator::ClearDistraction(APawn* NPC)
{
	if (AShooterAIController* AIController = Cast<AShooterAIController>(NPC ? NPC->GetController() : nullptr))
	{
		AIController->EndDistraction();
	}

	if (AShooterNPC* ShooterNPC = Cast<AShooterNPC>(NPC))
	{
		ShooterNPC->SetDistracted(false);
	}
}

int32 AAICombatCoordinator::GetEffectiveMaxAttackers() const
{
	TArray<APawn*> Players;
	CoopPlayers::GetAll(GetWorld(), Players);
	const int32 PlayerCount = FMath::Max(1, Players.Num());

	// One player has to come out exactly at the configured number, or every existing arena is
	// retuned by accident.
	const float Scaled = MaxSimultaneousAttackers * FMath::Pow(static_cast<float>(PlayerCount), PressureScalingExponent);
	return FMath::Max(MaxSimultaneousAttackers, FMath::RoundToInt(Scaled));
}

void AAICombatCoordinator::UpdatePlayerStateCacheForGroup(FTargetGroup& Group)
{
	FPlayerStateCache& Cache = Group.State;
	Cache.bIsValid = false;

	AShooterCharacter* Player = Cast<AShooterCharacter>(Group.Target.Get());
	if (!Player) return;

	Cache.HPPercent = Player->GetCurrentHP() / FMath::Max(1.0f, Player->GetMaxHP());
	Cache.ArmorPercent = Player->GetCurrentArmor() / FMath::Max(1.0f, Player->GetMaxArmor());
	Cache.Speed = Player->GetVelocity().Size();
	Cache.Position = Player->GetActorLocation();

	if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
	{
		Cache.FacingDirection = PC->GetControlRotation().Vector();
	}

	Cache.bIsValid = true;
}

void AAICombatCoordinator::AssignRoles()
{
	// No single-cache gate any more: roles are decided per NPC against its own group's player, and
	// one group having no valid state is no reason to leave every other enemy roleless.
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

		// Pressure is a response to how THIS NPC's own player is doing.
		const FTargetGroup* Group = Groups.IsValidIndex(Data.GroupIndex) ? &Groups[Data.GroupIndex] : nullptr;
		const bool bStateKnown = Group && Group->State.bIsValid;

		// Pressurer: low HP + melee → push for health drops
		if (bStateKnown && Group->State.HPPercent <= LowHPThreshold && Cast<AMeleeNPC>(Data.NPC.Get()))
		{
			Data.Role = EAICombatRole::Pressurer;
			continue;
		}

		// Pressurer: no armor → group up for channeling kills
		if (bStateKnown && Group->State.ArmorPercent <= LowArmorThreshold)
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
	if (!NPC) return 0.0f;

	// The angle is measured against the player THIS NPC is fighting, not against whoever happens to
	// be busiest — it decides whether the enemy counts as a flanker, and flanking is relative to the
	// person being flanked.
	const FTargetGroup* Group = FindGroupFor(NPC);
	if (!Group || !Group->State.bIsValid) return 0.0f;

	const FVector ToNPC = (NPC->GetActorLocation() - Group->State.Position).GetSafeNormal2D();
	const FVector PlayerFwd = Group->State.FacingDirection.GetSafeNormal2D();

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
	if (!Data.NPC.IsValid()) return 0.0f;

	const AActor* Target = ResolveTargetFor(Data.NPC.Get());
	if (!Target) return 0.0f;

	float Score = 0.0f;

	const float Distance = FVector::Dist(Data.NPC->GetActorLocation(), Target->GetActorLocation());
	const float NormalizedDistance = 1.0f - FMath::Clamp(Distance / MaxScoringDistance, 0.0f, 1.0f);
	Score += NormalizedDistance * DistanceWeight;

	if (HasLineOfSightToTarget(Data.NPC.Get()))
	{
		Score += LineOfSightWeight;
	}

	Score += Data.WaitTime * WaitTimeWeight;

	return Score;
}

void AAICombatCoordinator::RebuildTargetGroups()
{
	// Membership is rebuilt from scratch every tick; the groups themselves are not, because their
	// token pools hold live grants. @see FTargetGroup
	for (FTargetGroup& Group : Groups)
	{
		Group.Members.Reset();
	}

	for (int32 i = 0; i < RegisteredNPCs.Num(); ++i)
	{
		FRegisteredNPCData& Data = RegisteredNPCs[i];
		Data.GroupIndex = INDEX_NONE;

		AActor* Target = Data.Target.Get();
		if (!Data.NPC.IsValid() || !Target)
		{
			continue;
		}

		int32 GroupIdx = Groups.IndexOfByPredicate([Target](const FTargetGroup& G)
		{
			return G.Target.Get() == Target;
		});

		if (GroupIdx == INDEX_NONE)
		{
			FTargetGroup NewGroup;
			NewGroup.Target = Target;
			GroupIdx = Groups.Add(MoveTemp(NewGroup));
		}

		Groups[GroupIdx].Members.Add(i);
		Data.GroupIndex = GroupIdx;
	}

	// Retire groups nobody is fighting any more. Removing invalidates indices, so the membership
	// pass above is redone rather than patched — it is a handful of NPCs and it runs at 10Hz.
	const int32 Removed = Groups.RemoveAll([](const FTargetGroup& G)
	{
		return !G.Target.IsValid() || G.Members.Num() == 0;
	});

	if (Removed > 0)
	{
		for (int32 i = 0; i < RegisteredNPCs.Num(); ++i)
		{
			FRegisteredNPCData& Data = RegisteredNPCs[i];
			Data.GroupIndex = Groups.IndexOfByPredicate([&Data](const FTargetGroup& G)
			{
				return G.Target.Get() == Data.Target.Get();
			});
		}
	}
}

FTargetGroup* AAICombatCoordinator::FindGroupFor(APawn* NPC)
{
	const FRegisteredNPCData* Data = FindNPCData(NPC);
	if (!Data || !Groups.IsValidIndex(Data->GroupIndex))
	{
		return nullptr;
	}
	return &Groups[Data->GroupIndex];
}

const FTargetGroup* AAICombatCoordinator::FindGroupFor(APawn* NPC) const
{
	const FRegisteredNPCData* Data = FindNPCData(NPC);
	if (!Data || !Groups.IsValidIndex(Data->GroupIndex))
	{
		return nullptr;
	}
	return &Groups[Data->GroupIndex];
}

FTokenPool* AAICombatCoordinator::GetPoolFor(APawn* NPC, EAttackTokenType Type)
{
	// Kamikaze is deliberately global. @see FTargetGroup
	if (Type == EAttackTokenType::Kamikaze)
	{
		return &KamikazeTokenPool;
	}

	FTargetGroup* Group = FindGroupFor(NPC);
	if (!Group)
	{
		return nullptr;
	}

	switch (Type)
	{
	case EAttackTokenType::Melee:   return &Group->Melee;
	case EAttackTokenType::Special: return &Group->Special;
	default:                        return &Group->Ranged;
	}
}

float AAICombatCoordinator::GetApparentDistance(const FVector& FromLocation, APawn* Player) const
{
	if (!Player)
	{
		return TNumericLimits<float>::Max();
	}

	const float RealDistance = FVector::Dist(FromLocation, Player->GetActorLocation());

	// No component, no threat, and the answer is plain distance — which is exactly how this behaved
	// before threat existed, so a character without one is not a special case to handle anywhere.
	const UThreatComponent* Threat = Player->FindComponentByClass<UThreatComponent>();
	if (!Threat)
	{
		return RealDistance;
	}

	return RealDistance / (1.0f + FMath::Max(0.0f, Threat->GetThreat()));
}

AActor* AAICombatCoordinator::ResolveTargetFor(APawn* NPC) const
{
	// The gates below decide whether an NPC may attack, and they have to ask about the player THAT
	// NPC is fighting. Measuring them against PrimaryTarget was survivable while it was an arbitrary
	// but stable pick; once it became a tally of who the enemies are actually fighting, it started
	// disagreeing with any individual enemy far more often, and the failure was silent and total:
	// a player ran behind cover, the line-of-sight check against THEM failed, and every NPC stopped
	// shooting at the teammate standing in the open in front of it.
	if (const FRegisteredNPCData* Data = FindNPCData(NPC))
	{
		if (AActor* Remembered = Data->Target.Get())
		{
			return Remembered;
		}
	}
	return PrimaryTarget.Get();
}

bool AAICombatCoordinator::HasLineOfSightToTarget(APawn* NPC) const
{
	if (!NPC) return false;

	AActor* Target = ResolveTargetFor(NPC);
	if (!Target) return false;

	FHitResult HitResult;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(NPC);

	const FVector Start = NPC->GetPawnViewLocation();
	const FVector End = Target->GetActorLocation();

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		HitResult, Start, End, ECC_Visibility, QueryParams
	);

	return !bHit || HitResult.GetActor() == Target;
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
	if (!ResolveTargetFor(NPC)) return true;

	const float Distance = GetDistanceToTarget(NPC);
	return Distance <= MaxEngagementDistance;
}

float AAICombatCoordinator::GetDistanceToTarget(APawn* NPC) const
{
	if (!NPC) return MAX_FLT;

	AActor* Target = ResolveTargetFor(NPC);
	if (!Target) return MAX_FLT;

	return FVector::Dist(NPC->GetActorLocation(), Target->GetActorLocation());
}

// ==================== Debug Drawing ====================

void AAICombatCoordinator::DrawDebugInfo()
{
	if (!GetWorld()) return;

	// Long enough to survive until the next redraw. This actor ticks at 10Hz and these were drawn for
	// a single frame, so at 60fps every shape was visible for a sixth of the time and the whole
	// overlay read as a flicker rather than as a diagram.
	const float DebugDuration = PrimaryActorTick.TickInterval * 1.5f;

	// Engagement range
	for (const FTargetGroup& EngagementGroup : Groups)
	{
	if (EngagementGroup.Target.IsValid() && MaxEngagementDistance > 0.0f)
	{
		DrawDebugSphere(GetWorld(), EngagementGroup.Target->GetActorLocation(), MaxEngagementDistance,
			24, FColor::Green, false, DebugDuration, 0, 5.0f);
	}
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

		if (Data.bIsCurrentlyAttacking)
		{
			if (const AActor* AttackTarget = Data.Target.Get())
			{
				DrawDebugLine(GetWorld(), NPCLocation, AttackTarget->GetActorLocation(),
					DebugColorAttacking, false, DebugDuration, 0, 3.0f);
			}
		}

		DrawDebugString(GetWorld(), HeadLocation + FVector(0, 0, 30.0f),
			FString::Printf(TEXT("%s\n%s\nScore: %.1f"), *StatusText, *TokenText, Data.AttackScore),
			nullptr, StatusColor, DebugDuration, true, 1.0f);
	}

	// One summary above each player under attack, showing that group's own pressure. The global
	// ceiling is printed alongside so the two numbers can be read against each other.
	for (int32 GroupIdx = 0; GroupIdx < Groups.Num(); ++GroupIdx)
	{
		const FTargetGroup& Group = Groups[GroupIdx];
		AActor* GroupTarget = Group.Target.Get();
		if (!GroupTarget) continue;

		// Threat is otherwise invisible: it is a number nobody can see, which is the reason it fades
		// in seconds rather than accumulating, and the reason it has to be on the overlay while it is
		// being tuned. Shown in the form it is actually applied in.
		float Threat = 0.0f;
		if (const APawn* TargetPawn = Cast<APawn>(GroupTarget))
		{
			if (const UThreatComponent* ThreatComp = TargetPawn->FindComponentByClass<UThreatComponent>())
			{
				Threat = ThreatComp->GetThreat();
			}
		}

		const FVector StatsLocation = GroupTarget->GetActorLocation() + FVector(0, 0, 200.0f);
		DrawDebugString(GetWorld(), StatsLocation,
			FString::Printf(TEXT("GROUP %d: %d enemies\nThreat %.2f (looks %.2fx closer)\nTokens R:%d/%d M:%d/%d S:%d/%d\nAttackers total: %d / %d"),
				GroupIdx, Group.Members.Num(),
				Threat, 1.0f + Threat,
				Group.Ranged.MaxTokens - Group.Ranged.GetAvailableCount(), Group.Ranged.MaxTokens,
				Group.Melee.MaxTokens - Group.Melee.GetAvailableCount(), Group.Melee.MaxTokens,
				Group.Special.MaxTokens - Group.Special.GetAvailableCount(), Group.Special.MaxTokens,
				CountCurrentAttackers(), GetEffectiveMaxAttackers()),
			nullptr, GetGroupDebugColor(GroupIdx), DebugDuration, true, 1.2f);
	}
}

void AAICombatCoordinator::DrawBattleCircleDebug()
{
	if (!GetWorld()) return;

	// Long enough to survive until the next redraw. This actor ticks at 10Hz and these were drawn for
	// a single frame, so at 60fps every shape was visible for a sixth of the time and the whole
	// overlay read as a flicker rather than as a diagram.
	const float DebugDuration = PrimaryActorTick.TickInterval * 1.5f;

	// One set of rings per group. Drawing a single set around PrimaryTarget was fine while there was
	// only ever one formation; now there is one per player being fought, and seeing them separately
	// is the whole point of looking.
	for (int32 GroupIdx = 0; GroupIdx < Groups.Num(); ++GroupIdx)
	{
	const FTargetGroup& Group = Groups[GroupIdx];
	AActor* GroupTarget = Group.Target.Get();
	if (!GroupTarget) continue;

	const FVector PlayerPos = GroupTarget->GetActorLocation();

	// One colour per GROUP rather than per ring. Which ring a slot belongs to was the useful
	// distinction while there was only one formation; with several on screen the useful question is
	// which formation this is, and telling two same-coloured rings apart is impossible.
	const FColor GroupColor = GetGroupDebugColor(GroupIdx);

	auto DrawRingCircle = [this, &PlayerPos, DebugDuration, GroupColor](float Radius, float Thickness)
	{
		DrawDebugCircle(GetWorld(), PlayerPos, Radius, 48, GroupColor, false, DebugDuration, 0, Thickness,
			FVector(1, 0, 0), FVector(0, 1, 0), false);
	};

	// Inner ring drawn thickest so the three are still distinguishable within one colour.
	DrawRingCircle(InnerRingMinRadius, 6.0f);
	DrawRingCircle(InnerRingMaxRadius, 6.0f);
	DrawRingCircle(MiddleRingMinRadius, 3.0f);
	DrawRingCircle(MiddleRingMaxRadius, 3.0f);
	DrawRingCircle(OuterRingMinRadius, 1.5f);
	DrawRingCircle(OuterRingMaxRadius, 1.5f);

	// Name the formation at its centre, so a screenshot can be read without counting circles.
	DrawDebugString(GetWorld(), PlayerPos + FVector(0, 0, 120.0f),
		FString::Printf(TEXT("GROUP %d: %s"), GroupIdx, *GroupTarget->GetName()),
		nullptr, GroupColor, DebugDuration, true, 1.3f);

	// Draw each slot
	for (const FBattleSlot& Slot : Group.BattleSlots)
	{
		FColor SlotColor = GroupColor;

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
}

void AAICombatCoordinator::DrawRoleDebug()
{
	if (!GetWorld()) return;

	// Long enough to survive until the next redraw. This actor ticks at 10Hz and these were drawn for
	// a single frame, so at 60fps every shape was visible for a sixth of the time and the whole
	// overlay read as a flicker rather than as a diagram.
	const float DebugDuration = PrimaryActorTick.TickInterval * 1.5f;

	// Player state overlay, one per group
	for (const FTargetGroup& Group : Groups)
	{
		if (!Group.Target.IsValid() || !Group.State.bIsValid) continue;

		const FPlayerStateCache& CachedPlayerState = Group.State;
		const FVector PlayerLoc = Group.Target->GetActorLocation();

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

// ==================== Kamikaze Token Pool ====================

int32 AAICombatCoordinator::CountAliveKamikazeDrones() const
{
	int32 Count = 0;
	for (const FRegisteredNPCData& Data : RegisteredNPCs)
	{
		if (!Data.NPC.IsValid()) continue;
		AKamikazeDroneNPC* Kamikaze = Cast<AKamikazeDroneNPC>(Data.NPC.Get());
		if (Kamikaze && !Kamikaze->IsDead())
		{
			Count++;
		}
	}
	return Count;
}

void AAICombatCoordinator::UpdateKamikazeTokenPoolSize()
{
	const int32 AliveCount = CountAliveKamikazeDrones();
	if (AliveCount <= 0)
	{
		KamikazeTokenPool.MaxTokens = 0;
		return;
	}

	KamikazeTokenPool.MaxTokens = FMath::Clamp(AliveCount / DronesPerKamikazeToken, 1, MaxKamikazeTokensCap);
}

// ==================== Strafe Coordination ====================

void AAICombatCoordinator::RequestStrafeSlot(APawn* Drone, float OrbitDistance, FVector& OutCenter, FVector& OutAxis)
{
	if (!Drone) return;

	// The drone orbits the player IT is hunting. Orbiting PrimaryTarget put every drone in a ring
	// around the busiest player regardless of who it had actually picked.
	AActor* Target = ResolveTargetFor(Drone);
	if (!Target)
	{
		// Fallback: use drone's current position
		OutCenter = Drone->GetActorLocation();
		const FVector ToPlayer = -Drone->GetActorForwardVector();
		OutAxis = FVector::CrossProduct(FVector::UpVector, ToPlayer).GetSafeNormal();
		return;
	}

	const FVector PlayerPos = Target->GetActorLocation();

	// Clean up invalid slots
	StrafeSlots.RemoveAll([](const FStrafeSlot& Slot) { return !Slot.AssignedDrone.IsValid(); });

	// Check if drone already has a slot — update it
	FStrafeSlot* ExistingSlot = StrafeSlots.FindByPredicate([Drone](const FStrafeSlot& Slot)
	{
		return Slot.AssignedDrone.Get() == Drone;
	});

	// Sample directions around player
	TArray<float> ClearAngles;
	TArray<float> AllAngles;
	const float AngleStep = 360.0f / StrafeSampleDirections;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Drone);
	QueryParams.AddIgnoredActor(Target);

	for (int32 i = 0; i < StrafeSampleDirections; ++i)
	{
		const float AngleDeg = AngleStep * i;
		const float AngleRad = FMath::DegreesToRadians(AngleDeg);
		const FVector Dir(FMath::Cos(AngleRad), FMath::Sin(AngleRad), 0.0f);
		const FVector SamplePos = PlayerPos + Dir * OrbitDistance;

		AllAngles.Add(AngleDeg);

		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, PlayerPos, SamplePos, ECC_WorldStatic, QueryParams))
		{
			ClearAngles.Add(AngleDeg);
		}
	}

	// If nothing is clear, use least-blocked: pick direction closest to drone's current angle
	if (ClearAngles.Num() == 0)
	{
		const FVector DroneDir = (Drone->GetActorLocation() - PlayerPos);
		const float DroneAngle = FMath::RadiansToDegrees(FMath::Atan2(DroneDir.Y, DroneDir.X));
		float BestAngle = 0.0f;
		float BestDist = 999.0f;
		for (float Ang : AllAngles)
		{
			float Diff = FMath::Abs(FMath::FindDeltaAngleDegrees(Ang, DroneAngle));
			if (Diff < BestDist)
			{
				BestDist = Diff;
				BestAngle = Ang;
			}
		}
		ClearAngles.Add(BestAngle);
	}

	// Find the clear angle with maximum angular separation from other assigned drones
	float BestAngle = ClearAngles[0];
	float BestMinSeparation = -1.0f;

	for (float CandidateAngle : ClearAngles)
	{
		float MinSeparation = 360.0f;

		for (const FStrafeSlot& Slot : StrafeSlots)
		{
			if (Slot.AssignedDrone.Get() == Drone) continue; // skip self
			const float Sep = FMath::Abs(FMath::FindDeltaAngleDegrees(CandidateAngle, Slot.AngleDeg));
			MinSeparation = FMath::Min(MinSeparation, Sep);
		}

		if (MinSeparation > BestMinSeparation)
		{
			BestMinSeparation = MinSeparation;
			BestAngle = CandidateAngle;
		}
	}

	// Compute center and axis
	const float BestRad = FMath::DegreesToRadians(BestAngle);
	const FVector Dir(FMath::Cos(BestRad), FMath::Sin(BestRad), 0.0f);
	OutCenter = PlayerPos + Dir * OrbitDistance;
	OutAxis = FVector::CrossProduct(FVector::UpVector, Dir).GetSafeNormal();

	// Height offset: count drones within 30° of this angle
	float HeightOffset = 0.0f;
	int32 NearbyCount = 0;
	for (const FStrafeSlot& Slot : StrafeSlots)
	{
		if (Slot.AssignedDrone.Get() == Drone) continue;
		if (FMath::Abs(FMath::FindDeltaAngleDegrees(BestAngle, Slot.AngleDeg)) < 30.0f)
		{
			++NearbyCount;
		}
	}
	HeightOffset = NearbyCount * StrafeHeightStep;

	// Apply height offset to center
	OutCenter.Z += HeightOffset;

	// Store or update slot
	if (ExistingSlot)
	{
		ExistingSlot->Center = OutCenter;
		ExistingSlot->Axis = OutAxis;
		ExistingSlot->AngleDeg = BestAngle;
		ExistingSlot->HeightOffset = HeightOffset;
	}
	else
	{
		FStrafeSlot NewSlot;
		NewSlot.AssignedDrone = Drone;
		NewSlot.Center = OutCenter;
		NewSlot.Axis = OutAxis;
		NewSlot.AngleDeg = BestAngle;
		NewSlot.HeightOffset = HeightOffset;
		StrafeSlots.Add(NewSlot);
	}
}

void AAICombatCoordinator::ReleaseStrafeSlot(APawn* Drone)
{
	if (!Drone) return;
	StrafeSlots.RemoveAll([Drone](const FStrafeSlot& Slot)
	{
		return Slot.AssignedDrone.Get() == Drone;
	});
}

void AAICombatCoordinator::LogStateSnapshot()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Nothing registered means nothing to say. A client's coordinator is empty — the AI lives on the
	// authority — so this keeps the client log clean rather than filling it with empty snapshots.
	if (RegisteredNPCs.Num() == 0)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] SNAPSHOT auth=%d groups=%d registered=%d attackers=%d/%d"),
		HasAuthority() ? 1 : 0, Groups.Num(), RegisteredNPCs.Num(),
		CountCurrentAttackers(), GetEffectiveMaxAttackers());

	for (int32 GroupIdx = 0; GroupIdx < Groups.Num(); ++GroupIdx)
	{
		const FTargetGroup& Group = Groups[GroupIdx];
		const AActor* GroupTarget = Group.Target.Get();

		float Threat = 0.0f;
		if (const APawn* TargetPawn = Cast<APawn>(GroupTarget))
		{
			if (const UThreatComponent* ThreatComp = TargetPawn->FindComponentByClass<UThreatComponent>())
			{
				Threat = ThreatComp->GetThreat();
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG]   group %d target=%s enemies=%d slots=%d threat=%.2f hp=%.0f%% tokens R:%d/%d M:%d/%d S:%d/%d"),
			GroupIdx, *GetNameSafe(GroupTarget), Group.Members.Num(), Group.BattleSlots.Num(), Threat,
			Group.State.bIsValid ? Group.State.HPPercent * 100.0f : -1.0f,
			Group.Ranged.MaxTokens - Group.Ranged.GetAvailableCount(), Group.Ranged.MaxTokens,
			Group.Melee.MaxTokens - Group.Melee.GetAvailableCount(), Group.Melee.MaxTokens,
			Group.Special.MaxTokens - Group.Special.GetAvailableCount(), Group.Special.MaxTokens);

		for (int32 Index : Group.Members)
		{
			if (!RegisteredNPCs.IsValidIndex(Index)) continue;
			const FRegisteredNPCData& Data = RegisteredNPCs[Index];
			APawn* NPC = Data.NPC.Get();
			if (!NPC) continue;

			// Distance and line of sight are the two gates that decide whether this NPC may attack,
			// so they are printed next to the answer rather than left to be inferred.
			UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG]     %s role=%d perm=%d token=%d slot=%d dist=%.0f los=%d switchPressure=%.2f"),
				*NPC->GetName(), (int32)Data.Role, Data.bHasAttackPermission ? 1 : 0,
				Data.bHasToken ? 1 : 0, Data.AssignedSlotIndex,
				GetDistanceToTarget(NPC), HasLineOfSightToTarget(NPC) ? 1 : 0,
				Data.TargetSwitchPressure);
		}
	}
}

FColor AAICombatCoordinator::GetGroupDebugColor(int32 GroupIndex)
{
	// Groups have to be told apart at a glance, which is the entire reason the overlay exists once
	// there is more than one player being fought.
	static const FColor Palette[] = {
		FColor(80, 180, 255),   // blue
		FColor(255, 160, 60),   // orange
		FColor(140, 255, 120),  // green
		FColor(255, 110, 220),  // pink
	};
	return Palette[FMath::Abs(GroupIndex) % UE_ARRAY_COUNT(Palette)];
}

// ==================== Enemy Cluster Direction ====================

void AAICombatCoordinator::UpdateEnemyClusterDirection()
{
	// Still one direction for the whole fight rather than one per group. It biases where kamikaze
	// drones enter from, which is a coarse hint and not a position, so the busiest player is a
	// reasonable centre for it. Per-group would be the tidier answer if the hint ever gets sharper.
	if (!PrimaryTarget.IsValid())
	{
		CachedClusterDirection = FVector::ZeroVector;
		return;
	}

	const FVector PlayerPos = PrimaryTarget->GetActorLocation();
	FVector WeightedSum = FVector::ZeroVector;
	float TotalWeight = 0.0f;

	for (const FRegisteredNPCData& Data : RegisteredNPCs)
	{
		if (!Data.NPC.IsValid())
		{
			continue;
		}

		// Skip kamikaze drones — we want ground NPC clusters
		if (Cast<AKamikazeDroneNPC>(Data.NPC.Get()))
		{
			continue;
		}

		// Skip dead NPCs
		if (const AShooterNPC* NPC = Cast<AShooterNPC>(Data.NPC.Get()))
		{
			if (NPC->IsDead())
			{
				continue;
			}
		}

		const FVector NPCPos = Data.NPC->GetActorLocation();
		FVector ToNPC = NPCPos - PlayerPos;
		ToNPC.Z = 0.0f; // XY only
		const float Dist = ToNPC.Size();

		if (Dist < 50.0f)
		{
			continue; // Too close, skip
		}

		// Weight: closer NPCs count more (inverse distance, clamped)
		const float Weight = 1.0f / FMath::Max(Dist / 500.0f, 0.2f);
		WeightedSum += ToNPC.GetSafeNormal() * Weight;
		TotalWeight += Weight;
	}

	if (TotalWeight > 0.0f && !WeightedSum.IsNearlyZero(0.1f))
	{
		CachedClusterDirection = WeightedSum.GetSafeNormal();
	}
	else
	{
		CachedClusterDirection = FVector::ZeroVector;
	}
}
