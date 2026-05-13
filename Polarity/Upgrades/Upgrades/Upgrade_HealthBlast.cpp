// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "Upgrade_HealthBlast.h"
#include "UpgradeDefinition_HealthBlast.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "ChargeAnimationComponent.h"
#include "Upgrades/UpgradeManagerComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Variant_Shooter/HitMarkerComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"

// ========================================================================================
// AHealthBlastProjectile
// ========================================================================================

AHealthBlastProjectile::AHealthBlastProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	SetRootComponent(CollisionSphere);
	// Overlap with pawns/dynamic actors, block on world static geometry
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Overlap);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	CollisionSphere->SetSphereRadius(30.0f);
	CollisionSphere->SetSimulatePhysics(false);

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	MeshComponent->SetupAttachment(CollisionSphere);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->ProjectileGravityScale = 0.0f;
	ProjectileMovement->bShouldBounce = true;
	ProjectileMovement->Bounciness = 0.4f;
	ProjectileMovement->Friction = 0.3f;
}

void AHealthBlastProjectile::InitProjectile(float Speed, float Lifetime, float InDamage,
	float InDamageRadius, float InTargetKnockback, UNiagaraSystem* InHitVFX,
	USoundBase* InHitSound, UStaticMesh* InMesh, const FVector& InMeshScale, float InCollisionRadius)
{
	Damage = InDamage;
	DamageRadius = InDamageRadius;
	TargetKnockback = InTargetKnockback;
	HitVFX = InHitVFX;
	HitSound = InHitSound;

	CollisionSphere->SetSphereRadius(InCollisionRadius);

	if (InMesh)
	{
		MeshComponent->SetStaticMesh(InMesh);
		MeshComponent->SetWorldScale3D(InMeshScale);
	}

	ProjectileMovement->MaxSpeed = Speed;
	ProjectileMovement->Velocity = GetActorForwardVector() * Speed;

	// Bind overlap (for damageable actors)
	CollisionSphere->OnComponentBeginOverlap.AddDynamic(this, &AHealthBlastProjectile::OnProjectileOverlap);

	// On first bounce, enable gravity so projectiles fall naturally as debris
	ProjectileMovement->OnProjectileBounce.AddDynamic(this, &AHealthBlastProjectile::OnProjectileBounce);

	// Self-destruct timer
	GetWorldTimerManager().SetTimer(LifetimeTimer, this,
		&AHealthBlastProjectile::OnLifetimeExpired, Lifetime, false);
}

void AHealthBlastProjectile::OnProjectileOverlap(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	// Ignore self, player, and other health blast projectiles
	if (!OtherActor || OtherActor == GetInstigator() || OtherActor == this
		|| OtherActor->IsA<AHealthBlastProjectile>())
	{
		return;
	}

	// Prevent double hits on the same actor
	if (HitActors.Contains(OtherActor))
	{
		return;
	}

	// Skip anything that can't take damage
	if (!OtherActor->CanBeDamaged())
	{
		UE_LOG(LogTemp, Verbose, TEXT("[HEALTHBLAST] Overlap with %s — cannot be damaged, skipping"),
			*OtherActor->GetName());
		return;
	}

	HitActors.Add(OtherActor);

	const FVector HitLocation = SweepResult.ImpactPoint.IsZero() ? FVector(OtherActor->GetActorLocation()) : FVector(SweepResult.ImpactPoint);
	const FVector HitDirection = GetVelocity().GetSafeNormal();
	bool bKilled = false;

	// Apply damage
	if (Damage > 0.0f)
	{
		UGameplayStatics::ApplyDamage(OtherActor, Damage,
			GetInstigator() ? GetInstigator()->GetController() : nullptr,
			this, nullptr);

		bKilled = OtherActor->IsPendingKillPending();

		UE_LOG(LogTemp, Warning, TEXT("[HEALTHBLAST] HIT %s — Damage: %.1f, Killed: %s"),
			*OtherActor->GetName(), Damage, bKilled ? TEXT("YES") : TEXT("NO"));
	}

	// Apply knockback to characters
	if (TargetKnockback > 0.0f)
	{
		if (ACharacter* HitCharacter = Cast<ACharacter>(OtherActor))
		{
			HitCharacter->LaunchCharacter(HitDirection * TargetKnockback, false, false);
			UE_LOG(LogTemp, Warning, TEXT("[HEALTHBLAST] Knockback %s — Force: %.1f, Dir: %s"),
				*OtherActor->GetName(), TargetKnockback, *HitDirection.ToString());
		}
	}

	// Hitmarker on the player
	if (AShooterCharacter* Player = Cast<AShooterCharacter>(GetInstigator()))
	{
		if (UHitMarkerComponent* HitMarkerComp = Player->GetHitMarkerComponent())
		{
			HitMarkerComp->RegisterHit(HitLocation, HitDirection, Damage, false, bKilled);
		}
	}

	// Hit effects
	if (HitVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), HitVFX, HitLocation,
			FRotator::ZeroRotator, FVector::OneVector,
			true, true, ENCPoolMethod::None);
	}

	if (HitSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, HitSound, HitLocation);
	}

	// Don't destroy — let the projectile bounce off and continue
}

void AHealthBlastProjectile::OnProjectileBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity)
{
	// Enable gravity after first wall bounce so projectiles fall as debris
	if (ProjectileMovement && ProjectileMovement->ProjectileGravityScale == 0.0f)
	{
		ProjectileMovement->ProjectileGravityScale = 1.0f;
	}

	UE_LOG(LogTemp, Verbose, TEXT("[HEALTHBLAST] Bounce off %s at %s, remaining speed: %.1f"),
		ImpactResult.GetActor() ? *ImpactResult.GetActor()->GetName() : TEXT("World"),
		*ImpactResult.ImpactPoint.ToString(),
		ProjectileMovement ? ProjectileMovement->Velocity.Size() : 0.0f);
}

void AHealthBlastProjectile::OnLifetimeExpired()
{
	Destroy();
}

// ========================================================================================
// UUpgrade_HealthBlast
// ========================================================================================

UUpgrade_HealthBlast::UUpgrade_HealthBlast()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UUpgrade_HealthBlast::OnUpgradeActivated()
{
	// Cache typed definition
	CachedDef = Cast<UUpgradeDefinition_HealthBlast>(UpgradeDefinition);
	if (!CachedDef.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[HEALTHBLAST] Failed to cast UpgradeDefinition to HealthBlast type!"));
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

	// Cache ChargeAnimationComponent
	UChargeAnimationComponent* ChargeComp = Character->GetChargeAnimationComponent();
	CachedChargeComp = ChargeComp;

	if (ChargeComp)
	{
		ChargeComp->OnChannelingStarted.AddDynamic(this, &UUpgrade_HealthBlast::OnChannelingStarted);
		ChargeComp->OnChannelingEnded.AddDynamic(this, &UUpgrade_HealthBlast::OnChannelingEnded);
	}

	// Bind to prop captured to cancel empty capture timer
	Character->OnPropCaptured.AddDynamic(this, &UUpgrade_HealthBlast::OnPropCaptured);

	// Subscribe to the shared health-pickup pool so we can re-broadcast its
	// change events (UI keeps using our OnStoredPickupsChanged signature) and
	// honour MaxStoredPickups from this upgrade's definition by raising the
	// manager's cap if needed.
	UUpgradeManagerComponent* UpgradeManager = Character->GetUpgradeManager();
	CachedUpgradeManager = UpgradeManager;
	if (UpgradeManager)
	{
		// Raise the manager's cap if this upgrade allows more storage than the default.
		if (UpgradeManager->GetMaxStoredHealthPickups() < CachedDef->MaxStoredPickups)
		{
			UpgradeManager->MaxStoredHealthPickups = CachedDef->MaxStoredPickups;
		}

		UpgradeManager->OnStoredHealthPickupsChanged.AddDynamic(this, &UUpgrade_HealthBlast::HandleSharedPoolChanged);

		// Broadcast initial state so UI/HUD picks up current count immediately.
		OnStoredPickupsChanged.Broadcast(UpgradeManager->GetStoredHealthPickups(), UpgradeManager->GetMaxStoredHealthPickups());
	}
}

void UUpgrade_HealthBlast::OnUpgradeDeactivated()
{
	AShooterCharacter* Character = GetShooterCharacter();

	if (UChargeAnimationComponent* ChargeComp = CachedChargeComp.Get())
	{
		ChargeComp->OnChannelingStarted.RemoveDynamic(this, &UUpgrade_HealthBlast::OnChannelingStarted);
		ChargeComp->OnChannelingEnded.RemoveDynamic(this, &UUpgrade_HealthBlast::OnChannelingEnded);
	}

	if (Character)
	{
		Character->OnPropCaptured.RemoveDynamic(this, &UUpgrade_HealthBlast::OnPropCaptured);
	}

	if (UUpgradeManagerComponent* UpgradeManager = CachedUpgradeManager.Get())
	{
		UpgradeManager->OnStoredHealthPickupsChanged.RemoveDynamic(this, &UUpgrade_HealthBlast::HandleSharedPoolChanged);
	}

	// Clear timers
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EmptyCaptureTimer);
		World->GetTimerManager().ClearTimer(CooldownTimer);
	}
}

int32 UUpgrade_HealthBlast::GetStoredPickups() const
{
	if (const UUpgradeManagerComponent* UpgradeManager = CachedUpgradeManager.Get())
	{
		return UpgradeManager->GetStoredHealthPickups();
	}
	return 0;
}

void UUpgrade_HealthBlast::HandleSharedPoolChanged(int32 CurrentCount, int32 MaxCount)
{
	// Re-broadcast so existing UI / BP subscribers keep working.
	OnStoredPickupsChanged.Broadcast(CurrentCount, MaxCount);
}

void UUpgrade_HealthBlast::OnHealthPickupCollectedAtFullHP()
{
	// Pool incrementing is handled centrally by UUpgradeManagerComponent::NotifyHealthPickupCollectedAtFullHP
	// (which calls AddStoredHealthPickup BEFORE dispatching this hook). All we do here is play our own
	// feedback if the pool actually accepted the pickup — detect via current count vs cap.
	if (!CachedDef.IsValid())
	{
		return;
	}

	UUpgradeManagerComponent* UpgradeManager = CachedUpgradeManager.Get();
	if (!UpgradeManager)
	{
		return;
	}

	// If the pool was already at max BEFORE this notification, AddStoredHealthPickup returned false
	// and we shouldn't play stored-feedback. We can't observe that from here directly, but we can
	// rely on HandleSharedPoolChanged having fired on success — that already re-broadcasts to UI.
	// Stored VFX/SFX is a HealthBlast-specific cosmetic, so play it any time we get a pickup at
	// full HP while owned, regardless of whether the pool actually grew (player still sees feedback
	// even at cap, like Doom Eternal's "ammo full" sound).

	AShooterCharacter* Character = GetShooterCharacter();
	if (Character)
	{
		if (CachedDef->StoredVFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				GetWorld(), CachedDef->StoredVFX, Character->GetActorLocation(),
				FRotator::ZeroRotator, FVector::OneVector,
				true, true, ENCPoolMethod::None);
		}

		if (CachedDef->StoredSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, CachedDef->StoredSound, Character->GetActorLocation());
		}
	}
}

// ==================== Channeling Callbacks ====================

void UUpgrade_HealthBlast::OnChannelingStarted()
{
	// Only start empty capture timer if pool has pickups and we're not on cooldown
	if (GetStoredPickups() <= 0 || bOnCooldown || !CachedDef.IsValid())
	{
		return;
	}

	// Start timer — if nothing is captured within this delay, fire the blast
	GetWorld()->GetTimerManager().SetTimer(EmptyCaptureTimer, this,
		&UUpgrade_HealthBlast::OnEmptyCaptureTimerFired, CachedDef->EmptyCaptureDelay, false);
}

void UUpgrade_HealthBlast::OnPropCaptured(AActor* CapturedActor)
{
	// Something was captured — cancel the empty capture timer
	GetWorld()->GetTimerManager().ClearTimer(EmptyCaptureTimer);
}

void UUpgrade_HealthBlast::OnChannelingEnded()
{
	// Channeling ended (player released button) — cancel the empty capture timer
	GetWorld()->GetTimerManager().ClearTimer(EmptyCaptureTimer);
}

void UUpgrade_HealthBlast::OnEmptyCaptureTimerFired()
{
	FireHealthBlast();

	// Force-end channeling so the player returns to normal state
	if (UChargeAnimationComponent* ChargeComp = CachedChargeComp.Get())
	{
		ChargeComp->OnChannelButtonReleased();
	}
}

// ==================== Blast Logic ====================

void UUpgrade_HealthBlast::FireHealthBlast()
{
	if (bOnCooldown || !CachedDef.IsValid())
	{
		return;
	}

	UUpgradeManagerComponent* UpgradeManager = CachedUpgradeManager.Get();
	if (!UpgradeManager || UpgradeManager->GetStoredHealthPickups() <= 0)
	{
		return;
	}

	AShooterCharacter* Character = GetShooterCharacter();
	if (!Character)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(Character->GetController());
	if (!PC)
	{
		return;
	}

	// Drain the entire pool (HealthBlast is "all-or-nothing"). ConsumeStoredHealthPickups
	// returns the count actually drained and broadcasts the change.
	const int32 PickupsToFire = UpgradeManager->ConsumeStoredHealthPickups(UpgradeManager->GetStoredHealthPickups());
	if (PickupsToFire <= 0)
	{
		return;
	}

	// Spawn at plate position: camera + 200cm forward (matches channeling plate offset)
	FVector CameraLoc;
	FRotator CameraRot;
	PC->GetPlayerViewPoint(CameraLoc, CameraRot);
	const FVector SpawnLocation = CameraLoc + CameraRot.RotateVector(FVector(50.0f, 0.0f, -50.0f));
	const FRotator AimRotation = CameraRot;
	const FVector AimDirection = AimRotation.Vector();

	UWorld* World = GetWorld();

	// Spawn projectiles — one per stored pickup, spread in a cone
	for (int32 i = 0; i < PickupsToFire; ++i)
	{
		const FVector ProjectileDir = GetRandomConeDirection(AimDirection, CachedDef->SpreadHalfAngle);
		const FRotator ProjectileRot = ProjectileDir.Rotation();

		FActorSpawnParameters SpawnParams;
		SpawnParams.Instigator = Character;
		SpawnParams.Owner = Character;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AHealthBlastProjectile* Projectile = World->SpawnActor<AHealthBlastProjectile>(
			AHealthBlastProjectile::StaticClass(), SpawnLocation, ProjectileRot, SpawnParams);

		if (Projectile)
		{
			Projectile->InitProjectile(
				CachedDef->ProjectileSpeed,
				CachedDef->ProjectileLifetime,
				CachedDef->DamagePerPickup,
				CachedDef->DamageRadius,
				CachedDef->TargetKnockbackPerPickup,
				CachedDef->HitVFX,
				CachedDef->HitSound,
				CachedDef->ProjectileMesh,
				CachedDef->ProjectileMeshScale,
				CachedDef->ProjectileCollisionRadius
			);
		}
	}

	// Knockback the player backward (opposite to aim direction)
	const float TotalPlayerKnockback = PickupsToFire * CachedDef->PlayerKnockbackPerPickup;
	Character->LaunchCharacter(-AimDirection * TotalPlayerKnockback, false, false);

	// Shot VFX at player location
	if (CachedDef->ShotVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World, CachedDef->ShotVFX, SpawnLocation, AimRotation,
			FVector::OneVector, true, true, ENCPoolMethod::None);
	}

	// Shot sound
	if (CachedDef->ShotSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, CachedDef->ShotSound, SpawnLocation);
	}

	// Camera shake scaled by pickup count
	if (CachedDef->CameraShakeClass && PC)
	{
		const float ShakeScale = CachedDef->CameraShakeBaseScale * PickupsToFire;
		PC->ClientStartCameraShake(CachedDef->CameraShakeClass, ShakeScale);
	}

	UE_LOG(LogTemp, Warning, TEXT("[HEALTHBLAST] Fired %d health pickups!"), PickupsToFire);

	// Pool was already drained by ConsumeStoredHealthPickups above — broadcast happened there.

	// Start cooldown
	bOnCooldown = true;
	World->GetTimerManager().SetTimer(CooldownTimer, [this]()
	{
		bOnCooldown = false;
	}, CachedDef->Cooldown, false);
}

FVector UUpgrade_HealthBlast::GetRandomConeDirection(const FVector& Forward, float HalfAngleDegrees) const
{
	// Use FMath::VRandCone for uniform distribution within a cone
	return FMath::VRandCone(Forward, FMath::DegreesToRadians(HalfAngleDegrees));
}
