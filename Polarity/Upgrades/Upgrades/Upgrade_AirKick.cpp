// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_AirKick.h"
#include "UpgradeDefinition_AirKick.h"
#include "ShooterCharacter.h"
#include "EMFPhysicsProp.h"
#include "MeleeAttackComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "Animation/AnimMontage.h"
#include "Engine/OverlapResult.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/Weapons/DroppedRangedWeapon.h"
#include "Variant_Shooter/DamageTypes/DamageType_Melee.h"
#include "Variant_Shooter/AnimNotify_MeleeWindow.h"

const FName UUpgrade_AirKick::TAG_AirMailIncoming(TEXT("AirMailIncoming"));
const FName UUpgrade_AirKick::TAG_AirMailKicked(TEXT("AirMailKicked"));

UUpgrade_AirKick::UUpgrade_AirKick()
{
	// Tick is used only by the kick magnet — enabled on swing start, disabled after.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UUpgrade_AirKick::OnUpgradeActivated()
{
	DefAirKick = Cast<UUpgradeDefinition_AirKick>(UpgradeDefinition);
	if (!DefAirKick.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Air Mail: UpgradeDefinition is not UUpgradeDefinition_AirKick!"));
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

	UMeleeAttackComponent* MeleeComp = Character->FindComponentByClass<UMeleeAttackComponent>();
	if (MeleeComp)
	{
		MeleeComp->OnMeleeHit.AddDynamic(this, &UUpgrade_AirKick::HandleMeleeHit);
		MeleeComp->OnMeleeAttackStarted.AddDynamic(this, &UUpgrade_AirKick::HandleMeleeAttackStarted);
		MeleeComp->OnMeleeAttackEnded.AddDynamic(this, &UUpgrade_AirKick::HandleMeleeAttackEnded);
	}
}

void UUpgrade_AirKick::OnUpgradeDeactivated()
{
	StopKickMagnet();

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

	UMeleeAttackComponent* MeleeComp = Character->FindComponentByClass<UMeleeAttackComponent>();
	if (MeleeComp)
	{
		MeleeComp->OnMeleeHit.RemoveDynamic(this, &UUpgrade_AirKick::HandleMeleeHit);
		MeleeComp->OnMeleeAttackStarted.RemoveDynamic(this, &UUpgrade_AirKick::HandleMeleeAttackStarted);
		MeleeComp->OnMeleeAttackEnded.RemoveDynamic(this, &UUpgrade_AirKick::HandleMeleeAttackEnded);
	}
}

// ==================== Static / query helpers ====================

UUpgrade_AirKick* UUpgrade_AirKick::FindActiveAirMail(const UObject* WorldContextObject)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	APlayerController* PC = World->GetFirstPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return nullptr;
	}

	// Upgrade components live on the character and exist only while the upgrade is owned.
	return Pawn->FindComponentByClass<UUpgrade_AirKick>();
}

bool UUpgrade_AirKick::QualifiesForBounce(const FVector& PreImpactVelocity, const FVector& ImpactNormal,
	bool bSkipAngleCheck) const
{
	if (!DefAirKick.IsValid())
	{
		return false;
	}

	const float Speed = PreImpactVelocity.Size();
	if (Speed < DefAirKick->MinBounceImpactSpeed)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AIR_MAIL] bounce rejected: impact speed %.0f < %.0f"),
			Speed, DefAirKick->MinBounceImpactSpeed);
		return false;
	}

	if (bSkipAngleCheck)
	{
		return true;
	}

	// Incidence angle test. MinBounceAngleDeg is measured to the SURFACE PLANE (90 = head-on),
	// so the velocity-vs-normal angle must be <= (90 - MinBounceAngleDeg). A sliding hit has
	// the velocity nearly parallel to the plane (angle-to-normal near 90°) and is rejected.
	const FVector VelDir = PreImpactVelocity / Speed;
	const float CosToNormal = FVector::DotProduct(-VelDir, ImpactNormal.GetSafeNormal());
	const float CosLimit = FMath::Cos(FMath::DegreesToRadians(90.0f - FMath::Clamp(DefAirKick->MinBounceAngleDeg, 0.0f, 90.0f)));
	if (CosToNormal < CosLimit)
	{
		const float AngleToPlaneDeg = 90.0f - FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(CosToNormal, -1.0f, 1.0f)));
		UE_LOG(LogTemp, Warning, TEXT("[AIR_MAIL] bounce rejected: impact angle to surface %.0f° < %.0f° (glancing)"),
			AngleToPlaneDeg, DefAirKick->MinBounceAngleDeg);
		return false;
	}
	return true;
}

bool UUpgrade_AirKick::ComputeReturnVelocity(const FVector& FromLocation, FVector& OutVelocity) const
{
	if (!DefAirKick.IsValid())
	{
		return false;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return false;
	}

	// Target point: player camera (head) + configured height offset, captured NOW (no homing).
	FVector TargetPoint = Character->GetActorLocation();
	if (UCameraComponent* Camera = Character->GetFirstPersonCameraComponent())
	{
		TargetPoint = Camera->GetComponentLocation();
	}
	TargetPoint.Z += DefAirKick->ReturnTargetHeightOffset;

	const FVector ToTarget = TargetPoint - FromLocation;
	const float Distance = ToTarget.Size();
	if (Distance < KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float Speed = FMath::Max(DefAirKick->ReturnSpeed, 100.0f);
	OutVelocity = (ToTarget / Distance) * Speed;

	// Simple ballistic compensation: add half the gravity drop over the straight-line flight
	// time so the object arrives near the head-level point instead of sagging under it.
	const float FlightTime = Distance / Speed;
	const float GravityZ = GetWorld() ? GetWorld()->GetGravityZ() : -980.0f;
	OutVelocity.Z += 0.5f * (-GravityZ) * FlightTime;

	return true;
}

bool UUpgrade_AirKick::TryComputeBounce(const FVector& FromLocation, const FVector& PreImpactVelocity,
	const FVector& ImpactNormal, FVector& OutVelocity, bool bSkipAngleCheck) const
{
	return QualifiesForBounce(PreImpactVelocity, ImpactNormal, bSkipAngleCheck)
		&& ComputeReturnVelocity(FromLocation, OutVelocity);
}

void UUpgrade_AirKick::PlayBounceFeedback(const FVector& Location) const
{
	if (!DefAirKick.IsValid())
	{
		return;
	}

	if (DefAirKick->BounceFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), DefAirKick->BounceFX, Location, FRotator::ZeroRotator,
			FVector(DefAirKick->BounceFXScale), true, true, ENCPoolMethod::None);
	}

	if (DefAirKick->BounceSound)
	{
		UGameplayStatics::PlaySoundAtLocation(GetWorld(), DefAirKick->BounceSound, Location);
	}
}

float UUpgrade_AirKick::GetKickDamage() const
{
	return DefAirKick.IsValid() ? DefAirKick->KickDamage : 0.0f;
}

TSubclassOf<UDamageType> UUpgrade_AirKick::GetKickDamageType() const
{
	if (DefAirKick.IsValid() && DefAirKick->KickDamageType)
	{
		return DefAirKick->KickDamageType;
	}
	return UDamageType_Melee::StaticClass();
}

float UUpgrade_AirKick::GetReturnSpinSpeed() const
{
	return DefAirKick.IsValid() ? DefAirKick->ReturnSpinSpeed : 0.0f;
}

// ==================== Kick (air-melee redirect) ====================

void UUpgrade_AirKick::HandleMeleeHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage)
{
	if (!DefAirKick.IsValid() || !HitActor)
	{
		return;
	}

	// Only objects currently flying back to the player can be kicked.
	if (!HitActor->ActorHasTag(TAG_AirMailIncoming))
	{
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

	// The kick is the AIRBORNE melee attack — grounded swings don't redirect.
	UCharacterMovementComponent* MovementComp = Character->GetCharacterMovement();
	if (!MovementComp || !MovementComp->IsFalling())
	{
		return;
	}

	// Kick direction: camera forward.
	FVector KickDir = Character->GetActorForwardVector();
	if (APlayerController* PC = Cast<APlayerController>(Character->GetController()))
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		KickDir = ViewRotation.Vector();
	}

	const float KickSpeed = DefAirKick->KickSpeed;
	bool bKicked = false;

	if (ADroppedRangedWeapon* Weapon = Cast<ADroppedRangedWeapon>(HitActor))
	{
		if (UStaticMeshComponent* Mesh = Weapon->WeaponMesh)
		{
			Mesh->SetPhysicsLinearVelocity(KickDir * KickSpeed);
			if (DefAirKick->KickSpinSpeed > 0.0f)
			{
				Mesh->SetPhysicsAngularVelocityInDegrees(FMath::VRand() * DefAirKick->KickSpinSpeed);
			}
			bKicked = true;
		}
	}
	else if (AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(HitActor))
	{
		if (UStaticMeshComponent* Mesh = Prop->PropMesh)
		{
			// Force the deterministic single-target "weak impact" path on the next hit:
			// bCanExplode must be true for the impact branch to run at all, and an effectively
			// infinite ExplosionMinCharge guarantees it resolves to ApplyWeakImpactToNPC
			// (no AoE), dealing exactly KickDamage (curve override cleared).
			Prop->bCanExplode = true;
			Prop->bScaleExplosionWithCharge = false;
			Prop->ExplosionMinCharge = 1e9f;
			Prop->WeakImpactDamage = DefAirKick->KickDamage;
			Prop->WeakImpactDamageByCharge = nullptr;

			Mesh->SetPhysicsLinearVelocity(KickDir * KickSpeed);
			if (DefAirKick->KickSpinSpeed > 0.0f)
			{
				Mesh->SetPhysicsAngularVelocityInDegrees(FMath::VRand() * DefAirKick->KickSpinSpeed);
			}
			bKicked = true;
		}
	}
	else if (AShooterNPC* NPC = Cast<AShooterNPC>(HitActor))
	{
		if (!NPC->IsDead())
		{
			// Reuses the launched-state machinery (works on stunned enemies, humanoids no-op).
			NPC->LaunchIntoAir(KickDir * KickSpeed);
			bKicked = true;
		}
	}

	if (!bKicked)
	{
		return;
	}

	HitActor->Tags.Remove(TAG_AirMailIncoming);
	HitActor->Tags.Add(TAG_AirMailKicked);

	UE_LOG(LogTemp, Warning, TEXT("[AIR_MAIL] %s kicked %s — speed=%.0f, kickDamage=%.0f"),
		*Character->GetName(), *HitActor->GetName(), KickSpeed, DefAirKick->KickDamage);

	PlayKickFeedback(HitLocation, KickDir);
}

void UUpgrade_AirKick::PlayKickFeedback(const FVector& Location, const FVector& Direction) const
{
	if (!DefAirKick.IsValid())
	{
		return;
	}

	if (DefAirKick->KickImpactFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			DefAirKick->KickImpactFX,
			Location,
			Direction.Rotation(),
			FVector(DefAirKick->KickImpactFXScale),
			true,  // auto-destroy
			true,  // auto-activate
			ENCPoolMethod::None
		);
	}

	if (DefAirKick->KickSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			GetWorld(),
			DefAirKick->KickSound,
			Location,
			DefAirKick->KickSoundVolume,
			DefAirKick->KickSoundPitch
		);
	}
}

// ==================== Kick Magnet (timing assist) ====================

// File-scope velocity accessors for the three steerable body kinds (uniquely named — unity-safe).
static FVector GetAirMailBodyVelocity(AActor* Target)
{
	if (ADroppedRangedWeapon* Weapon = Cast<ADroppedRangedWeapon>(Target))
	{
		return Weapon->WeaponMesh ? Weapon->WeaponMesh->GetPhysicsLinearVelocity() : FVector::ZeroVector;
	}
	if (AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(Target))
	{
		return Prop->PropMesh ? Prop->PropMesh->GetPhysicsLinearVelocity() : FVector::ZeroVector;
	}
	if (ACharacter* Char = Cast<ACharacter>(Target))
	{
		return Char->GetCharacterMovement() ? Char->GetCharacterMovement()->Velocity : Char->GetVelocity();
	}
	return Target ? Target->GetVelocity() : FVector::ZeroVector;
}

static void SetAirMailBodyVelocity(AActor* Target, const FVector& NewVelocity)
{
	if (ADroppedRangedWeapon* Weapon = Cast<ADroppedRangedWeapon>(Target))
	{
		if (Weapon->WeaponMesh)
		{
			Weapon->WeaponMesh->SetPhysicsLinearVelocity(NewVelocity);
		}
		return;
	}
	if (AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(Target))
	{
		if (Prop->PropMesh)
		{
			Prop->PropMesh->SetPhysicsLinearVelocity(NewVelocity);
		}
		return;
	}
	if (ACharacter* Char = Cast<ACharacter>(Target))
	{
		if (UCharacterMovementComponent* CMC = Char->GetCharacterMovement())
		{
			CMC->Velocity = NewVelocity;
		}
	}
}

void UUpgrade_AirKick::HandleMeleeAttackStarted()
{
	if (!DefAirKick.IsValid() || !DefAirKick->bEnableKickMagnet)
	{
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	UMeleeAttackComponent* MeleeComp = Character ? Character->FindComponentByClass<UMeleeAttackComponent>() : nullptr;
	if (!MeleeComp)
	{
		return;
	}

	// The kick is the AIRBORNE melee attack — ground/slide swings don't steer bodies.
	if (MeleeComp->GetCurrentAttackType() != EMeleeAttackType::Airborne)
	{
		return;
	}

	bKickMagnetActive = true;
	MagnetTarget.Reset();
	SetComponentTickEnabled(true);
}

void UUpgrade_AirKick::HandleMeleeAttackEnded()
{
	StopKickMagnet();
}

void UUpgrade_AirKick::StopKickMagnet()
{
	bKickMagnetActive = false;
	MagnetTarget.Reset();
	SetComponentTickEnabled(false);
}

void UUpgrade_AirKick::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateKickMagnet(DeltaTime);
}

void UUpgrade_AirKick::UpdateKickMagnet(float DeltaTime)
{
	if (!bKickMagnetActive || !DefAirKick.IsValid())
	{
		StopKickMagnet();
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	UMeleeAttackComponent* MeleeComp = Character ? Character->FindComponentByClass<UMeleeAttackComponent>() : nullptr;
	if (!Character || !MeleeComp)
	{
		StopKickMagnet();
		return;
	}

	// Steer only during the pre-window phases. Once the damage window opens (Active) the
	// regular melee sweep takes over; any later state means the swing is done.
	const EMeleeAttackState State = MeleeComp->GetAttackState();
	const bool bPreWindowPhase =
		State == EMeleeAttackState::HidingWeapon ||
		State == EMeleeAttackState::InputDelay ||
		State == EMeleeAttackState::Windup;
	if (!bPreWindowPhase)
	{
		StopKickMagnet();
		return;
	}

	// Camera viewpoint.
	FVector CamLocation = Character->GetActorLocation();
	FRotator CamRotation = Character->GetActorRotation();
	if (APlayerController* PC = Cast<APlayerController>(Character->GetController()))
	{
		PC->GetPlayerViewPoint(CamLocation, CamRotation);
	}
	else if (UCameraComponent* Camera = Character->GetFirstPersonCameraComponent())
	{
		CamLocation = Camera->GetComponentLocation();
		CamRotation = Camera->GetComponentRotation();
	}
	const FVector CamDirection = CamRotation.Vector();

	// (Re)acquire a target while we have none — the object may only bounce into range
	// mid-windup (the player pressed melee early, which is exactly the case we assist).
	AActor* Target = MagnetTarget.Get();
	if (!Target || !Target->ActorHasTag(TAG_AirMailIncoming))
	{
		MagnetTarget = FindIncomingTargetInCone(CamLocation, CamDirection);
		Target = MagnetTarget.Get();
		if (!Target)
		{
			return; // keep ticking — a bounce may still happen during the windup
		}

		UE_LOG(LogTemp, Warning, TEXT("[AIR_MAIL] kick magnet engaged on %s"), *Target->GetName());
	}

	// Arrival point: the kick hitbox in front of the camera (tracks the view each tick).
	const FVector HitboxPoint = CamLocation + CamDirection * DefAirKick->MagnetHitboxDistance;
	const float RemainingT = FMath::Max(ComputeTimeUntilDamageWindow(MeleeComp), 0.05f);

	// Velocity that lands the body on the hitbox exactly when the window opens, capped so a
	// late swing doesn't teleport the body. Smoothly blended so the flight stays natural.
	FVector RequiredVelocity = (HitboxPoint - Target->GetActorLocation()) / RemainingT;
	RequiredVelocity = RequiredVelocity.GetClampedToMaxSize(DefAirKick->MagnetMaxSpeed);

	const FVector CurrentVelocity = GetAirMailBodyVelocity(Target);
	const FVector NewVelocity = FMath::VInterpTo(CurrentVelocity, RequiredVelocity, DeltaTime, DefAirKick->MagnetVelocityInterpSpeed);
	SetAirMailBodyVelocity(Target, NewVelocity);
}

AActor* UUpgrade_AirKick::FindIncomingTargetInCone(const FVector& CamLocation, const FVector& CamDirection) const
{
	if (!DefAirKick.IsValid() || !GetWorld())
	{
		return nullptr;
	}

	AShooterCharacter* Character = GetShooterCharacter();

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody); // thrown weapons / props
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);        // bounced NPCs

	FCollisionQueryParams QueryParams;
	if (Character)
	{
		QueryParams.AddIgnoredActor(Character);
	}

	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(
		Overlaps, CamLocation, FQuat::Identity, ObjectParams,
		FCollisionShape::MakeSphere(DefAirKick->MagnetSearchRadius), QueryParams);

	const float CosLimit = FMath::Cos(FMath::DegreesToRadians(DefAirKick->MagnetConeHalfAngleDeg));

	AActor* Best = nullptr;
	float BestDot = CosLimit; // candidates must beat the cone limit
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Candidate = Overlap.GetActor();
		if (!Candidate || Candidate == Best || !Candidate->ActorHasTag(TAG_AirMailIncoming))
		{
			continue;
		}

		const FVector ToCandidate = (Candidate->GetActorLocation() - CamLocation).GetSafeNormal();
		const float Dot = FVector::DotProduct(CamDirection, ToCandidate);
		if (Dot >= BestDot)
		{
			BestDot = Dot;
			Best = Candidate;
		}
	}

	return Best;
}

float UUpgrade_AirKick::ComputeTimeUntilDamageWindow(UMeleeAttackComponent* MeleeComp) const
{
	const float Fallback = DefAirKick.IsValid() ? DefAirKick->MagnetFallbackLeadTime : 0.25f;
	if (!MeleeComp || !MeleeComp->MeleeMesh)
	{
		return Fallback;
	}

	UAnimInstance* AnimInst = MeleeComp->MeleeMesh->GetAnimInstance();
	UAnimMontage* Montage = AnimInst ? AnimInst->GetCurrentActiveMontage() : nullptr;
	if (!Montage)
	{
		return Fallback;
	}

	const float Position = AnimInst->Montage_GetPosition(Montage);
	const float PlayRate = FMath::Max(AnimInst->Montage_GetPlayRate(Montage), 0.1f);

	// Earliest still-ahead damage-window trigger: either the window notify STATE's begin time
	// or the instant activate notify (both supported by MeleeAttackComponent).
	float BestTrigger = -1.0f;
	for (const FAnimNotifyEvent& Event : Montage->Notifies)
	{
		const bool bWindowState = Event.NotifyStateClass
			&& Event.NotifyStateClass->IsA<UAnimNotifyState_MeleeDamageWindow>();
		const bool bActivateNotify = Event.Notify
			&& Event.Notify->IsA<UAnimNotify_MeleeActivate>();
		if (!bWindowState && !bActivateNotify)
		{
			continue;
		}

		const float Trigger = Event.GetTriggerTime();
		if (Trigger >= Position - KINDA_SMALL_NUMBER)
		{
			BestTrigger = (BestTrigger < 0.0f) ? Trigger : FMath::Min(BestTrigger, Trigger);
		}
	}

	if (BestTrigger < 0.0f)
	{
		return Fallback;
	}

	return FMath::Max((BestTrigger - Position) / PlayRate, 0.0f);
}
