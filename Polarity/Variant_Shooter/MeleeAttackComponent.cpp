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
	UpdateMeshTransition(DeltaTime);
	UpdateMeleeMeshRotation();
	UpdateMontagePlayRate(DeltaTime);
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
			bHasHitThisAttack = true;

			// Apply damage
			ApplyDamage(HitActor, Hit);

			// Check for headshot
			bool bHeadshot = IsHeadshot(Hit);

			// Play effects
			PlaySound(HitSound);
			PlayCameraShake();
			SpawnImpactFX(Hit.ImpactPoint, Hit.ImpactNormal);

			// Broadcast hit event
			OnMeleeHit.Broadcast(HitActor, Hit.ImpactPoint, bHeadshot);

			// Optional: Debug visualization
#if WITH_EDITOR
			if (GEngine && GEngine->GameViewport)
			{
				DrawDebugSphere(GetWorld(), Hit.ImpactPoint, 10.0f, 8, FColor::Red, false, 1.0f);
			}
#endif
		}
	}
}

void UMeleeAttackComponent::ApplyDamage(AActor* HitActor, const FHitResult& HitResult)
{
	if (!HitActor || !OwnerCharacter)
	{
		return;
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

	// Calculate impulse with momentum multiplier
	float FinalImpulse = Settings.HitImpulse * CalculateMomentumImpulseMultiplier();
	FVector ImpulseDirection = GetTraceDirection();

	// Apply impulse - try character launch first, then physics
	ApplyCharacterImpulse(HitActor, ImpulseDirection, FinalImpulse);
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

	if (Settings.LungeDistance <= 0.0f || Settings.LungeDuration <= 0.0f)
	{
		return;
	}

	if (!OwnerCharacter)
	{
		return;
	}

	// Calculate lunge progress
	float TotalLungeTime = Settings.WindupTime + Settings.ActiveTime;
	if (TotalLungeTime <= 0.0f)
	{
		return;
	}

	// Apply lunge velocity
	UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement();
	if (Movement)
	{
		float LungeSpeed = Settings.LungeDistance / Settings.LungeDuration;

		// Only apply horizontal lunge
		FVector LungeVelocity = LungeDirection * LungeSpeed;
		LungeVelocity.Z = 0.0f;

		// Blend with existing velocity
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

		if (ClosestTarget)
		{
			MagnetismTarget = ClosestTarget;
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

void UMeleeAttackComponent::StopMagnetism()
{
	MagnetismTarget.Reset();
}

void UMeleeAttackComponent::ApplyCharacterImpulse(AActor* HitActor, const FVector& ImpulseDirection, float ImpulseStrength)
{
	if (!HitActor || ImpulseStrength <= 0.0f)
	{
		return;
	}

	// Calculate knockback velocity - ImpulseStrength is already in cm/s units
	// Add small vertical component to help launch character off ground
	FVector KnockbackVelocity = ImpulseDirection * ImpulseStrength;
	KnockbackVelocity.Z = FMath::Max(KnockbackVelocity.Z, ImpulseStrength * 0.2f); // At least 20% upward

	// Try ShooterNPC first (has ApplyKnockback with AI stun)
	if (AShooterNPC* NPC = Cast<AShooterNPC>(HitActor))
	{
		NPC->ApplyKnockback(KnockbackVelocity, 0.4f);
		return;
	}

	// Try generic character
	if (ACharacter* HitCharacter = Cast<ACharacter>(HitActor))
	{
		HitCharacter->LaunchCharacter(KnockbackVelocity, true, true);
		return;
	}

	// Fallback to physics impulse for non-characters
	if (UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(HitActor->GetRootComponent()))
	{
		if (RootPrimitive->IsSimulatingPhysics())
		{
			RootPrimitive->AddImpulse(ImpulseDirection * ImpulseStrength);
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
		MeleeMesh->SetVisibility(true);

		// Get per-attack hidden bones
		const FMeleeAnimationData& AnimData = GetCurrentAnimationData();
		CurrentlyHiddenBones = AnimData.HiddenBones;

		// Hide specified bones for this attack type
		for (const FName& BoneName : CurrentlyHiddenBones)
		{
			MeleeMesh->HideBoneByName(BoneName, EPhysBodyOp::PBO_None);
		}

		// Set initial rotation to match camera
		UpdateMeleeMeshRotation();
	}
}

void UMeleeAttackComponent::SwitchToFirstPersonMesh()
{
	if (MeleeMesh)
	{
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
	// Only update during active melee phases
	if (CurrentState != EMeleeAttackState::InputDelay &&
		CurrentState != EMeleeAttackState::Windup &&
		CurrentState != EMeleeAttackState::Active &&
		CurrentState != EMeleeAttackState::Recovery)
	{
		return;
	}

	if (!MeleeMesh || !OwnerController)
	{
		return;
	}

	// Get camera transform
	FVector CameraLocation;
	FRotator CameraRotation;
	OwnerController->GetPlayerViewPoint(CameraLocation, CameraRotation);

	// Get per-attack offsets
	const FMeleeAnimationData& AnimData = GetCurrentAnimationData();

	// Combine rotations: Camera + Global Offset + Per-Attack Offset
	FQuat CameraQuat = CameraRotation.Quaternion();
	FQuat GlobalOffsetQuat = MeleeMeshRotationOffset.Quaternion();
	FQuat AttackOffsetQuat = AnimData.MeshRotationOffset.Quaternion();
	FQuat FinalQuat = CameraQuat * GlobalOffsetQuat * AttackOffsetQuat;

	FRotator FinalRotation = FinalQuat.Rotator();

	// Calculate location: Camera position + offsets transformed by camera rotation
	FVector LocalOffset = AnimData.MeshLocationOffset;
	FVector WorldOffset = CameraRotation.RotateVector(LocalOffset);
	FVector FinalLocation = CameraLocation + WorldOffset;

#if WITH_EDITOR
	// Debug output
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 0.0f, FColor::Yellow,
			FString::Printf(TEXT("MeleeOffset: P=%.1f Y=%.1f R=%.1f | AttackOffset: P=%.1f Y=%.1f R=%.1f"),
				MeleeMeshRotationOffset.Pitch, MeleeMeshRotationOffset.Yaw, MeleeMeshRotationOffset.Roll,
				AnimData.MeshRotationOffset.Pitch, AnimData.MeshRotationOffset.Yaw, AnimData.MeshRotationOffset.Roll));
	}
#endif

	// Apply final transform to MeleeMesh
	MeleeMesh->SetWorldLocationAndRotation(FinalLocation, FinalRotation);
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