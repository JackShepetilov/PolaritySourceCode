// BuildableActor.cpp

#include "BuildableActor.h"

#include "AI/PolarityTeams.h"
#include "BuildableDefinition.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Coop/CoopPlayers.h"
#include "DispenserBuildable.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameplayTagContainer.h"
#include "Net/UnrealNetwork.h"
#include "Variant_Shooter/DamageTypes/DamageType_Melee.h"
#include "Variant_Shooter/DamageTypes/DamageType_MomentumBonus.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/Pickups/LootDropComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/ShooterPlayerState.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

ABuildableActor::ABuildableActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	bReplicates = true;
	// It never moves once placed; the spawn carries the transform, and that is all a client needs.
	SetReplicatingMovement(false);
	// Health and progress change a few times a second at most. The engine's default of 100 Hz
	// would spend bandwidth on nothing.
	SetNetUpdateFrequency(10.0f);
	SetMinNetUpdateFrequency(2.0f);

	USceneComponent* const Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Mesh->SetCanEverAffectNavigation(true);
	Mesh->SetGenerateOverlapEvents(false);

	Hitbox = CreateDefaultSubobject<UBoxComponent>(TEXT("Hitbox"));
	Hitbox->SetupAttachment(Mesh);
	Hitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Hitbox->SetCollisionObjectType(ECC_Pawn);
	Hitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
	Hitbox->SetGenerateOverlapEvents(false);
	Hitbox->SetCanEverAffectNavigation(false);
	Hitbox->CanCharacterStepUpOn = ECB_No;
	Hitbox->InitBoxExtent(FVector(50.0f, 50.0f, 50.0f));

	LootDrop = CreateDefaultSubobject<ULootDropComponent>(TEXT("Loot Drop"));

	// The quick melee only swings at pawns, dummies and actors carrying this tag
	// (UMeleeAttackComponent::IsValidMeleeTarget); without it the wrench never connects. The tag is
	// the project's existing way to say "melee may touch this", not a new mechanism.
	Tags.Add(TEXT("MeleeDestructible"));
}

void ABuildableActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ABuildableActor, Definition);
	DOREPLIFETIME(ABuildableActor, OwnerPlayerState);
	DOREPLIFETIME(ABuildableActor, State);
	DOREPLIFETIME(ABuildableActor, Health);
	DOREPLIFETIME(ABuildableActor, MaxHealth);
	DOREPLIFETIME(ABuildableActor, BuildLevel);
	DOREPLIFETIME(ABuildableActor, ConstructionProgress);
	DOREPLIFETIME(ABuildableActor, UpgradeMetal);
	DOREPLIFETIME(ABuildableActor, DispenserFuel);
}

// ==================== Setup ====================

void ABuildableActor::InitializeBuildable(UBuildableDefinition* InDefinition, AShooterPlayerState* InOwner)
{
	Definition = InDefinition;
	OwnerPlayerState = InOwner;
	BuildLevel = 1;
	const FBuildableLevelStats Stats = Definition ? Definition->GetLevelStats(BuildLevel) : FBuildableLevelStats{};
	MaxHealth = Stats.MaxHealth;
	Health = MaxHealth * StartHealthFraction;
	ConstructionProgress = 0.0f;
	State = EBuildableState::Constructing;
}

void ABuildableActor::BeginPlay()
{
	Super::BeginPlay();

	if (Mesh)
	{
		MeshRestLocation = Mesh->GetRelativeLocation();
		if (const UStaticMesh* const Asset = Mesh->GetStaticMesh())
		{
			MeshHeight = FMath::Max(10.0f, Asset->GetBoundingBox().GetSize().Z * Mesh->GetRelativeScale3D().Z);
		}
	}
	FitHitboxToMesh();

	// A building placed straight into a level (no builder, no owner) is simply standing there.
	if (HasAuthority() && !Definition)
	{
		State = EBuildableState::Active;
		ConstructionProgress = 1.0f;
		Health = MaxHealth;
	}

	VisualProgress = ConstructionProgress;
	SetActorTickEnabled(State == EBuildableState::Constructing || (State == EBuildableState::Active && NeedsActiveTick()));
	RefreshVisuals();
}

void ABuildableActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// A building removed any way but its own death (level change, owner quit and swept it away)
	// still has to leave the owner's list, or the HUD keeps a dead row.
	if (HasAuthority() && OwnerPlayerState)
	{
		OwnerPlayerState->UnregisterBuildable(this);
	}
	Super::EndPlay(EndPlayReason);
}

void ABuildableActor::FitHitboxToMesh()
{
	if (!Hitbox || !Mesh || !Mesh->GetStaticMesh())
	{
		return;
	}
	// Same recipe as AKamikazeCarrierDrone::FitHitboxToMesh: the box lives in the mesh's space, so
	// the mesh bounds fit it directly and the padding is divided back by the mesh scale.
	const FBox LocalBox = Mesh->GetStaticMesh()->GetBoundingBox();
	const FVector Scale = Mesh->GetComponentScale().GetAbs();
	const FVector Pad(
		HitboxPadding / FMath::Max(Scale.X, KINDA_SMALL_NUMBER),
		HitboxPadding / FMath::Max(Scale.Y, KINDA_SMALL_NUMBER),
		HitboxPadding / FMath::Max(Scale.Z, KINDA_SMALL_NUMBER));
	Hitbox->SetRelativeLocation(LocalBox.GetCenter());
	Hitbox->SetBoxExtent(LocalBox.GetExtent() + Pad);
}

// ==================== Queries ====================

int32 ABuildableActor::GetMaxLevel() const
{
	return Definition ? Definition->GetMaxLevel() : 1;
}

int32 ABuildableActor::GetUpgradeCost() const
{
	if (!Definition || BuildLevel >= Definition->GetMaxLevel())
	{
		return 0;
	}
	return Definition->GetLevelStats(BuildLevel).UpgradeCost;
}

// ==================== Tick ====================

void ABuildableActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (State == EBuildableState::Constructing)
	{
		if (HasAuthority() && Definition)
		{
			const float BuildTime = FMath::Max(0.01f, Definition->GetLevelStats(BuildLevel).BuildTime);
			const float Boost = (GetWorld()->GetTimeSeconds() < BoostUntilTime) ? BoostMultiplier : 0.0f;
			const float Step = DeltaSeconds * (1.0f + Boost) / BuildTime;
			const float OldProgress = ConstructionProgress;
			ConstructionProgress = FMath::Min(1.0f, ConstructionProgress + Step);

			// Health grows in with the frame, the way a TF2 building fills up as it rises. Damage taken
			// meanwhile simply subtracts from the same number.
			const float StartHealth = MaxHealth * StartHealthFraction;
			SetHealth(Health + (MaxHealth - StartHealth) * (ConstructionProgress - OldProgress));

			if (ConstructionProgress >= 1.0f)
			{
				FinishConstruction();
			}
			else
			{
				Announce();
			}
		}

		// Drawn progress chases the true one, so a replicated update every tenth of a second reads
		// as one motion. The host's copy is already exact and the chase is a no-op for it.
		VisualProgress = FMath::FInterpConstantTo(VisualProgress, ConstructionProgress, DeltaSeconds, 1.5f);
		RefreshVisuals();
		return;
	}

	if (State == EBuildableState::Active && HasAuthority())
	{
		TickActive(DeltaSeconds);
	}
}

// ==================== Dispenser behaviour ====================

bool ABuildableActor::IsDispenser() const
{
	if (IsA<ADispenserBuildable>())
	{
		return true;
	}
	// The blueprint dispenser in the project (BP_Buildable_Dispenser) is a DIRECT child of this
	// class; its definition's Buildable.Dispenser tag is what makes it one.
	return Definition
		&& Definition->BuildableTag.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Buildable.Dispenser")));
}

void ABuildableActor::TickActive(float DeltaSeconds)
{
	// The base class runs the dispenser heartbeat; a turret subclass overrides this with its own.
	if (IsDispenser())
	{
		TickDispenserBehavior(DeltaSeconds);
	}
}

void ABuildableActor::TickDispenserBehavior(float DeltaSeconds)
{
	if (!HasAuthority() || DeltaSeconds <= 0.0f)
	{
		return;
	}

	TArray<APawn*> Players;
	CoopPlayers::GetAll(GetWorld(), Players);
	AmmoAccumulator += AmmoRoundsPerSecond * DeltaSeconds;
	for (APawn* Pawn : Players)
	{
		AShooterCharacter* Player = Cast<AShooterCharacter>(Pawn);
		if (!Player || FVector::DistSquared(Player->GetActorLocation(), GetActorLocation()) > FMath::Square(ServiceRadius))
		{
			continue;
		}

		Player->RestoreHealth(HealPerSecond * DeltaSeconds);
		AShooterWeapon* const Weapon = Player->GetCurrentWeapon();
		if (!Weapon || DispenserFuel <= 0 || AmmoAccumulator < 1.0f)
		{
			continue;
		}

		if (Weapon->UsesEnergyReserve())
		{
			const int32 Room = Weapon->GetEnergyReserveCapacity() - Weapon->GetEnergyReserve();
			const int32 Rounds = FMath::Min3(FMath::FloorToInt(AmmoAccumulator), Room, DispenserFuel);
			if (Rounds > 0)
			{
				Weapon->SetEnergyReserve(Weapon->GetEnergyReserve() + Rounds);
				DispenserFuel -= Rounds;
				AmmoAccumulator -= Rounds;
			}
			continue;
		}

		// A looted gun carries its spare rounds in the inventory cells. Fuel fills that reserve the
		// same way an ammo pile would, limited only by the room in the bag.
		if (Weapon->OwnsAmmoCells())
		{
			if (UInventoryComponent* const Inventory = Player->GetInventoryComponent())
			{
				const int32 Rounds = FMath::Min(FMath::FloorToInt(AmmoAccumulator), DispenserFuel);
				if (Rounds > 0)
				{
					FInventoryItem Item;
					Item.Kind = EInventorySlotKind::Ammo;
					Item.Count = Rounds;
					Item.StackMax = Inventory->GetRoundsPerAmmoCell();
					const int32 Added = Rounds - Inventory->TryAdd(Item);
					if (Added > 0)
					{
						DispenserFuel -= Added;
						AmmoAccumulator -= Added;
					}
				}
			}
		}
	}
}

void ABuildableActor::AddFuel(int32 Amount)
{
	if (!HasAuthority() || Amount <= 0)
	{
		return;
	}
	DispenserFuel = FMath::Clamp(DispenserFuel + Amount, 0, MaxFuel);
	OnBuildableChanged.Broadcast(this);
}

bool ABuildableActor::AcceptWeaponForFuel(AShooterCharacter* Donor, AShooterWeapon* Weapon)
{
	if (!HasAuthority() || !Donor || !Weapon || Weapon->IsMeleeWeapon() || !IsActive())
	{
		return false;
	}

	const float Reach = ServiceRadius + 150.0f;
	if (FVector::DistSquared(Donor->GetActorLocation(), GetActorLocation()) > FMath::Square(Reach))
	{
		return false;
	}

	int32 Loaded = 0;
	int32 Reserve = -1;
	if (!Donor->ReleaseWeaponToMount(Weapon, Loaded, Reserve))
	{
		return false;
	}

	// An endless class weapon has no removable reserve. Its chassis still has its tuned value; its
	// default full reserve is used solely for the authored initial-price ratio.
	if (Reserve < 0)
	{
		Reserve = Weapon->UsesEnergyReserve() ? Weapon->GetEnergyReserveCapacity() : 0;
	}
	AddFuel(Weapon->GetDispenserFuelValue(Loaded, Reserve));
	UE_LOG(LogTemp, Log, TEXT("[DISPENSER_DEBUG] %s sacrificed %s for %d fuel (hopper now %d)"),
		*Donor->GetName(), *GetNameSafe(Weapon), Weapon->GetDispenserFuelValue(Loaded, Reserve), DispenserFuel);
	return true;
}

void ABuildableActor::OnRep_DispenserFuel()
{
	OnBuildableChanged.Broadcast(this);
}

// ==================== Construction ====================

void ABuildableActor::FinishConstruction()
{
	ConstructionProgress = 1.0f;
	VisualProgress = 1.0f;
	SetHealth(FMath::Min(Health, MaxHealth));
	SetState(EBuildableState::Active);
	UE_LOG(LogTemp, Log, TEXT("[BUILD_DEBUG] %s finished construction (%.0f/%.0f hp)"), *GetName(), Health, MaxHealth);
}

// ==================== Damage ====================

float ABuildableActor::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!HasAuthority() || State == EBuildableState::Destroyed || Damage <= 0.0f)
	{
		return 0.0f;
	}

	AActor* const Source = PolarityTeams::ResolveDamageSource(DamageCauser, EventInstigator);
	const bool bSameSide = Source && PolarityTeams::GetTeam(Source) == TeamByte;
	if (bSameSide)
	{
		// No friendly fire on a building, the same rule the NPCs keep among themselves. A friendly
		// melee hit is not fire at all: it is the wrench.
		if (IsWrenchHit(DamageEvent, DamageCauser))
		{
			AShooterPlayerState* const Hitter = EventInstigator ? EventInstigator->GetPlayerState<AShooterPlayerState>() : nullptr;
			ReceiveWrenchHit(Hitter);
		}
		return 0.0f;
	}

	ApplyBuildableDamage(Damage);
	return Damage;
}

bool ABuildableActor::IsWrenchHit(const FDamageEvent& DamageEvent, AActor* DamageCauser) const
{
	// The kinetic bonus of a swing is a second TakeDamage right behind the first, typed as a child
	// of melee. One swing, one hit of the wrench.
	if (DamageEvent.DamageTypeClass && DamageEvent.DamageTypeClass->IsChildOf(UDamageType_MomentumBonus::StaticClass()))
	{
		return false;
	}
	// Two signatures of a melee hit, either is enough: the quick melee names its damage type, and
	// it is also the only damage whose causer is the player's own pawn (a gun or a projectile
	// reports itself as the causer, never the pawn).
	const bool bMeleeType = DamageEvent.DamageTypeClass && DamageEvent.DamageTypeClass->IsChildOf(UDamageType_Melee::StaticClass());
	const bool bPawnCauser = CoopPlayers::IsPlayer(DamageCauser);
	return bMeleeType || bPawnCauser;
}

void ABuildableActor::ApplyBuildableDamage(float Damage)
{
	if (!HasAuthority() || State == EBuildableState::Destroyed || Damage <= 0.0f)
	{
		return;
	}
	SetHealth(Health - Damage);
	UE_LOG(LogTemp, Verbose, TEXT("[BUILD_DEBUG] %s took %.0f -> %.0f/%.0f"), *GetName(), Damage, Health, MaxHealth);
	if (Health <= 0.0f)
	{
		Die(false);
	}
}

void ABuildableActor::Demolish()
{
	if (!HasAuthority() || State == EBuildableState::Destroyed)
	{
		return;
	}
	Die(true);
}

void ABuildableActor::Die(bool bDemolished)
{
	SetHealth(0.0f);
	SetState(EBuildableState::Destroyed);
	OnDestroyed_Native();
	Multicast_OnDestroyed(bDemolished);

	// Scraps only for a building the enemy earned. The list lives on the Blueprint.
	if (!bDemolished && LootDrop)
	{
		LootDrop->DropLootHere();
	}

	UE_LOG(LogTemp, Log, TEXT("[BUILD_DEBUG] %s %s (owner %s)"), *GetName(),
		bDemolished ? TEXT("demolished") : TEXT("destroyed"),
		OwnerPlayerState ? *OwnerPlayerState->GetPlayerName() : TEXT("none"));

	if (OwnerPlayerState)
	{
		OwnerPlayerState->UnregisterBuildable(this);
		OwnerPlayerState = nullptr;
	}

	SetActorTickEnabled(false);
	SetLifeSpan(FMath::Max(0.01f, DestroyDelay));
}

// ==================== The wrench ====================

EBuildableWrenchResult ABuildableActor::ReceiveWrenchHit(AShooterPlayerState* Hitter)
{
	if (!HasAuthority() || !Definition || State == EBuildableState::Destroyed)
	{
		return EBuildableWrenchResult::Nothing;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	EBuildableWrenchResult Result = EBuildableWrenchResult::Nothing;
	int32 RemainingBudget = Hitter ? Hitter->GetMetal() : 0;
	if (Hitter)
	{
		const int32 HitBudget = GetWrenchMetalBudget();
		if (HitBudget >= 0)
		{
			RemainingBudget = FMath::Min(RemainingBudget, HitBudget);
		}
	}

	if (State == EBuildableState::Constructing)
	{
		// Free: hitting a building under construction costs nothing in TF2 either.
		BoostUntilTime = Now + Definition->WrenchBoostDuration;
		BoostMultiplier = Definition->WrenchBuildBoost;
		Result = EBuildableWrenchResult::SpedUpConstruction;
	}
	else
	{
		// One hit has one wallet budget.  Repair claims first, then a subclass may buy ammunition
		// from the exact remainder, and only the remainder after that can advance an upgrade.
		if (Health < MaxHealth - 0.5f && Hitter)
		{
			const float Missing = MaxHealth - Health;
			const float Wanted = FMath::Min(Definition->WrenchRepairHealth, Missing);
			const int32 MetalNeeded = FMath::CeilToInt(Wanted / Definition->RepairHealthPerMetal);
			const int32 MetalSpent = FMath::Min(MetalNeeded, RemainingBudget);
			if (MetalSpent > 0 && Hitter->TrySpendMetal(MetalSpent))
			{
				const float Healed = FMath::Min(Wanted, MetalSpent * Definition->RepairHealthPerMetal);
				SetHealth(Health + Healed);
				RemainingBudget -= MetalSpent;
				Result = EBuildableWrenchResult::Repaired;
				UE_LOG(LogTemp, Log, TEXT("[BUILD_DEBUG] %s repaired %.0f for %d metal by %s -> %.0f/%.0f"),
					*GetName(), Healed, MetalSpent, *Hitter->GetPlayerName(), Health, MaxHealth);
			}
		}
		if (OnWrenchHitExtra(Hitter, RemainingBudget) && Result == EBuildableWrenchResult::Nothing)
		{
			Result = EBuildableWrenchResult::Repaired;
		}
		if (Hitter && BuildLevel < Definition->GetMaxLevel())
		{
			const int32 Cost = GetUpgradeCost();
			const int32 Slice = FMath::Min3(Definition->WrenchUpgradeMetal, Cost - UpgradeMetal, RemainingBudget);
			if (Slice > 0 && Hitter->TrySpendMetal(Slice))
			{
				UpgradeMetal += Slice;
				RemainingBudget -= Slice;
				Result = EBuildableWrenchResult::UpgradeProgress;
				if (UpgradeMetal >= Cost)
				{
					BuildLevel += 1;
					UpgradeMetal = 0;
					MaxHealth = Definition->GetLevelStats(BuildLevel).MaxHealth;
					SetHealth(MaxHealth);
					OnLevelChanged(BuildLevel);
					BP_OnLevelChanged(BuildLevel);
					Result = EBuildableWrenchResult::Upgraded;
					UE_LOG(LogTemp, Log, TEXT("[BUILD_DEBUG] %s upgraded to level %d by %s"), *GetName(), BuildLevel, *Hitter->GetPlayerName());
				}
			}
		}
	}

	if (Result != EBuildableWrenchResult::Nothing)
	{
		Announce();
		Multicast_OnWrenchHit(Result);
	}
	return Result;
}

// ==================== Replicated writes and their echoes ====================

void ABuildableActor::SetHealth(float NewHealth)
{
	Health = FMath::Clamp(NewHealth, 0.0f, MaxHealth);
}

void ABuildableActor::SetState(EBuildableState NewState)
{
	if (State == NewState)
	{
		return;
	}
	const EBuildableState Old = State;
	State = NewState;
	// The host never gets its own OnRep, so it walks the same path here.
	OnRep_State(Old);
}

void ABuildableActor::Announce()
{
	OnBuildableChanged.Broadcast(this);
}

void ABuildableActor::OnRep_OwnerPlayerState()
{
	Announce();
}

void ABuildableActor::OnRep_State(EBuildableState OldState)
{
	if (State == EBuildableState::Active && OldState == EBuildableState::Constructing)
	{
		VisualProgress = 1.0f;
		OnConstructionFinished();
		BP_OnConstructionFinished();
	}
	// The tick exists for the construction and, after it, only for a kind that asked.
	SetActorTickEnabled(State == EBuildableState::Constructing || (State == EBuildableState::Active && NeedsActiveTick()));
	OnStateChanged(OldState, State);
	RefreshVisuals();
	Announce();
}

void ABuildableActor::OnRep_Health()
{
	Announce();
}

void ABuildableActor::OnRep_BuildLevel()
{
	OnLevelChanged(BuildLevel);
	BP_OnLevelChanged(BuildLevel);
	Announce();
}

void ABuildableActor::OnRep_ConstructionProgress()
{
	Announce();
}

void ABuildableActor::OnRep_UpgradeMetal()
{
	Announce();
}

void ABuildableActor::Multicast_OnDestroyed_Implementation(bool bDemolished)
{
	BP_OnBuildableDestroyed(bDemolished);
}

void ABuildableActor::Multicast_OnWrenchHit_Implementation(EBuildableWrenchResult Result)
{
	BP_OnWrenchHit(Result);
}

// ==================== Visuals ====================

void ABuildableActor::RefreshVisuals()
{
	if (!Mesh)
	{
		return;
	}

	switch (State)
	{
	case EBuildableState::Constructing:
	{
		// Rises out of the ground over the build: sunk by its full height at the start, seated at
		// its rest offset at the end. A subclass with a proper build animation replaces this.
		const float Sink = MeshHeight * (1.0f - FMath::Clamp(VisualProgress, 0.0f, 1.0f));
		Mesh->SetRelativeLocation(MeshRestLocation - FVector(0.0f, 0.0f, Sink));
		Mesh->SetVisibility(true, true);
		break;
	}
	case EBuildableState::Destroyed:
		Mesh->SetVisibility(false, true);
		SetActorEnableCollision(false);
		break;
	default:
		Mesh->SetRelativeLocation(MeshRestLocation);
		Mesh->SetVisibility(true, true);
		break;
	}
}
