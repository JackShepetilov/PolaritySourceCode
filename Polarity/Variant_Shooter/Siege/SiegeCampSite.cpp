// SiegeCampSite.cpp

#include "SiegeCampSite.h"

#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Coop/CoopPlayers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "MechPartDefinition.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "SiegeCampGuard.h"
#include "SiegeCampSubsystem.h"
#include "SiegeDirector.h"
#include "Variant_Shooter/AI/FlyingDrone.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"

namespace
{
	const TCHAR* CampStateName(ESiegeCampState InState)
	{
		switch (InState)
		{
		case ESiegeCampState::Dormant: return TEXT("Dormant");
		case ESiegeCampState::Guarded: return TEXT("Guarded");
		case ESiegeCampState::Cleared: return TEXT("Cleared");
		case ESiegeCampState::Looted:  return TEXT("Looted");
		default:                       return TEXT("?");
		}
	}
}

int32 FSiegeCampGuardKind::CountAt(float SiegeMinutes, int32 NumPlayers) const
{
	if (!NPCClass || SiegeMinutes < FirstMinute)
	{
		return 0;
	}
	int32 Count = BaseCount;
	if (AddOneEveryMinutes > KINDA_SMALL_NUMBER)
	{
		Count += FMath::FloorToInt32((SiegeMinutes - FirstMinute) / AddOneEveryMinutes);
	}
	Count += FMath::FloorToInt32(CountPerExtraPlayer * FMath::Max(0, NumPlayers - 1));
	if (MaxCount > 0)
	{
		Count = FMath::Min(Count, MaxCount);
	}
	return FMath::Max(0, Count);
}

ASiegeCampSite::ASiegeCampSite()
{
	PrimaryActorTick.bCanEverTick = true;
	// The camp thinks four times a second: a player distance check or two, nothing per frame.
	PrimaryActorTick.TickInterval = 0.25f;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	PartDisplay = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PartDisplay"));
	PartDisplay->SetupAttachment(Root);
	PartDisplay->SetRelativeLocation(FVector(0.0f, 0.0f, 120.0f));
	PartDisplay->SetCollisionProfileName(TEXT("NoCollision"));
	PartDisplay->SetCanEverAffectNavigation(false);
}

void ASiegeCampSite::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASiegeCampSite, State);
	DOREPLIFETIME(ASiegeCampSite, Part);
}

void ASiegeCampSite::BeginPlay()
{
	Super::BeginPlay();
	ApplyPartVisuals();
	ApplyStateVisuals();

	if (HasAuthority())
	{
		if (USiegeCampSubsystem* const Camps = GetWorld()->GetSubsystem<USiegeCampSubsystem>())
		{
			Camps->RegisterCamp(this);
		}
	}
	else
	{
		// Clients only draw: the part and the state arrive by replication.
		SetActorTickEnabled(false);
	}
}

uint8 ASiegeCampSite::GetPartRarity() const
{
	switch (Tier)
	{
	case ESiegeCampTier::Medium: return static_cast<uint8>(EUpgradeRarity::Rare);
	case ESiegeCampTier::Hard:   return static_cast<uint8>(EUpgradeRarity::Epic);
	default:                     return static_cast<uint8>(EUpgradeRarity::Common);
	}
}

void ASiegeCampSite::SetPart(UMechPartDefinition* InPart)
{
	if (!HasAuthority())
	{
		return;
	}
	Part = InPart;
	ApplyPartVisuals();
}

// ==================== Tick ====================

void ASiegeCampSite::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!HasAuthority())
	{
		return;
	}

	switch (State)
	{
	case ESiegeCampState::Dormant:
		if (NearestPlayerDistance2D() <= ActivationRadius)
		{
			SpawnGuards();
		}
		break;
	case ESiegeCampState::Guarded:
		TickGuarded(DeltaTime);
		break;
	case ESiegeCampState::Cleared:
		TickCleared();
		break;
	default:
		SetActorTickEnabled(false);
		break;
	}
}

float ASiegeCampSite::NearestPlayerDistance2D(APawn** OutPawn) const
{
	TArray<APawn*> Players;
	CoopPlayers::GetAll(GetWorld(), Players);
	float Best = TNumericLimits<float>::Max();
	for (APawn* const Player : Players)
	{
		if (!Player)
		{
			continue;
		}
		const float Dist = FVector::Dist2D(Player->GetActorLocation(), GetActorLocation());
		if (Dist < Best)
		{
			Best = Dist;
			if (OutPawn)
			{
				*OutPawn = Player;
			}
		}
	}
	return Best;
}

// ==================== Guards ====================

FVector ASiegeCampSite::GetPostLocal(int32 Index, int32 Total) const
{
	if (GuardPosts.IsValidIndex(Index))
	{
		return GuardPosts[Index];
	}
	// Past the authored posts: a ring around the landmark, evenly spread, started at a random angle so
	// two camps of the same piece do not look stamped.
	const int32 RingTotal = FMath::Max(1, Total - GuardPosts.Num());
	const int32 RingIndex = Index - GuardPosts.Num();
	const float Angle = RingPhase + 2.0f * PI * RingIndex / RingTotal;
	return FVector(FMath::Cos(Angle) * GuardRingRadius, FMath::Sin(Angle) * GuardRingRadius, 0.0f);
}

void ASiegeCampSite::SpawnGuards()
{
	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	// The siege clock, not the world clock: the camps grow with the waves (author 2026-09-30).
	const ASiegeDirector* const Director = ASiegeDirector::Get(World);
	const float Minutes = Director ? Director->GetSiegeMinutes() : 0.0f;
	const float HealthMultiplier = Director ? Director->GetCreepHealthMultiplier() : 1.0f;
	int32 NumPlayers = 1;
	if (Director)
	{
		NumPlayers = Director->GetHumanPlayerCount();
	}
	else
	{
		TArray<APawn*> Players;
		CoopPlayers::GetAll(World, Players);
		NumPlayers = FMath::Max(1, Players.Num());
	}

	TArray<TSubclassOf<AShooterNPC>> Roster;
	for (const FSiegeCampGuardKind& Kind : Guards)
	{
		const int32 Count = Kind.CountAt(Minutes, NumPlayers);
		for (int32 i = 0; i < Count; ++i)
		{
			Roster.Add(Kind.NPCClass);
		}
	}

	RingPhase = FMath::FRandRange(0.0f, 2.0f * PI);
	LiveGuards.Reset();
	GuardsLost = 0;
	NobodyNearSeconds = 0.0f;
	int32 Spawned = 0;
	for (int32 i = 0; i < Roster.Num(); ++i)
	{
		if (SpawnGuard(Roster[i], GetPostLocal(i, Roster.Num()), HealthMultiplier))
		{
			++Spawned;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[CAMP_DEBUG] %s (%s) wakes at %.1f min, %d players: %d of %d guards, hp x%.2f, part %s"),
		*GetName(), *SlotName.ToString(), Minutes, NumPlayers, Spawned, Roster.Num(), HealthMultiplier, *GetNameSafe(Part));

	// A camp with nobody to guard it is simply open.
	SetState(Spawned > 0 ? ESiegeCampState::Guarded : ESiegeCampState::Cleared);
}

bool ASiegeCampSite::SpawnGuard(TSubclassOf<AShooterNPC> NPCClass, const FVector& PostLocal, float HealthMultiplier)
{
	UWorld* const World = GetWorld();
	if (!World || !NPCClass)
	{
		return false;
	}
	const bool bFlyer = NPCClass->IsChildOf(AFlyingDrone::StaticClass());

	// The ground under the post, then the navmesh near it: the posts are drawn on a flat plan and the
	// camp may sit on a terrace, in a hollow or on a knoll.
	FVector Ground = GetActorTransform().TransformPosition(PostLocal);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SiegeCampGround), false, this);
	if (World->LineTraceSingleByChannel(Hit, Ground + FVector(0.0f, 0.0f, 3000.0f), Ground - FVector(0.0f, 0.0f, 3000.0f),
		ECC_WorldStatic, Params))
	{
		Ground = Hit.ImpactPoint;
	}
	if (!bFlyer)
	{
		if (UNavigationSystemV1* const NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
		{
			FNavLocation Nav;
			if (NavSys->ProjectPointToNavigation(Ground, Nav, FVector(500.0f, 500.0f, 800.0f)))
			{
				Ground = Nav.Location;
			}
		}
	}

	float HalfHeight = 100.0f;
	if (const AShooterNPC* const Cdo = NPCClass->GetDefaultObject<AShooterNPC>())
	{
		if (const UCapsuleComponent* const Capsule = Cdo->GetCapsuleComponent())
		{
			HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		}
	}
	const FVector SpawnAt = bFlyer ? Ground + FVector(0.0f, 0.0f, FlyerHeight) : Ground + FVector(0.0f, 0.0f, HalfHeight + 10.0f);
	FRotator Facing = (GetActorLocation() - SpawnAt).Rotation();
	Facing.Pitch = 0.0f;
	Facing.Roll = 0.0f;

	APawn* const SpawnedPawn = UAIBlueprintHelperLibrary::SpawnAIFromClass(World, NPCClass, nullptr, SpawnAt, Facing, true);
	AShooterNPC* const NPC = Cast<AShooterNPC>(SpawnedPawn);
	if (!NPC)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CAMP_DEBUG] %s: spawn of %s failed"), *GetName(), *GetNameSafe(NPCClass));
		return false;
	}

	// The waves' growth applies to the camps too. BeginPlay has set the Blueprint's health, this scales it.
	if (HealthMultiplier > 1.0f + KINDA_SMALL_NUMBER)
	{
		NPC->CurrentHP *= HealthMultiplier;
	}

	USiegeCampGuard* const Guard = NewObject<USiegeCampGuard>(NPC, TEXT("SiegeCampGuard"));
	Guard->LeashRadius = LeashRadius;
	Guard->MaxRoamRadius = MaxRoamRadius;
	Guard->Init(this, bFlyer ? SpawnAt : Ground, NPC->CurrentHP);
	Guard->RegisterComponent();
	NPC->AddInstanceComponent(Guard);

	NPC->OnNPCDeath.AddDynamic(this, &ASiegeCampSite::OnGuardDied);
	LiveGuards.Add(NPC);
	return true;
}

void ASiegeCampSite::TickGuarded(float DeltaTime)
{
	LiveGuards.RemoveAll([](const TWeakObjectPtr<AShooterNPC>& Weak)
	{
		return !Weak.IsValid() || Weak->IsDead();
	});
	if (LiveGuards.Num() == 0)
	{
		// Gone without a death event (fell out of the world, destroyed by a script): still cleared.
		UE_LOG(LogTemp, Log, TEXT("[CAMP_DEBUG] %s: no guards left"), *GetName());
		SetState(ESiegeCampState::Cleared);
		return;
	}

	// Untouched and nobody near for a while: the guards go away, and the next visit counts them on the
	// clock again. A camp that has lost a guard stays as it is, so a fight cannot be reset by walking off.
	if (GuardsLost == 0 && NearestPlayerDistance2D() > SleepRadius)
	{
		NobodyNearSeconds += DeltaTime;
		if (NobodyNearSeconds >= SleepSeconds)
		{
			UE_LOG(LogTemp, Log, TEXT("[CAMP_DEBUG] %s: nobody within %.0f m for %.0f s, the camp sleeps"),
				*GetName(), SleepRadius / 100.0f, SleepSeconds);
			DismissGuards();
			SetState(ESiegeCampState::Dormant);
		}
	}
	else
	{
		NobodyNearSeconds = 0.0f;
	}
}

void ASiegeCampSite::DismissGuards()
{
	for (const TWeakObjectPtr<AShooterNPC>& Weak : LiveGuards)
	{
		if (AShooterNPC* const NPC = Weak.Get())
		{
			NPC->OnNPCDeath.RemoveDynamic(this, &ASiegeCampSite::OnGuardDied);
			if (AController* const Controller = NPC->GetController())
			{
				Controller->Destroy();
			}
			NPC->Destroy();
		}
	}
	LiveGuards.Reset();
}

void ASiegeCampSite::OnGuardDied(AShooterNPC* DeadNPC)
{
	if (!HasAuthority() || State != ESiegeCampState::Guarded)
	{
		return;
	}
	LiveGuards.RemoveAll([DeadNPC](const TWeakObjectPtr<AShooterNPC>& Weak)
	{
		return !Weak.IsValid() || Weak.Get() == DeadNPC;
	});
	++GuardsLost;
	UE_LOG(LogTemp, Log, TEXT("[CAMP_DEBUG] %s: guard %s down, %d left"), *GetName(), *GetNameSafe(DeadNPC), LiveGuards.Num());
	if (LiveGuards.Num() == 0)
	{
		SetState(ESiegeCampState::Cleared);
	}
}

void ASiegeCampSite::AlertGuards(AActor* Attacker)
{
	for (const TWeakObjectPtr<AShooterNPC>& Weak : LiveGuards)
	{
		if (AShooterNPC* const NPC = Weak.Get())
		{
			if (USiegeCampGuard* const Guard = NPC->FindComponentByClass<USiegeCampGuard>())
			{
				Guard->Alert(Attacker);
			}
		}
	}
}

void ASiegeCampSite::Wake()
{
	if (HasAuthority() && State == ESiegeCampState::Dormant)
	{
		SpawnGuards();
	}
}

// ==================== Part ====================

void ASiegeCampSite::TickCleared()
{
	if (!Part)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CAMP_DEBUG] %s is cleared but has no part (PartPool empty?)"), *GetName());
		SetState(ESiegeCampState::Looted);
		return;
	}

	const FVector Crate = PartDisplay->GetComponentLocation();
	TArray<APawn*> Players;
	CoopPlayers::GetAll(GetWorld(), Players);
	for (APawn* const Player : Players)
	{
		AShooterCharacter* const Character = Cast<AShooterCharacter>(Player);
		UInventoryComponent* const Inventory = Character ? Character->GetInventoryComponent() : nullptr;
		if (!Inventory || FVector::Dist2D(Player->GetActorLocation(), Crate) > PickupRadius
			|| FMath::Abs(Player->GetActorLocation().Z - Crate.Z) > 400.0f)
		{
			continue;
		}

		// Straight into the bag of whoever walked up (author 2026-09-30).
		FInventoryItem Item;
		Item.Kind = EInventorySlotKind::MechPart;
		Item.Payload = Part;
		Item.Count = 1;
		Item.StackMax = 1;
		if (Inventory->TryAdd(Item) == 0)
		{
			UE_LOG(LogTemp, Log, TEXT("[CAMP_DEBUG] %s: %s took part %s (%s)"),
				*GetName(), *Player->GetName(), *Part->GetName(), *UMechPartDefinition::SlotName(Part->Slot));
			SetState(ESiegeCampState::Looted);
			return;
		}

		const float Now = GetWorld()->GetTimeSeconds();
		if (Now - LastFullBagLogTime > 3.0f)
		{
			LastFullBagLogTime = Now;
			UE_LOG(LogTemp, Log, TEXT("[CAMP_DEBUG] %s: %s has no free cell for the part"), *GetName(), *Player->GetName());
		}
	}
}

// ==================== State and looks ====================

void ASiegeCampSite::SetState(ESiegeCampState NewState)
{
	if (State == NewState)
	{
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[CAMP_DEBUG] %s: %s -> %s"), *GetName(), CampStateName(State), CampStateName(NewState));
	State = NewState;
	ApplyStateVisuals();
}

void ASiegeCampSite::OnRep_State()
{
	ApplyStateVisuals();
}

void ASiegeCampSite::OnRep_Part()
{
	ApplyPartVisuals();
}

void ASiegeCampSite::ApplyPartVisuals()
{
	if (!PartDisplay)
	{
		return;
	}
	PartDisplay->SetStaticMesh(Part ? Part->DisplayMesh.Get() : nullptr);
	if (Part)
	{
		PartDisplay->SetRelativeScale3D(Part->DisplayScale);
	}
	PartDisplay->SetVisibility(Part && State != ESiegeCampState::Looted);
}

void ASiegeCampSite::ApplyStateVisuals()
{
	if (PartDisplay)
	{
		PartDisplay->SetVisibility(Part && State != ESiegeCampState::Looted);
	}
	SetLandmarksLit(State != ESiegeCampState::Looted);
	OnStateChanged.Broadcast(State);
	BP_OnCampStateChanged(State);
}

void ASiegeCampSite::SetLandmarksLit(bool bLit)
{
	// The landmark goes dark with the camp: smoke stops, lamps and beams go out. Tagged pieces (a lamp
	// globe, a light beam) are hidden outright; the structure itself stays standing.
	for (AActor* const Landmark : LandmarkActors)
	{
		if (!IsValid(Landmark))
		{
			continue;
		}
		if (Landmark->ActorHasTag(TEXT("CampLit")))
		{
			Landmark->SetActorHiddenInGame(!bLit);
		}
		TArray<UNiagaraComponent*> Niagara;
		Landmark->GetComponents(Niagara);
		for (UNiagaraComponent* const Fx : Niagara)
		{
			if (bLit) { Fx->Activate(); } else { Fx->Deactivate(); }
		}
		TArray<UParticleSystemComponent*> Cascade;
		Landmark->GetComponents(Cascade);
		for (UParticleSystemComponent* const Fx : Cascade)
		{
			if (bLit) { Fx->Activate(); } else { Fx->Deactivate(); }
		}
		TArray<ULightComponent*> Lights;
		Landmark->GetComponents(Lights);
		for (ULightComponent* const Light : Lights)
		{
			Light->SetVisibility(bLit);
		}
	}
}
