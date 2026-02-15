// EMFProjectile.cpp
// Electromagnetic field projectile implementation

#include "EMFProjectile.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"

// EMF Plugin includes
#include "EMF_FieldComponent.h"
#include "EMF_PluginBPLibrary.h"

// Damage types
#include "Variant_Shooter/DamageTypes/DamageType_EMFWeapon.h"

AEMFProjectile::AEMFProjectile()
{
	// Enable tick for EMF force calculations
	PrimaryActorTick.bCanEverTick = true;

	// Create EMF Field Component - this is the only source of truth for charge/mass
	FieldComponent = CreateDefaultSubobject<UEMF_FieldComponent>(TEXT("EMFFieldComponent"));

	// Set projectile owner type for EM force filtering
	if (FieldComponent)
	{
		FieldComponent->SetOwnerType(EEMSourceOwnerType::Projectile);
	}

	// Set default damage type to EMFWeapon (EMF category for damage numbers)
	HitDamageType = UDamageType_EMFWeapon::StaticClass();
}

void AEMFProjectile::BeginPlay()
{
	Super::BeginPlay();

	// Initialize charge and mass in FieldComponent (source of truth)
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = DefaultCharge;
		Desc.PhysicsParams.Mass = DefaultMass;
		FieldComponent->SetSourceDescription(Desc);
	}

	// Note: Trail VFX is spawned in SetProjectileCharge() after charge is set by weapon
	// This ensures we use the correct polarity (player's charge sign)

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

void AEMFProjectile::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	// Spawn charge-based explosion VFX before parent processes the hit
	// (parent may destroy the projectile)
	if (bExplodeOnHit)
	{
		SpawnChargeBasedExplosionVFX(GetActorLocation());
	}
	else
	{
		// For non-explosive projectiles, spawn explosion VFX at impact point
		SpawnChargeBasedExplosionVFX(Hit.ImpactPoint);
	}

	// Call parent implementation
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
}

void AEMFProjectile::SetProjectileCharge(float NewCharge)
{
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = NewCharge;
		FieldComponent->SetSourceDescription(Desc);
	}

	// Update trail VFX to match new charge polarity
	SpawnChargeBasedTrailVFX();
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
		Desc.PhysicsParams.Mass = NewMass;
		FieldComponent->SetSourceDescription(Desc);
	}
}

float AEMFProjectile::GetProjectileMass() const
{
	if (FieldComponent)
	{
		return FieldComponent->GetSourceDescription().PhysicsParams.Mass;
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

void AEMFProjectile::ResetProjectileState()
{
	// Call parent to reset base state
	Super::ResetProjectileState();

	// Reset charge and mass to defaults
	if (FieldComponent)
	{
		FEMSourceDescription Desc = FieldComponent->GetSourceDescription();
		Desc.PointChargeParams.Charge = DefaultCharge;
		Desc.PhysicsParams.Mass = DefaultMass;
		FieldComponent->SetSourceDescription(Desc);
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
			float ChargeDamage = CalculateChargeDamage();

			// Apply tag-based damage multiplier (inherited from ShooterProjectile)
			float TagMultiplier = GetTagDamageMultiplier(HitActor);
			float FinalDamage = ChargeDamage * TagMultiplier;

			UE_LOG(LogTemp, Warning, TEXT("EMFProjectile::ProcessHit - Target: %s, BaseDamage: %.1f, ChargeDamage: %.1f, TagMultiplier: %.2f, FinalDamage: %.1f, Charge: %.2f"),
				*HitActor->GetName(),
				HitDamage,
				ChargeDamage,
				TagMultiplier,
				FinalDamage,
				ProjectileCharge);

			// Log all tags on target and all configured multipliers
			for (const auto& Pair : TagDamageMultipliers)
			{
				bool bHasTag = HitActor->ActorHasTag(Pair.Key);
				UE_LOG(LogTemp, Warning, TEXT("  EMFProjectile TagMultiplier: '%s' = %.2f, Target has tag: %s"),
					*Pair.Key.ToString(),
					Pair.Value,
					bHasTag ? TEXT("YES") : TEXT("NO"));
			}

			UGameplayStatics::ApplyDamage(HitCharacter, FinalDamage, GetInstigator()->GetController(), this, HitDamageType);
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

	// Calculate Lorentz force from each source with filtering
	FVector EMForce = FVector::ZeroVector;
	for (const FEMSourceDescription& Source : OtherSources)
	{
		// Get multiplier based on source owner type
		float Multiplier = 1.0f;
		switch (Source.OwnerType)
		{
		case EEMSourceOwnerType::Player:
			Multiplier = PlayerForceMultiplier;
			break;
		case EEMSourceOwnerType::NPC:
			Multiplier = NPCForceMultiplier;
			break;
		case EEMSourceOwnerType::Projectile:
			Multiplier = ProjectileForceMultiplier;
			break;
		case EEMSourceOwnerType::Environment:
			Multiplier = EnvironmentForceMultiplier;
			break;
		case EEMSourceOwnerType::None:
		default:
			Multiplier = UnknownForceMultiplier;
			break;
		}

		// Debug: Log source owner type and multiplier
		if (bLogEMForces)
		{
			UE_LOG(LogTemp, Warning, TEXT("EMFProjectile: Source OwnerType=%d, Multiplier=%.2f, Charge=%.2f"),
				static_cast<int32>(Source.OwnerType), Multiplier, Source.PointChargeParams.Charge);
		}

		// Skip if multiplier is zero (fully filtered)
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
			Velocity,
			SingleSource,
			true  // Include magnetic component
		);

		// Apply multiplier and accumulate
		EMForce += SourceForce * Multiplier;
	}

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

UNiagaraSystem* AEMFProjectile::GetChargeBasedVFX(UNiagaraSystem* PositiveVFX, UNiagaraSystem* NegativeVFX) const
{
	float Charge = GetProjectileCharge();

	if (Charge > 0.0f)
	{
		return PositiveVFX;
	}
	else if (Charge < 0.0f)
	{
		return NegativeVFX;
	}

	// Neutral charge - return nullptr (no VFX)
	return nullptr;
}

void AEMFProjectile::SpawnChargeBasedTrailVFX()
{
	// Get charge-based trail VFX
	UNiagaraSystem* ChargeTrailVFX = GetChargeBasedVFX(PositiveTrailVFX, NegativeTrailVFX);

	// If no charge-based VFX is set, parent's TrailFX will be used (already spawned in Super::BeginPlay)
	if (!ChargeTrailVFX)
	{
		return;
	}

	// Stop parent's trail if it was spawned
	if (TrailComponent)
	{
		TrailComponent->Deactivate();
		TrailComponent = nullptr;
	}

	// Spawn charge-based trail
	TrailComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
		ChargeTrailVFX,
		CollisionComponent,
		NAME_None,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		EAttachLocation::KeepRelativeOffset,
		true
	);
}

void AEMFProjectile::SpawnChargeBasedExplosionVFX(const FVector& Location)
{
	UNiagaraSystem* ExplosionVFX = GetChargeBasedVFX(PositiveExplosionVFX, NegativeExplosionVFX);

	if (!ExplosionVFX)
	{
		return;
	}

	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		ExplosionVFX,
		Location,
		FRotator::ZeroRotator,
		FVector(1.0f),
		true,
		true,
		ENCPoolMethod::None
	);
}
