// BuildableActor.cpp

#include "BuildableActor.h"

#include "AI/PolarityTeams.h"
#include "BuildableDefinition.h"
#include "BuilderComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Coop/CoopPlayers.h"
#include "DispenserBuildable.h"
#include "DispenserSlotMachineComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "Variant_Shooter/DamageTypes/DamageType_Melee.h"
#include "Variant_Shooter/DamageTypes/DamageType_MomentumBonus.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/Pickups/LootDropComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/ShooterPlayerState.h"
#include "Variant_Shooter/Siege/SiegeDirector.h"
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
	SlotMachine = CreateDefaultSubobject<UDispenserSlotMachineComponent>(TEXT("Slot Machine"));

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
	DOREPLIFETIME(ABuildableActor, bSiegeCore);
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

	// The hitbox is also what makes the building SOLID to shots. The mesh cannot be trusted with that:
	// the turret's SM_T01_Placement has no simple collision at all, so every ordinary trace and every
	// projectile sweep went straight through it (proved 2026-09-22 with one Visibility trace along the
	// firing line: it stopped on the siege core BEHIND the turret, and only a per-polygon trace touched
	// the turret's base). Consequences, all from that one fact: no shot ever damaged a turret, AI aim
	// rays at it "missed" 100% of the time, and the feed trace could not find it.
	//
	// Blocks everything except pawns and the camera: characters still walk as they did (the hitbox is
	// QueryOnly, it never touched physics) and the camera does not bump on it. Pawn stays Ignore as a
	// RESPONSE; the object type is still ECC_Pawn, which is how the hitscan's pawn query finds it.
	// Set here rather than in the constructor so a Blueprint template cannot override it.
	if (Hitbox)
	{
		Hitbox->SetCollisionResponseToAllChannels(ECR_Block);
		Hitbox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Hitbox->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}

	// A building placed straight into a level (no builder, no owner) is simply standing there.
	if (HasAuthority() && !Definition)
	{
		State = EBuildableState::Active;
		ConstructionProgress = 1.0f;
		Health = MaxHealth;
	}

	// A dispenser a player just put down tells the siege: the first one on a siege map becomes the
	// base's core and starts the clock. The director decides; a map without one ignores it.
	if (HasAuthority() && OwnerPlayerState && IsDispenser())
	{
		if (ASiegeDirector* const Director = ASiegeDirector::Get(GetWorld()))
		{
			Director->NotifyBuildablePlaced(this);
		}
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

static TAutoConsoleVariable<int32> CVarBuildInstant(
	TEXT("polarity.build.instant"),
	0,
	TEXT("1: a building that is going up finishes on its next tick, so a test does not wait out the build time. Bench/debug only."),
	ECVF_Cheat);

void ABuildableActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (State == EBuildableState::Constructing)
	{
		if (HasAuthority() && Definition)
		{
			const float BuildTime = CVarBuildInstant.GetValueOnGameThread() != 0
				? 0.01f : FMath::Max(0.01f, Definition->GetLevelStats(BuildLevel).BuildTime);
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

	// Refused while the gun is still in the hands: the machine is busy with a spin.
	const FString Refusal = SlotMachine ? SlotMachine->GetSpinRefusal(Donor) : FString(TEXT("Dispenser: no slot machine"));
	if (!Refusal.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("[CASINO_DEBUG] %s refused at %s: %s"), *Donor->GetName(), *GetName(), *Refusal);
		if (UBuilderComponent* const Builder = Donor->FindComponentByClass<UBuilderComponent>())
		{
			Builder->Client_ShowCasinoReceipt(Refusal);
		}
		return false;
	}

	// Read while the gun still exists: the release destroys it.
	const FString WeaponName = Weapon->GetWeaponDisplayName().ToString();

	int32 Loaded = 0;
	int32 Reserve = -1;
	if (!Donor->ReleaseWeaponToMount(Weapon, Loaded, Reserve))
	{
		return false;
	}
	const float Magazines = Weapon->GetDepositMagazines(Loaded, FMath::Max(0, Reserve));
	TotalDepositedMagazines += Magazines;

	// The building grows from what it is fed: maximum and current health alike, so a feed is also a
	// repair. From the stake, never from what the reels gave.
	const float HealthGain = Magazines * FMath::Max(0.0f, HealthPerMagazine);
	if (HealthGain > 0.0f)
	{
		DepositHealthBonus += HealthGain;
		MaxHealth += HealthGain;
		SetHealth(Health + HealthGain);
		OnBuildableChanged.Broadcast(this);
	}
	UE_LOG(LogTemp, Log, TEXT("[CASINO_DEBUG] %s fed %s to %s: %d loaded + %d reserve = %.1f magazines (total %.1f, health %.0f/%.0f%s)"),
		*Donor->GetName(), *WeaponName, *GetName(), Loaded, FMath::Max(0, Reserve), Magazines, TotalDepositedMagazines,
		Health, MaxHealth, bSiegeCore ? TEXT(", core") : TEXT(""));

	SlotMachine->StartSpin(Donor, Magazines, WeaponName);
	return true;
}

// ==================== Siege core ====================

void ABuildableActor::MakeSiegeCore()
{
	if (!HasAuthority() || bSiegeCore)
	{
		return;
	}
	bSiegeCore = true;
	OnBuildableChanged.Broadcast(this);
	UE_LOG(LogTemp, Log, TEXT("[SIEGE_DEBUG] %s is now the siege core (%.0f/%.0f hp, defend radius %.0f)"), *GetName(), Health, MaxHealth, DefendRadius);
}

bool ABuildableActor::IsDefended() const
{
	TArray<APawn*> Players;
	CoopPlayers::GetAll(GetWorld(), Players);

	const float RadiusSq = FMath::Square(DefendRadius);
	const FVector Here = GetActorLocation();
	for (const APawn* const Player : Players)
	{
		if (Player && FVector::DistSquared(Here, Player->GetActorLocation()) <= RadiusSq)
		{
			return true;
		}
	}
	return false;
}

ABuildableActor* ABuildableActor::FindUndefendedCore(const UWorld* World, const FVector& From)
{
	return FindCore(World, From, /*bRequireUndefended*/ true);
}

ABuildableActor* ABuildableActor::FindNearestCore(const UWorld* World, const FVector& From)
{
	return FindCore(World, From, /*bRequireUndefended*/ false);
}

ABuildableActor* ABuildableActor::FindCore(const UWorld* World, const FVector& From, bool bRequireUndefended)
{
	if (!World)
	{
		return nullptr;
	}

	// One core per level, two at most: a plain walk over the buildings is cheaper than any registry.
	ABuildableActor* Nearest = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();
	for (TActorIterator<ABuildableActor> It(World); It; ++It)
	{
		ABuildableActor* const Core = *It;
		if (!IsValid(Core) || !Core->IsSiegeCore() || Core->IsDestroyed() || (bRequireUndefended && Core->IsDefended()))
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(From, Core->GetActorLocation());
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			Nearest = Core;
		}
	}
	return Nearest;
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

static TAutoConsoleVariable<int32> CVarBuildableGod(
	TEXT("polarity.buildable.god"),
	0,
	TEXT("1: buildings (turret, siege core, dispenser...) take hits but lose no health. Hits are still logged with [BUILD_DEBUG]. Bench/debug only."),
	ECVF_Default);

float ABuildableActor::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!HasAuthority() || State == EBuildableState::Destroyed || Damage <= 0.0f)
	{
		return 0.0f;
	}

	AActor* const Source = PolarityTeams::ResolveDamageSource(DamageCauser, EventInstigator);
	const bool bSameSide = Source && PolarityTeams::GetTeam(Source) == TeamByte;

	// [BUILD_DEBUG] "the turret takes no damage at all" has two completely different causes and no
	// way to tell them apart from outside: either nothing ever reaches this function (the shots miss,
	// or the collision never registers), or it is reached and refused here. The absence of this line
	// is the first answer; the line itself, with both team numbers on it, is the second.
	UE_LOG(LogTemp, Log,
		TEXT("[BUILD_DEBUG] %s TakeDamage %.1f from %s (causer %s) | sourceTeam=%d myTeam=%d sameSide=%d type=%s"),
		*GetName(), Damage, *GetNameSafe(Source), *GetNameSafe(DamageCauser),
		Source ? PolarityTeams::GetTeam(Source) : 255, TeamByte, bSameSide,
		*GetNameSafe(DamageEvent.DamageTypeClass));

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

	// Buildings that cannot die, for benches: the hit above is still logged (the bench report counts
	// damage from that line), only the health stays. Without it a clean turret-vs-infantry run ends as
	// soon as the fix works: the enemies raze the turret and the core, and with players ignored they
	// are left standing with nothing to fight, weapons down.
	if (CVarBuildableGod.GetValueOnGameThread() != 0)
	{
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
		// Repair is free: its price is the time spent at the base swinging, not metal (the author's
		// call, 2026-09-24). Metal is progress only, so the whole budget goes to what follows.
		if (Health < MaxHealth - 0.5f && Hitter)
		{
			const float Healed = FMath::Min(Definition->WrenchRepairHealth, MaxHealth - Health);
			if (Healed > 0.0f)
			{
				SetHealth(Health + Healed);
				Result = EBuildableWrenchResult::Repaired;
				UE_LOG(LogTemp, Log, TEXT("[BUILD_DEBUG] %s repaired %.0f (free) by %s -> %.0f/%.0f"),
					*GetName(), Healed, *Hitter->GetPlayerName(), Health, MaxHealth);
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
					MaxHealth = Definition->GetLevelStats(BuildLevel).MaxHealth + DepositHealthBonus;
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
