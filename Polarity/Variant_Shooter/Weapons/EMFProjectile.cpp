// EMFProjectile.cpp
// Electromagnetic field projectile implementation

#include "EMFProjectile.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"

// EMF Plugin includes
#include "EMF_FieldComponent.h"
#include "EMF_PluginBPLibrary.h"

AEMFProjectile::AEMFProjectile()
{
	// Enable tick for EMF force calculations
	PrimaryActorTick.bCanEverTick = true;

	// Create EMF Field Component - this is the only source of truth for charge/mass
	FieldComponent = CreateDefaultSubobject<UEMF_FieldComponent>(TEXT("EMFFieldComponent"));
}

void AEMFProjectile::BeginPlay()
{
	Super::BeginPlay();

	// Initialize charge and mass in FieldComponent (source of truth)
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = DefaultCharge;
		Desc.Mass = DefaultMass;
		FieldComponent->SetSourceDescription(Desc);
	}

	// Log initialization for debugging
	UE_LOG(LogTemp, Log, TEXT("EMFProjectile spawned: Charge=%.2f, Mass=%.2f, AffectedByFields=%d"),
		GetProjectileCharge(), GetProjectileMass(), bAffectedByExternalFields);
}

void AEMFProjectile::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Apply EMF forces to projectile velocity
	if (bAffectedByExternalFields && FieldComponent && ProjectileMovement)
	{
		ApplyEMForces(DeltaTime);
	}
}

void AEMFProjectile::SetProjectileCharge(float NewCharge)
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = NewCharge;
		FieldComponent->SetSourceDescription(Desc);
	}
}

float AEMFProjectile::GetProjectileCharge() const
{
	if (FieldComponent)
	{
		return FieldComponent->GetSourceDescription().PointChargeParams.Charge;
	}
	return 0.0f;
}

void AEMFProjectile::SetProjectileMass(float NewMass)
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.Mass = NewMass;
		FieldComponent->SetSourceDescription(Desc);
	}
}

float AEMFProjectile::GetProjectileMass() const
{
	if (FieldComponent)
	{
		return FieldComponent->GetSourceDescription().Mass;
	}
	return 1.0f;
}

void AEMFProjectile::InitializeFromPlayerCharge(AActor* PlayerActor, float ChargeAmount)
{
	if (!PlayerActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("EMFProjectile::InitializeFromPlayerCharge: Invalid PlayerActor"));
		return;
	}

	// Get player's UEMF_FieldComponent to read charge sign
	UEMF_FieldComponent* PlayerFieldComp = PlayerActor->FindComponentByClass<UEMF_FieldComponent>();
	if (PlayerFieldComp)
	{
		// Use player's charge sign
		float PlayerCharge = PlayerFieldComp->GetSourceDescription().PointChargeParams.Charge;
		float ChargeSign = FMath::Sign(PlayerCharge);
		SetProjectileCharge(ChargeAmount * ChargeSign);

		UE_LOG(LogTemp, Log, TEXT("EMFProjectile initialized from player charge: %.2f"), ChargeAmount * ChargeSign);
	}
	else
	{
		// Fallback: just use the provided charge
		SetProjectileCharge(ChargeAmount);
		UE_LOG(LogTemp, Warning, TEXT("EMFProjectile: Player has no UEMF_FieldComponent, using raw charge %.2f"), ChargeAmount);
	}
}

void AEMFProjectile::ProcessHit(AActor* HitActor, UPrimitiveComponent* HitComp, const FVector& HitLocation, const FVector& HitDirection)
{
	// Store projectile charge before processing
	float ProjectileCharge = GetProjectileCharge();

	// Transfer charge if enabled
	if (bTransferChargeOnHit && HitActor)
	{
		TransferChargeToActor(HitActor);
	}

	// Apply EMF damage (with charge scaling if enabled)
	if (ACharacter* HitCharacter = Cast<ACharacter>(HitActor))
	{
		// Ignore the owner unless bDamageOwner is true
		if (HitCharacter != GetOwner() || bDamageOwner)
		{
			// Calculate damage with charge scaling
			float FinalDamage = CalculateChargeDamage();

			UGameplayStatics::ApplyDamage(HitCharacter, FinalDamage, GetInstigator()->GetController(), this, HitDamageType);

			UE_LOG(LogTemp, Log, TEXT("EMFProjectile hit: BaseDamage=%.1f, ChargeDamage=%.1f, Charge=%.2f"),
				HitDamage, FinalDamage, ProjectileCharge);
		}
	}

	// Apply physics forces (same as parent)
	if (HitComp && HitComp->IsSimulatingPhysics())
	{
		HitComp->AddImpulseAtLocation(HitDirection * PhysicsForce, HitLocation);
	}

	// Blueprint event for custom EMF effects
	if (HitActor)
	{
		FHitResult Hit;
		Hit.Location = HitLocation;
		Hit.ImpactPoint = HitLocation;
		Hit.Normal = HitDirection;
		Hit.ImpactNormal = -HitDirection;
		Hit.HitObjectHandle = FActorInstanceHandle(HitActor);
		Hit.Component = HitComp;

		BP_OnEMFHit(HitActor, ProjectileCharge, Hit);
	}
}

float AEMFProjectile::CalculateChargeDamage() const
{
	float BaseDamage = HitDamage;

	if (!bUseChargeDamageScaling)
	{
		return BaseDamage;
	}

	// Scale damage based on charge magnitude
	float Charge = FMath::Abs(GetProjectileCharge());
	float ChargeMultiplier = 1.0f + (ChargeDamageMultiplier * Charge);

	// Clamp to max multiplier
	ChargeMultiplier = FMath::Min(ChargeMultiplier, MaxChargeDamageMultiplier);

	return BaseDamage * ChargeMultiplier;
}

void AEMFProjectile::TransferChargeToActor(AActor* HitActor)
{
	if (!HitActor || !bTransferChargeOnHit)
	{
		return;
	}

	// Find target's EMF field component
	UEMF_FieldComponent* TargetFieldComp = HitActor->FindComponentByClass<UEMF_FieldComponent>();
	if (!TargetFieldComp)
	{
		// Target doesn't have EMF system, no charge transfer
		return;
	}

	float ProjectileCharge = GetProjectileCharge();
	float TargetCharge = TargetFieldComp->GetSourceDescription().PointChargeParams.Charge;
	float ChargeToTransfer = ProjectileCharge * ChargeTransferRatio;

	// Check if charges are opposite signs
	bool bOppositeCharges = (ProjectileCharge * TargetCharge) < 0.0f;

	if (bOppositeCharges && bNeutralizeOppositeCharges)
	{
		// Neutralization: reduce both charges
		float AbsProjectileCharge = FMath::Abs(ChargeToTransfer);
		float AbsTargetCharge = FMath::Abs(TargetCharge);

		if (AbsProjectileCharge >= AbsTargetCharge)
		{
			// Projectile charge neutralizes target completely
			FEMSourceDescription TargetDesc = TargetFieldComp->GetSourceDescription();
			TargetDesc.PointChargeParams.Charge = 0.0f;
			TargetFieldComp->SetSourceDescription(TargetDesc);
			UE_LOG(LogTemp, Log, TEXT("EMFProjectile: Target charge neutralized completely"));
		}
		else
		{
			// Partial neutralization
			float NewTargetCharge = TargetCharge + ChargeToTransfer; // Signs cancel out
			FEMSourceDescription TargetDesc = TargetFieldComp->GetSourceDescription();
			TargetDesc.PointChargeParams.Charge = NewTargetCharge;
			TargetFieldComp->SetSourceDescription(TargetDesc);
			UE_LOG(LogTemp, Log, TEXT("EMFProjectile: Partial neutralization, target charge %.2f -> %.2f"),
				TargetCharge, NewTargetCharge);
		}
	}
	else
	{
		// Same sign or neutralization disabled: add charges
		float NewTargetCharge = TargetCharge + ChargeToTransfer;
		FEMSourceDescription TargetDesc = TargetFieldComp->GetSourceDescription();
		TargetDesc.PointChargeParams.Charge = NewTargetCharge;
		TargetFieldComp->SetSourceDescription(TargetDesc);

		UE_LOG(LogTemp, Log, TEXT("EMFProjectile: Charge transferred to target: %.2f (target: %.2f -> %.2f)"),
			ChargeToTransfer, TargetCharge, NewTargetCharge);
	}
}

void AEMFProjectile::ApplyEMForces(float DeltaTime)
{
	if (!FieldComponent || !ProjectileMovement)
	{
		return;
	}

	float Charge = GetProjectileCharge();
	if (FMath::IsNearlyZero(Charge))
	{
		return;
	}

	// Get all other EMF sources (excluding self)
	TArray<FEMSourceDescription> OtherSources = FieldComponent->GetAllOtherSources();
	if (OtherSources.Num() == 0)
	{
		// No external fields to interact with
		return;
	}

	FVector Position = GetActorLocation();
	FVector Velocity = ProjectileMovement->Velocity;
	float Mass = GetProjectileMass();

	// Calculate Lorentz force: F = q(E + v × B)
	FVector EMForce = UEMF_PluginBPLibrary::CalculateLorentzForceComplete(
		Charge,
		Position,
		Velocity,
		OtherSources,
		true  // Include magnetic component
	);

	// Clamp maximum force
	if (EMForce.SizeSquared() > MaxEMForce * MaxEMForce)
	{
		EMForce = EMForce.GetSafeNormal() * MaxEMForce;
	}

	// Apply force: a = F/m, v_new = v_old + a*dt
	FVector Acceleration = EMForce / FMath::Max(Mass, 0.001f);
	FVector VelocityDelta = Acceleration * DeltaTime;

	// Directly modify projectile velocity
	ProjectileMovement->Velocity += VelocityDelta;

	// Debug visualization
	if (bDrawDebugForces)
	{
		DrawDebugDirectionalArrow(
			GetWorld(),
			Position,
			Position + EMForce.GetSafeNormal() * FMath::Min(EMForce.Size(), 200.0f),
			10.0f,
			FColor::Cyan,
			false,
			-1.0f,
			0,
			2.0f
		);
	}

	if (bLogEMForces && !EMForce.IsNearlyZero())
	{
		UE_LOG(LogTemp, Log, TEXT("EMFProjectile: Charge=%.2f Force=(%.2f, %.2f, %.2f) Sources=%d"),
			Charge, EMForce.X, EMForce.Y, EMForce.Z, OtherSources.Num());
	}
}
