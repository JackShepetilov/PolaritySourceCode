// ShooterWeapon_Melee.cpp

#include "ShooterWeapon_Melee.h"
#include "ShooterWeaponHolder.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Engine/DamageEvents.h"

AShooterWeapon_Melee::AShooterWeapon_Melee()
{
	// Disable irrelevant base weapon systems
	bUseHeatSystem = false;
	bUseZFactor = false;
	bUseHitscan = false;
	bUseChargeFiring = false;
	bFullAuto = true;

	// Melee-appropriate defaults
	RefireRate = 0.4f; // Swing speed (configured to match animation)
	MagazineSize = 999; // Effectively infinite
	ShotNoiseRange = 500.0f; // Melee is quieter than guns
	ShotLoudness = 0.3f;
}

void AShooterWeapon_Melee::BeginPlay()
{
	Super::BeginPlay();

	// Fill magazine so base class doesn't block firing
	CurrentBullets = MagazineSize;
}

void AShooterWeapon_Melee::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bIsLunging)
	{
		UpdateLunge(DeltaTime);
	}
}

void AShooterWeapon_Melee::Fire()
{
	// Base class handles firing flag check and refire timer scheduling
	if (!bIsFiring)
	{
		return;
	}

	// Keep ammo full (melee weapon doesn't consume ammo)
	CurrentBullets = MagazineSize;

	// Store velocity at swing start for momentum calculations
	if (PawnOwner)
	{
		if (UCharacterMovementComponent* Movement = PawnOwner->FindComponentByClass<UCharacterMovementComponent>())
		{
			VelocityAtSwingStart = Movement->Velocity;
		}
	}

	// Select and play swing animation
	const FMeleeWeaponSwingData* SelectedSwing = SelectWeightedSwing();
	if (SelectedSwing && SelectedSwing->SwingMontage)
	{
		WeaponOwner->PlayFiringMontage(SelectedSwing->SwingMontage);
		PlayMeleeCameraShake(SelectedSwing->SwingCameraShake, SelectedSwing->SwingShakeScale);
	}
	else if (FiringMontage)
	{
		// Fallback to base class FiringMontage
		WeaponOwner->PlayFiringMontage(FiringMontage);
	}

	// Play swing sound
	PlayMeleeSound(SwingSound);

	// Spawn swing trail VFX
	SpawnSwingTrail();

	// Perform hit detection
	FHitResult HitResult;
	bool bHit = PerformMeleeTrace(HitResult);

	if (bHit)
	{
		AActor* HitActor = HitResult.GetActor();
		float FinalDamage = ApplyMeleeDamage(HitActor, HitResult);

		// Apply knockback
		ApplyKnockback(HitActor);

		// Play hit effects
		PlayMeleeSound(HitSound);
		PlayMeleeCameraShake(HitCameraShake, HitShakeScale);
		SpawnMeleeImpactFX(HitResult.ImpactPoint, HitResult.ImpactNormal);

		// Report hit to weapon owner (hit markers, charge gain, etc.)
		bool bHeadshot = IsHeadshot(HitResult);
		FVector HitDirection = (HitResult.ImpactPoint - PawnOwner->GetActorLocation()).GetSafeNormal();
		bool bKilled = HitActor->IsActorBeingDestroyed() || (Cast<AShooterNPC>(HitActor) && Cast<AShooterNPC>(HitActor)->IsDead());
		WeaponOwner->OnWeaponHit(HitResult.ImpactPoint, HitDirection, FinalDamage, bHeadshot, bKilled);

		// Start lunge toward target if enabled
		if (bEnableLunge)
		{
			StartLunge(HitActor);
		}
	}
	else
	{
		// Miss
		PlayMeleeSound(MissSound);
	}

	// Fire perception event (AI awareness)
	OnShotFired.Broadcast();

	// Schedule next swing (base class handles timer via bFullAuto)
	float ActualRefireRate = RefireRate;
	if (SelectedSwing)
	{
		// Adjust refire rate based on animation play rate
		ActualRefireRate = RefireRate / SelectedSwing->BasePlayRate;
	}
	GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon_Melee::Fire, ActualRefireRate, false);
}

// ==================== Hit Detection ====================

bool AShooterWeapon_Melee::PerformMeleeTrace(FHitResult& OutHit)
{
	if (!PawnOwner || !PawnOwner->GetController())
	{
		return false;
	}

	// Get camera location and direction
	FVector CameraLocation;
	FRotator CameraRotation;
	PawnOwner->GetController()->GetPlayerViewPoint(CameraLocation, CameraRotation);
	FVector Forward = CameraRotation.Vector();

	// Calculate trace start and end
	FVector TraceStart = CameraLocation + Forward * TraceForwardOffset;
	FVector TraceEnd = TraceStart + Forward * AttackRange;

	// Sphere trace for pawns
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(PawnOwner);
	QueryParams.bReturnPhysicalMaterial = false;
	QueryParams.bTraceComplex = false;

	bool bHit = GetWorld()->SweepSingleByChannel(
		OutHit,
		TraceStart,
		TraceEnd,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(AttackRadius),
		QueryParams
	);

	// Validate hit target is a pawn
	if (bHit && OutHit.GetActor())
	{
		if (!Cast<APawn>(OutHit.GetActor()))
		{
			return false; // Only hit pawns
		}
		return true;
	}

	return false;
}

float AShooterWeapon_Melee::ApplyMeleeDamage(AActor* HitActor, const FHitResult& HitResult)
{
	if (!HitActor || !PawnOwner)
	{
		return 0.0f;
	}

	// Calculate base damage
	float FinalDamage = MeleeDamage;

	// Headshot multiplier
	if (IsHeadshot(HitResult))
	{
		FinalDamage *= MeleeHeadshotMultiplier;
	}

	// Momentum damage bonus
	FinalDamage += CalculateMomentumDamage(HitActor);

	// Apply damage
	FPointDamageEvent DamageEvent(FinalDamage, HitResult, (HitActor->GetActorLocation() - PawnOwner->GetActorLocation()).GetSafeNormal(), MeleeDamageType);
	HitActor->TakeDamage(FinalDamage, DamageEvent, PawnOwner->GetController(), PawnOwner);

	return FinalDamage;
}

bool AShooterWeapon_Melee::IsHeadshot(const FHitResult& HitResult) const
{
	FName BoneName = HitResult.BoneName;
	if (BoneName.IsNone())
	{
		return false;
	}

	FString BoneString = BoneName.ToString().ToLower();
	return BoneString.Contains(TEXT("head")) ||
		   BoneString.Contains(TEXT("neck"));
}

void AShooterWeapon_Melee::ApplyKnockback(AActor* HitActor)
{
	if (!HitActor || !PawnOwner)
	{
		return;
	}

	// Calculate knockback direction (center-to-center)
	FVector PlayerCenter = PawnOwner->GetActorLocation();
	FVector TargetCenter = HitActor->GetActorLocation();
	FVector KnockbackDirection = (TargetCenter - PlayerCenter).GetSafeNormal();

	// Add slight upward component for better visual
	KnockbackDirection.Z = FMath::Max(KnockbackDirection.Z, 0.1f);
	KnockbackDirection.Normalize();

	// Calculate knockback distance based on player speed toward target
	float PlayerSpeedTowardTarget = FMath::Max(0.0f, FVector::DotProduct(VelocityAtSwingStart, KnockbackDirection));
	float KnockbackDistance = BaseKnockbackDistance + PlayerSpeedTowardTarget * KnockbackDistancePerVelocity;

	// Try ShooterNPC first (has distance-based knockback with NPC multiplier)
	if (AShooterNPC* NPC = Cast<AShooterNPC>(HitActor))
	{
		NPC->ApplyKnockback(KnockbackDirection, KnockbackDistance, KnockbackDuration, PlayerCenter);
		return;
	}

	// For generic characters, convert to velocity-based launch
	if (ACharacter* HitCharacter = Cast<ACharacter>(HitActor))
	{
		FVector KnockbackVelocity = KnockbackDirection * (KnockbackDistance / KnockbackDuration);
		HitCharacter->LaunchCharacter(KnockbackVelocity, true, true);
		return;
	}

	// Fallback to physics impulse for non-characters
	if (UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(HitActor->GetRootComponent()))
	{
		if (RootPrimitive->IsSimulatingPhysics())
		{
			float Mass = RootPrimitive->GetMass();
			FVector Impulse = KnockbackDirection * (KnockbackDistance / KnockbackDuration) * Mass;
			RootPrimitive->AddImpulse(Impulse);
		}
	}
}

float AShooterWeapon_Melee::CalculateMomentumDamage(AActor* HitActor) const
{
	if (MomentumDamagePerSpeed <= 0.0f || !HitActor || !PawnOwner)
	{
		return 0.0f;
	}

	// Get velocity component toward the target
	FVector ToTarget = (HitActor->GetActorLocation() - PawnOwner->GetActorLocation()).GetSafeNormal();
	float VelocityTowardTarget = FMath::Max(0.0f, FVector::DotProduct(VelocityAtSwingStart, ToTarget));

	// Calculate bonus damage (per 100 cm/s)
	float BonusDamage = (VelocityTowardTarget / 100.0f) * MomentumDamagePerSpeed;
	return FMath::Min(BonusDamage, MaxMomentumDamage);
}

// ==================== Animation ====================

const FMeleeWeaponSwingData* AShooterWeapon_Melee::SelectWeightedSwing()
{
	if (SwingAnimations.Num() == 0)
	{
		return nullptr;
	}

	if (SwingAnimations.Num() == 1)
	{
		return &SwingAnimations[0];
	}

	// Calculate total weight
	float TotalWeight = 0.0f;
	for (const FMeleeWeaponSwingData& Swing : SwingAnimations)
	{
		TotalWeight += Swing.Weight;
	}

	if (TotalWeight <= 0.0f)
	{
		return &SwingAnimations[0];
	}

	// Select random, avoiding same animation twice in a row
	int32 SelectedIndex = -1;
	int32 Attempts = 0;
	while (SelectedIndex == -1 || (SelectedIndex == LastSwingIndex && Attempts < 3))
	{
		float RandomValue = FMath::FRandRange(0.0f, TotalWeight);
		float Accumulator = 0.0f;

		for (int32 i = 0; i < SwingAnimations.Num(); ++i)
		{
			Accumulator += SwingAnimations[i].Weight;
			if (RandomValue <= Accumulator)
			{
				SelectedIndex = i;
				break;
			}
		}
		Attempts++;
	}

	if (SelectedIndex >= 0)
	{
		LastSwingIndex = SelectedIndex;
		return &SwingAnimations[SelectedIndex];
	}

	return &SwingAnimations[0];
}

// ==================== Lunge ====================

void AShooterWeapon_Melee::StartLunge(AActor* Target)
{
	if (!Target || !PawnOwner || !bEnableLunge)
	{
		return;
	}

	// Check if player is moving fast enough
	if (VelocityAtSwingStart.Size() < MinSpeedForLunge)
	{
		return;
	}

	// Check distance
	float DistanceToTarget = FVector::Dist(PawnOwner->GetActorLocation(), Target->GetActorLocation());
	if (DistanceToTarget > LungeMaxRange || DistanceToTarget < LungeStopBuffer)
	{
		return;
	}

	LungeTarget = Target;
	LungeTargetPosition = Target->GetActorLocation();
	bIsLunging = true;
}

void AShooterWeapon_Melee::UpdateLunge(float DeltaTime)
{
	if (!bIsLunging || !PawnOwner)
	{
		bIsLunging = false;
		return;
	}

	// Update target position if target is still valid
	if (LungeTarget.IsValid())
	{
		LungeTargetPosition = LungeTarget->GetActorLocation();
	}

	FVector CurrentLocation = PawnOwner->GetActorLocation();
	FVector ToTarget = LungeTargetPosition - CurrentLocation;
	float DistanceRemaining = ToTarget.Size();

	// Stop when close enough
	if (DistanceRemaining <= LungeStopBuffer)
	{
		bIsLunging = false;
		return;
	}

	// Move toward target
	FVector LungeDirection = ToTarget.GetSafeNormal();
	float MoveDistance = LungeSpeed * DeltaTime;

	// Don't overshoot
	if (MoveDistance > DistanceRemaining - LungeStopBuffer)
	{
		MoveDistance = DistanceRemaining - LungeStopBuffer;
		bIsLunging = false;
	}

	if (ACharacter* OwnerChar = Cast<ACharacter>(PawnOwner))
	{
		OwnerChar->LaunchCharacter(LungeDirection * LungeSpeed, true, true);
		// Lunge is one-shot impulse
		bIsLunging = false;
	}
}

// ==================== VFX/SFX ====================

void AShooterWeapon_Melee::SpawnSwingTrail()
{
	StopSwingTrail();

	if (!SwingTrailFX)
	{
		return;
	}

	USkeletalMeshComponent* WeaponMesh = GetFirstPersonMesh();
	if (!WeaponMesh)
	{
		return;
	}

	ActiveTrailFX = UNiagaraFunctionLibrary::SpawnSystemAttached(
		SwingTrailFX,
		WeaponMesh,
		TrailSocketName,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		EAttachLocation::SnapToTarget,
		true
	);
}

void AShooterWeapon_Melee::StopSwingTrail()
{
	if (ActiveTrailFX)
	{
		ActiveTrailFX->DestroyComponent();
		ActiveTrailFX = nullptr;
	}
}

void AShooterWeapon_Melee::SpawnMeleeImpactFX(const FVector& Location, const FVector& Normal)
{
	if (!MeleeImpactFX)
	{
		return;
	}

	UNiagaraComponent* ImpactComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		MeleeImpactFX,
		Location,
		Normal.Rotation(),
		FVector(ImpactFXScale),
		true,
		true,
		ENCPoolMethod::AutoRelease
	);
}

void AShooterWeapon_Melee::PlayMeleeSound(USoundBase* Sound)
{
	if (!Sound || !PawnOwner)
	{
		return;
	}

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		Sound,
		PawnOwner->GetActorLocation(),
		FRotator::ZeroRotator,
		1.0f,
		FMath::FRandRange(0.95f, 1.05f)
	);
}

void AShooterWeapon_Melee::PlayMeleeCameraShake(TSubclassOf<UCameraShakeBase> ShakeClass, float Scale)
{
	if (!ShakeClass || !PawnOwner)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(PawnOwner->GetController());
	if (PC)
	{
		PC->ClientStartCameraShake(ShakeClass, Scale);
	}
}
