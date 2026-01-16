// EMFProjectile.cpp
// Electromagnetic field projectile implementation

#include "EMFProjectile.h"
#include "EMFVelocityModifier.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"

// EMF Plugin includes
#include "EMF_FieldComponent.h"
#include "EMF_PluginBPLibrary.h"

AEMFProjectile::AEMFProjectile()
{
	// Enable tick for EMF updates
	PrimaryActorTick.bCanEverTick = true;

	// Create EMF Field Component
	FieldComponent = CreateDefaultSubobject<UEMF_FieldComponent>(TEXT("EMFFieldComponent"));
	if (FieldComponent)
	{
		// Initialize with default values
		// Note: Actual property names may differ based on EMF_Plugin implementation
		// Adjust these based on your plugin's API
	}

	// Create EMF Velocity Modifier (for field interactions)
	VelocityModifier = CreateDefaultSubobject<UEMFVelocityModifier>(TEXT("EMFVelocityModifier"));
	if (VelocityModifier)
	{
		// Configure to work with projectile
		VelocityModifier->bEnabled = true;
	}
}

void AEMFProjectile::BeginPlay()
{
	Super::BeginPlay();

	// Initialize charge and mass from defaults
	SetProjectileCharge(DefaultCharge);
	SetProjectileMass(DefaultMass);

	// Enable/disable external field influence
	if (VelocityModifier)
	{
		VelocityModifier->SetEnabled(bAffectedByExternalFields);
	}

	// Log initialization for debugging
	UE_LOG(LogTemp, Log, TEXT("EMFProjectile spawned: Charge=%.2f, Mass=%.2f, AffectedByFields=%d"),
		GetProjectileCharge(), GetProjectileMass(), bAffectedByExternalFields);
}

void AEMFProjectile::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// EMF forces are automatically handled by VelocityModifier
	// Additional per-frame logic can go here if needed
}

void AEMFProjectile::SetProjectileCharge(float NewCharge)
{
	if (VelocityModifier)
	{
		VelocityModifier->SetCharge(NewCharge);
	}
}

float AEMFProjectile::GetProjectileCharge() const
{
	if (VelocityModifier)
	{
		return VelocityModifier->GetCharge();
	}
	return 0.0f;
}

void AEMFProjectile::SetProjectileMass(float NewMass)
{
	if (VelocityModifier)
	{
		VelocityModifier->SetMass(NewMass);
	}
}

float AEMFProjectile::GetProjectileMass() const
{
	if (VelocityModifier)
	{
		return VelocityModifier->GetMass();
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

	// Get player's EMFVelocityModifier to read excess charge
	UEMFVelocityModifier* PlayerModifier = PlayerActor->FindComponentByClass<UEMFVelocityModifier>();
	if (PlayerModifier)
	{
		// For now, just set the charge amount provided
		// Future: Calculate based on player's bonus charge, extract it, etc.
		float ChargeSign = FMath::Sign(PlayerModifier->GetCharge());
		SetProjectileCharge(ChargeAmount * ChargeSign);

		UE_LOG(LogTemp, Log, TEXT("EMFProjectile initialized from player charge: %.2f"), ChargeAmount * ChargeSign);
	}
	else
	{
		// Fallback: just use the provided charge
		SetProjectileCharge(ChargeAmount);
		UE_LOG(LogTemp, Warning, TEXT("EMFProjectile: Player has no EMFVelocityModifier, using raw charge %.2f"), ChargeAmount);
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

	// Find target's EMF components
	UEMFVelocityModifier* TargetModifier = HitActor->FindComponentByClass<UEMFVelocityModifier>();
	if (!TargetModifier)
	{
		// Target doesn't have EMF system, no charge transfer
		return;
	}

	float ProjectileCharge = GetProjectileCharge();
	float TargetCharge = TargetModifier->GetCharge();
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
			TargetModifier->SetCharge(0.0f);
			UE_LOG(LogTemp, Log, TEXT("EMFProjectile: Target charge neutralized completely"));
		}
		else
		{
			// Partial neutralization
			float NewTargetCharge = TargetCharge + ChargeToTransfer; // Signs cancel out
			TargetModifier->SetCharge(NewTargetCharge);
			UE_LOG(LogTemp, Log, TEXT("EMFProjectile: Partial neutralization, target charge %.2f -> %.2f"),
				TargetCharge, NewTargetCharge);
		}
	}
	else
	{
		// Same sign or neutralization disabled: add charges
		float NewTargetCharge = TargetCharge + ChargeToTransfer;
		TargetModifier->SetCharge(NewTargetCharge);

		UE_LOG(LogTemp, Log, TEXT("EMFProjectile: Charge transferred to target: %.2f (target: %.2f -> %.2f)"),
			ChargeToTransfer, TargetCharge, NewTargetCharge);
	}
}
