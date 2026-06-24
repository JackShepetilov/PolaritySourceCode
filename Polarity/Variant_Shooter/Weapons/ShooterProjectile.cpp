// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterProjectile.h"
#include "ProjectilePoolSubsystem.h"
#include "ApexMovementComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/DamageEvents.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"

AShooterProjectile::AShooterProjectile()
{
	PrimaryActorTick.bCanEverTick = true;

	// create the collision component and assign it as the root
	RootComponent = CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("Collision Component"));

	CollisionComponent->SetSphereRadius(16.0f);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionComponent->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;

	// create the projectile movement component. No need to attach it because it's not a Scene Component
	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Projectile Movement"));

	ProjectileMovement->InitialSpeed = 3000.0f;
	ProjectileMovement->MaxSpeed = 3000.0f;
	ProjectileMovement->bShouldBounce = true;

	// set the default damage type
	HitDamageType = UDamageType::StaticClass();
}

void AShooterProjectile::BeginPlay()
{
	Super::BeginPlay();

	// If this projectile is being spawned for pool, skip normal initialization
	// InitializeForPool() will be called right after BeginPlay
	if (bIsPooled)
	{
		return;
	}

	// Normal spawn path (not from pool)
	// ignore the pawn that shot this projectile
	if (APawn* InstigatorPawn = GetInstigator())
	{
		CollisionComponent->IgnoreActorWhenMoving(InstigatorPawn, true);
		CollisionComponent->MoveIgnoreActors.Add(InstigatorPawn);

		// Also ignore instigator's collision with us
		if (UPrimitiveComponent* InstigatorRoot = Cast<UPrimitiveComponent>(InstigatorPawn->GetRootComponent()))
		{
			InstigatorRoot->IgnoreActorWhenMoving(this, true);
		}
	}

	// Spawn trail VFX if configured
	if (TrailFX)
	{
		TrailComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			TrailFX,
			CollisionComponent,
			NAME_None,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset,
			false // Keep the component alive; pooled projectiles reactivate it on reuse.
		);
	}
}

void AShooterProjectile::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the destruction timer
	GetWorld()->GetTimerManager().ClearTimer(DestructionTimer);
}

void AShooterProjectile::NotifyHit(class UPrimitiveComponent* MyComp, AActor* Other, class UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	// ignore if we've already hit something else
	if (bHit)
	{
		return;
	}

	bHit = true;

	// disable collision on the projectile
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Stop trail VFX on hit
	if (IsValid(TrailComponent))
	{
		TrailComponent->Deactivate();
	}

	// make AI perception noise
	MakeNoise(NoiseLoudness, GetInstigator(), GetActorLocation(), NoiseRange, NoiseTag);

	if (bExplodeOnHit)
	{
		
		// apply explosion damage centered on the projectile
		ExplosionCheck(GetActorLocation(), Other);

	} else {

		// single hit projectile. Process the collided actor
		ProcessHit(Other, OtherComp, Hit.ImpactPoint, -Hit.ImpactNormal);

	}

	// pass control to BP for any extra effects
	BP_OnProjectileHit(Hit);

	// check if we should schedule deferred destruction of the projectile
	if (DeferredDestructionTime > 0.0f)
	{
		GetWorld()->GetTimerManager().SetTimer(DestructionTimer, this, &AShooterProjectile::OnDeferredDestruction, DeferredDestructionTime, false);
	}
	else
	{
		// Return to pool or destroy right away
		ReturnToPoolOrDestroy();
	}
}

void AShooterProjectile::ExplosionCheck(const FVector& ExplosionCenter, AActor* DirectHitActor)
{
	// do a sphere overlap check look for nearby actors to damage
	TArray<FOverlapResult> Overlaps;

	FCollisionShape OverlapShape;
	OverlapShape.SetSphere(ExplosionRadius);

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	if (!bDamageOwner && !bEnableOwnerRocketJump)
	{
		QueryParams.AddIgnoredActor(GetInstigator());
		QueryParams.AddIgnoredActor(GetOwner());
	}

	GetWorld()->OverlapMultiByObjectType(Overlaps, ExplosionCenter, FQuat::Identity, ObjectParams, OverlapShape, QueryParams);

	TArray<AActor*> DamagedActors;

	// process the overlap results
	for (const FOverlapResult& CurrentOverlap : Overlaps)
	{
		// overlaps may return the same actor multiple times per each component overlapped
		// ensure we only damage each actor once by adding it to a damaged list
		AActor* HitActor = CurrentOverlap.GetActor();
		if (HitActor && DamagedActors.Find(HitActor) == INDEX_NONE)
		{
			DamagedActors.Add(HitActor);

			// push and/or damage the overlapped actor
			ProcessExplosionHit(HitActor, CurrentOverlap.GetComponent(), ExplosionCenter, DirectHitActor);
		}
			
	}
}

void AShooterProjectile::ProcessExplosionHit(AActor* HitActor, UPrimitiveComponent* HitComp, const FVector& ExplosionCenter, AActor* DirectHitActor)
{
	if (!HitActor)
	{
		return;
	}

	const bool bIsOwner =
		HitActor == GetInstigator() ||
		HitActor == GetOwner();

	if (bIsOwner && !bDamageOwner && !bEnableOwnerRocketJump)
	{
		return;
	}

	if (bRequireExplosionLineOfSight && HitActor != DirectHitActor)
	{
		FHitResult LOSHit;
		FCollisionQueryParams LOSParams;
		LOSParams.AddIgnoredActor(this);
		LOSParams.AddIgnoredActor(HitActor);

		const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
			LOSHit, ExplosionCenter, HitActor->GetActorLocation(), ECC_Visibility, LOSParams);
		if (bBlocked)
		{
			return;
		}
	}

	const float Distance = FVector::Dist(ExplosionCenter, HitActor->GetActorLocation());
	const float SplashScale = CalculateExplosionSplashScale(Distance, HitActor, DirectHitActor);
	if (SplashScale <= 0.0f)
	{
		return;
	}

	const FVector HitDirection = (HitActor->GetActorLocation() - ExplosionCenter).GetSafeNormal();

	if (ACharacter* HitCharacter = Cast<ACharacter>(HitActor))
	{
		const bool bCanAffectOwner = !bIsOwner || bDamageOwner || bEnableOwnerRocketJump;
		if (bCanAffectOwner)
		{
			float TagMultiplier = GetTagDamageMultiplier(HitActor);
			float FinalDamage = HitDamage * TagMultiplier * SplashScale;
			if (bIsOwner)
			{
				FinalDamage *= OwnerSelfDamageMultiplier;
			}

			if (FinalDamage > 0.0f && (!bIsOwner || bDamageOwner))
			{
				FRadialDamageEvent RadialDamageEvent;
				RadialDamageEvent.DamageTypeClass = HitDamageType;
				RadialDamageEvent.Origin = ExplosionCenter;
				RadialDamageEvent.Params.BaseDamage = HitDamage;
				RadialDamageEvent.Params.OuterRadius = ExplosionRadius;

				AController* InstigatorController = GetInstigator() ? GetInstigator()->GetController() : nullptr;
				HitCharacter->TakeDamage(FinalDamage, RadialDamageEvent, InstigatorController, this);
			}

			if (CharacterKnockbackForce > 0.0f && (!bIsOwner || bEnableOwnerRocketJump))
			{
				FVector LaunchDir = HitDirection;
				LaunchDir.Z += KnockbackUpwardBias;
				LaunchDir = LaunchDir.GetSafeNormal();

				float LaunchMultiplier = SplashScale;
				if (bIsOwner)
				{
					LaunchMultiplier *= GetOwnerRocketJumpMultiplier(HitCharacter);
				}

				HitCharacter->LaunchCharacter(LaunchDir * CharacterKnockbackForce * LaunchMultiplier, true, true);
			}
		}
	}

	if (HitComp && HitComp->IsSimulatingPhysics())
	{
		HitComp->AddImpulseAtLocation(HitDirection * PhysicsForce * SplashScale, ExplosionCenter);
	}
}

float AShooterProjectile::CalculateExplosionSplashScale(float Distance, const AActor* HitActor, const AActor* DirectHitActor) const
{
	if (bDirectHitIgnoresSplashFalloff && HitActor && HitActor == DirectHitActor)
	{
		return 1.0f;
	}

	if (ExplosionRadius <= 0.0f)
	{
		return 0.0f;
	}

	const float T = FMath::Clamp(Distance / ExplosionRadius, 0.0f, 1.0f);
	return FMath::Lerp(1.0f, ExplosionEdgeDamageMultiplier, FMath::Pow(T, ExplosionFalloffExponent));
}

float AShooterProjectile::GetOwnerRocketJumpMultiplier(const ACharacter* HitCharacter) const
{
	if (!HitCharacter)
	{
		return 1.0f;
	}

	const UCharacterMovementComponent* CharacterMovement = HitCharacter->GetCharacterMovement();
	if (!CharacterMovement || !CharacterMovement->IsFalling())
	{
		return OwnerGroundKnockbackMultiplier;
	}

	bool bCrouchHeld = CharacterMovement->IsCrouching();
	if (const UApexMovementComponent* ApexMovement = Cast<UApexMovementComponent>(CharacterMovement))
	{
		bCrouchHeld = bCrouchHeld ||
			ApexMovement->bIsCrouchedInAir ||
			ApexMovement->bWantsSlideOnLand ||
			ApexMovement->IsCrouchInputHeld();
	}

	return bCrouchHeld ? OwnerAirCrouchKnockbackMultiplier : OwnerAirKnockbackMultiplier;
}

void AShooterProjectile::ProcessHit(AActor* HitActor, UPrimitiveComponent* HitComp, const FVector& HitLocation, const FVector& HitDirection)
{
	// have we hit a character?
	if (ACharacter* HitCharacter = Cast<ACharacter>(HitActor))
	{
		// ignore the owner of this projectile
		if (HitCharacter != GetOwner() || bDamageOwner)
		{
			// Calculate tag-based damage multiplier
			float TagMultiplier = GetTagDamageMultiplier(HitActor);
			float FinalDamage = HitDamage * TagMultiplier;

			UE_LOG(LogTemp, Warning, TEXT("Projectile::ProcessHit - Target: %s, BaseDamage: %.1f, TagMultiplier: %.2f, FinalDamage: %.1f, TagMultipliers count: %d"),
				*HitActor->GetName(),
				HitDamage,
				TagMultiplier,
				FinalDamage,
				TagDamageMultipliers.Num());

			// Log all tags on target and all configured multipliers
			for (const auto& Pair : TagDamageMultipliers)
			{
				bool bHasTag = HitActor->ActorHasTag(Pair.Key);
				UE_LOG(LogTemp, Warning, TEXT("  TagMultiplier: '%s' = %.2f, Target has tag: %s"),
					*Pair.Key.ToString(),
					Pair.Value,
					bHasTag ? TEXT("YES") : TEXT("NO"));
			}

			// apply damage to the character
			UGameplayStatics::ApplyDamage(HitCharacter, FinalDamage, GetInstigator()->GetController(), this, HitDamageType);

			// knockback: CharacterMovement ignores physics impulses, so launch the character instead
			if (CharacterKnockbackForce > 0.0f)
			{
				// add an upward bias so explosions at the feet pop the target up (TF2-style) instead of purely sideways
				FVector LaunchDir = HitDirection;
				LaunchDir.Z += KnockbackUpwardBias;
				LaunchDir = LaunchDir.GetSafeNormal();

				// override XY and Z so the launch is predictable regardless of the target's current velocity
				HitCharacter->LaunchCharacter(LaunchDir * CharacterKnockbackForce, true, true);
			}
		}
	}

	// have we hit a physics object?
	if (HitComp && HitComp->IsSimulatingPhysics())
	{
		// give some physics impulse to the object
		HitComp->AddImpulseAtLocation(HitDirection * PhysicsForce, HitLocation);
	}
}

float AShooterProjectile::GetTagDamageMultiplier(AActor* Target) const
{
	if (!Target || TagDamageMultipliers.Num() == 0)
	{
		return 1.0f;
	}

	float Multiplier = 1.0f;

	for (const auto& Pair : TagDamageMultipliers)
	{
		if (Target->ActorHasTag(Pair.Key))
		{
			Multiplier *= Pair.Value;
		}
	}

	return Multiplier;
}

void AShooterProjectile::OnDeferredDestruction()
{
	// Return to pool or destroy
	ReturnToPoolOrDestroy();
}

// ==================== Pooling Implementation ====================

void AShooterProjectile::ActivateFromPool(const FTransform& SpawnTransform, AActor* NewOwner, APawn* NewInstigator)
{
	// Reset state
	ResetProjectileState();

	// Set owner and instigator
	SetOwner(NewOwner);
	SetInstigator(NewInstigator);

	// Set transform
	SetActorTransform(SpawnTransform);

	// Enable collision
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	// Setup instigator ignore (same as BeginPlay)
	if (NewInstigator)
	{
		CollisionComponent->IgnoreActorWhenMoving(NewInstigator, true);
		CollisionComponent->MoveIgnoreActors.Add(NewInstigator);

		if (UPrimitiveComponent* InstigatorRoot = Cast<UPrimitiveComponent>(NewInstigator->GetRootComponent()))
		{
			InstigatorRoot->IgnoreActorWhenMoving(this, true);
		}
	}

	// Reset and activate projectile movement
	ProjectileMovement->SetVelocityInLocalSpace(FVector(ProjectileMovement->InitialSpeed, 0.0f, 0.0f));
	ProjectileMovement->SetUpdatedComponent(CollisionComponent);
	ProjectileMovement->Activate(true);

	// Show actor
	SetActorHiddenInGame(false);
	SetActorTickEnabled(true);

	if (TrailComponent && !IsValid(TrailComponent))
	{
		TrailComponent = nullptr;
	}

	// Spawn trail VFX if configured
	if (TrailFX && !TrailComponent)
	{
		TrailComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			TrailFX,
			CollisionComponent,
			NAME_None,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset,
			false // Keep the component alive; pooled projectiles reactivate it on reuse.
		);
	}
	else if (IsValid(TrailComponent))
	{
		TrailComponent->Activate(true);
	}
}

void AShooterProjectile::DeactivateToPool()
{
	// Hide actor
	SetActorHiddenInGame(true);
	SetActorTickEnabled(false);

	// Disable collision
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Stop movement
	ProjectileMovement->Deactivate();
	ProjectileMovement->Velocity = FVector::ZeroVector;

	// Stop trail VFX
	if (IsValid(TrailComponent))
	{
		TrailComponent->Deactivate();
	}

	// Clear timers
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DestructionTimer);
	}

	// Clear ignore actors for next use
	CollisionComponent->ClearMoveIgnoreActors();
}

void AShooterProjectile::ResetProjectileState()
{
	// Reset hit flag
	bHit = false;

	// Clear previous instigator ignores
	CollisionComponent->ClearMoveIgnoreActors();
}

void AShooterProjectile::ReturnToPoolOrDestroy()
{
	if (bIsPooled)
	{
		// Return to pool for reuse
		if (UWorld* World = GetWorld())
		{
			if (UProjectilePoolSubsystem* Pool = World->GetSubsystem<UProjectilePoolSubsystem>())
			{
				Pool->ReturnProjectile(this);
				return;
			}
		}
	}

	// Not pooled or pool not found - destroy normally
	Destroy();
}
