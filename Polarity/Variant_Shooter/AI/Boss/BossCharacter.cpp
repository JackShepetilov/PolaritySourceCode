// BossCharacter.cpp
// Hybrid boss character implementation

#include "BossCharacter.h"
#include "BossAIController.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Polarity/Arena/ArenaManager.h"
#include "EMFVelocityModifier.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/DamageEvents.h"
#include "Components/CapsuleComponent.h"
#include "Animation/AnimInstance.h"
#include "Curves/CurveFloat.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "DrawDebugHelpers.h"

namespace
{
	// Safe phase-name lookup that won't read past the array if a stale StateTree asset
	// hands us a value outside the current EBossPhase enum (e.g. the old "Finisher = 2"
	// from when EBossPhase still had an Aerial member).
	FString BossPhaseName(EBossPhase Phase)
	{
		switch (Phase)
		{
		case EBossPhase::Ground:   return TEXT("Ground");
		case EBossPhase::Finisher: return TEXT("Finisher");
		default:                   return FString::Printf(TEXT("Unknown(%d)"), (int)Phase);
		}
	}

	bool IsValidBossPhase(EBossPhase Phase)
	{
		return Phase == EBossPhase::Ground || Phase == EBossPhase::Finisher;
	}
}

ABossCharacter::ABossCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Set AI Controller class
	AIControllerClass = ABossAIController::StaticClass();

	// Boss-specific defaults
	CurrentHP = 1000.0f;
	MaxHP = 1000.0f;
}

void ABossCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Cache max HP for Posture threshold calculations
	MaxHP = CurrentHP;

	CurrentPhase = EBossPhase::Ground;

	// Cache default walk speed for slowdown restoration
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		DefaultMaxWalkSpeed = MovementComp->MaxWalkSpeed;
	}

	// Subscribe to the arena's prop-percent broadcast so Posture regen can scale with it.
	// Sync-load the arena once (it's expected to be in the persistent or already-loaded sublevel).
	if (AArenaManager* Arena = LinkedArena.LoadSynchronous())
	{
		Arena->OnPropPercentChanged.AddDynamic(this, &ABossCharacter::OnArenaPropPercentChanged);
		UE_LOG(LogTemp, Log, TEXT("[BOSS] Subscribed to LinkedArena (%s) prop-percent broadcast"), *Arena->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BOSS] LinkedArena not set; Posture regen will use fallback formula at full prop percent."));
	}

	// ===== Boot-time debug dump =====
	UE_LOG(LogTemp, Warning,
		TEXT("[BOSS_DEBUG] BeginPlay: Phase=%s, HP=%.0f/%.0f, DefaultMaxWalkSpeed=%.0f, "
			 "ApproachDashMontage=%s, CircleDashMontage=%s, MeleeAttackMontages=%d, "
			 "FinisherEnterStunMontage=%s, FinisherKnockbackMontage=%s, "
			 "Controller=%s, LinkedArena=%s"),
		*BossPhaseName(CurrentPhase), CurrentHP, MaxHP, DefaultMaxWalkSpeed,
		*GetNameSafe(ApproachDashMontage), *GetNameSafe(CircleDashMontage), MeleeAttackMontages.Num(),
		*GetNameSafe(FinisherEnterStunMontage), *GetNameSafe(FinisherKnockbackMontage),
		*GetNameSafe(GetController()), *GetNameSafe(LinkedArena.Get()));

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Green,
			FString::Printf(TEXT("[BOSS_DEBUG] BeginPlay HP=%.0f Montages=%d Ctrl=%s Arena=%s"),
				CurrentHP, MeleeAttackMontages.Num(),
				*GetNameSafe(GetController()), *GetNameSafe(LinkedArena.Get())));
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

	// Posture regen scales with datacenter prop percent
	UpdatePostureRegen(DeltaTime);

	// ===== Throttled state debug =====
	static double LastBossDebugLogTime = -1.0;
	const double NowSec = GetWorld()->GetTimeSeconds();
	if (NowSec - LastBossDebugLogTime >= 1.0)
	{
		LastBossDebugLogTime = NowSec;
		const FString TargetName = CurrentTarget.IsValid() ? CurrentTarget->GetName() : TEXT("NONE");
		UE_LOG(LogTemp, Warning,
			TEXT("[BOSS_DEBUG] State: Phase=%s Trans=%d Dash=%d(Cool=%d) Att=%d(Cool=%d) Windup=%d DmgWin=%d Slowed=%d HP=%.0f/%.0f ArenaPct=%.2f Tgt=%s"),
			*BossPhaseName(CurrentPhase), bIsTransitioning,
			bIsDashing, bDashOnCooldown, bIsAttacking, bMeleeOnCooldown,
			bIsInMeleeWindup, bDamageWindowActive, bIsSlowed,
			CurrentHP, MaxHP, CachedArenaPropPercent, *TargetName);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(1001, 1.2f, FColor::Cyan,
				FString::Printf(TEXT("BOSS %s | Dsh=%d Att=%d Wnd=%d Trans=%d | HP=%.0f Pct=%.2f | Tgt=%s"),
					*BossPhaseName(CurrentPhase), bIsDashing, bIsAttacking, bIsInMeleeWindup, bIsTransitioning,
					CurrentHP, CachedArenaPropPercent, *TargetName));
		}
	}
}

void ABossCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clear all timers
	GetWorld()->GetTimerManager().ClearTimer(DashCooldownTimer);
	GetWorld()->GetTimerManager().ClearTimer(MeleeCooldownTimer);
	GetWorld()->GetTimerManager().ClearTimer(DamageWindowStartTimer);
	GetWorld()->GetTimerManager().ClearTimer(DamageWindowEndTimer);

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

	// Counter: any hit during boss windup or dash absorbs damage and slows the boss instead.
	if (IsBeingCountered(DamageCauser))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BOSS] Countered by %s (windup=%d, dashing=%d) — damage absorbed, applying slowdown"),
			DamageCauser ? *DamageCauser->GetName() : TEXT("NULL"), bIsInMeleeWindup, bIsDashing);

		// Cancel any in-progress windup attack so the boss doesn't follow through with the swing
		bIsInMeleeWindup = false;
		bIsAttacking = false;
		bDamageWindowActive = false;
		GetWorld()->GetTimerManager().ClearTimer(DamageWindowStartTimer);
		GetWorld()->GetTimerManager().ClearTimer(DamageWindowEndTimer);

		// Also cancel a dash if we were countered mid-dash
		if (bIsDashing)
		{
			EndDash();
		}

		ApplyExplosionStun(CounterSlowdownDuration, nullptr);
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
	if (!IsValidBossPhase(NewPhase))
	{
		// Rate-limited so a StateTree task that gets re-entered every frame doesn't flood the log.
		static double LastInvalidPhaseWarnTime = -10.0;
		const double NowSec = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (NowSec - LastInvalidPhaseWarnTime >= 5.0)
		{
			LastInvalidPhaseWarnTime = NowSec;
			UE_LOG(LogTemp, Error,
				TEXT("[BOSS] SetPhase called with invalid value %d. The StateTree asset still stores the old Aerial=1/Finisher=2 mapping — open ST_Boss and either DELETE the BossSetPhase task or re-pick the phase enum dropdown. Suppressing this warning for 5s."),
				(int)NewPhase);
		}
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[BOSS] SetPhase called: Current=%s, New=%s"),
		*BossPhaseName(CurrentPhase), *BossPhaseName(NewPhase));

	if (CurrentPhase != NewPhase)
	{
		ExecutePhaseTransition(NewPhase);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BOSS] SetPhase: Already in %s phase, no transition needed"), *BossPhaseName(NewPhase));
	}
}

void ABossCharacter::ExecutePhaseTransition(EBossPhase NewPhase)
{
	if (!IsValidBossPhase(NewPhase))
	{
		UE_LOG(LogTemp, Error, TEXT("[BOSS] ExecutePhaseTransition: invalid phase %d, ignoring."), (int)NewPhase);
		return;
	}

	const EBossPhase OldPhase = CurrentPhase;

	UE_LOG(LogTemp, Error, TEXT("[BOSS PHASE] >>> TRANSITION: %s -> %s (HP=%.0f/%.0f)"),
		*BossPhaseName(OldPhase), *BossPhaseName(NewPhase),
		CurrentHP, MaxHP);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red,
			FString::Printf(TEXT("BOSS PHASE: %s -> %s"), *BossPhaseName(OldPhase), *BossPhaseName(NewPhase)));
	}

	CurrentPhase = NewPhase;

	bIsTransitioning = true;
	float TransitionDuration = 0.0f;

	switch (NewPhase)
	{
	case EBossPhase::Ground:
		// Ground transition completes immediately (no aerial phase to land from)
		if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
		{
			MovementComp->SetMovementMode(MOVE_Walking);
		}
		bIsTransitioning = false;
		break;

	case EBossPhase::Finisher:
		// Finisher phase handled by EnterFinisherPhase()
		bIsTransitioning = false;
		break;

	default:
		// Unreachable — IsValidBossPhase above filters this.
		break;
	}

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

	OnPhaseChanged.Broadcast(OldPhase, NewPhase);
}

void ABossCharacter::OnPhaseTransitionComplete()
{
	if (CurrentPhase == EBossPhase::Ground)
	{
		if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
		{
			if (MovementComp->IsFalling())
			{
				UE_LOG(LogTemp, Warning, TEXT("[BOSS] OnPhaseTransitionComplete called but still falling (Z=%.1f) - waiting for Landed()"),
					GetActorLocation().Z);
				return;
			}

			MovementComp->SetMovementMode(MOVE_Walking);
			MovementComp->Velocity = FVector::ZeroVector;
		}
	}

	bIsTransitioning = false;
	UE_LOG(LogTemp, Warning, TEXT("[BOSS] Phase transition complete, boss can now attack. Phase=%s, Z=%.1f"),
		*BossPhaseName(CurrentPhase), GetActorLocation().Z);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green,
			FString::Printf(TEXT("BOSS TRANSITION COMPLETE: Now in %s (Z=%.0f)"), *BossPhaseName(CurrentPhase), GetActorLocation().Z));
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

	// Calculate approximate arc length for fallback duration
	float AngleDelta = FMath::Abs(AngleOffsetRad);
	float AverageRadius = (DashStartRadius + DashTargetRadius) * 0.5f;
	float ArcLength = AngleDelta * AverageRadius + FMath::Abs(DashStartRadius - DashTargetRadius);
	float FallbackDuration = FMath::Max(ArcLength / DashSpeed, 0.2f);

	// Duration is determined by montage length if available, otherwise by arc calculation
	if (ApproachDashMontage)
	{
		DashTotalDuration = ApproachDashMontage->GetPlayLength();
		UE_LOG(LogTemp, Warning, TEXT("[BossDash] Using montage length for duration: %.2fs"), DashTotalDuration);
	}
	else
	{
		DashTotalDuration = FallbackDuration;
		UE_LOG(LogTemp, Warning, TEXT("[BossDash] No montage, using calculated duration: %.2fs"), DashTotalDuration);
	}
	DashElapsedTime = 0.0f;

	bIsDashing = true;
	LastDashTime = GetWorld()->GetTimeSeconds();

	// Disable EMF forces during dash
	if (EMFVelocityModifier)
	{
		EMFVelocityModifier->SetEnabled(false);
	}

	// Crossfade into approach dash montage (architectural crossfade — no per-asset blend reliance)
	CrossfadeToMontage(ApproachDashMontage, DashStartBlendTime);

	UE_LOG(LogTemp, Warning, TEXT("[BossDash] APPROACH DYNAMIC: AngleOffset=%.1f, StartRadius=%.0f -> TargetRadius=%.0f, Duration=%.2fs (Arc would be %.2fs)"),
		AngleOffsetDeg, DashStartRadius, DashTargetRadius, DashTotalDuration, FallbackDuration);

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

	// Crossfade into circle dash montage
	CrossfadeToMontage(CircleDashMontage, DashStartBlendTime);

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
		UE_LOG(LogTemp, Warning, TEXT("[BossDash] CanDash=FALSE: Dead=%d, Dashing=%d, Knockback=%d, Finisher=%d, Transitioning=%d"),
			bIsDead, bIsDashing, bIsInKnockback, bIsInFinisherPhase, bIsTransitioning);
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
	bIsInMeleeWindup = true; // counter window opens until OnDamageWindowStart
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

	// Crossfade from (currently-playing) dash montage into the attack montage
	if (MeleeAttackMontages.Num() > 0)
	{
		int32 MontageIndex = FMath::RandRange(0, MeleeAttackMontages.Num() - 1);
		UAnimMontage* SelectedMontage = MeleeAttackMontages[MontageIndex];

		if (SelectedMontage)
		{
			CrossfadeToMontage(SelectedMontage, DashToAttackBlendTime);

			// Bind montage end delegate so OnAttackMontageEnded fires for cooldown handling
			if (USkeletalMeshComponent* MeshComp = GetMesh())
			{
				if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
				{
					FOnMontageEnded EndDelegate;
					EndDelegate.BindUObject(this, &ABossCharacter::OnAttackMontageEnded);
					AnimInstance->Montage_SetEndDelegate(EndDelegate, SelectedMontage);
				}
			}
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
	bIsInMeleeWindup = false; // counter window closes once the swing becomes live
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

// ==================== Finisher Phase ====================

void ABossCharacter::EnterFinisherPhase()
{
	if (bIsInFinisherPhase)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[BOSS] EnterFinisherPhase: Posture broken, freezing on the spot. CurrentPhase=%d"),
		(int)CurrentPhase);

	bIsInFinisherPhase = true;

	// Cancel any ongoing phase transition
	bIsTransitioning = false;
	GetWorld()->GetTimerManager().ClearTimer(PhaseTransitionTimer);

	// Stop any current actions
	bIsDashing = false;
	bIsAttacking = false;
	bDamageWindowActive = false;
	bIsInMeleeWindup = false;

	// Cancel knockback completely
	bIsInKnockback = false;
	bIsKnockbackInterpolating = false;
	KnockbackElapsedTime = 0.0f;

	// Cancel slowdown — finisher freeze overrides it
	if (bIsSlowed)
	{
		EndSlowdown();
	}

	// Stop all combat timers
	GetWorld()->GetTimerManager().ClearTimer(DashCooldownTimer);
	GetWorld()->GetTimerManager().ClearTimer(MeleeCooldownTimer);
	GetWorld()->GetTimerManager().ClearTimer(DamageWindowStartTimer);
	GetWorld()->GetTimerManager().ClearTimer(DamageWindowEndTimer);
	GetWorld()->GetTimerManager().ClearTimer(SlowdownRecoveryTimer);

	// Stop any montages and clear crossfade state
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
		{
			AnimInstance->StopAllMontages(0.1f);
		}
	}
	ActiveCrossfadeMontage.Reset();

	// Freeze movement IN PLACE — no teleport, boss falls into stun pose right where he is
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->StopMovementImmediately();
		MovementComp->Velocity = FVector::ZeroVector;
		MovementComp->DisableMovement();
	}

	// Transition to finisher phase (notifies StateTree, broadcasts OnPhaseChanged)
	ExecutePhaseTransition(EBossPhase::Finisher);

	// Face the player so the finisher window looks natural
	if (CurrentTarget.IsValid())
	{
		FVector DirectionToPlayer = (CurrentTarget->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		if (!DirectionToPlayer.IsNearlyZero())
		{
			FRotator NewRotation = DirectionToPlayer.Rotation();
			NewRotation.Pitch = 0.0f;
			NewRotation.Roll = 0.0f;
			SetActorRotation(NewRotation);
		}
	}

	// Play the in-place stun montage
	if (FinisherEnterStunMontage)
	{
		if (USkeletalMeshComponent* MeshComp = GetMesh())
		{
			if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
			{
				AnimInstance->Montage_Play(FinisherEnterStunMontage);
			}
		}
	}

	// Spawn vulnerability VFX attached to boss
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

	OnFinisherReady.Broadcast();
}

void ABossCharacter::ApplyKnockback(const FVector& InKnockbackDirection, float Distance, float Duration, const FVector& AttackerLocation, bool bKeepEMFEnabled, EKnockbackStyle Style)
{
	// Ignore knockback in finisher phase - boss must stay at teleport position
	if (bIsInFinisherPhase)
	{
		return;
	}

	// Call parent implementation
	Super::ApplyKnockback(InKnockbackDirection, Distance, Duration, AttackerLocation, bKeepEMFEnabled, Style);
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
	AActor* OldTarget = CurrentTarget.Get();
	if (OldTarget != NewTarget)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BOSS_DEBUG] SetTarget: %s -> %s"),
			*GetNameSafe(OldTarget), *GetNameSafe(NewTarget));
	}
	CurrentTarget = NewTarget;
}

AArenaManager* ABossCharacter::GetLinkedArena() const
{
	return LinkedArena.Get();
}

// ==================== Animation Blending ====================

void ABossCharacter::CrossfadeToMontage(UAnimMontage* NewMontage, float CrossfadeTime, float PlayRate)
{
	if (!NewMontage)
	{
		return;
	}

	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp)
	{
		return;
	}

	UAnimInstance* AnimInst = MeshComp->GetAnimInstance();
	if (!AnimInst)
	{
		return;
	}

	// Blend out the previously-tracked montage so it fades while the new one ramps up.
	UAnimMontage* OldMontage = ActiveCrossfadeMontage.Get();
	if (OldMontage && OldMontage != NewMontage && AnimInst->Montage_IsPlaying(OldMontage))
	{
		AnimInst->Montage_Stop(CrossfadeTime, OldMontage);
	}

	// Play new montage with an explicit blend-in (UE 5.5+ API).
	// bStopAllMontages=false because we already blended out the previous tracked montage above;
	// blending in here is what produces the actual crossfade overlap.
	FAlphaBlendArgs BlendInArgs;
	BlendInArgs.BlendTime = CrossfadeTime;
	AnimInst->Montage_PlayWithBlendIn(NewMontage, BlendInArgs, PlayRate,
		EMontagePlayReturnType::MontageLength, 0.0f, false);

	ActiveCrossfadeMontage = NewMontage;
}

// ==================== Stun / Slowdown ====================

void ABossCharacter::ApplyExplosionStun(float Duration, UAnimMontage* StunMontage)
{
	// While Posture is alive, prop impacts only SLOW the boss; they don't stun.
	// Actual stun (full freeze + finisher window) only comes from Posture break = HP→1.
	if (bIsInFinisherPhase || bIsDead || CurrentHP <= 1.0f)
	{
		return;
	}

	UCharacterMovementComponent* MovementComp = GetCharacterMovement();
	if (!MovementComp)
	{
		return;
	}

	// (Re)apply slowdown. Starting a fresh slowdown resets the timer to the new duration.
	bIsSlowed = true;
	MovementComp->MaxWalkSpeed = DefaultMaxWalkSpeed * SlowdownStrength;

	const float SlowdownDuration = Duration * StunToSlowdownTimeScale;
	if (SlowdownDuration > 0.0f)
	{
		GetWorld()->GetTimerManager().SetTimer(SlowdownRecoveryTimer, this,
			&ABossCharacter::EndSlowdown, SlowdownDuration, false);
	}
	else
	{
		EndSlowdown();
	}

	UE_LOG(LogTemp, Warning, TEXT("[BOSS] Prop-impact slowdown: MaxWalkSpeed=%.0f for %.2fs"),
		MovementComp->MaxWalkSpeed, SlowdownDuration);
}

void ABossCharacter::EndSlowdown()
{
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->MaxWalkSpeed = DefaultMaxWalkSpeed;
	}
	bIsSlowed = false;
}

// ==================== Counter ====================

bool ABossCharacter::IsBeingCountered(AActor* /*Attacker*/) const
{
	// Counter window: any hit landing while the boss is mid-dash or in melee windup
	// (i.e. before the swing has become live and dealt damage to the player).
	// CounterDistance / CounterDotThreshold are intentionally unused now — kept on the
	// header for future tuning if we want to gate the counter geometrically again.
	return bIsInMeleeWindup || bIsDashing;
}

// ==================== Posture Regen ====================

void ABossCharacter::UpdatePostureRegen(float DeltaTime)
{
	if (bIsInFinisherPhase || bIsDead || CurrentHP >= MaxHP)
	{
		return;
	}

	// Sample regen-per-second from the curve; fall back to base × percent² if no curve.
	float RegenPerSec = 0.0f;
	if (PostureRegenByArenaPropCurve)
	{
		RegenPerSec = PostureRegenByArenaPropCurve->GetFloatValue(CachedArenaPropPercent);
	}
	else
	{
		RegenPerSec = FallbackPostureRegenBase * CachedArenaPropPercent * CachedArenaPropPercent;
	}

	if (RegenPerSec <= 0.0f)
	{
		return;
	}

	CurrentHP = FMath::Min(MaxHP, CurrentHP + RegenPerSec * DeltaTime);
}

void ABossCharacter::OnArenaPropPercentChanged(float RemainingPercent, int32 AliveCount)
{
	CachedArenaPropPercent = FMath::Clamp(RemainingPercent, 0.0f, 1.0f);
}

