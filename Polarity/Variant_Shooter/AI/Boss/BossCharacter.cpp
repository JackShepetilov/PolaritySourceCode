// BossCharacter.cpp
// Ground-phase boss built on AHumanoidNPC. See BossCharacter.h for the design overview.

#include "BossCharacter.h"
#include "BossAIController.h"
#include "ShooterWeapon.h"
#include "Polarity/Arena/ArenaManager.h"
#include "EMFVelocityModifier.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"

namespace
{
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
	AIControllerClass = ABossAIController::StaticClass();

	// Posture pool (CurrentHP is Posture).
	CurrentHP = 1000.0f;
	MaxHP = 1000.0f;

	// Melee tuning (inherited AMeleeNPC properties). The damage window is AnimNotify-driven:
	// place the "Melee: Damage Window State" notify in the boss attack montages.
	AttackDamage = 50.0f;
	bUseTimerDamageWindow = false;
	bEnableAttackMagnetism = true;
	MagnetismSpeed = 900.0f;         // lunge pull speed (cm/s)
	MagnetismStopDistance = 120.0f;  // stand-off where the lunge stops in front of the player
	AttackRange = 400.0f;            // approach stops here = lunge launch distance (keep < StrafeRadius)
	AttackCooldown = 0.6f;

	// Solo boss — don't gate fire on the squad combat coordinator.
	bUseCoordinator = false;
}

void ABossCharacter::BeginPlay()
{
	// AHumanoidNPC::BeginPlay spawns WeaponInventory[0], wires burst counting, disables body capture.
	Super::BeginPlay();

	MaxHP = CurrentHP;
	CurrentPhase = EBossPhase::Ground;

	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		DefaultMaxWalkSpeed = MovementComp->MaxWalkSpeed;

		// Face the target via controller desired rotation (AIController focus) instead of orienting
		// to movement — this is what lets the boss strafe sideways while facing the player and feed
		// a strafe blendspace (Sekiro/DS feel).
		MovementComp->bOrientRotationToMovement = false;
		MovementComp->bUseControllerDesiredRotation = true;
	}

	// Subscribe to the arena's prop-percent broadcast so posture regen can scale with it.
	if (AArenaManager* Arena = LinkedArena.LoadSynchronous())
	{
		Arena->OnPropPercentChanged.AddDynamic(this, &ABossCharacter::OnArenaPropPercentChanged);
		UE_LOG(LogTemp, Log, TEXT("[BOSS] Subscribed to LinkedArena (%s) prop-percent broadcast"), *Arena->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BOSS] LinkedArena not set; posture regen uses fallback formula at full prop percent."));
	}

	UE_LOG(LogTemp, Log, TEXT("[BOSS] BeginPlay: Phase=%s, Posture=%.0f/%.0f, Weapon=%s"),
		*BossPhaseName(CurrentPhase), CurrentHP, MaxHP, *GetNameSafe(Weapon));
}

void ABossCharacter::Tick(float DeltaTime)
{
	// Finisher knockback is fully scripted (SetActorLocation) — skip the normal NPC/melee tick.
	if (bIsFinisherKnockback)
	{
		UpdateFinisherKnockback(DeltaTime);
		return;
	}

	// AMeleeNPC::Tick = attack magnetism (lunge pull) + melee trace; AShooterNPC::Tick = charge etc.
	Super::Tick(DeltaTime);

	if (bIsDead)
	{
		return;
	}

	// While slowed from a prop impact, cap horizontal velocity to the slowed walk speed. Prop
	// explosions add a radial impulse straight to the capsule (bypassing MaxWalkSpeed); capping
	// each tick kills the launch but leaves normal slow walking untouched.
	if (bIsSlowed)
	{
		if (UCharacterMovementComponent* MC = GetCharacterMovement())
		{
			const float MaxSlowSpeed = DefaultMaxWalkSpeed * FMath::Max(SlowdownStrength, 0.0f);
			const float MaxSlowSqr = FMath::Square(MaxSlowSpeed);
			const FVector V = MC->Velocity;
			const float Speed2DSqr = V.X * V.X + V.Y * V.Y;
			if (Speed2DSqr > MaxSlowSqr)
			{
				const FVector V2D(V.X, V.Y, 0.0f);
				const FVector Capped = V2D.GetSafeNormal() * MaxSlowSpeed;
				MC->Velocity = FVector(Capped.X, Capped.Y, V.Z);
			}
		}
	}

	// Posture regen scales with datacenter prop percent.
	UpdatePostureRegen(DeltaTime);
}

void ABossCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PhaseTransitionTimer);
		World->GetTimerManager().ClearTimer(SlowdownRecoveryTimer);
	}

	Super::EndPlay(EndPlayReason);
}

// ==================== Damage Handling ====================

float ABossCharacter::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// In finisher phase the boss is invulnerable; the finisher is triggered via ExecuteFinisher()
	// (player melee), not through damage.
	if (bIsInFinisherPhase)
	{
		return 0.0f;
	}

	// Counter: a hit landing during the boss's melee windup (before the damage notify opens)
	// interrupts the swing and slows the boss. Damage still applies (counter is interrupt+slow,
	// not a damage shield). The knockback that pushes the boss back is allowed by ApplyKnockback
	// while LastCounterRegisteredTime is fresh (see below).
	if (IsBeingCountered(DamageCauser))
	{
		LastCounterRegisteredTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

		UE_LOG(LogTemp, Warning, TEXT("[BOSS] Countered by %s during windup — interrupt + slowdown"),
			DamageCauser ? *DamageCauser->GetName() : TEXT("NULL"));

		// Interrupt the swing so the AnimNotify damage window never opens.
		if (USkeletalMeshComponent* MeshComp = GetMesh())
		{
			if (UAnimInstance* AnimInst = MeshComp->GetAnimInstance())
			{
				AnimInst->StopAllMontages(AttackEndBlendTime);
			}
		}
		ActiveCrossfadeMontage.Reset();
		bIsAttacking = false;
		bDamageWindowActive = false;
		if (UWorld* W = GetWorld())
		{
			W->GetTimerManager().ClearTimer(DamageWindowStartTimer);
			W->GetTimerManager().ClearTimer(DamageWindowEndTimer);
		}

		ApplyExplosionStun(CounterSlowdownDuration, nullptr);
		// fall through — Super::TakeDamage still applies the player's damage below
	}

	// Posture break: if this hit would drop Posture to 1 or below, clamp to 1 and enter Finisher.
	if (CurrentHP - Damage <= 1.0f)
	{
		const float DamageToApply = CurrentHP - 1.0f;
		CurrentHP = 1.0f;

		OnDamageTaken.Broadcast(this, DamageToApply, TSubclassOf<UDamageType>(), GetActorLocation(), DamageCauser);

		EnterFinisherPhase();
		return DamageToApply;
	}

	// Normal damage. Prevent auto-retaliation fire in ground phase — boss fire is StateTree-driven.
	const bool bWasShootingBefore = bIsShooting;
	const float Result = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

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
		static double LastInvalidPhaseWarnTime = -10.0;
		const double NowSec = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (NowSec - LastInvalidPhaseWarnTime >= 5.0)
		{
			LastInvalidPhaseWarnTime = NowSec;
			UE_LOG(LogTemp, Error, TEXT("[BOSS] SetPhase called with invalid value %d (stale StateTree enum?). Suppressing for 5s."), (int)NewPhase);
		}
		return;
	}

	if (CurrentPhase != NewPhase)
	{
		ExecutePhaseTransition(NewPhase);
	}
}

void ABossCharacter::ExecutePhaseTransition(EBossPhase NewPhase)
{
	if (!IsValidBossPhase(NewPhase))
	{
		return;
	}

	const EBossPhase OldPhase = CurrentPhase;

	UE_LOG(LogTemp, Warning, TEXT("[BOSS PHASE] %s -> %s (Posture=%.0f/%.0f)"),
		*BossPhaseName(OldPhase), *BossPhaseName(NewPhase), CurrentHP, MaxHP);

	CurrentPhase = NewPhase;
	bIsTransitioning = true;

	switch (NewPhase)
	{
	case EBossPhase::Ground:
		if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
		{
			MovementComp->SetMovementMode(MOVE_Walking);
		}
		bIsTransitioning = false;
		break;

	case EBossPhase::Finisher:
		// Finisher state set up by EnterFinisherPhase().
		bIsTransitioning = false;
		break;

	default:
		break;
	}

	OnPhaseChanged.Broadcast(OldPhase, NewPhase);
}

void ABossCharacter::OnPhaseTransitionComplete()
{
	bIsTransitioning = false;
}

// ==================== Melee ====================

void ABossCharacter::StartBossMeleeAttack(AActor* Target)
{
	if (!Target || bIsDead || bIsInFinisherPhase || !CanAttack())
	{
		return;
	}

	NotifyTargetAcquired(Target);

	// Reuse the inherited AMeleeNPC attack state so its Tick magnetism / trace / damage-window
	// machinery drives this attack.
	bIsAttacking = true;
	CurrentMeleeTarget = Target;
	SetTarget(Target);
	HitActorsThisAttack.Empty();
	bHasDealtDamage = false;
	LastAttackTime = GetWorld()->GetTimeSeconds();

	// Face target (yaw only).
	FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
	ToTarget.Z = 0.0f;
	if (!ToTarget.IsNearlyZero())
	{
		FRotator NewRotation = ToTarget.Rotation();
		NewRotation.Pitch = 0.0f;
		NewRotation.Roll = 0.0f;
		SetActorRotation(NewRotation);
	}

	// Lunge (far) vs in-place (close). Magnet pulls only on a lunge.
	const float Dist = FVector::Dist2D(GetActorLocation(), Target->GetActorLocation());
	const bool bLunge = Dist > InPlaceMeleeRange;
	bEnableAttackMagnetism = bLunge;

	const TArray<TObjectPtr<UAnimMontage>>& Pool = bLunge ? LungeMeleeMontages : InPlaceMeleeMontages;
	UAnimMontage* MontageToPlay = (Pool.Num() > 0) ? Pool[FMath::RandRange(0, Pool.Num() - 1)].Get() : nullptr;

	if (MontageToPlay)
	{
		CrossfadeToMontage(MontageToPlay, MeleeStartBlendTime);

		if (USkeletalMeshComponent* MeshComp = GetMesh())
		{
			if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
			{
				FOnMontageEnded EndDelegate;
				EndDelegate.BindUObject(this, &ABossCharacter::OnBossAttackMontageEnded);
				AnimInstance->Montage_SetEndDelegate(EndDelegate, MontageToPlay);
			}
		}
		// The damage window is opened/closed by the "Melee: Damage Window State" AnimNotify in
		// the montage, which calls the inherited NotifyDamageWindowStart/End on this boss.
	}
	else
	{
		// No montage configured — fail safe so the attack still deals damage and ends.
		UE_LOG(LogTemp, Warning, TEXT("[BOSS] StartBossMeleeAttack: no %s montage set — using instant window."),
			bLunge ? TEXT("Lunge") : TEXT("InPlace"));
		bDamageWindowActive = true;
		FTimerHandle TmpEnd;
		GetWorld()->GetTimerManager().SetTimer(TmpEnd, [this]()
		{
			bDamageWindowActive = false;
			bHasDealtDamage = true;
			bIsAttacking = false;
		}, 0.3f, false);
	}
}

bool ABossCharacter::IsInMeleeWindup() const
{
	// Own attack started, but the damage window has not opened and no damage has been dealt yet.
	return bIsAttacking && !bDamageWindowActive && !bHasDealtDamage;
}

void ABossCharacter::OnBossAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (bDamageWindowActive)
	{
		// Mirror AMeleeNPC::OnDamageWindowEnd side effects (stops magnetism).
		bDamageWindowActive = false;
		bHasDealtDamage = true;
	}

	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(DamageWindowStartTimer);
		W->GetTimerManager().ClearTimer(DamageWindowEndTimer);
	}

	bIsAttacking = false;

	if (ActiveCrossfadeMontage.Get() == Montage)
	{
		ActiveCrossfadeMontage.Reset();
	}
}

// ==================== Counter ====================

bool ABossCharacter::IsBeingCountered(AActor* /*Attacker*/) const
{
	return IsInMeleeWindup();
}

// ==================== Ranged / Disarm ====================

bool ABossCharacter::IsDisarmed() const
{
	return !bIsDead && !bIsInFinisherPhase && (Weapon == nullptr);
}

bool ABossCharacter::CanBeYanked() const
{
	if (!Super::CanBeYanked())
	{
		return false;
	}
	const float ChargeAbs = EMFVelocityModifier ? FMath::Abs(EMFVelocityModifier->GetCharge()) : 0.0f;
	return ChargeAbs >= YankChargeThreshold;
}

void ABossCharacter::SpawnNextWeapon()
{
	if (bIsDead || bIsInFinisherPhase)
	{
		return;
	}

	// Cyclic re-arm: always respawn the single boss weapon (CurrentWeaponIndex stays 0), instead of
	// AHumanoidNPC's exhaust-into-permanent-melee behavior. The disarmed window is the
	// WeaponSwitchDelay between the yank and this call.
	if (!WeaponInventory.IsValidIndex(0) || !WeaponInventory[0])
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	Weapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponInventory[0], GetActorTransform(), SpawnParams);
	// Notify-driven firing: no OnShotFired binding needed (the fire montage's notifies fire shots).

	UE_LOG(LogTemp, Warning, TEXT("[BOSS] Re-armed: %s"), *GetNameSafe(Weapon));
}

void ABossCharacter::StartShootBurst(AActor* Target)
{
	if (!Target || bIsDead || bIsInFinisherPhase || IsDisarmed() || FireShotMontages.Num() == 0)
	{
		return;
	}

	SetTarget(Target);          // focus → faces the player while firing
	CurrentShotIndex = 0;
	bShootMontageActive = true;

	// Play the first shot montage; its "Boss — Fire One Shot" notify fires the round and advances.
	PlayShootMontage(0);
}

void ABossCharacter::PlayShootMontage(int32 Index)
{
	// This runs deferred (next tick) — bail if the burst was stopped in the meantime.
	if (!bShootMontageActive || bIsDead || bIsInFinisherPhase)
	{
		return;
	}
	if (!FireShotMontages.IsValidIndex(Index))
	{
		return;
	}

	UAnimMontage* Montage = FireShotMontages[Index].Get();
	if (!Montage)
	{
		return;
	}

	CurrentShotIndex = Index;
	CrossfadeToMontage(Montage, FireMontageBlendTime);
	// End delegate is bound only on the LAST montage (in FireOneShotFromNotify) to end the burst.
}

void ABossCharacter::FireOneShotFromNotify()
{
	if (bIsDead || bIsInFinisherPhase || !bShootMontageActive)
	{
		return;
	}

	// Fire this montage's shot.
	if (Weapon)
	{
		CurrentAimTarget = CurrentTarget.Get();   // aim at the player; Fire() reads CurrentAimTarget
		Weapon->FireOnce();
	}

	const int32 NextIndex = CurrentShotIndex + 1;
	if (FireShotMontages.IsValidIndex(NextIndex))
	{
		// Crossfade to the next shot's montage on the NEXT TICK. Playing/restarting a montage from
		// inside the current montage's own notify is unreliable in UE (it dropped shots when the same
		// animation was reused). Deferring one tick makes the crossfade — and the same-asset restart —
		// reliable, and the next montage blends in right after the shot (no return-to-idle stutter).
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateUObject(this, &ABossCharacter::PlayShootMontage, NextIndex));
		}
	}
	else if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		// Last shot: let this montage play out, then end the burst (back to locomotion).
		if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
		{
			if (UAnimMontage* LastMontage = FireShotMontages[CurrentShotIndex].Get())
			{
				FOnMontageEnded EndDelegate;
				EndDelegate.BindUObject(this, &ABossCharacter::OnShootMontageEnded);
				AnimInstance->Montage_SetEndDelegate(EndDelegate, LastMontage);
			}
		}
	}
}

void ABossCharacter::OnShootMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// Bound only on the final shot montage — when it finishes (or is cut), the burst is over.
	if (bShootMontageActive)
	{
		StopShootBurst();
	}
}

void ABossCharacter::StopShootBurst()
{
	bShootMontageActive = false;
	CrossfadeToMontage(nullptr, FireMontageBlendTime);   // blend back to locomotion
}

// ==================== Behavior (StateTree-driven) ====================

void ABossCharacter::ChooseNextAction()
{
	// No weapon → can only melee.
	if (IsDisarmed())
	{
		PendingAction = EBossAction::Melee;
		return;
	}

	const float Total = FMath::Max(ShootActionWeight + MeleeActionWeight, KINDA_SMALL_NUMBER);
	const float Roll = FMath::FRand() * Total;
	PendingAction = (Roll < ShootActionWeight) ? EBossAction::Shoot : EBossAction::Melee;
}

float ABossCharacter::GetStrafeDurationForState() const
{
	return IsDisarmed() ? StrafeDurationDisarmed : StrafeDuration;
}

void ABossCharacter::BeginStrafe(AActor* Target)
{
	// Face the player while orbiting (drives the strafe blendspace via controller-desired rotation).
	SetTarget(Target);
}

void ABossCharacter::StrafeStep(AActor* Target, float Direction)
{
	if (!Target)
	{
		return;
	}

	AAIController* AI = Cast<AAIController>(GetController());
	if (!AI)
	{
		return;
	}

	const FVector TargetLoc = Target->GetActorLocation();
	FVector ToBoss = GetActorLocation() - TargetLoc;
	ToBoss.Z = 0.0f;
	if (ToBoss.IsNearlyZero())
	{
		ToBoss = -GetActorForwardVector();
	}
	ToBoss = ToBoss.GetSafeNormal2D();

	// Step around the player's circle in the chosen direction, keeping StrafeRadius distance.
	const float SignedStep = (Direction >= 0.0f ? 1.0f : -1.0f) * StrafeStepAngleDeg;
	const FVector StepDir = ToBoss.RotateAngleAxis(SignedStep, FVector::UpVector);
	const FVector RingPoint = TargetLoc + StepDir * StrafeRadius;

	AI->MoveToLocation(RingPoint, StrafeAcceptanceRadius,
		/*bStopOnOverlap*/ false, /*bUsePathfinding*/ true,
		/*bProjectDestinationToNavigation*/ true, /*bCanStrafe*/ true);
}

void ABossCharacter::StopStrafe()
{
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->StopMovement();
	}
}

// ==================== Finisher Phase ====================

void ABossCharacter::EnterFinisherPhase()
{
	if (bIsInFinisherPhase)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[BOSS] EnterFinisherPhase: Posture broken, freezing on the spot."));

	bIsInFinisherPhase = true;

	// Cancel any ongoing transition.
	bIsTransitioning = false;
	GetWorld()->GetTimerManager().ClearTimer(PhaseTransitionTimer);

	// Stop ranged fire and all combat actions.
	StopShooting();
	bIsAttacking = false;
	bDamageWindowActive = false;
	bIsDashing = false;

	// Cancel knockback completely.
	bIsInKnockback = false;
	bIsKnockbackInterpolating = false;
	KnockbackElapsedTime = 0.0f;

	// Cancel slowdown — finisher freeze overrides it.
	if (bIsSlowed)
	{
		EndSlowdown();
	}

	// Stop all combat timers (inherited + boss).
	if (UWorld* W = GetWorld())
	{
		FTimerManager& TM = W->GetTimerManager();
		TM.ClearTimer(DamageWindowStartTimer);
		TM.ClearTimer(DamageWindowEndTimer);
		TM.ClearTimer(AttackCooldownTimer);
		TM.ClearTimer(SlowdownRecoveryTimer);
	}

	// Stop montages and clear crossfade tracking.
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
		{
			AnimInstance->StopAllMontages(0.1f);
		}
	}
	ActiveCrossfadeMontage.Reset();

	// Freeze movement IN PLACE — boss falls into the stun pose where it stands.
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->StopMovementImmediately();
		MovementComp->Velocity = FVector::ZeroVector;
		MovementComp->DisableMovement();
	}

	// Notify StateTree / widgets.
	ExecutePhaseTransition(EBossPhase::Finisher);

	// Face the player so the finisher window reads naturally.
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

	// Play the in-place stun montage.
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

	// Spawn vulnerability VFX attached to boss.
	if (FinisherVulnerabilityVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAttached(
			FinisherVulnerabilityVFX, GetRootComponent(), NAME_None,
			FVector::ZeroVector, FRotator::ZeroRotator,
			EAttachLocation::SnapToTarget, true, true);
	}

	OnFinisherReady.Broadcast();
}

void ABossCharacter::ExecuteFinisher(AActor* Attacker)
{
	if (!bIsInFinisherPhase || bIsFinisherKnockback)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[BOSS] ExecuteFinisher — starting knockback"));
	StartFinisherKnockback();
}

void ABossCharacter::StartFinisherKnockback()
{
	bIsFinisherKnockback = true;
	bIsInFinisherPhase = false;

	FinisherKnockbackStartPos = GetActorLocation();
	const FVector NormalizedDirection = FinisherKnockbackDirection.GetSafeNormal();
	FinisherKnockbackEndPos = FinisherKnockbackStartPos + NormalizedDirection * FinisherKnockbackDistance;
	FinisherKnockbackElapsed = 0.0f;

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

	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->DisableMovement();
	}
}

void ABossCharacter::UpdateFinisherKnockback(float DeltaTime)
{
	FinisherKnockbackElapsed += DeltaTime;

	const float Alpha = FMath::Clamp(FinisherKnockbackElapsed / FinisherKnockbackDuration, 0.0f, 1.0f);
	const float EasedAlpha = 1.0f - FMath::Pow(1.0f - Alpha, 2.0f);

	const FVector NewPosition = FMath::Lerp(FinisherKnockbackStartPos, FinisherKnockbackEndPos, EasedAlpha);
	SetActorLocation(NewPosition);

	if (Alpha >= 1.0f)
	{
		OnFinisherKnockbackComplete();
	}
}

void ABossCharacter::OnFinisherKnockbackComplete()
{
	UE_LOG(LogTemp, Warning, TEXT("[BOSS] Knockback complete — death"));

	bIsFinisherKnockback = false;

	if (FinisherDeathVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), FinisherDeathVFX, GetActorLocation(), GetActorRotation(),
			FVector(1.0f), true, true);
	}

	// Ragdoll the mesh.
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetSimulatePhysics(true);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);

		const FVector Impulse = FinisherKnockbackDirection.GetSafeNormal() * 500.0f;
		MeshComp->AddImpulse(Impulse, NAME_None, true);
	}

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	OnBossDefeated.Broadcast();

	CurrentHP = 0.0f;
	bIsDead = true;
}

// ==================== Knockback Override ====================

void ABossCharacter::ApplyKnockback(const FVector& InKnockbackDirection, float Distance, float Duration, const FVector& AttackerLocation, bool bKeepEMFEnabled, EKnockbackStyle Style)
{
	// Frozen during finisher / scripted finisher knockback.
	if (bIsInFinisherPhase || bIsFinisherKnockback)
	{
		return;
	}

	// Boss is generally immune to being shoved around (inherited HumanoidNPC body immunity).
	// The ONLY exception: a player hit during the boss's own melee windup (the counter window).
	const bool bCounterWindow = IsInMeleeWindup()
		|| (GetWorld() && (GetWorld()->GetTimeSeconds() - LastCounterRegisteredTime) < 0.15f);

	if (!bCounterWindow)
	{
		return;
	}

	// Counter knockback: interrupt the swing, then apply a REAL knockback. Call AShooterNPC's
	// implementation directly to bypass HumanoidNPC's blanket immunity and MeleeNPC's parry path.
	bIsAttacking = false;
	bDamageWindowActive = false;
	if (UWorld* W = GetWorld())
	{
		W->GetTimerManager().ClearTimer(DamageWindowStartTimer);
		W->GetTimerManager().ClearTimer(DamageWindowEndTimer);
	}

	AShooterNPC::ApplyKnockback(InKnockbackDirection, Distance, Duration, AttackerLocation, bKeepEMFEnabled, Style);
}

// ==================== Stun / Slowdown ====================

void ABossCharacter::ApplyExplosionStun(float Duration, UAnimMontage* StunMontage)
{
	// While Posture is alive, prop impacts only SLOW the boss; they don't stun. A real stun (full
	// freeze + finisher window) only comes from Posture break (HP -> 1).
	if (bIsInFinisherPhase || bIsDead || CurrentHP <= 1.0f)
	{
		return;
	}

	UCharacterMovementComponent* MovementComp = GetCharacterMovement();
	if (!MovementComp)
	{
		return;
	}

	// Prop impact: interrupt whatever the boss is doing so it drops back to strafe (ranged burst or
	// melee swing), all at once with the slowdown. No knockback impulse — that's blocked at the source.
	bShootMontageActive = false;   // ends a ranged burst; the pending next-tick shot montage no-ops
	bIsAttacking = false;          // ends a melee attack → the StateTree melee task returns to strafe
	bDamageWindowActive = false;
	bHasDealtDamage = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DamageWindowStartTimer);
		World->GetTimerManager().ClearTimer(DamageWindowEndTimer);
	}
	CrossfadeToMontage(nullptr, AttackEndBlendTime);   // blend the current action montage out

	bIsSlowed = true;
	MovementComp->MaxWalkSpeed = DefaultMaxWalkSpeed * SlowdownStrength;

	const float SlowdownDuration = Duration * StunToSlowdownTimeScale;
	if (SlowdownDuration > 0.0f)
	{
		GetWorld()->GetTimerManager().SetTimer(SlowdownRecoveryTimer, this, &ABossCharacter::EndSlowdown, SlowdownDuration, false);
	}
	else
	{
		EndSlowdown();
	}
}

void ABossCharacter::EndSlowdown()
{
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->MaxWalkSpeed = DefaultMaxWalkSpeed;
	}
	bIsSlowed = false;
}

// ==================== Posture Regen ====================

void ABossCharacter::UpdatePostureRegen(float DeltaTime)
{
	if (bIsInFinisherPhase || bIsDead || CurrentHP >= MaxHP)
	{
		return;
	}

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

	const float OldHP = CurrentHP;
	CurrentHP = FMath::Min(MaxHP, CurrentHP + RegenPerSec * DeltaTime);

	// Repaint the posture bar on regen, throttled to once per integer-HP crossing.
	const int32 OldHPInt = FMath::FloorToInt(OldHP);
	const int32 NewHPInt = FMath::FloorToInt(CurrentHP);
	if (NewHPInt != OldHPInt)
	{
		OnDamageTaken.Broadcast(this, 0.0f, TSubclassOf<UDamageType>(), GetActorLocation(), nullptr);
	}
}

void ABossCharacter::OnArenaPropPercentChanged(float RemainingPercent, int32 AliveCount)
{
	// Remap raw RemainingPercent (1 = nothing destroyed) into "datacenter HP" (1 = full,
	// 0 = destroyed-at-threshold) so the posture-regen curve's X axis matches the player-facing bar.
	float EffectivePercent = FMath::Clamp(RemainingPercent, 0.0f, 1.0f);
	if (AArenaManager* Arena = LinkedArena.Get())
	{
		const float Threshold = FMath::Clamp(Arena->DatacenterVictoryDestroyedPercent, KINDA_SMALL_NUMBER, 1.0f);
		const float Floor = 1.0f - Threshold;
		EffectivePercent = FMath::Clamp((EffectivePercent - Floor) / Threshold, 0.0f, 1.0f);
	}
	CachedArenaPropPercent = EffectivePercent;
}

// ==================== Animation Blending ====================

void ABossCharacter::CrossfadeToMontage(UAnimMontage* NewMontage, float CrossfadeTime, float PlayRate)
{
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

	UAnimMontage* OldMontage = ActiveCrossfadeMontage.Get();

	// nullptr → blend the tracked montage out to locomotion.
	if (!NewMontage)
	{
		if (OldMontage && AnimInst->Montage_IsPlaying(OldMontage))
		{
			AnimInst->Montage_Stop(CrossfadeTime, OldMontage);
		}
		ActiveCrossfadeMontage.Reset();
		return;
	}

	// Blend out a DIFFERENT tracked montage so it fades while the new one ramps up. If the next
	// montage reuses the SAME asset (e.g. the same shoot montage for several shots), we fall through
	// to Montage_PlayWithBlendIn below, which restarts it from the top so its per-shot notify fires
	// again — that's why there is no "already playing → skip" guard here.
	if (OldMontage && OldMontage != NewMontage && AnimInst->Montage_IsPlaying(OldMontage))
	{
		AnimInst->Montage_Stop(CrossfadeTime, OldMontage);
	}

	FAlphaBlendArgs BlendInArgs;
	BlendInArgs.BlendTime = CrossfadeTime;
	AnimInst->Montage_PlayWithBlendIn(NewMontage, BlendInArgs, PlayRate, EMontagePlayReturnType::MontageLength, 0.0f, false);

	ActiveCrossfadeMontage = NewMontage;
}

// ==================== Target Management ====================

void ABossCharacter::SetTarget(AActor* NewTarget)
{
	CurrentTarget = NewTarget;

	// Keep the controller focused on the target so bUseControllerDesiredRotation faces the player
	// in every state (strafe sideways, lunge, in-place, shoot).
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		if (NewTarget)
		{
			AI->SetFocus(NewTarget);
		}
		else
		{
			AI->ClearFocus(EAIFocusPriority::Gameplay);
		}
	}
}

AArenaManager* ABossCharacter::GetLinkedArena() const
{
	return LinkedArena.Get();
}
