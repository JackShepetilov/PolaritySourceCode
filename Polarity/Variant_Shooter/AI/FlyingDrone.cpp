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
#include "EMF_FieldComponent.h"
#include "../DamageTypes/DamageType_Melee.h"
#include "AIController.h"
#include "Engine/OverlapResult.h"

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

	// Drones fly farther and faster when knocked back (lighter than ground NPCs)
	KnockbackDistanceMultiplier = 1.5f;
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

	// OnCapsuleHit is bound in ShooterNPC::BeginPlay() via AddDynamic.
	// UE dynamic delegates resolve by function name through reflection,
	// so our override is called automatically (no rebinding needed).
	// During knockback interpolation, our override blocks OnCapsuleHit to prevent
	// duplicate damage (parent's UpdateKnockbackInterpolation handles wall hits via sweep).

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

	// Mark damage taken for StateTree evasion trigger
	bTookDamageThisFrame = true;
	LastDamageTakenTime = GetWorld()->GetTimeSeconds();

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

	// Handle melee charge transfer (copied from ShooterNPC)
	if (DamageEvent.DamageTypeClass && DamageEvent.DamageTypeClass->IsChildOf(UDamageType_Melee::StaticClass()))
	{
		if (EMFVelocityModifier && EventInstigator)
		{
			APawn* Attacker = EventInstigator->GetPawn();
			if (Attacker)
			{
				UEMFVelocityModifier* AttackerEMF = Attacker->FindComponentByClass<UEMFVelocityModifier>();
				float ChargeToAdd = ChargeChangeOnMeleeHit;

				if (AttackerEMF)
				{
					float AttackerCharge = AttackerEMF->GetCharge();
					ChargeToAdd = -FMath::Abs(ChargeChangeOnMeleeHit) * FMath::Sign(AttackerCharge);

					if (FMath::Abs(AttackerCharge) < KINDA_SMALL_NUMBER)
					{
						ChargeToAdd = ChargeChangeOnMeleeHit;
					}
				}

				float OldCharge = EMFVelocityModifier->GetCharge();
				float NewCharge = OldCharge + ChargeToAdd;
				EMFVelocityModifier->SetCharge(NewCharge);
			}
		}
	}

	// Broadcast damage taken event for damage numbers system
	// Use actor center + offset for hit location (same as ShooterNPC)
	FVector HitLocation = GetActorLocation() + FVector(0.0f, 0.0f, 50.0f);
	OnDamageTaken.Broadcast(this, Damage, DamageEvent.DamageTypeClass, HitLocation, DamageCauser);

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

	// Broadcast death (BP can spawn VFX here)
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

	// ============== AGGRESSIVE DEACTIVATION FOR PERFORMANCE ==============

	// Disable ALL collision immediately
	if (DroneCollision)
	{
		DroneCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Disable movement components
	if (FlyingMovement)
	{
		FlyingMovement->SetComponentTickEnabled(false);
	}
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->StopMovementImmediately();
		CMC->DisableMovement();
		CMC->SetComponentTickEnabled(false);
	}

	// Hide mesh
	if (DroneMesh)
	{
		DroneMesh->SetVisibility(false);
		DroneMesh->SetComponentTickEnabled(false);
	}

	// Disable EMF components and unregister from registry (inherited from ShooterNPC)
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->SetCharge(0.0f);
		EMFVelocityModifier->SetComponentTickEnabled(false);
	}
	if (FieldComponent)
	{
		FieldComponent->UnregisterFromRegistry();  // Immediately remove from EMF calculations
		FieldComponent->SetComponentTickEnabled(false);
	}

	// Disable AI components
	if (AccuracyComponent)
	{
		AccuracyComponent->SetComponentTickEnabled(false);
	}

	// Unpossess to stop AI controller
	if (AController* MyController = GetController())
	{
		MyController->UnPossess();
	}

	// Disable actor tick
	SetActorTickEnabled(false);

	// Destroy weapon
	if (Weapon)
	{
		Weapon->Destroy();
		Weapon = nullptr;
	}

	// Schedule fast destruction
	GetWorld()->GetTimerManager().SetTimer(
		DeathSequenceTimer,
		this,
		&AFlyingDrone::DeathDestroy,
		0.5f,  // Fast destruction instead of DeathEffectDuration
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

	// Apply explosion damage to all actors in radius
	// Manual overlap instead of ApplyRadialDamage: NPC friendly-fire check blocks
	// damage from ShooterNPC-derived sources, so we pass nullptr as instigator/causer
	if (ExplosionDamage > 0.0f && ExplosionRadius > 0.0f)
	{
		const FVector Origin = GetActorLocation();

		TArray<FOverlapResult> Overlaps;
		FCollisionShape Sphere = FCollisionShape::MakeSphere(ExplosionRadius);
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);

		// Sweep Pawn channel (players + NPCs)
		GetWorld()->OverlapMultiByChannel(Overlaps, Origin, FQuat::Identity, ECC_Pawn, Sphere, QueryParams);

		// Player pawn as DamageCauser: not a ShooterNPC, so friendly-fire check passes.
		// DamageNumbersSubsystem recognizes player pawn as "from player" and shows numbers.
		APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);

		TSet<AActor*> DamagedActors;

		for (const FOverlapResult& Overlap : Overlaps)
		{
			AActor* HitActor = Overlap.GetActor();
			if (!HitActor || DamagedActors.Contains(HitActor))
			{
				continue;
			}
			DamagedActors.Add(HitActor);

			// Line-of-sight check (same as ApplyRadialDamage with bDoFullDamage=false)
			FHitResult LOSHit;
			FCollisionQueryParams LOSParams;
			LOSParams.AddIgnoredActor(this);
			LOSParams.AddIgnoredActor(HitActor);
			const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
				LOSHit, Origin, HitActor->GetActorLocation(), ECC_Visibility, LOSParams);
			if (bBlocked)
			{
				continue;
			}

			// Linear falloff: full damage at center, zero at edge
			const float Distance = FVector::Dist(Origin, HitActor->GetActorLocation());
			const float DamageScale = FMath::Clamp(1.0f - Distance / ExplosionRadius, 0.0f, 1.0f);
			const float FinalDamage = ExplosionDamage * DamageScale;

			if (FinalDamage <= 0.0f)
			{
				continue;
			}

			// nullptr instigator bypasses NPC friendly-fire check.
			// PlayerPawn as DamageCauser bypasses Cast<AShooterNPC> and triggers damage numbers.
			FDamageEvent DamageEvent;
			DamageEvent.DamageTypeClass = UDamageType::StaticClass();
			HitActor->TakeDamage(FinalDamage, DamageEvent, nullptr, PlayerPawn);
		}
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

void AFlyingDrone::ApplyKnockback(const FVector& InKnockbackDirection, float Distance, float Duration, const FVector& AttackerLocation, bool bKeepEMFEnabled)
{
	// Don't apply new knockback if already in knockback (prevents jitter from multiple hits)
	if (bIsInKnockback)
	{
#if WITH_EDITOR
		UE_LOG(LogTemp, Warning, TEXT("Drone ApplyKnockback: BLOCKED - already in knockback"));
#endif
		return;
	}

	// Stop FlyingAIMovementComponent before parent takes over
	if (FlyingMovement)
	{
		FlyingMovement->StopMovement();
	}

	// Ignore collision with player during knockback to prevent jitter from dropkick
	if (!AttackerLocation.IsZero())
	{
		if (ACharacter* PlayerChar = UGameplayStatics::GetPlayerCharacter(this, 0))
		{
			float DistToAttacker = FVector::Dist(PlayerChar->GetActorLocation(), AttackerLocation);
			if (DistToAttacker < 300.0f) // Player is the attacker
			{
				KnockbackIgnoreActor = PlayerChar;
				MoveIgnoreActorAdd(PlayerChar);
			}
		}
	}

	// Delegate to parent's interpolation-based knockback system.
	// This uses SetActorLocation() with capsule sweep each frame, which provides:
	// - Reliable wall collision detection (CheckKnockbackWallCollision)
	// - Reliable wall slam damage (HandleKnockbackWallHit with mathematically computed velocity)
	// - Wall bounce with energy loss
	// - NPC-NPC collision detection
	// Gravity is automatically zero because our CMC has GravityScale = 0.
	Super::ApplyKnockback(InKnockbackDirection, Distance, Duration, AttackerLocation, bKeepEMFEnabled);

#if WITH_EDITOR
	if (GEngine)
	{
		float FinalDistance = Distance * KnockbackDistanceMultiplier;
		float Speed = FinalDistance / Duration;
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
			FString::Printf(TEXT("Drone Knockback (interpolated): Dist=%.0f, Duration=%.2f, Speed=%.0f"),
				FinalDistance, Duration, Speed));
	}
#endif
}

void AFlyingDrone::EndKnockbackStun()
{
	// Restore collision with player
	if (KnockbackIgnoreActor.IsValid())
	{
		MoveIgnoreActorRemove(KnockbackIgnoreActor.Get());
		KnockbackIgnoreActor.Reset();
	}

	// Call parent implementation (clears bIsInKnockback, restores friction, re-enables EMF)
	Super::EndKnockbackStun();

	// Restore flying movement mode (parent may have left it in walking/falling)
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->SetMovementMode(MOVE_Flying);
	}
}

void AFlyingDrone::OnCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// During knockback interpolation, wall collisions are handled by the parent's
	// CheckKnockbackWallCollision + HandleKnockbackWallHit in UpdateKnockbackInterpolation.
	// OnCapsuleHit from CMC physics is unreliable (uses post-collision PreviousTickVelocity).
	// So we skip parent's OnCapsuleHit during interpolation to avoid duplicate/incorrect damage.
	if (bIsKnockbackInterpolating)
	{
		return;
	}

	// Outside of knockback interpolation, let parent handle normally
	// (e.g. NPC-NPC collision during launched state)
	Super::OnCapsuleHit(HitComponent, OtherActor, OtherComp, NormalImpulse, Hit);
}

// ==================== StateTree Support ====================

bool AFlyingDrone::CanPerformEvasiveDash() const
{
	if (!GetWorld() || bIsDead)
	{
		return false;
	}

	// Don't evade during knockback (conflicts with knockback physics)
	if (bIsInKnockback)
	{
		return false;
	}

	// Check evasive dash cooldown
	const float CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastEvasiveDashTime < EvasiveDashCooldown)
	{
		return false;
	}

	// Check if FlyingMovement dash is available
	if (FlyingMovement && FlyingMovement->IsDashOnCooldown())
	{
		return false;
	}

	return true;
}

bool AFlyingDrone::PerformRandomEvasiveDash()
{
	if (!CanPerformEvasiveDash() || !FlyingMovement)
	{
		return false;
	}

	// Generate random direction (horizontal with slight vertical variance)
	const float RandomAngle = FMath::RandRange(0.0f, 2.0f * PI);
	const float VerticalComponent = FMath::RandRange(-0.3f, 0.3f);

	FVector DashDirection;
	DashDirection.X = FMath::Cos(RandomAngle);
	DashDirection.Y = FMath::Sin(RandomAngle);
	DashDirection.Z = VerticalComponent;
	DashDirection.Normalize();

	// Attempt dash
	UE_LOG(LogTemp, Warning, TEXT("FlyingDrone::PerformRandomEvasiveDash - Attempting dash in direction (%.2f, %.2f, %.2f)"),
		DashDirection.X, DashDirection.Y, DashDirection.Z);

	if (FlyingMovement->StartDash(DashDirection))
	{
		LastEvasiveDashTime = GetWorld()->GetTimeSeconds();
		UE_LOG(LogTemp, Warning, TEXT("FlyingDrone::PerformRandomEvasiveDash - Dash started successfully!"));
		return true;
	}

	UE_LOG(LogTemp, Warning, TEXT("FlyingDrone::PerformRandomEvasiveDash - StartDash returned false"));
	return false;
}

bool AFlyingDrone::TookDamageRecently(float GracePeriod) const
{
	if (!GetWorld())
	{
		return false;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();
	return (CurrentTime - LastDamageTakenTime) <= GracePeriod;
}