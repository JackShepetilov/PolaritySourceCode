// SniperTurretNPC.cpp

#include "SniperTurretNPC.h"
#include "ShooterWeapon.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "Engine/DamageEvents.h"

ASniperTurretNPC::ASniperTurretNPC(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Create turret visual mesh
	TurretMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TurretMesh"));
	TurretMesh->SetupAttachment(RootComponent);
	TurretMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Hide the inherited SkeletalMesh
	GetMesh()->SetVisibility(false);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Disable character movement - turret is stationary
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	if (CMC)
	{
		CMC->GravityScale = 0.0f;
		CMC->MaxWalkSpeed = 0.0f;
		CMC->MaxAcceleration = 0.0f;
		CMC->bOrientRotationToMovement = false;
		CMC->bUseControllerDesiredRotation = false;
	}

	// Turrets are immune to knockback
	KnockbackDistanceMultiplier = 0.0f;

	// Single precise shot, no burst fire
	BurstShotCount = 1;
	BurstCooldown = 0.0f;

	// Independent firing - no coordinator
	bUseCoordinator = false;
}

// ==================== Lifecycle ====================

void ASniperTurretNPC::BeginPlay()
{
	Super::BeginPlay();
	// Super spawns weapon via WeaponClass, registers with subsystems
}

void ASniperTurretNPC::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Rotate turret toward target when engaged
	if (AimTarget.IsValid() && CurrentAimState != ETurretAimState::Idle)
	{
		UpdateTurretRotation(DeltaTime);
	}

	// Advance aim progress
	if (CurrentAimState == ETurretAimState::Aiming)
	{
		UpdateAimProgress(DeltaTime);
	}
}

// ==================== Aiming Interface ====================

void ASniperTurretNPC::StartAiming(AActor* Target)
{
	if (!Target || bIsDead)
	{
		return;
	}

	AimTarget = Target;
	ResetAimProgress();
	SetAimState(ETurretAimState::Aiming);
}

void ASniperTurretNPC::StopAiming()
{
	AimTarget = nullptr;
	ResetAimProgress();
	SetAimState(ETurretAimState::Idle);

	GetWorld()->GetTimerManager().ClearTimer(DamageRecoveryTimer);
	GetWorld()->GetTimerManager().ClearTimer(PostFireCooldownTimer);
}

void ASniperTurretNPC::SetLOSStatus(bool bNewHasLOS)
{
	const bool bWasLOS = bHasLOS;
	bHasLOS = bNewHasLOS;

	if (bWasLOS && !bNewHasLOS)
	{
		// LOS lost - reset aim progress, no recovery delay
		if (CurrentAimState == ETurretAimState::Aiming)
		{
			ResetAimProgress();
		}
	}
	// When LOS regained, UpdateAimProgress resumes naturally since bHasLOS is true
}

// ==================== Internal Aiming Logic ====================

void ASniperTurretNPC::UpdateAimProgress(float DeltaTime)
{
	if (!AimTarget.IsValid() || bIsDead || !bHasLOS)
	{
		return;
	}

	AimProgress += DeltaTime / AimDuration;
	AimProgress = FMath::Clamp(AimProgress, 0.0f, 1.0f);

	OnAimProgressChanged.Broadcast(AimProgress, CurrentAimState);

	if (AimProgress >= 1.0f)
	{
		FireAtTarget();
	}
}

void ASniperTurretNPC::UpdateTurretRotation(float DeltaTime)
{
	if (!TurretMesh || !AimTarget.IsValid())
	{
		return;
	}

	const FVector TurretLoc = TurretMesh->GetComponentLocation();
	const FVector TargetLoc = AimTarget->GetActorLocation();
	FRotator DesiredRot = (TargetLoc - TurretLoc).Rotation();

	if (bYawOnly)
	{
		DesiredRot.Pitch = 0.0f;
	}
	else
	{
		DesiredRot.Pitch = FMath::Clamp(DesiredRot.Pitch, -MaxPitchDown, MaxPitchUp);
	}

	const FRotator CurrentRot = TurretMesh->GetComponentRotation();
	const FRotator NewRot = FMath::RInterpConstantTo(
		CurrentRot, DesiredRot, DeltaTime, TurretRotationSpeed);

	TurretMesh->SetWorldRotation(NewRot);
}

void ASniperTurretNPC::FireAtTarget()
{
	if (!AimTarget.IsValid() || !Weapon || bIsDead)
	{
		return;
	}

	SetAimState(ETurretAimState::Firing);

	// Set aim target on parent for GetWeaponTargetLocation
	CurrentAimTarget = AimTarget;

	// Fire using parent's weapon system (bHasExternalPermission = true skips coordinator)
	StartShooting(AimTarget.Get(), true);
	StopShooting();

	OnTurretFired.Broadcast();

	// Transition to post-fire cooldown
	ResetAimProgress();
	SetAimState(ETurretAimState::PostFireCooldown);

	GetWorld()->GetTimerManager().SetTimer(
		PostFireCooldownTimer,
		this, &ASniperTurretNPC::OnPostFireCooldownEnd,
		PostFireCooldownDuration, false);
}

void ASniperTurretNPC::OnDamageRecoveryEnd()
{
	if (AimTarget.IsValid() && bHasLOS && !bIsDead)
	{
		SetAimState(ETurretAimState::Aiming);
	}
	else
	{
		SetAimState(ETurretAimState::Idle);
	}
}

void ASniperTurretNPC::OnPostFireCooldownEnd()
{
	if (AimTarget.IsValid() && bHasLOS && !bIsDead)
	{
		SetAimState(ETurretAimState::Aiming);
	}
	else
	{
		SetAimState(ETurretAimState::Idle);
	}
}

void ASniperTurretNPC::ResetAimProgress()
{
	AimProgress = 0.0f;
	OnAimProgressChanged.Broadcast(AimProgress, CurrentAimState);
}

void ASniperTurretNPC::SetAimState(ETurretAimState NewState)
{
	CurrentAimState = NewState;
	OnAimProgressChanged.Broadcast(AimProgress, CurrentAimState);
}

// ==================== Overrides from AShooterNPC ====================

float ASniperTurretNPC::TakeDamage(float Damage, FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	const float ActualDamage = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	if (bIsDead)
	{
		return ActualDamage;
	}

	// Interrupt aiming if damage exceeds threshold
	if (ActualDamage >= AimInterruptDamageThreshold
		&& CurrentAimState == ETurretAimState::Aiming)
	{
		ResetAimProgress();
		SetAimState(ETurretAimState::DamageRecovery);

		GetWorld()->GetTimerManager().ClearTimer(DamageRecoveryTimer);
		GetWorld()->GetTimerManager().SetTimer(
			DamageRecoveryTimer,
			this, &ASniperTurretNPC::OnDamageRecoveryEnd,
			DamageRecoveryDelay, false);
	}

	return ActualDamage;
}

void ASniperTurretNPC::AttachWeaponMeshes(AShooterWeapon* WeaponToAttach)
{
	if (!WeaponToAttach)
	{
		return;
	}

	const FAttachmentTransformRules AttachRule(EAttachmentRule::SnapToTarget, false);
	WeaponToAttach->AttachToActor(this, AttachRule);

	// Hide first person mesh
	if (WeaponToAttach->GetFirstPersonMesh())
	{
		WeaponToAttach->GetFirstPersonMesh()->SetVisibility(false);
	}

	// Attach third person mesh to turret's weapon socket
	if (WeaponToAttach->GetThirdPersonMesh())
	{
		WeaponToAttach->GetThirdPersonMesh()->AttachToComponent(
			TurretMesh, AttachRule, TurretWeaponSocket);
	}
}

FVector ASniperTurretNPC::GetWeaponTargetLocation()
{
	// Sniper turret aims directly at target - no spread.
	// Progressive aim time IS the accuracy mechanic.
	if (AimTarget.IsValid())
	{
		return AimTarget->GetActorLocation();
	}

	// Fallback: aim forward
	return GetActorLocation() + GetActorForwardVector() * AimRange;
}

void ASniperTurretNPC::ApplyKnockback(const FVector& KnockbackDirection, float Distance,
	float Duration, const FVector& AttackerLocation, bool bKeepEMFEnabled)
{
	// Turrets are stationary - ignore knockback entirely
}

void ASniperTurretNPC::ApplyKnockbackVelocity(const FVector& KnockbackVelocity, float StunDuration)
{
	// Turrets are stationary - ignore knockback entirely
}

bool ASniperTurretNPC::HasLineOfSightTo(AActor* Target) const
{
	if (!Target || !GetWorld())
	{
		return false;
	}

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	FHitResult Hit;
	const FVector Start = TurretMesh
		? TurretMesh->GetComponentLocation()
		: GetActorLocation();
	const FVector End = Target->GetActorLocation();

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit, Start, End, ECC_Visibility, QueryParams);

	if (bHit)
	{
		return Hit.GetActor() == Target;
	}

	return true;
}
