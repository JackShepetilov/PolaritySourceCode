// DroppedRangedWeapon.cpp

#include "DroppedRangedWeapon.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/Pickups/AmmoPickup.h"
#include "ChargeAnimationComponent.h"
#include "ShooterWeapon.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/UI/EMFChargeWidgetSubsystem.h"
#include "EMF_FieldComponent.h"
#include "EMFVelocityModifier.h"
#include "Upgrades/UpgradeManagerComponent.h"
#include "Upgrades/Upgrades/Upgrade_AirKick.h"
#include "Upgrades/Upgrades/AirMailSpear.h"
#include "Variant_Shooter/Weapons/WeaponDropSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Curves/CurveFloat.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/DamageEvents.h"
#include "Net/UnrealNetwork.h"

// Marks a pull started by the grapple rather than by a yank. Server-side only, like the pull itself;
// written by BeginGrappleFetchPull.
static const FName GrappleFetchPullTag(TEXT("GrappleFetchPull"));

ADroppedRangedWeapon::ADroppedRangedWeapon()
{
	PrimaryActorTick.bCanEverTick = true;

	// A drop is spawned by whatever died, and things die on the server. Until this replicated, a
	// client's world had no dropped weapons in it at all: not to see, not to scan for, not to pick
	// up — the pickup button doing nothing was the last symptom of that, not the cause.
	//
	// One drop, one simulation, same as AEMFPhysicsProp: the server simulates and everyone else is
	// shown the result. See BeginPlay and PostNetReceivePhysicState for the other halves.
	bReplicates = true;
	SetReplicateMovement(true);

	// Body — root, physics-simulated. A box rather than the gun's own mesh: the look is a skeletal
	// mesh taken from whatever weapon class this drop carries, and a skeletal mesh without a physics
	// asset has nothing to simulate. The box is sized to that mesh in RefreshVisualFromWeaponClass.
	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->InitBoxExtent(FVector(35.0f, 5.0f, 12.0f));
	Body->SetSimulatePhysics(true);
	Body->SetCollisionProfileName(FName("PhysicsActor"));
	Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Body->SetGenerateOverlapEvents(true);
	Body->BodyInstance.bUseCCD = true;

	// The look. No collision of its own: the box is the body.
	WeaponVisual = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponVisual"));
	WeaponVisual->SetupAttachment(Body);
	WeaponVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponVisual->SetGenerateOverlapEvents(false);
	WeaponVisual->SetCanEverAffectNavigation(false);
	WeaponVisual->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

	// Legacy, kept so the old per-gun drop blueprints still load. Never seen, never collides.
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(Body);
	WeaponMesh->SetSimulatePhysics(false);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponMesh->SetGenerateOverlapEvents(false);
	WeaponMesh->SetVisibility(false);
	WeaponMesh->SetHiddenInGame(true);

	// EMF field component for charge storage
	FieldComponent = CreateDefaultSubobject<UEMF_FieldComponent>(TEXT("FieldComponent"));
}

void ADroppedRangedWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ADroppedRangedWeapon, bIsBeingPulled);
	DOREPLIFETIME(ADroppedRangedWeapon, bPullComplete);
	DOREPLIFETIME(ADroppedRangedWeapon, ReplicatedCharge);
	// Set before FinishSpawning and never changed after, so the first packet is enough.
	DOREPLIFETIME_CONDITION(ADroppedRangedWeapon, WeaponClass, COND_InitialOnly);
}

UPrimitiveComponent* ADroppedRangedWeapon::GetBody() const
{
	return Body;
}

ADroppedRangedWeapon* ADroppedRangedWeapon::SpawnFor(UWorld* World, TSubclassOf<AShooterWeapon> InWeaponClass, const FTransform& Where)
{
	if (!World || !InWeaponClass || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}

	UClass* const DropClass = UWeaponDropSettings::GetDropActorClass();

	// Deferred, so WeaponClass is on the actor before BeginPlay and before the first replication.
	ADroppedRangedWeapon* const Drop = World->SpawnActorDeferred<ADroppedRangedWeapon>(DropClass, Where, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Drop)
	{
		UE_LOG(LogTemp, Error, TEXT("[WEAPON_DROP] could not spawn %s for %s"), *GetNameSafe(DropClass), *GetNameSafe(InWeaponClass));
		return nullptr;
	}
	Drop->WeaponClass = InWeaponClass;
	Drop->FinishSpawning(Where);

	UE_LOG(LogTemp, Log, TEXT("[WEAPON_DROP] %s spawned as %s"), *GetNameSafe(InWeaponClass), *Drop->GetName());
	return Drop;
}

namespace
{
	/** The player behind a killing blow: the causer itself, its owner chain, or an instigator on
	 *  the way. */
	AShooterCharacter* ResolveDropKiller(AActor* DamageCauser)
	{
		for (AActor* Candidate = DamageCauser; Candidate; Candidate = Candidate->GetOwner())
		{
			if (AShooterCharacter* Character = Cast<AShooterCharacter>(Candidate))
			{
				return Character;
			}
			if (AShooterCharacter* InstigatorCharacter = Cast<AShooterCharacter>(Candidate->GetInstigator()))
			{
				return InstigatorCharacter;
			}
		}
		return nullptr;
	}
}

void ADroppedRangedWeapon::ApplyEnemyDropHooks(ADroppedRangedWeapon* Drop, float Charge, AActor* KillingDamageCauser, AActor* DroppedBy)
{
	if (!Drop || !Drop->HasAuthority())
	{
		return;
	}

	if (!FMath::IsNearlyZero(Charge))
	{
		Drop->SetCharge(Charge);
	}

	// The killer's upgrades may want to know a gun came out of their kill.
	if (AShooterCharacter* Killer = ResolveDropKiller(KillingDamageCauser))
	{
		if (UUpgradeManagerComponent* Upgrades = Killer->GetUpgradeManager())
		{
			Upgrades->NotifyEnemyDroppedRangedWeapon(Drop, DroppedBy);
		}
	}
}

void ADroppedRangedWeapon::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// Seen in the editor too: a drop placed in a level shows its gun without pressing Play.
	RefreshVisualFromWeaponClass();
}

void ADroppedRangedWeapon::OnRep_WeaponClass()
{
	RefreshVisualFromWeaponClass();
}

void ADroppedRangedWeapon::RefreshVisualFromWeaponClass()
{
	if (!WeaponVisual || !Body || VisualBuiltFor == WeaponClass)
	{
		return;
	}
	VisualBuiltFor = WeaponClass;

	const AShooterWeapon* const CDO = WeaponClass ? WeaponClass->GetDefaultObject<AShooterWeapon>() : nullptr;
	const USkeletalMeshComponent* const SourceMesh = CDO ? CDO->GetThirdPersonMesh() : nullptr;
	USkeletalMesh* const Mesh = SourceMesh ? SourceMesh->GetSkeletalMeshAsset() : nullptr;
	if (!Mesh)
	{
		// An old drop blueprint without a class, or a class with no third person mesh. Nothing to
		// show, and saying so is more useful than an invisible pickup.
		WeaponVisual->SetSkeletalMeshAsset(nullptr);
		if (WeaponClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("[WEAPON_DROP] %s: %s has no third person mesh, the drop is invisible"),
				*GetName(), *GetNameSafe(WeaponClass));
		}
		return;
	}

	// Same scale as in the hands, so a gun on the floor is the size the player saw it.
	const FVector Scale = SourceMesh->GetRelativeScale3D();
	WeaponVisual->SetSkeletalMeshAsset(Mesh);
	WeaponVisual->SetRelativeRotation(FRotator::ZeroRotator);
	WeaponVisual->SetRelativeScale3D(Scale);

	// The box is the mesh's bounds, and the mesh is moved so those bounds sit centred in the box.
	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const FVector Extent = (Bounds.BoxExtent * Scale).GetAbs().ComponentMax(FVector(3.0f));
	WeaponVisual->SetRelativeLocation(-Bounds.Origin * Scale);
	Body->SetBoxExtent(Extent);

	// Mass and inertia come from the shape, and the old ones belong to the default box.
	if (Body->IsPhysicsStateCreated())
	{
		Body->RecreatePhysicsState();
	}
}

void ADroppedRangedWeapon::OnRep_DropCharge()
{
	// Through the normal setter so anything hanging off charge (widget, visuals) behaves as it does
	// on the server.
	SetCharge(ReplicatedCharge);
}

void ADroppedRangedWeapon::PostNetReceivePhysicState()
{
	if (Body && !Body->IsSimulatingPhysics())
	{
		const FRepMovement& RepMove = GetReplicatedMovement();
		SetActorLocationAndRotation(
			FRepMovement::RebaseOntoLocalOrigin(RepMove.Location, this), RepMove.Rotation);
		return;
	}

	Super::PostNetReceivePhysicState();
}

void ADroppedRangedWeapon::BeginPlay()
{
	Super::BeginPlay();

	// On every machine: the owning client's brackets search this list, the server's claim check too.
	GrappleFetch::Register(this);

	// The look, on every machine. A client normally gets it from OnRep_WeaponClass, but a class that
	// equals the blueprint default never arrives as a change, so it is built here as well.
	RefreshVisualFromWeaponClass();

	// Old per-gun drop blueprints may have tuned the legacy mesh; it must stay out of the way.
	if (WeaponMesh)
	{
		WeaponMesh->SetSimulatePhysics(false);
		WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WeaponMesh->SetHiddenInGame(true);
	}

	// Only the authority simulates the drop; everyone else is shown where it landed.
	if (!HasAuthority() && Body)
	{
		Body->SetSimulatePhysics(false);
	}

	UE_LOG(LogTemp, Warning, TEXT("[DroppedRangedWeapon] %s BeginPlay: Weapon=%s, Charge=%.2f, bCanBeCaptured=%d"),
		*GetName(), *GetNameSafe(WeaponClass), GetCharge(), bCanBeCaptured);

	// Bind hit callback for stun-on-impact. The callback gates on bCanStunOnImpact at runtime,
	// so we always bind (cheap) regardless of whether stun is currently enabled.
	if (Body)
	{
		Body->SetNotifyRigidBodyCollision(true);
		Body->OnComponentHit.AddDynamic(this, &ADroppedRangedWeapon::OnWeaponMeshHit);
	}

	// Opt-in yank-style limited ammo for death drops. Skip if the yank path already rolled
	// (SpawnedBulletCount >= 0 means RollSpawnedBulletCount ran before BeginPlay, e.g. via
	// SpawnActorDeferred — though current callers don't use that pattern).
	if (bForceLimitedAmmo && SpawnedBulletCount < 0)
	{
		RollSpawnedBulletCount();
		UE_LOG(LogTemp, Warning, TEXT("[YANK_AMMO] %s: bForceLimitedAmmo=true → auto-rolled SpawnedBulletCount=%d"),
			*GetName(), SpawnedBulletCount);
	}
}

void ADroppedRangedWeapon::OnWeaponMeshHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!OtherActor || !Body) return;

	// ==================== Air Mail: kicked flight resolves on first impact ====================
	// The kicked weapon deals the upgrade's KickDamage to the NPC it slams into (plus the
	// regular throw stun below, since bCanStunOnImpact is still set from the throw).
	if (ActorHasTag(UUpgrade_AirKick::TAG_AirMailKicked))
	{
		Tags.Remove(UUpgrade_AirKick::TAG_AirMailKicked);

		AShooterNPC* KickedNPC = Cast<AShooterNPC>(OtherActor);
		if (KickedNPC && !KickedNPC->IsDead())
		{
			if (UUpgrade_AirKick* AirMail = UUpgrade_AirKick::FindActiveAirMail(this))
			{
				AShooterCharacter* Player = AirMail->GetShooterCharacter();
				FPointDamageEvent KickDamageEvent;
				KickDamageEvent.DamageTypeClass = AirMail->GetKickDamageType();
				KickDamageEvent.HitInfo = Hit;
				KickedNPC->TakeDamage(AirMail->GetKickDamage(), KickDamageEvent,
					Player ? Player->GetController() : nullptr, Player);

				UE_LOG(LogTemp, Warning, TEXT("[AIR_MAIL] kicked weapon %s slammed %s for %.0f"),
					*GetName(), *KickedNPC->GetName(), AirMail->GetKickDamage());
			}
		}
		// Fall through: the throw stun below may still apply on the same impact.
	}
	// Returning flight that hits anything without being kicked just lands — clear the state.
	else if (ActorHasTag(UUpgrade_AirKick::TAG_AirMailIncoming))
	{
		Tags.Remove(UUpgrade_AirKick::TAG_AirMailIncoming);
	}

	if (!bCanStunOnImpact) return;

	const FVector ImpactVelocity = PreImpactVelocity;
	const float ImpactSpeed = ImpactVelocity.Size();

	// ==================== Throw stun (existing behavior) ====================
	// Cooldown + velocity gates prevent stun-spam from a settling/rolling weapon.
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastStunTime >= StunCooldown && ImpactSpeed >= StunImpactVelocityThreshold)
	{
		// Only NPCs (not props, not the player). HumanoidNPC's ApplyExplosionStun is currently
		// no-op (immune to forces) — that's intentional per spec; passes through as no stun.
		AShooterNPC* NPC = Cast<AShooterNPC>(OtherActor);
		if (NPC && !NPC->IsDead())
		{
			NPC->ApplyExplosionStun(StunDuration, StunMontage);
			LastStunTime = Now;

			UE_LOG(LogTemp, Warning, TEXT("[YANK_THROW] %s stunned %s for %.1fs (impact speed=%.0f)"),
				*GetName(), *NPC->GetName(), StunDuration, ImpactSpeed);
		}
	}

	// ==================== Air Mail: bounce back to the player ====================
	// One bounce per throw; never off the player themselves. Angle/speed gates live in the
	// upgrade (60–120° incidence band — glancing slides don't return).
	if (!bAirMailBounceConsumed
		&& !OtherActor->IsA<AShooterCharacter>()
		&& !ActorHasTag(UUpgrade_AirKick::TAG_AirMailKicked))
	{
		if (UUpgrade_AirKick* AirMail = UUpgrade_AirKick::FindActiveAirMail(this))
		{
			// Thrown INTO a character: the capsule normal vs the throw's arced trajectory makes
			// the incidence angle meaningless — enemy hits always qualify (speed gate remains).
			const bool bCharacterImpact = OtherActor->IsA<APawn>();

			FVector ReturnVelocity;
			if (AirMail->TryComputeBounce(GetActorLocation(), ImpactVelocity, Hit.ImpactNormal, ReturnVelocity, bCharacterImpact))
			{
				bAirMailBounceConsumed = true;
				Body->SetPhysicsLinearVelocity(ReturnVelocity);
				if (AirMail->GetReturnSpinSpeed() > 0.0f)
				{
					AirMailOrientSpear(Body, ReturnVelocity);
				}
				Tags.Add(UUpgrade_AirKick::TAG_AirMailIncoming);
				AirMail->PlayBounceFeedback(Hit.ImpactPoint);

				UE_LOG(LogTemp, Warning, TEXT("[AIR_MAIL] thrown weapon %s bounced off %s toward player (impact speed=%.0f)"),
					*GetName(), *OtherActor->GetName(), ImpactSpeed);
			}
		}
	}
}

void ADroppedRangedWeapon::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Cache pre-contact velocity for OnWeaponMeshHit — at hit-callback time the physics solver
	// has already altered the velocity, which breaks the Air Mail incidence-angle test.
	if (Body && Body->IsSimulatingPhysics())
	{
		PreImpactVelocity = Body->GetPhysicsLinearVelocity();
	}

	// Air Mail spear: while kicked, drive orientation kinematically (nose along velocity + roll)
	// so the asymmetric body can't precess/tumble. Self-stops once the weapon slows down.
	if (ActorHasTag(UUpgrade_AirKick::TAG_AirMailKicked))
	{
		UUpgrade_AirKick* AirMail = UUpgrade_AirKick::FindActiveAirMail(this);
		AirMailTickSpear(Body, AirMail ? AirMail->GetKickSpinSpeed() : 720.0f);
	}

	// Mirror the authority's charge out to clients — their capture scan gates on it, and the value
	// itself lives in the plugin's field component, which replicates nothing.
	if (HasAuthority() && !FMath::IsNearlyEqual(ReplicatedCharge, GetCharge()))
	{
		ReplicatedCharge = GetCharge();
	}

	if (bIsBeingPulled)
	{
		UpdatePull(DeltaTime);
	}
}

void ADroppedRangedWeapon::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GrappleFetch::Unregister(this);
	Super::EndPlay(EndPlayReason);
}

bool ADroppedRangedWeapon::CanBeGrappleFetchedBy(const AShooterCharacter* Caster) const
{
	// Already on its way to somebody, already granted, or never meant to be picked up.
	return Caster && bCanBeCaptured && WeaponClass && !bIsBeingPulled && !bPullComplete && !IsHidden();
}

bool ADroppedRangedWeapon::BeginGrappleFetchPull(AShooterCharacter* Caster)
{
	// The tag goes on before the pull starts: it decides where the pull flies (the line's origin, not
	// the camera offset) and how the weapon is handed over (straight into the hand, no swap
	// animation). Read by UpdatePull and CompletePull.
	if (!Caster || bIsBeingPulled || bPullComplete)
	{
		return false;
	}
	Tags.AddUnique(GrappleFetchPullTag);
	if (TryStartPullForClient(Caster))
	{
		return true;
	}
	// Refused after all: take the mark back off, so a later yank of the same drop behaves as a yank.
	Tags.Remove(GrappleFetchPullTag);
	return false;
}

bool ADroppedRangedWeapon::TryStartPullForClient(AShooterCharacter* Requester)
{
	if (!Requester || bIsBeingPulled || bPullComplete || !bCanBeCaptured)
	{
		return false;
	}

	StartPull(Requester);
	return bIsBeingPulled;
}

// ==================== Charge API ====================

float ADroppedRangedWeapon::GetCharge() const
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		return Desc.PointChargeParams.Charge;
	}
	return 0.0f;
}

void ADroppedRangedWeapon::SetCharge(float NewCharge)
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = NewCharge;
		FieldComponent->SetSourceDescription(Desc);
	}

	// No charge widget over a dropped gun any more. It was there to say "channel the opposite sign to
	// take this"; the grapple fetches drops now, and the brackets plus the weapon card say everything
	// the player needs. @see AShooterCharacter::UpdateGrappleFetchAiming
}

// ==================== Ammo Distribution ====================

void ADroppedRangedWeapon::GrantAmmoToInventory(AShooterCharacter* Player, AShooterWeapon* Weapon, bool bFillMagazine)
{
	if (!Player || !Weapon || !HasAuthority())
	{
		return;
	}

	UInventoryComponent* Inventory = Player->GetInventoryComponent();
	if (!Inventory || !Weapon->OwnsAmmoCells())
	{
		// The energy weapon owns no cells, and a character without a bag is not on this economy.
		return;
	}

	const int32 MagSize = FMath::Max(1, Weapon->GetMagazineSize());
	// Zero is a real answer, not "unset": a gun thrown away empty comes back empty. Only the -1
	// default means nobody rolled a number, and that still grants a full magazine.
	const int32 Offered = (SpawnedBulletCount >= 0) ? SpawnedBulletCount : MagSize;
	if (Offered <= 0)
	{
		// A freshly granted gun from an empty drop starts dry. An already owned one must not be
		// emptied by the pickup: picking up an empty copy takes nothing from the player's weapon.
		if (bFillMagazine)
		{
			Weapon->SetBulletCount(0);
		}
		return;
	}

	FInventoryItem Item;
	Item.Kind = EInventorySlotKind::Ammo;
	Item.Count = Offered;
	// One cell holds the inventory's cell size. Anything past that opens another cell, and how many
	// cells of ammo fit is the meta's business, not this function's.
	Item.StackMax = Inventory->GetRoundsPerAmmoCell();

	const int32 Left = Inventory->TryAdd(Item);
	const int32 Taken = Offered - Left;

	if (bFillMagazine)
	{
		// Only a gun this drop just granted reaches for its first magazine. Written this way rather
		// than "what this pickup gave" so a top-up on a half-empty gun fills it instead of replacing
		// its rounds with the new ones. A second copy leaves the magazine alone on purpose: its
		// rounds land in the reserve, the reload key moves them.
		Weapon->SetBulletCount(FMath::Min(MagSize, Inventory->GetAmmo()));
	}

	UE_LOG(LogTemp, Warning, TEXT("[AMMO_CELLS] %s offered %d rounds, %d taken, %d left over"),
		*Weapon->GetName(), Offered, Taken, Left);

	if (Left > 0)
	{
		// What did not fit stays in the world. Respawned at the player's feet rather than left on
		// the original drop, because the original is about to be destroyed by the pickup and the
		// player should be able to see what they could not carry.
		SpawnLeftoverDrop(Player, Left);
	}
}

void ADroppedRangedWeapon::CarryEnergyAmmoFrom(const AShooterWeapon* Weapon)
{
	if (!Weapon || !HasAuthority())
	{
		return;
	}

	// The reserve is the server's own number and exact. The magazine is not: rounds are counted by
	// whoever pulls the trigger, so for a client's gun this is the server's copy, which misses the
	// client's shots and usually reads full. Taking a thrown gun back can therefore top up its
	// magazine. TODO(COOP): exact only once the server mirrors a client's magazine.
	SpawnedBulletCount = Weapon->GetBulletCount();
	CarriedEnergyReserve = Weapon->GetEnergyReserve();

	UE_LOG(LogTemp, Log, TEXT("[ENERGY_AMMO] %s thrown away with %d loaded, %d in reserve"),
		*Weapon->GetName(), SpawnedBulletCount, CarriedEnergyReserve);
}

void ADroppedRangedWeapon::GrantEnergyAmmo(AShooterWeapon* Weapon)
{
	if (!Weapon || !HasAuthority() || !Weapon->UsesEnergyReserve())
	{
		return;
	}

	const int32 MagSize = FMath::Max(1, Weapon->GetMagazineSize());

	// An exact count wins over the designer's fill: zero is a real answer (a gun thrown away empty),
	// and only the -1 default means nobody set one.
	const int32 Loaded = (SpawnedBulletCount >= 0)
		? FMath::Clamp(SpawnedBulletCount, 0, MagSize)
		: FMath::Clamp(FMath::RoundToInt(EnergyMagazineFill * MagSize), 0, MagSize);

	const int32 Reserve = (CarriedEnergyReserve >= 0)
		? CarriedEnergyReserve
		: FMath::RoundToInt(EnergyReserveMagazines * MagSize);

	// The weapon was spawned a moment ago with a full reserve of its own; both of these overwrite
	// that. SetEnergyReserve clamps to the weapon's capacity and starts the refill.
	Weapon->SetBulletCount(Loaded);
	Weapon->SetEnergyReserve(Reserve);

	UE_LOG(LogTemp, Warning, TEXT("[ENERGY_AMMO] %s picked up from %s: %d loaded, %d in reserve (asked for %d)"),
		*Weapon->GetName(), *GetName(), Weapon->GetBulletCount(), Weapon->GetEnergyReserve(), Reserve);
}

void ADroppedRangedWeapon::SpawnLeftoverDrop(AShooterCharacter* Player, int32 Rounds)
{
	if (!Player || Rounds <= 0 || !HasAuthority())
	{
		return;
	}

	// Rounds are not a gun. This used to spawn another whole ADroppedRangedWeapon carrying the
	// leftover bullets, which meant a full bag PRINTED A SECOND COPY OF THE WEAPON on the floor:
	// walk up to one drop with no room, and the team could stack identical rifles out of nothing.
	// What is left over is ammo, so it comes back as ammo.
	const AShooterWeapon* WeaponCDO = WeaponClass ? WeaponClass->GetDefaultObject<AShooterWeapon>() : nullptr;
	const TSubclassOf<AAmmoPickup> PileClass = WeaponCDO ? WeaponCDO->AmmoPickupClass : nullptr;

	if (!PileClass)
	{
		// Deliberately NOT falling back to spawning a weapon. Losing the rounds is a smaller bug
		// than duplicating the gun, and the log says exactly which weapon needs the field set.
		UE_LOG(LogTemp, Warning,
			TEXT("[AMMO_CELLS] %d rounds would not fit, but %s has no AmmoPickupClass - nothing dropped"),
			Rounds, *GetNameSafe(WeaponClass));
		return;
	}

	FInventoryItem Pile;
	Pile.Kind = EInventorySlotKind::Ammo;
	Pile.Count = Rounds;
	Pile.StackMax = GetDefault<UInventoryComponent>()->GetRoundsPerAmmoCell();

	const FTransform Where(GetActorRotation(), Player->GetActorLocation());
	if (AInventoryPickup::SpawnForItem(this, PileClass, Where, Pile, GetCharge()))
	{
		UE_LOG(LogTemp, Warning, TEXT("[AMMO_CELLS] %d rounds would not fit, dropped as a pile at the player's feet"), Rounds);
	}
}

void ADroppedRangedWeapon::RollSpawnedBulletCount()
{
	if (!WeaponClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YANK_AMMO] %s: RollSpawnedBulletCount skipped — WeaponClass is null"), *GetName());
		return;
	}

	const AShooterWeapon* CDO = WeaponClass->GetDefaultObject<AShooterWeapon>();
	const int32 MagSize = CDO ? CDO->GetMagazineSize() : 0;
	if (MagSize <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YANK_AMMO] %s: RollSpawnedBulletCount skipped — invalid MagSize=%d"),
			*GetName(), MagSize);
		return;
	}

	int32 RolledCount;
	if (AmmoDistributionCurve)
	{
		// Inverse-transform sampling: roll random in [0..1], read fraction from curve
		const float Roll = FMath::FRand();
		const float Fraction = FMath::Clamp(AmmoDistributionCurve->GetFloatValue(Roll), 0.0f, 1.0f);
		RolledCount = FMath::RoundToInt(Fraction * MagSize);
		UE_LOG(LogTemp, Log, TEXT("[YANK_AMMO] %s: curve roll=%.3f, fraction=%.3f, count=%d"),
			*GetName(), Roll, Fraction, RolledCount);
	}
	else
	{
		// Fallback: uniform random [1, MagSize] inclusive
		RolledCount = FMath::RandRange(1, MagSize);
		UE_LOG(LogTemp, Log, TEXT("[YANK_AMMO] %s: no curve, random fallback count=%d (range 1..%d)"),
			*GetName(), RolledCount, MagSize);
	}

	// Clamp to [1, MagSize] — minimum 1 bullet so the pickup is always usable
	SpawnedBulletCount = FMath::Clamp(RolledCount, 1, MagSize);
}

bool ADroppedRangedWeapon::SampleDensityCurve(const UCurveFloat* Curve, float& OutValue)
{
	if (!Curve || Curve->FloatCurve.GetNumKeys() == 0)
	{
		return false;
	}

	float MinX = 0.0f;
	float MaxX = 0.0f;
	Curve->GetTimeRange(MinX, MaxX);
	if (MaxX - MinX <= KINDA_SMALL_NUMBER)
	{
		// One key, or all keys on one X: that value, always.
		OutValue = MinX;
		return true;
	}

	// Walk the curve in equal steps and add up the area (trapezoids). Picking a point uniformly under
	// that area and reading its X is what "sample from the density" means. 64 steps is plenty for a
	// hand-drawn curve and costs nothing once per death.
	constexpr int32 Steps = 64;
	const float Step = (MaxX - MinX) / Steps;
	float Cumulative[Steps + 1];
	Cumulative[0] = 0.0f;
	float PrevY = FMath::Max(0.0f, Curve->GetFloatValue(MinX));
	for (int32 Index = 1; Index <= Steps; ++Index)
	{
		const float Y = FMath::Max(0.0f, Curve->GetFloatValue(MinX + Step * Index));
		Cumulative[Index] = Cumulative[Index - 1] + 0.5f * (PrevY + Y) * Step;
		PrevY = Y;
	}

	const float Total = Cumulative[Steps];
	if (Total <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogTemp, Warning, TEXT("[WEAPON_ROLL] density curve %s has no area above zero, ignored"), *GetNameSafe(Curve));
		return false;
	}

	const float Target = FMath::FRand() * Total;
	for (int32 Index = 1; Index <= Steps; ++Index)
	{
		if (Cumulative[Index] >= Target)
		{
			const float SegmentArea = Cumulative[Index] - Cumulative[Index - 1];
			const float Alpha = SegmentArea > KINDA_SMALL_NUMBER ? (Target - Cumulative[Index - 1]) / SegmentArea : 0.5f;
			OutValue = MinX + Step * (Index - 1 + Alpha);
			return true;
		}
	}
	OutValue = MaxX;
	return true;
}

void ADroppedRangedWeapon::RollDropAmmo(const UCurveFloat* MagazineDensity, const UCurveFloat* ReserveDensity)
{
	if (!HasAuthority() || !WeaponClass)
	{
		return;
	}

	const AShooterWeapon* const CDO = WeaponClass->GetDefaultObject<AShooterWeapon>();
	const int32 MagSize = CDO ? FMath::Max(1, CDO->GetMagazineSize()) : 1;

	// Unset parts stay at what the drop would hand out anyway: a full magazine, and the drop's own
	// EnergyReserveMagazines of spare.
	int32 Loaded = MagSize;
	int32 Reserve = FMath::Max(0, FMath::RoundToInt(EnergyReserveMagazines * MagSize));

	float Fraction = 1.0f;
	const bool bRolledMagazine = SampleDensityCurve(MagazineDensity, Fraction);
	if (bRolledMagazine)
	{
		Loaded = FMath::Clamp(FMath::RoundToInt(FMath::Clamp(Fraction, 0.0f, 1.0f) * MagSize), 1, MagSize);
	}

	float SpareMagazines = 0.0f;
	const bool bRolledReserve = SampleDensityCurve(ReserveDensity, SpareMagazines);
	if (bRolledReserve)
	{
		Reserve = FMath::Max(0, FMath::RoundToInt(SpareMagazines * MagSize));
	}

	if (!bRolledMagazine && !bRolledReserve)
	{
		// Nothing rolled: leave the drop exactly as it was, so its defaults apply the usual way.
		return;
	}

	// Written the way the pickup reads it: an energy gun takes a loaded count and a reserve, a cells
	// gun one number for everything. By class, not by owner (Docs/Gotchas/Weapons.md).
	if (CDO && CDO->IsEnergyClass())
	{
		SpawnedBulletCount = Loaded;
		CarriedEnergyReserve = Reserve;
	}
	else
	{
		SpawnedBulletCount = Loaded + Reserve;
	}

	UE_LOG(LogTemp, Log, TEXT("[WEAPON_ROLL] %s (%s): %d loaded (%.2f of %d), %d reserve (%.2f magazines)"),
		*GetName(), *GetNameSafe(WeaponClass), Loaded, Fraction, MagSize, Reserve, SpareMagazines);
}

// ==================== Capture Range ====================

float ADroppedRangedWeapon::CalculateCaptureRange() const
{
	return UChargeAnimationComponent::GetCaptureRangeFor(this, FMath::Abs(GetCharge()));
}

// ==================== Pull ====================

void ADroppedRangedWeapon::StartPull(AShooterCharacter* InPullingPlayer)
{
	if (!InPullingPlayer || bIsBeingPulled || bPullComplete)
	{
		return;
	}

	bIsBeingPulled = true;
	PullElapsed = 0.0f;
	PullingCharacter = InPullingPlayer;
	PullStartLocation = GetActorLocation();
	PullStartRotation = GetActorRotation();

	// Snapshot the player's current weapon class for the Bandolier check at CompletePull —
	// the player may switch weapons mid-pull, but capacity is gated by what they had in hand
	// at the moment they committed to pulling this specific drop.
	if (const AShooterWeapon* CurrentHeld = InPullingPlayer->GetCurrentWeapon())
	{
		PullingClientCurrentWeaponClass = CurrentHeld->GetClass();
	}

	// Disable physics — we drive position directly
	Body->SetSimulatePhysics(false);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Stop exerting EMF force while under scripted pull. The drop spawns at its charged origin —
	// a yank spawns it AT the boss carrying the boss's charge — and is flown to the player by
	// SetActorLocation. While its field stays registered, that live charge attracts the player
	// toward the drop's origin (the boss), because the player filters NPC-typed sources
	// (NPCForceMultiplier=0) but NOT this drop's source. The charge value remains in SourceParams
	// for GetCharge()/widget/capture-range queries (those read it locally, not via the registry),
	// and the capture scan already skips weapons that are being pulled.
	if (FieldComponent)
	{
		FieldComponent->UnregisterFromRegistry();
	}
}

void ADroppedRangedWeapon::UpdatePull(float DeltaTime)
{
	if (!PullingCharacter.IsValid())
	{
		// Player gone — drop the weapon back
		bIsBeingPulled = false;
		Body->SetSimulatePhysics(true);
		Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		// Restore EMF source registration (unregistered in StartPull) now that it's a world drop again.
		if (FieldComponent)
		{
			FieldComponent->RegisterWithRegistry();
		}
		return;
	}

	PullElapsed += DeltaTime;
	const float Alpha = FMath::Clamp(PullElapsed / PullDuration, 0.0f, 1.0f);
	const float CurvedAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 2.0f);

	// Calculate camera-relative target in world space, relative to the camera of whoever is actually
	// pulling. Player zero is the host on every machine, and the pull runs on the server, so a
	// client's pickup used to fly at the host's face and only land in the right hands at the end.
	FVector CameraLoc;
	FRotator CameraRot;
	if (const APlayerController* PullerPC = Cast<APlayerController>(PullingCharacter->GetController());
		PullerPC && PullerPC->PlayerCameraManager)
	{
		CameraLoc = PullerPC->PlayerCameraManager->GetCameraLocation();
		CameraRot = PullerPC->PlayerCameraManager->GetCameraRotation();
	}
	else
	{
		// No controller to ask (an NPC puller, or a controller not yet resolved): the pawn's own
		// eyes are the honest fallback, and they are at least the right character's.
		PullingCharacter->GetActorEyesViewPoint(CameraLoc, CameraRot);
	}

	// Transform offset into world space relative to camera. A grapple fetch instead reels the weapon
	// in to where the line leaves the character, so the gun arrives at the end of the rope rather
	// than at a point beside it. Re-read every tick: the player keeps moving while it flies.
	const FVector WorldTarget = ActorHasTag(GrappleFetchPullTag)
		? PullingCharacter->GetGrappleHandLocation()
		: CameraLoc + CameraRot.RotateVector(PullTargetOffset);
	const FRotator WorldTargetRot = CameraRot + PullTargetRotation;

	// Interpolate
	const FVector NewPos = FMath::Lerp(PullStartLocation, WorldTarget, CurvedAlpha);
	const FRotator NewRot = FMath::Lerp(PullStartRotation, WorldTargetRot, CurvedAlpha);
	SetActorLocation(NewPos);
	SetActorRotation(NewRot);

	if (Alpha >= 1.0f)
	{
		CompletePull();
	}
}

void ADroppedRangedWeapon::CompletePull()
{
	bPullComplete = true;
	bIsBeingPulled = false;

	// Unregister charge widget
	if (UEMFChargeWidgetSubsystem* WidgetSub = GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>())
	{
		WidgetSub->UnregisterDroppedRangedWeapon(this);
	}

	// Hide this actor
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);

	const bool bGrappleFetch = ActorHasTag(GrappleFetchPullTag);

	if (!PullingCharacter.IsValid() || !WeaponClass)
	{
		// A fetch put the puller's weapon away; with nothing to give, give that one back.
		if (bGrappleFetch && PullingCharacter.IsValid())
		{
			PullingCharacter->FinishWeaponFetch(false);
		}
		Destroy();
		return;
	}

	AShooterCharacter* Player = PullingCharacter.Get();

	// Play pickup sound
	if (PickupSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, PickupSound, GetActorLocation());
	}

	// Check if player already has a weapon of this class — if so, skip (no stacking for ranged)
	AShooterWeapon* ExistingWeapon = Player->FindWeaponOfType(WeaponClass);

	UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] CompletePull (DROPPED): WeaponClass=%s, ExistingWeapon=%s, SpawnedBulletCount=%d"),
		*GetNameSafe(WeaponClass), *GetNameSafe(ExistingWeapon), SpawnedBulletCount);

	if (!ExistingWeapon)
	{
		// Grant a new weapon (permanent). A yank plays the animated lower→swap→raise transition
		// (AddWeaponClassAnimated falls back to instant equip if the player is unarmed). A grapple
		// fetch does not: its throw already put the old weapon away, so the new one goes straight
		// into the hand here and FinishWeaponFetch below plays its draw. Running the swap on top
		// would bring the old weapon out only to holster it again.
		if (bGrappleFetch)
		{
			Player->AddWeaponClass(WeaponClass);
		}
		else
		{
			Player->AddWeaponClassAnimated(WeaponClass);
		}

		// Tag the freshly-added weapon as yank-acquired so the strict "one yanked weapon at a time"
		// rule (ThrowYankedWeaponIfAny) can identify and discard it on subsequent yanks.
		if (AShooterWeapon* AddedWeapon = Player->FindWeaponOfType(WeaponClass))
		{
			AddedWeapon->bWasYanked = true;
			// Enemy drops have a finite initial reserve. The player may spend it, but it never
			// silently turns into the regenerating base energy weapon.
			AddedWeapon->ConfigureFiniteEnergyReserve();
			AddedWeapon->SourceYankDropClass = GetClass();
			// Kept so throwing the gun away puts back a drop that can be picked up again.
			AddedWeapon->SourceDropCharge = GetCharge();

			if (AddedWeapon->UsesEnergyReserve())
			{
				// An energy gun keeps its rounds on itself, so none of the cell handling below
				// applies: a magazine and a reserve, both set from this drop's Ammo|Energy numbers.
				GrantEnergyAmmo(AddedWeapon);
			}
			else
			{
				// Limited-ammo behavior: only set when this drop was yank-spawned (HumanoidNPC called
				// RollSpawnedBulletCount → SpawnedBulletCount > 0). Death drops leave SpawnedBulletCount
				// at the -1 default, so the granted weapon stays at full mag with infinite refills.
				if (SpawnedBulletCount > 0)
				{
					// Flag first: SetBulletCount is what tells the owning client about both, so setting
					// the flag after it would send the client "forty rounds, and they refill".
					AddedWeapon->bHasLimitedAmmo = true;
					AddedWeapon->SetBulletCount(SpawnedBulletCount);
					UE_LOG(LogTemp, Warning, TEXT("[YANK_AMMO] CompletePull — %s granted with %d bullets (limited ammo)"),
						*AddedWeapon->GetName(), SpawnedBulletCount);
				}

				// The rounds go into the bag as well as into the gun. A cell is a magazine: the loaded
				// one costs a cell like any other, which is why picking a weapon up costs capacity
				// before it has fired a shot.
				GrantAmmoToInventory(Player, AddedWeapon);
			}

			UE_LOG(LogTemp, Warning, TEXT("[YANK_THROW] CompletePull — tagged %s: bWasYanked=true, SourceYankDropClass=%s, bHasLimitedAmmo=%d"),
				*AddedWeapon->GetName(), *GetClass()->GetName(), AddedWeapon->bHasLimitedAmmo ? 1 : 0);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[YANK_THROW] CompletePull — FindWeaponOfType returned NULL after AddWeaponClassAnimated! Weapon class: %s"),
				*WeaponClass->GetName());
		}
	}
	else
	{
		// Already carrying this gun, so the drop is worth exactly its ammo.
		//
		// This used to be the Bandolier branch, which handed the player a hidden SECOND COPY of the
		// weapon and spilled bullets between copies once at the cap. That was a whole parallel
		// carrying system next to the inventory, and it is gone: the Bandolier upgrade now raises
		// how many MAGAZINE CELLS a weapon may occupy, and the rounds go into those cells like any
		// other pickup. One system, one place to look, and the cells are what the HUD already reads.
		// An energy gun gets nothing from the cells, so its whole pickup lands in its own reserve:
		// loaded and spare alike, never touching the current magazine (2026-09-15).
		if (ExistingWeapon->UsesEnergyReserve())
		{
			const int32 Mag = ExistingWeapon->GetMagazineSize();
			const int32 OfferedLoaded = SpawnedBulletCount >= 0
				? FMath::Clamp(SpawnedBulletCount, 0, Mag)
				: FMath::Clamp(FMath::RoundToInt(EnergyMagazineFill * Mag), 0, Mag);
			const int32 OfferedReserve = CarriedEnergyReserve >= 0
				? FMath::Max(0, CarriedEnergyReserve)
				: FMath::Max(0, FMath::RoundToInt(EnergyReserveMagazines * Mag));

			const int32 ReserveRoom = FMath::Max(0, ExistingWeapon->GetEnergyReserveCapacity() - ExistingWeapon->GetEnergyReserve());
			const int32 AddReserve = FMath::Min(ReserveRoom, OfferedLoaded + OfferedReserve);
			ExistingWeapon->SetEnergyReserve(ExistingWeapon->GetEnergyReserve() + AddReserve);
			const int32 Left = (OfferedLoaded + OfferedReserve) - AddReserve;
			if (Left > 0)
			{
				ADroppedRangedWeapon* Leftover = SpawnFor(GetWorld(), WeaponClass, Player->GetActorTransform());
				if (Leftover)
				{
					Leftover->EnergyMagazineFill = 0.0f;
					Leftover->EnergyReserveMagazines = static_cast<float>(Left) / FMath::Max(1, Mag);
					Leftover->bCanBeCaptured = true;
					Leftover->SetCharge(GetCharge());
				}
			}
		}
		else
		{
			// A second copy is worth only its rounds, and they land in the reserve cells: the
			// current magazine is not topped up by the pickup.
			GrantAmmoToInventory(Player, ExistingWeapon, /*bFillMagazine*/ false);
		}

		if (ExistingWeapon == Player->GetCurrentWeapon())
		{
			Player->UpdateWeaponHUD(ExistingWeapon->GetBulletCount(), ExistingWeapon->GetMagazineSize());
		}
	}

	// The hands went on the rope at the throw. Now the gun is here: draw what is in hand, which is
	// the fetched gun when it was granted and the old one when the drop was worth only its rounds.
	if (bGrappleFetch)
	{
		Player->FinishWeaponFetch(/*bGotWeapon*/ !ExistingWeapon);
	}

	// Destroy this world actor (weapon is now in player's inventory)
	Destroy();
}
