// AbilityHandler_EMFBurst.cpp

#include "AbilityHandler_EMFBurst.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/EMFProjectile.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"

void UAbilityHandler_EMFBurst::OnPerShotEffect_Implementation()
{
	if (!OwningCharacter || !CachedBurstDef)
	{
		return;
	}

	const TSubclassOf<AEMFProjectile> ProjectileClass = CachedBurstDef->GetProjectileClassAtLevel(CurrentLevel);
	if (!ProjectileClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_DEBUG] EMFBurst: no ProjectileClass set"));
		return;
	}

	USkeletalMeshComponent* FPMesh = OwningCharacter->GetFirstPersonMesh();
	if (!FPMesh)
	{
		return;
	}

	const FVector SpawnLoc = FPMesh->GetSocketLocation(CachedBurstDef->ProjectileSpawnSocket);

	APlayerCameraManager* CamMgr = UGameplayStatics::GetPlayerCameraManager(OwningCharacter->GetWorld(), 0);
	if (!CamMgr)
	{
		return;
	}
	const FVector CameraLoc = CamMgr->GetCameraLocation();
	const FRotator CameraRot = CamMgr->GetCameraRotation();
	const FVector AimEnd = CameraLoc + CameraRot.Vector() * 100000.0f;

	const FVector VariedTarget = AimEnd + UKismetMathLibrary::RandomUnitVector() * CachedStats.AimVariance;
	const FRotator AimRot = UKismetMathLibrary::FindLookAtRotation(SpawnLoc, VariedTarget);
	const FTransform SpawnTransform(AimRot, SpawnLoc, FVector::OneVector);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = OwningCharacter;
	SpawnParams.Instigator = OwningCharacter;

	AEMFProjectile* Projectile = OwningCharacter->GetWorld()->SpawnActor<AEMFProjectile>(
		ProjectileClass, SpawnTransform, SpawnParams);
	if (!Projectile)
	{
		return;
	}

	Projectile->InitializeFromPlayerCharge(OwningCharacter, CachedStats.ChargePerShot);

	if (UProjectileMovementComponent* PMC = Projectile->FindComponentByClass<UProjectileMovementComponent>())
	{
		PMC->Velocity = AimRot.Vector() * CachedStats.ProjectileSpeed;
	}
}
