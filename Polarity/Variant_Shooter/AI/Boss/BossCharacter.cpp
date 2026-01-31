// BossCharacter.cpp
// Hybrid boss character implementation

#include "BossCharacter.h"
#include "BossAIController.h"
#include "BossProjectile.h"
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

	// Set AI Controller class
	AIControllerClass = ABossAIController::StaticClass();

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

	// Update finisher knockback if in progress
	if (bIsFinisherKnockback)
	{
		UpdateFinisherKnockback(DeltaTime);
		return; // Skip all other updates during knockback
	}

	// Update arc dash if in progress
	if (bIsDashing)
	{
		UpdateArcDash(DeltaTime);
	}

	// Pull towards player and perform melee trace during attack
	if (bIsAttacking && CurrentTarget.IsValid())
	{
		UpdateMeleeAttackPull(DeltaTime);
	}
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
		OnDamageTaken.Broadcast(this, DamageToApply, TSubclassOf<UDamageType>(), GetActorLocation(), DamageCauser);

		// Enter finisher phase
		EnterFinisherPhase();

		return DamageToApply;
	}

	// Normal damage handling - but prevent auto-retaliation shooting in ground phase
	bool bWasShootingBefore = bIsShooting;
	float Result = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	// In ground phase, boss should NOT shoot - only melee
	if (CurrentPhase == EBossPhase::Ground && !bWasShootingBefore)
	{
		StopShooting();
	}

	return Result;
}

// ==================== Phase Control ====================

void ABossCharacter::SetPhase(EBossPhase NewPhase)
{
	FString PhaseNames[] = { TEXT("Ground"), TEXT("Aerial"), TEXT("Finisher") };
	UE_LOG(LogTemp, Warning, TEXT("[BOSS] SetPhase called: Current=%s, New=%s"),
		*PhaseNames[(int)CurrentPhase], *PhaseNames[(int)NewPhase]);

	if (CurrentPhase != NewPhase)
	{
		ExecutePhaseTransition(NewPhase);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BOSS] SetPhase: Already in %s phase, no transition needed"), *PhaseNames[(int)NewPhase]);
	}
}

bool ABossCharacter::ShouldTransitionToAerial() const
{
	if (CurrentPhase != EBossPhase::Ground)
	{
		return false;
	}

	// Check if still in cooldown after returning from aerial phase
	if (GroundPhaseStartTime > 0.0f)
	{
		float TimeInGround = GetWorld()->GetTimeSeconds() - GroundPhaseStartTime;
		if (TimeInGround < GroundPhaseCooldown)
		{
			return false;
		}
	}

	// Check if currently transitioning
	if (bIsTransitioning)
	{
		return false;
	}

	// Check HP threshold (only triggers once per fight)
	float HPPercent = CurrentHP / MaxHP;
	if (HPPercent <= AerialPhaseHPThreshold && !bHPThresholdTriggered)
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
	// Check parry count
	if (CurrentParryCount >= ParriesBeforeGroundPhase)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BOSS] ShouldTransitionToGround: TRUE (parry count %d >= %d)"),
			CurrentParryCount, ParriesBeforeGroundPhase);
		return true;
	}

	// Check timeout (uses AerialPhaseStartTime set when entering aerial phase)
	if (AerialPhaseStartTime > 0.0f)
	{
		float TimeInAerial = GetWorld()->GetTimeSeconds() - AerialPhaseStartTime;
		if (TimeInAerial >= MaxAerialPhaseDuration)
		{
			UE_LOG(LogTemp, Warning, TEXT("[BOSS] ShouldTransitionToGround: TRUE (timeout %.1f >= %.1f)"),
				TimeInAerial, MaxAerialPhaseDuration);
			return true;
		}
	}

	return false;
}

void ABossCharacter::ExecutePhaseTransition(EBossPhase NewPhase)
{
	EBossPhase OldPhase = CurrentPhase;

	// Debug: Print stack trace to see who called this
	FString PhaseNames[] = { TEXT("Ground"), TEXT("Aerial"), TEXT("Finisher") };
	UE_LOG(LogTemp, Error, TEXT("[BOSS PHASE] >>> TRANSITION: %s -> %s (HP=%.0f/%.0f, DashCount=%d)"),
		*PhaseNames[(int)OldPhase], *PhaseNames[(int)NewPhase],
		CurrentHP, MaxHP, CurrentDashAttackCount);

	// On-screen debug message (red, 5 seconds)
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red,
			FString::Printf(TEXT("BOSS PHASE: %s -> %s"), *PhaseNames[(int)OldPhase], *PhaseNames[(int)NewPhase]));
	}

	CurrentPhase = NewPhase;

	// Start transition - boss cannot attack until complete
	bIsTransitioning = true;
	float TransitionDuration = 0.0f;

	// Reset phase-specific counters and start movement
	switch (NewPhase)
	{
	case EBossPhase::Ground:
		CurrentDashAttackCount = 0;
		GroundPhaseStartTime = GetWorld()->GetTimeSeconds();
		StopHovering();
		StopParryDetection();
		TransitionDuration = LandingDuration;
		break;

	case EBossPhase::Aerial:
		CurrentParryCount = 0;
		AerialPhaseStartTime = GetWorld()->GetTimeSeconds();
		// Mark HP threshold as triggered so it doesn't keep firing
		if (!bHPThresholdTriggered && (CurrentHP / MaxHP) <= AerialPhaseHPThreshold)
		{
			bHPThresholdTriggered = true;
			UE_LOG(LogTemp, Warning, TEXT("[BOSS] HP threshold triggered (HP=%.0f/%.0f = %.1f%%), won't trigger again"),
				CurrentHP, MaxHP, (CurrentHP / MaxHP) * 100.0f);
		}
		StartHovering();
		StartParryDetection();
		TransitionDuration = TakeOffDuration;
		break;

	case EBossPhase::Finisher:
		// Finisher phase handled by EnterFinisherPhase()
		bIsTransitioning = false; // No transition delay for finisher
		break;
	}

	// Set timer to complete transition
	if (TransitionDuration > 0.0f)
	{
		GetWorld()->GetTimerManager().SetTimer(
			PhaseTransitionTimer,
			this,
			&ABossCharacter::OnPhaseTransitionComplete,
			TransitionDuration,
			false
		);
		UE_LOG(LogTemp, Warning, TEXT("[BOSS] Phase transition started, duration: %.2f seconds"), TransitionDuration);
	}

	// Broadcast phase change
	OnPhaseChanged.Broadcast(OldPhase, NewPhase);
}

void ABossCharacter::OnPhaseTransitionComplete()
{
	bIsTransitioning = false;
	UE_LOG(LogTemp, Warning, TEXT("[BOSS] Phase transition complete, boss can now attack. Phase=%d, Z=%.1f"),
		(int)CurrentPhase, GetActorLocation().Z);

	// On-screen debug message (red, 5 seconds)
	FString PhaseNames[] = { TEXT("Ground"), TEXT("Aerial"), TEXT("Finisher") };
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red,
			FString::Printf(TEXT("BOSS TRANSITION COMPLETE: Now in %s (Z=%.0f)"), *PhaseNames[(int)CurrentPhase], GetActorLocation().Z));
	}

	// If we landed (transitioned to Ground), ensure walking mode
	if (CurrentPhase == EBossPhase::Ground)
	{
		if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
		{
			// Boss should already be on ground from falling, just ensure walking mode
			if (MovementComp->IsMovingOnGround())
			{
				MovementComp->SetMovementMode(MOVE_Walking);
			}
			MovementComp->Velocity = FVector::ZeroVector;
		}
	}
}

void ABossCharacter::CheckAerialPhaseTimeout()
{
	if (ShouldTransitionToGround())
	{
		SetPhase(EBossPhase::Ground);
	}
}

// ==================== Ground Phase: Approach Dash ====================

bool ABossCharacter::StartApproachDash(AActor* Target)
{
	if (!CanDash() || !Target)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossDash] StartApproachDash FAILED - CanDash=%d, Target=%s"),
			CanDash(), Target ? *Target->GetName() : TEXT("NULL"));
		return false;
	}

	CurrentTarget = Target;
	bIsApproachDash = true;

	FVector PlayerPos = Target->GetActorLocation();
	FVector BossPos = GetActorLocation();
	DashStartPosition = BossPos; // Keep for Z reference

	// Calculate start position in polar coordinates (relative to player)
	FVector PlayerToBoss = BossPos - PlayerPos;
	DashStartRadius = PlayerToBoss.Size2D();
	DashStartAngle = FMath::Atan2(PlayerToBoss.Y, PlayerToBoss.X);

	// Target: random point on circle around player at melee range
	DashTargetRadius = DashTargetDistanceFromPlayer;

	// Pick random target angle within ±120 degrees
	float AngleOffsetDeg = FMath::RandRange(-120.0f, 120.0f);
	float AngleOffsetRad = FMath::DegreesToRadians(AngleOffsetDeg);
	DashTargetAngle = DashStartAngle + AngleOffsetRad;

	// Determine arc direction (shorter path around, but always outward from player)
	// Positive offset = counter-clockwise, negative = clockwise
	DashArcDirection = (AngleOffsetDeg >= 0) ? 1.0f : -1.0f;

	// Calculate approximate arc length for duration
	// Arc travels from StartAngle to TargetAngle while radius shrinks from StartRadius to TargetRadius
	float AngleDelta = FMath::Abs(AngleOffsetRad);
	float AverageRadius = (DashStartRadius + DashTargetRadius) * 0.5f;
	float ArcLength = AngleDelta * AverageRadius + FMath::Abs(DashStartRadius - DashTargetRadius);
	DashTotalDuration = FMath::Max(ArcLength / DashSpeed, 0.2f);
	DashElapsedTime = 0.0f;

	bIsDashing = true;
	LastDashTime = GetWorld()->GetTimeSeconds();

	// Disable EMF forces during dash
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->SetEnabled(false);
	}

	UE_LOG(LogTemp, Warning, TEXT("[BossDash] APPROACH DYNAMIC: AngleOffset=%.1f, StartRadius=%.0f -> TargetRadius=%.0f, Duration=%.2fs"),
		AngleOffsetDeg, DashStartRadius, DashTargetRadius, DashTotalDuration);

	return true;
}

// ==================== Ground Phase: Circle Dash ====================

bool ABossCharacter::StartCircleDash(AActor* Target)
{
	// Circle Dash does NOT check cooldown - it chains immediately after Approach Dash
	// Only check basic state (not dead, not already dashing, etc.)
	if (!Target || bIsDead || bIsDashing || bIsInKnockback || bIsInFinisherPhase)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossDash] StartCircleDash FAILED - Target=%s, bIsDead=%d, bIsDashing=%d"),
			Target ? *Target->GetName() : TEXT("NULL"), bIsDead, bIsDashing);
		return false;
	}

	CurrentTarget = Target;
	bIsApproachDash = false;

	FVector PlayerPos = Target->GetActorLocation();
	FVector BossPos = GetActorLocation();
	DashStartPosition = BossPos; // Keep for Z reference

	// Calculate start position in polar coordinates
	FVector PlayerToBoss = BossPos - PlayerPos;
	DashStartRadius = FMath::Max(PlayerToBoss.Size2D(), DashTargetDistanceFromPlayer);
	DashStartAngle = FMath::Atan2(PlayerToBoss.Y, PlayerToBoss.X);

	// Circle dash keeps the same radius
	DashTargetRadius = DashStartRadius;

	// Random angle offset (45-135 degrees either direction)
	float AngleOffsetDeg = FMath::RandRange(MinDashAngleOffset, MaxDashAngleOffset);
	if (FMath::RandBool())
	{
		AngleOffsetDeg = -AngleOffsetDeg;
	}
	float AngleOffsetRad = FMath::DegreesToRadians(AngleOffsetDeg);
	DashTargetAngle = DashStartAngle + AngleOffsetRad;
	DashArcDirection = (AngleOffsetDeg >= 0) ? 1.0f : -1.0f;

	// Calculate duration based on arc length
	float ArcLength = FMath::Abs(AngleOffsetRad) * DashStartRadius;
	DashTotalDuration = FMath::Max(ArcLength / DashSpeed, 0.2f);
	DashElapsedTime = 0.0f;

	bIsDashing = true;
	LastDashTime = GetWorld()->GetTimeSeconds();

	// Disable EMF forces during dash
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->SetEnabled(false);
	}

	UE_LOG(LogTemp, Warning, TEXT("[BossDash] CIRCLE DYNAMIC: AngleOffset=%.1f, Radius=%.0f, Duration=%.2fs"),
		AngleOffsetDeg, DashStartRadius, DashTotalDuration);

	return true;
}

bool ABossCharacter::IsTargetFar(AActor* Target) const
{
	if (!Target)
	{
		return true;
	}
	float Distance = FVector::Dist2D(GetActorLocation(), Target->GetActorLocation());
	// Consider "far" if beyond melee range + some buffer
	return Distance > (MeleeAttackRange + 100.0f);
}

bool ABossCharacter::CanDash() const
{
	if (bIsDead || bIsDashing || bIsInKnockback || bIsInFinisherPhase || bIsTransitioning)
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

	// Direction from player to boss (we want to end up on the other side)
	FVector FromPlayerToBoss = (BossLocation - TargetLocation).GetSafeNormal2D();

	// Random angle offset - pick a point around the player, offset from our current angle
	// Positive angle = clockwise, Negative = counter-clockwise (looking down)
	float AngleOffset = FMath::RandRange(MinDashAngleOffset, MaxDashAngleOffset);
	if (FMath::RandBool())
	{
		AngleOffset = -AngleOffset;
	}

	// Rotate to get direction from player to dash target position
	FVector DirectionToTarget = FromPlayerToBoss.RotateAngleAxis(AngleOffset, FVector::UpVector);

	// Dash target is on a circle around the player at melee range
	FVector DashTarget = TargetLocation + DirectionToTarget * DashTargetDistanceFromPlayer;

	// Clamp distance from current boss position to max dash distance
	float DistanceToTarget = FVector::Dist2D(BossLocation, DashTarget);
	if (DistanceToTarget > MaxDashDistance)
	{
		// If too far, move target closer along the line from boss to target
		FVector DirectionFromBoss = (DashTarget - BossLocation).GetSafeNormal2D();
		DashTarget = BossLocation + DirectionFromBoss * MaxDashDistance;
	}

	// Keep same Z height (ground phase)
	DashTarget.Z = BossLocation.Z;

	UE_LOG(LogTemp, Warning, TEXT("[BossDash] Target calc: Boss(%s) -> DashTarget(%s), Angle=%.1f, DistFromPlayer=%.1f"),
		*BossLocation.ToString(), *DashTarget.ToString(), AngleOffset, FVector::Dist2D(TargetLocation, DashTarget));

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
	if (!bIsDashing || !CurrentTarget.IsValid())
	{
		if (bIsDashing)
		{
			EndDash();
		}
		return;
	}

	DashElapsedTime += DeltaTime;
	float Alpha = FMath::Clamp(DashElapsedTime / DashTotalDuration, 0.0f, 1.0f);

	FVector PlayerPos = CurrentTarget->GetActorLocation();
	FVector NewPosition;

	// Both dash types use polar coordinates relative to CURRENT player position
	// This makes the dash dynamically track the player

	// Interpolate angle and radius
	float CurrentAngle = FMath::Lerp(DashStartAngle, DashTargetAngle, Alpha);
	float CurrentRadius = FMath::Lerp(DashStartRadius, DashTargetRadius, Alpha);

	// Convert polar to cartesian, centered on current player position
	NewPosition.X = PlayerPos.X + FMath::Cos(CurrentAngle) * CurrentRadius;
	NewPosition.Y = PlayerPos.Y + FMath::Sin(CurrentAngle) * CurrentRadius;
	NewPosition.Z = DashStartPosition.Z;

	// Face the player during dash
	FVector ToPlayerDir = (PlayerPos - NewPosition).GetSafeNormal2D();
	if (!ToPlayerDir.IsNearlyZero())
	{
		FRotator NewRotation = ToPlayerDir.Rotation();
		NewRotation.Pitch = 0.0f;
		NewRotation.Roll = 0.0f;
		SetActorRotation(NewRotation);
	}

	// Move to new position with sweep (like MeleeNPC does)
	FVector CurrentPos = GetActorLocation();
	SetActorLocation(NewPosition, true);

	// Update velocity for visuals and animations (like MeleeNPC does)
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		if (DeltaTime > 0.0f)
		{
			FVector FrameVelocity = (NewPosition - CurrentPos) / DeltaTime;
			MovementComp->Velocity = FrameVelocity;
		}
	}

	// Check if dash complete
	if (Alpha >= 1.0f)
	{
		EndDash();
	}
}

void ABossCharacter::EndDash()
{
	bIsDashing = false;

	// Stop velocity and restore walking movement (like MeleeNPC does)
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->Velocity = FVector::ZeroVector;
		MovementComp->SetMovementMode(MOVE_Walking);
	}

	// Re-enable EMF forces
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->SetEnabled(true);
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
		UE_LOG(LogTemp, Warning, TEXT("[BossMelee] StartMeleeAttack FAILED - CanMeleeAttack=%d, Target=%s, bIsAttacking=%d, bIsDashing=%d"),
			CanMeleeAttack(), Target ? *Target->GetName() : TEXT("NULL"), bIsAttacking, bIsDashing);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[BossMelee] StartMeleeAttack SUCCESS - Target=%s"), *Target->GetName());

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

void ABossCharacter::UpdateMeleeAttackPull(float DeltaTime)
{
	if (!bIsAttacking || !CurrentTarget.IsValid() || MeleeAttackPullSpeed <= 0.0f)
	{
		return;
	}

	FVector BossLocation = GetActorLocation();
	FVector PlayerLocation = CurrentTarget->GetActorLocation();

	// Calculate direction to player (2D only, keep Z)
	FVector ToPlayer = PlayerLocation - BossLocation;
	ToPlayer.Z = 0.0f;

	float DistanceToPlayer = ToPlayer.Size();

	// Don't pull if already very close
	if (DistanceToPlayer < 50.0f)
	{
		return;
	}

	// Calculate pull movement
	FVector PullDirection = ToPlayer.GetSafeNormal();
	float PullDistance = MeleeAttackPullSpeed * DeltaTime;

	// Don't overshoot the player
	PullDistance = FMath::Min(PullDistance, DistanceToPlayer - 50.0f);

	FVector NewLocation = BossLocation + PullDirection * PullDistance;
	NewLocation.Z = BossLocation.Z; // Keep same height

	// Move with sweep to avoid going through walls
	SetActorLocation(NewLocation, true);

	// Face the player during pull
	if (!PullDirection.IsNearlyZero())
	{
		FRotator NewRotation = PullDirection.Rotation();
		NewRotation.Pitch = 0.0f;
		NewRotation.Roll = 0.0f;
		SetActorRotation(NewRotation);
	}
}

// ==================== Aerial Phase ====================

void ABossCharacter::StartHovering()
{
	UE_LOG(LogTemp, Warning, TEXT("[BOSS] StartHovering() called! CurrentPhase=%d"), (int)CurrentPhase);

	// Enable forced flying mode for aerial phase
	if (FlyingMovement)
	{
		FlyingMovement->bEnforceFlyingMode = true;
	}

	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->SetMovementMode(MOVE_Flying);
		MovementComp->GravityScale = 0.0f; // Disable gravity for flying
	}

	// Fly to hover height
	if (FlyingMovement)
	{
		FVector CurrentLocation = GetActorLocation();
		FVector HoverLocation = CurrentLocation;
		HoverLocation.Z += AerialHoverHeight;

		// Calculate speed based on height and take off duration
		float TakeOffSpeed = AerialHoverHeight / FMath::Max(TakeOffDuration, 0.1f);
		FlyingMovement->FlySpeed = TakeOffSpeed;
		FlyingMovement->FlyToLocation(HoverLocation);

		UE_LOG(LogTemp, Warning, TEXT("[BOSS] Taking off: Z %.1f -> %.1f (speed=%.1f)"),
			CurrentLocation.Z, HoverLocation.Z, TakeOffSpeed);
	}
}

void ABossCharacter::StopHovering()
{
	UE_LOG(LogTemp, Warning, TEXT("[BOSS] StopHovering() called! Current Z=%.1f"), GetActorLocation().Z);

	// Disable forced flying mode so boss can fall
	if (FlyingMovement)
	{
		FlyingMovement->bEnforceFlyingMode = false;
		FlyingMovement->StopMovement();
	}

	// Switch to walking mode and enable gravity - boss will fall naturally
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->SetMovementMode(MOVE_Falling);
		MovementComp->GravityScale = 1.0f;
		UE_LOG(LogTemp, Warning, TEXT("[BOSS] Gravity enabled, boss will fall to ground"));
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
	if (!FlyingMovement || CurrentPhase != EBossPhase::Aerial || bIsTransitioning)
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

	UE_LOG(LogTemp, Warning, TEXT("[BossCharacter] RegisterParry: ParryCount=%d/%d"),
		CurrentParryCount, ParriesBeforeGroundPhase);

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

void ABossCharacter::OnProjectileParried(ABossProjectile* Projectile)
{
	if (!Projectile)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[BossCharacter] OnProjectileParried called!"));

	// Change boss polarity to OPPOSITE of projectile
	// This ensures the projectile is ATTRACTED to boss (opposite charges attract)
	if (EMFVelocityModifier)
	{
		float ProjectileCharge = Projectile->GetProjectileCharge();
		float BossCharge = EMFVelocityModifier->GetCharge();

		// If same sign (would repel), toggle to opposite
		if ((ProjectileCharge * BossCharge) > 0.0f)
		{
			EMFVelocityModifier->ToggleChargeSign();
			UE_LOG(LogTemp, Log, TEXT("[BossCharacter] Toggled polarity to attract parried projectile"));
		}
	}

	// Register the parry (increments counter, does dash, checks phase transition)
	RegisterParry();
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

	// Teleport to finisher position
	TeleportToFinisherPosition();

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

void ABossCharacter::TeleportToFinisherPosition()
{
	FVector OldPosition = GetActorLocation();

	// Spawn disappear VFX at old position
	if (TeleportDisappearVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			TeleportDisappearVFX,
			OldPosition,
			GetActorRotation(),
			FVector(1.0f),
			true,
			true
		);
	}

	// Teleport to finisher position
	SetActorLocation(FinisherTeleportPosition);

	// Set flying mode
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->SetMovementMode(MOVE_Flying);
		MovementComp->Velocity = FVector::ZeroVector;
	}

	// Stop flying movement
	if (FlyingMovement)
	{
		FlyingMovement->StopFlying();
	}

	// Spawn appear VFX at new position
	if (TeleportAppearVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			TeleportAppearVFX,
			FinisherTeleportPosition,
			GetActorRotation(),
			FVector(1.0f),
			true,
			true
		);
	}

	UE_LOG(LogTemp, Warning, TEXT("[BOSS] Teleported to finisher position: %s"), *FinisherTeleportPosition.ToString());
}

void ABossCharacter::ExecuteFinisher(AActor* Attacker)
{
	if (!bIsInFinisherPhase || bIsFinisherKnockback)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[BOSS] ExecuteFinisher called, starting knockback"));

	// Start knockback sequence instead of instant death
	StartFinisherKnockback();
}

void ABossCharacter::StartFinisherKnockback()
{
	bIsFinisherKnockback = true;
	bIsInFinisherPhase = false; // No longer in finisher phase

	// Calculate knockback positions
	FinisherKnockbackStartPos = GetActorLocation();
	FVector NormalizedDirection = FinisherKnockbackDirection.GetSafeNormal();
	FinisherKnockbackEndPos = FinisherKnockbackStartPos + NormalizedDirection * FinisherKnockbackDistance;
	FinisherKnockbackElapsed = 0.0f;

	// Play knockback animation
	if (FinisherKnockbackMontage)
	{
		if (USkeletalMeshComponent* MeshComp = GetMesh())
		{
			if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
			{
				AnimInstance->Montage_Play(FinisherKnockbackMontage);
			}
		}
	}

	// Disable movement
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->DisableMovement();
	}

	UE_LOG(LogTemp, Warning, TEXT("[BOSS] Knockback started: %s -> %s over %.2f seconds"),
		*FinisherKnockbackStartPos.ToString(), *FinisherKnockbackEndPos.ToString(), FinisherKnockbackDuration);
}

void ABossCharacter::UpdateFinisherKnockback(float DeltaTime)
{
	FinisherKnockbackElapsed += DeltaTime;

	float Alpha = FMath::Clamp(FinisherKnockbackElapsed / FinisherKnockbackDuration, 0.0f, 1.0f);

	// Use EaseOut for knockback (fast start, slow at end)
	float EasedAlpha = 1.0f - FMath::Pow(1.0f - Alpha, 2.0f);

	FVector NewPosition = FMath::Lerp(FinisherKnockbackStartPos, FinisherKnockbackEndPos, EasedAlpha);
	SetActorLocation(NewPosition);

	if (Alpha >= 1.0f)
	{
		OnFinisherKnockbackComplete();
	}
}

void ABossCharacter::OnFinisherKnockbackComplete()
{
	UE_LOG(LogTemp, Warning, TEXT("[BOSS] Knockback complete, triggering death"));

	bIsFinisherKnockback = false;

	// Spawn death VFX
	if (FinisherDeathVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			FinisherDeathVFX,
			GetActorLocation(),
			GetActorRotation(),
			FVector(1.0f),
			true,
			true
		);
	}

	// Enable ragdoll on mesh
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetSimulatePhysics(true);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);

		// Apply small impulse in knockback direction for dramatic effect
		FVector Impulse = FinisherKnockbackDirection.GetSafeNormal() * 500.0f;
		MeshComp->AddImpulse(Impulse, NAME_None, true);
	}

	// Disable capsule collision
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// Broadcast defeat event (for cutscene trigger)
	OnBossDefeated.Broadcast();

	// Mark as dead
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
	// Cannot shoot while transitioning between phases
	if (bIsTransitioning)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossCharacter] FireEMFProjectile: Cannot shoot while transitioning"));
		return;
	}

	if (!Target)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BossCharacter] FireEMFProjectile: No target"));
		return;
	}

	if (!BossProjectileClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[BossCharacter] FireEMFProjectile: BossProjectileClass not set!"));
		return;
	}

	// Calculate spawn transform - from boss towards target
	FVector MuzzleLocation = GetActorLocation() + GetActorForwardVector() * 100.0f + FVector(0, 0, 50.0f);
	FVector DirectionToTarget = (Target->GetActorLocation() - MuzzleLocation).GetSafeNormal();
	FRotator SpawnRotation = DirectionToTarget.Rotation();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;

	// Spawn BossProjectile
	ABossProjectile* Projectile = GetWorld()->SpawnActor<ABossProjectile>(
		BossProjectileClass,
		MuzzleLocation,
		SpawnRotation,
		SpawnParams
	);

	if (Projectile)
	{
		// Initialize for boss - sets opposite charge to player, stores references
		Projectile->InitializeForBoss(this, Target);

		// Set projectile velocity
		UProjectileMovementComponent* ProjMovement = Projectile->FindComponentByClass<UProjectileMovementComponent>();
		if (ProjMovement)
		{
			ProjMovement->Velocity = DirectionToTarget * ProjectileSpeed;
			ProjMovement->InitialSpeed = ProjectileSpeed;
			ProjMovement->MaxSpeed = ProjectileSpeed * 2.0f;
		}

		// Track for legacy parry detection (can be removed later)
		TrackProjectile(Projectile);

		UE_LOG(LogTemp, Log, TEXT("[BossCharacter] Fired BossProjectile at %s"), *Target->GetName());
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
