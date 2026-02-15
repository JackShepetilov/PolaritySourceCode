// EMFVelocityModifier.cpp

#include "EMFVelocityModifier.h"
#include "ApexMovementComponent.h"
#include "EMFChannelingPlateActor.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "GameFramework/Character.h"
#include "DrawDebugHelpers.h"

// EMF Plugin includes
#include "EMF_FieldComponent.h"
#include "EMF_PluginBPLibrary.h"

UEMFVelocityModifier::UEMFVelocityModifier()
{
	PrimaryComponentTick.bCanEverTick = true; // Needed for bonus charge decay
}

void UEMFVelocityModifier::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// Find UEMF_FieldComponent on the same actor
	FieldComponent = Owner->FindComponentByClass<UEMF_FieldComponent>();
	if (FieldComponent)
	{
		PreviousCharge = GetCharge();
		// Initialize charge from BaseCharge
		UpdateFieldComponentCharge();
	}

	// Find and register with MovementComponent
	if (ACharacter* Character = Cast<ACharacter>(Owner))
	{
		MovementComponent = Cast<UApexMovementComponent>(Character->GetCharacterMovement());

		if (MovementComponent)
		{
			MovementComponent->RegisterVelocityModifier(this);
		}
	}

	// Subscribe to overlap events
	Owner->OnActorBeginOverlap.AddDynamic(this, &UEMFVelocityModifier::OnOwnerBeginOverlap);
}

void UEMFVelocityModifier::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (MovementComponent)
	{
		MovementComponent->UnregisterVelocityModifier(this);
	}

	Super::EndPlay(EndPlayReason);
}

// ==================== IVelocityModifier Interface ====================

bool UEMFVelocityModifier::ModifyVelocity_Implementation(float DeltaTime, const FVector& CurrentVelocity, FVector& OutVelocityDelta)
{
	if (!bEnabled)
	{
		OutVelocityDelta = FVector::ZeroVector;
		CurrentEMForce = FVector::ZeroVector;
		CurrentAcceleration = FVector::ZeroVector;
		return false;
	}

	if (!FieldComponent)
	{
		OutVelocityDelta = FVector::ZeroVector;
		CurrentEMForce = FVector::ZeroVector;
		CurrentAcceleration = FVector::ZeroVector;
		return false;
	}

	// In proxy mode, skip charge-zero check (player's own charge may be unregistered)
	if (!bChannelingProxyMode)
	{
		float Charge = GetCharge();
		if (FMath::IsNearlyZero(Charge))
		{
			OutVelocityDelta = FVector::ZeroVector;
			CurrentEMForce = FVector::ZeroVector;
			CurrentAcceleration = FVector::ZeroVector;
			return false;
		}
	}

	// Calculate velocity delta using data from FieldComponent
	OutVelocityDelta = ComputeVelocityDelta(DeltaTime, CurrentVelocity);

	// Add pending impulses
	OutVelocityDelta += PendingImpulse;
	PendingImpulse = FVector::ZeroVector;

	// Check for charge changes
	CheckChargeChanged();

	return !OutVelocityDelta.IsNearlyZero();
}

float UEMFVelocityModifier::GetAccelerationMultiplier_Implementation()
{
	return 1.0f;
}

FVector UEMFVelocityModifier::GetExternalForce_Implementation()
{
	return CurrentEMForce;
}

// ==================== Public Interface ====================

float UEMFVelocityModifier::GetCharge() const
{
	if (FieldComponent)
	{
		return FieldComponent->GetSourceDescription().PointChargeParams.Charge;
	}
	return 0.0f;
}

void UEMFVelocityModifier::SetCharge(float NewCharge)
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = NewCharge;
		FieldComponent->SetSourceDescription(Desc);
		CheckChargeChanged();
	}
}

float UEMFVelocityModifier::GetMass() const
{
	if (FieldComponent)
	{
		return FieldComponent->GetSourceDescription().PhysicsParams.Mass;
	}
	return 70.0f; // Default
}

void UEMFVelocityModifier::SetMass(float NewMass)
{
	if (FieldComponent)
	{
		FieldComponent->SetMass(NewMass);
	}
}

void UEMFVelocityModifier::SetEnabled(bool bNewEnabled)
{
	bEnabled = bNewEnabled;

	if (!bEnabled)
	{
		CurrentEMForce = FVector::ZeroVector;
		CurrentAcceleration = FVector::ZeroVector;
		bHasPreviousPlatePosition = false;
	}
}

float UEMFVelocityModifier::GetChargeMassRatio() const
{
	float Mass = GetMass();
	return GetCharge() / FMath::Max(Mass, 0.001f);
}

void UEMFVelocityModifier::AddEMImpulse(FVector Impulse)
{
	PendingImpulse += Impulse;
}

void UEMFVelocityModifier::ToggleChargeSign()
{
	if (FMath::IsNearlyZero(BaseCharge))
	{
		BaseCharge = 10.0f;
	}
	else
	{
		BaseCharge = -BaseCharge;
	}

	UpdateFieldComponentCharge();
}

int32 UEMFVelocityModifier::GetChargeSign() const
{
	float Charge = GetCharge();
	if (FMath::IsNearlyZero(Charge))
	{
		return 0;
	}
	return (Charge > 0.0f) ? 1 : -1;
}

void UEMFVelocityModifier::NeutralizeCharge()
{
	SetCharge(0.0f);
	LastNeutralizationTime = GetWorld()->GetTimeSeconds();
}

void UEMFVelocityModifier::SetOwnerType(EEMSourceOwnerType NewOwnerType)
{
	if (FieldComponent)
	{
		FieldComponent->SetOwnerType(NewOwnerType);
	}
}

EEMSourceOwnerType UEMFVelocityModifier::GetOwnerType() const
{
	if (FieldComponent)
	{
		return FieldComponent->GetOwnerType();
	}
	return EEMSourceOwnerType::None;
}

// ==================== Private ====================

FVector UEMFVelocityModifier::ComputeVelocityDelta(float DeltaTime, const FVector& CurrentVelocity)
{
	if (!FieldComponent)
	{
		return FVector::ZeroVector;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector::ZeroVector;
	}

	// Channeling proxy mode: compute forces at plate position from Environment sources
	if (bChannelingProxyMode && ProxyPlateActor.IsValid())
	{
		return ComputeProxyVelocityDelta(DeltaTime, CurrentVelocity);
	}

	// Get all other sources (excluding self)
	TArray<FEMSourceDescription> OtherSources = FieldComponent->GetAllOtherSources();

	if (OtherSources.Num() == 0)
	{
		// No other sources - no force
		CurrentEMForce = FVector::ZeroVector;
		CurrentAcceleration = FVector::ZeroVector;
		return FVector::ZeroVector;
	}

	FVector Position = Owner->GetActorLocation();
	float Charge = GetCharge();
	float Mass = GetMass();

	// Pre-calculate squared max distance for faster comparison
	const float MaxDistSq = MaxSourceDistance * MaxSourceDistance;

	// Calculate Lorentz force from each source individually with multipliers
	FVector TotalForce = FVector::ZeroVector;
	FVector PlateForce = FVector::ZeroVector; // Separated for viscous capture suppression

	// Viscous capture: resolve plate position from direct reference (not registry search)
	FVector NearestPlatePosition = FVector::ZeroVector;
	float NearestPlateDistSq = MAX_FLT;
	bool bFoundPlate = false;
	AEMFChannelingPlateActor* CaptPlate = nullptr;

	if (bEnableViscousCapture && CapturingPlate.IsValid())
	{
		CaptPlate = CapturingPlate.Get();
		NearestPlatePosition = CaptPlate->GetActorLocation();
		NearestPlateDistSq = FVector::DistSquared(Position, NearestPlatePosition);
		bFoundPlate = true;
	}

	for (const FEMSourceDescription& Source : OtherSources)
	{
		// OPTIMIZATION: Skip sources with zero charge/current/field - they produce no force
		if (IsSourceEffectivelyZero(Source))
		{
			continue;
		}

		// OPTIMIZATION: Distance culling - skip sources too far away
		float DistSq = FVector::DistSquared(Position, Source.Position);
		if (DistSq > MaxDistSq)
		{
			continue;
		}

		// Identify plate sources (player-owned finite plates from channeling)
		const bool bIsChannelingPlate =
			Source.SourceType == EEMSourceType::FinitePlate &&
			Source.OwnerType == EEMSourceOwnerType::Player;

		// Non-captured NPCs with viscous capture enabled: SKIP plate forces entirely.
		// Only the captured NPC should feel the plate.
		if (bIsChannelingPlate && bEnableViscousCapture && !bFoundPlate)
		{
			continue;
		}

		bool bIsPlateSource = bFoundPlate && bIsChannelingPlate;

		// Get multiplier for this source's owner type
		float Multiplier = GetForceMultiplierForOwnerType(Source.OwnerType);

		// Skip sources with zero multiplier for optimization
		if (FMath::IsNearlyZero(Multiplier))
		{
			continue;
		}

		// Calculate force from this single source
		TArray<FEMSourceDescription> SingleSource;
		SingleSource.Add(Source);

		FVector SourceForce = UEMF_PluginBPLibrary::CalculateLorentzForceComplete(
			Charge,
			Position,
			CurrentVelocity,
			SingleSource,
			true  // Include magnetic component
		);

		SourceForce *= Multiplier;

		// Separate plate forces for later suppression
		if (bIsPlateSource)
		{
			PlateForce += SourceForce;
		}
		else
		{
			TotalForce += SourceForce;
		}
	}

	// ===== Viscous Capture: suppress ALL EM forces + add viscosity =====
	float CaptureStrength = 0.0f; // 0 = no capture, 1 = full capture

	if (bFoundPlate)
	{
		const float Distance = FMath::Sqrt(NearestPlateDistSq);

		if (Distance < CaptureRadius)
		{
			// Smoothstep falloff: 1 at plate → 0 at CaptureRadius edge
			const float T = Distance / CaptureRadius;
			CaptureStrength = 1.0f - T * T * (3.0f - 2.0f * T);
		}

		// Auto-release: if CaptureStrength too weak for too long, release NPC
		if (CaptureStrength < CaptureMinStrength)
		{
			WeakCaptureTimer += DeltaTime;
			if (WeakCaptureTimer >= CaptureReleaseTimeout)
			{
				ReleasedFromCapture();
				// Continue with normal (non-captured) force calculation
				bFoundPlate = false;
				CaptPlate = nullptr;
				CaptureStrength = 0.0f;
			}
		}
		else
		{
			WeakCaptureTimer = 0.0f;
		}

		PreviousPlatePosition = NearestPlatePosition;
		bHasPreviousPlatePosition = true;
	}
	else if (bEnableViscousCapture)
	{
		bHasPreviousPlatePosition = false;
	}

	// Suppress forces based on capture mode:
	// Normal capture: suppress ALL forces (plate + others) to hold NPC in place.
	// Reverse mode: plate force passes through fully (for launch), suppress only others.
	const bool bReverse = CaptPlate && CaptPlate->IsInReverseMode();
	const float PassThrough = (1.0f - CaptureStrength) * (1.0f - CaptureStrength);

	if (bReverse)
	{
		// Reverse mode: suppress non-plate forces.
		// Redirect full plate force magnitude along plate normal (camera forward)
		// so NPC always launches straight away from player regardless of EM field shape.
		TotalForce *= PassThrough;

		const FVector PlateNormal = CaptPlate->GetPlateNormal();
		TotalForce += PlateNormal * PlateForce.Size();
	}
	else
	{
		// Suppress everything (normal capture hold)
		TotalForce += PlateForce;
		TotalForce *= PassThrough;
	}

	CurrentEMForce = TotalForce;

	// Clamp maximum force
	if (CurrentEMForce.SizeSquared() > MaxForce * MaxForce)
	{
		CurrentEMForce = CurrentEMForce.GetSafeNormal() * MaxForce;
	}

	// a = F / m
	CurrentAcceleration = CurrentEMForce / FMath::Max(Mass, 0.001f);

	// Euler: delta_v = a * dt
	FVector VelocityDelta = CurrentAcceleration * DeltaTime;

	// ===== Viscous Capture: damping + gravity compensation =====
	if (CaptureStrength > 0.0f && CaptPlate)
	{
		// Plate velocity via finite difference
		FVector PlateVelocity = FVector::ZeroVector;
		if (bHasPreviousPlatePosition && DeltaTime > SMALL_NUMBER)
		{
			PlateVelocity = (NearestPlatePosition - PreviousPlatePosition) / DeltaTime;
		}

		// Relative velocity: NPC movement relative to plate
		const FVector RelativeVelocity = CurrentVelocity - PlateVelocity;

		// Exponential decay damping (unconditionally stable)
		const float DampingFactor = 1.0f - FMath::Exp(-ViscosityCoefficient * CaptureStrength * DeltaTime);

		FVector ViscousDelta;
		if (CaptPlate->IsInReverseMode())
		{
			// Reverse mode: damp only tangential velocity, preserve normal (launch direction)
			const FVector PlateNormal = CaptPlate->GetPlateNormal();
			const float NormalSpeed = FVector::DotProduct(RelativeVelocity, PlateNormal);
			const FVector V_tangential = RelativeVelocity - NormalSpeed * PlateNormal;
			ViscousDelta = -V_tangential * DampingFactor;
			// No gravity compensation in reverse mode — NPC launches freely
		}
		else
		{
			// Normal capture: damp all relative velocity
			ViscousDelta = -RelativeVelocity * DampingFactor;

			// Gravity counteraction (scaled by capture strength)
			if (bCounterGravityWhenCaptured)
			{
				const float WorldGravityZ = GetWorld()->GetGravityZ(); // negative
				VelocityDelta.Z += -WorldGravityZ * GravityCounterStrength * CaptureStrength * DeltaTime;
			}
		}

		VelocityDelta += ViscousDelta;
	}

	// Debug
	if (bDrawDebug)
	{
		DrawDebugForces(Position, CurrentEMForce);
	}

	return VelocityDelta;
}

float UEMFVelocityModifier::GetForceMultiplierForOwnerType(EEMSourceOwnerType OwnerType) const
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
	case EEMSourceOwnerType::None:
	default:
		return UnknownForceMultiplier;
	}
}

bool UEMFVelocityModifier::CanBeNeutralized() const
{
	if (!bCanNeutralizeOnContact || FMath::IsNearlyZero(GetCharge()))
	{
		return false;
	}

	float CurrentTime = GetWorld()->GetTimeSeconds();
	return (CurrentTime - LastNeutralizationTime) >= NeutralizationCooldown;
}

void UEMFVelocityModifier::CheckChargeChanged()
{
	float CurrentCharge = GetCharge();
	if (!FMath::IsNearlyEqual(CurrentCharge, PreviousCharge, 0.001f))
	{
		OnChargeChanged.Broadcast(CurrentCharge);
		PreviousCharge = CurrentCharge;
	}
}

void UEMFVelocityModifier::OnOwnerBeginOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	if (!OtherActor || OtherActor == GetOwner() || !bCanNeutralizeOnContact)
	{
		return;
	}

	// Check if other actor has UEMF_FieldComponent
	UEMF_FieldComponent* OtherFieldComp = OtherActor->FindComponentByClass<UEMF_FieldComponent>();
	if (!OtherFieldComp)
	{
		return;
	}

	float MyCharge = GetCharge();
	float OtherCharge = OtherFieldComp->GetSourceDescription().PointChargeParams.Charge;

	// Check: opposite signs?
	bool bOppositeSign = (MyCharge * OtherCharge) < 0.0f;

	if (bOppositeSign && FMath::Abs(OtherCharge) >= MinChargeToNeutralize)
	{
		float PrevCharge = MyCharge;

		// Neutralize self if possible
		if (CanBeNeutralized())
		{
			NeutralizeCharge();
			OnChargeNeutralized.Broadcast(OtherActor, PrevCharge);
		}

		// Neutralize target only if bNeutralizeTargetOnly = false
		if (!bNeutralizeTargetOnly)
		{
			UEMFVelocityModifier* OtherModifier = OtherActor->FindComponentByClass<UEMFVelocityModifier>();
			if (OtherModifier)
			{
				if (OtherModifier->CanBeNeutralized())
				{
					OtherModifier->NeutralizeCharge();
					OtherModifier->OnChargeNeutralized.Broadcast(GetOwner(), OtherCharge);
				}
			}
			else
			{
				// If no modifier - just zero the charge on component
				OtherFieldComp->SetCharge(0.0f);
			}
		}
	}
}

bool UEMFVelocityModifier::IsSourceEffectivelyZero(const FEMSourceDescription& Source)
{
	// Check based on source type - different types store "strength" differently
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

	// Passive sources (dielectrics, grounded conductors) - they modify fields, not create them
	// But they still need external sources to work, so skip them if no permittivity effect
	case EEMSourceType::DielectricSphere:
		return FMath::IsNearlyEqual(Source.DielectricSphereParams.RelativePermittivity, 1.0f);

	case EEMSourceType::DielectricSlab:
		return FMath::IsNearlyEqual(Source.DielectricSlabParams.RelativePermittivity, 1.0f);

	case EEMSourceType::GroundedConductor:
	case EEMSourceType::GroundedPlate:
		// Grounded conductors always affect fields if present
		return false;

	case EEMSourceType::Antenna:
	case EEMSourceType::WaveGuide:
	case EEMSourceType::Custom:
	default:
		// For unknown/custom types, check legacy Charge field as fallback
		return FMath::IsNearlyZero(Source.PointChargeParams.Charge);
	}
}

// ==================== Viscous Capture API ====================

void UEMFVelocityModifier::SetCapturedByPlate(AEMFChannelingPlateActor* Plate)
{
	if (!Plate)
	{
		return;
	}

	CapturingPlate = Plate;
	bHasPreviousPlatePosition = false;
	WeakCaptureTimer = 0.0f;

	// Enter captured state on the NPC
	if (AShooterNPC* NPC = Cast<AShooterNPC>(GetOwner()))
	{
		NPC->EnterCapturedState();
	}
}

void UEMFVelocityModifier::ReleasedFromCapture()
{
	CapturingPlate = nullptr;
	bHasPreviousPlatePosition = false;

	// Exit captured state on the NPC
	if (AShooterNPC* NPC = Cast<AShooterNPC>(GetOwner()))
	{
		NPC->ExitCapturedState();
	}
}

void UEMFVelocityModifier::DetachFromPlate()
{
	CapturingPlate = nullptr;
	bHasPreviousPlatePosition = false;
	// NOTE: Does NOT call ExitCapturedState — NPC stays in knockback
	// for plate swap during ExitChanneling → ReverseChanneling transition
}

// ==================== Channeling Proxy Mode ====================

void UEMFVelocityModifier::SetChannelingProxyMode(bool bEnable, AEMFChannelingPlateActor* PlateActor)
{
	bChannelingProxyMode = bEnable;
	ProxyPlateActor = PlateActor;

	if (!bEnable)
	{
		// Clear force state when exiting proxy mode
		CurrentEMForce = FVector::ZeroVector;
		CurrentAcceleration = FVector::ZeroVector;
	}
}

FVector UEMFVelocityModifier::ComputeProxyVelocityDelta(float DeltaTime, const FVector& CurrentVelocity)
{
	AEMFChannelingPlateActor* Plate = ProxyPlateActor.Get();
	if (!Plate || !Plate->PlateFieldComponent)
	{
		CurrentEMForce = FVector::ZeroVector;
		CurrentAcceleration = FVector::ZeroVector;
		return FVector::ZeroVector;
	}

	// Get all sources visible to the plate (excluding the plate actor itself)
	TArray<FEMSourceDescription> OtherSources = Plate->PlateFieldComponent->GetAllOtherSources();

	if (OtherSources.Num() == 0)
	{
		CurrentEMForce = FVector::ZeroVector;
		CurrentAcceleration = FVector::ZeroVector;
		return FVector::ZeroVector;
	}

	FVector PlatePosition = Plate->GetActorLocation();
	float PlateCharge = Plate->GetPlateChargeDensity();
	float Mass = GetMass();

	// Pre-calculate squared max distance
	const float MaxDistSq = MaxSourceDistance * MaxSourceDistance;

	FVector TotalForce = FVector::ZeroVector;

	for (const FEMSourceDescription& Source : OtherSources)
	{
		// Only interact with Environment sources for player movement
		if (Source.OwnerType != EEMSourceOwnerType::Environment)
		{
			continue;
		}

		if (IsSourceEffectivelyZero(Source))
		{
			continue;
		}

		float DistSq = FVector::DistSquared(PlatePosition, Source.Position);
		if (DistSq > MaxDistSq)
		{
			continue;
		}

		// Calculate force on the plate from this environment source
		TArray<FEMSourceDescription> SingleSource;
		SingleSource.Add(Source);

		FVector SourceForce = UEMF_PluginBPLibrary::CalculateLorentzForceComplete(
			PlateCharge,
			PlatePosition,
			CurrentVelocity, // Use player's velocity for magnetic component
			SingleSource,
			true
		);

		TotalForce += SourceForce * EnvironmentForceMultiplier;
	}

	CurrentEMForce = TotalForce;

	// Clamp maximum force
	if (CurrentEMForce.SizeSquared() > MaxForce * MaxForce)
	{
		CurrentEMForce = CurrentEMForce.GetSafeNormal() * MaxForce;
	}

	CurrentAcceleration = CurrentEMForce / FMath::Max(Mass, 0.001f);
	FVector VelocityDelta = CurrentAcceleration * DeltaTime;

	if (bDrawDebug)
	{
		// Draw force at plate position pointing toward player
		DrawDebugForces(PlatePosition, CurrentEMForce);
	}

	return VelocityDelta;
}

void UEMFVelocityModifier::DrawDebugForces(const FVector& Position, const FVector& Force) const
{
	UWorld* World = GetWorld();
	if (!World || !FieldComponent)
	{
		return;
	}

	// Force (red)
	if (!Force.IsNearlyZero())
	{
		DrawDebugDirectionalArrow(
			World,
			Position,
			Position + Force.GetSafeNormal() * FMath::Min(Force.Size(), 200.0f),
			10.0f,
			FColor::Red,
			false,
			-1.0f,
			0,
			2.0f
		);
	}

	// E-field (yellow)
	FVector ElectricField = FieldComponent->ElectricField;
	if (!ElectricField.IsNearlyZero())
	{
		DrawDebugDirectionalArrow(
			World,
			Position + FVector(0, 0, 20),
			Position + FVector(0, 0, 20) + ElectricField.GetSafeNormal() * 100.0f,
			8.0f,
			FColor::Yellow,
			false,
			-1.0f,
			0,
			1.5f
		);
	}

	// B-field (blue)
	FVector MagneticField = FieldComponent->MagneticField;
	if (!MagneticField.IsNearlyZero())
	{
		DrawDebugDirectionalArrow(
			World,
			Position + FVector(0, 0, 40),
			Position + FVector(0, 0, 40) + MagneticField.GetSafeNormal() * 100.0f,
			8.0f,
			FColor::Blue,
			false,
			-1.0f,
			0,
			1.5f
		);
	}
}

// ==================== Charge Accumulation System ====================

void UEMFVelocityModifier::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Decay bonus charge over time
	if (CurrentBonusCharge > 0.0f)
	{
		CurrentBonusCharge = FMath::Max(0.0f, CurrentBonusCharge - BonusChargeDecayRate * DeltaTime);
		UpdateFieldComponentCharge();
	}
}

void UEMFVelocityModifier::AddBonusCharge(float Amount)
{
	if (Amount <= 0.0f)
	{
		return;
	}

	CurrentBonusCharge = FMath::Min(CurrentBonusCharge + Amount, MaxBonusCharge);
	UpdateFieldComponentCharge();
}

void UEMFVelocityModifier::AddPermanentCharge(float Amount)
{
	if (Amount == 0.0f)
	{
		return;
	}

	// Keep the sign of the charge
	float Sign = (BaseCharge >= 0.0f) ? 1.0f : -1.0f;
	float CurrentModule = FMath::Abs(BaseCharge);

	// Positive Amount increases module, negative decreases
	float NewModule = CurrentModule + Amount;

	// Clamp module: min 0, max MaxBaseCharge
	NewModule = FMath::Clamp(NewModule, 0.0f, MaxBaseCharge);

	// Restore sign
	BaseCharge = Sign * NewModule;
	UpdateFieldComponentCharge();
}

void UEMFVelocityModifier::SetBaseCharge(float NewBaseCharge)
{
	// Clamp by module, keeping sign
	float Sign = (NewBaseCharge >= 0.0f) ? 1.0f : -1.0f;
	float Module = FMath::Min(FMath::Abs(NewBaseCharge), MaxBaseCharge);
	BaseCharge = Sign * Module;
	UpdateFieldComponentCharge();
}

void UEMFVelocityModifier::DeductCharge(float Amount)
{
	if (Amount <= 0.0f)
	{
		return;
	}

	float RemainingToDeduct = Amount;

	// First deduct from bonus charge
	if (CurrentBonusCharge > 0.0f)
	{
		float DeductFromBonus = FMath::Min(CurrentBonusCharge, RemainingToDeduct);
		CurrentBonusCharge -= DeductFromBonus;
		RemainingToDeduct -= DeductFromBonus;
	}

	// If remaining - deduct from base charge
	if (RemainingToDeduct > 0.0f)
	{
		AddPermanentCharge(-RemainingToDeduct);
	}
	else
	{
		// Update FieldComponent even if only deducted from bonus
		UpdateFieldComponentCharge();
	}
}

float UEMFVelocityModifier::GetTotalCharge() const
{
	// Sign is determined by BaseCharge, bonus is added by module
	float Sign = (BaseCharge >= 0.0f) ? 1.0f : -1.0f;
	return BaseCharge + Sign * CurrentBonusCharge;
}

void UEMFVelocityModifier::UpdateFieldComponentCharge()
{
	if (FieldComponent)
	{
		float TotalCharge = GetTotalCharge();

		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = TotalCharge;
		FieldComponent->SetSourceDescription(Desc);

		CheckChargeChanged();
	}
}
