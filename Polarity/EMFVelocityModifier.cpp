// EMFVelocityModifier.cpp

#include "EMFVelocityModifier.h"
#include "ApexMovementComponent.h"
#include "EMFChannelingPlateActor.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "GameFramework/Character.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"

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

	// Sync ChargeSign from serialized BaseCharge (for NPCs with negative charge in Blueprint)
	if (!FMath::IsNearlyZero(BaseCharge))
	{
		ChargeSign = (BaseCharge > 0.0f) ? 1 : -1;
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
	// Sync ChargeSign and BaseCharge from the signed value
	if (!FMath::IsNearlyZero(NewCharge))
	{
		ChargeSign = (NewCharge > 0.0f) ? 1 : -1;
	}
	BaseCharge = NewCharge;

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
	ChargeSign = -ChargeSign;
	BaseCharge = -BaseCharge;
	UpdateFieldComponentCharge();
}

int32 UEMFVelocityModifier::GetChargeSign() const
{
	return ChargeSign;
}

void UEMFVelocityModifier::NeutralizeCharge()
{
	BaseCharge = 0.0f;
	// ChargeSign preserved — polarity remembered even at zero charge
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = 0.0f;
		FieldComponent->SetSourceDescription(Desc);
		CheckChargeChanged();
	}
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

	// Debug: always-visible capture range sphere around this NPC
	if (bDrawDebug && bEnableViscousCapture)
	{
		// Get player charge: from plate if captured, from player pawn otherwise
		float DebugPlayerCharge = 0.0f;
		if (CapturingPlate.IsValid())
		{
			DebugPlayerCharge = CapturingPlate.Get()->GetPlateChargeDensity();
		}
		else if (ACharacter* PlayerChar = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0))
		{
			if (UEMFVelocityModifier* PlayerMod = PlayerChar->FindComponentByClass<UEMFVelocityModifier>())
			{
				DebugPlayerCharge = PlayerMod->GetCharge();
			}
		}

		const float DebugChargeProduct = FMath::Abs(DebugPlayerCharge) * FMath::Abs(Charge);
		const float DebugRatio = DebugChargeProduct / FMath::Max(CaptureChargeNormCoeff, 0.01f);
		const float DebugRangeMultiplier = FMath::Max(1.0f, 1.0f + FMath::Loge(FMath::Max(DebugRatio, KINDA_SMALL_NUMBER)));
		const float DebugCaptureRange = CaptureBaseRange * DebugRangeMultiplier;

		DrawDebugSphere(GetWorld(), Position, DebugCaptureRange, 32, FColor::Cyan, false, -1.0f, 0, 1.5f);
	}

	// Pre-calculate squared distances for faster comparison
	const float MaxDistSq = MaxSourceDistance * MaxSourceDistance;
	const float OppositeChargeMinDistSq = bEnableOppositeChargeDistanceCutoff
		? OppositeChargeMinDistance * OppositeChargeMinDistance : 0.0f;

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

		// Opposite-charge distance cutoff: skip sources too close with opposite charge
		// Prevents extreme forces from Coulomb 1/r² singularity
		if (bEnableOppositeChargeDistanceCutoff && DistSq < OppositeChargeMinDistSq)
		{
			const int32 SourceChargeSign = GetSourceEffectiveChargeSign(Source);
			const int32 MyChargeSign = (Charge > KINDA_SMALL_NUMBER) ? 1 : ((Charge < -KINDA_SMALL_NUMBER) ? -1 : 0);
			if (SourceChargeSign != 0 && MyChargeSign != 0 && SourceChargeSign != MyChargeSign)
			{
				continue;
			}
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

	// ===== Hard Hold Capture: suppress EM forces + rigid hold =====
	if (bFoundPlate)
	{
		const float Distance = FMath::Sqrt(NearestPlateDistSq);
		const float EffectiveRange = CalculateCaptureRange();

		// Auto-release: if NPC outside effective capture range for too long
		if (Distance > EffectiveRange)
		{
			WeakCaptureTimer += DeltaTime;
			if (WeakCaptureTimer >= CaptureReleaseTimeout)
			{
				ReleasedFromCapture();
				bFoundPlate = false;
				CaptPlate = nullptr;
			}
		}
		else
		{
			WeakCaptureTimer = 0.0f;
		}
	}

	// Suppress ALL EM forces when captured (hard hold manages position directly)
	const bool bReverse = CaptPlate && CaptPlate->IsInReverseMode();

	if (bFoundPlate && !bReverse)
	{
		// Normal capture: suppress everything — hard hold handles positioning
		TotalForce = FVector::ZeroVector;
		PlateForce = FVector::ZeroVector;
	}
	else if (bReverse)
	{
		// Reverse mode: redirect plate force along plate normal (camera forward)
		// Other forces apply normally with launched multipliers
		const FVector PlateNormal = CaptPlate->GetPlateNormal();
		TotalForce += PlateNormal * PlateForce.Size();
	}
	else
	{
		TotalForce += PlateForce;
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

	// ===== Hard Hold: pull-in or rigid position lock =====
	if (bFoundPlate && CaptPlate)
	{
		VelocityDelta = ComputeHardHoldDelta(DeltaTime, CurrentVelocity, CaptPlate);
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
	if (bUseLaunchedForceFiltering)
	{
		switch (OwnerType)
		{
		case EEMSourceOwnerType::Player:
			return LaunchedPlayerForceMultiplier;
		case EEMSourceOwnerType::NPC:
			return LaunchedNPCForceMultiplier;
		case EEMSourceOwnerType::Projectile:
			return LaunchedProjectileForceMultiplier;
		case EEMSourceOwnerType::Environment:
			return LaunchedEnvironmentForceMultiplier;
		case EEMSourceOwnerType::PhysicsProp:
			return LaunchedPhysicsPropForceMultiplier;
		case EEMSourceOwnerType::None:
		default:
			return LaunchedUnknownForceMultiplier;
		}
	}

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

int32 UEMFVelocityModifier::GetSourceEffectiveChargeSign(const FEMSourceDescription& Source)
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

// ==================== Capture: Hard Hold ====================

float UEMFVelocityModifier::CalculateCaptureRange() const
{
	// Get player charge from the plate's charge density (plate mirrors player charge)
	float PlayerCharge = 0.0f;
	if (CapturingPlate.IsValid())
	{
		PlayerCharge = CapturingPlate.Get()->GetPlateChargeDensity();
	}

	const float NPCCharge = FMath::Abs(GetCharge());
	const float PlayerChargeAbs = FMath::Abs(PlayerCharge);

	// Product of charges: higher product = longer range
	const float ChargeProduct = PlayerChargeAbs * NPCCharge;

	// Range = BaseRange * max(1, 1 + ln(ChargeProduct / NormCoeff))
	// At ChargeProduct == NormCoeff: Range = BaseRange (ln(1) = 0, so 1+0 = 1)
	// At ChargeProduct == NormCoeff * e: Range = BaseRange * 2
	// At ChargeProduct < NormCoeff: ln < 0, but clamped to 1 so range never below BaseRange
	const float Ratio = ChargeProduct / FMath::Max(CaptureChargeNormCoeff, 0.01f);
	const float RangeMultiplier = FMath::Max(1.0f, 1.0f + FMath::Loge(FMath::Max(Ratio, KINDA_SMALL_NUMBER)));

	return CaptureBaseRange * RangeMultiplier;
}

float UEMFVelocityModifier::GetEffectiveCaptureRange() const
{
	return CalculateCaptureRange();
}

FVector UEMFVelocityModifier::ComputeHardHoldDelta(float DeltaTime, const FVector& CurrentVelocity, AEMFChannelingPlateActor* Plate)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Plate)
	{
		return FVector::ZeroVector;
	}

	const FVector Position = Owner->GetActorLocation();
	const FVector PlatePos = Plate->GetActorLocation();

	if (Plate->IsInReverseMode())
	{
		// === REVERSE MODE: launch NPC along camera forward ===
		// Exit hard hold — NPC is free to be pushed by plate EM force
		bHardHoldActive = false;

		// Plate force is already in VelocityDelta from EM calculation above.
		// We only need to damp tangential velocity so NPC flies straight.
		const FVector PlateNormal = Plate->GetPlateNormal();
		const float NormalSpeed = FVector::DotProduct(CurrentVelocity, PlateNormal);
		const FVector Tangential = CurrentVelocity - NormalSpeed * PlateNormal;

		// Strong tangential damping to keep NPC on the line of fire
		const float TangentialDampFactor = 1.0f - FMath::Exp(-10.0f * DeltaTime);
		FVector ReverseDelta = -Tangential * TangentialDampFactor;

		// Add the EM-driven acceleration (computed earlier in ComputeVelocityDelta)
		ReverseDelta += CurrentAcceleration * DeltaTime;

		return ReverseDelta;
	}

	// === NORMAL CAPTURE MODE ===
	const FVector ToPlate = PlatePos - Position;
	const float Distance = ToPlate.Size();

	if (bHardHoldActive || Distance <= CaptureSnapDistance)
	{
		// --- HARD HOLD: lock NPC to plate position ---
		bHardHoldActive = true;

		// Directly teleport NPC to plate position
		// Cancel all current velocity and return delta that places NPC at plate
		const FVector DesiredDelta = ToPlate; // Move exactly to plate
		const FVector VelocityToCancel = -CurrentVelocity; // Zero out movement velocity

		// Use SetActorLocation for instant placement (bypasses movement interpolation)
		Owner->SetActorLocation(PlatePos);

		// Return negative velocity to zero out the movement component's velocity
		return VelocityToCancel;
	}
	else
	{
		// --- PULL-IN PHASE: smoothly move NPC toward plate ---
		const float PullSpeed = CaptureBaseSpeed;
		const FVector Direction = ToPlate.GetSafeNormal();

		// VInterpTo-style: move toward plate at PullSpeed, don't overshoot
		const float MoveDistance = FMath::Min(PullSpeed * DeltaTime, Distance);
		const FVector DesiredPosition = Position + Direction * MoveDistance;

		// Cancel current velocity and replace with pull velocity
		const FVector PullVelocity = Direction * PullSpeed;
		return PullVelocity - CurrentVelocity;
	}
}

// ==================== Capture API ====================

void UEMFVelocityModifier::SetCapturedByPlate(AEMFChannelingPlateActor* Plate)
{
	if (!Plate)
	{
		return;
	}

	CapturingPlate = Plate;
	bHardHoldActive = false;
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
	bHardHoldActive = false;

	// Exit captured state on the NPC
	if (AShooterNPC* NPC = Cast<AShooterNPC>(GetOwner()))
	{
		NPC->ExitCapturedState();
	}
}

void UEMFVelocityModifier::DetachFromPlate()
{
	CapturingPlate = nullptr;
	bHardHoldActive = false;
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
	// Unified pool — routes to AddPermanentCharge
	AddPermanentCharge(Amount);
}

void UEMFVelocityModifier::AddPermanentCharge(float Amount)
{
	if (Amount == 0.0f)
	{
		return;
	}

	float CurrentModule = FMath::Abs(BaseCharge);
	float NewModule = FMath::Clamp(CurrentModule + Amount, 0.0f, MaxBaseCharge);
	BaseCharge = static_cast<float>(ChargeSign) * NewModule;
	UpdateFieldComponentCharge();
}

void UEMFVelocityModifier::SetBaseCharge(float NewBaseCharge)
{
	if (!FMath::IsNearlyZero(NewBaseCharge))
	{
		ChargeSign = (NewBaseCharge >= 0.0f) ? 1 : -1;
	}
	float Module = FMath::Min(FMath::Abs(NewBaseCharge), MaxBaseCharge);
	BaseCharge = static_cast<float>(ChargeSign) * Module;
	UpdateFieldComponentCharge();
}

void UEMFVelocityModifier::DeductCharge(float Amount)
{
	if (Amount <= 0.0f)
	{
		return;
	}

	float Module = FMath::Abs(BaseCharge);
	Module = FMath::Max(0.0f, Module - Amount);
	BaseCharge = static_cast<float>(ChargeSign) * Module;
	UpdateFieldComponentCharge();
}

float UEMFVelocityModifier::GetTotalCharge() const
{
	return BaseCharge;
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
