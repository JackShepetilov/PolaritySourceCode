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
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "DrawDebugHelpers.h"
#include "Engine/DamageEvents.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "ApexMovementComponent.h"
#include "PolarityCharacter.h"
#include "ShooterCharacter.h"
#include "ShooterWeapon.h"

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

	// Auto-detect mesh references
	AutoDetectMeshReferences();

	// Store base transforms for FirstPersonMesh
	if (FirstPersonMesh)
	{
		FirstPersonMeshBaseLocation = FirstPersonMesh->GetRelativeLocation();
		FirstPersonMeshBaseRotation = FirstPersonMesh->GetRelativeRotation();
	}

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
	UpdateMeshTransition(DeltaTime);
	UpdateMeleeMeshRotation();
	UpdateMontagePlayRate(DeltaTime);
	UpdateCameraFocus(DeltaTime);
}

bool UMeleeAttackComponent::StartAttack()
{
	if (!CanAttack())
	{
		return false;
	}

	// Lock input immediately to prevent spam
	bInputLocked = true;

	// Reset attack state
	bHasHitThisAttack = false;
	HitActorsThisAttack.Empty();
	MeshTransitionProgress = 0.0f;
	MontageTimeElapsed = 0.0f;

	// Determine attack type based on movement state
	CurrentAttackType = DetermineAttackType();

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

	// Start with mesh transition (hiding weapon)
	BeginHideWeapon();
	SetState(EMeleeAttackState::HidingWeapon);

	return true;
}

bool UMeleeAttackComponent::CancelAttack()
{
	// Can only cancel during early phases
	if (CurrentState != EMeleeAttackState::HidingWeapon &&
		CurrentState != EMeleeAttackState::InputDelay &&
		CurrentState != EMeleeAttackState::Windup)
	{
		return false;
	}

	StopAttackAnimation();
	SwitchToFirstPersonMesh();
	bInputLocked = false;
	SetState(EMeleeAttackState::Ready);

	return true;
}

bool UMeleeAttackComponent::CanAttack() const
{
	// Must be ready and input not locked
	if (CurrentState != EMeleeAttackState::Ready || bInputLocked)
	{
		return false;
	}

	// Must have valid owner
	if (!OwnerCharacter)
	{
		return false;
	}

	// Don't attack if charge animation is playing
	if (UChargeAnimationComponent* ChargeAnim = OwnerCharacter->FindComponentByClass<UChargeAnimationComponent>())
	{
		if (ChargeAnim->IsAnimating())
		{
			return false;
		}
	}

	// Check airborne restriction
	UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement();
	if (Movement)
	{
		if (!Settings.bCanAttackInAir && Movement->IsFalling())
		{
			return false;
		}

		// Note: Sliding check would require ApexMovementComponent
		// For now, we allow it and let the character class handle restrictions
	}

	return true;
}

bool UMeleeAttackComponent::IsAttacking() const
{
	return CurrentState == EMeleeAttackState::HidingWeapon ||
		CurrentState == EMeleeAttackState::InputDelay ||
		CurrentState == EMeleeAttackState::Windup ||
		CurrentState == EMeleeAttackState::Active ||
		CurrentState == EMeleeAttackState::Recovery ||
		CurrentState == EMeleeAttackState::ShowingWeapon;
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
	CurrentState = NewState;

	switch (NewState)
	{
	case EMeleeAttackState::Ready:
		StateTimeRemaining = 0.0f;
		bInputLocked = false;
		break;

	case EMeleeAttackState::HidingWeapon:
		StateTimeRemaining = Settings.HideWeaponTime;
		MeshTransitionProgress = 0.0f;
		break;

	case EMeleeAttackState::InputDelay:
		StateTimeRemaining = Settings.InputDelayTime;
		break;

	case EMeleeAttackState::Windup:
		StateTimeRemaining = Settings.WindupTime;
		break;

	case EMeleeAttackState::Active:
		StateTimeRemaining = Settings.ActiveTime;
		SpawnSwingTrailFX();
		break;

	case EMeleeAttackState::Recovery:
		StateTimeRemaining = Settings.RecoveryTime;
		StopSwingTrailFX();
		StopMagnetism();
		if (!bHasHitThisAttack)
		{
			PlaySound(MissSound);

			// ==================== Titanfall 2: Preserve Momentum on Miss ====================
			// If player missed, restore their original momentum so they keep flying
			// This is crucial for the Titanfall 2 feel - missing shouldn't punish your movement
			if (Settings.bPreserveMomentum && OwnerCharacter)
			{
				if (UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement())
				{
					// Restore the velocity we had when we started the attack
					FVector RestoredVelocity = OwnerVelocityAtAttackStart * Settings.MomentumPreservationRatio;

					// Keep current Z velocity if we're falling (don't fight gravity)
					if (Movement->IsFalling())
					{
						RestoredVelocity.Z = Movement->Velocity.Z;
					}

					Movement->Velocity = RestoredVelocity;

#if WITH_EDITOR
					if (GEngine)
					{
						GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Yellow,
							FString::Printf(TEXT("Titanfall Melee Miss: Restored velocity %.0f"),
								RestoredVelocity.Size()));
					}
#endif
				}
			}
		}
		break;

	case EMeleeAttackState::ShowingWeapon:
		StateTimeRemaining = Settings.ShowWeaponTime;
		MeshTransitionProgress = 0.0f;
		StopAttackAnimation();
		SwitchToFirstPersonMesh();
		break;

	case EMeleeAttackState::Cooldown:
		StateTimeRemaining = Settings.Cooldown;
		OnMeleeAttackEnded.Broadcast();
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
		case EMeleeAttackState::HidingWeapon:
			// Mesh transition complete - switch meshes and start attack
			SwitchToMeleeMesh();
			StartMagnetism();
			PlayAttackAnimation();
			PlaySwingCameraShake();
			PlaySound(SwingSound);
			OnMeleeAttackStarted.Broadcast();

			if (Settings.InputDelayTime > 0.0f)
			{
				SetState(EMeleeAttackState::InputDelay);
			}
			else if (Settings.WindupTime > 0.0f)
			{
				SetState(EMeleeAttackState::Windup);
			}
			else
			{
				SetState(EMeleeAttackState::Active);
			}
			break;

		case EMeleeAttackState::InputDelay:
			if (Settings.WindupTime > 0.0f)
			{
				SetState(EMeleeAttackState::Windup);
			}
			else
			{
				SetState(EMeleeAttackState::Active);
			}
			break;

		case EMeleeAttackState::Windup:
			SetState(EMeleeAttackState::Active);
			break;

		case EMeleeAttackState::Active:
			SetState(EMeleeAttackState::Recovery);
			break;

		case EMeleeAttackState::Recovery:
			SetState(EMeleeAttackState::ShowingWeapon);
			break;

		case EMeleeAttackState::ShowingWeapon:
			SetState(EMeleeAttackState::Cooldown);
			break;

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

	return false;
}

void UMeleeAttackComponent::PerformHitDetection()
{
	if (!OwnerCharacter)
	{
		return;
	}

	const FVector Start = GetTraceStart();
	const FVector End = GetTraceEnd();

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
	bool bHit = GetWorld()->SweepMultiByChannel(
		HitResults,
		Start,
		End,
		FQuat::Identity,
		ECC_Pawn, // Or use a custom trace channel
		FCollisionShape::MakeSphere(Settings.AttackRadius),
		QueryParams
	);

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

			bHasHitThisAttack = true;

			// Check for headshot
			bool bHeadshot = IsHeadshot(Hit);

			// Apply damage and get final damage value
			float FinalDamage = ApplyDamage(HitActor, Hit);

			// Play effects
			PlaySound(HitSound);
			PlayCameraShake();
			SpawnImpactFX(Hit.ImpactPoint, Hit.ImpactNormal);

			// Broadcast hit event with actual damage dealt
			OnMeleeHit.Broadcast(HitActor, Hit.ImpactPoint, bHeadshot, FinalDamage);

			// Debug visualization for hit impact
			if (bEnableDebugVisualization)
			{
				FColor HitColor = bHeadshot ? FColor::Red : FColor::White;
				DrawDebugSphere(GetWorld(), Hit.ImpactPoint, 15.0f, 12, HitColor, false, DebugShapeDuration);
				DrawDebugString(GetWorld(), Hit.ImpactPoint + FVector(0, 0, 30),
					bHeadshot ? TEXT("HEADSHOT!") : TEXT("HIT"), nullptr, HitColor, DebugShapeDuration);
			}
		}
	}
}

float UMeleeAttackComponent::ApplyDamage(AActor* HitActor, const FHitResult& HitResult)
{
	if (!HitActor || !OwnerCharacter)
	{
		return 0.0f;
	}

	// Calculate base damage
	float FinalDamage = Settings.BaseDamage;

	// Apply headshot multiplier
	if (IsHeadshot(HitResult))
	{
		FinalDamage *= Settings.HeadshotMultiplier;
	}

	// Apply momentum bonus damage
	FinalDamage += CalculateMomentumDamage(HitActor);

	// Apply drop kick bonus damage
	FinalDamage += CalculateDropKickBonusDamage();

	// Create damage event
	FPointDamageEvent DamageEvent(
		FinalDamage,
		HitResult,
		GetTraceDirection(),
		Settings.DamageType
	);

	// Apply damage
	HitActor->TakeDamage(
		FinalDamage,
		DamageEvent,
		OwnerCharacter->GetController(),
		OwnerCharacter
	);

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
	// Only lunge during active phase
	if (CurrentState != EMeleeAttackState::Active && CurrentState != EMeleeAttackState::Windup)
	{
		return;
	}

	if (!OwnerCharacter)
	{
		return;
	}

	UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	// ==================== Titanfall 2 Momentum System ====================
	// Key principle: NEVER kill the player's momentum during melee
	// This allows high-speed gameplay where you can punch while flying at 2000+ units/sec

	if (Settings.bPreserveMomentum)
	{
		// Titanfall 2 style: Preserve original velocity, optionally add lunge boost

		// Start with the velocity we had when we started the attack
		// IMPORTANT: Only preserve XY velocity, let Z be affected by gravity naturally
		FVector PreservedVelocity = OwnerVelocityAtAttackStart * Settings.MomentumPreservationRatio;
		PreservedVelocity.Z = Movement->Velocity.Z; // Keep current Z velocity (gravity applied)

		// If we have a magnetism target and lunge-to-target is enabled, move toward them
		if (Settings.bLungeToTarget && MagnetismTarget.IsValid())
		{
			// ==================== Lunge to Pre-Calculated Target Position ====================
			// LungeTargetPosition was validated in StartMagnetism() via SweepSphere
			FVector PlayerPos = OwnerCharacter->GetActorLocation();
			FVector ToLungeTarget = LungeTargetPosition - PlayerPos;
			float DistToLungeTarget = ToLungeTarget.Size();

			// Only lunge if we're moving fast enough (prevents weak lunges when stationary)
			float CurrentSpeed = OwnerVelocityAtAttackStart.Size();
			if (CurrentSpeed >= Settings.MinSpeedForLungeToTarget && DistToLungeTarget > 10.0f)
			{
				// Interpolate position using LungeDuration
				// Calculate what percentage of the lunge we've completed
				float LungeAlpha = FMath::Clamp(LungeProgress, 0.0f, 1.0f);

				// Calculate velocity needed to reach target position
				// Use simple linear interpolation based on time remaining
				float TimeRemaining = Settings.LungeDuration * (1.0f - LungeAlpha);
				if (TimeRemaining > 0.01f)
				{
					// Velocity = Distance / Time
					PreservedVelocity = ToLungeTarget / TimeRemaining;

					// Clamp total velocity to prevent excessive speeds
					float MaxSpeed = 3000.0f;
					if (PreservedVelocity.Size() > MaxSpeed)
					{
						PreservedVelocity = PreservedVelocity.GetSafeNormal() * MaxSpeed;
					}
				}
				else
				{
					// Almost there - stop moving
					PreservedVelocity = FVector::ZeroVector;
				}

#if WITH_EDITOR
				if (GEngine && bEnableDebugVisualization)
				{
					GEngine->AddOnScreenDebugMessage(-1, 0.0f, FColor::Cyan,
						FString::Printf(TEXT("Lunge: Dist=%.0f, Speed=%.0f, Progress=%.2f"),
							DistToLungeTarget, PreservedVelocity.Size(), LungeAlpha));
				}
#endif
			}
		}
		else if (Settings.LungeDistance > 0.0f && Settings.LungeDuration > 0.0f)
		{
			// No magnetism target - apply standard lunge in movement direction
			// But still PRESERVE momentum, just ADD lunge on top
			float LungeSpeed = Settings.LungeDistance / Settings.LungeDuration;
			FVector LungeBoost = LungeDirection * LungeSpeed;
			LungeBoost.Z = 0.0f;

			// Add lunge boost to preserved velocity
			PreservedVelocity.X += LungeBoost.X;
			PreservedVelocity.Y += LungeBoost.Y;
		}

		// Apply the final velocity
		Movement->Velocity = PreservedVelocity;
	}
	else
	{
		// Legacy behavior: Override velocity with lunge (kills momentum)
		if (Settings.LungeDistance <= 0.0f || Settings.LungeDuration <= 0.0f)
		{
			return;
		}

		float LungeSpeed = Settings.LungeDistance / Settings.LungeDuration;
		FVector LungeVelocity = LungeDirection * LungeSpeed;
		LungeVelocity.Z = 0.0f;

		FVector CurrentVelocity = Movement->Velocity;
		CurrentVelocity.X = LungeVelocity.X;
		CurrentVelocity.Y = LungeVelocity.Y;

		Movement->Velocity = CurrentVelocity;
	}

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

			float PlayRate = AnimData.BasePlayRate;

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

void UMeleeAttackComponent::StartMagnetism()
{
	if (!Settings.bEnableTargetMagnetism || !OwnerCharacter)
	{
		return;
	}

	MagnetismTarget.Reset();
	bIsDropKick = false;
	DropKickHeightDifference = 0.0f;

	// Check for drop kick first (airborne + looking down)
	if (ShouldPerformDropKick() && TryStartDropKick())
	{
		// Drop kick started successfully - skip normal magnetism
		return;
	}

	const FVector Start = GetTraceStart();
	const FVector End = Start + GetTraceDirection() * Settings.MagnetismRange;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(OwnerCharacter);

	TArray<FHitResult> HitResults;
	bool bHit = GetWorld()->SweepMultiByChannel(
		HitResults,
		Start,
		End,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(Settings.MagnetismRadius),
		QueryParams
	);

	// Debug visualization for magnetism trace
	if (bEnableDebugVisualization)
	{
		FColor MagnetismColor = bHit ? FColor::Magenta : FColor::Orange;
		DrawDebugCapsule(
			GetWorld(),
			(Start + End) * 0.5f,
			FVector::Dist(Start, End) * 0.5f,
			Settings.MagnetismRadius,
			FQuat::FindBetweenNormals(FVector::UpVector, (End - Start).GetSafeNormal()),
			MagnetismColor,
			false,
			DebugShapeDuration
		);
		DrawDebugSphere(GetWorld(), Start, Settings.MagnetismRadius, 8, FColor::Cyan, false, DebugShapeDuration);
		DrawDebugSphere(GetWorld(), End, Settings.MagnetismRadius, 8, FColor::Purple, false, DebugShapeDuration);
		DrawDebugLine(GetWorld(), Start, End, MagnetismColor, false, DebugShapeDuration, 0, 3.0f);
	}

	if (bHit)
	{
		// Find the closest valid target
		float ClosestDist = FLT_MAX;
		AActor* ClosestTarget = nullptr;

		for (const FHitResult& Hit : HitResults)
		{
			AActor* HitActor = Hit.GetActor();
			if (HitActor && HitActor != OwnerCharacter)
			{
				// Check if it's a character (enemy)
				if (Cast<ACharacter>(HitActor))
				{
					float Dist = FVector::DistSquared(Start, Hit.ImpactPoint);
					if (Dist < ClosestDist)
					{
						ClosestDist = Dist;
						ClosestTarget = HitActor;
					}
				}
			}
		}

		if (ClosestTarget && Settings.bLungeToTarget)
		{
			// ==================== Calculate Lunge Target Position ====================
			// Target position is on the line player->enemy at distance (AttackRange - Buffer) from enemy
			FVector PlayerPos = OwnerCharacter->GetActorLocation();
			FVector TargetPos = ClosestTarget->GetActorLocation();
			FVector ToTarget = TargetPos - PlayerPos;
			float DistanceToTarget = ToTarget.Size();

			// Calculate ideal stop position
			float StopDistance = Settings.AttackRange - Settings.LungeStopDistanceBuffer;
			FVector DirectionFromTarget = (PlayerPos - TargetPos).GetSafeNormal();
			FVector IdealLungePos = TargetPos + DirectionFromTarget * StopDistance;

			// ==================== Path Validation via SweepSphere ====================
			// Sweep a sphere from player to ideal position to check for obstacles
			FHitResult SweepHit;
			FCollisionQueryParams SweepParams;
			SweepParams.AddIgnoredActor(OwnerCharacter);
			SweepParams.AddIgnoredActor(ClosestTarget); // Ignore target itself

			bool bPathBlocked = GetWorld()->SweepSingleByChannel(
				SweepHit,
				PlayerPos,
				IdealLungePos,
				FQuat::Identity,
				ECC_Visibility, // Check for walls/geometry
				FCollisionShape::MakeSphere(Settings.LungeStopDistanceBuffer),
				SweepParams
			);

			// Debug visualization for path validation
			if (bEnableDebugVisualization)
			{
				FColor PathColor = bPathBlocked ? FColor::Red : FColor::Green;
				DrawDebugSphere(GetWorld(), IdealLungePos, Settings.LungeStopDistanceBuffer, 12, PathColor, false, DebugShapeDuration);
				DrawDebugLine(GetWorld(), PlayerPos, IdealLungePos, PathColor, false, DebugShapeDuration, 0, 2.0f);
			}

			// If path is clear, establish lock-on
			if (!bPathBlocked)
			{
				MagnetismTarget = ClosestTarget;
				LungeTargetPosition = IdealLungePos;

				// Start camera focus when lunge target is found
				StartCameraFocus(ClosestTarget);

				// Disable gravity and EMF during lock-on for smooth Z-alignment
				if (UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement())
				{
					Movement->GravityScale = 0.0f;
				}

				if (APolarityCharacter* PolarityChar = Cast<APolarityCharacter>(OwnerCharacter))
				{
					// TODO: Add EMF disable call here when EMF component is accessible
					// PolarityChar->DisableEMF();
				}

#if WITH_EDITOR
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Green,
						FString::Printf(TEXT("Lock-On: Distance=%.0f, StopAt=%.0f from target"),
							DistanceToTarget, StopDistance));
				}
#endif
			}
			else
			{
				// Path blocked - cancel lock-on
#if WITH_EDITOR
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Red,
						TEXT("Lock-On FAILED: Path blocked"));
				}
#endif
			}
		}
		else if (ClosestTarget)
		{
			// No lunge-to-target - simple magnetism without path validation
			MagnetismTarget = ClosestTarget;
			StartCameraFocus(ClosestTarget);
		}
	}
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

	if (!Settings.bEnableTargetMagnetism || !MagnetismTarget.IsValid())
	{
		return;
	}

	AActor* Target = MagnetismTarget.Get();
	ACharacter* TargetChar = Cast<ACharacter>(Target);
	if (!TargetChar)
	{
		return;
	}

	// Skip magnetism if target NPC is in knockback state
	if (AShooterNPC* TargetNPC = Cast<AShooterNPC>(Target))
	{
		if (TargetNPC->IsInKnockback())
		{
			return;
		}
	}

	// ==================== Titanfall 2 Magnetism ====================
	// In Titanfall 2, the PLAYER moves toward the target, not vice versa
	// This creates the satisfying "magnetic kick" where you fly toward enemies

	if (Settings.bLungeToTarget)
	{
		// Titanfall 2 style: Player lunges toward target
		// The actual velocity is applied in UpdateLunge() - this function just maintains
		// the magnetism target and can do additional target tracking/rotation

		// ==================== Dynamic Z-Alignment ====================
		// Update Z-component of LungeTargetPosition each frame to match target's current height
		// This prevents player from floating in air if target moves vertically
		if (OwnerCharacter)
		{
			FVector TargetPos = Target->GetActorLocation();
			FVector PlayerPos = OwnerCharacter->GetActorLocation();

			// Recalculate lunge position with updated target Z
			FVector DirectionFromTarget = (PlayerPos - TargetPos);
			DirectionFromTarget.Z = 0.0f; // Keep horizontal direction
			DirectionFromTarget.Normalize();

			float StopDistance = Settings.AttackRange - Settings.LungeStopDistanceBuffer;
			FVector NewLungePos = TargetPos + DirectionFromTarget * StopDistance;

			// Update only Z to match target's current height (keep XY from original path calculation)
			LungeTargetPosition.Z = NewLungePos.Z;
		}

		// Debug visualization for lunge direction
		if (bEnableDebugVisualization && OwnerCharacter)
		{
			FVector PlayerPos = OwnerCharacter->GetActorLocation();
			FVector TargetPos = Target->GetActorLocation();
			DrawDebugDirectionalArrow(
				GetWorld(),
				PlayerPos,
				TargetPos,
				50.0f,
				FColor::Green,
				false,
				0.0f,  // Single frame
				0,
				4.0f
			);
			DrawDebugSphere(GetWorld(), TargetPos, 30.0f, 8, FColor::Green, false, 0.0f);
			// Visualize lunge target position (where player will stop)
			DrawDebugSphere(GetWorld(), LungeTargetPosition, 20.0f, 8, FColor::Yellow, false, 0.0f);
		}

		// Optional: Rotate player to face target for better kick feel
		if (OwnerCharacter && OwnerController)
		{
			FVector ToTarget = Target->GetActorLocation() - OwnerCharacter->GetActorLocation();
			ToTarget.Z = 0.0f;

			if (ToTarget.SizeSquared() > 100.0f)
			{
				// Smoothly rotate view toward target (subtle aim assist)
				FRotator TargetRotation = ToTarget.Rotation();
				FRotator CurrentRotation = OwnerController->GetControlRotation();

				// Very subtle rotation assist - don't override player's aim too much
				float RotationAssistStrength = 0.1f; // 10% blend per frame
				FRotator NewRotation = FMath::RInterpTo(CurrentRotation, TargetRotation, DeltaTime, RotationAssistStrength * 60.0f);

				// Only adjust yaw, keep pitch and roll as player set them
				NewRotation.Pitch = CurrentRotation.Pitch;
				NewRotation.Roll = CurrentRotation.Roll;

				// Uncomment below line if you want auto-aim assist during melee:
				// OwnerController->SetControlRotation(NewRotation);
			}
		}
	}
	else
	{
		// Legacy behavior: Pull enemy toward player's attack center

		// Get impact center and target position
		FVector ImpactCenter = GetImpactCenter();
		FVector TargetPos = Target->GetActorLocation();

		// Calculate direction to pull
		FVector PullDirection = (ImpactCenter - TargetPos);
		PullDirection.Z = 0.0f; // Keep horizontal only
		float DistToCenter = PullDirection.Size();

		// Stop if close enough
		if (DistToCenter < 10.0f)
		{
			return;
		}

		PullDirection.Normalize();

		// Calculate pull amount this frame
		float PullAmount = FMath::Min(Settings.MagnetismPullSpeed * DeltaTime, DistToCenter);

		// Apply movement
		FVector NewLocation = TargetPos + PullDirection * PullAmount;
		Target->SetActorLocation(NewLocation, true);
	}
}

void UMeleeAttackComponent::StopMagnetism()
{
	MagnetismTarget.Reset();

	// Reset drop kick state
	bIsDropKick = false;
	DropKickHeightDifference = 0.0f;
	DropKickTargetPosition = FVector::ZeroVector;

	// Restore gravity and EMF after lock-on ends (after Recovery phase)
	if (OwnerCharacter)
	{
		if (UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement())
		{
			Movement->GravityScale = 1.0f;
		}

		// Re-enable EMF if available
		if (APolarityCharacter* PolarityChar = Cast<APolarityCharacter>(OwnerCharacter))
		{
			// TODO: Add EMF enable call here when EMF component is accessible
			// PolarityChar->EnableEMF();
		}
	}
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

	// Get center-to-center direction (horizontal only for consistent behavior)
	FVector PlayerCenter = OwnerCharacter->GetActorLocation();
	FVector TargetCenter = HitActor->GetActorLocation();
	FVector KnockbackDirection = TargetCenter - PlayerCenter;
	KnockbackDirection.Z = 0.0f; // Horizontal knockback only
	KnockbackDirection.Normalize();

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

#if WITH_EDITOR
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Green,
			FString::Printf(TEXT("Melee Knockback: PlayerSpeed=%.0f, Distance=%.0f, Duration=%.2f, NPCMult=%.2f"),
				PlayerSpeedTowardTarget, KnockbackDistance, KnockbackDuration, NPCMultiplier));
	}
#endif

	// Try ShooterNPC first (has distance-based ApplyKnockback)
	if (AShooterNPC* NPC = Cast<AShooterNPC>(HitActor))
	{
		// Note: NPC multiplier already applied to distance, so we pass it directly
		// The NPC will apply its own multiplier again, so we need to divide it out
		float DistanceForNPC = KnockbackDistance / NPCMultiplier;
		NPC->ApplyKnockback(KnockbackDirection, DistanceForNPC, KnockbackDuration, PlayerCenter);
		return;
	}

	// For generic characters, convert to velocity-based launch
	if (ACharacter* HitCharacter = Cast<ACharacter>(HitActor))
	{
		// Convert distance/duration to velocity
		FVector KnockbackVelocity = KnockbackDirection * (KnockbackDistance / KnockbackDuration);
		HitCharacter->LaunchCharacter(KnockbackVelocity, true, true);
		return;
	}

	// Fallback to physics impulse for non-characters
	if (UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(HitActor->GetRootComponent()))
	{
		if (RootPrimitive->IsSimulatingPhysics())
		{
			// Convert to impulse (mass * velocity)
			float Mass = RootPrimitive->GetMass();
			FVector Impulse = KnockbackDirection * (KnockbackDistance / KnockbackDuration) * Mass;
			RootPrimitive->AddImpulse(Impulse);
		}
	}
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

const FMeleeAnimationData& UMeleeAttackComponent::GetCurrentAnimationData() const
{
	switch (CurrentAttackType)
	{
	case EMeleeAttackType::Airborne:
		return AirborneAttack;
	case EMeleeAttackType::Sliding:
		return SlidingAttack;
	case EMeleeAttackType::Ground:
	default:
		return GroundAttack;
	}
}

void UMeleeAttackComponent::BeginHideWeapon()
{
	MeshTransitionProgress = 0.0f;

	// Store current FirstPersonMesh transform
	if (FirstPersonMesh)
	{
		FirstPersonMeshBaseLocation = FirstPersonMesh->GetRelativeLocation();
		FirstPersonMeshBaseRotation = FirstPersonMesh->GetRelativeRotation();
	}
}

void UMeleeAttackComponent::UpdateMeshTransition(float DeltaTime)
{
	if (CurrentState == EMeleeAttackState::HidingWeapon)
	{
		if (Settings.HideWeaponTime > 0.0f)
		{
			MeshTransitionProgress += DeltaTime / Settings.HideWeaponTime;
			MeshTransitionProgress = FMath::Clamp(MeshTransitionProgress, 0.0f, 1.0f);

			// Interpolate FirstPersonMesh down
			if (FirstPersonMesh)
			{
				float Alpha = FMath::InterpEaseIn(0.0f, 1.0f, MeshTransitionProgress, 2.0f);
				FVector TargetLocation = FirstPersonMeshBaseLocation - FVector(0.0f, 0.0f, 100.0f); // Move down
				FVector NewLocation = FMath::Lerp(FirstPersonMeshBaseLocation, TargetLocation, Alpha);
				FirstPersonMesh->SetRelativeLocation(NewLocation);
			}
		}
	}
	else if (CurrentState == EMeleeAttackState::ShowingWeapon)
	{
		if (Settings.ShowWeaponTime > 0.0f)
		{
			MeshTransitionProgress += DeltaTime / Settings.ShowWeaponTime;
			MeshTransitionProgress = FMath::Clamp(MeshTransitionProgress, 0.0f, 1.0f);

			// Interpolate FirstPersonMesh back up
			if (FirstPersonMesh)
			{
				float Alpha = FMath::InterpEaseOut(0.0f, 1.0f, MeshTransitionProgress, 2.0f);
				FVector CurrentLocation = FirstPersonMeshBaseLocation - FVector(0.0f, 0.0f, 100.0f);
				FVector NewLocation = FMath::Lerp(CurrentLocation, FirstPersonMeshBaseLocation, Alpha);
				FirstPersonMesh->SetRelativeLocation(NewLocation);
			}
		}
	}
}

void UMeleeAttackComponent::SwitchToMeleeMesh()
{
	if (FirstPersonMesh)
	{
		FirstPersonMesh->SetVisibility(false);
	}

	// Hide current weapon
	if (AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(OwnerCharacter))
	{
		if (AShooterWeapon* Weapon = ShooterChar->GetCurrentWeapon())
		{
			Weapon->SetActorHiddenInGame(true);
		}
	}

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

	if (FirstPersonMesh)
	{
		FirstPersonMesh->SetVisibility(true);
		// Location will be interpolated back in UpdateMeshTransition
	}

	// Show current weapon
	if (AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(OwnerCharacter))
	{
		if (AShooterWeapon* Weapon = ShooterChar->GetCurrentWeapon())
		{
			Weapon->SetActorHiddenInGame(false);
		}
	}
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
	if (CurrentState != EMeleeAttackState::InputDelay &&
		CurrentState != EMeleeAttackState::Windup &&
		CurrentState != EMeleeAttackState::Active &&
		CurrentState != EMeleeAttackState::Recovery)
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

	// Sample the curve
	float CurveValue = AnimData.PlayRateCurve->GetFloatValue(NormalizedProgress);
	float NewPlayRate = AnimData.BasePlayRate * CurveValue;

	// Apply new play rate
	AnimInstance->Montage_SetPlayRate(CurrentMeleeMontage, NewPlayRate);
}

void UMeleeAttackComponent::OnMeleeMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage == CurrentMeleeMontage)
	{
		CurrentMeleeMontage = nullptr;

		// If montage ended naturally during Active or Recovery, transition to next state
		if (!bInterrupted && (CurrentState == EMeleeAttackState::Active || CurrentState == EMeleeAttackState::Recovery))
		{
			// Let the state machine handle the transition
		}
	}
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
	// Force transition to Active state (damage window)
	if (CurrentState != EMeleeAttackState::Active)
	{
		SetState(EMeleeAttackState::Active);
	}
}

void UMeleeAttackComponent::DeactivateDamageWindowFromNotify()
{
	// Force transition to Recovery state (end damage window)
	if (CurrentState == EMeleeAttackState::Active)
	{
		SetState(EMeleeAttackState::Recovery);
	}
}

// ==================== Drop Kick Implementation ====================

bool UMeleeAttackComponent::ShouldPerformDropKick() const
{
	if (!Settings.bEnableDropKick || !OwnerCharacter || !OwnerController)
	{
		UE_LOG(LogTemp, Warning, TEXT("DropKick: Settings disabled or no owner (bEnable=%d)"), Settings.bEnableDropKick);
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
	if (!OwnerCharacter || !OwnerController)
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

		// Must be a character
		ACharacter* HitCharacter = Cast<ACharacter>(HitActor);
		if (!HitCharacter)
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
			// Calculate perpendicular distance to camera look ray (closest point on ray to target)
			// This is what we minimize to find target closest to crosshair
			FVector ClosestPointOnRay = Start + CameraForward * DistanceAlongRay;
			float DistanceToRay = FVector::Dist(TargetPos, ClosestPointOnRay);

			UE_LOG(LogTemp, Warning, TEXT("DropKick: %s IN CONE! DistToRay=%.1f (best=%.1f)"),
				*HitActor->GetName(), DistanceToRay, BestDistanceToLookRay);

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

		float StopDistance = Settings.AttackRange - Settings.LungeStopDistanceBuffer;
		LungeTargetPosition = BestTargetPos + DirectionFromTarget * StopDistance;
		LungeTargetPosition.Z = BestTargetPos.Z;

		// Start camera focus
		StartCameraFocus(BestTarget);

#if WITH_EDITOR
		if (GEngine)
		{
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

void UMeleeAttackComponent::UpdateDropKick(float DeltaTime)
{
	if (!bIsDropKick || !MagnetismTarget.IsValid() || !OwnerCharacter)
	{
		return;
	}

	AActor* Target = MagnetismTarget.Get();
	FVector CurrentPos = OwnerCharacter->GetActorLocation();
	FVector TargetPos = Target->GetActorLocation();

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

	float StopDistance = Settings.AttackRange - Settings.LungeStopDistanceBuffer;
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

	// Apply movement
	if (UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement())
	{
		// Set velocity to match movement
		Movement->Velocity = MoveDirection * Settings.DropKickDiveSpeed;
	}

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