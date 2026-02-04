// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterProjectile.h"
#include "ProjectilePoolSubsystem.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
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
			true
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
	if (TrailComponent)
	{
		TrailComponent->Deactivate();
	}

	// make AI perception noise
	MakeNoise(NoiseLoudness, GetInstigator(), GetActorLocation(), NoiseRange, NoiseTag);

	if (bExplodeOnHit)
	{
		
		// apply explosion damage centered on the projectile
		ExplosionCheck(GetActorLocation());

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

void AShooterProjectile::ExplosionCheck(const FVector& ExplosionCenter)
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
	if (!bDamageOwner)
	{
		QueryParams.AddIgnoredActor(GetInstigator());
	}

	GetWorld()->OverlapMultiByObjectType(Overlaps, ExplosionCenter, FQuat::Identity, ObjectParams, OverlapShape, QueryParams);

	TArray<AActor*> DamagedActors;

	// process the overlap results
	for (const FOverlapResult& CurrentOverlap : Overlaps)
	{
		// overlaps may return the same actor multiple times per each component overlapped
		// ensure we only damage each actor once by adding it to a damaged list
		if (DamagedActors.Find(CurrentOverlap.GetActor()) == INDEX_NONE)
		{
			DamagedActors.Add(CurrentOverlap.GetActor());

			// apply physics force away from the explosion
			const FVector& ExplosionDir = CurrentOverlap.GetActor()->GetActorLocation() - GetActorLocation();

			// push and/or damage the overlapped actor
			ProcessHit(CurrentOverlap.GetActor(), CurrentOverlap.GetComponent(), GetActorLocation(), ExplosionDir.GetSafeNormal());
		}
			
	}
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

void AShooterProjectile::InitializeForPool()
{
	bIsPooled = true;
	DeactivateToPool();
}

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
			true
		);
	}
	else if (TrailComponent)
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
	if (TrailComponent)
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
