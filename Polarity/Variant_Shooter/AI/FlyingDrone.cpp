// Copyright Epic Games, Inc. All Rights Reserved.

#include "FlyingDrone.h"
#include "HAL/IConsoleManager.h"
#include "Variant_Shooter/Weapons/ShooterWeapon_Melee.h"
#include "FlyingAIMovementComponent.h"
#include "AI/SmokeVisionSubsystem.h"
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
#include "AI/PolarityTeams.h"
#include "AI/AimPoints.h"
#include "ShooterGameMode.h"
#include "EMFVelocityModifier.h"
#include "EMF_FieldComponent.h"
#include "Coop/CoopPlayers.h"
#include "../DamageTypes/DamageType_Melee.h"
#include "../DamageTypes/DamageType_Wallslam.h"
#include "../DamageTypes/DamageType_EMFProximity.h"
#include "AIController.h"
#include "Engine/OverlapResult.h"
#include "Net/UnrealNetwork.h"
#include "../Pickups/HealthPickup.h"
#include "../Weapons/DroppedMeleeWeapon.h"
#include "../Weapons/DroppedRangedWeapon.h"
#include "../DamageTypes/DamageType_DroneExplosion.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Field/FieldSystemObjects.h"
#include "ShooterCharacter.h"
#include "Polarity/Upgrades/UpgradeManagerComponent.h"

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

	// Hull damage-stage FX (activated per stage from UpdateDamageStage / OnRep_DroneDamageStage)
	DamagedHullFXComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("DamagedHullFX"));
	DamagedHullFXComponent->SetupAttachment(DroneMesh);
	DamagedHullFXComponent->bAutoActivate = false;

	CriticalHullFXComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("CriticalHullFX"));
	CriticalHullFXComponent->SetupAttachment(DroneMesh);
	CriticalHullFXComponent->bAutoActivate = false;

	RepairHealFXComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("RepairHealFX"));
	RepairHealFXComponent->SetupAttachment(DroneMesh);
	RepairHealFXComponent->bAutoActivate = false;

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

	// Shoot at the visible body, not at a human chest. The hull sits BELOW the actor origin (mesh
	// bounds centre about -24 with the authored scale) and is only some 60 units tall, so the
	// inherited human offsets put every burst at its lower rim or under it entirely.
	LocalAimPoint = FVector(0.0f, 0.0f, -24.0f);
	AimVerticalJitter = 20.0f;

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

		// Remember the configured fly speed so stage multipliers can restore it exactly
		BaseFlySpeed = FlyingMovement->FlySpeed;
	}

	// Capture spawn HP as the reference for damage-stage fractions
	MaxHPAtSpawn = CurrentHP;

	// Assign hull FX assets (components stay deactivated until the stage demands them)
	if (DamagedHullFXComponent && DamagedHullFX)
	{
		DamagedHullFXComponent->SetAsset(DamagedHullFX);
	}
	if (CriticalHullFXComponent && CriticalHullFX)
	{
		CriticalHullFXComponent->SetAsset(CriticalHullFX);
	}
	if (RepairHealFXComponent && RepairHealFX)
	{
		RepairHealFXComponent->SetAsset(RepairHealFX);
	}

	// Initial stage pass (Intact: no VFX, no speed penalty, but keeps speeds in sync)
	UpdateDamageStage();

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

		if (bIsRepairing)
		{
			TickRepair(DeltaTime);
		}
	}
}

// ==================== Damage & Death Handling ====================

UMeshComponent* AFlyingDrone::GetHitFlashMeshComponent() const
{
	return DroneMesh;
}

void AFlyingDrone::EnterLaunchedState()
{
	Super::EnterLaunchedState();

	// Apply random spin to drone mesh on launch (like prop's ReverseLaunchSpinSpeed)
	if (LaunchSpinSpeed > 0.0f)
	{
		const FVector RandomSpin = FMath::VRand() * LaunchSpinSpeed;
		MeshAngularVelocity = RandomSpin;
	}
}

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
		// Allow collision/physics damage types (Wallslam, EMFProximity) —
		// these come from NPC-NPC collisions and wall slams, NOT weapon fire
		bool bIsCollisionDamage = DamageEvent.DamageTypeClass &&
			(DamageEvent.DamageTypeClass->IsChildOf(UDamageType_Wallslam::StaticClass()) ||
			 DamageEvent.DamageTypeClass->IsChildOf(UDamageType_EMFProximity::StaticClass()));

		// Same rule as AShooterNPC::TakeDamage: only a shooter on the SAME side is ignored, so two
		// factions can actually hurt each other.
		if (!bIsCollisionDamage)
		{
			AActor* DamageOwner = DamageCauser->GetOwner();
			if (AShooterNPC* Shooter = Cast<AShooterNPC>(DamageCauser) ? Cast<AShooterNPC>(DamageCauser) : Cast<AShooterNPC>(DamageOwner))
			{
				if (!PolarityTeams::AreHostile(this, Shooter))
				{
					return 0.0f;
				}
			}
		}

		if (EventInstigator)
		{
			if (AShooterNPC* InstigatorNPC = Cast<AShooterNPC>(EventInstigator->GetPawn()))
			{
				if (!PolarityTeams::AreHostile(this, InstigatorNPC))
				{
					return 0.0f;
				}
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
			// Skipped for a blade that states its own ionization: it has already charged this target
			// through the ordinary weapon path, and paying out here as well would land the same hit
			// twice, leaving the number on the weapon describing half of what actually happens.
			// Everything else still comes through here -- bare fists, an enemy hitting a player, a
			// drone -- because for those this IS where the amount is authored.
			APawn* Attacker = EventInstigator->GetPawn();
			if (Attacker && !AShooterWeapon_Melee::AttackerOverridesLegacyMeleeCharge(Attacker))
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

	// Apply angular impulse from the hit (visual stabilization system)
	if (DamageCauser)
	{
		const FVector HitDirection = (GetActorLocation() - DamageCauser->GetActorLocation()).GetSafeNormal();
		ApplyAngularImpulse(HitDirection, Damage);
	}

	// Broadcast damage taken event for damage numbers system
	// Use actor center + offset for hit location (same as ShooterNPC)
	FVector HitLocation = GetActorLocation() + FVector(0.0f, 0.0f, 50.0f);
	OnDamageTaken.Broadcast(this, Damage, DamageEvent.DamageTypeClass, HitLocation, DamageCauser);

	// Damage-stage progression and repair-retreat bookkeeping
	if (CurrentHP > 0.0f)
	{
		DamageSinceLastRepair += Damage;

		if (bIsRepairing)
		{
			// A heavy single hit aborts the retreat immediately; the cooldown gates the next one
			if (Damage >= RepairInterruptDamage)
			{
				UE_LOG(LogTemp, Warning, TEXT("[DRONE_DEBUG] %s repair interrupted by %.1f damage"),
					*GetName(), Damage);
				EndRepairRetreat(false);
			}
		}
		else
		{
			UpdateDamageStage();

			if (ShouldBeginRepair())
			{
				UE_LOG(LogTemp, Warning, TEXT("[DRONE_DEBUG] %s begins repair retreat (stage=%d, accDamage=%.1f)"),
					*GetName(), static_cast<int32>(DamageStage), DamageSinceLastRepair);
				BeginRepairRetreat();
			}
		}
	}

	// Check if we should die
	if (CurrentHP <= 0.0f)
	{
		// Store killing blow info before death for health pickup logic
		LastKillingDamageType = DamageEvent.DamageTypeClass;
		LastKillingDamageCauser = DamageCauser;

		// Compute killing hit direction for GC directional bias
		if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
		{
			const FRadialDamageEvent& RadialEvent = static_cast<const FRadialDamageEvent&>(DamageEvent);
			LastKillingHitDirection = (GetActorLocation() - RadialEvent.Origin).GetSafeNormal();
		}
		else if (DamageCauser)
		{
			LastKillingHitDirection = (GetActorLocation() - DamageCauser->GetActorLocation()).GetSafeNormal();
		}

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

	// A dead drone repairs nothing
	if (bIsRepairing)
	{
		bIsRepairing = false;
		bRepairHovering = false;
	}

	const float CachedNPCCharge = EMFVelocityModifier ? EMFVelocityModifier->GetCharge() : 0.0f;

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
	OnNPCDeathDetailed.Broadcast(this, LastKillingDamageType, LastKillingDamageCauser);

	// Spawn death drops before explosion/deactivation can clear EMF charge or destroy the weapon actor.
	if (!bSuppressDeathDrops && LootDrop)
	{
		// Drones always ran their own kill rules: no armour, no reduced tier for being slammed by
		// an enemy, and a channelled drone killed by a wall slam pays out like a prop kill (its
		// DamageCauser is the other NPC, so ShouldDropHealth alone misses it).
		const bool bChannelingKineticKill = bWasChannelingTarget && LastKillingDamageType &&
			LastKillingDamageType->IsChildOf(UDamageType_Wallslam::StaticClass());

		FLootDropContext Context = MakeLootContext(CachedNPCCharge);
		Context.bChanneled = false;
		Context.bNPCCollisionKill = false;
		Context.bPropOrDroneKill = bStunnedByExplosion || bChannelingKineticKill ||
			AHealthPickup::ShouldDropHealth(LastKillingDamageType, LastKillingDamageCauser);

		LootDrop->DropLoot(Context);
	}

	if (bExplodeOnDeath)
	{
		TriggerExplosion();
	}
	else
	{
		StartDeathFall();
	}

	// Spawn death gibs (GC actor is independent — survives drone's 0.5s Destroy)
	if (DeathGeometryCollection)
	{
		const FDeathModeConfig& DeathConfig = ResolveDeathConfig();
		SpawnDeathGeometryCollection(DeathConfig);
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

	if (RepairHealFXComponent)
	{
		RepairHealFXComponent->Deactivate();
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

namespace
{
	/** Pawn responsible for this drone's death explosion. The killing blow decides who answers for
	 *  it: the owner chain is walked because the causer is usually a weapon or projectile rather
	 *  than the character itself (same shape as ResolveShooterCharacterFromShooterNPCDamageCauser
	 *  in ShooterNPC.cpp). Any pawn qualifies, so an NPC faction's killer is attributed honestly
	 *  as well and the friendly-fire filter works against the right side.
	 *  Returns null for world kills / chain deaths with no pawn behind them - then the explosion
	 *  damages everyone in radius, including the drone's own faction. */
	APawn* ResolveExplosionInstigator(AActor* KillingCauser)
	{
		for (AActor* Candidate = KillingCauser; Candidate; Candidate = Candidate->GetOwner())
		{
			if (APawn* PawnCandidate = Cast<APawn>(Candidate))
			{
				return PawnCandidate;
			}

			if (APawn* PawnInstigator = Cast<APawn>(Candidate->GetInstigator()))
			{
				return PawnInstigator;
			}
		}

		return nullptr;
	}
}

void AFlyingDrone::TriggerExplosion()
{
	// Charge-proportionate scaling (like EMFPhysicsProp::Explode)
	float ChargeScale = 1.0f;
	if (bScaleExplosionWithCharge && EMFVelocityModifier)
	{
		const float AbsCharge = FMath::Abs(EMFVelocityModifier->GetCharge());
		ChargeScale = FMath::Clamp(AbsCharge / ExplosionReferenceCharge, MinChargeScale, MaxChargeScale);
	}

	const float FinalExplosionDamage = ExplosionDamage * ChargeScale;
	const float FinalExplosionRadius = ExplosionRadius * ChargeScale;

	// Spawn explosion VFX
	SpawnExplosionEffect();

	// Play explosion sound
	if (ExplosionSound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), ExplosionSound, GetActorLocation());
	}

	// Apply explosion damage to all actors in radius + track for delegate
	float ImpactTotalDamage = 0.0f;
	int32 ImpactKillCount = 0;

	if (FinalExplosionDamage > 0.0f && FinalExplosionRadius > 0.0f)
	{
		const FVector Origin = GetActorLocation();

		TArray<FOverlapResult> Overlaps;
		FCollisionShape Sphere = FCollisionShape::MakeSphere(FinalExplosionRadius);
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);

		GetWorld()->OverlapMultiByChannel(Overlaps, Origin, FQuat::Identity, ECC_Pawn, Sphere, QueryParams);

		// Honest attribution: whoever dealt the killing blow answers for the blast, so the
		// friendly-fire filter in each victim's TakeDamage works against the right side.
		APawn* ExplosionInstigator = ResolveExplosionInstigator(LastKillingDamageCauser);

		TSet<AActor*> DamagedActors;

		for (const FOverlapResult& Overlap : Overlaps)
		{
			AActor* HitActor = Overlap.GetActor();
			if (!HitActor || DamagedActors.Contains(HitActor))
			{
				continue;
			}
			DamagedActors.Add(HitActor);

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

			const float Distance = FVector::Dist(Origin, HitActor->GetActorLocation());
			const float DamageScale = FMath::Clamp(1.0f - Distance / FinalExplosionRadius, 0.0f, 1.0f);
			const float ActorDamage = FinalExplosionDamage * DamageScale;

			if (ActorDamage <= 0.0f)
			{
				continue;
			}

			// Track NPC state for kill detection
			AShooterNPC* HitNPC = Cast<AShooterNPC>(HitActor);
			const bool bWasAlive = HitNPC && !HitNPC->IsDead();

			FRadialDamageEvent RadialDamageEvent;
			RadialDamageEvent.DamageTypeClass = UDamageType_DroneExplosion::StaticClass();
			RadialDamageEvent.Origin = Origin;
			RadialDamageEvent.Params.BaseDamage = FinalExplosionDamage;
			RadialDamageEvent.Params.OuterRadius = FinalExplosionRadius;
			HitActor->TakeDamage(ActorDamage, RadialDamageEvent, nullptr, ExplosionInstigator);

			if (bWasAlive)
			{
				ImpactTotalDamage += ActorDamage;
				if (HitNPC->IsDead())
				{
					ImpactKillCount++;
				}
			}
		}
	}

	// Credit the kill assist only when a player character actually caused the death; NPC-caused
	// blasts stay attributed to their faction without feeding player score.
	if (ImpactTotalDamage > 0.0f)
	{
		if (AShooterCharacter* Credited = Cast<AShooterCharacter>(
			ResolveExplosionInstigator(LastKillingDamageCauser)))
		{
			Credited->OnPropImpact.Broadcast(nullptr, ImpactTotalDamage, ImpactKillCount);
		}
	}

	// Stun nearby NPCs (same logic as EMFPhysicsProp::Explode)
	if (bApplyExplosionStun && FinalExplosionRadius > 0.0f)
	{
		TArray<FOverlapResult> StunOverlaps;
		FCollisionShape StunSphere = FCollisionShape::MakeSphere(FinalExplosionRadius);
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

			UE_LOG(LogTemp, Warning, TEXT("[Drone Explosion] Stunned %s for %.1fs"),
				*NPC->GetName(), ExplosionStunDuration);
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

void AFlyingDrone::SpawnDeathGeometryCollection(const FDeathModeConfig& Config)
{
	if (!DeathGeometryCollection || !GetWorld())
	{
		return;
	}

	// Use DroneMesh transform (the actual visible mesh) instead of hidden SkeletalMesh
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
	// Note: can't use RagdollCollisionProfile here — drone sets it to "NoCollision"
	GCComp->SetCollisionProfileName(FName("Ragdoll"));
	GCComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GCComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

	// Assign GC asset and initialize physics
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
	URadialVector* AngularVelocity = NewObject<URadialVector>(GCActor);
	AngularVelocity->Magnitude = Config.DismembermentAngularImpulse;
	AngularVelocity->Position = Origin;
	GCComp->ApplyPhysicsField(true,
		EGeometryCollectionPhysicsTypeEnum::Chaos_AngularVelocity,
		nullptr, AngularVelocity);

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

	GCActor->SetLifeSpan(GibLifetime);

	UE_LOG(LogTemp, Log, TEXT("Drone SpawnDeathGC: %s impulse=%.0f dir=[%.2f,%.2f,%.2f]"),
		*GetName(), Config.DismembermentImpulse,
		LastKillingHitDirection.X, LastKillingHitDirection.Y, LastKillingHitDirection.Z);
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
		// Same question, same answer as the ground NPCs: the target says where it gets shot
		AimTarget = PolarityAim::ResolveAimPoint(Target);

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

	// Smoke, which has no collision and therefore cannot be found by the trace below.
	// @see USmokeVisionSubsystem
	if (USmokeVisionSubsystem::IsSightBlockedInWorld(GetWorld(), Start, End))
	{
		return false;
	}

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

	// Sides, not the "Player" tag. Same answer while players were the only other side, and the only
	// way a gunship can be pointed at another faction.
	TArray<APawn*> FoundActors;
	PolarityTeams::GatherHostilePawns(this, FoundActors);

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
	// Та же глушилка, что у попаданий оружия: polarity.debug.novfx. Читается по имени, потому что
	// объявлена в ShooterWeapon.cpp - отдельный заголовок ради одного флага стоил бы пересборки.
	static IConsoleVariable* const NoVFX =
		IConsoleManager::Get().FindConsoleVariable(TEXT("polarity.debug.novfx"));
	if (NoVFX && NoVFX->GetInt() != 0)
	{
		return;
	}

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
	UpdateStabilization(DeltaTime);
}

void AFlyingDrone::UpdateDroneRotation(float DeltaTime)
{
	// When captured: don't update actor rotation every frame.
	// The initial rotation is set in EnterCapturedState, and UpdateStabilization
	// handles the mesh tilt via CapturedTiltOffset. Continuously rotating the actor
	// here fights with the PD stabilization controller, causing angular jiggle.
	if (bIsCaptured)
	{
		return;
	}

	FRotator TargetRotation = GetActorRotation();

	// Honest dash pause: while dashing the drone does not track its target at all - it flies
	// where the dash carries it and only resumes turning after the maneuver. Predictable pause
	// instead of a jerk with continued fire.
	AActor* Target = (IsDashing() || bIsRepairing) ? nullptr : CurrentAimTarget.Get();
	if (Target && !Target->IsPendingKillPending())
	{
		const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
		TargetRotation = ToTarget.Rotation();
		TargetRotation.Pitch = 0.0f;
	}
	else if (FlyingMovement && FlyingMovement->IsMoving())
	{
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

void AFlyingDrone::ApplyAngularImpulse(const FVector& HitDirection, float Damage)
{
	// Convert hit direction to drone's local space
	const FVector LocalHitDir = GetActorTransform().InverseTransformVectorNoScale(HitDirection);

	// Cross product with up vector gives torque axis:
	// Hit from right (+Y) → roll left, hit from front (+X) → pitch back
	// Cross(LocalHitDir, Up) = (LocalHitDir.Y, -LocalHitDir.X, 0)
	FVector Impulse;
	Impulse.X = LocalHitDir.Y;   // Roll from lateral hits
	Impulse.Y = -LocalHitDir.X;  // Pitch from frontal hits
	Impulse.Z = FMath::RandRange(-ImpulseYawRandomness, ImpulseYawRandomness); // Random yaw spin

	// Scale with damage
	Impulse.X *= Damage * AngularImpulsePerDamage;
	Impulse.Y *= Damage * AngularImpulsePerDamage;

	// Accumulate (multiple hits stack)
	MeshAngularVelocity += Impulse;

	// Clamp to max
	MeshAngularVelocity = MeshAngularVelocity.GetClampedToMaxSize(MaxAngularVelocity);
}

void AFlyingDrone::UpdateStabilization(float DeltaTime)
{
	if (!DroneMesh)
	{
		return;
	}

	// Get current tilt as euler angles
	const FRotator CurrentTilt = DroneMesh->GetRelativeRotation();
	const FVector CurrentAngle(CurrentTilt.Roll, CurrentTilt.Pitch, CurrentTilt.Yaw);

	// Stabilization target: zero (level) normally, CapturedTiltOffset when captured
	const FVector TargetAngle = bIsCaptured
		? FVector(CapturedTiltOffset.Roll, CapturedTiltOffset.Pitch, CapturedTiltOffset.Yaw)
		: FVector::ZeroVector;

	// PD Controller: restoring torque toward target + damping
	// Torque = Spring * (Target - Current) - Damping * AngularVelocity
	const FVector RestoringTorque = StabilizationSpring * (TargetAngle - CurrentAngle);
	const FVector DampingTorque = -StabilizationDamping * MeshAngularVelocity;

	// Integrate angular velocity
	MeshAngularVelocity += (RestoringTorque + DampingTorque) * DeltaTime;

	// Clamp angular velocity
	MeshAngularVelocity = MeshAngularVelocity.GetClampedToMaxSize(MaxAngularVelocity);

	// Integrate angle
	FVector NewAngle = CurrentAngle + MeshAngularVelocity * DeltaTime;

	// Clamp tilt to max angle (only when NOT captured — captured uses CapturedTiltOffset as its own limit)
	if (!bIsCaptured)
	{
		NewAngle.X = FMath::Clamp(NewAngle.X, -MaxTiltAngle, MaxTiltAngle); // Roll
		NewAngle.Y = FMath::Clamp(NewAngle.Y, -MaxTiltAngle, MaxTiltAngle); // Pitch

		// If at boundary, zero out velocity on that axis to prevent bounce
		if (FMath::Abs(NewAngle.X) >= MaxTiltAngle && FMath::Sign(NewAngle.X) == FMath::Sign(MeshAngularVelocity.X))
		{
			MeshAngularVelocity.X = 0.0f;
		}
		if (FMath::Abs(NewAngle.Y) >= MaxTiltAngle && FMath::Sign(NewAngle.Y) == FMath::Sign(MeshAngularVelocity.Y))
		{
			MeshAngularVelocity.Y = 0.0f;
		}
	}

	// Apply to mesh
	DroneMesh->SetRelativeRotation(FRotator(NewAngle.Y, NewAngle.Z, NewAngle.X));
}

// ==================== Captured State (EMF Channeling) ====================

void AFlyingDrone::EnterCapturedState(UAnimMontage* OverrideMontage)
{
	// Stop FlyingAIMovementComponent before capture takes over
	if (FlyingMovement)
	{
		FlyingMovement->StopMovement();
	}

	// Reset angular velocity to prevent pre-capture spin from causing jiggle
	MeshAngularVelocity = FVector::ZeroVector;

	// Set initial "away from player" rotation immediately (no interp)
	// so UpdateDroneRotation doesn't need to chase a moving target.
	// EnterCapturedState is not told who captured the drone; capture range is short, so the
	// nearest player is the captor in practice. TODO(COOP): pass the captor in explicitly.
	if (const APawn* Player = CoopPlayers::GetNearest(GetWorld(), GetActorLocation()))
	{
		const FVector AwayFromPlayer = (GetActorLocation() - Player->GetActorLocation());
		if (!AwayFromPlayer.IsNearlyZero())
		{
			FRotator CaptureRotation = AwayFromPlayer.Rotation();
			CaptureRotation.Pitch = 0.0f;
			SetActorRotation(CaptureRotation);
		}
	}

	// Let base class handle bIsCaptured, bIsInKnockback, stop AI, stop CMC, etc.
	Super::EnterCapturedState(OverrideMontage);
}

void AFlyingDrone::ExitCapturedState()
{
	// Let base class handle cleanup first (clears bIsCaptured, bIsInKnockback, etc.)
	Super::ExitCapturedState();

	// Restore flying movement mode (parent leaves it in Falling)
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->SetMovementMode(MOVE_Flying);
	}
}

// ==================== Knockback ====================

void AFlyingDrone::ApplyKnockback(const FVector& InKnockbackDirection, float Distance, float Duration, const FVector& AttackerLocation, bool bKeepEMFEnabled, EKnockbackStyle Style)
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

	// Ignore collision with the attacker during knockback to prevent jitter from dropkick.
	// The attacker is identified by position, so ask the team who was standing there.
	if (!AttackerLocation.IsZero())
	{
		if (APawn* Attacker = CoopPlayers::GetNearest(GetWorld(), AttackerLocation))
		{
			const float DistToAttacker = FVector::Dist(Attacker->GetActorLocation(), AttackerLocation);
			if (DistToAttacker < 300.0f) // Player is the attacker
			{
				KnockbackIgnoreActor = Attacker;
				MoveIgnoreActorAdd(Attacker);
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
	Super::ApplyKnockback(InKnockbackDirection, Distance, Duration, AttackerLocation, bKeepEMFEnabled, Style);

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

	// Running away is a full-time job. The dash is an evasion inside a fight - a sideways jink to
	// spoil somebody's aim - and it works against an escape: the drone spends its flight jerking
	// across the line it is trying to leave along, and covers no ground. The squad turns weapons off
	// on a member it has ordered to run; this is the same switch, for the same reason.
	if (IsCombatDisabled())
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

// ==================== Damage Stages ====================

void AFlyingDrone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AFlyingDrone, DamageStage);
}

float AFlyingDrone::GetStageSpeedMultiplier() const
{
	switch (DamageStage)
	{
	case EDroneDamageStage::Damaged:  return DamagedSpeedMultiplier;
	case EDroneDamageStage::Critical: return CriticalSpeedMultiplier;
	default:                          return 1.0f;
	}
}

float AFlyingDrone::GetStageOverheatScale() const
{
	return DamageStage == EDroneDamageStage::Critical ? CriticalOverheatScale : 1.0f;
}

void AFlyingDrone::UpdateDamageStage()
{
	const float MaxHP = MaxHPAtSpawn > 0.0f ? MaxHPAtSpawn : FMath::Max(CurrentHP, 1.0f);
	const float HPFraction = CurrentHP / MaxHP;

	EDroneDamageStage NewStage = EDroneDamageStage::Intact;
	if (HPFraction <= CriticalStageHPFraction)
	{
		NewStage = EDroneDamageStage::Critical;
	}
	else if (HPFraction <= DamagedStageHPFraction)
	{
		NewStage = EDroneDamageStage::Damaged;
	}

	DamageStage = NewStage;

	// Speed penalty follows the stage; the component's ApplyMovementInput pushes FlySpeed into
	// CMC every tick, and CompleteDash restores from this same property, so one write covers all.
	if (FlyingMovement && BaseFlySpeed > 0.0f)
	{
		FlyingMovement->FlySpeed = BaseFlySpeed * GetStageSpeedMultiplier();
	}

	ApplyStageVFX(NewStage);
}

void AFlyingDrone::ApplyStageVFX(EDroneDamageStage Stage)
{
	if (DamagedHullFXComponent)
	{
		if (Stage == EDroneDamageStage::Damaged || Stage == EDroneDamageStage::Critical)
		{
			DamagedHullFXComponent->Activate();
		}
		else
		{
			DamagedHullFXComponent->Deactivate();
		}
	}

	if (CriticalHullFXComponent)
	{
		if (Stage == EDroneDamageStage::Critical)
		{
			CriticalHullFXComponent->Activate();
		}
		else
		{
			CriticalHullFXComponent->Deactivate();
		}
	}
}

void AFlyingDrone::OnRep_DroneDamageStage()
{
	// Clients mirror the hull VFX; speed stays server-side because AI movement is authoritative
	ApplyStageVFX(DamageStage);
}

// ==================== Repair Retreat ====================

bool AFlyingDrone::ShouldBeginRepair() const
{
	if (bIsRepairing || bIsDead || !GetWorld())
	{
		return false;
	}

	const float CurrentTime = GetWorld()->GetTimeSeconds();
	if (CurrentTime - LastRepairEndTime < RepairCooldown)
	{
		return false;
	}

	return DamageStage == EDroneDamageStage::Critical ||
	       DamageSinceLastRepair >= RepairDamageThreshold;
}

bool AFlyingDrone::BeginRepairRetreat()
{
	if (bIsRepairing || bIsDead || !FlyingMovement)
	{
		return false;
	}

	bIsRepairing = true;
	bRepairHovering = false;
	RepairAnchorLocation = GetActorLocation();

	// Break off: stop firing and climb straight up, out of the fight's altitude band
	StopShooting();
	FlyingMovement->StopMovement();
	FlyingMovement->FlyToLocationUnclamped(RepairAnchorLocation + FVector(0.0f, 0.0f, RepairAltitude));

	if (RepairHealFXComponent)
	{
		RepairHealFXComponent->Activate();
	}

	return true;
}

void AFlyingDrone::EndRepairRetreat(bool bCompleted)
{
	if (!bIsRepairing)
	{
		return;
	}

	bIsRepairing = false;
	bRepairHovering = false;
	LastRepairEndTime = GetWorld()->GetTimeSeconds();

	if (RepairHealFXComponent)
	{
		RepairHealFXComponent->Deactivate();
	}

	UE_LOG(LogTemp, Warning, TEXT("[DRONE_DEBUG] %s repair ended (%s), HP=%.1f"),
		*GetName(), bCompleted ? TEXT("completed") : TEXT("interrupted"), CurrentHP);
}

void AFlyingDrone::TickRepair(float DeltaTime)
{
	if (!bIsRepairing || bIsDead || !FlyingMovement)
	{
		return;
	}

	// Still climbing - wait until the movement component reports arrival (or stuck-abort)
	if (!bRepairHovering)
	{
		if (!FlyingMovement->IsMoving())
		{
			bRepairHovering = true;
		}
		else
		{
			return;
		}
	}

	// Hovering at the repair altitude: heal up to spawn HP. Collisions are untouched, so a
	// spotter can still shoot the drone down here.
	const float HealedHP = FMath::Min(CurrentHP + RepairHPPerSecond * DeltaTime, MaxHPAtSpawn);
	CurrentHP = HealedHP;

	UpdateDamageStage();

	if (CurrentHP >= MaxHPAtSpawn - KINDA_SMALL_NUMBER)
	{
		// Full repair clears the accumulated-damage trigger for the next cycle
		DamageSinceLastRepair = 0.0f;
		EndRepairRetreat(true);
	}
}
