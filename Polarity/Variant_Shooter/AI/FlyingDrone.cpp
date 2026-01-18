// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlyingDrone.h"
#include "FlyingAIMovementComponent.h"
#include "ShooterWeapon.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/DamageEvents.h"
#include "TimerManager.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "../../AI/Components/AIAccuracyComponent.h"
#include "ShooterGameMode.h"
#include "EMFVelocityModifier.h"

AFlyingDrone::AFlyingDrone(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Create flying movement component
	FlyingMovement = CreateDefaultSubobject<UFlyingAIMovementComponent>(TEXT("FlyingMovement"));

	// Create sphere collision (for visual attachment and overlap detection)
	DroneCollision = CreateDefaultSubobject<USphereComponent>(TEXT("DroneCollision"));
	DroneCollision->InitSphereRadius(CollisionRadius);
	DroneCollision->SetCollisionProfileName(FName("OverlapAllDynamic"));
	DroneCollision->SetupAttachment(RootComponent);

	// Create visual mesh
	DroneMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DroneMesh"));
	DroneMesh->SetupAttachment(DroneCollision);
	DroneMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Load default sphere mesh (placeholder)
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere"));
	if (SphereMesh.Succeeded())
	{
		DroneMesh->SetStaticMesh(SphereMesh.Object);
		// Scale to match collision radius (default sphere is 100cm diameter)
		const float MeshScale = (CollisionRadius * 2.0f) / 100.0f;
		DroneMesh->SetRelativeScale3D(FVector(MeshScale));
	}

	// Configure CapsuleComponent for movement collision (CharacterMovementComponent uses this)
	// Make it sphere-like by setting radius = halfHeight
	GetCapsuleComponent()->SetCapsuleSize(CollisionRadius, CollisionRadius);
	GetCapsuleComponent()->SetCollisionProfileName(FName("Pawn"));

	// Hide character meshes (we use DroneMesh instead)
	GetMesh()->SetVisibility(false);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Configure character movement for flying
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	if (CMC)
	{
		CMC->SetMovementMode(MOVE_Flying);
		CMC->GravityScale = 0.0f;
		CMC->bOrientRotationToMovement = false;
		CMC->bUseControllerDesiredRotation = false;
	}

	// Drone doesn't use ragdoll
	RagdollCollisionProfile = FName("NoCollision");
}

void AFlyingDrone::BeginPlay()
{
	Super::BeginPlay();

	// Update sphere collision radius
	if (DroneCollision)
	{
		DroneCollision->SetSphereRadius(CollisionRadius);
	}

	// Update CapsuleComponent size to match CollisionRadius (sphere-like)
	GetCapsuleComponent()->SetCapsuleSize(CollisionRadius, CollisionRadius);

	// Update mesh scale to match collision
	if (DroneMesh && DroneMesh->GetStaticMesh())
	{
		const float MeshScale = (CollisionRadius * 2.0f) / 100.0f;
		DroneMesh->SetRelativeScale3D(FVector(MeshScale));
	}

	// Subscribe to movement completed event
	if (FlyingMovement)
	{
		FlyingMovement->OnMovementCompleted.AddDynamic(this, &AFlyingDrone::OnMovementCompleted);
	}

	// Start combat check timer
	if (bAutoEngage)
	{
		GetWorld()->GetTimerManager().SetTimer(
			CombatTimerHandle,
			this,
			&AFlyingDrone::UpdateCombat,
			TargetCheckInterval,
			true
		);
	}
}

void AFlyingDrone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bIsDead)
	{
		UpdateDroneVisuals(DeltaTime);
	}
}

// ==================== Damage & Death Handling ====================

float AFlyingDrone::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// Ignore if already dead
	if (bIsDead)
	{
		return 0.0f;
	}

	// Ignore friendly fire from other NPCs (same logic as ShooterNPC)
	if (DamageCauser)
	{
		AActor* DamageOwner = DamageCauser->GetOwner();
		if (Cast<AShooterNPC>(DamageCauser) || Cast<AShooterNPC>(DamageOwner))
		{
			return 0.0f;
		}

		if (EventInstigator)
		{
			if (Cast<AShooterNPC>(EventInstigator->GetPawn()))
			{
				return 0.0f;
			}
		}
	}

	// Reduce HP
	CurrentHP -= Damage;

	// Check if we should die
	if (CurrentHP <= 0.0f)
	{
		DroneDie();
	}

	return Damage;
}

void AFlyingDrone::DroneDie()
{
	// Ignore if already dead or death sequence started
	if (bIsDead || bDeathSequenceStarted)
	{
		return;
	}

	bDeathSequenceStarted = true;
	bIsDead = true;

	// Stop combat timer
	GetWorld()->GetTimerManager().ClearTimer(CombatTimerHandle);

	// Stop shooting
	StopShooting();

	// Stop movement
	StopMovement();

	// Unregister from coordinator to free attack slot
	UnregisterFromCoordinator();

	// Increment team score
	if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->IncrementTeamScore(TeamByte);
	}

	// Broadcast death
	UE_LOG(LogTemp, Warning, TEXT("FlyingDrone::DroneDie() - Broadcasting OnNPCDeath for %s"), *GetName());
	OnNPCDeath.Broadcast(this);

	if (bExplodeOnDeath)
	{
		TriggerExplosion();
	}
	else
	{
		StartDeathFall();
	}

	// Schedule destruction
	GetWorld()->GetTimerManager().SetTimer(
		DeathSequenceTimer,
		this,
		&AFlyingDrone::DeathDestroy,
		DeathEffectDuration,
		false
	);
}

void AFlyingDrone::TriggerExplosion()
{
	// Spawn explosion VFX
	SpawnExplosionEffect();

	// Play explosion sound
	if (ExplosionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), ExplosionSound, GetActorLocation());
	}

	// Apply radial damage
	if (ExplosionDamage > 0.0f && ExplosionRadius > 0.0f)
	{
		UGameplayStatics::ApplyRadialDamage(
			GetWorld(),
			ExplosionDamage,
			GetActorLocation(),
			ExplosionRadius,
			UDamageType::StaticClass(),
			TArray<AActor*>({ this }),
			this,
			GetController(),
			true,
			ECC_Visibility
		);
	}

	// Disable collision immediately
	if (DroneCollision)
	{
		DroneCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Hide mesh (explosion replaces it)
	if (DroneMesh)
	{
		DroneMesh->SetVisibility(false);
	}
}

void AFlyingDrone::StartDeathFall()
{
	// Enable gravity to make drone fall
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	if (CMC)
	{
		CMC->GravityScale = 1.0f;
		CMC->SetMovementMode(MOVE_Falling);
	}

	// Could add spin/tumble effect here
}

void AFlyingDrone::DeathDestroy()
{
	Destroy();
}

// ==================== Weapon Handling ====================

void AFlyingDrone::AttachWeaponMeshes(AShooterWeapon* WeaponToAttach)
{
	if (!WeaponToAttach)
	{
		return;
	}

	// Attach weapon to drone body
	const FAttachmentTransformRules AttachmentRule(EAttachmentRule::SnapToTarget, false);

	WeaponToAttach->AttachToActor(this, AttachmentRule);

	// Position weapon below/in front of drone
	// Hide first person mesh (drone doesn't have first person view)
	if (WeaponToAttach->GetFirstPersonMesh())
	{
		WeaponToAttach->GetFirstPersonMesh()->SetVisibility(false);
	}

	// Attach third person mesh to drone
	if (WeaponToAttach->GetThirdPersonMesh())
	{
		WeaponToAttach->GetThirdPersonMesh()->AttachToComponent(
			DroneMesh,
			AttachmentRule,
			NAME_None
		);

		// Offset weapon to be visible below drone
		WeaponToAttach->GetThirdPersonMesh()->SetRelativeLocation(FVector(CollisionRadius * 0.8f, 0.0f, -CollisionRadius * 0.5f));
		WeaponToAttach->GetThirdPersonMesh()->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));
	}
}

FVector AFlyingDrone::GetWeaponTargetLocation()
{
	// Drones aim from their center position
	const FVector AimSource = GetActorLocation();

	FVector AimDir, AimTarget = FVector::ZeroVector;

	// Do we have an aim target? (using weak pointer)
	AActor* Target = CurrentAimTarget.Get();
	if (Target && !Target->IsPendingKillPending())
	{
		// Target the actor location
		AimTarget = Target->GetActorLocation();

		// Apply a vertical offset to target head/body
		AimTarget.Z += FMath::RandRange(MinAimOffsetZ, MaxAimOffsetZ);

		// Use AccuracyComponent for spread calculation
		if (AccuracyComponent)
		{
			AimDir = AccuracyComponent->CalculateAimDirection(AimTarget, Target);
		}
		else
		{
			// Fallback if component is missing
			AimDir = (AimTarget - AimSource).GetSafeNormal();
		}
	}
	else
	{
		// No aim target, use forward direction with accuracy spread
		if (AccuracyComponent)
		{
			AimDir = AccuracyComponent->CalculateAimDirection(
				AimSource + GetActorForwardVector() * AimRange,
				nullptr
			);
		}
		else
		{
			AimDir = GetActorForwardVector();
		}
	}

	// Calculate the unobstructed aim target location
	AimTarget = AimSource + (AimDir * AimRange);

	// Run a visibility trace to see if there's obstructions
	FHitResult OutHit;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	GetWorld()->LineTraceSingleByChannel(OutHit, AimSource, AimTarget, ECC_Visibility, QueryParams);

	// Return either the impact point or the trace end
	return OutHit.bBlockingHit ? OutHit.ImpactPoint : OutHit.TraceEnd;
}

// ==================== Movement Interface ====================

void AFlyingDrone::FlyTo(const FVector& Location)
{
	if (FlyingMovement && !bIsDead)
	{
		FlyingMovement->FlyToLocation(Location);
	}
}

void AFlyingDrone::FlyToTarget(AActor* Target)
{
	if (FlyingMovement && !bIsDead && Target)
	{
		FlyingMovement->FlyToActor(Target);
	}
}

bool AFlyingDrone::PerformEvasion(const FVector& ThreatLocation)
{
	if (FlyingMovement && !bIsDead)
	{
		return FlyingMovement->StartEvasiveDash(ThreatLocation);
	}
	return false;
}

void AFlyingDrone::StartPatrol()
{
	if (FlyingMovement && !bIsDead)
	{
		bIsPatrolling = true;

		FVector PatrolPoint;
		if (FlyingMovement->GetRandomPatrolPoint(PatrolPoint))
		{
			FlyingMovement->FlyToLocation(PatrolPoint);
		}
	}
}

void AFlyingDrone::StopPatrol()
{
	bIsPatrolling = false;
	StopMovement();
}

void AFlyingDrone::OnMovementCompleted(bool bSuccess)
{
	// If we're in patrol mode, pick a new patrol point
	if (bIsPatrolling && !bIsDead)
	{
		FVector PatrolPoint;
		if (FlyingMovement && FlyingMovement->GetRandomPatrolPoint(PatrolPoint))
		{
			FlyingMovement->FlyToLocation(PatrolPoint);
		}
	}
}

void AFlyingDrone::StopMovement()
{
	if (FlyingMovement)
	{
		FlyingMovement->StopMovement();
	}
}

// ==================== State Queries ====================

bool AFlyingDrone::IsFlying() const
{
	return FlyingMovement && FlyingMovement->IsMoving();
}

bool AFlyingDrone::IsDashing() const
{
	return FlyingMovement && FlyingMovement->IsDashing();
}

// ==================== Combat ====================

void AFlyingDrone::EngageTarget(AActor* Target)
{
	if (!Target || bIsDead)
	{
		return;
	}

	// Spawn muzzle flash when starting to shoot
	SpawnMuzzleFlashEffect();

	// Play shoot sound
	if (ShootSound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), ShootSound, GetActorLocation());
	}

	StartShooting(Target);
}

void AFlyingDrone::DisengageTarget()
{
	if (bIsShooting)
	{
		StopShooting();
	}
}

bool AFlyingDrone::HasLineOfSightTo(AActor* Target) const
{
	if (!Target || !GetWorld())
	{
		return false;
	}

	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	const FVector Start = GetActorLocation();
	const FVector End = Target->GetActorLocation();

	bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams);

	if (bHit)
	{
		return Hit.GetActor() == Target;
	}

	return true;
}

void AFlyingDrone::UpdateCombat()
{
	if (bIsDead || !bAutoEngage)
	{
		return;
	}

	// Check if current target is still valid (using weak pointer)
	AActor* Target = CurrentAimTarget.Get();
	if (Target && !Target->IsPendingKillPending())
	{
		float DistanceToTarget = FVector::Dist(GetActorLocation(), Target->GetActorLocation());

		if (DistanceToTarget > EngageRange || !HasLineOfSightTo(Target))
		{
			DisengageTarget();
		}
	}

	// If not shooting, look for new target
	if (!bIsShooting)
	{
		AActor* NewTarget = FindClosestEnemy();
		if (NewTarget)
		{
			EngageTarget(NewTarget);
		}
	}
}

AActor* AFlyingDrone::FindClosestEnemy() const
{
	if (!GetWorld())
	{
		return nullptr;
	}

	AActor* ClosestEnemy = nullptr;
	float ClosestDistance = EngageRange;

	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(), EnemyTag, FoundActors);

	for (AActor* Actor : FoundActors)
	{
		if (!Actor || Actor == this)
		{
			continue;
		}

		float Distance = FVector::Dist(GetActorLocation(), Actor->GetActorLocation());

		if (Distance < ClosestDistance && HasLineOfSightTo(Actor))
		{
			ClosestDistance = Distance;
			ClosestEnemy = Actor;
		}
	}

	return ClosestEnemy;
}

// ==================== VFX ====================

void AFlyingDrone::SpawnExplosionEffect()
{
	if (!ExplosionFX || !GetWorld())
	{
		return;
	}

	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		ExplosionFX,
		GetActorLocation(),
		GetActorRotation(),
		FVector(ExplosionFXScale),
		true,  // Auto destroy
		true,  // Auto activate
		ENCPoolMethod::None,
		true   // Pre cull check
	);
}

void AFlyingDrone::SpawnMuzzleFlashEffect()
{
	if (!MuzzleFlashFX || !GetWorld())
	{
		return;
	}

	// Calculate muzzle position (offset from drone center in local space)
	const FVector WorldOffset = GetActorRotation().RotateVector(MuzzleFlashOffset);
	const FVector MuzzleLocation = GetActorLocation() + WorldOffset;

	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		MuzzleFlashFX,
		MuzzleLocation,
		GetActorRotation(),
		FVector(MuzzleFlashScale),
		true,  // Auto destroy
		true,  // Auto activate
		ENCPoolMethod::None,
		true   // Pre cull check
	);
}

// ==================== Visual Updates ====================

void AFlyingDrone::UpdateDroneVisuals(float DeltaTime)
{
	UpdateDroneRotation(DeltaTime);
}

void AFlyingDrone::UpdateDroneRotation(float DeltaTime)
{
	// Rotate drone to face target or movement direction
	FRotator TargetRotation = GetActorRotation();

	// Check target validity using weak pointer
	AActor* Target = CurrentAimTarget.Get();
	if (Target && !Target->IsPendingKillPending())
	{
		// Face the target we're shooting at
		const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
		TargetRotation = ToTarget.Rotation();
		TargetRotation.Pitch = 0.0f; // Keep drone level (only yaw)
	}
	else if (FlyingMovement && FlyingMovement->IsMoving())
	{
		// Face movement direction
		const FVector Velocity = GetVelocity();
		if (!Velocity.IsNearlyZero())
		{
			TargetRotation = Velocity.Rotation();
			TargetRotation.Pitch = 0.0f;
		}
	}

	// Smoothly interpolate rotation
	const FRotator NewRotation = FMath::RInterpTo(GetActorRotation(), TargetRotation, DeltaTime, 5.0f);
	SetActorRotation(NewRotation);
}

void AFlyingDrone::ApplyKnockback(const FVector& InKnockbackDirection, float Distance, float Duration, const FVector& AttackerLocation)
{
	// Apply NPC's knockback distance multiplier (inherited from ShooterNPC)
	float FinalDistance = Distance * KnockbackDistanceMultiplier;

	// Don't apply knockback if distance is negligible
	if (FinalDistance < 1.0f)
	{
		return;
	}

	// Mark as in knockback state
	bIsInKnockback = true;

	// Calculate velocity needed to cover the distance in the given duration
	// Velocity = Distance / Time
	FVector KnockbackVelocity = InKnockbackDirection.GetSafeNormal() * (FinalDistance / Duration);

	// Stop flying movement
	if (FlyingMovement)
	{
		FlyingMovement->StopMovement();
	}

	// Disable EMF forces during knockback for consistent physics
	if (bDisableEMFDuringKnockback && EMFVelocityModifier)
	{
		EMFVelocityModifier->SetEnabled(false);
	}

	// Apply knockback using LaunchCharacter (velocity-based, works with physics)
	LaunchCharacter(KnockbackVelocity, true, true);

	// Clear any existing stun timer
	GetWorld()->GetTimerManager().ClearTimer(KnockbackStunTimer);

	// Schedule stun end
	GetWorld()->GetTimerManager().SetTimer(
		KnockbackStunTimer,
		this,
		&AFlyingDrone::EndKnockbackStun,
		Duration,
		false
	);

#if WITH_EDITOR
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
			FString::Printf(TEXT("Drone Knockback: Vel=(%.2f,%.2f,%.2f), Dist=%.0f, Duration=%.2f"),
				KnockbackVelocity.X, KnockbackVelocity.Y, KnockbackVelocity.Z,
				FinalDistance, Duration));
	}
#endif
}