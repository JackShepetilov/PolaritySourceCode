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
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/Weapons/DroppedRangedWeapon.h"
#include "Variant_Shooter/DamageTypes/DamageType_Melee.h"

const FName UUpgrade_AirKick::TAG_AirMailIncoming(TEXT("AirMailIncoming"));
const FName UUpgrade_AirKick::TAG_AirMailKicked(TEXT("AirMailKicked"));

UUpgrade_AirKick::UUpgrade_AirKick()
{
	PrimaryComponentTick.bCanEverTick = false;
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
	}
}

void UUpgrade_AirKick::OnUpgradeDeactivated()
{
	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

	UMeleeAttackComponent* MeleeComp = Character->FindComponentByClass<UMeleeAttackComponent>();
	if (MeleeComp)
	{
		MeleeComp->OnMeleeHit.RemoveDynamic(this, &UUpgrade_AirKick::HandleMeleeHit);
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

bool UUpgrade_AirKick::QualifiesForBounce(const FVector& PreImpactVelocity, const FVector& ImpactNormal) const
{
	if (!DefAirKick.IsValid())
	{
		return false;
	}

	const float Speed = PreImpactVelocity.Size();
	if (Speed < DefAirKick->MinBounceImpactSpeed)
	{
		return false;
	}

	// Incidence angle test. MinBounceAngleDeg is measured to the SURFACE PLANE (90 = head-on),
	// so the velocity-vs-normal angle must be <= (90 - MinBounceAngleDeg). A sliding hit has
	// the velocity nearly parallel to the plane (angle-to-normal near 90°) and is rejected.
	const FVector VelDir = PreImpactVelocity / Speed;
	const float CosToNormal = FVector::DotProduct(-VelDir, ImpactNormal.GetSafeNormal());
	const float CosLimit = FMath::Cos(FMath::DegreesToRadians(90.0f - FMath::Clamp(DefAirKick->MinBounceAngleDeg, 0.0f, 90.0f)));
	return CosToNormal >= CosLimit;
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
	const FVector& ImpactNormal, FVector& OutVelocity) const
{
	return QualifiesForBounce(PreImpactVelocity, ImpactNormal)
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
