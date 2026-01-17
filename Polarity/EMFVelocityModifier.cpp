// EMFVelocityModifier.cpp

#include "EMFVelocityModifier.h"
#include "ApexMovementComponent.h"
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
		UE_LOG(LogTemp, Error, TEXT("EMFVelocityModifier: No Owner!"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("EMFVelocityModifier: BeginPlay on %s"), *Owner->GetName());

	// ÃÂÃÂ°ÃÂ¹Ã‘â€šÃÂ¸ UEMF_FieldComponent ÃÂ½ÃÂ° Ã‘â€šÃÂ¾ÃÂ¼ ÃÂ¶ÃÂµ ÃÂ°ÃÂºÃ‘â€šÃÂ¾Ã‘â‚¬ÃÂµ
	FieldComponent = Owner->FindComponentByClass<UEMF_FieldComponent>();
	if (!FieldComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("EMFVelocityModifier: No UEMF_FieldComponent found on %s!"), *Owner->GetName());
	}
	else
	{
		PreviousCharge = GetCharge();
		// Initialize charge from BaseCharge
		UpdateFieldComponentCharge();
		UE_LOG(LogTemp, Warning, TEXT("EMFVelocityModifier: Found UEMF_FieldComponent, Charge=%.2f, Mass=%.2f"),
			GetCharge(), GetMass());
	}

	// ÃÂÃÂ°ÃÂ¹Ã‘â€šÃÂ¸ ÃÂ¸ ÃÂ·ÃÂ°Ã‘â‚¬ÃÂµÃÂ³ÃÂ¸Ã‘ÂÃ‘â€šÃ‘â‚¬ÃÂ¸Ã‘â‚¬ÃÂ¾ÃÂ²ÃÂ°Ã‘â€šÃ‘Å’Ã‘ÂÃ‘Â ÃÂ² MovementComponent
	if (ACharacter* Character = Cast<ACharacter>(Owner))
	{
		MovementComponent = Cast<UApexMovementComponent>(Character->GetCharacterMovement());

		if (MovementComponent)
		{
			MovementComponent->RegisterVelocityModifier(this);
			UE_LOG(LogTemp, Warning, TEXT("EMFVelocityModifier: Registered with ApexMovementComponent on %s"), *Owner->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("EMFVelocityModifier: %s has CharacterMovement but it's not ApexMovementComponent!"), *Owner->GetName());
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("EMFVelocityModifier: Owner %s is not a Character, skipping movement registration"), *Owner->GetName());
	}

	// ÃÅ¸ÃÂ¾ÃÂ´ÃÂ¿ÃÂ¸Ã‘ÂÃÂ°Ã‘â€šÃ‘Å’Ã‘ÂÃ‘Â ÃÂ½ÃÂ° overlap Ã‘ÂÃÂ¾ÃÂ±Ã‘â€¹Ã‘â€šÃÂ¸Ã‘Â ÃÂ²ÃÂ»ÃÂ°ÃÂ´ÃÂµÃÂ»Ã‘Å’Ã‘â€ ÃÂ°
	Owner->OnActorBeginOverlap.AddDynamic(this, &UEMFVelocityModifier::OnOwnerBeginOverlap);
	UE_LOG(LogTemp, Warning, TEXT("EMFVelocityModifier: Subscribed to OnActorBeginOverlap"));
}

void UEMFVelocityModifier::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UE_LOG(LogTemp, Warning, TEXT("EMFVelocityModifier: EndPlay on %s"), GetOwner() ? *GetOwner()->GetName() : TEXT("null"));

	if (MovementComponent)
	{
		MovementComponent->UnregisterVelocityModifier(this);
		UE_LOG(LogTemp, Warning, TEXT("EMFVelocityModifier: Unregistered from ApexMovementComponent"));
	}

	Super::EndPlay(EndPlayReason);
}

// ==================== IVelocityModifier Interface ====================

bool UEMFVelocityModifier::ModifyVelocity_Implementation(float DeltaTime, const FVector& CurrentVelocity, FVector& OutVelocityDelta)
{
	static bool bFirstCall = true;
	if (bFirstCall)
	{
		UE_LOG(LogTemp, Warning, TEXT("EMFVelocityModifier: ModifyVelocity called for first time!"));
		bFirstCall = false;
	}

	if (!bEnabled)
	{
		if (bLogForces) UE_LOG(LogTemp, Warning, TEXT("EMFVelocityModifier: Disabled"));
		OutVelocityDelta = FVector::ZeroVector;
		CurrentEMForce = FVector::ZeroVector;
		CurrentAcceleration = FVector::ZeroVector;
		return false;
	}

	if (!FieldComponent)
	{
		if (bLogForces) UE_LOG(LogTemp, Warning, TEXT("EMFVelocityModifier: No FieldComponent"));
		OutVelocityDelta = FVector::ZeroVector;
		CurrentEMForce = FVector::ZeroVector;
		CurrentAcceleration = FVector::ZeroVector;
		return false;
	}

	float Charge = GetCharge();
	if (FMath::IsNearlyZero(Charge))
	{
		OutVelocityDelta = FVector::ZeroVector;
		CurrentEMForce = FVector::ZeroVector;
		CurrentAcceleration = FVector::ZeroVector;
		return false;
	}

	// Ãâ€™Ã‘â€¹Ã‘â€¡ÃÂ¸Ã‘ÂÃÂ»ÃÂ¸Ã‘â€šÃ‘Å’ velocity delta ÃÂ¸Ã‘ÂÃÂ¿ÃÂ¾ÃÂ»Ã‘Å’ÃÂ·Ã‘Æ’Ã‘Â ÃÂ´ÃÂ°ÃÂ½ÃÂ½Ã‘â€¹ÃÂµ ÃÂ¸ÃÂ· FieldComponent
	OutVelocityDelta = ComputeVelocityDelta(DeltaTime, CurrentVelocity);

	// Ãâ€ÃÂ¾ÃÂ±ÃÂ°ÃÂ²ÃÂ¸Ã‘â€šÃ‘Å’ pending ÃÂ¸ÃÂ¼ÃÂ¿Ã‘Æ’ÃÂ»Ã‘Å’Ã‘ÂÃ‘â€¹
	OutVelocityDelta += PendingImpulse;
	PendingImpulse = FVector::ZeroVector;

	// ÃÅ¸Ã‘â‚¬ÃÂ¾ÃÂ²ÃÂµÃ‘â‚¬ÃÂ¸Ã‘â€šÃ‘Å’ ÃÂ¸ÃÂ·ÃÂ¼ÃÂµÃÂ½ÃÂµÃÂ½ÃÂ¸ÃÂµ ÃÂ·ÃÂ°Ã‘â‚¬Ã‘ÂÃÂ´ÃÂ°
	CheckChargeChanged();

	if (bLogForces && !CurrentEMForce.IsNearlyZero())
	{
		UE_LOG(LogTemp, Log, TEXT("EMF: Charge=%.2f Force=(%.2f, %.2f, %.2f) VelDelta=(%.2f, %.2f, %.2f)"),
			Charge,
			CurrentEMForce.X, CurrentEMForce.Y, CurrentEMForce.Z,
			OutVelocityDelta.X, OutVelocityDelta.Y, OutVelocityDelta.Z);
	}

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
		// Ð‘ÐµÑ€Ñ‘Ð¼ Ð·Ð°Ñ€ÑÐ´ Ð¸Ð· PointChargeParams Ð²Ð¼ÐµÑÑ‚Ð¾ Charge
		return FieldComponent->GetSourceDescription().PointChargeParams.Charge;
	}
	return 0.0f;
}

void UEMFVelocityModifier::SetCharge(float NewCharge)
{
	if (FieldComponent)
	{
		// Ð£ÑÑ‚Ð°Ð½Ð°Ð²Ð»Ð¸Ð²Ð°ÐµÐ¼ Ð·Ð°Ñ€ÑÐ´ Ð² PointChargeParams
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
		return FieldComponent->GetSourceDescription().Mass;
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
	// Ð˜Ð½Ð²ÐµÑ€Ñ‚Ð¸Ñ€ÑƒÐµÐ¼ Ð·Ð½Ð°Ðº Ð±Ð°Ð·Ð¾Ð²Ð¾Ð³Ð¾ Ð·Ð°Ñ€ÑÐ´Ð°
	if (FMath::IsNearlyZero(BaseCharge))
	{
		// Ð•ÑÐ»Ð¸ Ð±Ð°Ð·Ð¾Ð²Ñ‹Ð¹ Ð·Ð°Ñ€ÑÐ´ Ð½ÑƒÐ»ÐµÐ²Ð¾Ð¹ - ÑƒÑÑ‚Ð°Ð½Ð¾Ð²Ð¸Ñ‚ÑŒ Ð¿Ð¾Ð»Ð¾Ð¶Ð¸Ñ‚ÐµÐ»ÑŒÐ½Ñ‹Ð¹
		BaseCharge = 10.0f;
		UE_LOG(LogTemp, Warning, TEXT("EMF: BaseCharge was 0, set to 10.0"));
	}
	else
	{
		// Ð˜Ð½Ð°Ñ‡Ðµ Ð¸Ð½Ð²ÐµÑ€Ñ‚Ð¸Ñ€Ð¾Ð²Ð°Ñ‚ÑŒ Ð·Ð½Ð°Ðº
		BaseCharge = -BaseCharge;
	}
	
	UpdateFieldComponentCharge();
	
	if (bLogForces)
	{
		UE_LOG(LogTemp, Log, TEXT("EMF: Charge toggled. Base=%.2f, Bonus=%.2f, Total=%.2f"),
			BaseCharge, CurrentBonusCharge, GetTotalCharge());
	}
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
	float PrevCharge = GetCharge();
	SetCharge(0.0f);
	LastNeutralizationTime = GetWorld()->GetTimeSeconds();

	if (bLogForces)
	{
		UE_LOG(LogTemp, Log, TEXT("EMF: Charge neutralized (was %.2f)"), PrevCharge);
	}
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

	// ÃÅ¸ÃÂ¾ÃÂ»Ã‘Æ’Ã‘â€¡ÃÂ¸Ã‘â€šÃ‘Å’ ÃÂ²Ã‘ÂÃÂµ ÃÂ´Ã‘â‚¬Ã‘Æ’ÃÂ³ÃÂ¸ÃÂµ ÃÂ¸Ã‘ÂÃ‘â€šÃÂ¾Ã‘â€¡ÃÂ½ÃÂ¸ÃÂºÃÂ¸ (ÃÂ¸Ã‘ÂÃÂºÃÂ»Ã‘Å½Ã‘â€¡ÃÂ°Ã‘Â Ã‘ÂÃÂµÃÂ±Ã‘Â)
	TArray<FEMSourceDescription> OtherSources = FieldComponent->GetAllOtherSources();

	if (OtherSources.Num() == 0)
	{
		// ÃÂÃÂµÃ‘â€š ÃÂ´Ã‘â‚¬Ã‘Æ’ÃÂ³ÃÂ¸Ã‘â€¦ ÃÂ¸Ã‘ÂÃ‘â€šÃÂ¾Ã‘â€¡ÃÂ½ÃÂ¸ÃÂºÃÂ¾ÃÂ² - ÃÂ½ÃÂµÃ‘â€š Ã‘ÂÃÂ¸ÃÂ»Ã‘â€¹
		CurrentEMForce = FVector::ZeroVector;
		CurrentAcceleration = FVector::ZeroVector;
		return FVector::ZeroVector;
	}

	FVector Position = Owner->GetActorLocation();
	float Charge = GetCharge();
	float Mass = GetMass();

	// Calculate Lorentz force from each source individually with multipliers
	FVector TotalForce = FVector::ZeroVector;

	for (const FEMSourceDescription& Source : OtherSources)
	{
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

		// Apply multiplier and add to total
		TotalForce += SourceForce * Multiplier;

		if (bLogForces && !SourceForce.IsNearlyZero())
		{
			UE_LOG(LogTemp, Log, TEXT("EMF: Source OwnerType=%d, Multiplier=%.2f, SourceForce=(%.4f, %.4f, %.4f)"),
				static_cast<int32>(Source.OwnerType), Multiplier,
				SourceForce.X, SourceForce.Y, SourceForce.Z);
		}
	}

	CurrentEMForce = TotalForce;

	if (bLogForces && OtherSources.Num() > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("EMF: Sources=%d, Position=(%.0f, %.0f, %.0f), TotalForce=(%.4f, %.4f, %.4f)"),
			OtherSources.Num(), Position.X, Position.Y, Position.Z,
			CurrentEMForce.X, CurrentEMForce.Y, CurrentEMForce.Z);
	}

	// Clamp maximum force
	if (CurrentEMForce.SizeSquared() > MaxForce * MaxForce)
	{
		CurrentEMForce = CurrentEMForce.GetSafeNormal() * MaxForce;
	}

	// a = F / m
	CurrentAcceleration = CurrentEMForce / FMath::Max(Mass, 0.001f);

	// Euler: delta_v = a * dt
	FVector VelocityDelta = CurrentAcceleration * DeltaTime;

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
	UE_LOG(LogTemp, Warning, TEXT("EMFVelocityModifier: OnOwnerBeginOverlap - %s overlapped with %s"),
		OverlappedActor ? *OverlappedActor->GetName() : TEXT("null"),
		OtherActor ? *OtherActor->GetName() : TEXT("null"));

	if (!OtherActor || OtherActor == GetOwner() || !bCanNeutralizeOnContact)
	{
		UE_LOG(LogTemp, Warning, TEXT("EMFVelocityModifier: Overlap ignored (null=%d, self=%d, canNeutralize=%d)"),
			!OtherActor, OtherActor == GetOwner(), bCanNeutralizeOnContact);
		return;
	}

	// ÃÅ¸Ã‘â‚¬ÃÂ¾ÃÂ²ÃÂµÃ‘â‚¬ÃÂ¸Ã‘â€šÃ‘Å’, ÃÂµÃ‘ÂÃ‘â€šÃ‘Å’ ÃÂ»ÃÂ¸ Ã‘Æ’ ÃÂ´Ã‘â‚¬Ã‘Æ’ÃÂ³ÃÂ¾ÃÂ³ÃÂ¾ ÃÂ°ÃÂºÃ‘â€šÃÂ¾Ã‘â‚¬ÃÂ° UEMF_FieldComponent
	UEMF_FieldComponent* OtherFieldComp = OtherActor->FindComponentByClass<UEMF_FieldComponent>();
	if (!OtherFieldComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("EMFVelocityModifier: %s has no UEMF_FieldComponent"), *OtherActor->GetName());
		return;
	}

	float MyCharge = GetCharge();
	float OtherCharge = OtherFieldComp->GetSourceDescription().Charge;

	UE_LOG(LogTemp, Warning, TEXT("EMFVelocityModifier: MyCharge=%.2f, OtherCharge=%.2f"), MyCharge, OtherCharge);

	// ÃÅ¸Ã‘â‚¬ÃÂ¾ÃÂ²ÃÂµÃ‘â‚¬ÃÂ¸Ã‘â€šÃ‘Å’: ÃÂ¿Ã‘â‚¬ÃÂ¾Ã‘â€šÃÂ¸ÃÂ²ÃÂ¾ÃÂ¿ÃÂ¾ÃÂ»ÃÂ¾ÃÂ¶ÃÂ½Ã‘â€¹ÃÂµ ÃÂ·ÃÂ½ÃÂ°ÃÂºÃÂ¸?
	bool bOppositeSign = (MyCharge * OtherCharge) < 0.0f;

	if (bOppositeSign && FMath::Abs(OtherCharge) >= MinChargeToNeutralize)
	{
		float PrevCharge = MyCharge;

		UE_LOG(LogTemp, Warning, TEXT("EMFVelocityModifier: Opposite signs! Neutralizing... (bNeutralizeTargetOnly=%d)"), bNeutralizeTargetOnly);

		// ÃÂÃÂµÃÂ¹Ã‘â€šÃ‘â‚¬ÃÂ°ÃÂ»ÃÂ¸ÃÂ·ÃÂ¾ÃÂ²ÃÂ°Ã‘â€šÃ‘Å’ Ã‘ÂÃÂµÃÂ±Ã‘Â ÃÂµÃ‘ÂÃÂ»ÃÂ¸ ÃÂ¼ÃÂ¾ÃÂ¶ÃÂµÃÂ¼
		if (CanBeNeutralized())
		{
			NeutralizeCharge();
			OnChargeNeutralized.Broadcast(OtherActor, PrevCharge);
			UE_LOG(LogTemp, Warning, TEXT("EMFVelocityModifier: Self neutralized"));
		}

		// ÃÂÃÂµÃÂ¹Ã‘â€šÃ‘â‚¬ÃÂ°ÃÂ»ÃÂ¸ÃÂ·ÃÂ¾ÃÂ²ÃÂ°Ã‘â€šÃ‘Å’ Ã‘â€ ÃÂµÃÂ»Ã‘Å’ Ã‘â€šÃÂ¾ÃÂ»Ã‘Å’ÃÂºÃÂ¾ ÃÂµÃ‘ÂÃÂ»ÃÂ¸ bNeutralizeTargetOnly = false
		if (!bNeutralizeTargetOnly)
		{
			UEMFVelocityModifier* OtherModifier = OtherActor->FindComponentByClass<UEMFVelocityModifier>();
			if (OtherModifier)
			{
				if (OtherModifier->CanBeNeutralized())
				{
					OtherModifier->NeutralizeCharge();
					OtherModifier->OnChargeNeutralized.Broadcast(GetOwner(), OtherCharge);
					UE_LOG(LogTemp, Warning, TEXT("EMFVelocityModifier: Target %s neutralized via Modifier"), *OtherActor->GetName());
				}
			}
			else
			{
				// Ãâ€¢Ã‘ÂÃÂ»ÃÂ¸ ÃÂ½ÃÂµÃ‘â€š ÃÂ¼ÃÂ¾ÃÂ´ÃÂ¸Ã‘â€žÃÂ¸ÃÂºÃÂ°Ã‘â€šÃÂ¾Ã‘â‚¬ÃÂ° Ã¢â‚¬â€ ÃÂ¿Ã‘â‚¬ÃÂ¾Ã‘ÂÃ‘â€šÃÂ¾ ÃÂ¾ÃÂ±ÃÂ½Ã‘Æ’ÃÂ»ÃÂ¸Ã‘â€šÃ‘Å’ ÃÂ·ÃÂ°Ã‘â‚¬Ã‘ÂÃÂ´ ÃÂºÃÂ¾ÃÂ¼ÃÂ¿ÃÂ¾ÃÂ½ÃÂµÃÂ½Ã‘â€šÃÂ°
				OtherFieldComp->SetCharge(0.0f);

				// ÃÅ¸Ã‘â‚¬ÃÂ¾ÃÂ²ÃÂµÃ‘â‚¬ÃÂ¸Ã‘â€šÃ‘Å’ Ã‘â€¡Ã‘â€šÃÂ¾ ÃÂ·ÃÂ°Ã‘â‚¬Ã‘ÂÃÂ´ Ã‘â‚¬ÃÂµÃÂ°ÃÂ»Ã‘Å’ÃÂ½ÃÂ¾ ÃÂ¸ÃÂ·ÃÂ¼ÃÂµÃÂ½ÃÂ¸ÃÂ»Ã‘ÂÃ‘Â
				float NewCharge = OtherFieldComp->GetSourceDescription().Charge;
				UE_LOG(LogTemp, Warning, TEXT("EMFVelocityModifier: Target %s neutralized via FieldComponent. NewCharge=%.2f (should be 0)"),
					*OtherActor->GetName(), NewCharge);

				if (!FMath::IsNearlyZero(NewCharge))
				{
					UE_LOG(LogTemp, Error, TEXT("EMFVelocityModifier: SetCharge(0) didn't work! Check bUseOwnerInterface on %s"), *OtherActor->GetName());
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("EMFVelocityModifier: bNeutralizeTargetOnly=true, target not neutralized"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("EMFVelocityModifier: No neutralization (oppositeSign=%d, otherCharge=%.2f >= minCharge=%.2f: %d)"),
			bOppositeSign, FMath::Abs(OtherCharge), MinChargeToNeutralize, FMath::Abs(OtherCharge) >= MinChargeToNeutralize);
	}
}

void UEMFVelocityModifier::DrawDebugForces(const FVector& Position, const FVector& Force) const
{
	UWorld* World = GetWorld();
	if (!World || !FieldComponent)
	{
		return;
	}

	// ÃÂ¡ÃÂ¸ÃÂ»ÃÂ° (ÃÂºÃ‘â‚¬ÃÂ°Ã‘ÂÃÂ½Ã‘â€¹ÃÂ¹)
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

	// E-ÃÂ¿ÃÂ¾ÃÂ»ÃÂµ (ÃÂ¶Ã‘â€˜ÃÂ»Ã‘â€šÃ‘â€¹ÃÂ¹)
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

	// B-ÃÂ¿ÃÂ¾ÃÂ»ÃÂµ (Ã‘ÂÃÂ¸ÃÂ½ÃÂ¸ÃÂ¹)
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

	if (bLogForces)
	{
		UE_LOG(LogTemp, Log, TEXT("EMF: Added %.2f bonus charge. Total bonus: %.2f, Total charge: %.2f"),
			Amount, CurrentBonusCharge, GetTotalCharge());
	}
}

void UEMFVelocityModifier::AddPermanentCharge(float Amount)
{
	if (Amount == 0.0f)
	{
		return;
	}

	// Сохраняем знак заряда
	float Sign = (BaseCharge >= 0.0f) ? 1.0f : -1.0f;
	float CurrentModule = FMath::Abs(BaseCharge);

	// Положительный Amount увеличивает модуль, отрицательный уменьшает
	float NewModule = CurrentModule + Amount;

	// Не позволяем модулю стать отрицательным (клампим до 0)
	NewModule = FMath::Max(0.0f, NewModule);

	// Восстанавливаем знак
	BaseCharge = Sign * NewModule;
	UpdateFieldComponentCharge();

	if (bLogForces)
	{
		UE_LOG(LogTemp, Log, TEXT("EMF: Added %.2f permanent charge. BaseCharge: %.2f, Total charge: %.2f"),
			Amount, BaseCharge, GetTotalCharge());
	}
}

void UEMFVelocityModifier::SetBaseCharge(float NewBaseCharge)
{
	BaseCharge = NewBaseCharge;
	UpdateFieldComponentCharge();
}

void UEMFVelocityModifier::DeductCharge(float Amount)
{
	if (Amount <= 0.0f)
	{
		return;
	}

	float RemainingToDeduct = Amount;

	// Сначала вычитаем из бонусного заряда
	if (CurrentBonusCharge > 0.0f)
	{
		float DeductFromBonus = FMath::Min(CurrentBonusCharge, RemainingToDeduct);
		CurrentBonusCharge -= DeductFromBonus;
		RemainingToDeduct -= DeductFromBonus;

		if (bLogForces)
		{
			UE_LOG(LogTemp, Log, TEXT("EMF: Deducted %.2f from bonus charge. Remaining bonus: %.2f"),
				DeductFromBonus, CurrentBonusCharge);
		}
	}

	// Если остались невычтенные единицы - вычитаем из базового заряда
	if (RemainingToDeduct > 0.0f)
	{
		AddPermanentCharge(-RemainingToDeduct);

		if (bLogForces)
		{
			UE_LOG(LogTemp, Log, TEXT("EMF: Deducted %.2f from base charge. BaseCharge: %.2f"),
				RemainingToDeduct, BaseCharge);
		}
	}
	else
	{
		// Обновляем FieldComponent даже если вычли только из бонуса
		UpdateFieldComponentCharge();
	}
}

float UEMFVelocityModifier::GetTotalCharge() const
{
	// Ð—Ð½Ð°Ðº Ð¾Ð¿Ñ€ÐµÐ´ÐµÐ»ÑÐµÑ‚ÑÑ BaseCharge, Ð±Ð¾Ð½ÑƒÑ Ð´Ð¾Ð±Ð°Ð²Ð»ÑÐµÑ‚ÑÑ Ð¿Ð¾ Ð¼Ð¾Ð´ÑƒÐ»ÑŽ
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