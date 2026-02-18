// EMFPhysicsProp.cpp
// Physics-simulated prop with full EMF system integration

#include "EMFPhysicsProp.h"
#include "Engine/DamageEvents.h"
#include "EMFChannelingPlateActor.h"
#include "EMF_FieldComponent.h"
#include "EMF_PluginBPLibrary.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/DamageTypes/DamageType_Wallslam.h"
#include "Variant_Shooter/DamageTypes/DamageType_EMFProximity.h"
#include "Variant_Shooter/DamageTypes/DamageType_Melee.h"
#include "EMFVelocityModifier.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "DrawDebugHelpers.h"

AEMFPhysicsProp::AEMFPhysicsProp()
{
	PrimaryActorTick.bCanEverTick = true;

	// Physics mesh as root (physics body drives actor transform)
	PropMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PropMesh"));
	SetRootComponent(PropMesh);
	PropMesh->SetSimulatePhysics(true);
	PropMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	PropMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	PropMesh->SetGenerateOverlapEvents(true);
	PropMesh->BodyInstance.bUseCCD = true;

	// EMF field component
	FieldComponent = CreateDefaultSubobject<UEMF_FieldComponent>(TEXT("FieldComponent"));
}

// ==================== AActor Overrides ====================

void AEMFPhysicsProp::BeginPlay()
{
	Super::BeginPlay();

	CurrentHP = MaxHP;

	// Initialize EMF field component
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = DefaultCharge;
		Desc.PhysicsParams.Mass = DefaultMass;
		Desc.OwnerType = EEMSourceOwnerType::PhysicsProp;
		FieldComponent->SetSourceDescription(Desc);
	}

	// Sync physics body mass with EMF mass
	if (PropMesh)
	{
		PropMesh->SetMassOverrideInKg(NAME_None, DefaultMass, true);
		PropMesh->OnComponentBeginOverlap.AddDynamic(this, &AEMFPhysicsProp::OnPropOverlap);
	}
}

void AEMFPhysicsProp::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bIsDead)
	{
		return;
	}

	if (bAffectedByExternalFields && FieldComponent && PropMesh && PropMesh->IsSimulatingPhysics())
	{
		ApplyEMForces(DeltaTime);
	}

	if (bCanBeCaptured && CapturingPlate.IsValid())
	{
		UpdateCaptureForces(DeltaTime);
	}

	UpdateChargeTracking();
}

// ==================== EMF Force Application ====================

void AEMFPhysicsProp::ApplyEMForces(float DeltaTime)
{
	const float Charge = GetCharge();
	if (FMath::IsNearlyZero(Charge))
	{
		return;
	}

	TArray<FEMSourceDescription> OtherSources = FieldComponent->GetAllOtherSources();
	if (OtherSources.Num() == 0)
	{
		return;
	}

	const FVector Position = GetActorLocation();
	const FVector Velocity = PropMesh->GetPhysicsLinearVelocity();
	const float MaxDistSq = MaxSourceDistance * MaxSourceDistance;
	const float OppositeChargeMinDistSq = OppositeChargeMinDistance * OppositeChargeMinDistance;
	const int32 MyChargeSign = (Charge > KINDA_SMALL_NUMBER) ? 1 : ((Charge < -KINDA_SMALL_NUMBER) ? -1 : 0);

	FVector TotalForce = FVector::ZeroVector;
	bool bShouldApplyProximityDamping = false;

	for (const FEMSourceDescription& Source : OtherSources)
	{
		if (IsSourceEffectivelyZero(Source))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(Position, Source.Position);

		if (DistSq > MaxDistSq)
		{
			continue;
		}

		// Opposite-charge distance cutoff: skip close opposite-charge sources to prevent Coulomb 1/r² singularity
		if (bEnableOppositeChargeDistanceCutoff && DistSq < OppositeChargeMinDistSq)
		{
			const int32 SourceChargeSign = GetSourceEffectiveChargeSign(Source);
			if (SourceChargeSign != 0 && MyChargeSign != 0 && SourceChargeSign != MyChargeSign)
			{
				bShouldApplyProximityDamping = true;
				continue;
			}
		}

		const float Multiplier = GetForceMultiplierForOwnerType(Source.OwnerType);
		if (FMath::IsNearlyZero(Multiplier))
		{
			continue;
		}

		// Skip channeling plate forces if captured (handled by UpdateCaptureForces)
		if (CapturingPlate.IsValid() &&
			Source.SourceType == EEMSourceType::FinitePlate &&
			Source.OwnerType == EEMSourceOwnerType::Player)
		{
			continue;
		}

		TArray<FEMSourceDescription> SingleSource;
		SingleSource.Add(Source);

		const FVector SourceForce = UEMF_PluginBPLibrary::CalculateLorentzForceComplete(
			Charge, Position, Velocity, SingleSource, true);

		TotalForce += SourceForce * Multiplier;
	}

	// Suppress all non-plate forces during reverse channeling launch (mirrors NPC PassThrough behavior)
	if (CapturingPlate.IsValid() && CapturingPlate.Get()->IsInReverseMode())
	{
		TotalForce = FVector::ZeroVector;
	}

	// Clamp
	if (TotalForce.SizeSquared() > MaxEMForce * MaxEMForce)
	{
		TotalForce = TotalForce.GetSafeNormal() * MaxEMForce;
	}

	// Apply as continuous force to physics body
	if (!TotalForce.IsNearlyZero())
	{
		PropMesh->AddForce(TotalForce);
	}

	// Proximity damping: viscous braking when inside opposite-charge cutoff distance
	// Prevents prop from passing through the source after EM force is suppressed
	if (bShouldApplyProximityDamping && OppositeChargeProximityDamping > 0.0f)
	{
		const float PhysMass = PropMesh->GetMass();
		const FVector DampingForce = -Velocity * OppositeChargeProximityDamping * PhysMass;
		PropMesh->AddForce(DampingForce);

		if (bDrawDebugForces)
		{
			DrawDebugDirectionalArrow(
				GetWorld(), Position,
				Position + DampingForce.GetSafeNormal() * FMath::Min(DampingForce.Size() * 0.01f, 100.0f),
				8.0f, FColor::Orange, false, -1.0f, 0, 2.0f);
		}
	}

	// Debug
	if (bDrawDebugForces && !TotalForce.IsNearlyZero())
	{
		DrawDebugDirectionalArrow(
			GetWorld(), Position,
			Position + TotalForce.GetSafeNormal() * FMath::Min(TotalForce.Size() * 0.01f, 200.0f),
			10.0f, FColor::Cyan, false, -1.0f, 0, 2.0f);
	}

	if (bLogEMForces && !TotalForce.IsNearlyZero())
	{
		UE_LOG(LogTemp, Log, TEXT("EMFPhysicsProp %s: Charge=%.2f Force=(%.0f, %.0f, %.0f) Sources=%d"),
			*GetName(), Charge, TotalForce.X, TotalForce.Y, TotalForce.Z, OtherSources.Num());
	}
}

// ==================== Channeling Capture ====================

void AEMFPhysicsProp::SetCapturedByPlate(AEMFChannelingPlateActor* Plate)
{
	if (!Plate || !bCanBeCaptured)
	{
		return;
	}

	CapturingPlate = Plate;
	WeakCaptureTimer = 0.0f;
	bHasPreviousPlatePosition = false;
}

void AEMFPhysicsProp::ReleasedFromCapture()
{
	CapturingPlate.Reset();
	bHasPreviousPlatePosition = false;
	WeakCaptureTimer = 0.0f;
}

void AEMFPhysicsProp::DetachFromPlate()
{
	CapturingPlate.Reset();
	bHasPreviousPlatePosition = false;
}

void AEMFPhysicsProp::UpdateCaptureForces(float DeltaTime)
{
	AEMFChannelingPlateActor* Plate = CapturingPlate.Get();
	if (!Plate || !PropMesh || !PropMesh->IsSimulatingPhysics())
	{
		return;
	}

	const FVector Position = GetActorLocation();
	const FVector PlatePos = Plate->GetActorLocation();
	const float Distance = FVector::Dist(Position, PlatePos);

	// Wall check: if there's a wall between prop and plate, don't apply capture forces
	{
		FHitResult WallCheck;
		FCollisionQueryParams WallParams;
		WallParams.AddIgnoredActor(this);
		WallParams.AddIgnoredActor(Plate);
		const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
			WallCheck, Position, PlatePos, ECC_Visibility, WallParams);

		if (bBlocked)
		{
			WeakCaptureTimer += DeltaTime;
			if (WeakCaptureTimer >= CaptureReleaseTimeout)
			{
				ReleasedFromCapture();
			}
			return;
		}
	}

	// Smoothstep capture strength
	float CaptureStrength = 0.0f;
	if (Distance < CaptureRadius)
	{
		const float T = Distance / CaptureRadius;
		CaptureStrength = 1.0f - T * T * (3.0f - 2.0f * T);
	}

	// Auto-release check
	if (CaptureStrength < CaptureMinStrength)
	{
		WeakCaptureTimer += DeltaTime;
		if (WeakCaptureTimer >= CaptureReleaseTimeout)
		{
			ReleasedFromCapture();
			return;
		}
	}
	else
	{
		WeakCaptureTimer = 0.0f;
	}

	// Plate velocity via finite difference
	FVector PlateVelocity = FVector::ZeroVector;
	if (bHasPreviousPlatePosition && DeltaTime > SMALL_NUMBER)
	{
		PlateVelocity = (PlatePos - PreviousPlatePosition) / DeltaTime;
	}
	PreviousPlatePosition = PlatePos;
	bHasPreviousPlatePosition = true;

	const FVector PropVelocity = PropMesh->GetPhysicsLinearVelocity();
	const FVector RelativeVelocity = PropVelocity - PlateVelocity;
	const float PhysMass = PropMesh->GetMass();

	if (Plate->IsInReverseMode())
	{
		// Reverse mode: direct velocity correction (mirrors NPC velocity-based damping)
		const FVector PlateNormal = Plate->GetPlateNormal();

		// Zero all velocity — launch force will set the correct direction.
		// Projecting onto PlateNormal caused sideways teleportation when camera rotated between frames.
		PropMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);

		// Apply launch force along plate normal (camera forward)
		const float PropCharge = GetCharge();
		if (!FMath::IsNearlyZero(PropCharge) && Plate->PlateFieldComponent)
		{
			FEMSourceDescription PlateSource = Plate->PlateFieldComponent->GetSourceDescription();
			TArray<FEMSourceDescription> SingleSource;
			SingleSource.Add(PlateSource);

			const FVector PlateForce = UEMF_PluginBPLibrary::CalculateLorentzForceComplete(
				PropCharge, Position, FVector::ZeroVector, SingleSource, true);

			PropMesh->AddForce(PlateNormal * PlateForce.Size());
		}
		// No gravity compensation in reverse mode — prop launches freely
	}
	else
	{
		// Normal capture: damp all relative velocity
		const float DampingFactor = 1.0f - FMath::Exp(-ViscosityCoefficient * CaptureStrength * DeltaTime);
		const FVector DampingForce = -RelativeVelocity * DampingFactor * PhysMass / FMath::Max(DeltaTime, SMALL_NUMBER);

		PropMesh->AddForce(DampingForce);

		// Gravity counteraction
		if (bCounterGravityWhenCaptured)
		{
			const float GravityZ = GetWorld()->GetGravityZ();
			const float CounterForceZ = -GravityZ * GravityCounterStrength * CaptureStrength * PhysMass;
			PropMesh->AddForce(FVector(0.0f, 0.0f, CounterForceZ));
		}

		// Hooke spring: force proportional to distance (stronger pull when far, gentle near center)
		if (CaptureSpringStiffness > 0.0f)
		{
			const FVector ToPlate = PlatePos - Position;
			const FVector SpringForce = ToPlate * CaptureSpringStiffness * CaptureStrength * PhysMass;
			PropMesh->AddForce(SpringForce);
		}
	}
}

// ==================== Collision Damage ====================

void AEMFPhysicsProp::OnPropOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!bDealCollisionDamage || !OtherActor || bIsDead)
	{
		return;
	}

	// Cooldown check
	const float CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastCollisionDamageTime < CollisionDamageCooldown)
	{
		return;
	}

	// Only damage ShooterNPC targets
	AShooterNPC* HitNPC = Cast<AShooterNPC>(OtherActor);
	if (!HitNPC || HitNPC->IsDead())
	{
		return;
	}

	// Impact speed from prop's velocity
	const FVector PropVelocity = PropMesh->GetPhysicsLinearVelocity();
	const float ImpactSpeed = PropVelocity.Size();

	// Kinetic damage
	float KineticDamage = 0.0f;
	if (ImpactSpeed >= CollisionVelocityThreshold)
	{
		const float Excess = ImpactSpeed - CollisionVelocityThreshold;
		KineticDamage = (Excess / 100.0f) * CollisionDamagePerVelocity;
	}

	// EMF discharge damage (opposite charges)
	float EMFDamage = 0.0f;
	const float PropCharge = GetCharge();
	UEMFVelocityModifier* NPCModifier = HitNPC->FindComponentByClass<UEMFVelocityModifier>();
	if (NPCModifier && !FMath::IsNearlyZero(PropCharge))
	{
		const float NPCCharge = NPCModifier->GetCharge();
		if (PropCharge * NPCCharge < 0.0f) // Opposite charges
		{
			const float TotalMag = FMath::Abs(PropCharge) + FMath::Abs(NPCCharge);
			EMFDamage = EMFProximityDamage * (TotalMag / 100.0f);
			EMFDamage = FMath::Max(EMFDamage, EMFProximityDamage);
		}
	}

	// Impact point: use midpoint between actors (overlap doesn't provide exact contact)
	const FVector ImpactPoint = (GetActorLocation() + HitNPC->GetActorLocation()) * 0.5f;

	// Apply kinetic damage
	if (KineticDamage > 0.0f)
	{
		FDamageEvent KineticEvent;
		KineticEvent.DamageTypeClass = UDamageType_Wallslam::StaticClass();
		HitNPC->TakeDamage(KineticDamage, KineticEvent, nullptr, this);
	}

	// Apply EMF damage
	if (EMFDamage > 0.0f)
	{
		FDamageEvent EMFEvent;
		EMFEvent.DamageTypeClass = UDamageType_EMFProximity::StaticClass();
		HitNPC->TakeDamage(EMFDamage, EMFEvent, nullptr, this);

		// EMF discharge VFX
		if (EMFDischargeVFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				GetWorld(), EMFDischargeVFX, ImpactPoint,
				FRotator::ZeroRotator, FVector(EMFDischargeVFXScale),
				true, true, ENCPoolMethod::None);
		}
	}

	// Impact sound
	if (ImpactSound && (KineticDamage > 0.0f || EMFDamage > 0.0f))
	{
		UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, ImpactPoint);
	}

	LastCollisionDamageTime = CurrentTime;

	if (bLogEMForces)
	{
		UE_LOG(LogTemp, Warning, TEXT("EMFPhysicsProp %s hit NPC %s: Speed=%.0f, KineticDmg=%.1f, EMFDmg=%.1f"),
			*GetName(), *HitNPC->GetName(), ImpactSpeed, KineticDamage, EMFDamage);
	}
}

// ==================== Damage & Health ====================

float AEMFPhysicsProp::TakeDamage(float Damage, FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	if (bIsDead)
	{
		return 0.0f;
	}

	const float ActualDamage = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	// Melee charge transfer
	if (DamageEvent.DamageTypeClass &&
		DamageEvent.DamageTypeClass->IsChildOf(UDamageType_Melee::StaticClass()))
	{
		if (FieldComponent && EventInstigator)
		{
			APawn* Attacker = EventInstigator->GetPawn();
			if (Attacker)
			{
				float ChargeToAdd = ChargeChangeOnMeleeHit;

				// Read attacker's charge sign
				UEMFVelocityModifier* AttackerEMF = Attacker->FindComponentByClass<UEMFVelocityModifier>();
				if (AttackerEMF)
				{
					const float AttackerCharge = AttackerEMF->GetCharge();
					if (FMath::Abs(AttackerCharge) >= KINDA_SMALL_NUMBER)
					{
						ChargeToAdd = -FMath::Abs(ChargeChangeOnMeleeHit) * FMath::Sign(AttackerCharge);
					}
				}

				const float OldCharge = GetCharge();
				SetCharge(OldCharge + ChargeToAdd);
			}
		}
	}

	CurrentHP = FMath::Max(0.0f, CurrentHP - ActualDamage);
	OnPropDamaged.Broadcast(this, ActualDamage, DamageCauser);

	if (CurrentHP <= 0.0f)
	{
		Die(DamageCauser);
	}

	return ActualDamage;
}

void AEMFPhysicsProp::Die(AActor* Killer)
{
	if (bIsDead)
	{
		return;
	}

	bIsDead = true;
	OnPropDeath.Broadcast(this, Killer);

	// Release from capture if held
	if (CapturingPlate.IsValid())
	{
		ReleasedFromCapture();
	}
}

// ==================== Charge API ====================

float AEMFPhysicsProp::GetCharge() const
{
	if (!FieldComponent)
	{
		return 0.0f;
	}
	return FieldComponent->GetSourceDescription().PointChargeParams.Charge;
}

void AEMFPhysicsProp::SetCharge(float NewCharge)
{
	if (!FieldComponent)
	{
		return;
	}
	FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
	Desc.PointChargeParams.Charge = NewCharge;
	FieldComponent->SetSourceDescription(Desc);
}

float AEMFPhysicsProp::GetPropMass() const
{
	if (!FieldComponent)
	{
		return DefaultMass;
	}
	return FieldComponent->GetSourceDescription().PhysicsParams.Mass;
}

void AEMFPhysicsProp::SetPropMass(float NewMass)
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PhysicsParams.Mass = NewMass;
		FieldComponent->SetSourceDescription(Desc);
	}

	// Keep physics body mass in sync
	if (PropMesh)
	{
		PropMesh->SetMassOverrideInKg(NAME_None, NewMass, true);
	}
}

// ==================== IShooterDummyTarget ====================

bool AEMFPhysicsProp::GrantsStableCharge_Implementation() const
{
	return bGrantsStableCharge;
}

float AEMFPhysicsProp::GetStableChargeAmount_Implementation() const
{
	return StableChargePerHit;
}

float AEMFPhysicsProp::GetKillChargeBonus_Implementation() const
{
	return KillChargeBonus;
}

bool AEMFPhysicsProp::IsDummyDead_Implementation() const
{
	return bIsDead;
}

// ==================== Charge Tracking & Overlay ====================

void AEMFPhysicsProp::UpdateChargeTracking()
{
	const float Charge = GetCharge();

	uint8 CurrentPolarity = 0;
	if (Charge > KINDA_SMALL_NUMBER)
	{
		CurrentPolarity = 1;
	}
	else if (Charge < -KINDA_SMALL_NUMBER)
	{
		CurrentPolarity = 2;
	}

	if (!FMath::IsNearlyEqual(Charge, PreviousChargeValue, 0.001f))
	{
		OnChargeChanged.Broadcast(Charge, CurrentPolarity);
		PreviousChargeValue = Charge;
	}

	if (CurrentPolarity != PreviousPolarity)
	{
		UpdateChargeOverlay(CurrentPolarity);
		PreviousPolarity = CurrentPolarity;
	}
}

void AEMFPhysicsProp::UpdateChargeOverlay(uint8 NewPolarity)
{
	if (!bUseChargeOverlay || !PropMesh)
	{
		return;
	}

	UMaterialInterface* TargetMaterial = nullptr;

	switch (NewPolarity)
	{
	case 0:
		TargetMaterial = NeutralChargeOverlayMaterial;
		break;
	case 1:
		TargetMaterial = PositiveChargeOverlayMaterial;
		break;
	case 2:
		TargetMaterial = NegativeChargeOverlayMaterial;
		break;
	}

	PropMesh->SetOverlayMaterial(TargetMaterial);
}

// ==================== Force Filtering ====================

float AEMFPhysicsProp::GetForceMultiplierForOwnerType(EEMSourceOwnerType OwnerType) const
{
	switch (OwnerType)
	{
	case EEMSourceOwnerType::Player:
		return PlayerForceMultiplier;
	case EEMSourceOwnerType::NPC:
		return NPCForceMultiplier;
	case EEMSourceOwnerType::Projectile:
		return ProjectileForceMultiplier;
	case EEMSourceOwnerType::Environment:
		return EnvironmentForceMultiplier;
	case EEMSourceOwnerType::PhysicsProp:
		return PhysicsPropForceMultiplier;
	case EEMSourceOwnerType::None:
	default:
		return UnknownForceMultiplier;
	}
}

// ==================== Source Zero Check ====================

bool AEMFPhysicsProp::IsSourceEffectivelyZero(const FEMSourceDescription& Source)
{
	switch (Source.SourceType)
	{
	case EEMSourceType::PointCharge:
		return FMath::IsNearlyZero(Source.PointChargeParams.Charge);
	case EEMSourceType::LineCharge:
		return FMath::IsNearlyZero(Source.LineChargeParams.LinearChargeDensity);
	case EEMSourceType::ChargedRing:
		return FMath::IsNearlyZero(Source.RingParams.TotalCharge);
	case EEMSourceType::ChargedSphere:
		return FMath::IsNearlyZero(Source.SphereParams.TotalCharge);
	case EEMSourceType::ChargedBall:
		return FMath::IsNearlyZero(Source.BallParams.TotalCharge);
	case EEMSourceType::InfinitePlate:
	case EEMSourceType::FinitePlate:
		return FMath::IsNearlyZero(Source.PlateParams.SurfaceChargeDensity);
	case EEMSourceType::Dipole:
		return Source.DipoleParams.DipoleMoment.IsNearlyZero();
	case EEMSourceType::CurrentWire:
		return FMath::IsNearlyZero(Source.WireParams.Current);
	case EEMSourceType::CurrentLoop:
		return FMath::IsNearlyZero(Source.LoopParams.Current);
	case EEMSourceType::Solenoid:
		return FMath::IsNearlyZero(Source.SolenoidParams.Current);
	case EEMSourceType::MagneticDipole:
		return Source.MagneticDipoleParams.MagneticMoment.IsNearlyZero();
	case EEMSourceType::SectorMagnet:
		return FMath::IsNearlyZero(Source.SectorMagnetParams.FieldStrength);
	case EEMSourceType::PlateMagnet:
		return FMath::IsNearlyZero(Source.PlateMagnetParams.FieldStrength);
	case EEMSourceType::DielectricSphere:
		return FMath::IsNearlyEqual(Source.DielectricSphereParams.RelativePermittivity, 1.0f);
	case EEMSourceType::DielectricSlab:
		return FMath::IsNearlyEqual(Source.DielectricSlabParams.RelativePermittivity, 1.0f);
	case EEMSourceType::GroundedConductor:
	case EEMSourceType::GroundedPlate:
		return false;
	default:
		return FMath::IsNearlyZero(Source.PointChargeParams.Charge);
	}
}

int32 AEMFPhysicsProp::GetSourceEffectiveChargeSign(const FEMSourceDescription& Source)
{
	float EffectiveCharge = 0.0f;

	switch (Source.SourceType)
	{
	case EEMSourceType::PointCharge:
		EffectiveCharge = Source.PointChargeParams.Charge;
		break;
	case EEMSourceType::LineCharge:
		EffectiveCharge = Source.LineChargeParams.LinearChargeDensity;
		break;
	case EEMSourceType::ChargedRing:
		EffectiveCharge = Source.RingParams.TotalCharge;
		break;
	case EEMSourceType::ChargedSphere:
		EffectiveCharge = Source.SphereParams.TotalCharge;
		break;
	case EEMSourceType::ChargedBall:
		EffectiveCharge = Source.BallParams.TotalCharge;
		break;
	case EEMSourceType::InfinitePlate:
	case EEMSourceType::FinitePlate:
		EffectiveCharge = Source.PlateParams.SurfaceChargeDensity;
		break;
	default:
		// Magnetic sources, dielectrics, grounded conductors — no charge sign concept
		return 0;
	}

	if (EffectiveCharge > KINDA_SMALL_NUMBER) return 1;
	if (EffectiveCharge < -KINDA_SMALL_NUMBER) return -1;
	return 0;
}
