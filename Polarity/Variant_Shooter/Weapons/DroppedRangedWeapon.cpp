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
#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/DamageEvents.h"
#include "Net/UnrealNetwork.h"

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

	// Weapon mesh — root, physics-simulated
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	SetRootComponent(WeaponMesh);
	WeaponMesh->SetSimulatePhysics(true);
	WeaponMesh->SetCollisionProfileName(FName("PhysicsActor"));
	WeaponMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	WeaponMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	WeaponMesh->SetGenerateOverlapEvents(true);
	WeaponMesh->BodyInstance.bUseCCD = true;

	// EMF field component for charge storage
	FieldComponent = CreateDefaultSubobject<UEMF_FieldComponent>(TEXT("FieldComponent"));
}

void ADroppedRangedWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ADroppedRangedWeapon, bIsBeingPulled);
	DOREPLIFETIME(ADroppedRangedWeapon, bPullComplete);
	DOREPLIFETIME(ADroppedRangedWeapon, ReplicatedCharge);
}

void ADroppedRangedWeapon::OnRep_DropCharge()
{
	// Through the normal setter so anything hanging off charge (widget, visuals) behaves as it does
	// on the server.
	SetCharge(ReplicatedCharge);
}

void ADroppedRangedWeapon::PostNetReceivePhysicState()
{
	if (WeaponMesh && !WeaponMesh->IsSimulatingPhysics())
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

	// Only the authority simulates the drop; everyone else is shown where it landed.
	if (!HasAuthority() && WeaponMesh)
	{
		WeaponMesh->SetSimulatePhysics(false);
	}

	UE_LOG(LogTemp, Warning, TEXT("[DroppedRangedWeapon] %s BeginPlay: Charge=%.2f, bCanBeCaptured=%d"),
		*GetName(), GetCharge(), bCanBeCaptured);

	// Bind hit callback for stun-on-impact. The callback gates on bCanStunOnImpact at runtime,
	// so we always bind (cheap) regardless of whether stun is currently enabled.
	if (WeaponMesh)
	{
		WeaponMesh->SetNotifyRigidBodyCollision(true);
		WeaponMesh->OnComponentHit.AddDynamic(this, &ADroppedRangedWeapon::OnWeaponMeshHit);
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
	if (!OtherActor || !WeaponMesh) return;

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
				WeaponMesh->SetPhysicsLinearVelocity(ReturnVelocity);
				if (AirMail->GetReturnSpinSpeed() > 0.0f)
				{
					AirMailOrientSpear(WeaponMesh, ReturnVelocity);
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
	if (WeaponMesh && WeaponMesh->IsSimulatingPhysics())
	{
		PreImpactVelocity = WeaponMesh->GetPhysicsLinearVelocity();
	}

	// Air Mail spear: while kicked, drive orientation kinematically (nose along velocity + roll)
	// so the asymmetric body can't precess/tumble. Self-stops once the weapon slows down.
	if (ActorHasTag(UUpgrade_AirKick::TAG_AirMailKicked))
	{
		UUpgrade_AirKick* AirMail = UUpgrade_AirKick::FindActiveAirMail(this);
		AirMailTickSpear(WeaponMesh, AirMail ? AirMail->GetKickSpinSpeed() : 720.0f);
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

	// Register (or re-register) charge widget
	if (UEMFChargeWidgetSubsystem* WidgetSub = GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>())
	{
		WidgetSub->UnregisterDroppedRangedWeapon(this);
		WidgetSub->RegisterDroppedRangedWeapon(this);
	}
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
	WeaponMesh->SetSimulatePhysics(false);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

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
		WeaponMesh->SetSimulatePhysics(true);
		WeaponMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
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

	// Transform offset into world space relative to camera
	const FVector WorldTarget = CameraLoc
		+ CameraRot.RotateVector(PullTargetOffset);
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

	if (!PullingCharacter.IsValid() || !WeaponClass)
	{
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
		// Grant a new weapon (permanent) with animated lower→swap→raise transition.
		// AddWeaponClassAnimated falls back to instant equip if player is unarmed.
		Player->AddWeaponClassAnimated(WeaponClass);

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
				ADroppedRangedWeapon* Leftover = GetWorld()->SpawnActor<ADroppedRangedWeapon>(
					GetClass(), Player->GetActorLocation(), Player->GetActorRotation());
				if (Leftover)
				{
					Leftover->WeaponClass = WeaponClass;
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

	// Destroy this world actor (weapon is now in player's inventory)
	Destroy();
}
