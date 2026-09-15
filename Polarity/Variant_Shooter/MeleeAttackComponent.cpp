// MeleeAttackComponent.cpp
// Quick melee attack system implementation

#include "MeleeAttackComponent.h"
#include "ChargeAnimationComponent.h"
#include "ShooterDummyInterface.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "DrawDebugHelpers.h"
#include "Engine/DamageEvents.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/AI/Boss/BossCharacter.h"
#include "ApexMovementComponent.h"
#include "MovementSettings.h"
#include "PolarityCharacter.h"
#include "ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "EMFPhysicsProp.h"
#include "Upgrades/UpgradeManagerComponent.h"
#include "Upgrades/Upgrades/Upgrade_AirKick.h"
#include "Foliage/FoliageConversionLibrary.h"
#include "Variant_Shooter/DamageTypes/DamageType_MomentumBonus.h"
#include "Variant_Shooter/DamageTypes/DamageType_Dropkick.h"
#include "Arena/SportsBall.h"

UMeleeAttackComponent::UMeleeAttackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UMeleeAttackComponent::BeginPlay()
{
	Super::BeginPlay();

	// Cache owner references
	OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (OwnerCharacter)
	{
		OwnerController = Cast<APlayerController>(OwnerCharacter->GetController());
	}

	// Hand the lunge tunables to the component that now does the flying. This runs on every machine
	// — server included, where this component is otherwise idle for a remote player — and reads the
	// same Blueprint defaults on each, so both ends fly at the same speed off the same settings
	// without any of it going on the wire.
	if (OwnerCharacter)
	{
		if (UApexMovementComponent* Apex = Cast<UApexMovementComponent>(OwnerCharacter->GetCharacterMovement()))
		{
			Apex->SetMeleeLungeTuning(Settings.LungeMaxSpeed, Settings.MomentumPreservationRatio,
				Settings.bDisableGravityDuringLunge, Settings.DropKickDiveSpeed);
		}
	}

	// Auto-detect mesh references
	AutoDetectMeshReferences();

	// Initially hide MeleeMesh
	if (MeleeMesh)
	{
		MeleeMesh->SetVisibility(false);
	}
}

void UMeleeAttackComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateState(DeltaTime);
	UpdateLunge(DeltaTime);
	UpdateMagnetism(DeltaTime);
	UpdateCoolKick(DeltaTime);
	UpdateMeleeMeshRotation();
	UpdateMontagePlayRate(DeltaTime);
	UpdateFocus(DeltaTime);
	UpdateCameraFocus(DeltaTime);

	// Update drop kick cooldown
	if (DropKickCooldownRemaining > 0.0f)
	{
		DropKickCooldownRemaining -= DeltaTime;
		if (DropKickCooldownRemaining <= 0.0f)
		{
			DropKickCooldownRemaining = 0.0f;
			OnDropKickCooldownEnded.Broadcast();
		}
	}
}

// Side-effect-free boss-finisher detection that reuses the lunge cone (range + half-angle from the
// camera forward). Returns a boss currently in its finisher phase inside the cone, or null.
static ABossCharacter* FindFinisherBossInCone(UWorld* World, AActor* Owner, const FVector& Start, const FVector& Forward, float Range, float ConeHalfAngleDeg)
{
	if (!World || !Owner)
	{
		return nullptr;
	}
	const float CosThreshold = FMath::Cos(FMath::DegreesToRadians(ConeHalfAngleDeg));

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(Owner);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByChannel(Overlaps, Start, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeSphere(Range), QueryParams);

	ABossCharacter* Best = nullptr;
	float BestDot = CosThreshold;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		ABossCharacter* Boss = Cast<ABossCharacter>(Overlap.GetActor());
		if (!Boss || !Boss->IsInFinisherPhase())
		{
			continue;
		}
		FVector ToTarget = Boss->GetActorLocation() - Start;
		const float Dist = ToTarget.Size();
		if (Dist <= KINDA_SMALL_NUMBER || Dist > Range)
		{
			continue;
		}
		ToTarget /= Dist;
		const float Dot = FVector::DotProduct(Forward, ToTarget);
		if (Dot >= BestDot)
		{
			BestDot = Dot;
			Best = Boss;
		}
	}
	return Best;
}

bool UMeleeAttackComponent::StartAttack()
{
	UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] === StartAttack ENTER === State=%d, bInputLocked=%d, bExternallyDisabled=%d, bIsDropKick=%d, bHasHitThisAttack=%d"),
		(int32)CurrentState, bInputLocked, bExternallyDisabled, bIsDropKick, bHasHitThisAttack);

	bool bCanAttackNow = CanAttack();
	bool bIsCurrentlyAttacking = IsAttacking();
	bool bShouldDropKick = ShouldPerformDropKick();
	bool bHasTarget = bShouldDropKick ? HasDropKickTarget() : false;

	UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] CanAttack=%d, IsAttacking=%d, ShouldDropKick=%d, HasTarget=%d"),
		bCanAttackNow, bIsCurrentlyAttacking, bShouldDropKick, bHasTarget);

	// Drop kick can interrupt an ongoing attack:
	// - Always interrupts a normal (non-dropkick) attack
	// - Only interrupts another drop kick after it has dealt damage
	// - Must have an actual dropkick target in the cone (prevents air melee spam)
	if (!bCanAttackNow && bIsCurrentlyAttacking && (!bIsDropKick || bHasHitThisAttack) && bShouldDropKick && bHasTarget)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] INTERRUPTING current attack for dropkick"));
		// Cleanup current attack
		StopAttackAnimation();
		StopSwingTrailFX();
		StopMagnetism();
		StopCameraFocus();
		SwitchToFirstPersonMesh();
		bInputLocked = false;
		SetState(EMeleeAttackState::Ready);
		// Fall through to start a new attack (which will become a drop kick via StartMagnetism)
	}
	else if (!bCanAttackNow)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] BLOCKED by CanAttack=false (no dropkick interrupt path)"));
		return false;
	}

	// A readiness notify may arrive during recovery or while the previous weapon draw is blending.
	// Cut that presentation cleanly and let this new swing own the mesh from its first frame.
	if (bReadyForNextAttackFromNotify && CurrentState != EMeleeAttackState::Ready)
	{
		StopAttackAnimation();
		StopSwingTrailFX();
		StopMagnetism();
		StopCameraFocus();
		SwitchToFirstPersonMesh();
		SetState(EMeleeAttackState::Ready);
	}
	bReadyForNextAttackFromNotify = false;

	// ==================== Boss Finisher Trigger (start of swing) ====================
	// If a finisher-phase boss is within the lunge cone/range as the swing starts, run the cinematic
	// finisher INSTEAD of a melee attack. Returning here fully cancels the swing — none of the attack
	// machinery below runs, so there is no animation, damage, hitmarker, lunge, or swing camera shake.
	if (OwnerCharacter)
	{
		if (ABossCharacter* FinisherBoss = FindFinisherBossInCone(GetWorld(), OwnerCharacter, GetTraceStart(), GetTraceDirection(), Settings.LungeRange, Settings.LungeConeHalfAngle))
		{
			FinisherBoss->ExecuteFinisher(OwnerCharacter);
			return true;
		}
	}

	// Lock input immediately to prevent spam
	bInputLocked = true;

	// Reset attack state
	bHasHitThisAttack = false;
	bHitEnemyThisAttack = false;
	HitActorsThisAttack.Empty();
	// The previous swing's miss verdict must not survive into this one, or a swing that connects
	// still hands back the momentum of the one before it.
	if (OwnerCharacter)
	{
		if (UApexMovementComponent* Apex = Cast<UApexMovementComponent>(OwnerCharacter->GetCharacterMovement()))
		{
			Apex->SetMeleeLungeRestoreOnEnd(false);
		}
	}
	MontageTimeElapsed = 0.0f;
	// Cleared so the Windup gate can reliably detect "no montage this swing" (timer fallback).
	CurrentMeleeMontage = nullptr;

	// Determine attack type based on movement state
	CurrentAttackType = DetermineAttackType();

	// Select random animation based on attack type and weights
	switch (CurrentAttackType)
	{
	case EMeleeAttackType::Airborne:
		SelectedAnimationData = SelectWeightedAnimation(AirborneAttacks);
		break;
	case EMeleeAttackType::Sliding:
		SelectedAnimationData = SelectWeightedAnimation(SlidingAttacks);
		break;
	case EMeleeAttackType::Ground:
	default:
		SelectedAnimationData = SelectWeightedAnimation(GroundAttacks);
		break;
	}

	// Cache owner velocity for momentum calculations
	if (OwnerCharacter)
	{
		if (UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement())
		{
			OwnerVelocityAtAttackStart = Movement->Velocity;
		}
	}

	// Store lunge direction based on current movement velocity
	LungeDirection = GetLungeDirection();
	LungeProgress = 0.0f;

	// Broadcast dropkick delegate immediately on input, before the swing starts
	if (ShouldPerformDropKick() && HasDropKickTarget())
	{
		OnDropKickStarted.Broadcast();
	}

	// The weapon leaves the hands on THIS frame, with no holster and no lowering: the swing itself is
	// the animation. It comes back through its own draw once the montage is over (@see EndSwing).
	// Already empty after the boss finisher's pre-lower, or after a swing cut short by a drop kick;
	// the stow is idempotent and simply finds them so.
	HideWeaponForSwing();
	SwitchToMeleeMesh();
	StartMagnetism();
	PlayAttackAnimation();
	PlaySwingCameraShake();
	PlaySound(SwingSound);
	OnMeleeAttackStarted.Broadcast();

	// Armed and waiting: the damage window (Active) is opened by the montage's notify, not by a timer.
	SetState(EMeleeAttackState::Windup);

	return true;
}

bool UMeleeAttackComponent::StartDelegatedDropKick()
{
	UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] === StartDelegatedDropKick ENTER ==="));

	if (!OwnerCharacter || !OwnerController)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] DelegatedDropKick: FALSE - no owner/controller"));
		return false;
	}

	// Must be airborne
	UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement();
	if (!Movement || !Movement->IsFalling())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] DelegatedDropKick: FALSE - not falling (Mode=%d)"),
			Movement ? (int32)Movement->MovementMode : -1);
		return false;
	}

	// Temporarily allow the component to operate (it's disabled when melee weapon is equipped)
	bool bWasDisabled = bExternallyDisabled;
	bExternallyDisabled = false;

	// Reset attack state
	bHasHitThisAttack = false;
	bHitEnemyThisAttack = false;
	HitActorsThisAttack.Empty();
	bIsDropKick = false;
	DropKickHeightDifference = 0.0f;
	MagnetismTarget.Reset();

	// Cache velocity for momentum calculations
	OwnerVelocityAtAttackStart = Movement->Velocity;

	UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] DelegatedDropKick: calling TryStartDropKick..."));
	// Try cone-based dropkick detection (finds target, sets LungeTargetPosition, etc.)
	bool bSuccess = TryStartDropKick();

	if (bSuccess)
	{
		bDelegatedDropKick = true;
		bInputLocked = true;

		// Jump directly to Active state — skip HidingWeapon, InputDelay, Windup
		float DistToTarget = FVector::Dist(OwnerCharacter->GetActorLocation(), LungeTargetPosition);
		float TravelTime = (Settings.DropKickDiveSpeed > 0.0f) ? (DistToTarget / Settings.DropKickDiveSpeed) : Settings.ActiveTime;
		StateTimeRemaining = FMath::Max(Settings.ActiveTime, TravelTime + 0.15f);
		CurrentState = EMeleeAttackState::Active;

		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] DelegatedDropKick: SUCCESS! DistToTarget=%.0f, TravelTime=%.2f, StateTime=%.2f"),
			DistToTarget, TravelTime, StateTimeRemaining);

		// Don't spawn trail FX or play animation — the weapon handles its own
		OnDropKickStarted.Broadcast();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] DelegatedDropKick: FAILED - TryStartDropKick returned false"));
		// Failed — restore disabled state
		bExternallyDisabled = bWasDisabled;
	}

	return bSuccess;
}

bool UMeleeAttackComponent::TryStartDelegatedLunge()
{
	if (!OwnerCharacter)
	{
		return false;
	}

	// Any previous flight ends first. A fast swinger can start the next swing while the last lunge is
	// still publishing intent, and two overlapping acquisitions would leave the older target flying.
	EndDelegatedLunge();

	// The speed gate in UpdateLunge reads this, so it has to be the speed at the START of the swing
	// and not whatever the velocity happens to be by the time the flight begins. On this component
	// the gate defaults to 0 -- works from a standstill -- which is the behaviour the weapon's own
	// copy did not have.
	if (const UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement())
	{
		OwnerVelocityAtAttackStart = Movement->Velocity;
	}

	// Not a dropkick: that is the weapon's other delegation and it has its own entry point. Cleared
	// here so a lunge started right after a dropkick cannot inherit its flags.
	bIsDropKick = false;
	DropKickHeightDifference = 0.0f;

	StartMagnetism();

	// StartMagnetism is allowed to find nothing, and a swing at empty air is not a failure -- it just
	// does not fly. Reporting that honestly lets the weapon skip its own bookkeeping.
	if (!MagnetismTarget.IsValid())
	{
		return false;
	}

	bDelegatedLunge = true;

	if (AShooterCharacter::IsLungeDebugEnabled())
	{
		UE_LOG(LogTemp, Warning, TEXT("[LUNGE_DEBUG] COMPONENT: delegated lunge started at %s (entry speed %.0f, gate %.0f)"),
			*GetNameSafe(MagnetismTarget.Get()), OwnerVelocityAtAttackStart.Size(), Settings.MinSpeedForLunge);
	}

	return true;
}

void UMeleeAttackComponent::EndDelegatedLunge()
{
	if (!bDelegatedLunge)
	{
		return;
	}

	bDelegatedLunge = false;

	// UpdateLunge publishes "not lunging" on its next tick, and that falling edge is what actually
	// ends the flight on both machines and gives gravity and move-collision back. StopMagnetism only
	// clears this component's own bookkeeping.
	StopMagnetism();

	if (AShooterCharacter::IsLungeDebugEnabled())
	{
		UE_LOG(LogTemp, Warning, TEXT("[LUNGE_DEBUG] COMPONENT: delegated lunge ended"));
	}
}

bool UMeleeAttackComponent::CancelAttack()
{
	// Can only cancel before the damage window opens
	if (CurrentState != EMeleeAttackState::Windup)
	{
		return false;
	}

	StopAttackAnimation();
	SwitchToFirstPersonMesh();
	DrawWeaponBack();
	bInputLocked = false;
	SetState(EMeleeAttackState::Ready);

	return true;
}

bool UMeleeAttackComponent::CanAttack() const
{
	// Blocked by melee weapon being equipped
	if (bExternallyDisabled)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] CanAttack: FALSE - bExternallyDisabled"));
		return false;
	}

	// Must be ready and input not locked
	if ((!bReadyForNextAttackFromNotify && CurrentState != EMeleeAttackState::Ready) || bInputLocked)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] CanAttack: FALSE - State=%d (need Ready=0), bInputLocked=%d"), (int32)CurrentState, bInputLocked);
		return false;
	}

	// Must have valid owner
	if (!OwnerCharacter)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] CanAttack: FALSE - No OwnerCharacter"));
		return false;
	}

	// Don't attack if charge animation is playing
	if (UChargeAnimationComponent* ChargeAnim = OwnerCharacter->FindComponentByClass<UChargeAnimationComponent>())
	{
		if (ChargeAnim->IsAnimating())
		{
			UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] CanAttack: FALSE - ChargeAnimation playing"));
			return false;
		}
	}

	// Check airborne restriction
	UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement();
	if (Movement)
	{
		if (!Settings.bCanAttackInAir && Movement->IsFalling())
		{
			UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] CanAttack: FALSE - Airborne but bCanAttackInAir=false"));
			return false;
		}

		// Note: Sliding check would require ApexMovementComponent
		// For now, we allow it and let the character class handle restrictions
	}

	UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] CanAttack: TRUE"));
	return true;
}

bool UMeleeAttackComponent::IsAttacking() const
{
	// The draw that follows a swing (ShowingWeapon) is NOT part of it: it belongs to the character's
	// weapon phase, and swaps may cut it short exactly as they cut any draw short. Only a new swing
	// may not -- CanAttack wants Ready.
	return CurrentState == EMeleeAttackState::Windup ||
		CurrentState == EMeleeAttackState::Active ||
		CurrentState == EMeleeAttackState::Recovery;
}

float UMeleeAttackComponent::GetCooldownProgress() const
{
	if (CurrentState != EMeleeAttackState::Cooldown)
	{
		return CurrentState == EMeleeAttackState::Ready ? 1.0f : 0.0f;
	}

	if (Settings.Cooldown <= 0.0f)
	{
		return 1.0f;
	}

	return 1.0f - (StateTimeRemaining / Settings.Cooldown);
}

void UMeleeAttackComponent::SetState(EMeleeAttackState NewState)
{
	UE_LOG(LogTemp, Warning, TEXT("[AIRBORNE_DEBUG] SetState: %d -> %d (type=%d elapsed=%.3fs / total=%.3fs %.0f%%)"),
		(int32)CurrentState,
		(int32)NewState,
		(int32)CurrentAttackType,
		MontageTimeElapsed,
		MontageTotalDuration,
		MontageTotalDuration > 0.0f ? (MontageTimeElapsed / MontageTotalDuration) * 100.0f : -1.0f);

	CurrentState = NewState;

	switch (NewState)
	{
	case EMeleeAttackState::Ready:
		StateTimeRemaining = 0.0f;
		bInputLocked = false;
		// Clean up delegated dropkick — re-disable component for melee weapon
		if (bDelegatedDropKick)
		{
			bDelegatedDropKick = false;
			bExternallyDisabled = true;
		}
		break;

	case EMeleeAttackState::Windup:
		// No wind-up time: the swing is armed from its first frame and waits for the notify. The
		// timer only has to reach zero for UpdateState to look at the no-montage fallback.
		StateTimeRemaining = 0.0f;
		break;

	case EMeleeAttackState::Active:
		if (bIsDropKick && MagnetismTarget.IsValid() && OwnerCharacter)
		{
			// For dropkick: calculate travel time to reach target, ensure enough active time.
			// Travel time is physics-bound (DropKickDiveSpeed) — don't shrink it via combo.
			float DistToTarget = FVector::Dist(OwnerCharacter->GetActorLocation(), LungeTargetPosition);
			float TravelTime = (Settings.DropKickDiveSpeed > 0.0f) ? (DistToTarget / Settings.DropKickDiveSpeed) : Settings.ActiveTime;
			StateTimeRemaining = FMath::Max(Settings.ActiveTime / ComboSpeedMultiplier, TravelTime + 0.15f);
		}
		else
		{
			// Active phase fallback timer kept large — the real Active End is driven by
			// the UAnimNotifyState_MeleeDamageWindow's NotifyEnd (calls DeactivateDamageWindowFromNotify
			// which transitions Active -> Recovery) OR by the montage starting to blend out.
			StateTimeRemaining = (MontageTotalDuration > 0.0f)
				? (MontageTotalDuration / ComboSpeedMultiplier) + 0.5f  // safety buffer past natural end
				: (Settings.ActiveTime / ComboSpeedMultiplier);
		}

		// One-shot forward boost when no lunge target was acquired.
		// Natural friction/drag handles the falloff — no per-frame tug needed.
		//
		// NOTE: with bPreserveMomentum on (the shipped setting) this has no effect and never did.
		// The swing is holding its entry speed for as long as it lasts, so the very next simulated
		// move assigns Velocity outright and this addition is gone before it moves anybody. Left
		// exactly as it was rather than quietly turned into a real boost — that is a feel change and
		// the author's call. It is also why it was safe to leave behind in the tick.
		if (!bIsDropKick && !MagnetismTarget.IsValid() && Settings.NoTargetBoostSpeed > 0.0f && OwnerCharacter)
		{
			if (UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement())
			{
				FVector BoostDir = LungeDirection;
				BoostDir.Z = 0.0f;
				if (BoostDir.SizeSquared() < KINDA_SMALL_NUMBER)
				{
					BoostDir = OwnerCharacter->GetActorForwardVector();
					BoostDir.Z = 0.0f;
				}
				BoostDir.Normalize();
				Movement->Velocity += BoostDir * Settings.NoTargetBoostSpeed;
			}
		}

		SpawnSwingTrailFX();
		break;

	case EMeleeAttackState::Recovery:
	{
		// The rest of the montage after the damage window. The swing ends when the animation does,
		// through OnMeleeMontageBlendingOut; this timer is only the fallback for a montage that is
		// cut from outside and never reports its own end. With no montage playing (drop kick, a
		// delegated swing) there is nothing to wait for, and the next tick ends the swing.
		StateTimeRemaining = 0.0f;
		if (CurrentMeleeMontage && MeleeMesh)
		{
			if (const UAnimInstance* AnimInstance = MeleeMesh->GetAnimInstance())
			{
				if (AnimInstance->Montage_IsPlaying(CurrentMeleeMontage))
				{
					const float Rate = FMath::Max(FMath::Abs(AnimInstance->Montage_GetPlayRate(CurrentMeleeMontage)), KINDA_SMALL_NUMBER);
					const float Left = CurrentMeleeMontage->GetPlayLength() - AnimInstance->Montage_GetPosition(CurrentMeleeMontage);
					// Generous on purpose: the play rate curve can slow the tail down after this point.
					StateTimeRemaining = FMath::Max(0.0f, Left) / Rate + 0.5f;
				}
			}
		}
		StopSwingTrailFX();

		// Start drop kick cooldown BEFORE StopMagnetism (which resets bIsDropKick)
		// Skip cooldown for delegated dropkick (weapon doesn't use MeleeAttackComponent's cooldown)
		if (bIsDropKick && Settings.DropKickCooldown > 0.0f && !bDelegatedDropKick)
		{
			DropKickCooldownRemaining = Settings.DropKickCooldown;
			OnDropKickCooldownStarted.Broadcast(Settings.DropKickCooldown);
		}

		StopMagnetism();

		if (!bHasHitThisAttack && !bDelegatedDropKick)
		{
			PlaySound(MissSound);

			// ==================== Titanfall 2: Preserve Momentum on Miss ====================
			// Missing must not punish your movement — the player keeps the speed they swung at.
			// The impulse itself is applied by UApexMovementComponent when the lunge's falling edge
			// is simulated, one move from now: writing Velocity from here is exactly the tick-side
			// write this whole change exists to remove. This only records the decision, and the
			// decision travels with the move.
			if (Settings.bPreserveMomentum && OwnerCharacter)
			{
				if (UApexMovementComponent* Apex = Cast<UApexMovementComponent>(OwnerCharacter->GetCharacterMovement()))
				{
					Apex->SetMeleeLungeRestoreOnEnd(true);
				}
			}
		}
		break;
	}

	case EMeleeAttackState::ShowingWeapon:
		// The weapon's own draw is running on the character. No timer: UpdateState watches the
		// character's phase, so a draw that is cut short (a grapple, a swap) releases this too.
		StateTimeRemaining = 0.0f;
		break;

	case EMeleeAttackState::Cooldown:
		if (bDelegatedDropKick)
		{
			// Delegated: skip cooldown, just broadcast end and clean up
			StateTimeRemaining = 0.0f;
		}
		else
		{
			StateTimeRemaining = Settings.Cooldown / ComboSpeedMultiplier;
		}
		OnMeleeAttackEnded.Broadcast();
		break;

	default:
		break;
	}
}

void UMeleeAttackComponent::UpdateState(float DeltaTime)
{
	if (CurrentState == EMeleeAttackState::Ready)
	{
		return;
	}

	// Perform hit detection during active phase
	if (CurrentState == EMeleeAttackState::Active)
	{
		PerformHitDetection();
	}

	// Update timer
	StateTimeRemaining -= DeltaTime;

	if (StateTimeRemaining <= 0.0f)
	{
		// Transition to next state
		switch (CurrentState)
		{
		case EMeleeAttackState::Windup:
			// Damage window (Active) is opened by the animation notify
			// (ActivateDamageWindowFromNotify), NOT by this timer. Park here and wait for it.
			// Fall back to timer-driven Active only when there is no montage to carry the
			// notify, or for dropkicks (which use their own distance-based damage in Active).
			if (bIsDropKick || CurrentMeleeMontage == nullptr)
			{
				SetState(EMeleeAttackState::Active);
			}
			break;

		case EMeleeAttackState::Active:
			SetState(EMeleeAttackState::Recovery);
			break;

		case EMeleeAttackState::Recovery:
			// Only reached when the montage never reported its end (or there was none).
			EndSwing();
			break;

		case EMeleeAttackState::ShowingWeapon:
		{
			// Held here, re-checked every tick, until the draw is over.
			const AShooterCharacter* Shooter = Cast<AShooterCharacter>(OwnerCharacter);
			if (!Shooter || Shooter->GetWeaponSwitchPhase() != EWeaponSwitchPhase::Drawing)
			{
				// Skip cooldown if we didn't hit an enemy (allows spam-hitting props)
				SetState(bHitEnemyThisAttack ? EMeleeAttackState::Cooldown : EMeleeAttackState::Ready);
			}
			break;
		}

		case EMeleeAttackState::Cooldown:
			SetState(EMeleeAttackState::Ready);
			break;

		default:
			break;
		}
	}
}

bool UMeleeAttackComponent::IsValidMeleeTarget(AActor* HitActor) const
{
	if (!HitActor)
	{
		return false;
	}

	// Don't hit ourselves
	if (HitActor == OwnerCharacter)
	{
		return false;
	}

	// Check if it's a Pawn (character, AI, etc.)
	APawn* HitPawn = Cast<APawn>(HitActor);
	if (HitPawn)
	{
		return true;
	}

	// Check if it implements IShooterDummyTarget (training dummies, etc.)
	if (HitActor->Implements<UShooterDummyTarget>())
	{
		return true;
	}

	// Check if it's a destructible environment target (islands, etc.)
	if (HitActor->ActorHasTag(TEXT("MeleeDestructible")))
	{
		return true;
	}

	// Air Mail: objects flying back to the player (incl. thrown weapons, which are plain
	// actors) must be kickable — the upgrade redirects them on OnMeleeHit.
	if (HitActor->ActorHasTag(UUpgrade_AirKick::TAG_AirMailIncoming))
	{
		return true;
	}

	if (Cast<ASportsBall>(HitActor))
	{
		return true;
	}

	return false;
}

void UMeleeAttackComponent::PerformHitDetection()
{
	if (!OwnerCharacter)
	{
		return;
	}

	// Only allow one hit per attack - exit if we already hit something
	if (bHasHitThisAttack)
	{
		return;
	}

	// Drop kick special case: use distance to target instead of camera trace
	// This prevents missing when camera isn't looking directly at target
	if (bIsDropKick && MagnetismTarget.IsValid())
	{
		AActor* Target = MagnetismTarget.Get();
		FVector PlayerPos = OwnerCharacter->GetActorLocation();
		FVector TargetPos = Target->GetActorLocation();
		float DistanceToTarget = FVector::Dist(PlayerPos, TargetPos);

		// Use AttackRange as hit threshold
		if (DistanceToTarget <= Settings.AttackRange)
		{
			bHasHitThisAttack = true;

			// Delegated mode: only signal hit, weapon handles its own damage/effects
			if (bDelegatedDropKick)
			{
				OnDropKickHit.Broadcast(Target, TargetPos, 0.0f);
				return;
			}

			// Create a fake hit result for the target
			FHitResult FakeHit;
			FakeHit.ImpactPoint = TargetPos;
			FakeHit.ImpactNormal = (PlayerPos - TargetPos).GetSafeNormal();
			FakeHit.Location = TargetPos;
			FakeHit.bBlockingHit = true;

			// Set the hit actor through the component
			if (UPrimitiveComponent* TargetRoot = Cast<UPrimitiveComponent>(Target->GetRootComponent()))
			{
				FakeHit.Component = TargetRoot;
			}

			// Valid hit!
			HitActorsThisAttack.Add(Target);

			// An enemy was hit: the swing earns its cooldown
			bHitEnemyThisAttack = true;

			// Check for headshot (approximate - use upper part of target)
			bool bHeadshot = IsHeadshot(FakeHit);

			// Apply damage and get final damage value
			float FinalDamage = ApplyDamage(Target, FakeHit);

			// Play effects
			PlaySound(HitSound);
			if (!bIsWeaponLowered)
			{
				PlayCameraShake();
			}
			SpawnImpactFX(FakeHit.ImpactPoint, FakeHit.ImpactNormal);

			// Broadcast hit events
			OnMeleeHit.Broadcast(Target, FakeHit.ImpactPoint, bHeadshot, FinalDamage);
			OnDropKickHit.Broadcast(Target, FakeHit.ImpactPoint, FinalDamage);

#if WITH_EDITOR
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
					FString::Printf(TEXT("DropKick HIT! Distance=%.0f, Damage=%.0f"), DistanceToTarget, FinalDamage));
			}
#endif

			// Debug visualization
			if (bEnableDebugVisualization)
			{
				DrawDebugSphere(GetWorld(), TargetPos, Settings.AttackRange, 16, FColor::Green, false, DebugShapeDuration);
			}

			return; // Exit early - we hit the target
		}

		// Debug: show detection radius
		if (bEnableDebugVisualization)
		{
			DrawDebugSphere(GetWorld(), TargetPos, Settings.AttackRange, 16, FColor::Yellow, false, 0.0f);
		}

		// IMPORTANT: During drop kick with magnetism target, do NOT fall through to sweep trace.
		// This prevents accidentally hitting other targets (like ShooterKey) while flying toward the intended target.
		return;
	}

	const FVector Start = GetTraceStart();
	const FVector End = GetTraceEnd();

	UE_LOG(LogTemp, Warning, TEXT("[MeleeHitDetection] Sweep trace: bIsDropKick=%d, Start=(%.1f, %.1f, %.1f), End=(%.1f, %.1f, %.1f)"),
		bIsDropKick, Start.X, Start.Y, Start.Z, End.X, End.Y, End.Z);

	// Set up collision query
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwnerCharacter);
	QueryParams.bTraceComplex = true;
	QueryParams.bReturnPhysicalMaterial = true;

	// Add already-hit actors to ignore list
	for (AActor* HitActor : HitActorsThisAttack)
	{
		QueryParams.AddIgnoredActor(HitActor);
	}

	// Perform sphere trace
	TArray<FHitResult> HitResults;
	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectQueryParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	bool bHit = GetWorld()->SweepMultiByObjectType(
		HitResults,
		Start,
		End,
		FQuat::Identity,
		ObjectQueryParams,
		FCollisionShape::MakeSphere(Settings.AttackRadius),
		QueryParams
	);

	if (bHit)
	{
		HitResults.Sort([](const FHitResult& A, const FHitResult& B)
		{
			return A.Distance < B.Distance;
		});
	}

	// Debug visualization for hit detection trace
	if (bEnableDebugVisualization)
	{
		FColor TraceColor = bHit ? FColor::Green : FColor::Red;
		DrawDebugCapsule(
			GetWorld(),
			(Start + End) * 0.5f,
			FVector::Dist(Start, End) * 0.5f,
			Settings.AttackRadius,
			FQuat::FindBetweenNormals(FVector::UpVector, (End - Start).GetSafeNormal()),
			TraceColor,
			false,
			DebugShapeDuration
		);
		DrawDebugSphere(GetWorld(), Start, Settings.AttackRadius, 12, FColor::Blue, false, DebugShapeDuration);
		DrawDebugSphere(GetWorld(), End, Settings.AttackRadius, 12, FColor::Yellow, false, DebugShapeDuration);
		DrawDebugLine(GetWorld(), Start, End, TraceColor, false, DebugShapeDuration, 0, 2.0f);
	}

	if (bHit)
	{
		for (const FHitResult& Hit : HitResults)
		{
			AActor* HitActor = Hit.GetActor();

			// Skip if already hit this attack
			if (!HitActor || HitActorsThisAttack.Contains(HitActor))
			{
				continue;
			}

			// FIX: Check if this is a valid melee target (Pawn, not wall/geometry)
			if (!IsValidMeleeTarget(HitActor))
			{
				continue;
			}

			// FIX: During drop kick fallback (original target lost mid-flight), don't hit IShooterDummyTarget
			// This prevents accidentally damaging ShooterKey when the intended NPC target dies during the dive
			// Direct drop kick targeting of ShooterKey still works via MagnetismTarget path above
			if (bIsDropKick && HitActor->Implements<UShooterDummyTarget>())
			{
				continue;
			}

			// Check angle if using cone detection
			if (Settings.AttackAngle > 0.0f)
			{
				FVector ToTarget = (Hit.ImpactPoint - Start).GetSafeNormal();
				FVector Forward = GetTraceDirection();
				float Angle = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Forward, ToTarget)));

				if (Angle > Settings.AttackAngle)
				{
					continue;
				}
			}

			// Valid hit!
			HitActorsThisAttack.Add(HitActor);

			UE_LOG(LogTemp, Warning, TEXT("[MeleeHitDetection] SWEEP HIT: %s [%s] at (%.1f, %.1f, %.1f)"),
				*HitActor->GetName(), *HitActor->GetClass()->GetName(),
				Hit.ImpactPoint.X, Hit.ImpactPoint.Y, Hit.ImpactPoint.Z);

			// Check for cool kick trigger (first hit, airborne, no lunge target)
#if WITH_EDITOR
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow,
					FString::Printf(TEXT("Hit Check: bHasHit=%d, AttackType=%d (Airborne=1), HasMagnetism=%d"),
						bHasHitThisAttack ? 1 : 0,
						(int32)CurrentAttackType,
						MagnetismTarget.IsValid() ? 1 : 0));
			}
#endif

			if (!bHasHitThisAttack && CurrentAttackType == EMeleeAttackType::Airborne && !MagnetismTarget.IsValid())
			{
				StartCoolKick();
			}

			// Only an enemy earns the swing its cooldown, not a prop or a destructible
			if (!bHasHitThisAttack && Cast<AShooterNPC>(HitActor))
			{
				bHitEnemyThisAttack = true;
			}
			bHasHitThisAttack = true;

			bool bHeadshot = false;
			float FinalDamage = 0.0f;

			if (ASportsBall* SportsBall = Cast<ASportsBall>(HitActor))
			{
				SportsBall->HandleMeleeAttackHit(
					OwnerCharacter,
					Hit,
					CurrentAttackType,
					GetTraceDirection(),
					OwnerCharacter->GetVelocity());
			}
			else
			{
				// Check for headshot
				bHeadshot = IsHeadshot(Hit);

				// Apply damage and get final damage value
				FinalDamage = ApplyDamage(HitActor, Hit);
			}

			// Play effects
			PlaySound(HitSound);
			// Skip camera shake during boss finisher (weapon is pre-lowered)
			if (!bIsWeaponLowered)
			{
				PlayCameraShake();
			}
			SpawnImpactFX(Hit.ImpactPoint, Hit.ImpactNormal);

			// Broadcast hit event with actual damage dealt
			OnMeleeHit.Broadcast(HitActor, Hit.ImpactPoint, bHeadshot, FinalDamage);

			// Broadcast drop kick specific event
			if (bIsDropKick)
			{
				OnDropKickHit.Broadcast(HitActor, Hit.ImpactPoint, FinalDamage);
			}

			// Debug visualization for hit impact
			if (bEnableDebugVisualization)
			{
				FColor HitColor = bHeadshot ? FColor::Red : FColor::White;
				DrawDebugSphere(GetWorld(), Hit.ImpactPoint, 15.0f, 12, HitColor, false, DebugShapeDuration);
				DrawDebugString(GetWorld(), Hit.ImpactPoint + FVector(0, 0, 30),
					bHeadshot ? TEXT("HEADSHOT!") : TEXT("HIT"), nullptr, HitColor, DebugShapeDuration);
			}

			// Only hit one target per attack
			break;
		}
	}
}

void UMeleeAttackComponent::DealMeleeDamage(AActor* HitActor, float Damage,
	TSubclassOf<UDamageType> DamageTypeClass, const FHitResult& HitResult, const FVector& ShotDirection)
{
	if (!HitActor || !OwnerCharacter || Damage <= 0.0f)
	{
		return;
	}

	if (OwnerCharacter->HasAuthority())
	{
		// Collect any shield pledged against this enemy first. The loan was taken in exchange for the
		// approach; this is the swing that arrives, so this is where it is paid. Done before the
		// damage so a hit that opens the shield and a hit that lands on health read in that order.
		if (AShooterNPC* NPCTarget = Cast<AShooterNPC>(HitActor))
		{
			NPCTarget->ConsumeShieldLoan();
		}

		FPointDamageEvent DamageEvent(Damage, HitResult, ShotDirection, DamageTypeClass);
		HitActor->TakeDamage(Damage, DamageEvent, OwnerCharacter->GetController(), OwnerCharacter);
		return;
	}

	// Client: health belongs to the server, so a local TakeDamage changes nothing on any machine —
	// which is precisely why a client's punches used to do nothing at all. Report it instead and let
	// the authority decide, exactly as a client's gunfire does through DealDamage.
	AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(OwnerCharacter);
	if (!ShooterChar)
	{
		// Not a player character (an NPC's melee component on a client), and nothing to report
		// through. Said out loud rather than returning quietly: a silent return here would read as
		// "the hit never happened" in a log that shows the swing landing.
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s landed a melee hit on a client but is not a ShooterCharacter - %.0f damage dropped"),
			*OwnerCharacter->GetName(), Damage);
		return;
	}

	ShooterChar->Server_ReportMeleeDamage(HitActor, Damage, DamageTypeClass);
}

float UMeleeAttackComponent::GetMaxReportedSingleHitDamage() const
{
	// A melee component with no base damage configured still needs a non-zero ceiling, or every
	// reported hit would clamp to nothing. Mirrors AShooterWeapon::GetMaxReportedSingleHitDamage.
	const float Base = Settings.BaseDamage > 0.0f ? Settings.BaseDamage : 1.0f;
	return Base
		* FMath::Max(Settings.HeadshotMultiplier, 1.0f)
		* FMath::Max(Settings.MaxReportedDamageMultiplier, 1.0f);
}

float UMeleeAttackComponent::GetLungeRangeFor(const AActor* Target) const
{
	const float BaseRange = Settings.bEnableLunge ? Settings.LungeRange : 0.0f;
	if (BaseRange <= 0.0f)
	{
		return 0.0f;
	}

	// The owner's passive gets to extend this, through the character rather than through a lookup of
	// its own: AShooterWeapon_Melee carries a second copy of this whole lunge and has to apply the
	// same passive to ITS base range, and two hand-rolled copies of the lookup would be two things
	// to keep in step.
	if (const AShooterCharacter* Shooter = Cast<AShooterCharacter>(OwnerCharacter))
	{
		return Shooter->ApplyLungePassiveToRange(Target, BaseRange);
	}

	return BaseRange;
}

float UMeleeAttackComponent::GetMaxLungeRange() const
{
	// A null target is the agreed way to ask a passive for its ceiling rather than for one enemy's
	// answer. See UAbilityHandler::ModifyLungeRange.
	return GetLungeRangeFor(nullptr);
}

float UMeleeAttackComponent::GetMaxReportedReach() const
{
	// The swing itself, plus the ground the approach could have covered before it landed. The lunge
	// and the dropkick dive are alternatives, never both at once, so the larger of the two is the
	// honest bound.
	//
	// The lunge half has to be the CEILING, not Settings.LungeRange: a Melee who crosses a room at a
	// stripped enemy lands a hit further out than the base range, and validating against the base
	// range would reject his own legitimate swing as reaching too far. That failure is silent from
	// the player's side -- the swing plays, the enemy takes nothing -- so it is worth spelling out.
	const float Approach = FMath::Max(
		GetMaxLungeRange(),
		Settings.bEnableDropKick ? Settings.DropKickMaxRange : 0.0f);
	return Settings.AttackRange + Approach;
}

float UMeleeAttackComponent::ApplyDamage(AActor* HitActor, const FHitResult& HitResult)
{
	if (!HitActor || !OwnerCharacter)
	{
		return 0.0f;
	}

	// EMF Foliage->Prop conversion: a melee strike on a UEMFConvertibleFoliageType
	// instance promotes the foliage to a real EMFPhysicsProp before any damage runs.
	// All subsequent TakeDamage calls in this function are routed to the spawned prop.
	if (AEMFPhysicsProp* ConvertedProp = UFoliageConversionLibrary::TryConvertFoliageInstance(HitResult, Settings.BaseDamage))
	{
		HitActor = ConvertedProp;
	}

	// Boss finisher now triggers at the START of the swing (lunge-cone detection in StartMagnetism),
	// not on a successful hit. A finisher-phase boss is invulnerable in TakeDamage, so a stray hit
	// landing here is simply a no-op.

	float TotalDamage = 0.0f;
	FVector TraceDir = GetTraceDirection();

	// ==================== 1. Apply Base Melee Damage ====================
	float BaseDamage = Settings.BaseDamage;

	// Apply headshot multiplier to base damage only
	if (IsHeadshot(HitResult))
	{
		BaseDamage *= Settings.HeadshotMultiplier;
	}

	// Apply upgrade-driven melee multiplier (e.g. Backstab: 3x on stunned NPC from behind).
	if (AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(OwnerCharacter))
	{
		if (UUpgradeManagerComponent* UpgradeMgr = ShooterChar->GetUpgradeManager())
		{
			const float UpgradeMult = UpgradeMgr->GetCombinedMeleeDamageMultiplier(HitActor);
			if (!FMath::IsNearlyEqual(UpgradeMult, 1.0f))
			{
				UE_LOG(LogTemp, Warning, TEXT("[MELEE_DMG_DEBUG] Upgrade multiplier %.2fx vs %s — base %.1f -> %.1f"),
					UpgradeMult, *HitActor->GetName(), BaseDamage, BaseDamage * UpgradeMult);
				BaseDamage *= UpgradeMult;
			}
		}
	}

	// Apply tag-based melee multiplier (mirrors AShooterWeapon::TagDamageMultipliers).
	const float TagMult = GetTagDamageMultiplier(HitActor);
	if (!FMath::IsNearlyEqual(TagMult, 1.0f))
	{
		UE_LOG(LogTemp, Warning, TEXT("[MELEE_DMG_DEBUG] Tag multiplier %.2fx vs %s — base %.1f -> %.1f"),
			TagMult, *HitActor->GetName(), BaseDamage, BaseDamage * TagMult);
		BaseDamage *= TagMult;
	}

	if (BaseDamage > 0.0f)
	{
		DealMeleeDamage(HitActor, BaseDamage, Settings.DamageType, HitResult, TraceDir);
		TotalDamage += BaseDamage;
	}

	// ==================== 2. Apply Momentum Bonus Damage (Kinetic category) ====================
	float MomentumDamage = CalculateMomentumDamage(HitActor);
	if (MomentumDamage > 0.0f)
	{
		DealMeleeDamage(HitActor, MomentumDamage, UDamageType_MomentumBonus::StaticClass(), HitResult, TraceDir);
		TotalDamage += MomentumDamage;
	}

	// ==================== 3. Apply Drop Kick Bonus Damage (Kinetic category) ====================
	float DropKickDamage = CalculateDropKickBonusDamage();
	if (DropKickDamage > 0.0f)
	{
		DealMeleeDamage(HitActor, DropKickDamage, UDamageType_Dropkick::StaticClass(), HitResult, TraceDir);
		TotalDamage += DropKickDamage;
	}

	float FinalDamage = TotalDamage;

	// ==================== Titanfall 2 Momentum Transfer ====================
	// When hitting an enemy while flying at high speed, transfer that momentum to them
	// This creates the satisfying "flying kick" feel where enemies get launched

	FVector ImpulseDirection = GetTraceDirection();
	float FinalImpulse = Settings.HitImpulse * CalculateMomentumImpulseMultiplier();

	if (Settings.bTransferMomentumOnHit)
	{
		// Calculate momentum-based impulse from player velocity
		FVector MomentumImpulse = OwnerVelocityAtAttackStart * Settings.MomentumTransferMultiplier;

		// Project player velocity onto attack direction for more directed knockback
		float VelocityInAttackDir = FVector::DotProduct(OwnerVelocityAtAttackStart, ImpulseDirection);

		if (VelocityInAttackDir > 0.0f)
		{
			// Player was moving toward target - add that momentum as extra knockback
			// This makes high-speed attacks feel much more powerful
			float MomentumBonus = VelocityInAttackDir * Settings.MomentumTransferMultiplier;
			FinalImpulse += MomentumBonus;

			// REMOVED: Vertical "pop" effect - now using friction reduction for smooth ground slide
		}

		// Debug: Show momentum transfer
#if WITH_EDITOR
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
				FString::Printf(TEXT("Titanfall Melee: Speed=%.0f, Impulse=%.0f"),
					OwnerVelocityAtAttackStart.Size(), FinalImpulse));
		}
#endif
	}

	// Apply impulse - try character launch first, then physics
	ApplyCharacterImpulse(HitActor, ImpulseDirection, FinalImpulse);

	return FinalDamage;
}

bool UMeleeAttackComponent::IsHeadshot(const FHitResult& HitResult) const
{
	// Check bone name for common head bone names
	FName BoneName = HitResult.BoneName;
	if (BoneName.IsNone())
	{
		return false;
	}

	FString BoneString = BoneName.ToString().ToLower();
	return BoneString.Contains(TEXT("head")) ||
		BoneString.Contains(TEXT("neck")) ||
		BoneString.Contains(TEXT("face"));
}

void UMeleeAttackComponent::UpdateLunge(float DeltaTime)
{
	// ==================== Titanfall 2 Momentum System ====================
	// Key principle: NEVER kill the player's momentum during melee. That is what lets you punch
	// while flying at 2000+ units/sec and come out the other side still flying.
	//
	// This function no longer moves anybody. It decides, once per frame, WHETHER the swing wants
	// velocity and WHERE it is flying, and hands that to UApexMovementComponent, which does the
	// flying from inside the simulated move. The reason is networking, not tidiness: velocity
	// written from a component tick is not part of the move the server replays, so the server
	// simulated a walking player while the client flew, and corrected the client back every frame.
	// The symptom was the usual one — it worked for the host and only for the host.
	UApexMovementComponent* Apex = OwnerCharacter
		? Cast<UApexMovementComponent>(OwnerCharacter->GetCharacterMovement())
		: nullptr;
	if (!Apex)
	{
		return;
	}

	// Everything below is only meaningful during the two phases the swing actually flies in. Note
	// this runs on every other frame too, and publishes "not lunging" — that falling edge is what
	// ends the flight on both machines.
	//
	// A delegated lunge is a third way in, and the only one that does not involve this component's
	// own state machine at all: the melee weapon runs its own swing and borrows only the flight, so
	// there is no Windup and no Active here to look at.
	const bool bInLungePhase =
		(CurrentState == EMeleeAttackState::Windup || CurrentState == EMeleeAttackState::Active)
		|| bDelegatedLunge;

	// bPreserveMomentum off now means the swing does not drive velocity at all. The old branch for it
	// pushed the character forward at NoTargetBoostSpeed every frame from this tick, and there is no
	// honest way to network that without inventing wire data for a setting the project ships as true
	// (its own comments say to keep it true). Turning it off is a "no lunge" switch now.
	//
	// A drop kick is the same kind of flight and rides the same channel: it reuses the target and the
	// target actor and adds one flag, rather than opening a second set of everything.
	const bool bLunging = bInLungePhase && (Settings.bPreserveMomentum || bIsDropKick);

	if (!bLunging)
	{
		Apex->SetMeleeLungeIntent(false, false, false, FVector::ZeroVector, nullptr);
		return;
	}

	// Optional gate: require a minimum starting speed (TF2 default is 0 = works from standstill).
	// A swing that fails the gate is a swing with nobody to fly at, gravity and all.
	const bool bSpeedGatePassed = OwnerVelocityAtAttackStart.Size() >= Settings.MinSpeedForLunge;
	const bool bHasTarget = MagnetismTarget.IsValid() && bSpeedGatePassed;

	// Stop homing the instant the target is knocked back, otherwise the player keeps driving into
	// the spot the enemy just vacated (mirrors the UpdateMagnetism guard).
	bool bTargetInKnockback = false;
	if (AShooterNPC* TargetNPC = Cast<AShooterNPC>(MagnetismTarget.Get()))
	{
		bTargetInKnockback = TargetNPC->IsInKnockback();
	}

	// bHasHitThisAttack gate: once the hit has landed, never resume homing — otherwise, if the
	// target's knockback stun ends while the Active phase is still running, the player gets dragged
	// toward the enemy a second time.
	const bool bHoming = Settings.bEnableLunge && bHasTarget && !bTargetInKnockback && !bHasHitThisAttack;

	if (bIsDropKick)
	{
		// Diving. Gravity stays on, as it always did for the dive, so bHasTarget is false here on
		// purpose. The "still tracking" question is the one UpdateDropKick answers.
		const bool bDivingNow = MagnetismTarget.IsValid() && !bHasHitThisAttack;

		// Whether the player is asking to carry momentum out of the dive. Read HERE, on the machine
		// that actually has the input, and sent as a decision — inside the simulation the server sees
		// a remote pawn's input as nothing at all.
		bool bForwardHeld = false;
		if (UCharacterMovementComponent* Move = OwnerCharacter->GetCharacterMovement())
		{
			FVector InputVector = Move->GetPendingInputVector();
			if (InputVector.IsNearlyZero())
			{
				InputVector = Move->GetLastInputVector();
			}
			FVector ForwardDir = OwnerCharacter->GetActorForwardVector();
			ForwardDir.Z = 0.0f;
			if (ForwardDir.Normalize())
			{
				bForwardHeld = FVector::DotProduct(InputVector, ForwardDir) > 0.1f;
			}
		}

		Apex->SetMeleeLungeIntent(true, /*bHasTarget*/ false, bDivingNow, LungeTargetPosition,
			MagnetismTarget.Get(), /*bDropKick*/ true, bForwardHeld);

		LungeProgress += DeltaTime / Settings.LungeDuration;
		LungeProgress = FMath::Clamp(LungeProgress, 0.0f, 1.0f);
		return;
	}

	// LungeTargetPosition is refreshed every frame in UpdateMagnetism and tracks the target in XY
	// and Z both. It is the one piece of geometry that travels to the server rather than being
	// re-derived there: two enemies side by side, and each end would pick a different one.
	Apex->SetMeleeLungeIntent(true, bHasTarget, bHoming, LungeTargetPosition, MagnetismTarget.Get());

	LungeProgress += DeltaTime / Settings.LungeDuration;
	LungeProgress = FMath::Clamp(LungeProgress, 0.0f, 1.0f);
}

void UMeleeAttackComponent::PlayAttackAnimation()
{
	if (!OwnerCharacter)
	{
		return;
	}

	// Get animation data for current attack type
	const FMeleeAnimationData& AnimData = GetCurrentAnimationData();

	// Play melee mesh montage
	if (AnimData.AttackMontage && MeleeMesh)
	{
		if (UAnimInstance* AnimInstance = MeleeMesh->GetAnimInstance())
		{
			CurrentMeleeMontage = AnimData.AttackMontage;
			MontageTimeElapsed = 0.0f;
			MontageTotalDuration = AnimData.AttackMontage->GetPlayLength();

			float PlayRate = AnimData.BasePlayRate * ComboSpeedMultiplier;

			// Sample play rate curve at start if available
			if (AnimData.PlayRateCurve)
			{
				PlayRate *= AnimData.PlayRateCurve->GetFloatValue(0.0f);
			}

			AnimInstance->Montage_Play(AnimData.AttackMontage, PlayRate);

			// Bind to montage end
			FOnMontageEnded EndDelegate;
			EndDelegate.BindUObject(this, &UMeleeAttackComponent::OnMeleeMontageEnded);
			AnimInstance->Montage_SetEndDelegate(EndDelegate, AnimData.AttackMontage);

			// And to the start of its blend-out, which is where the swing actually ends.
			FOnMontageBlendingOutStarted BlendOutDelegate;
			BlendOutDelegate.BindUObject(this, &UMeleeAttackComponent::OnMeleeMontageBlendingOut);
			AnimInstance->Montage_SetBlendingOutDelegate(BlendOutDelegate, AnimData.AttackMontage);

			UE_LOG(LogTemp, Warning, TEXT("[AIRBORNE_DEBUG] PlayAttackAnimation: type=%d (0=Ground,1=Airborne,2=Sliding) montage='%s' totalLen=%.3fs playRate=%.2f -> realDuration=%.3fs basePlayRate=%.2f comboMult=%.2f"),
				(int32)CurrentAttackType,
				*AnimData.AttackMontage->GetName(),
				MontageTotalDuration,
				PlayRate,
				PlayRate > 0.0f ? MontageTotalDuration / PlayRate : -1.0f,
				AnimData.BasePlayRate,
				ComboSpeedMultiplier);
		}
	}

	// Play third person montage
	if (ThirdPersonMontage)
	{
		if (USkeletalMeshComponent* TPMesh = OwnerCharacter->GetMesh())
		{
			if (UAnimInstance* AnimInstance = TPMesh->GetAnimInstance())
			{
				AnimInstance->Montage_Play(ThirdPersonMontage);
			}
		}
	}
}

void UMeleeAttackComponent::StopAttackAnimation()
{
	if (!OwnerCharacter)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[AIRBORNE_DEBUG] StopAttackAnimation called: state=%d type=%d elapsed=%.3fs / total=%.3fs (%.0f%%) montage='%s'"),
		(int32)CurrentState,
		(int32)CurrentAttackType,
		MontageTimeElapsed,
		MontageTotalDuration,
		MontageTotalDuration > 0.0f ? (MontageTimeElapsed / MontageTotalDuration) * 100.0f : -1.0f,
		CurrentMeleeMontage ? *CurrentMeleeMontage->GetName() : TEXT("NULL"));

	// Stop melee mesh montage
	if (CurrentMeleeMontage && MeleeMesh)
	{
		if (UAnimInstance* AnimInstance = MeleeMesh->GetAnimInstance())
		{
			AnimInstance->Montage_Stop(0.2f, CurrentMeleeMontage);
		}
	}
	CurrentMeleeMontage = nullptr;

	// Stop third person montage
	if (ThirdPersonMontage)
	{
		if (USkeletalMeshComponent* TPMesh = OwnerCharacter->GetMesh())
		{
			if (UAnimInstance* AnimInstance = TPMesh->GetAnimInstance())
			{
				AnimInstance->Montage_Stop(0.2f, ThirdPersonMontage);
			}
		}
	}
}

void UMeleeAttackComponent::PlaySound(USoundBase* Sound)
{
	if (!Sound || !OwnerCharacter)
	{
		return;
	}

	UGameplayStatics::PlaySoundAtLocation(
		this,
		Sound,
		OwnerCharacter->GetActorLocation(),
		1.0f,  // VolumeMultiplier
		1.0f   // PitchMultiplier
	);
}

void UMeleeAttackComponent::PlayCameraShake()
{
	if (!HitCameraShake || !OwnerController)
	{
		return;
	}

	OwnerController->ClientStartCameraShake(HitCameraShake, CameraShakeScale);
}

FVector UMeleeAttackComponent::GetTraceStart() const
{
	if (!OwnerCharacter)
	{
		return FVector::ZeroVector;
	}

	// Try to get camera location first
	if (OwnerController)
	{
		FVector CameraLocation;
		FRotator CameraRotation;
		OwnerController->GetPlayerViewPoint(CameraLocation, CameraRotation);

		// Add forward offset
		return CameraLocation + CameraRotation.Vector() * Settings.TraceForwardOffset;
	}

	// Fallback to character location + eye height
	return OwnerCharacter->GetPawnViewLocation() +
		OwnerCharacter->GetActorForwardVector() * Settings.TraceForwardOffset;
}

FVector UMeleeAttackComponent::GetTraceEnd() const
{
	return GetTraceStart() + GetTraceDirection() * Settings.AttackRange;
}

FVector UMeleeAttackComponent::GetTraceDirection() const
{
	if (!OwnerCharacter)
	{
		return FVector::ForwardVector;
	}

	// Try to get camera direction first
	if (OwnerController)
	{
		FVector CameraLocation;
		FRotator CameraRotation;
		OwnerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
		return CameraRotation.Vector();
	}

	// Fallback to character forward
	return OwnerCharacter->GetActorForwardVector();
}

FVector UMeleeAttackComponent::GetLungeDirection() const
{
	if (!OwnerCharacter)
	{
		return FVector::ForwardVector;
	}

	// Get current movement velocity
	UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement();
	if (Movement)
	{
		FVector Velocity = Movement->Velocity;
		// Only consider horizontal velocity for lunge direction
		Velocity.Z = 0.0f;

		// Use velocity direction if moving fast enough (threshold to avoid jitter when nearly stationary)
		const float MinVelocityThreshold = 50.0f;
		if (Velocity.SizeSquared() > FMath::Square(MinVelocityThreshold))
		{
			return Velocity.GetSafeNormal();
		}
	}

	// Fallback to camera/view direction if not moving
	FVector ViewDirection = GetTraceDirection();
	// Make it horizontal for consistent lunge behavior
	ViewDirection.Z = 0.0f;
	return ViewDirection.GetSafeNormal();
}

void UMeleeAttackComponent::SpawnSwingTrailFX()
{
	if (!SwingTrailFX || !OwnerCharacter)
	{
		return;
	}

	// Try to find the first person mesh to attach to
	USkeletalMeshComponent* AttachMesh = nullptr;

	// Look for a skeletal mesh component (first person mesh)
	TArray<USkeletalMeshComponent*> SkeletalMeshes;
	OwnerCharacter->GetComponents<USkeletalMeshComponent>(SkeletalMeshes);

	for (USkeletalMeshComponent* Mesh : SkeletalMeshes)
	{
		// Skip the main character mesh (third person)
		if (Mesh != OwnerCharacter->GetMesh())
		{
			AttachMesh = Mesh;
			break;
		}
	}

	if (AttachMesh)
	{
		// Spawn attached to socket
		ActiveTrailFX = UNiagaraFunctionLibrary::SpawnSystemAttached(
			SwingTrailFX,
			AttachMesh,
			TrailSocketName,
			TrailOffset,
			TrailRotationOffset,
			EAttachLocation::SnapToTarget,
			false // bAutoDestroy - we'll manage lifetime manually
		);
	}
	else
	{
		// Fallback: spawn at character location
		ActiveTrailFX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			SwingTrailFX,
			OwnerCharacter->GetActorLocation() + TrailOffset,
			OwnerCharacter->GetActorRotation() + TrailRotationOffset
		);
	}
}

void UMeleeAttackComponent::StopSwingTrailFX()
{
	if (ActiveTrailFX)
	{
		// Deactivate the system (allows particles to finish)
		ActiveTrailFX->Deactivate();

		// Clear reference - component will auto-destroy when particles finish
		ActiveTrailFX = nullptr;
	}
}

void UMeleeAttackComponent::SpawnImpactFX(const FVector& Location, const FVector& Normal)
{
	if (!ImpactFX)
	{
		return;
	}

	// Calculate rotation from normal
	FRotator ImpactRotation = Normal.Rotation();

	// Spawn impact effect
	UNiagaraComponent* ImpactComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		ImpactFX,
		Location,
		ImpactRotation,
		FVector(ImpactFXScale),
		true,  // bAutoDestroy
		true,  // bAutoActivate
		ENCPoolMethod::None
	);

	// Optional: Set any parameters on the impact effect
	if (ImpactComponent)
	{
		// You can set Niagara parameters here if needed
		// ImpactComponent->SetVariableFloat(FName("Intensity"), 1.0f);
	}
}

// ==================== Focus lock ====================

bool UMeleeAttackComponent::TryStartFocus()
{
	// The machine that pressed the button and no other. The view is this client's own, and the lunge
	// target it produces travels inside the saved move like it always did.
	if (!Settings.bEnableLunge || !OwnerCharacter || !OwnerCharacter->IsLocallyControlled())
	{
		return false;
	}

	AActor* Candidate = FindBestLungeCandidate();
	if (!Candidate)
	{
		// Nothing in reach. The press was not used, so the caller is free to do whatever it did
		// before -- aim down sights, or nothing at all.
		return false;
	}

	FocusTarget = Candidate;

	if (AShooterCharacter::IsLungeDebugEnabled())
	{
		UE_LOG(LogTemp, Warning, TEXT("[LUNGE_DEBUG] FOCUS on %s: locked %s at %.0f cm (allowed %.0f)"),
			*GetNameSafe(OwnerCharacter), *GetNameSafe(Candidate),
			FVector::Dist(GetTraceStart(), Candidate->GetActorLocation()), GetLungeRangeFor(Candidate));
	}

	return true;
}

void UMeleeAttackComponent::StopFocus()
{
	// Drops the TARGET, not the request. Called both when the button comes up and when a locked
	// target stops qualifying (it died, it went down, it left reach), and those two must not mean the
	// same thing: an enemy dying under a held button should hand the lock to the next one, not end
	// the lock until the player presses again. Letting go is SetFocusHeld(false).
	FocusTarget.Reset();
}

void UMeleeAttackComponent::SetFocusHeld(bool bHeld)
{
	bFocusHeld = bHeld;

	// Look on the very next frame rather than after a full interval: the press itself already looked
	// once, and this is for the case where that look found nothing.
	FocusRetryTimer = 0.0f;

	if (!bHeld)
	{
		FocusTarget.Reset();
	}
}

bool UMeleeAttackComponent::IsFocusTargetStillValid(const AActor* Target) const
{
	if (!IsValid(Target) || !OwnerCharacter)
	{
		return false;
	}

	// The same two exclusions the search applies, re-asked every frame: a target can die or go down
	// while it is being held, and holding the view on a corpse is the failure this catches.
	if (const AShooterNPC* NPCTarget = Cast<AShooterNPC>(Target))
	{
		if (NPCTarget->IsDead())
		{
			return false;
		}
	}

	if (const AShooterCharacter* PlayerTarget = Cast<AShooterCharacter>(Target))
	{
		if (PlayerTarget->IsDowned() || PlayerTarget->IsDead())
		{
			return false;
		}
	}

	// Range only, deliberately: no cone. Once locked, the target is allowed to be anywhere on screen
	// -- holding the view on it is the mechanic, and re-testing the cone would fight the very thing
	// the lock is doing.
	const float Allowed = GetLungeRangeFor(Target) * FMath::Max(1.0f, FocusBreakRangeSlack);
	return Allowed > 0.0f
		&& FVector::DistSquared(GetTraceStart(), Target->GetActorLocation()) <= FMath::Square(Allowed);
}

FVector UMeleeAttackComponent::GetFocusAimPoint(const AActor* Target) const
{
	if (!Target)
	{
		return FVector::ZeroVector;
	}

	// The bone first. It is the only answer that moves with the target's animation, and it is also
	// the only one the author can point at something specific with.
	if (!FocusAimBone.IsNone())
	{
		if (const USkeletalMeshComponent* Mesh = Target->FindComponentByClass<USkeletalMeshComponent>())
		{
			// DoesSocketExist covers both real sockets and bones, which is what makes this safe to
			// point at a rig that does not have the bone: it answers false instead of handing back
			// the component's own origin, which is what GetSocketLocation does on a miss and would
			// have looked like the lock silently aiming at the feet.
			if (Mesh->DoesSocketExist(FocusAimBone))
			{
				return Mesh->GetSocketLocation(FocusAimBone) + FVector(0.0f, 0.0f, FocusAimZOffset);
			}
		}
	}

	// No skeleton, or no such bone on it. Height off the target's own collision bounds.
	FVector Origin, Extent;
	Target->GetActorBounds(true, Origin, Extent);

	const float Bottom = Origin.Z - Extent.Z;
	const float Height = Extent.Z * 2.0f;

	return FVector(
		Origin.X,
		Origin.Y,
		Bottom + Height * FMath::Clamp(FocusAimHeightFraction, 0.0f, 1.0f) + FocusAimZOffset);
}

void UMeleeAttackComponent::UpdateFocus(float DeltaTime)
{
	// Held with nothing locked: keep looking. This is the whole "do not make me press again" half of
	// the mechanic -- the player asks once, by holding, and the lock answers as soon as it can.
	if (bFocusHeld && !FocusTarget.IsValid())
	{
		FocusRetryTimer -= DeltaTime;
		if (FocusRetryTimer <= 0.0f)
		{
			FocusRetryTimer = FMath::Max(0.02f, FocusRetryInterval);
			TryStartFocus();
		}
	}

	if (!FocusTarget.IsValid())
	{
		return;
	}

	if (!OwnerController || !OwnerCharacter || !IsFocusTargetStillValid(FocusTarget.Get()))
	{
		StopFocus();
		return;
	}

	// A pull, not a pin. The player's own mouse movement is applied to the control rotation as usual
	// and this leans it back toward the target every frame, so the lock can be fought and aimed
	// around instead of taking the camera away.
	const FRotator Current = OwnerController->GetControlRotation();

	FRotator Desired = (GetFocusAimPoint(FocusTarget.Get()) - GetTraceStart()).Rotation();
	Desired.Roll = Current.Roll;

	// Inside the snap window the view is PUT on the target, not pulled toward it.
	//
	// An interpolation is asymptotic: it closes a fraction of the error per frame and therefore never
	// arrives, so against anything that moves the target sits a few degrees off centre for the whole
	// lock. That residual error is exactly what read as "вяло наводится". The pull still does the
	// long part of the swing, which is what keeps the camera from being snatched away.
	const float ErrorDegrees = FMath::Abs(FRotator::NormalizeAxis(Desired.Yaw - Current.Yaw))
		+ FMath::Abs(FRotator::NormalizeAxis(Desired.Pitch - Current.Pitch));

	if (ErrorDegrees <= FocusHardSnapDegrees)
	{
		OwnerController->SetControlRotation(Desired);
		return;
	}

	OwnerController->SetControlRotation(
		FMath::RInterpTo(Current, Desired, DeltaTime, FMath::Max(0.5f, FocusTrackingSpeed)));
}

AActor* UMeleeAttackComponent::FindBestLungeCandidate() const
{
	// ==================== Cone-based Target Acquisition (TF2-style) ====================
	// Sphere overlap within LungeRange, filtered by dot-product against camera forward.
	// Picks the candidate with the highest dot (most centered in the cone).
	const FVector Start = GetTraceStart();
	const FVector Forward = GetTraceDirection();
	const float CosThreshold = FMath::Cos(FMath::DegreesToRadians(Settings.LungeConeHalfAngle));

	// Two ranges, and they are not the same number any more. The sphere is sized to the furthest any
	// enemy could possibly be allowed to be, because a target has to be FOUND before its own allowed
	// range can be worked out from its shield; each candidate is then held to its own range in the
	// filter below. Sizing the search to the base range instead would mean the extended reach could
	// never see the enemy it exists for.
	const float SearchRadius = GetMaxLungeRange();
	if (SearchRadius <= 0.0f)
	{
		return nullptr;
	}

	// Says COMPONENT so a log full of lunge lines cannot be mistaken for the weapon's copy. If this
	// line appears while a blade is equipped, something has gone wrong with SetExternallyDisabled --
	// the two are never supposed to run in the same swing.
	if (AShooterCharacter::IsLungeDebugEnabled())
	{
		UE_LOG(LogTemp, Warning, TEXT("[LUNGE_DEBUG] COMPONENT on %s: searching %.0f cm (ceiling), base %.0f, cone %.0f deg"),
			*GetNameSafe(OwnerCharacter), SearchRadius, Settings.LungeRange, Settings.LungeConeHalfAngle);
	}

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwnerCharacter);

	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByChannel(
		Overlaps,
		Start,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(SearchRadius),
		QueryParams
	);

	AActor* BestTarget = nullptr;
	float BestDot = CosThreshold;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* HitActor = Overlap.GetActor();
		if (!HitActor || HitActor == OwnerCharacter || !Cast<ACharacter>(HitActor))
		{
			continue;
		}

		// Not at corpses. The filter was "is it a character", which a dead enemy still is: its actor
		// lives on for the ragdoll, and under the pooling model it lives on indefinitely and gets
		// parked and moved. A swing near one flew the player at whatever the body's actor location
		// happened to be — the bench caught a lunge aimed eight metres up.
		if (const AShooterNPC* NPCTarget = Cast<AShooterNPC>(HitActor))
		{
			if (NPCTarget->IsDead())
			{
				continue;
			}
		}

		// Nor at a teammate who is already down. Flying at somebody waiting to be picked up is never
		// what the swing meant, and they cannot be hurt anyway.
		if (const AShooterCharacter* PlayerTarget = Cast<AShooterCharacter>(HitActor))
		{
			if (PlayerTarget->IsDowned() || PlayerTarget->IsDead())
			{
				continue;
			}
		}

		// Each candidate is judged against its OWN allowed reach, not against one range shared by
		// all of them: for the Melee class that is the base range against an intact shield and much
		// further into a broken one, so two enemies standing side by side can legitimately give
		// different answers.
		FVector ToTarget = HitActor->GetActorLocation() - Start;
		const float Dist = ToTarget.Size();
		if (Dist <= KINDA_SMALL_NUMBER || Dist > GetLungeRangeFor(HitActor))
		{
			continue;
		}
		ToTarget /= Dist;

		const float Dot = FVector::DotProduct(Forward, ToTarget);
		if (Dot >= BestDot)
		{
			BestDot = Dot;
			BestTarget = HitActor;
		}
	}

	if (bEnableDebugVisualization)
	{
		DrawDebugCone(
			GetWorld(),
			Start,
			Forward,
			SearchRadius,
			FMath::DegreesToRadians(Settings.LungeConeHalfAngle),
			FMath::DegreesToRadians(Settings.LungeConeHalfAngle),
			16,
			BestTarget ? FColor::Magenta : FColor::Orange,
			false,
			DebugShapeDuration
		);
	}

	return BestTarget;
}

void UMeleeAttackComponent::StartMagnetism()
{
	if (!Settings.bEnableLunge || !OwnerCharacter)
	{
		return;
	}

	MagnetismTarget.Reset();
	bIsDropKick = false;
	DropKickHeightDifference = 0.0f;

	// Check for drop kick first (airborne + looking down)
	UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] StartMagnetism: checking dropkick..."));
	if (ShouldPerformDropKick() && TryStartDropKick())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] StartMagnetism: DROPKICK started successfully!"));
		// Drop kick started successfully - skip normal lunge
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] StartMagnetism: dropkick NOT started, proceeding with normal lunge"));

	// ==================== The locked target, and only it ====================
	// The swing no longer looks for a victim. An automatic pull toward whatever the camera happened
	// to cross is exactly what this rework removed: the player holds aim, that locks a target inside
	// the reach the passive grants, and the swing flies at that one or at nobody.
	//
	// A swing with no lock still swings. It just does not travel, which is what NoTargetBoostSpeed
	// further down is for.
	AActor* BestTarget = nullptr;
	if (FocusTarget.IsValid() && IsFocusTargetStillValid(FocusTarget.Get()))
	{
		BestTarget = FocusTarget.Get();
	}

	if (!BestTarget)
	{
		if (AShooterCharacter::IsLungeDebugEnabled())
		{
			UE_LOG(LogTemp, Warning, TEXT("[LUNGE_DEBUG] COMPONENT on %s: no focus lock, swing does not travel"),
				*GetNameSafe(OwnerCharacter));
		}
		return;
	}

	// ==================== Calculate Stop Position ====================
	// Direct stop distance from target (no AttackRange math anymore).
	FVector PlayerPos = OwnerCharacter->GetActorLocation();
	FVector TargetPos = BestTarget->GetActorLocation();
	FVector DirectionFromTarget = (PlayerPos - TargetPos).GetSafeNormal();
	FVector IdealLungePos = TargetPos + DirectionFromTarget * Settings.LungeStopDistance;

	// ==================== Path Validation ====================
	FHitResult SweepHit;
	FCollisionQueryParams SweepParams;
	SweepParams.AddIgnoredActor(OwnerCharacter);
	SweepParams.AddIgnoredActor(BestTarget);

	const float ProbeRadius = FMath::Max(Settings.LungeStopDistance, 20.0f);
	const bool bPathBlocked = GetWorld()->SweepSingleByChannel(
		SweepHit,
		PlayerPos,
		IdealLungePos,
		FQuat::Identity,
		ECC_Visibility,
		FCollisionShape::MakeSphere(ProbeRadius),
		SweepParams
	);

	if (bEnableDebugVisualization)
	{
		FColor PathColor = bPathBlocked ? FColor::Red : FColor::Green;
		DrawDebugSphere(GetWorld(), IdealLungePos, ProbeRadius, 12, PathColor, false, DebugShapeDuration);
		DrawDebugLine(GetWorld(), PlayerPos, IdealLungePos, PathColor, false, DebugShapeDuration, 0, 2.0f);
	}

	if (bPathBlocked)
	{
#if WITH_EDITOR
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Red, TEXT("Lunge FAILED: Path blocked"));
		}
#endif
		return;
	}

	MagnetismTarget = BestTarget;
	LungeTargetPosition = IdealLungePos;

	// The mutual move-collision ignore for the flight is NOT done here any more. It used to be, and
	// it ran only on the swinging machine: a client passed through the enemy while the server kept
	// colliding with it, and the two disagreed about where the character finished the lunge. It now
	// lives in UApexMovementComponent, driven by the lunge flags, so both ends do it together.
	// @see UApexMovementComponent::SetMeleeLungeTargetIgnored

	StartCameraFocus(BestTarget);

	// Gravity is no longer switched off here. GravityScale is movement state, and setting it from
	// this component's tick left the server's copy untouched — it kept applying gravity while the
	// client flew flat. UApexMovementComponent now derives it from the lunge flags, which travel,
	// and puts it back on its own when the flight ends. @see UApexMovementComponent::SyncMeleeLungeGravity

#if WITH_EDITOR
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Green,
			FString::Printf(TEXT("Lunge: locked Target=%s, StopAt=%.0fcm"),
				*BestTarget->GetName(), Settings.LungeStopDistance));
	}
#endif
}

void UMeleeAttackComponent::UpdateMagnetism(float DeltaTime)
{
	// Only during windup and active phases
	if (CurrentState != EMeleeAttackState::Windup && CurrentState != EMeleeAttackState::Active)
	{
		return;
	}

	// Handle drop kick movement separately
	if (bIsDropKick)
	{
		UpdateDropKick(DeltaTime);
		return;
	}

	if (!Settings.bEnableLunge || !MagnetismTarget.IsValid() || !OwnerCharacter)
	{
		return;
	}

	AActor* Target = MagnetismTarget.Get();
	ACharacter* TargetChar = Cast<ACharacter>(Target);
	if (!TargetChar)
	{
		return;
	}

	// Skip tracking if target NPC is in knockback state
	if (AShooterNPC* TargetNPC = Cast<AShooterNPC>(Target))
	{
		if (TargetNPC->IsInKnockback())
		{
			return;
		}
	}

	// ==================== Full XY+Z Homing (TF2-style) ====================
	// Recalculate the lunge end position each frame so the player tracks the target
	// horizontally AND vertically. The actual velocity push happens in UpdateLunge().
	FVector TargetPos = Target->GetActorLocation();
	FVector PlayerPos = OwnerCharacter->GetActorLocation();

	FVector DirectionFromTarget = (PlayerPos - TargetPos);
	DirectionFromTarget.Z = 0.0f;
	if (!DirectionFromTarget.Normalize())
	{
		// Player is directly above/below target — fall back to forward axis
		DirectionFromTarget = -GetTraceDirection();
		DirectionFromTarget.Z = 0.0f;
		DirectionFromTarget.Normalize();
	}

	LungeTargetPosition = TargetPos + DirectionFromTarget * Settings.LungeStopDistance;

	if (bEnableDebugVisualization)
	{
		DrawDebugDirectionalArrow(GetWorld(), PlayerPos, TargetPos, 50.0f, FColor::Green, false, 0.0f, 0, 4.0f);
		DrawDebugSphere(GetWorld(), TargetPos, 30.0f, 8, FColor::Green, false, 0.0f);
		DrawDebugSphere(GetWorld(), LungeTargetPosition, 20.0f, 8, FColor::Yellow, false, 0.0f);
	}

	// ==================== Soft Aim Assist ====================
	// Gently steer camera yaw toward the target. Pitch untouched so vertical aim is the player's.
	if (Settings.bSoftAimAssistDuringLunge && OwnerController && Settings.SoftAimAssistStrength > 0.0f)
	{
		FVector ToTarget = TargetPos - PlayerPos;
		ToTarget.Z = 0.0f;
		if (ToTarget.SizeSquared() > 100.0f)
		{
			FRotator CurrentRotation = OwnerController->GetControlRotation();
			FRotator TargetRotation = ToTarget.Rotation();

			const float InterpSpeed = Settings.SoftAimAssistStrength * 30.0f; // 0..30
			FRotator NewRotation = FMath::RInterpTo(CurrentRotation, TargetRotation, DeltaTime, InterpSpeed);
			NewRotation.Pitch = CurrentRotation.Pitch;
			NewRotation.Roll = CurrentRotation.Roll;
			OwnerController->SetControlRotation(NewRotation);
		}
	}
}

void UMeleeAttackComponent::StopMagnetism()
{
#if WITH_EDITOR
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Magenta,
			FString::Printf(TEXT("StopMagnetism called: bIsDropKick=%d, DropKickVel=%.0f"),
				bIsDropKick ? 1 : 0, DropKickVelocity.Size()));
	}
#endif

	// Move-collision is restored by UApexMovementComponent when the lunge's falling edge is
	// simulated, on both ends. @see UApexMovementComponent::EndMeleeLunge
	MagnetismTarget.Reset();

	// The dropkick's exit momentum is applied by UApexMovementComponent on the lunge's falling edge,
	// on both machines. It used to be done here, reading the player's input directly — which the
	// server sees as nothing for a remote pawn, so a client's dropkick would have ended in a dead
	// stop every time. The "was forward held" answer travels as a flag instead.
	// @see UApexMovementComponent::EndMeleeLunge

	// Reset drop kick state
	bIsDropKick = false;
	DropKickHeightDifference = 0.0f;
	DropKickTargetPosition = FVector::ZeroVector;
	DropKickVelocity = FVector::ZeroVector;

	// Gravity is not restored here either — it comes back with the lunge's falling edge, inside the
	// simulated move, on every machine. @see UApexMovementComponent::EndMeleeLunge
}

void UMeleeAttackComponent::ApplyCharacterImpulse(AActor* HitActor, const FVector& ImpulseDirection, float ImpulseStrength)
{
	if (!HitActor || !OwnerCharacter)
	{
		return;
	}

	// ==================== Distance-Based Knockback System ====================
	// Calculate knockback using center-to-center direction from player to target
	// This is more intuitive than camera direction for knockback physics

	// Get center-to-center direction
	FVector PlayerCenter = OwnerCharacter->GetActorLocation();
	FVector TargetCenter = HitActor->GetActorLocation();
	FVector KnockbackDirection = TargetCenter - PlayerCenter;

	// For dropkick, preserve vertical component (player is above target, so knockback goes down)
	// For normal melee, keep horizontal only for consistent ground behavior
	if (bIsDropKick)
	{
		// Dropkick: use full 3D direction but ensure some downward component
		KnockbackDirection.Normalize();
		// If somehow the direction is mostly upward, clamp it
		if (KnockbackDirection.Z > 0.3f)
		{
			KnockbackDirection.Z = 0.0f;
			KnockbackDirection.Normalize();
		}
	}
	else
	{
		// Normal melee: horizontal knockback only
		KnockbackDirection.Z = 0.0f;
		KnockbackDirection.Normalize();
	}

	// Calculate player speed toward target for distance calculation
	float PlayerSpeedTowardTarget = 0.0f;
	if (!OwnerVelocityAtAttackStart.IsNearlyZero())
	{
		PlayerSpeedTowardTarget = FMath::Max(0.0f, FVector::DotProduct(OwnerVelocityAtAttackStart, KnockbackDirection));
	}

	// Calculate total knockback distance
	// Distance = BaseDistance + (PlayerSpeed * DistancePerVelocity)
	float KnockbackDistance = Settings.BaseKnockbackDistance + (PlayerSpeedTowardTarget * Settings.KnockbackDistancePerVelocity);

	// Calculate duration proportional to distance
	float KnockbackDuration = Settings.KnockbackBaseDuration + (KnockbackDistance * Settings.KnockbackDurationPerDistance);

	// Get NPC multiplier if applicable
	float NPCMultiplier = 1.0f;
	if (AShooterNPC* NPC = Cast<AShooterNPC>(HitActor))
	{
		NPCMultiplier = NPC->GetKnockbackDistanceMultiplier();
	}

	// Apply NPC multiplier to distance (not duration - heavier enemies move same speed, just less distance)
	KnockbackDistance *= NPCMultiplier;

	// Upgrade-driven multiplier (e.g. Tractor Beam Lv2: bonus knockback on NPCs being pulled).
	// Applied AFTER NPCMultiplier so the divide-out below only undoes NPCMultiplier, not this one.
	if (AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(OwnerCharacter))
	{
		if (UUpgradeManagerComponent* UpgradeMgr = ShooterChar->GetUpgradeManager())
		{
			const float UpgradeKnockMult = UpgradeMgr->GetCombinedMeleeKnockbackDistanceMultiplier(HitActor);
			if (!FMath::IsNearlyEqual(UpgradeKnockMult, 1.0f))
			{
				KnockbackDistance *= UpgradeKnockMult;
			}
		}
	}

#if WITH_EDITOR
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Green,
			FString::Printf(TEXT("Melee Knockback: PlayerSpeed=%.0f, Distance=%.0f, Duration=%.2f, NPCMult=%.2f"),
				PlayerSpeedTowardTarget, KnockbackDistance, KnockbackDuration, NPCMultiplier));
	}
#endif

	// The NPC applies its own multiplier again, so divide out the one folded in above.
	const float DistanceToSend = Cast<AShooterNPC>(HitActor) ? (KnockbackDistance / NPCMultiplier) : KnockbackDistance;

	// ==================== Who is allowed to shove ====================
	// The numbers above could only be computed here: they need the swing's entry speed and this
	// character's upgrade multipliers, neither of which the other machine has. Applying them is a
	// different question, and the answer is never "whoever swung".
	if (OwnerCharacter->HasAuthority())
	{
		ApplyKnockbackOnAuthority(HitActor, KnockbackDirection, DistanceToSend, KnockbackDuration, PlayerCenter);
		return;
	}

	// A client. Send it up, and shove NOTHING locally.
	//
	// An earlier version did shove an NPC here too, for the instant read, on the theory that the
	// server's answer would replicate over the top the way a reported ionization does. That theory
	// does not survive contact with movement: a charge is a value that gets overwritten, a position
	// is a stream that keeps arriving. The local shove and the replicated one fought each other for
	// the length of the knockback and the enemy visibly stuttered. One round trip of delay is the
	// cheaper of the two, and it is what everything else here already pays.
	if (AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(OwnerCharacter))
	{
		ShooterChar->Server_ReportMeleeKnockback(HitActor, KnockbackDirection, DistanceToSend, KnockbackDuration);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s shoved something on a client but is not a ShooterCharacter - shove dropped"),
			*OwnerCharacter->GetName());
	}
}

void UMeleeAttackComponent::ApplyKnockbackOnAuthority(AActor* Target, const FVector& Direction,
	float Distance, float Duration, const FVector& AttackerLocation)
{
	if (!Target || Duration <= 0.0f)
	{
		return;
	}

	// An NPC's movement is the server's, so the server's shove is the whole story.
	if (AShooterNPC* NPC = Cast<AShooterNPC>(Target))
	{
		NPC->ApplyKnockback(Direction, Distance, Duration, AttackerLocation);
		return;
	}

	if (ACharacter* HitCharacter = Cast<ACharacter>(Target))
	{
		const FVector LaunchVelocity = Direction * (Distance / Duration);

		// Here, so the authority's copy moves and everyone watching sees it.
		HitCharacter->LaunchCharacter(LaunchVelocity, true, true);

		// Logged on BOTH ends on purpose. The pair of lines, and the gap between their timestamps,
		// is the only way to tell a shove that happened once at the same simulated moment from one
		// that happened twice half a round trip apart.
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s shoved on the authority at %.0f u/s (t=%.3f)"),
			*HitCharacter->GetName(), LaunchVelocity.Size(),
			HitCharacter->GetWorld() ? HitCharacter->GetWorld()->GetTimeSeconds() : 0.0f);

		// And on the machine that predicts this character, or the two will disagree about where it
		// went for as long as the shove lasts. That disagreement is what the stuttering was.
		if (AShooterCharacter* HitShooter = Cast<AShooterCharacter>(HitCharacter))
		{
			if (!HitShooter->IsLocallyControlled())
			{
				HitShooter->Client_ApplyKnockback(LaunchVelocity);
			}
		}
		return;
	}

	// Fallback to physics impulse for non-characters.
	if (UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(Target->GetRootComponent()))
	{
		if (RootPrimitive->IsSimulatingPhysics())
		{
			const float Mass = RootPrimitive->GetMass();
			RootPrimitive->AddImpulse(Direction * (Distance / Duration) * Mass);
		}
	}
}

float UMeleeAttackComponent::GetMaxReportedKnockbackDistance() const
{
	// The base shove, plus what the fastest plausible approach could add, plus headroom for the
	// upgrade multipliers that do not replicate (Tractor Beam and friends). Same spirit as the
	// damage ceiling: bound the absurd, never clip an honest hit.
	const float FromSpeed = Settings.KnockbackDistancePerVelocity * FMath::Max(Settings.LungeMaxSpeed, 3000.0f);
	return (Settings.BaseKnockbackDistance + FromSpeed) * FMath::Max(Settings.MaxReportedDamageMultiplier, 1.0f);
}

float UMeleeAttackComponent::CalculateMomentumDamage(AActor* HitActor) const
{
	if (Settings.MomentumDamagePerSpeed <= 0.0f || !HitActor)
	{
		return 0.0f;
	}

	// Get velocity component towards the target
	FVector ToTarget = (HitActor->GetActorLocation() - OwnerCharacter->GetActorLocation()).GetSafeNormal();
	float VelocityTowardsTarget = FVector::DotProduct(OwnerVelocityAtAttackStart, ToTarget);

	// Only positive velocity counts (moving towards target)
	if (VelocityTowardsTarget <= 0.0f)
	{
		return 0.0f;
	}

	// Calculate bonus damage (per 100 units of velocity)
	float BonusDamage = (VelocityTowardsTarget / 100.0f) * Settings.MomentumDamagePerSpeed;
	return FMath::Min(BonusDamage, Settings.MaxMomentumDamage);
}

float UMeleeAttackComponent::CalculateMomentumImpulseMultiplier() const
{
	if (Settings.MomentumImpulseMultiplier <= 0.0f)
	{
		return 1.0f;
	}

	float Speed = OwnerVelocityAtAttackStart.Size();
	return 1.0f + (Speed * Settings.MomentumImpulseMultiplier);
}

FVector UMeleeAttackComponent::GetImpactCenter() const
{
	// Impact center is at the end of attack range
	return GetTraceStart() + GetTraceDirection() * Settings.AttackRange;
}

// ==================== Mesh Transition Implementation ====================

EMeleeAttackType UMeleeAttackComponent::DetermineAttackType() const
{
	if (!OwnerCharacter)
	{
		return EMeleeAttackType::Ground;
	}

	// Check for ApexMovementComponent for slide detection
	if (APolarityCharacter* PolarityChar = Cast<APolarityCharacter>(OwnerCharacter))
	{
		if (UApexMovementComponent* ApexMovement = PolarityChar->GetApexMovement())
		{
			if (ApexMovement->IsSliding())
			{
				return EMeleeAttackType::Sliding;
			}
		}
	}

	// Check for airborne
	UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement();
	if (Movement && Movement->IsFalling())
	{
		return EMeleeAttackType::Airborne;
	}

	return EMeleeAttackType::Ground;
}

const FMeleeAnimationData* UMeleeAttackComponent::SelectWeightedAnimation(const TArray<FMeleeAnimationData>& Animations)
{
	if (Animations.Num() == 0)
	{
		return nullptr;
	}

	if (Animations.Num() == 1)
	{
		return &Animations[0];
	}

	// Calculate total weight
	float TotalWeight = 0.0f;
	for (const FMeleeAnimationData& Anim : Animations)
	{
		TotalWeight += FMath::Max(0.0f, Anim.Weight);
	}

	if (TotalWeight <= 0.0f)
	{
		// All weights are zero, pick first one
		return &Animations[0];
	}

	// Random value in range [0, TotalWeight)
	float RandomValue = FMath::FRand() * TotalWeight;

	// Find animation based on cumulative weight
	float CumulativeWeight = 0.0f;
	for (const FMeleeAnimationData& Anim : Animations)
	{
		CumulativeWeight += FMath::Max(0.0f, Anim.Weight);
		if (RandomValue < CumulativeWeight)
		{
			return &Anim;
		}
	}

	// Fallback (shouldn't happen)
	return &Animations.Last();
}

const FMeleeAnimationData& UMeleeAttackComponent::GetCurrentAnimationData() const
{
	if (SelectedAnimationData)
	{
		return *SelectedAnimationData;
	}
	return DefaultAnimationData;
}

void UMeleeAttackComponent::LowerWeapon()
{
	// Boss finisher approach: the hands go away now, and the finisher's own swing brings the weapon
	// back when it ends (@see EndSwing). Nothing is lowered any more -- the weapon simply leaves.
	bIsWeaponLowered = true;
	HideWeaponForSwing();
}

void UMeleeAttackComponent::HideWeaponForSwing()
{
	// The character owns the hands: taking the weapon away is its weapon phase, so that firing,
	// reloading, swapping and abilities are all shut off by the gates that already read it.
	if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(OwnerCharacter))
	{
		Shooter->StowWeaponForMelee();
	}
}

void UMeleeAttackComponent::DrawWeaponBack()
{
	if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(OwnerCharacter))
	{
		Shooter->DrawWeaponAfterMelee(DrawSpeedMultiplier);
	}
}

void UMeleeAttackComponent::EndSwing()
{
	// Already blending out if we got here naturally; stopping it again is harmless, clears
	// CurrentMeleeMontage, and keeps the third person montage in step as it always has.
	StopAttackAnimation();

	// A delegated drop kick never took the hands or showed the melee mesh: the melee weapon runs
	// its own animation, and has nothing to be given back.
	if (!bDelegatedDropKick)
	{
		SwitchToFirstPersonMesh();
		DrawWeaponBack();
	}

	bIsWeaponLowered = false;

	// The draw is cosmetic recovery.  A new input is still held behind CanAttack's normal cooldown,
	// while the authored readiness notify can explicitly open the next swing during this draw.
	const AShooterCharacter* Shooter = Cast<AShooterCharacter>(OwnerCharacter);
	if (!bDelegatedDropKick && Shooter && Shooter->GetWeaponSwitchPhase() == EWeaponSwitchPhase::Drawing)
	{
		SetState(EMeleeAttackState::ShowingWeapon);
		return;
	}

	// Skip cooldown if we didn't hit an enemy (allows spam-hitting props)
	SetState(bHitEnemyThisAttack ? EMeleeAttackState::Cooldown : EMeleeAttackState::Ready);
}

void UMeleeAttackComponent::SwitchToMeleeMesh()
{
	if (MeleeMesh)
	{
		// ==================== Attach to Camera ====================
		// This ensures perfect synchronization even at high speeds
		// The mesh moves with the camera as a child component

		UCameraComponent* Camera = nullptr;
		if (APolarityCharacter* PolarityChar = Cast<APolarityCharacter>(OwnerCharacter))
		{
			Camera = PolarityChar->GetFirstPersonCameraComponent();
		}

		if (Camera)
		{
			// Attach to camera with snap to target
			MeleeMesh->AttachToComponent(
				Camera,
				FAttachmentTransformRules::SnapToTargetNotIncludingScale
			);

			// Set relative transform (offset from camera)
			const FMeleeAnimationData& AnimData = GetCurrentAnimationData();
			MeleeMesh->SetRelativeLocation(AnimData.MeshLocationOffset);

			// Combine global and per-attack rotation offsets
			FRotator FinalRelativeRotation = MeleeMeshRotationOffset + AnimData.MeshRotationOffset;
			MeleeMesh->SetRelativeRotation(FinalRelativeRotation);
		}
		else
		{
			// Fallback: if no camera found, use world positioning (old method)
			UpdateMeleeMeshRotation();
		}

		MeleeMesh->SetVisibility(true);

		// Get per-attack hidden bones
		const FMeleeAnimationData& AnimData = GetCurrentAnimationData();
		CurrentlyHiddenBones = AnimData.HiddenBones;

		// Hide specified bones for this attack type
		for (const FName& BoneName : CurrentlyHiddenBones)
		{
			MeleeMesh->HideBoneByName(BoneName, EPhysBodyOp::PBO_None);
		}
	}
}

void UMeleeAttackComponent::SwitchToFirstPersonMesh()
{
	if (MeleeMesh)
	{
		// Detach from camera
		MeleeMesh->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);

		MeleeMesh->SetVisibility(false);

		// Unhide bones that were hidden for this attack
		for (const FName& BoneName : CurrentlyHiddenBones)
		{
			MeleeMesh->UnHideBoneByName(BoneName);
		}
		CurrentlyHiddenBones.Empty();
	}

	// The arms and the weapon are NOT shown here. They come back through the character's draw
	// (@see DrawWeaponBack), which is what plays the weapon's own draw animation.
}

void UMeleeAttackComponent::UpdateMeleeMeshRotation()
{
	// ==================== Camera Attachment Solution ====================
	// MeleeMesh is now attached directly to the camera component during melee
	// This means it automatically follows the camera perfectly, even at high speeds
	// No manual synchronization needed!

	// This function is kept as a stub for compatibility and potential future use
	// (e.g., dynamic offset adjustments, special effects, etc.)

	// Only process if mesh is active and not attached (fallback mode)
	if (!IsAttacking())
	{
		return;
	}

	if (!MeleeMesh || !OwnerController || !OwnerCharacter)
	{
		return;
	}

	// Check if mesh is attached to camera - if so, nothing to do
	if (MeleeMesh->GetAttachParent() != nullptr)
	{
		// Mesh is attached to camera, perfect sync is automatic
		return;
	}

	// Fallback: Manual positioning if not attached (shouldn't happen in normal flow)
	FVector CameraLocation;
	FRotator CameraRotation;
	OwnerController->GetPlayerViewPoint(CameraLocation, CameraRotation);

	const FMeleeAnimationData& AnimData = GetCurrentAnimationData();

	// Combine rotations
	FQuat CameraQuat = CameraRotation.Quaternion();
	FQuat GlobalOffsetQuat = MeleeMeshRotationOffset.Quaternion();
	FQuat AttackOffsetQuat = AnimData.MeshRotationOffset.Quaternion();
	FQuat FinalQuat = CameraQuat * GlobalOffsetQuat * AttackOffsetQuat;

	// Calculate location
	FVector LocalOffset = AnimData.MeshLocationOffset;
	FVector WorldOffset = CameraRotation.RotateVector(LocalOffset);
	FVector FinalLocation = CameraLocation + WorldOffset;

	// Apply transform
	MeleeMesh->SetWorldLocationAndRotation(FinalLocation, FinalQuat.Rotator());
}

void UMeleeAttackComponent::PlaySwingCameraShake()
{
	if (!OwnerController)
	{
		return;
	}

	const FMeleeAnimationData& AnimData = GetCurrentAnimationData();

	if (AnimData.SwingCameraShake)
	{
		OwnerController->ClientStartCameraShake(AnimData.SwingCameraShake, AnimData.SwingShakeScale);
	}
}

void UMeleeAttackComponent::UpdateMontagePlayRate(float DeltaTime)
{
	if (!CurrentMeleeMontage || !MeleeMesh)
	{
		return;
	}

	UAnimInstance* AnimInstance = MeleeMesh->GetAnimInstance();
	if (!AnimInstance || !AnimInstance->Montage_IsPlaying(CurrentMeleeMontage))
	{
		return;
	}

	const FMeleeAnimationData& AnimData = GetCurrentAnimationData();
	if (!AnimData.PlayRateCurve || MontageTotalDuration <= 0.0f)
	{
		return;
	}

	// Update elapsed time
	MontageTimeElapsed += DeltaTime;

	// Calculate normalized progress (0-1)
	float NormalizedProgress = FMath::Clamp(MontageTimeElapsed / MontageTotalDuration, 0.0f, 1.0f);

	// Final play rate = BasePlayRate * PlayRateCurve(progress) * ComboSpeedMultiplier
	//   BasePlayRate         — static per-animation tuning
	//   PlayRateCurve        — per-frame modulation in 0..1 normalized time (Y=1.0 = no change)
	//   ComboSpeedMultiplier — global combo-driven multiplier (1.0 outside Combo upgrade)
	float CurveValue = AnimData.PlayRateCurve->GetFloatValue(NormalizedProgress);
	float NewPlayRate = AnimData.BasePlayRate * CurveValue * ComboSpeedMultiplier;

	// Apply new play rate
	AnimInstance->Montage_SetPlayRate(CurrentMeleeMontage, NewPlayRate);
}

void UMeleeAttackComponent::OnMeleeMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	UE_LOG(LogTemp, Warning, TEXT("[AIRBORNE_DEBUG] OnMeleeMontageEnded: montage='%s' bInterrupted=%d state=%d type=%d elapsed=%.3fs / total=%.3fs (%.0f%%) isCurrent=%d"),
		Montage ? *Montage->GetName() : TEXT("NULL"),
		bInterrupted ? 1 : 0,
		(int32)CurrentState,
		(int32)CurrentAttackType,
		MontageTimeElapsed,
		MontageTotalDuration,
		MontageTotalDuration > 0.0f ? (MontageTimeElapsed / MontageTotalDuration) * 100.0f : -1.0f,
		Montage == CurrentMeleeMontage ? 1 : 0);

	if (Montage != CurrentMeleeMontage)
	{
		return;
	}

	// Normally the swing already ended when this montage started blending out, and EndSwing has
	// cleared CurrentMeleeMontage, so this is not reached. It is the safety net for a montage that
	// ended without ever blending out.
	if (!bInterrupted && IsAttacking())
	{
		OnMeleeMontageBlendingOut(Montage, false);
		return;
	}

	CurrentMeleeMontage = nullptr;
}

void UMeleeAttackComponent::OnMeleeMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	// Interrupted means someone else stopped it (a drop kick cutting in, a cancel, the charged
	// punch taking the mesh over), and that someone owns what happens next.
	if (bInterrupted || Montage != CurrentMeleeMontage || !IsAttacking())
	{
		return;
	}

	// A damage window still open at the end of the animation closes here, and a swing whose montage
	// never carried the notify at all still gets its miss handling: both go through Recovery, which
	// is where that cleanup lives.
	if (CurrentState != EMeleeAttackState::Recovery)
	{
		SetState(EMeleeAttackState::Recovery);
	}

	EndSwing();
}

void UMeleeAttackComponent::AutoDetectMeshReferences()
{
	if (!OwnerCharacter)
	{
		return;
	}

	// Try to get FirstPersonMesh from PolarityCharacter
	if (!FirstPersonMesh)
	{
		if (APolarityCharacter* PolarityChar = Cast<APolarityCharacter>(OwnerCharacter))
		{
			FirstPersonMesh = PolarityChar->GetFirstPersonMesh();
		}
	}

	// If still not found, try to find by component name or tag
	if (!FirstPersonMesh)
	{
		TArray<USkeletalMeshComponent*> SkeletalMeshes;
		OwnerCharacter->GetComponents<USkeletalMeshComponent>(SkeletalMeshes);

		for (USkeletalMeshComponent* Mesh : SkeletalMeshes)
		{
			// Skip the main character mesh (third person)
			if (Mesh != OwnerCharacter->GetMesh())
			{
				// Check for "FirstPerson" in component name
				if (Mesh->GetName().Contains(TEXT("FirstPerson")))
				{
					FirstPersonMesh = Mesh;
					break;
				}
			}
		}
	}

	// MeleeMesh should be set manually in Blueprint or through component reference
	// We can try to find it by tag
	if (!MeleeMesh)
	{
		TArray<USkeletalMeshComponent*> SkeletalMeshes;
		OwnerCharacter->GetComponents<USkeletalMeshComponent>(SkeletalMeshes);

		for (USkeletalMeshComponent* Mesh : SkeletalMeshes)
		{
			if (Mesh->ComponentHasTag(TEXT("MeleeMesh")))
			{
				MeleeMesh = Mesh;
				break;
			}
		}
	}
}

// ==================== Camera Focus Implementation ====================

void UMeleeAttackComponent::StartCameraFocus(AActor* Target)
{
	if (!bEnableCameraFocusOnLunge || !Target || !OwnerController || !OwnerCharacter)
	{
		return;
	}

	CameraFocusTarget = Target;

	// Use LungeDuration for camera focus - camera and movement interpolate together
	CameraFocusDuration = Settings.LungeDuration;
	CameraFocusTimeRemaining = CameraFocusDuration;

	// Store current camera rotation
	CameraFocusStartRotation = OwnerController->GetControlRotation();

	// Calculate target rotation (look at target)
	FVector ToTarget = Target->GetActorLocation() - OwnerCharacter->GetActorLocation();
	CameraFocusTargetRotation = ToTarget.Rotation();

	// Preserve roll (usually zero for FPS)
	CameraFocusTargetRotation.Roll = CameraFocusStartRotation.Roll;

#if WITH_EDITOR
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Cyan,
			FString::Printf(TEXT("Camera Focus Started on %s (Duration: %.2fs)"),
				*Target->GetName(), CameraFocusDuration));
	}
#endif
}

void UMeleeAttackComponent::UpdateCameraFocus(float DeltaTime)
{
	if (CameraFocusTimeRemaining <= 0.0f || !CameraFocusTarget.IsValid() || !OwnerController)
	{
		return;
	}

	CameraFocusTimeRemaining -= DeltaTime;

	// ==================== Smooth Tracking Camera Focus ====================
	// Continuously update target rotation to current enemy position (tracking)
	// But use smooth interpolation instead of instant snap
	if (AActor* Target = CameraFocusTarget.Get())
	{
		FVector ToTarget = Target->GetActorLocation() - OwnerCharacter->GetActorLocation();
		CameraFocusTargetRotation = ToTarget.Rotation();
		CameraFocusTargetRotation.Roll = OwnerController->GetControlRotation().Roll;
	}

	// Get current camera rotation
	FRotator CurrentRotation = OwnerController->GetControlRotation();

	// Smooth interpolation to the (updating) target rotation
	// InterpSpeed controls how fast camera follows - higher = snappier
	float InterpSpeed = CameraFocusStrength * 10.0f; // CameraFocusStrength as base multiplier
	FRotator NewRotation = FMath::RInterpTo(
		CurrentRotation,
		CameraFocusTargetRotation,
		DeltaTime,
		InterpSpeed
	);

	// Apply rotation to controller
	OwnerController->SetControlRotation(NewRotation);

	// Stop focus when time runs out
	if (CameraFocusTimeRemaining <= 0.0f)
	{
		StopCameraFocus();
	}
}

void UMeleeAttackComponent::StopCameraFocus()
{
	CameraFocusTarget.Reset();
	CameraFocusTimeRemaining = 0.0f;
}

// ==================== Cool Kick Implementation ====================

void UMeleeAttackComponent::StartCoolKick()
{
	if (Settings.CoolKickDuration <= 0.0f || Settings.CoolKickSpeedBoost <= 0.0f)
	{
		return;
	}

	if (!OwnerCharacter)
	{
		return;
	}

	CoolKickTimeRemaining = Settings.CoolKickDuration;

	// Get current movement direction for the boost
	if (UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement())
	{
		FVector Velocity = Movement->Velocity;
		Velocity.Z = 0.0f; // Only horizontal direction

		if (Velocity.SizeSquared() > FMath::Square(50.0f))
		{
			CoolKickDirection = Velocity.GetSafeNormal();
		}
		else
		{
			// If not moving, use view direction
			CoolKickDirection = GetTraceDirection();
			CoolKickDirection.Z = 0.0f;
			CoolKickDirection.Normalize();
		}
	}

#if WITH_EDITOR
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Orange,
			FString::Printf(TEXT("Cool Kick Started! Duration=%.2fs, Boost=%.0f cm/s"),
				Settings.CoolKickDuration, Settings.CoolKickSpeedBoost));
	}
#endif
}

void UMeleeAttackComponent::UpdateCoolKick(float DeltaTime)
{
	if (CoolKickTimeRemaining <= 0.0f)
	{
		return;
	}

	if (!OwnerCharacter)
	{
		CoolKickTimeRemaining = 0.0f;
		return;
	}

	UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement();
	if (!Movement)
	{
		CoolKickTimeRemaining = 0.0f;
		return;
	}

	// Calculate how much boost to add this frame
	// Total boost is CoolKickSpeedBoost, distributed over CoolKickDuration
	float BoostPerSecond = Settings.CoolKickSpeedBoost / Settings.CoolKickDuration;
	float BoostThisFrame = BoostPerSecond * DeltaTime;

	// Apply boost in movement direction
	FVector BoostVelocity = CoolKickDirection * BoostThisFrame;
	Movement->Velocity += BoostVelocity;

	CoolKickTimeRemaining -= DeltaTime;

#if WITH_EDITOR
	if (GEngine && bEnableDebugVisualization)
	{
		GEngine->AddOnScreenDebugMessage(-1, 0.0f, FColor::Orange,
			FString::Printf(TEXT("Cool Kick: %.2fs remaining, Speed=%.0f"),
				CoolKickTimeRemaining, Movement->Velocity.Size()));
	}
#endif
}

// ==================== Animation Notify API ====================

void UMeleeAttackComponent::ActivateDamageWindowFromNotify()
{
	UE_LOG(LogTemp, Warning, TEXT("[AIRBORNE_DEBUG] ActivateDamageWindowFromNotify: state=%d type=%d elapsed=%.3fs / total=%.3fs (%.0f%%) externallyDisabled=%d"),
		(int32)CurrentState,
		(int32)CurrentAttackType,
		MontageTimeElapsed,
		MontageTotalDuration,
		MontageTotalDuration > 0.0f ? (MontageTimeElapsed / MontageTotalDuration) * 100.0f : -1.0f,
		bExternallyDisabled ? 1 : 0);

	// Don't activate if externally disabled (melee weapon is handling this)
	if (bExternallyDisabled)
	{
		return;
	}

	// Force transition to Active state (damage window)
	if (CurrentState != EMeleeAttackState::Active)
	{
		SetState(EMeleeAttackState::Active);
	}
}

void UMeleeAttackComponent::DeactivateDamageWindowFromNotify()
{
	UE_LOG(LogTemp, Warning, TEXT("[AIRBORNE_DEBUG] DeactivateDamageWindowFromNotify: state=%d type=%d elapsed=%.3fs / total=%.3fs (%.0f%%) externallyDisabled=%d"),
		(int32)CurrentState,
		(int32)CurrentAttackType,
		MontageTimeElapsed,
		MontageTotalDuration,
		MontageTotalDuration > 0.0f ? (MontageTimeElapsed / MontageTotalDuration) * 100.0f : -1.0f,
		bExternallyDisabled ? 1 : 0);

	// Don't process if externally disabled (melee weapon is handling this)
	if (bExternallyDisabled)
	{
		return;
	}

	// Force transition to Recovery state (end damage window)
	if (CurrentState == EMeleeAttackState::Active)
	{
		SetState(EMeleeAttackState::Recovery);
	}
}

void UMeleeAttackComponent::EndRecoveryFromNotify()
{
	// No-op — Recovery now passes through in a single tick (StateTimeRemaining = 0).
	// Kept declared in the header for source compatibility with any external caller.
}

void UMeleeAttackComponent::NotifyMeleeReadyFromNotify()
{
	if (CurrentState == EMeleeAttackState::Windup || CurrentState == EMeleeAttackState::Active ||
		CurrentState == EMeleeAttackState::Recovery)
	{
		bReadyForNextAttackFromNotify = true;
		bInputLocked = false;
		UE_LOG(LogTemp, Log, TEXT("[MELEE_DEBUG] %s: authored ready notify opened next-attack window"),
			*GetNameSafe(GetOwner()));
	}
}

// ==================== External / Charged Punch API ====================

void UMeleeAttackComponent::EnterMeleeMeshView()
{
	// Force-cancel any in-flight regular swing so the external upgrade owns the view.
	// CancelAttack only works before the damage window opens; past that, we just stop
	// the anim and switch state to Ready. The hands stay empty either way.
	if (CurrentState != EMeleeAttackState::Ready)
	{
		StopAttackAnimation();
		StopSwingTrailFX();
		StopMagnetism();
		StopCameraFocus();
		bInputLocked = false;
		SetState(EMeleeAttackState::Ready);
	}

	HideWeaponForSwing();
	SwitchToMeleeMesh();

	// Kill anything still playing on MeleeMesh (e.g. a ground swing montage that's
	// already past CancelAttack's allowed phases). Without this, the ground anim
	// keeps playing on top of the upgrade's air-attack montage.
	if (MeleeMesh)
	{
		if (UAnimInstance* AnimInst = MeleeMesh->GetAnimInstance())
		{
			AnimInst->StopAllMontages(0.0f);
		}
	}
	CurrentMeleeMontage = nullptr;
}

void UMeleeAttackComponent::ExitMeleeMeshView()
{
	SwitchToFirstPersonMesh();
	DrawWeaponBack();
}

void UMeleeAttackComponent::PlayMontageOnMeleeMesh(UAnimMontage* Montage, float PlayRate)
{
	if (!Montage)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_ANIM] PlayMontageOnMeleeMesh: Montage is NULL"));
		return;
	}
	if (!MeleeMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_ANIM] PlayMontageOnMeleeMesh: MeleeMesh is NULL on this component — was AutoDetectMeshReferences successful?"));
		return;
	}

	if (UAnimInstance* AnimInst = MeleeMesh->GetAnimInstance())
	{
		// Stop anything else currently playing on this mesh first so we don't blend
		// from the regular swing into the charged-punch swing on the same channel.
		AnimInst->StopAllMontages(0.0f);
		const float PlayedDuration = AnimInst->Montage_Play(Montage, PlayRate);
		CurrentMeleeMontage = Montage;
		MontageTimeElapsed = 0.0f;
		MontageTotalDuration = Montage->GetPlayLength();
		UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_ANIM] Montage_Play('%s', rate=%.2f) returned duration=%.2f, totalLen=%.2f, meshVisible=%d"),
			*Montage->GetName(), PlayRate, PlayedDuration, MontageTotalDuration,
			MeleeMesh->IsVisible() ? 1 : 0);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[CHARGED_PUNCH_ANIM] PlayMontageOnMeleeMesh: MeleeMesh has no AnimInstance (no AnimBP set?)"));
	}
}

void UMeleeAttackComponent::ApplyComboSpeedMultiplier(float NewMultiplier)
{
	// Clamp to a safe range (>=0.1 so we never divide by zero / freeze the state machine).
	const float Clamped = FMath::Max(0.1f, NewMultiplier);
	if (FMath::IsNearlyEqual(Clamped, ComboSpeedMultiplier))
	{
		return;
	}

	ComboSpeedMultiplier = Clamped;

	// If a montage is currently playing, recompute its play rate with the same
	// three-factor formula UpdateMontagePlayRate uses every tick:
	//   PlayRate = BasePlayRate * PlayRateCurve(progress) * ComboSpeedMultiplier
	// We don't want to skip the curve when reacting to a mid-swing combo change.
	if (CurrentMeleeMontage && MeleeMesh)
	{
		if (UAnimInstance* AnimInstance = MeleeMesh->GetAnimInstance())
		{
			const FMeleeAnimationData& AnimData = GetCurrentAnimationData();
			float PlayRate = AnimData.BasePlayRate * ComboSpeedMultiplier;
			if (AnimData.PlayRateCurve && MontageTotalDuration > 0.0f)
			{
				const float NormalizedProgress = FMath::Clamp(MontageTimeElapsed / MontageTotalDuration, 0.0f, 1.0f);
				PlayRate *= AnimData.PlayRateCurve->GetFloatValue(NormalizedProgress);
			}
			AnimInstance->Montage_SetPlayRate(CurrentMeleeMontage, PlayRate);
		}
	}
}

// ==================== Drop Kick Implementation ====================

bool UMeleeAttackComponent::ShouldPerformDropKick() const
{
	if (!bDropKickUnlocked)
	{
		return false;
	}

	if (!Settings.bEnableDropKick || !OwnerCharacter || !OwnerController)
	{
		UE_LOG(LogTemp, Warning, TEXT("DropKick: Settings disabled or no owner (bEnable=%d)"), Settings.bEnableDropKick);
		return false;
	}

	// Check drop kick cooldown
	if (DropKickCooldownRemaining > 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] ShouldDropKick: FALSE - cooldown remaining: %.1f"), DropKickCooldownRemaining);
		return false;
	}

	// Must be airborne
	UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement();
	if (!Movement)
	{
		UE_LOG(LogTemp, Warning, TEXT("DropKick: No movement component"));
		return false;
	}

	bool bIsOnGround = Movement->IsMovingOnGround();
	bool bIsFalling = Movement->IsFalling();

	UE_LOG(LogTemp, Warning, TEXT("DropKick: IsOnGround=%d, IsFalling=%d, MovementMode=%d"),
		bIsOnGround, bIsFalling, (int32)Movement->MovementMode);

	if (bIsOnGround)
	{
		return false;
	}

	// Check camera pitch (looking down)
	FRotator CameraRotation = OwnerController->GetControlRotation();
	// Normalize pitch to -180 to 180 range
	float NormalizedPitch = FRotator::NormalizeAxis(CameraRotation.Pitch);
	// In UE, negative pitch = looking down, positive = looking up
	// We want positive value when looking down for comparison with threshold
	float LookDownAngle = -NormalizedPitch;

	UE_LOG(LogTemp, Warning, TEXT("DropKick: RawPitch=%.1f, Normalized=%.1f, LookDown=%.1f, Threshold=%.1f, Pass=%d"),
		CameraRotation.Pitch, NormalizedPitch, LookDownAngle, Settings.DropKickPitchThreshold, LookDownAngle >= Settings.DropKickPitchThreshold);

	return LookDownAngle >= Settings.DropKickPitchThreshold;
}

bool UMeleeAttackComponent::TryStartDropKick()
{
	if (!bDropKickUnlocked || !OwnerCharacter || !OwnerController)
	{
		return false;
	}

	const FVector Start = OwnerCharacter->GetActorLocation();
	const FVector CameraForward = OwnerController->GetControlRotation().Vector();
	const float ConeHalfAngleRad = FMath::DegreesToRadians(Settings.DropKickConeAngle);

	// Calculate cone so that FAR EDGE of base touches the floor, not center
	// First, trace to find floor distance
	FHitResult FloorHit;
	FCollisionQueryParams FloorQueryParams;
	FloorQueryParams.AddIgnoredActor(OwnerCharacter);

	// Trace straight down to find floor
	float FloorZ = Start.Z - 5000.0f; // Default fallback
	if (GetWorld()->LineTraceSingleByChannel(FloorHit, Start, Start - FVector(0, 0, 5000.0f), ECC_WorldStatic, FloorQueryParams))
	{
		FloorZ = FloorHit.Location.Z;
	}

	// Calculate where the camera forward ray hits floor level
	float HeightAboveFloor = Start.Z - FloorZ;

	// Adjust cone length so the FAR edge (upper/back part of base circle) touches the floor
	// The far edge from player is the point on the base circle that is BEHIND the cone center
	// relative to the look direction - this is the "upper" point when looking down
	//
	// Far edge Z = ConeCenter.Z + Radius * CosPitch (+ because it's above the center)
	// We want: Start.Z + CameraForward.Z * Length + Radius * CosPitch = FloorZ
	// Radius = Length * tan(ConeAngle)
	// So: Start.Z + CameraForward.Z * Length + Length * tan(ConeAngle) * CosPitch = FloorZ
	// Length * (CameraForward.Z + tan(ConeAngle) * CosPitch) = FloorZ - Start.Z
	// Length = (FloorZ - Start.Z) / (CameraForward.Z + tan(ConeAngle) * CosPitch)

	float ConeLengthToFloor;
	if (CameraForward.Z < -0.1f) // Looking down
	{
		float SinPitch = -CameraForward.Z; // How much we're looking down (0-1)
		float CosPitch = FMath::Sqrt(1.0f - SinPitch * SinPitch);

		float TanCone = FMath::Tan(ConeHalfAngleRad);
		float Denominator = CameraForward.Z + TanCone * CosPitch; // + for far edge (upper point)

		if (FMath::Abs(Denominator) > 0.01f)
		{
			ConeLengthToFloor = (FloorZ - Start.Z) / Denominator;
			ConeLengthToFloor = FMath::Clamp(ConeLengthToFloor, 100.0f, Settings.DropKickMaxRange);
		}
		else
		{
			ConeLengthToFloor = Settings.DropKickMaxRange;
		}
	}
	else
	{
		ConeLengthToFloor = Settings.DropKickMaxRange;
	}

	const float ConeLength = ConeLengthToFloor;
	const float ConeRadius = ConeLength * FMath::Tan(ConeHalfAngleRad);
	const FVector ConeEnd = Start + CameraForward * ConeLength;

	// Use OverlapMulti for more reliable detection instead of sweep
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwnerCharacter);

	TArray<FOverlapResult> OverlapResults;
	// Search in a large sphere that encompasses the entire cone
	float SearchRadius = FMath::Max(ConeLength, ConeRadius) * 1.2f;
	FVector SearchCenter = Start + CameraForward * (ConeLength * 0.5f);

	GetWorld()->OverlapMultiByChannel(
		OverlapResults,
		SearchCenter,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(SearchRadius),
		QueryParams
	);

	// Debug: Draw the adjusted cone
	if (bEnableDebugVisualization)
	{
		const int32 NumSegments = 16;

		// Get perpendicular vectors for cone circle
		FVector Right = FVector::CrossProduct(CameraForward, FVector::UpVector).GetSafeNormal();
		if (Right.IsNearlyZero())
		{
			Right = FVector::CrossProduct(CameraForward, FVector::RightVector).GetSafeNormal();
		}
		FVector Up = FVector::CrossProduct(Right, CameraForward);

		// Draw cone outline
		for (int32 i = 0; i < NumSegments; ++i)
		{
			float Angle1 = (float)i / NumSegments * 2.0f * PI;
			float Angle2 = (float)(i + 1) / NumSegments * 2.0f * PI;

			FVector Point1 = ConeEnd + (Right * FMath::Cos(Angle1) + Up * FMath::Sin(Angle1)) * ConeRadius;
			FVector Point2 = ConeEnd + (Right * FMath::Cos(Angle2) + Up * FMath::Sin(Angle2)) * ConeRadius;

			// Circle at cone base
			DrawDebugLine(GetWorld(), Point1, Point2, FColor::Yellow, false, DebugShapeDuration, 0, 2.0f);
			// Lines from apex to base
			if (i % 4 == 0)
			{
				DrawDebugLine(GetWorld(), Start, Point1, FColor::Yellow, false, DebugShapeDuration, 0, 1.5f);
			}
		}

		// Center line
		DrawDebugLine(GetWorld(), Start, ConeEnd, FColor::Orange, false, DebugShapeDuration, 0, 3.0f);

		// Floor reference
		DrawDebugLine(GetWorld(), FVector(Start.X, Start.Y, FloorZ), FVector(ConeEnd.X, ConeEnd.Y, FloorZ), FColor::White, false, DebugShapeDuration, 0, 1.0f);
	}

	AActor* BestTarget = nullptr;
	float BestDistanceToLookRay = FLT_MAX; // Closest to camera look ray wins
	FVector BestTargetPos = FVector::ZeroVector;

	UE_LOG(LogTemp, Warning, TEXT("DropKick TryStart: NumOverlaps=%d, ConeLength=%.1f, ConeRadius=%.1f"),
		OverlapResults.Num(), ConeLength, ConeRadius);

	for (const FOverlapResult& Overlap : OverlapResults)
	{
		AActor* HitActor = Overlap.GetActor();
		if (!HitActor || HitActor == OwnerCharacter)
		{
			continue;
		}

		// Must be a character OR a valid dummy target (like ShooterKey)
		ACharacter* HitCharacter = Cast<ACharacter>(HitActor);
		bool bIsDummyTarget = HitActor->Implements<UShooterDummyTarget>();
		if (!HitCharacter && !bIsDummyTarget)
		{
			continue;
		}

		FVector TargetPos = HitActor->GetActorLocation();
		FVector ToTarget = TargetPos - Start;
		float Distance = ToTarget.Size();

		if (Distance < KINDA_SMALL_NUMBER)
		{
			continue;
		}

		// Check if target is within cone angle
		FVector ToTargetNorm = ToTarget.GetSafeNormal();
		float DotProduct = FVector::DotProduct(CameraForward, ToTargetNorm);
		float AngleToTarget = FMath::Acos(FMath::Clamp(DotProduct, -1.0f, 1.0f));

		// Also check if target is within cone length (project onto camera forward)
		float DistanceAlongRay = FVector::DotProduct(ToTarget, CameraForward);
		if (DistanceAlongRay < 0 || DistanceAlongRay > ConeLength * 1.1f) // Small tolerance
		{
			if (bEnableDebugVisualization)
			{
				DrawDebugSphere(GetWorld(), TargetPos, 25.0f, 4, FColor::Blue, false, DebugShapeDuration); // Out of range
			}
			continue;
		}

		UE_LOG(LogTemp, Warning, TEXT("DropKick: %s Angle=%.1f deg, ConeAngle=%.1f deg, DistAlongRay=%.1f"),
			*HitActor->GetName(), FMath::RadiansToDegrees(AngleToTarget), Settings.DropKickConeAngle, DistanceAlongRay);

		if (AngleToTarget <= ConeHalfAngleRad)
		{
			// Check minimum height difference - player must be above target
			float HeightDiff = Start.Z - TargetPos.Z;
			if (HeightDiff < Settings.DropKickMinHeightDifference)
			{
				UE_LOG(LogTemp, Warning, TEXT("DropKick: %s IN CONE but too low! HeightDiff=%.1f < Min=%.1f"),
					*HitActor->GetName(), HeightDiff, Settings.DropKickMinHeightDifference);
				if (bEnableDebugVisualization)
				{
					DrawDebugSphere(GetWorld(), TargetPos, 35.0f, 4, FColor::Yellow, false, DebugShapeDuration); // Too low
				}
				continue;
			}

			// Calculate perpendicular distance to camera look ray (closest point on ray to target)
			// This is what we minimize to find target closest to crosshair
			FVector ClosestPointOnRay = Start + CameraForward * DistanceAlongRay;
			float DistanceToRay = FVector::Dist(TargetPos, ClosestPointOnRay);

			UE_LOG(LogTemp, Warning, TEXT("DropKick: %s IN CONE! HeightDiff=%.1f, DistToRay=%.1f (best=%.1f)"),
				*HitActor->GetName(), HeightDiff, DistanceToRay, BestDistanceToLookRay);

			if (DistanceToRay < BestDistanceToLookRay)
			{
				BestDistanceToLookRay = DistanceToRay;
				BestTarget = HitActor;
				BestTargetPos = TargetPos;
			}

			// Debug: mark valid targets
			if (bEnableDebugVisualization)
			{
				DrawDebugSphere(GetWorld(), TargetPos, 50.0f, 8, FColor::Green, false, DebugShapeDuration);
			}
		}
		else if (bEnableDebugVisualization)
		{
			// Debug: mark invalid targets (outside cone angle)
			DrawDebugSphere(GetWorld(), TargetPos, 30.0f, 4, FColor::Red, false, DebugShapeDuration);
		}
	}

	if (BestTarget)
	{
		// Start drop kick!
		bIsDropKick = true;

		MagnetismTarget = BestTarget;
		DropKickTargetPosition = BestTargetPos;

		// Calculate height difference for bonus damage
		DropKickHeightDifference = Start.Z - BestTargetPos.Z;
		if (DropKickHeightDifference < 0.0f)
		{
			DropKickHeightDifference = 0.0f; // No bonus if target is above us
		}

		// Calculate lunge target position
		FVector DirectionFromTarget = (Start - BestTargetPos);
		DirectionFromTarget.Z = 0.0f;
		DirectionFromTarget.Normalize();

		float StopDistance = Settings.LungeStopDistance;
		LungeTargetPosition = BestTargetPos + DirectionFromTarget * StopDistance;
		LungeTargetPosition.Z = BestTargetPos.Z;

		// Update OwnerVelocityAtAttackStart with dropkick dive velocity
		// This ensures knockback calculations and HP regen use the dive speed, not the cached velocity from attack start
		FVector DiveDirection = (BestTargetPos - Start).GetSafeNormal();
		OwnerVelocityAtAttackStart = DiveDirection * Settings.DropKickDiveSpeed;

		// Start camera focus
		StartCameraFocus(BestTarget);

#if WITH_EDITOR
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
				FString::Printf(TEXT("DropKick Velocity Set: Speed=%.0f, Dir=(%.2f,%.2f,%.2f)"),
					OwnerVelocityAtAttackStart.Size(), DiveDirection.X, DiveDirection.Y, DiveDirection.Z));
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Yellow,
				FString::Printf(TEXT("DROP KICK! Target: %s, Height Diff: %.0f cm, Bonus Damage: %.0f"),
					*BestTarget->GetName(), DropKickHeightDifference, CalculateDropKickBonusDamage()));
		}
#endif

		// Debug: draw line to target
		if (bEnableDebugVisualization)
		{
			DrawDebugLine(GetWorld(), Start, BestTargetPos, FColor::Yellow, false, DebugShapeDuration, 0, 5.0f);
			DrawDebugSphere(GetWorld(), LungeTargetPosition, 30.0f, 8, FColor::Cyan, false, DebugShapeDuration);
		}

		return true;
	}

	return false;
}

// The dive is decided here and integrated in UApexMovementComponent, the same way the lunge is.
// Cool kick is the one velocity writer still living in this tick, left alone by the author's
// decision; UpdateCoolKick is where it is.
void UMeleeAttackComponent::UpdateDropKick(float DeltaTime)
{
	if (!bIsDropKick || !MagnetismTarget.IsValid() || !OwnerCharacter)
	{
		return;
	}

	AActor* Target = MagnetismTarget.Get();
	FVector CurrentPos = OwnerCharacter->GetActorLocation();
	FVector TargetPos = Target->GetActorLocation();

	// If target NPC is in knockback AND we already hit them, stop tracking to prevent jitter.
	// But if they were already stunned before our dropkick, keep diving toward them.
	if (AShooterNPC* TargetNPC = Cast<AShooterNPC>(Target))
	{
		if (TargetNPC->IsInKnockback() && bHasHitThisAttack)
		{
			// Stop tracking. The stop itself happens inside the simulated move, when this stops
			// publishing a dive. @see UApexMovementComponent::UpdateMeleeLunge
			return;
		}
	}

	// Update lunge target position to track target
	FVector DirectionFromTarget = (CurrentPos - TargetPos);
	DirectionFromTarget.Z = 0.0f;
	if (!DirectionFromTarget.IsNearlyZero())
	{
		DirectionFromTarget.Normalize();
	}
	else
	{
		DirectionFromTarget = -OwnerController->GetControlRotation().Vector();
		DirectionFromTarget.Z = 0.0f;
		DirectionFromTarget.Normalize();
	}

	float StopDistance = Settings.LungeStopDistance;
	LungeTargetPosition = TargetPos + DirectionFromTarget * StopDistance;
	LungeTargetPosition.Z = TargetPos.Z;

	// Check if we've reached target
	float DistanceToTarget = FVector::Dist(CurrentPos, LungeTargetPosition);
	if (DistanceToTarget < 50.0f)
	{
		// Reached target - stop drop kick movement
		return;
	}

	// Move toward target at drop kick speed
	FVector MoveDirection = (LungeTargetPosition - CurrentPos).GetSafeNormal();
	float MoveDistance = Settings.DropKickDiveSpeed * DeltaTime;
	MoveDistance = FMath::Min(MoveDistance, DistanceToTarget);

	FVector NewPos = CurrentPos + MoveDirection * MoveDistance;

	// The dive itself is integrated by UApexMovementComponent inside the simulated move; this
	// function only decides WHERE it is going. Writing Velocity from here is what made the dropkick
	// work for the host and get corrected away for everybody else.
	DropKickVelocity = MoveDirection * Settings.DropKickDiveSpeed;

	// Debug visualization
	if (bEnableDebugVisualization)
	{
		DrawDebugLine(GetWorld(), CurrentPos, LungeTargetPosition, FColor::Yellow, false, 0.0f, 0, 3.0f);
	}
}

float UMeleeAttackComponent::CalculateDropKickBonusDamage() const
{
	if (!bIsDropKick || DropKickHeightDifference <= 0.0f)
	{
		return 0.0f;
	}

	// Bonus damage per 100cm of height
	float BonusDamage = (DropKickHeightDifference / 100.0f) * Settings.DropKickDamagePerHeight;

	// Clamp to max
	return FMath::Min(BonusDamage, Settings.DropKickMaxBonusDamage);
}

bool UMeleeAttackComponent::HasDropKickTarget() const
{
	UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] HasDropKickTarget: checking..."));
	if (!OwnerCharacter || !OwnerController)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] HasDropKickTarget: FALSE - no owner/controller"));
		return false;
	}

	const FVector Start = OwnerCharacter->GetActorLocation();
	const FVector CameraForward = OwnerController->GetControlRotation().Vector();
	const float ConeHalfAngleRad = FMath::DegreesToRadians(Settings.DropKickConeAngle);

	// Calculate cone length to floor (same logic as TryStartDropKick)
	FHitResult FloorHit;
	FCollisionQueryParams FloorQueryParams;
	FloorQueryParams.AddIgnoredActor(OwnerCharacter);

	float FloorZ = Start.Z - 5000.0f;
	if (GetWorld()->LineTraceSingleByChannel(FloorHit, Start, Start - FVector(0, 0, 5000.0f), ECC_WorldStatic, FloorQueryParams))
	{
		FloorZ = FloorHit.Location.Z;
	}

	float ConeLengthToFloor;
	if (CameraForward.Z < -0.1f)
	{
		float SinPitch = -CameraForward.Z;
		float CosPitch = FMath::Sqrt(1.0f - SinPitch * SinPitch);
		float TanCone = FMath::Tan(ConeHalfAngleRad);
		float Denominator = CameraForward.Z + TanCone * CosPitch;

		if (FMath::Abs(Denominator) > 0.01f)
		{
			ConeLengthToFloor = (FloorZ - Start.Z) / Denominator;
			ConeLengthToFloor = FMath::Clamp(ConeLengthToFloor, 100.0f, Settings.DropKickMaxRange);
		}
		else
		{
			ConeLengthToFloor = Settings.DropKickMaxRange;
		}
	}
	else
	{
		ConeLengthToFloor = Settings.DropKickMaxRange;
	}

	const float ConeLength = ConeLengthToFloor;

	// Overlap search
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwnerCharacter);

	TArray<FOverlapResult> OverlapResults;
	float SearchRadius = FMath::Max(ConeLength, ConeLength * FMath::Tan(ConeHalfAngleRad)) * 1.2f;
	FVector SearchCenter = Start + CameraForward * (ConeLength * 0.5f);

	GetWorld()->OverlapMultiByChannel(
		OverlapResults,
		SearchCenter,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(SearchRadius),
		QueryParams
	);

	for (const FOverlapResult& Overlap : OverlapResults)
	{
		AActor* HitActor = Overlap.GetActor();
		if (!HitActor || HitActor == OwnerCharacter)
		{
			continue;
		}

		ACharacter* HitCharacter = Cast<ACharacter>(HitActor);
		bool bIsDummyTarget = HitActor->Implements<UShooterDummyTarget>();
		if (!HitCharacter && !bIsDummyTarget)
		{
			continue;
		}

		FVector TargetPos = HitActor->GetActorLocation();
		FVector ToTarget = TargetPos - Start;
		float Distance = ToTarget.Size();

		if (Distance < KINDA_SMALL_NUMBER)
		{
			continue;
		}

		// Check cone angle
		FVector ToTargetNorm = ToTarget.GetSafeNormal();
		float DotProduct = FVector::DotProduct(CameraForward, ToTargetNorm);
		float AngleToTarget = FMath::Acos(FMath::Clamp(DotProduct, -1.0f, 1.0f));

		// Check cone length
		float DistanceAlongRay = FVector::DotProduct(ToTarget, CameraForward);
		if (DistanceAlongRay < 0 || DistanceAlongRay > ConeLength * 1.1f)
		{
			continue;
		}

		if (AngleToTarget <= ConeHalfAngleRad)
		{
			// Check minimum height difference
			float HeightDiff = Start.Z - TargetPos.Z;
			UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] HasDropKickTarget: %s in cone (angle=%.1f), HeightDiff=%.1f (need>=%.1f)"),
				*HitActor->GetName(), FMath::RadiansToDegrees(AngleToTarget), HeightDiff, Settings.DropKickMinHeightDifference);
			if (HeightDiff >= Settings.DropKickMinHeightDifference)
			{
				UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] HasDropKickTarget: TRUE - found %s"), *HitActor->GetName());
				return true; // Found a valid target
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] HasDropKickTarget: %s OUTSIDE cone (angle=%.1f > max=%.1f)"),
				*HitActor->GetName(), FMath::RadiansToDegrees(AngleToTarget), Settings.DropKickConeAngle);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] HasDropKickTarget: FALSE - no valid targets found (checked %d overlaps, ConeLen=%.0f, SearchRadius=%.0f)"),
		OverlapResults.Num(), ConeLength, SearchRadius);
	return false;
}

float UMeleeAttackComponent::GetDropKickCooldownProgress() const
{
	if (DropKickCooldownRemaining <= 0.0f)
	{
		return 1.0f; // Ready
	}

	if (Settings.DropKickCooldown <= 0.0f)
	{
		return 1.0f;
	}

	return 1.0f - (DropKickCooldownRemaining / Settings.DropKickCooldown);
}

// ==================== Tag-Based Damage ====================

float UMeleeAttackComponent::GetTagDamageMultiplier(AActor* Target) const
{
	if (!Target || TagDamageMultipliers.Num() == 0)
	{
		return 1.0f;
	}

	float Multiplier = 1.0f;
	for (const auto& Pair : TagDamageMultipliers)
	{
		if (Target->ActorHasTag(Pair.Key))
		{
			Multiplier *= Pair.Value;
		}
	}
	return Multiplier;
}
