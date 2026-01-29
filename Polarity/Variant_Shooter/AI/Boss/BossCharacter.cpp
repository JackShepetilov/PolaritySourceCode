// BossCharacter.cpp
// Hybrid boss character implementation

#include "BossCharacter.h"
#include "Variant_Shooter/AI/FlyingAIMovementComponent.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Variant_Shooter/Weapons/ShooterProjectile.h"
#include "Variant_Shooter/Weapons/EMFProjectile.h"
#include "EMFVelocityModifier.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/DamageEvents.h"
#include "Components/CapsuleComponent.h"
#include "Animation/AnimInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "DrawDebugHelpers.h"

ABossCharacter::ABossCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Create flying movement component
	FlyingMovement = CreateDefaultSubobject<UFlyingAIMovementComponent>(TEXT("FlyingMovement"));

	// Boss-specific defaults
	CurrentHP = 1000.0f;
	MaxHP = 1000.0f;
}

void ABossCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Cache max HP for threshold calculations
	MaxHP = CurrentHP;

	// Initialize in ground phase
	CurrentPhase = EBossPhase::Ground;

	// Configure flying movement for aerial phase
	if (FlyingMovement)
	{
		FlyingMovement->DefaultHoverHeight = AerialHoverHeight;
		FlyingMovement->MinHoverHeight = AerialHoverHeight - 200.0f;
		FlyingMovement->MaxHoverHeight = AerialHoverHeight + 200.0f;
		FlyingMovement->FlySpeed = AerialStrafeSpeed;
	}
}

void ABossCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Update arc dash if in progress
	if (bIsDashing)
	{
		UpdateArcDash(DeltaTime);
	}

	// Perform melee trace if damage window is active
	if (bDamageWindowActive)
	{
		PerformMeleeTrace();
	}

	// Check aerial phase timeout
	if (CurrentPhase == EBossPhase::Aerial)
	{
		CheckAerialPhaseTimeout();
	}
}

void ABossCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clear all timers
	GetWorld()->GetTimerManager().ClearTimer(DashCooldownTimer);
	GetWorld()->GetTimerManager().ClearTimer(MeleeCooldownTimer);
	GetWorld()->GetTimerManager().ClearTimer(DamageWindowStartTimer);
	GetWorld()->GetTimerManager().ClearTimer(DamageWindowEndTimer);
	GetWorld()->GetTimerManager().ClearTimer(AerialPhaseTimer);
	GetWorld()->GetTimerManager().ClearTimer(ParryCheckTimer);

	// Clear tracked projectiles
	TrackedProjectiles.Empty();
	ProjectileOriginalTargetPolarity.Empty();

	Super::EndPlay(EndPlayReason);
}

// ==================== Damage Handling ====================

float ABossCharacter::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// In finisher phase, only accept melee damage from player to trigger finisher
	if (bIsInFinisherPhase)
	{
		// Check if this is a melee hit from player
		// The player's melee system should call ExecuteFinisher() directly
		// Here we just ignore all damage
		return 0.0f;
	}

	// Calculate damage that would be applied
	float ActualDamage = Damage;

	// Check if this damage would bring HP to 1 or below
	if (CurrentHP - ActualDamage <= 1.0f)
	{
		// Set HP to exactly 1 and enter finisher phase
		float DamageToApply = CurrentHP - 1.0f;
		CurrentHP = 1.0f;

		// Broadcast damage taken event
		OnDamageTaken.Broadcast(this, DamageToApply, nullptr, GetActorLocation(), DamageCauser);

		// Enter finisher phase
		EnterFinisherPhase();

		return DamageToApply;
	}

	// Normal damage handling
	return Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
}

// ==================== Phase Control ====================

void ABossCharacter::SetPhase(EBossPhase NewPhase)
{
	if (CurrentPhase != NewPhase)
	{
		ExecutePhaseTransition(NewPhase);
	}
}

bool ABossCharacter::ShouldTransitionToAerial() const
{
	if (CurrentPhase != EBossPhase::Ground)
	{
		return false;
	}

	// Check HP threshold
	float HPPercent = CurrentHP / MaxHP;
	if (HPPercent <= AerialPhaseHPThreshold)
	{
		return true;
	}

	// Check dash attack count
	if (CurrentDashAttackCount >= DashAttacksBeforeAerialPhase)
	{
		return true;
	}

	return false;
}

bool ABossCharacter::ShouldTransitionToGround() const
{
	if (CurrentPhase != EBossPhase::Aerial)
	{
		return false;
	}

	// Check parry count
	if (CurrentParryCount >= ParriesBeforeGroundPhase)
	{
		return true;
	}

	// Check timeout
	float TimeInAerial = GetWorld()->GetTimeSeconds() - AerialPhaseStartTime;
	if (TimeInAerial >= MaxAerialPhaseDuration)
	{
		return true;
	}

	return false;
}

void ABossCharacter::ExecutePhaseTransition(EBossPhase NewPhase)
{
	EBossPhase OldPhase = CurrentPhase;
	CurrentPhase = NewPhase;

	// Reset phase-specific counters
	switch (NewPhase)
	{
	case EBossPhase::Ground:
		CurrentDashAttackCount = 0;
		StopHovering();
		StopParryDetection();
		break;

	case EBossPhase::Aerial:
		CurrentParryCount = 0;
		AerialPhaseStartTime = GetWorld()->GetTimeSeconds();
		StartHovering();
		StartParryDetection();
		break;

	case EBossPhase::Finisher:
		// Finisher phase handled by EnterFinisherPhase()
		break;
	}

	// Broadcast phase change
	OnPhaseChanged.Broadcast(OldPhase, NewPhase);
}

void ABossCharacter::CheckAerialPhaseTimeout()
{
	if (ShouldTransitionToGround())
	{
		SetPhase(EBossPhase::Ground);
	}
}

// ==================== Ground Phase: Arc Dash ====================

bool ABossCharacter::StartArcDash(AActor* Target)
{
	if (!CanDash() || !Target)
	{
		return false;
	}

	// Store target
	CurrentTarget = Target;

	// Calculate dash parameters
	DashStartPosition = GetActorLocation();
	DashTargetPosition = CalculateArcDashTarget(Target);
	DashArcControlPoint = CalculateArcControlPoint(DashStartPosition, DashTargetPosition, Target);

	// Calculate dash duration based on arc length (approximate)
	float DirectDistance = FVector::Dist(DashStartPosition, DashTargetPosition);
	float ArcLength = DirectDistance * 1.3f; // Arc is roughly 30% longer than direct path
	DashTotalDuration = ArcLength / DashSpeed;
	DashElapsedTime = 0.0f;

	// Start dash
	bIsDashing = true;
	LastDashTime = GetWorld()->GetTimeSeconds();

	// Disable regular movement during dash
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->SetMovementMode(MOVE_Flying);
	}

	return true;
}

bool ABossCharacter::CanDash() const
{
	if (bIsDead || bIsDashing || bIsInKnockback || bIsInFinisherPhase)
	{
		return false;
	}

	// Check cooldown
	float TimeSinceLastDash = GetWorld()->GetTimeSeconds() - LastDashTime;
	return TimeSinceLastDash >= DashCooldown;
}

FVector ABossCharacter::CalculateArcDashTarget(AActor* Target) const
{
	if (!Target)
	{
		return GetActorLocation();
	}

	FVector TargetLocation = Target->GetActorLocation();
	FVector BossLocation = GetActorLocation();

	// Direction from boss to player
	FVector ToPlayer = (TargetLocation - BossLocation).GetSafeNormal2D();

	// Random angle offset (either side of the player)
	float AngleOffset = FMath::RandRange(MinDashAngleOffset, MaxDashAngleOffset);
	if (FMath::RandBool())
	{
		AngleOffset = -AngleOffset;
	}

	// Rotate the direction by angle offset
	FVector OffsetDirection = ToPlayer.RotateAngleAxis(AngleOffset, FVector::UpVector);

	// Target position is at attack range distance from player in the offset direction
	FVector DashTarget = TargetLocation - OffsetDirection * DashTargetDistanceFromPlayer;

	// Clamp distance from current position
	float DistanceToTarget = FVector::Dist2D(BossLocation, DashTarget);
	if (DistanceToTarget > MaxDashDistance)
	{
		FVector DirectionToTarget = (DashTarget - BossLocation).GetSafeNormal2D();
		DashTarget = BossLocation + DirectionToTarget * MaxDashDistance;
	}

	// Keep same Z height (ground phase)
	DashTarget.Z = BossLocation.Z;

	return DashTarget;
}

FVector ABossCharacter::CalculateArcControlPoint(const FVector& Start, const FVector& End, AActor* Target) const
{
	// Midpoint between start and end
	FVector Midpoint = (Start + End) * 0.5f;

	// Direction perpendicular to the line (towards the player for a curved path around them)
	FVector LineDirection = (End - Start).GetSafeNormal2D();
	FVector PerpDirection = FVector::CrossProduct(LineDirection, FVector::UpVector);

	// Determine which side to curve towards (towards player makes more interesting arc)
	if (Target)
	{
		FVector ToPlayer = (Target->GetActorLocation() - Midpoint).GetSafeNormal2D();
		if (FVector::DotProduct(PerpDirection, ToPlayer) < 0)
		{
			PerpDirection = -PerpDirection;
		}
	}

	// Control point offset (creates the arc)
	float ArcIntensity = FVector::Dist2D(Start, End) * 0.3f;
	FVector ControlPoint = Midpoint + PerpDirection * ArcIntensity;
	ControlPoint.Z = Start.Z;

	return ControlPoint;
}

void ABossCharacter::UpdateArcDash(float DeltaTime)
{
	if (!bIsDashing)
	{
		return;
	}

	DashElapsedTime += DeltaTime;
	float Alpha = FMath::Clamp(DashElapsedTime / DashTotalDuration, 0.0f, 1.0f);

	// Evaluate position on bezier curve
	FVector NewPosition = EvaluateBezier(DashStartPosition, DashArcControlPoint, DashTargetPosition, Alpha);

	// Calculate facing direction (tangent of curve)
	float TangentAlpha = FMath::Clamp(Alpha + 0.01f, 0.0f, 1.0f);
	FVector TangentPosition = EvaluateBezier(DashStartPosition, DashArcControlPoint, DashTargetPosition, TangentAlpha);
	FVector FacingDirection = (TangentPosition - NewPosition).GetSafeNormal();

	// Face movement direction
	if (!FacingDirection.IsNearlyZero())
	{
		FRotator NewRotation = FacingDirection.Rotation();
		NewRotation.Pitch = 0.0f;
		NewRotation.Roll = 0.0f;
		SetActorRotation(NewRotation);
	}

	// Move to new position
	SetActorLocation(NewPosition);

	// Check if dash complete
	if (Alpha >= 1.0f)
	{
		EndDash();
	}
}

void ABossCharacter::EndDash()
{
	bIsDashing = false;

	// Restore walking movement
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->SetMovementMode(MOVE_Walking);
	}

	// Increment dash attack counter
	CurrentDashAttackCount++;

	// Start cooldown
	bDashOnCooldown = true;
	GetWorld()->GetTimerManager().SetTimer(DashCooldownTimer, this, &ABossCharacter::OnDashCooldownEnd, DashCooldown, false);
}

void ABossCharacter::OnDashCooldownEnd()
{
	bDashOnCooldown = false;
}

FVector ABossCharacter::EvaluateBezier(const FVector& P0, const FVector& P1, const FVector& P2, float T) const
{
	// Quadratic Bezier: B(t) = (1-t)^2 * P0 + 2*(1-t)*t * P1 + t^2 * P2
	float OneMinusT = 1.0f - T;
	return (OneMinusT * OneMinusT * P0) + (2.0f * OneMinusT * T * P1) + (T * T * P2);
}

// ==================== Ground Phase: Melee Attack ====================

void ABossCharacter::StartMeleeAttack(AActor* Target)
{
	if (!CanMeleeAttack() || !Target)
	{
		return;
	}

	CurrentTarget = Target;
	bIsAttacking = true;
	HitActorsThisAttack.Empty();

	// Face target
	FVector DirectionToTarget = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	if (!DirectionToTarget.IsNearlyZero())
	{
		FRotator NewRotation = DirectionToTarget.Rotation();
		NewRotation.Pitch = 0.0f;
		NewRotation.Roll = 0.0f;
		SetActorRotation(NewRotation);
	}

	// Play random attack montage
	if (MeleeAttackMontages.Num() > 0)
	{
		int32 MontageIndex = FMath::RandRange(0, MeleeAttackMontages.Num() - 1);
		UAnimMontage* SelectedMontage = MeleeAttackMontages[MontageIndex];

		if (SelectedMontage && GetMesh() && GetMesh()->GetAnimInstance())
		{
			UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
			AnimInstance->Montage_Play(SelectedMontage);

			// Bind montage end delegate
			FOnMontageEnded EndDelegate;
			EndDelegate.BindUObject(this, &ABossCharacter::OnAttackMontageEnded);
			AnimInstance->Montage_SetEndDelegate(EndDelegate, SelectedMontage);
		}
	}

	// Start damage window timer (use fixed timing for now, can be AnimNotify later)
	float DamageWindowStartDelay = 0.2f;
	float DamageWindowDuration = 0.3f;

	GetWorld()->GetTimerManager().SetTimer(DamageWindowStartTimer, this, &ABossCharacter::OnDamageWindowStart, DamageWindowStartDelay, false);
	GetWorld()->GetTimerManager().SetTimer(DamageWindowEndTimer, this, &ABossCharacter::OnDamageWindowEnd, DamageWindowStartDelay + DamageWindowDuration, false);

	// Record attack time
	LastMeleeAttackTime = GetWorld()->GetTimeSeconds();
}

bool ABossCharacter::CanMeleeAttack() const
{
	if (bIsDead || bIsAttacking || bIsInKnockback || bIsDashing || bIsInFinisherPhase)
	{
		return false;
	}

	// Check cooldown
	float TimeSinceLastAttack = GetWorld()->GetTimeSeconds() - LastMeleeAttackTime;
	return TimeSinceLastAttack >= MeleeAttackCooldown;
}

bool ABossCharacter::IsTargetInMeleeRange(AActor* Target) const
{
	if (!Target)
	{
		return false;
	}

	float Distance = FVector::Dist(GetActorLocation(), Target->GetActorLocation());
	return Distance <= MeleeAttackRange;
}

void ABossCharacter::OnDamageWindowStart()
{
	bDamageWindowActive = true;
}

void ABossCharacter::OnDamageWindowEnd()
{
	bDamageWindowActive = false;
}

void ABossCharacter::OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	bIsAttacking = false;
	bDamageWindowActive = false;

	// Start cooldown
	bMeleeOnCooldown = true;
	GetWorld()->GetTimerManager().SetTimer(MeleeCooldownTimer, this, &ABossCharacter::OnMeleeCooldownEnd, MeleeAttackCooldown, false);
}

void ABossCharacter::OnMeleeCooldownEnd()
{
	bMeleeOnCooldown = false;
}

void ABossCharacter::PerformMeleeTrace()
{
	if (!bDamageWindowActive)
	{
		return;
	}

	// Trace start/end
	FVector TraceStart = GetActorLocation() + FVector(0, 0, 50.0f);
	FVector TraceEnd = TraceStart + GetActorForwardVector() * MeleeTraceDistance;

	// Perform sphere trace
	TArray<FHitResult> HitResults;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	bool bHit = GetWorld()->SweepMultiByChannel(
		HitResults,
		TraceStart,
		TraceEnd,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(MeleeTraceRadius),
		QueryParams
	);

	if (bHit)
	{
		for (const FHitResult& Hit : HitResults)
		{
			AActor* HitActor = Hit.GetActor();
			if (HitActor && !HitActorsThisAttack.Contains(HitActor))
			{
				// Check if this is the player (has Player tag)
				if (HitActor->ActorHasTag(FName("Player")))
				{
					ApplyMeleeDamage(HitActor, Hit);
					HitActorsThisAttack.Add(HitActor);
				}
			}
		}
	}
}

void ABossCharacter::ApplyMeleeDamage(AActor* HitActor, const FHitResult& HitResult)
{
	if (!HitActor)
	{
		return;
	}

	// Apply damage
	FPointDamageEvent DamageEvent(MeleeAttackDamage, HitResult, GetActorForwardVector(), nullptr);
	HitActor->TakeDamage(MeleeAttackDamage, DamageEvent, GetController(), this);
}

// ==================== Aerial Phase ====================

void ABossCharacter::StartHovering()
{
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->SetMovementMode(MOVE_Flying);
	}

	// Fly to hover height
	if (FlyingMovement)
	{
		FVector CurrentLocation = GetActorLocation();
		FVector HoverLocation = CurrentLocation;
		HoverLocation.Z += AerialHoverHeight;

		FlyingMovement->FlyToLocation(HoverLocation);
	}
}

void ABossCharacter::StopHovering()
{
	if (FlyingMovement)
	{
		FlyingMovement->StopMovement();
	}

	// Return to walking
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->SetMovementMode(MOVE_Walking);
	}
}

void ABossCharacter::AerialStrafe(const FVector& Direction)
{
	if (CurrentPhase != EBossPhase::Aerial || !FlyingMovement)
	{
		return;
	}

	// Calculate strafe target
	FVector StrafeTarget = GetActorLocation() + Direction.GetSafeNormal() * 200.0f;
	FlyingMovement->FlyToLocation(StrafeTarget);
}

bool ABossCharacter::PerformAerialDash()
{
	if (!FlyingMovement || CurrentPhase != EBossPhase::Aerial)
	{
		return false;
	}

	// Random direction for evasion
	FVector RandomDirection = FMath::VRand();
	RandomDirection.Z = 0.0f;
	RandomDirection.Normalize();

	return FlyingMovement->StartDash(RandomDirection);
}

void ABossCharacter::MatchOppositePolarity(AActor* Target)
{
	if (!Target || !EMFVelocityModifier)
	{
		return;
	}

	// Get target's EMF component
	UEMFVelocityModifier* TargetEMF = Target->FindComponentByClass<UEMFVelocityModifier>();
	if (!TargetEMF)
	{
		return;
	}

	// Get target's charge sign and set our charge to opposite
	int32 TargetSign = TargetEMF->GetChargeSign();
	float BossCurrentCharge = EMFVelocityModifier->GetCharge();
	int32 CurrentSign = (BossCurrentCharge >= 0) ? 1 : -1;

	// If same sign, toggle to opposite
	if (CurrentSign == TargetSign)
	{
		EMFVelocityModifier->ToggleChargeSign();
	}
}

void ABossCharacter::RegisterParry()
{
	CurrentParryCount++;

	// Change polarity when parried (so returning projectile hits us)
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->ToggleChargeSign();
	}

	// Perform evasive dash after parry
	if (bDashAfterParry)
	{
		PerformAerialDash();
	}

	// Check if should transition to ground
	if (ShouldTransitionToGround())
	{
		SetPhase(EBossPhase::Ground);
	}
}

// ==================== Finisher Phase ====================

void ABossCharacter::EnterFinisherPhase()
{
	if (bIsInFinisherPhase)
	{
		return;
	}

	bIsInFinisherPhase = true;

	// Stop any current actions
	bIsDashing = false;
	bIsAttacking = false;

	// Transition to finisher phase
	ExecutePhaseTransition(EBossPhase::Finisher);

	// If on ground, take off
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->SetMovementMode(MOVE_Flying);
	}

	// Fly to finisher position
	if (FlyingMovement)
	{
		FVector FinisherPosition = GetActorLocation() + FinisherHoverOffset;
		FinisherPosition.Z = GetActorLocation().Z + FinisherHoverHeight;

		FlyingMovement->FlyToLocation(FinisherPosition);
	}

	// Spawn vulnerability VFX
	if (FinisherVulnerabilityVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAttached(
			FinisherVulnerabilityVFX,
			GetRootComponent(),
			NAME_None,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::SnapToTarget,
			true,
			true
		);
	}

	// Broadcast event
	OnFinisherReady.Broadcast();
}

void ABossCharacter::ExecuteFinisher(AActor* Attacker)
{
	if (!bIsInFinisherPhase)
	{
		return;
	}

	// Boss is defeated
	bIsInFinisherPhase = false;

	// Broadcast defeat event (for cutscene trigger)
	OnBossDefeated.Broadcast();

	// Actual death will be handled by cutscene/game flow
	// For now, just mark as dead
	CurrentHP = 0.0f;
	bIsDead = true;
}

// ==================== Target Management ====================

void ABossCharacter::SetTarget(AActor* NewTarget)
{
	CurrentTarget = NewTarget;
}

// ==================== Projectile Firing ====================

void ABossCharacter::FireEMFProjectile(AActor* Target)
{
	if (!Target)
	{
		return;
	}

	// Use inherited ShooterNPC shooting through weapon
	// This will fire the weapon's configured projectile (should be EMFProjectile)
	StartShooting(Target, true);

	// The projectile tracking is done via TrackProjectile() which should be called
	// after the weapon fires. For now, we rely on weapon's OnShotFired delegate
	// or the weapon can call TrackProjectile directly.

	// Record target's polarity at time of shot for parry detection
	if (Target)
	{
		UEMFVelocityModifier* TargetEMF = Target->FindComponentByClass<UEMFVelocityModifier>();
		if (TargetEMF)
		{
			// Store for later comparison (will be associated with projectile in TrackProjectile)
		}
	}
}

void ABossCharacter::TrackProjectile(AShooterProjectile* Projectile)
{
	if (!Projectile)
	{
		return;
	}

	// Add to tracking list
	TrackedProjectiles.Add(Projectile);

	// Store target's polarity at spawn time
	if (CurrentTarget.IsValid())
	{
		UEMFVelocityModifier* TargetEMF = CurrentTarget->FindComponentByClass<UEMFVelocityModifier>();
		if (TargetEMF)
		{
			ProjectileOriginalTargetPolarity.Add(Projectile, TargetEMF->GetChargeSign());
		}
	}
}

// ==================== Parry Detection ====================

void ABossCharacter::StartParryDetection()
{
	// Start periodic parry check
	GetWorld()->GetTimerManager().SetTimer(
		ParryCheckTimer,
		this,
		&ABossCharacter::OnParryCheckTimer,
		ParryCheckInterval,
		true // Looping
	);
}

void ABossCharacter::StopParryDetection()
{
	GetWorld()->GetTimerManager().ClearTimer(ParryCheckTimer);

	// Clean up tracking data
	TrackedProjectiles.Empty();
	ProjectileOriginalTargetPolarity.Empty();
}

void ABossCharacter::OnParryCheckTimer()
{
	CleanupTrackedProjectiles();
	CheckProjectilesForParry();
}

void ABossCharacter::CleanupTrackedProjectiles()
{
	// Remove invalid/destroyed projectiles
	for (int32 i = TrackedProjectiles.Num() - 1; i >= 0; --i)
	{
		if (!TrackedProjectiles[i].IsValid())
		{
			// Also remove from polarity map
			ProjectileOriginalTargetPolarity.Remove(TrackedProjectiles[i]);
			TrackedProjectiles.RemoveAt(i);
		}
	}
}

void ABossCharacter::CheckProjectilesForParry()
{
	for (TWeakObjectPtr<AShooterProjectile>& ProjectilePtr : TrackedProjectiles)
	{
		AShooterProjectile* Projectile = ProjectilePtr.Get();
		if (!Projectile)
		{
			continue;
		}

		if (IsProjectileReturning(Projectile))
		{
			// Projectile is being parried (returning to boss)
			RegisterParry();

			// Remove this projectile from tracking (parry registered)
			ProjectileOriginalTargetPolarity.Remove(ProjectilePtr);
			ProjectilePtr.Reset();

			// Only count one parry per check cycle
			break;
		}
	}
}

bool ABossCharacter::IsProjectileReturning(AShooterProjectile* Projectile) const
{
	if (!Projectile)
	{
		return false;
	}

	// Get projectile's current velocity
	FVector ProjectileVelocity = FVector::ZeroVector;

	// Try to get velocity from ProjectileMovementComponent
	UProjectileMovementComponent* ProjectileMovement = Projectile->FindComponentByClass<UProjectileMovementComponent>();
	if (ProjectileMovement)
	{
		ProjectileVelocity = ProjectileMovement->Velocity;
	}
	else
	{
		// Fallback: estimate from actor velocity
		ProjectileVelocity = Projectile->GetVelocity();
	}

	if (ProjectileVelocity.IsNearlyZero())
	{
		return false;
	}

	// Check distance to boss
	FVector ToBoss = GetActorLocation() - Projectile->GetActorLocation();
	float DistanceToBoss = ToBoss.Size();

	if (DistanceToBoss > ParryDetectionRadius)
	{
		// Too far, not considered returning yet
		return false;
	}

	// Check if projectile is moving towards boss
	FVector VelocityDir = ProjectileVelocity.GetSafeNormal();
	FVector ToBossDir = ToBoss.GetSafeNormal();

	float DotProduct = FVector::DotProduct(VelocityDir, ToBossDir);
	float AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(DotProduct));

	// If angle is small enough, projectile is heading towards boss
	if (AngleDegrees <= ParryReturnAngleThreshold)
	{
		// Additional check: did the player change polarity?
		// The projectile should be repelled by player and attracted to boss
		// This happens naturally via EMF, but we can verify by checking
		// if the projectile's charge now attracts it to boss

		AEMFProjectile* EMFProj = Cast<AEMFProjectile>(Projectile);
		if (EMFProj && EMFVelocityModifier)
		{
			float ProjectileCharge = EMFProj->GetProjectileCharge();
			float BossCharge = EMFVelocityModifier->GetCharge();

			// Opposite charges attract - if charges have opposite signs, projectile is attracted to boss
			bool bAttractedToBoss = (ProjectileCharge * BossCharge) < 0;

			if (bAttractedToBoss)
			{
				return true;
			}
		}
		else
		{
			// No EMF data, just use direction check
			return true;
		}
	}

	return false;
}
