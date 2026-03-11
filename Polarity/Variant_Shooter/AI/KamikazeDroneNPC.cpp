// KamikazeDroneNPC.cpp

#include "KamikazeDroneNPC.h"
#include "FlyingAIMovementComponent.h"
#include "FPVTiltComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/DamageEvents.h"
#include "TimerManager.h"
#include "NiagaraFunctionLibrary.h"
#include "AIController.h"
#include "Engine/OverlapResult.h"
#include "EMFVelocityModifier.h"
#include "EMF_FieldComponent.h"
#include "../DamageTypes/DamageType_Melee.h"
#include "../DamageTypes/DamageType_Wallslam.h"
#include "../DamageTypes/DamageType_EMFProximity.h"
#include "../DamageTypes/DamageType_KamikazeExplosion.h"
#include "../Pickups/HealthPickup.h"
#include "ShooterGameMode.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Field/FieldSystemObjects.h"

AKamikazeDroneNPC::AKamikazeDroneNPC(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Create flying movement component
	FlyingMovement = CreateDefaultSubobject<UFlyingAIMovementComponent>(TEXT("FlyingMovement"));

	// Create sphere collision
	DroneCollision = CreateDefaultSubobject<USphereComponent>(TEXT("DroneCollision"));
	DroneCollision->InitSphereRadius(CollisionRadius);
	DroneCollision->SetCollisionProfileName(FName("OverlapAllDynamic"));
	DroneCollision->SetupAttachment(RootComponent);

	// Create visual mesh
	DroneMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DroneMesh"));
	DroneMesh->SetupAttachment(DroneCollision);
	DroneMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Load placeholder sphere mesh
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere"));
	if (SphereMesh.Succeeded())
	{
		DroneMesh->SetStaticMesh(SphereMesh.Object);
		const float MeshScale = (CollisionRadius * 2.0f) / 100.0f;
		DroneMesh->SetRelativeScale3D(FVector(MeshScale));
	}

	// Create FPV tilt component
	FPVTilt = CreateDefaultSubobject<UFPVTiltComponent>(TEXT("FPVTilt"));

	// Configure CapsuleComponent as sphere for movement collision
	GetCapsuleComponent()->SetCapsuleSize(CollisionRadius, CollisionRadius);
	GetCapsuleComponent()->SetCollisionProfileName(FName("Pawn"));

	// Hide character meshes (we use DroneMesh)
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

	// Kamikaze is light — resists knockback less
	KnockbackDistanceMultiplier = 0.5f;

	// Low HP
	CurrentHP = 40.0f;

	// No weapon
	WeaponClass = nullptr;

	// No perception delay — telegraph serves that role
	PerceptionDelay = 0.0f;
}

// ==================== Lifecycle ====================

void AKamikazeDroneNPC::BeginPlay()
{
	Super::BeginPlay();

	// Initialize per-instance random
	InstanceRandom.Initialize(GetUniqueID());

	// Update collision sizes
	if (DroneCollision)
	{
		DroneCollision->SetSphereRadius(CollisionRadius);
	}
	GetCapsuleComponent()->SetCapsuleSize(CollisionRadius, CollisionRadius);

	// Update mesh scale
	if (DroneMesh && DroneMesh->GetStaticMesh())
	{
		const float MeshScale = (CollisionRadius * 2.0f) / 100.0f;
		DroneMesh->SetRelativeScale3D(FVector(MeshScale));
	}

	// Initialize FPV tilt
	if (FPVTilt)
	{
		FPVTilt->Initialize(DroneMesh, AttackSpeed, GetUniqueID());
	}

	// Initialize orbit
	CurrentOrbitRadius = OrbitStartRadius;
	OrbitAngle = InstanceRandom.FRandRange(0.0f, UE_TWO_PI);
	OrbitHeightPhaseOffset = InstanceRandom.FRandRange(0.0f, UE_TWO_PI);
	SpeedNoiseTimeOffset = InstanceRandom.FRandRange(0.0f, 100.0f);

	// Initialize orbit center to player position
	if (APawn* Player = GetPlayerPawn())
	{
		OrbitCenter = Player->GetActorLocation();
	}
	else
	{
		OrbitCenter = GetActorLocation();
	}

	CurrentState = EKamikazeState::Orbiting;
}

void AKamikazeDroneNPC::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bIsDead)
	{
		return;
	}

	// Reset per-frame flags
	bTookDamageThisFrame = false;

	// State machine
	switch (CurrentState)
	{
	case EKamikazeState::Orbiting:
		UpdateOrbiting(DeltaTime);
		break;
	case EKamikazeState::Telegraphing:
		UpdateTelegraphing(DeltaTime);
		break;
	case EKamikazeState::Attacking:
		UpdateAttacking(DeltaTime);
		break;
	case EKamikazeState::PostAttack:
		UpdatePostAttack(DeltaTime);
		break;
	case EKamikazeState::Recovery:
		UpdateRecovery(DeltaTime);
		break;
	default:
		break;
	}

	// Update FPV tilt every frame
	if (FPVTilt && !bIsDead)
	{
		const FVector Vel = GetVelocity();
		const float Speed = Vel.Size();
		// Approximate acceleration from velocity change (CMC doesn't expose it directly)
		const FVector Accel = GetCharacterMovement() ? GetCharacterMovement()->GetCurrentAcceleration() : FVector::ZeroVector;
		FPVTilt->SetMovementState(Speed, Vel, Accel);
	}

	// Orient actor to face velocity direction (yaw only)
	const FVector Vel = GetVelocity();
	if (!Vel.IsNearlyZero(10.0f))
	{
		const FRotator CurrentRot = GetActorRotation();
		const FRotator TargetRot = Vel.Rotation();
		// Use yaw interp speed appropriate to current state
		const float YawSpeed = (CurrentState == EKamikazeState::Attacking || CurrentState == EKamikazeState::PostAttack)
			? AttackTurnRate * AttackTurnRateMultiplier
			: OrbitMaxTurnRate;
		const FRotator NewRot = FMath::RInterpTo(CurrentRot, FRotator(0.0f, TargetRot.Yaw, 0.0f), DeltaTime, YawSpeed / 45.0f);
		SetActorRotation(NewRot);
	}
}

// ==================== State Machine ====================

void AKamikazeDroneNPC::SetState(EKamikazeState NewState)
{
	CurrentState = NewState;
	StateTimer = 0.0f;
}

void AKamikazeDroneNPC::UpdateOrbiting(float DeltaTime)
{
	APawn* Player = GetPlayerPawn();
	if (!Player)
	{
		return;
	}

	// --- Update orbit center (smoothed tracking of player) ---
	TimeSinceOrbitCenterUpdate += DeltaTime;
	if (TimeSinceOrbitCenterUpdate >= OrbitCenterUpdateInterval)
	{
		TimeSinceOrbitCenterUpdate = 0.0f;
		const FVector PlayerPos = Player->GetActorLocation();
		OrbitCenter = FMath::VInterpTo(OrbitCenter, PlayerPos, OrbitCenterUpdateInterval, 500.0f / OrbitCenterUpdateInterval);
	}

	// --- Advance orbit angle ---
	const float SpeedNoise = SpeedNoiseAmplitude * FMath::Sin((GetWorld()->GetTimeSeconds() + SpeedNoiseTimeOffset) * 2.7f);
	const float EffectiveSpeed = CruiseSpeed * (1.0f + SpeedNoise);
	const float AngularSpeed = EffectiveSpeed / FMath::Max(CurrentOrbitRadius, 100.0f);
	OrbitAngle += AngularSpeed * DeltaTime;

	// --- Lap completion: shrink radius ---
	if (OrbitAngle >= UE_TWO_PI)
	{
		OrbitAngle -= UE_TWO_PI;
		CurrentOrbitRadius = FMath::Max(CurrentOrbitRadius - OrbitShrinkPerLap, OrbitMinRadius);
	}

	// --- Calculate target position on elliptical orbit ---
	const float SemiMajor = CurrentOrbitRadius;
	const float SemiMinor = CurrentOrbitRadius * (1.0f - OrbitEccentricity);
	const float TargetX = OrbitCenter.X + SemiMajor * FMath::Cos(OrbitAngle);
	const float TargetY = OrbitCenter.Y + SemiMinor * FMath::Sin(OrbitAngle);

	// Vertical sinusoid
	const float HeightOscillation = OrbitHeightAmplitude * FMath::Sin(OrbitAngle + OrbitHeightPhaseOffset);
	const float TargetZ = OrbitCenter.Z + OrbitBaseHeight + HeightOscillation;

	const FVector TargetOrbitPos(TargetX, TargetY, TargetZ);

	// --- Geometry check ---
	TimeSinceGeometryCheck += DeltaTime;
	if (TimeSinceGeometryCheck >= GeometryCheckInterval)
	{
		TimeSinceGeometryCheck = 0.0f;

		const FVector ForwardDir = (TargetOrbitPos - GetActorLocation()).GetSafeNormal();
		FHitResult Hit;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);

		if (GetWorld()->LineTraceSingleByChannel(Hit, GetActorLocation(),
			GetActorLocation() + ForwardDir * 200.0f, ECC_WorldStatic, QueryParams))
		{
			// Obstacle ahead — track time unable to orbit
			OrbitForcedTimer += GeometryCheckInterval;
		}
		else
		{
			OrbitForcedTimer = FMath::Max(OrbitForcedTimer - GeometryCheckInterval, 0.0f);
		}

		// Forced attack if can't maintain orbit for 1.5s
		if (OrbitForcedTimer >= 1.5f && CurrentOrbitRadius <= MinOrbitSpaceThreshold)
		{
			bOrbitForced = true;
			BeginTelegraph(false);
			return;
		}
	}

	// --- Move drone toward orbit position ---
	const FVector MoveDir = (TargetOrbitPos - GetActorLocation()).GetSafeNormal();
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->AddInputVector(MoveDir);
		CMC->MaxFlySpeed = EffectiveSpeed;
	}

	// --- Proximity attack check ---
	const float DistToPlayer = FVector::Dist(GetActorLocation(), Player->GetActorLocation());
	if (CurrentOrbitRadius <= ProximityAttackRadius)
	{
		ProximityTimer += DeltaTime;
		if (ProximityTimer >= ProximityAttackDelay)
		{
			// Emergency attack — stuck at close range too long
			BeginTelegraph(false);
			return;
		}
	}
	else
	{
		ProximityTimer = 0.0f;
	}
}

void AKamikazeDroneNPC::UpdateTelegraphing(float DeltaTime)
{
	StateTimer += DeltaTime;

	// During telegraph: face toward player aggressively
	if (APawn* Player = GetPlayerPawn())
	{
		const FVector ToPlayer = (Player->GetActorLocation() - GetActorLocation()).GetSafeNormal();
		const FRotator TargetRot = ToPlayer.Rotation();
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.0f, TargetRot.Yaw, 0.0f), DeltaTime, 15.0f));
	}

	// Still moving on orbit during telegraph (but slowing to a more predictable path)
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->MaxFlySpeed = CruiseSpeed * 0.5f;
	}

	if (StateTimer >= TelegraphDuration)
	{
		CommitAttack();
	}
}

void AKamikazeDroneNPC::UpdateAttacking(float DeltaTime)
{
	StateTimer += DeltaTime;

	// Move toward attack target with limited steering
	const FVector CurrentDir = GetVelocity().GetSafeNormal();
	const FVector DesiredDir = (AttackTargetPosition - GetActorLocation()).GetSafeNormal();

	// Limited turn rate during attack
	const float MaxTurnThisFrame = FMath::DegreesToRadians(AttackTurnRate * AttackTurnRateMultiplier) * DeltaTime;
	const FVector SteeringDir = FMath::VInterpNormalRotationTo(CurrentDir, DesiredDir, DeltaTime, AttackTurnRate * AttackTurnRateMultiplier);

	AttackDirection = SteeringDir;

	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->MaxFlySpeed = AttackSpeed;
		CMC->AddInputVector(SteeringDir);
	}

	// --- Check player collision (sphere overlap) ---
	if (APawn* Player = GetPlayerPawn())
	{
		const float DistToPlayer = FVector::Dist(GetActorLocation(), Player->GetActorLocation());
		if (DistToPlayer < CollisionRadius + 60.0f) // Player capsule radius ~34
		{
			TriggerCollisionExplosion();
			KamikazeDie();
			return;
		}
	}

	// --- Check if we've passed the target ---
	const FVector ToTarget = AttackTargetPosition - GetActorLocation();
	if (FVector::DotProduct(ToTarget, AttackDirection) < 0.0f)
	{
		// Passed the target — enter post-attack inertia
		SetState(EKamikazeState::PostAttack);
	}
}

void AKamikazeDroneNPC::UpdatePostAttack(float DeltaTime)
{
	StateTimer += DeltaTime;

	// Continue at attack speed along current direction
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->MaxFlySpeed = AttackSpeed;
		CMC->AddInputVector(AttackDirection);
	}

	// Raycast forward for geometry collision
	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	if (GetWorld()->LineTraceSingleByChannel(Hit, GetActorLocation(),
		GetActorLocation() + AttackDirection * 500.0f, ECC_WorldStatic, QueryParams))
	{
		// Will crash into geometry
		TriggerCrashExplosion();
		KamikazeDie();
		return;
	}

	// After inertia time: roll crash chance or recover
	if (StateTimer >= PostAttackInertiaTime)
	{
		// Calculate crash chance
		const float Speed = GetVelocity().Size();
		const float SpeedFactor = FMath::Clamp((Speed - CruiseSpeed) / AttackSpeed, 0.0f, 1.0f) * SpeedCrashFactor;

		float PitchAngle = 0.0f;
		if (FPVTilt)
		{
			// Use the current mesh pitch as proxy
			PitchAngle = DroneMesh ? FMath::Abs(DroneMesh->GetRelativeRotation().Pitch) : 0.0f;
		}
		const float AngleFactor = (PitchAngle / 90.0f) * AngleCrashFactor;

		const float CrashChance = BaseCrashChance + SpeedFactor + AngleFactor;

		if (InstanceRandom.FRand() < CrashChance)
		{
			// Random crash — lost control
			TriggerCrashExplosion();
			KamikazeDie();
			return;
		}

		// Recovery
		SetState(EKamikazeState::Recovery);
	}
}

void AKamikazeDroneNPC::UpdateRecovery(float DeltaTime)
{
	StateTimer += DeltaTime;

	// Decelerate and turn back toward orbit
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		// Gradually reduce speed back to cruise
		const float TargetSpeed = FMath::FInterpTo(CMC->MaxFlySpeed, CruiseSpeed, DeltaTime, 3.0f);
		CMC->MaxFlySpeed = TargetSpeed;

		// Turn toward orbit center
		if (APawn* Player = GetPlayerPawn())
		{
			const FVector ToPlayer = (Player->GetActorLocation() - GetActorLocation()).GetSafeNormal();
			CMC->AddInputVector(ToPlayer);
		}
	}

	// Recovery takes ~2 seconds, then return to orbit
	if (StateTimer >= 2.0f)
	{
		// Reset orbit
		CurrentOrbitRadius = OrbitStartRadius;
		OrbitForcedTimer = 0.0f;
		bOrbitForced = false;
		bIsRetaliating = false;
		ProximityTimer = 0.0f;

		// Recalculate orbit angle based on current position relative to orbit center
		const FVector Offset = GetActorLocation() - OrbitCenter;
		OrbitAngle = FMath::Atan2(Offset.Y, Offset.X);

		SetState(EKamikazeState::Orbiting);
	}
}

// ==================== Attack ====================

void AKamikazeDroneNPC::BeginTelegraph(bool bRetaliation)
{
	if (CurrentState != EKamikazeState::Orbiting)
	{
		return;
	}

	bIsRetaliating = bRetaliation;
	SetState(EKamikazeState::Telegraphing);

	// Play telegraph sound
	if (TelegraphSound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), TelegraphSound, GetActorLocation());
	}
}

void AKamikazeDroneNPC::CommitAttack()
{
	// Calculate predicted target position
	AttackTargetPosition = CalculatePredictedPosition();
	AttackDirection = (AttackTargetPosition - GetActorLocation()).GetSafeNormal();

	SetState(EKamikazeState::Attacking);
}

FVector AKamikazeDroneNPC::CalculatePredictedPosition() const
{
	APawn* Player = GetPlayerPawn();
	if (!Player)
	{
		return GetActorLocation() + GetActorForwardVector() * 500.0f;
	}

	const FVector PlayerPos = Player->GetActorLocation();

	if (PredictionOrder == 0)
	{
		// Zero order: aim at current position
		return PlayerPos;
	}

	// First order: pos + vel * timeToImpact
	const float Distance = FVector::Dist(GetActorLocation(), PlayerPos);
	const float TimeToImpact = Distance / FMath::Max(AttackSpeed, 1.0f);
	const FVector PlayerVel = Player->GetVelocity();

	return PlayerPos + PlayerVel * TimeToImpact;
}

// ==================== Damage ====================

float AKamikazeDroneNPC::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (bIsDead)
	{
		return 0.0f;
	}

	// Mark damage for StateTree
	bTookDamageThisFrame = true;
	LastDamageTakenTime = GetWorld()->GetTimeSeconds();

	// Friendly fire filter (same pattern as FlyingDrone)
	if (DamageCauser)
	{
		bool bIsCollisionDamage = DamageEvent.DamageTypeClass &&
			(DamageEvent.DamageTypeClass->IsChildOf(UDamageType_Wallslam::StaticClass()) ||
			 DamageEvent.DamageTypeClass->IsChildOf(UDamageType_EMFProximity::StaticClass()));

		if (!bIsCollisionDamage)
		{
			AActor* DamageOwner = DamageCauser->GetOwner();
			if (Cast<AShooterNPC>(DamageCauser) || Cast<AShooterNPC>(DamageOwner))
			{
				return 0.0f;
			}
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

	// Handle melee charge transfer
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
				EMFVelocityModifier->SetCharge(OldCharge + ChargeToAdd);
			}
		}
	}

	// Broadcast damage event
	FVector HitLocation = GetActorLocation() + FVector(0.0f, 0.0f, 30.0f);
	OnDamageTaken.Broadcast(this, Damage, DamageEvent.DamageTypeClass, HitLocation, DamageCauser);

	// Retaliation: if hit during orbit → immediate attack
	if (bRetaliateOnDamage && CurrentState == EKamikazeState::Orbiting)
	{
		BeginTelegraph(true);
	}

	// Check for death
	if (CurrentHP <= 0.0f)
	{
		LastKillingDamageType = DamageEvent.DamageTypeClass;
		LastKillingDamageCauser = DamageCauser;

		if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
		{
			const FRadialDamageEvent& RadialEvent = static_cast<const FRadialDamageEvent&>(DamageEvent);
			LastKillingHitDirection = (GetActorLocation() - RadialEvent.Origin).GetSafeNormal();
		}
		else if (DamageCauser)
		{
			LastKillingHitDirection = (GetActorLocation() - DamageCauser->GetActorLocation()).GetSafeNormal();
		}

		KamikazeDie();
	}

	return Damage;
}

// ==================== Death ====================

void AKamikazeDroneNPC::KamikazeDie()
{
	if (bIsDead || bDeathSequenceStarted)
	{
		return;
	}

	bDeathSequenceStarted = true;
	bIsDead = true;

	// Stop shooting (no weapon, but base class safety)
	StopShooting();

	// Unregister from coordinator
	UnregisterFromCoordinator();

	// Increment score
	if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->IncrementTeamScore(TeamByte);
	}

	// Broadcast death
	OnNPCDeath.Broadcast(this);
	OnNPCDeathDetailed.Broadcast(this, LastKillingDamageType, LastKillingDamageCauser);

	// Death behavior depends on state at death
	switch (CurrentState)
	{
	case EKamikazeState::Orbiting:
	case EKamikazeState::Recovery:
		// Killed on orbit — debris fall, no explosion, YES HP pickup
		TriggerDebrisFall();
		break;

	case EKamikazeState::Telegraphing:
	case EKamikazeState::Attacking:
	{
		// Killed during attack — check distance to player
		if (APawn* Player = GetPlayerPawn())
		{
			const float DistToPlayer = FVector::Dist(GetActorLocation(), Player->GetActorLocation());
			if (DistToPlayer <= AttackDeathDistanceThreshold)
			{
				// Close to player — air explosion
				TriggerAirExplosion();
				break;
			}
		}
		// Far from player — just debris
		TriggerDebrisFall();
		break;
	}

	case EKamikazeState::PostAttack:
		// After miss — treat as debris (already past target)
		TriggerDebrisFall();
		break;

	default:
		TriggerDebrisFall();
		break;
	}

	// Spawn death gibs
	if (DeathGeometryCollection)
	{
		const FDeathModeConfig& DeathConfig = ResolveDeathConfig();
		SpawnDeathGeometryCollection(DeathConfig);
	}

	// Aggressive deactivation
	DeactivateAllSystems();

	// Schedule destruction
	GetWorld()->GetTimerManager().SetTimer(
		DeathSequenceTimer, this, &AKamikazeDroneNPC::DeathDestroy, 0.5f, false);
}

void AKamikazeDroneNPC::TriggerDebrisFall()
{
	// No explosion — just enable gravity so debris/mesh falls
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->GravityScale = 1.0f;
		CMC->SetMovementMode(MOVE_Falling);
	}

	// Drop HP pickups (reward for killing)
	if (HealthPickupClass)
	{
		AHealthPickup::SpawnHealthPickups(GetWorld(), HealthPickupClass, GetActorLocation(),
			HealthPickupDropCount, HealthPickupScatterRadius, HealthPickupFloorOffset);
	}
}

void AKamikazeDroneNPC::TriggerAirExplosion()
{
	// Smaller explosion when killed during attack approach
	DoExplosion(CrashExplosionRadius, CrashDamage, UDamageType_KamikazeExplosion::StaticClass(), true);
}

void AKamikazeDroneNPC::TriggerCrashExplosion()
{
	// Crash into geometry — explosion but NO HP pickup
	DoExplosion(CrashExplosionRadius, CrashDamage, UDamageType_KamikazeExplosion::StaticClass(), false);

	// Play crash sound
	if (CrashSound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), CrashSound, GetActorLocation());
	}
}

void AKamikazeDroneNPC::TriggerCollisionExplosion()
{
	// Direct hit on player — full explosion
	DoExplosion(ExplosionRadius, CollisionDamage, UDamageType_KamikazeExplosion::StaticClass(), true);
}

void AKamikazeDroneNPC::DoExplosion(float Radius, float Damage, TSubclassOf<UDamageType> DamageTypeClass, bool bDropHealthPickup)
{
	// Spawn VFX
	UNiagaraSystem* FXToUse = (Radius >= ExplosionRadius) ? ExplosionFX : CrashExplosionFX;
	if (FXToUse)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), FXToUse, GetActorLocation());
	}

	// Play sound
	if (ExplosionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), ExplosionSound, GetActorLocation());
	}

	// Apply radial damage (same manual overlap pattern as FlyingDrone::TriggerExplosion)
	if (Damage > 0.0f && Radius > 0.0f)
	{
		const FVector Origin = GetActorLocation();

		TArray<FOverlapResult> Overlaps;
		FCollisionShape Sphere = FCollisionShape::MakeSphere(Radius);
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);

		GetWorld()->OverlapMultiByChannel(Overlaps, Origin, FQuat::Identity, ECC_Pawn, Sphere, QueryParams);

		// Use PlayerPawn as DamageCauser to bypass NPC friendly-fire and show damage numbers
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

			// LOS check
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

			// Linear falloff
			const float Distance = FVector::Dist(Origin, HitActor->GetActorLocation());
			const float DamageScale = FMath::Clamp(1.0f - Distance / Radius, 0.0f, 1.0f);
			const float FinalDamage = Damage * DamageScale;

			if (FinalDamage <= 0.0f)
			{
				continue;
			}

			FRadialDamageEvent RadialDamageEvent;
			RadialDamageEvent.DamageTypeClass = DamageTypeClass;
			RadialDamageEvent.Origin = Origin;
			RadialDamageEvent.Params.BaseDamage = Damage;
			RadialDamageEvent.Params.OuterRadius = Radius;
			HitActor->TakeDamage(FinalDamage, RadialDamageEvent, nullptr, PlayerPawn);
		}
	}

	// Stun nearby NPCs
	if (Radius > 0.0f)
	{
		TArray<FOverlapResult> StunOverlaps;
		FCollisionShape StunSphere = FCollisionShape::MakeSphere(Radius);
		FCollisionQueryParams StunQueryParams;
		StunQueryParams.AddIgnoredActor(this);

		GetWorld()->OverlapMultiByChannel(
			StunOverlaps, GetActorLocation(), FQuat::Identity,
			ECC_Pawn, StunSphere, StunQueryParams);

		TSet<AShooterNPC*> StunnedNPCs;

		for (const FOverlapResult& Overlap : StunOverlaps)
		{
			AShooterNPC* NPC = Cast<AShooterNPC>(Overlap.GetActor());
			if (!NPC || StunnedNPCs.Contains(NPC) || NPC->IsDead() || NPC == this)
			{
				continue;
			}
			StunnedNPCs.Add(NPC);
			NPC->ApplyExplosionStun(ExplosionStunDuration, ExplosionStunMontage);
		}
	}

	// Drop HP pickups
	if (bDropHealthPickup && HealthPickupClass)
	{
		AHealthPickup::SpawnHealthPickups(GetWorld(), HealthPickupClass, GetActorLocation(),
			HealthPickupDropCount, HealthPickupScatterRadius, HealthPickupFloorOffset);
	}

	// Hide mesh (explosion replaces it)
	if (DroneMesh)
	{
		DroneMesh->SetVisibility(false);
	}
}

void AKamikazeDroneNPC::DeactivateAllSystems()
{
	// Disable collision
	if (DroneCollision)
	{
		DroneCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Disable movement
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

	// Disable EMF
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->SetCharge(0.0f);
		EMFVelocityModifier->SetComponentTickEnabled(false);
	}
	if (FieldComponent)
	{
		FieldComponent->UnregisterFromRegistry();
		FieldComponent->SetComponentTickEnabled(false);
	}

	// Disable accuracy component (kamikaze doesn't use it, but inherited from ShooterNPC)
	// AccuracyComponent is not used by kamikaze drone — skip

	// Disable FPV tilt
	if (FPVTilt)
	{
		FPVTilt->SetComponentTickEnabled(false);
	}

	// Unpossess
	if (AController* MyController = GetController())
	{
		MyController->UnPossess();
	}

	// Disable actor tick
	SetActorTickEnabled(false);
}

void AKamikazeDroneNPC::DeathDestroy()
{
	Destroy();
}

// ==================== Knockback Overrides (FlyingDrone pattern) ====================

void AKamikazeDroneNPC::ApplyKnockback(const FVector& InKnockbackDirection, float Distance, float Duration, const FVector& AttackerLocation, bool bKeepEMFEnabled)
{
	if (bIsInKnockback)
	{
		return;
	}

	// Stop FlyingMovement
	if (FlyingMovement)
	{
		FlyingMovement->StopMovement();
	}

	// Ignore player collision during knockback
	if (!AttackerLocation.IsZero())
	{
		if (ACharacter* PlayerChar = UGameplayStatics::GetPlayerCharacter(this, 0))
		{
			float DistToAttacker = FVector::Dist(PlayerChar->GetActorLocation(), AttackerLocation);
			if (DistToAttacker < 300.0f)
			{
				KnockbackIgnoreActor = PlayerChar;
				MoveIgnoreActorAdd(PlayerChar);
			}
		}
	}

	Super::ApplyKnockback(InKnockbackDirection, Distance, Duration, AttackerLocation, bKeepEMFEnabled);
}

void AKamikazeDroneNPC::EndKnockbackStun()
{
	// Restore player collision
	if (KnockbackIgnoreActor.IsValid())
	{
		MoveIgnoreActorRemove(KnockbackIgnoreActor.Get());
		KnockbackIgnoreActor.Reset();
	}

	Super::EndKnockbackStun();

	// Restore flying mode
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->SetMovementMode(MOVE_Flying);
	}
}

void AKamikazeDroneNPC::OnCapsuleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// Block parent's OnCapsuleHit during knockback interpolation (same as FlyingDrone)
	if (bIsKnockbackInterpolating)
	{
		return;
	}

	Super::OnCapsuleHit(HitComponent, OtherActor, OtherComp, NormalImpulse, Hit);
}

void AKamikazeDroneNPC::SpawnDeathGeometryCollection(const FDeathModeConfig& Config)
{
	if (!DeathGeometryCollection || !GetWorld())
	{
		return;
	}

	// Use DroneMesh transform instead of SkeletalMesh
	const FTransform MeshTransform = DroneMesh ? DroneMesh->GetComponentTransform()
		: FTransform(GetActorLocation());
	const FVector Origin = MeshTransform.GetLocation();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AGeometryCollectionActor* GCActor = GetWorld()->SpawnActor<AGeometryCollectionActor>(
		Origin, MeshTransform.GetRotation().Rotator(), SpawnParams);

	if (!GCActor)
	{
		return;
	}

	UGeometryCollectionComponent* GCComp = GCActor->GetGeometryCollectionComponent();
	if (!GCComp)
	{
		GCActor->Destroy();
		return;
	}

	// Scale GC to match drone visual mesh
	if (DroneMesh)
	{
		GCActor->SetActorScale3D(DroneMesh->GetComponentScale());
	}

	// Collision: gibs collide with world but not pawns/camera
	GCComp->SetCollisionProfileName(FName("Ragdoll"));
	GCComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

	GCComp->SetRestCollection(DeathGeometryCollection);

	// Copy materials from DroneMesh to GC gibs
	if (DroneMesh)
	{
		const int32 NumMats = DroneMesh->GetNumMaterials();
		for (int32 i = 0; i < NumMats; i++)
		{
			if (UMaterialInterface* Mat = DroneMesh->GetMaterial(i))
			{
				GCComp->SetMaterial(i, Mat);
			}
		}
	}

	GCComp->SetSimulatePhysics(true);
	GCComp->RecreatePhysicsState();

	// Break all clusters
	UUniformScalar* StrainField = NewObject<UUniformScalar>(GCActor);
	StrainField->Magnitude = 999999.0f;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_ExternalClusterStrain,
		nullptr, StrainField);

	// Scatter pieces radially
	URadialVector* RadialVelocity = NewObject<URadialVector>(GCActor);
	RadialVelocity->Magnitude = Config.DismembermentImpulse;
	RadialVelocity->Position = Origin;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_LinearVelocity,
		nullptr, RadialVelocity);

	// Angular velocity for tumbling
	URadialVector* AngularVel = NewObject<URadialVector>(GCActor);
	AngularVel->Magnitude = Config.DismembermentAngularImpulse;
	AngularVel->Position = Origin;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_AngularVelocity,
		nullptr, AngularVel);

	// Directional bias from killing hit direction
	if (!LastKillingHitDirection.IsNearlyZero() && Config.DirectionalBiasMultiplier > 0.0f)
	{
		UUniformVector* DirectionalBias = NewObject<UUniformVector>(GCActor);
		DirectionalBias->Magnitude = Config.DismembermentImpulse * Config.DirectionalBiasMultiplier;
		DirectionalBias->Direction = LastKillingHitDirection;
		GCComp->ApplyPhysicsField(true,
			EGeometryCollectionPhysicsTypeEnum::Chaos_LinearVelocity,
			nullptr, DirectionalBias);
	}

	// Auto-destroy gibs
	GCActor->SetLifeSpan(GibLifetime);
}

// ==================== Public Queries ====================

bool AKamikazeDroneNPC::TookDamageRecently(float GracePeriod) const
{
	return (GetWorld()->GetTimeSeconds() - LastDamageTakenTime) <= GracePeriod;
}

bool AKamikazeDroneNPC::IsInAttackSequence() const
{
	return CurrentState == EKamikazeState::Telegraphing
		|| CurrentState == EKamikazeState::Attacking
		|| CurrentState == EKamikazeState::PostAttack;
}

// ==================== Helpers ====================

APawn* AKamikazeDroneNPC::GetPlayerPawn() const
{
	return UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
}
