// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrackedTankNPC.h"
#include "HAL/IConsoleManager.h"
#include "Engine/OverlapResult.h"
#include "TrackedTankMovementComponent.h"
#include "ShooterWeapon.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/DamageEvents.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "AIController.h"
#include "ShooterAIController.h"
#include "AI/PolarityTeams.h"
#include "../DamageTypes/DamageType_DroneExplosion.h"

namespace
{
	/** Pawn responsible for this tank's death explosion - same honest attribution as the flying
	 *  drone: the killing blow's owner chain decides, any pawn qualifies, no pawn (world kill)
	 *  means the blast damages everyone including the tank's own faction. */
	APawn* ResolveExplosionInstigator(AActor* KillingCauser)
	{
		for (AActor* Candidate = KillingCauser; Candidate; Candidate = Candidate->GetOwner())
		{
			if (APawn* PawnCandidate = Cast<APawn>(Candidate))
			{
				return PawnCandidate;
			}

			if (APawn* PawnInstigator = Cast<APawn>(Candidate->GetInstigator()))
			{
				return PawnInstigator;
			}
		}

		return nullptr;
	}
}

ATrackedTankNPC::ATrackedTankNPC(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UTrackedTankMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	// Big hull on a walking capsule. Sized to the actual mesh (377 x 208 x 213): the radius covers
	// the width, the half height covers the hull. Note the engine raises the half height to the
	// radius (FMath::Max3 in SetCapsuleSize), so a radius larger than the half height silently
	// turns the whole thing into a sphere.
	GetCapsuleComponent()->SetCapsuleSize(104.0f, 107.0f);

	// Hull mesh. The model already faces +X, so unlike a character it needs no yaw correction;
	// it only drops by the half height to stand on the bottom of the capsule.
	if (USkeletalMeshComponent* HullMesh = GetMesh())
	{
		static ConstructorHelpers::FObjectFinder<USkeletalMesh> TanketteMesh(
			TEXT("/Game/Variant_Shooter/Blueprints/AI/Geometry/tankette_skeletal"));
		if (TanketteMesh.Succeeded())
		{
			HullMesh->SetSkeletalMesh(TanketteMesh.Object);
		}

		HullMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -107.0f));
		HullMesh->SetRelativeRotation(FRotator::ZeroRotator);
	}

	// Heavy hull barely moves when knocked back
	KnockbackDistanceMultiplier = 0.1f;

	// Shot at in the middle of the hull, with room to scatter over it: the hull is 213 tall, so the
	// human chest offset would land under the tracks.
	LocalAimPoint = FVector(0.0f, 0.0f, 0.0f);
	AimVerticalJitter = 40.0f;

	// Death is an explosion, not a ragdoll/GC wreck (v1)
	DefaultDeathConfig.Mode = EDeathMode::HideOnly;

	// VFX anchor. No mesh of its own any more: the hull is the skeletal mesh above, and this stays
	// only so the stage FX below keep the position they were tuned at.
	TankMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TankMesh"));
	TankMesh->SetupAttachment(RootComponent);
	TankMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TankMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 10.0f));

	// Hull damage-stage FX
	DamagedHullFXComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("DamagedHullFX"));
	DamagedHullFXComponent->SetupAttachment(TankMesh);
	DamagedHullFXComponent->bAutoActivate = false;

	CriticalHullFXComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("CriticalHullFX"));
	CriticalHullFXComponent->SetupAttachment(TankMesh);
	CriticalHullFXComponent->bAutoActivate = false;
}

// polarity.debug.novfx: та же глушилка, что у оружия. Гасится только картинка - урон, звук и
// смерть идут как обычно, иначе отладочный флаг менял бы исход боя.
static bool TankNoVFX()
{
	static IConsoleVariable* const Var =
		IConsoleManager::Get().FindConsoleVariable(TEXT("polarity.debug.novfx"));
	return Var && Var->GetInt() != 0;
}

void ATrackedTankNPC::BeginPlay()
{
	Super::BeginPlay();

	MaxHPAtSpawn = CurrentHP;
	BaseMaxWalkSpeed = GetCharacterMovement() ? GetCharacterMovement()->MaxWalkSpeed : 0.0f;

	// The weapon the base class spawned from WeaponClass IS the machine gun barrel
	MachineGun = Weapon;

	// Spawn the main gun alongside it
	if (CannonWeaponClass && GetWorld())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		CannonWeapon = GetWorld()->SpawnActor<AShooterWeapon>(CannonWeaponClass, GetActorTransform(), SpawnParams);
		if (CannonWeapon)
		{
			// The weapon subscribes itself to our burst counting via its own BeginPlay; the shot
			// counter needs the same hook the base gun got in ShooterNPC::BeginPlay
			CannonWeapon->OnShotFired.AddDynamic(this, &ATrackedTankNPC::OnWeaponShotFired);

			// The weapon attached itself during BeginPlay before CannonWeapon was stored, so the
			// barrel-specific placement did not know which slot it is - re-attach explicitly
			AttachWeaponMeshes(CannonWeapon);
		}
	}

	// Assign stage FX assets (components stay deactivated until a stage demands them)
	if (DamagedHullFXComponent && DamagedHullFX)
	{
		DamagedHullFXComponent->SetAsset(DamagedHullFX);
	}
	if (CriticalHullFXComponent && CriticalHullFX)
	{
		CriticalHullFXComponent->SetAsset(CriticalHullFX);
	}

	UpdateTankStage();
}

// ==================== Weapons ====================

void ATrackedTankNPC::SelectMainGun()
{
	SelectBarrel(true);
}

void ATrackedTankNPC::SelectMachineGun()
{
	SelectBarrel(false);
}

void ATrackedTankNPC::SelectBarrel(bool bMainGun)
{
	bMainGunActive = bMainGun;

	AShooterWeapon* Target = bMainGun ? CannonWeapon.Get() : MachineGun.Get();
	if (!Target || Weapon == Target || bIsDead)
	{
		return;
	}

	// Never leave a live burst on the barrel going away
	StopShooting();

	Weapon = Target;
	UpdateBarrelVisibility();

	UE_LOG(LogTemp, Log, TEXT("[TANK_DEBUG] %s selected %s"), *GetName(), bMainGun ? TEXT("main gun") : TEXT("machine gun"));
}

void ATrackedTankNPC::UpdateBarrelVisibility()
{
	// Both barrels are modelled on the hull, so there is nothing to show or hide: the weapon
	// actors are invisible shot emitters parked on the muzzle bones (see AttachWeaponMeshes).
	if (USkeletalMeshComponent* HullMesh = GetMesh())
	{
		if (HullMesh->DoesSocketExist(FName("muzzle_cannon")))
		{
			return;
		}
	}

	const auto SetBarrelVisible = [this](AShooterWeapon* Barrel, bool bVisible)
	{
		if (!Barrel)
		{
			return;
		}
		if (UMeshComponent* TPMesh = Barrel->GetThirdPersonMesh())
		{
			TPMesh->SetVisibility(bVisible, true);
		}
	};

	SetBarrelVisible(CannonWeapon.Get(), bMainGunActive);
	SetBarrelVisible(MachineGun.Get(), !bMainGunActive);
}

void ATrackedTankNPC::AttachWeaponMeshes(AShooterWeapon* WeaponToAttach)
{
	if (!WeaponToAttach)
	{
		return;
	}

	// Explicit form on purpose: the two-argument SnapToTarget rule snaps the scale as well and
	// wipes whatever the weapon blueprint was authored at (that is how the enemy ended up holding
	// a smaller rifle than the player).
	const FAttachmentTransformRules AttachmentRule(
		EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepRelative, false);

	WeaponToAttach->AttachToActor(this, AttachmentRule);

	if (WeaponToAttach->GetFirstPersonMesh())
	{
		WeaponToAttach->GetFirstPersonMesh()->SetVisibility(false);
	}

	if (USkeletalMeshComponent* TPMesh = WeaponToAttach->GetThirdPersonMesh())
	{
		const bool bIsCannon = (WeaponToAttach == CannonWeapon) ||
			(CannonWeaponClass && WeaponToAttach->IsA(CannonWeaponClass));

		USkeletalMeshComponent* const HullMesh = GetMesh();
		const FName MuzzleBone = bIsCannon ? FName("muzzle_cannon") : FName("muzzle_mg");

		if (HullMesh && HullMesh->DoesSocketExist(MuzzleBone))
		{
			// The barrels are part of the hull model, so the weapon actor is only here to own the
			// shot: hide its mesh and park it so its own Muzzle socket lands exactly on the barrel
			// tip. Otherwise shells appear a gun length away from where the tank looks like it
			// fired, and at this hull size that is metres.
			TPMesh->AttachToComponent(HullMesh, AttachmentRule, MuzzleBone);

			// "Muzzle" is the socket AShooterWeapon resolves shots through (MuzzleSocketName)
			static const FName WeaponMuzzleSocket("Muzzle");
			const FVector MuzzleOffset = TPMesh->DoesSocketExist(WeaponMuzzleSocket)
				? TPMesh->GetSocketTransform(WeaponMuzzleSocket, RTS_Component).GetLocation()
				: FVector::ZeroVector;

			TPMesh->SetRelativeLocation(-MuzzleOffset);
			TPMesh->SetRelativeRotation(FRotator::ZeroRotator);
			TPMesh->SetVisibility(false);
		}
		else
		{
			// No mesh yet (or an older hull): fall back to the hand-placed offsets
			TPMesh->AttachToComponent(TankMesh, AttachmentRule, NAME_None);
			TPMesh->SetRelativeLocation(bIsCannon ? CannonMeshOffset : MGMeshOffset);
			TPMesh->SetRelativeRotation(FRotator::ZeroRotator);
		}
	}
}

UMeshComponent* ATrackedTankNPC::GetHitFlashMeshComponent() const
{
	// The hull that is actually visible, otherwise the hit flash plays on an invisible anchor
	if (USkeletalMeshComponent* HullMesh = GetMesh())
	{
		if (HullMesh->GetSkeletalMeshAsset())
		{
			return HullMesh;
		}
	}

	return TankMesh;
}

// ==================== Damage & Death ====================

float ATrackedTankNPC::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// Track-zone classification must read the event before Super consumes anything
	RegisterTrackHit(Damage, DamageEvent, EventInstigator, DamageCauser);

	const float AppliedDamage = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	if (!bIsDead)
	{
		UpdateTankStage();
	}

	return AppliedDamage;
}

void ATrackedTankNPC::RegisterTrackHit(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (Damage <= 0.0f || bImmobilized || !GetCapsuleComponent())
	{
		return;
	}

	// Impact point with the same guarded shape as PolarityCharacter::TakeDamage: hand-raised
	// radial events carry no ComponentHits, and their GetBestHitInfo would ensure-crash.
	FHitResult HitInfo;
	FVector ImpulseDir = FVector::ZeroVector;

	if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
	{
		const FRadialDamageEvent& RadialEvent = static_cast<const FRadialDamageEvent&>(DamageEvent);
		if (RadialEvent.ComponentHits.Num() > 0)
		{
			DamageEvent.GetBestHitInfo(EventInstigator ? EventInstigator->GetPawn() : this, DamageCauser, HitInfo, ImpulseDir);
		}
		else
		{
			// Blast origin stands in for the impact; a low blast next to the hull chews tracks too
			HitInfo.ImpactPoint = RadialEvent.Origin;
		}
	}
	else
	{
		DamageEvent.GetBestHitInfo(EventInstigator ? EventInstigator->GetPawn() : this, DamageCauser, HitInfo, ImpulseDir);
	}

	if (HitInfo.ImpactPoint.IsZero())
	{
		return;
	}

	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const float CapsuleBottomZ = GetActorLocation().Z - Capsule->GetScaledCapsuleHalfHeight();
	const float CapsuleHeight = Capsule->GetScaledCapsuleHalfHeight() * 2.0f;

	const float HeightFraction = (HitInfo.ImpactPoint.Z - CapsuleBottomZ) / CapsuleHeight;
	if (HeightFraction > TrackHitHeightFraction)
	{
		return;
	}

	TrackAccumulatedDamage += Damage;
	UE_LOG(LogTemp, Log, TEXT("[TANK_DEBUG] %s track hit %.1f (acc %.1f / %.1f)"),
		*GetName(), Damage, TrackAccumulatedDamage, TrackDamageToImmobilize);

	if (TrackAccumulatedDamage >= TrackDamageToImmobilize)
	{
		Immobilize();
	}
}

void ATrackedTankNPC::Immobilize()
{
	if (bImmobilized || bIsDead)
	{
		return;
	}

	bImmobilized = true;

	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->MaxWalkSpeed = 0.0f;
		CMC->StopMovementImmediately();
	}
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->StopMovement();
	}

	OnRep_bImmobilized();

	UE_LOG(LogTemp, Warning, TEXT("[TANK_DEBUG] %s immobilized - tracks destroyed, guns still up"), *GetName());
}

void ATrackedTankNPC::OnRep_bImmobilized()
{
	// TODO(VFX): track sparks while broken - needs Niagara assets assigned in the editor
}

void ATrackedTankNPC::Die()
{
	if (bExplodeOnDeath)
	{
		// Before Super: Die() zeroes the EMF charge that scales the blast
		TriggerExplosion();
	}

	Super::Die();
}

void ATrackedTankNPC::TriggerExplosion()
{
	float ChargeScale = 1.0f;
	if (bScaleExplosionWithCharge && EMFVelocityModifier)
	{
		const float AbsCharge = FMath::Abs(EMFVelocityModifier->GetCharge());
		ChargeScale = FMath::Clamp(AbsCharge / ExplosionReferenceCharge, MinChargeScale, MaxChargeScale);
	}

	const float FinalExplosionDamage = ExplosionDamage * ChargeScale;
	const float FinalExplosionRadius = ExplosionRadius * ChargeScale;

	if (ExplosionFX && GetWorld() && !TankNoVFX())
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), ExplosionFX, GetActorLocation(), GetActorRotation(),
			FVector(ExplosionFXScale), true, true, ENCPoolMethod::None, true);
	}

	if (ExplosionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), ExplosionSound, GetActorLocation());
	}

	if (FinalExplosionDamage <= 0.0f || FinalExplosionRadius <= 0.0f)
	{
		return;
	}

	const FVector Origin = GetActorLocation();

	TArray<FOverlapResult> Overlaps;
	FCollisionShape Sphere = FCollisionShape::MakeSphere(FinalExplosionRadius);
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	GetWorld()->OverlapMultiByChannel(Overlaps, Origin, FQuat::Identity, ECC_Pawn, Sphere, QueryParams);

	// Honest attribution: whoever dealt the killing blow answers for the blast; no pawn (world
	// kill) means the blast damages everyone including the robot faction
	APawn* ExplosionInstigator = ResolveExplosionInstigator(LastKillingDamageCauser);

	TSet<AActor*> DamagedActors;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* HitActor = Overlap.GetActor();
		if (!HitActor || DamagedActors.Contains(HitActor))
		{
			continue;
		}
		DamagedActors.Add(HitActor);

		FHitResult LOSHit;
		FCollisionQueryParams LOSParams;
		LOSParams.AddIgnoredActor(this);
		LOSParams.AddIgnoredActor(HitActor);
		if (GetWorld()->LineTraceSingleByChannel(LOSHit, Origin, HitActor->GetActorLocation(), ECC_Visibility, LOSParams))
		{
			continue;
		}

		const float Distance = FVector::Dist(Origin, HitActor->GetActorLocation());
		const float DamageScale = FMath::Clamp(1.0f - Distance / FinalExplosionRadius, 0.0f, 1.0f);
		const float ActorDamage = FinalExplosionDamage * DamageScale;
		if (ActorDamage <= 0.0f)
		{
			continue;
		}

		FRadialDamageEvent RadialDamageEvent;
		RadialDamageEvent.DamageTypeClass = UDamageType_DroneExplosion::StaticClass();
		RadialDamageEvent.Origin = Origin;
		RadialDamageEvent.Params.BaseDamage = FinalExplosionDamage;
		RadialDamageEvent.Params.OuterRadius = FinalExplosionRadius;
		HitActor->TakeDamage(ActorDamage, RadialDamageEvent, nullptr, ExplosionInstigator);
	}

	// Stun nearby NPCs (same logic as drone/prop explosions)
	if (bApplyExplosionStun)
	{
		TArray<FOverlapResult> StunOverlaps;
		FCollisionQueryParams StunQueryParams;
		StunQueryParams.AddIgnoredActor(this);

		GetWorld()->OverlapMultiByChannel(StunOverlaps, Origin, FQuat::Identity, ECC_Pawn, Sphere, StunQueryParams);

		TSet<AShooterNPC*> StunnedNPCs;
		for (const FOverlapResult& Overlap : StunOverlaps)
		{
			AShooterNPC* NPC = Cast<AShooterNPC>(Overlap.GetActor());
			if (!NPC || StunnedNPCs.Contains(NPC) || NPC->IsDead() || NPC == this)
			{
				continue;
			}
			StunnedNPCs.Add(NPC);
			NPC->ApplyExplosionStun(ExplosionStunDuration, nullptr);
		}
	}
}

// ==================== Damage Stages ====================

void ATrackedTankNPC::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATrackedTankNPC, TankDamageStage);
	DOREPLIFETIME(ATrackedTankNPC, bImmobilized);
	DOREPLIFETIME(ATrackedTankNPC, TurretYaw);
	DOREPLIFETIME(ATrackedTankNPC, GunPitch);
}

// ==================== Turret ====================

void ATrackedTankNPC::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Authority only: the aim comes from the AI controller, which clients do not have. They read
	// the result off the two replicated angles.
	if (HasAuthority())
	{
		UpdateTurret(DeltaSeconds);
	}
}

void ATrackedTankNPC::SetTurretAimLocation(const FVector& WorldLocation)
{
	TurretAimLocation = WorldLocation;
	bHasTurretAim = true;
}

void ATrackedTankNPC::ClearTurretAim()
{
	bHasTurretAim = false;
}

bool ATrackedTankNPC::IsTurretOnTarget(float ToleranceDegrees) const
{
	if (!bTurretTracking)
	{
		return false;
	}

	return FMath::Abs(FMath::FindDeltaAngleDegrees(TurretYaw, TurretDesiredYaw)) <= ToleranceDegrees
		&& FMath::Abs(FMath::FindDeltaAngleDegrees(GunPitch, TurretDesiredPitch)) <= ToleranceDegrees;
}

void ATrackedTankNPC::UpdateTurret(float DeltaSeconds)
{
	TurretDesiredYaw = 0.0f;
	TurretDesiredPitch = 0.0f;

	// An explicit request wins. Failing that, track whatever this tank is fighting: otherwise the
	// turret would only move for the tasks that aim it deliberately, and a tank firing straight
	// past its own barrel is the first thing anyone notices. Kept in locals, because latching the
	// member here would freeze the turret on the position the enemy had when it was first seen.
	FVector AimAt = TurretAimLocation;
	bool bAiming = bHasTurretAim;

	if (!bAiming)
	{
		if (const AShooterAIController* AI = Cast<AShooterAIController>(GetController()))
		{
			if (const AActor* Enemy = AI->GetCurrentTarget())
			{
				AimAt = Enemy->GetActorLocation();
				bAiming = true;
			}
		}
	}

	if (bAiming && !IsDead())
	{
		// Aim from the turret ring, not from the actor origin: at close range the difference
		// between the two is most of the elevation angle.
		const FVector Pivot = GetActorLocation() + FVector(0.0f, 0.0f, TurretPivotHeight - GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		const FRotator Look = (AimAt - Pivot).Rotation();
		const FRotator Local = (Look - GetActorRotation()).GetNormalized();

		TurretDesiredYaw = Local.Yaw;
		TurretDesiredPitch = FMath::Clamp(Local.Pitch, GunPitchMin, GunPitchMax);
	}

	bTurretTracking = bAiming && !IsDead();

	// FixedTurn takes the short way round and stops exactly on the target, so a turret that has
	// arrived stops dithering by a fraction of a degree every frame.
	TurretYaw = FMath::FixedTurn(TurretYaw, TurretDesiredYaw, TurretYawRate * DeltaSeconds);
	GunPitch = FMath::FixedTurn(GunPitch, TurretDesiredPitch, GunPitchRate * DeltaSeconds);
}

float ATrackedTankNPC::GetStageSpeedMultiplier() const
{
	switch (TankDamageStage)
	{
	case ETankDamageStage::Damaged:  return DamagedSpeedMultiplier;
	case ETankDamageStage::Critical: return CriticalSpeedMultiplier;
	default:                         return 1.0f;
	}
}

void ATrackedTankNPC::UpdateTankStage()
{
	const float MaxHP = MaxHPAtSpawn > 0.0f ? MaxHPAtSpawn : FMath::Max(CurrentHP, 1.0f);
	const float HPFraction = CurrentHP / MaxHP;

	ETankDamageStage NewStage = ETankDamageStage::Intact;
	if (HPFraction <= CriticalStageHPFraction)
	{
		NewStage = ETankDamageStage::Critical;
	}
	else if (HPFraction <= DamagedStageHPFraction)
	{
		NewStage = ETankDamageStage::Damaged;
	}

	TankDamageStage = NewStage;

	ApplyStageVFX(NewStage);

	// Speed follows the stage; immobilization wins over any multiplier
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->MaxWalkSpeed = bImmobilized ? 0.0f : BaseMaxWalkSpeed * GetStageSpeedMultiplier();
	}
}

void ATrackedTankNPC::ApplyStageVFX(ETankDamageStage Stage)
{
	if (DamagedHullFXComponent)
	{
		if (Stage == ETankDamageStage::Damaged || Stage == ETankDamageStage::Critical)
		{
			DamagedHullFXComponent->Activate();
		}
		else
		{
			DamagedHullFXComponent->Deactivate();
		}
	}

	if (CriticalHullFXComponent)
	{
		if (Stage == ETankDamageStage::Critical)
		{
			CriticalHullFXComponent->Activate();
		}
		else
		{
			CriticalHullFXComponent->Deactivate();
		}
	}
}

void ATrackedTankNPC::OnRep_TankDamageStage()
{
	ApplyStageVFX(TankDamageStage);
}

// ==================== Pool Recycling ====================

void ATrackedTankNPC::ResetForPool(const FVector& NewLocation, const FRotator& NewRotation)
{
	// Own state first (virtual dispatch order, same pattern as HumanoidNPC::ResetForPool)
	TankDamageStage = ETankDamageStage::Intact;
	TrackAccumulatedDamage = 0.0f;
	bImmobilized = false;

	Super::ResetForPool(NewLocation, NewRotation);

	// The base class may have rebuilt its (machine-gun) weapon after death
	MachineGun = Weapon;

	// Rebuild the main gun if it was destroyed with the previous life
	if ((!IsValid(CannonWeapon)) && CannonWeaponClass && GetWorld())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		CannonWeapon = GetWorld()->SpawnActor<AShooterWeapon>(CannonWeaponClass, GetActorTransform(), SpawnParams);
		if (CannonWeapon)
		{
			CannonWeapon->OnShotFired.AddDynamic(this, &ATrackedTankNPC::OnWeaponShotFired);
			AttachWeaponMeshes(CannonWeapon);
		}
	}

	SelectBarrel(bMainGunActive);
	UpdateTankStage();
	UpdateBarrelVisibility();
}
