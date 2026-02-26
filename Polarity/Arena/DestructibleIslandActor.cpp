// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "DestructibleIslandActor.h"
#include "Polarity/Variant_Shooter/Weapons/EMFProjectile.h"
#include "Polarity/EMFPhysicsProp.h"
#include "Polarity/Variant_Shooter/DamageTypes/DamageType_Melee.h"
#include "Polarity/Variant_Shooter/DamageTypes/DamageType_MomentumBonus.h"
#include "Polarity/Variant_Shooter/DamageTypes/DamageType_Dropkick.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/DamageEvents.h"
#include "DestroyedIslandsSubsystem.h"

ADestructibleIslandActor::ADestructibleIslandActor()
{
	PrimaryActorTick.bCanEverTick = false;

	IslandMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("IslandMesh"));
	SetRootComponent(IslandMesh);

	// BlockAll so player walks on it, projectiles/props hit it
	IslandMesh->SetCollisionProfileName(TEXT("BlockAll"));
	IslandMesh->SetNotifyRigidBodyCollision(true);

	// Tag for MeleeAttackComponent to recognize as a valid melee target
	Tags.Add(TEXT("MeleeDestructible"));
}

void ADestructibleIslandActor::BeginPlay()
{
	Super::BeginPlay();

	IslandMesh->OnComponentHit.AddDynamic(this, &ADestructibleIslandActor::OnIslandHit);

	// Check persistence: if this island was already destroyed this session, hide immediately
	if (!IslandID.IsNone())
	{
		if (UDestroyedIslandsSubsystem* Subsystem = GetGameInstance()->GetSubsystem<UDestroyedIslandsSubsystem>())
		{
			if (Subsystem->IsIslandDestroyed(IslandID))
			{
				bIsDestroyed = true;
				IslandMesh->SetVisibility(false);
				IslandMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				UE_LOG(LogTemp, Log, TEXT("DestructibleIsland [%s]: Already destroyed — hiding on BeginPlay"), *IslandID.ToString());
				return;
			}
		}
	}
}

// ==================== Collision (Projectiles & Props) ====================

void ADestructibleIslandActor::OnIslandHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (bIsDestroyed || !OtherActor)
	{
		return;
	}

	// EMF Projectile — uses ProjectileMovementComponent velocity
	if (AEMFProjectile* Projectile = Cast<AEMFProjectile>(OtherActor))
	{
		if (UProjectileMovementComponent* ProjMove = Projectile->FindComponentByClass<UProjectileMovementComponent>())
		{
			const float Speed = ProjMove->Velocity.Size();
			TakeImpactDamage(Speed, MinImpactSpeed, Projectile);
		}
		return;
	}

	// EMF Physics Prop — uses physics linear velocity
	if (AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(OtherActor))
	{
		if (UStaticMeshComponent* PropMesh = Cast<UStaticMeshComponent>(Prop->GetRootComponent()))
		{
			const float Speed = PropMesh->GetPhysicsLinearVelocity().Size();
			TakeImpactDamage(Speed, MinImpactSpeed, Prop);
		}
		return;
	}
}

void ADestructibleIslandActor::TakeImpactDamage(float Speed, float MinSpeed, AActor* DamageCauser)
{
	if (Speed < MinSpeed)
	{
		return;
	}

	const float Damage = (Speed - MinSpeed) * DamagePerSpeed;
	IslandHP -= Damage;

	UE_LOG(LogTemp, Log, TEXT("DestructibleIsland [%s]: Impact damage %.0f (speed %.0f), HP: %.0f/%.0f"),
		*IslandID.ToString(), Damage, Speed, IslandHP, MaxIslandHP);

	if (IslandHP <= 0.f)
	{
		DestroyIsland(DamageCauser);
	}
}

// ==================== Melee Damage ====================

float ADestructibleIslandActor::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	if (bIsDestroyed)
	{
		return 0.f;
	}

	// Only accept melee damage types
	const UDamageType* DamageTypeCDO = DamageEvent.DamageTypeClass ?
		DamageEvent.DamageTypeClass->GetDefaultObject<UDamageType>() : nullptr;

	const bool bIsMeleeDamage = DamageTypeCDO &&
		(DamageTypeCDO->IsA<UDamageType_Melee>() ||
		 DamageTypeCDO->IsA<UDamageType_MomentumBonus>() ||
		 DamageTypeCDO->IsA<UDamageType_Dropkick>());

	if (!bIsMeleeDamage)
	{
		return 0.f;
	}

	// Check player velocity — melee only counts if player is moving fast
	float PlayerSpeed = 0.f;
	if (DamageCauser)
	{
		if (ACharacter* PlayerChar = Cast<ACharacter>(DamageCauser))
		{
			if (UCharacterMovementComponent* Movement = PlayerChar->GetCharacterMovement())
			{
				PlayerSpeed = Movement->Velocity.Size();
			}
		}
	}

	if (PlayerSpeed < MinMeleeSpeed)
	{
		UE_LOG(LogTemp, Verbose, TEXT("DestructibleIsland [%s]: Melee rejected — player speed %.0f < min %.0f"),
			*IslandID.ToString(), PlayerSpeed, MinMeleeSpeed);
		return 0.f;
	}

	// Apply melee damage + speed bonus
	const float SpeedBonus = (PlayerSpeed - MinMeleeSpeed) * DamagePerSpeed;
	const float TotalDamage = DamageAmount + SpeedBonus;
	IslandHP -= TotalDamage;

	UE_LOG(LogTemp, Log, TEXT("DestructibleIsland [%s]: Melee damage %.0f (base) + %.0f (speed bonus), HP: %.0f/%.0f"),
		*IslandID.ToString(), DamageAmount, SpeedBonus, IslandHP, MaxIslandHP);

	if (IslandHP <= 0.f)
	{
		DestroyIsland(DamageCauser);
	}

	return TotalDamage;
}

// ==================== Destruction ====================

void ADestructibleIslandActor::DestroyIsland(AActor* Destroyer)
{
	if (bIsDestroyed)
	{
		return;
	}

	bIsDestroyed = true;

	// Register in persistence subsystem
	if (!IslandID.IsNone())
	{
		if (UDestroyedIslandsSubsystem* Subsystem = GetGameInstance()->GetSubsystem<UDestroyedIslandsSubsystem>())
		{
			Subsystem->RegisterDestroyedIsland(IslandID);
		}
	}

	// Hide mesh and disable collision
	IslandMesh->SetVisibility(false);
	IslandMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Spawn VFX/debris actor
	if (DestroyedEffectClass)
	{
		GetWorld()->SpawnActor<AActor>(DestroyedEffectClass, GetActorLocation(), GetActorRotation());
	}

	// Broadcast destruction event
	OnIslandDestroyed.Broadcast(this, Destroyer);

	UE_LOG(LogTemp, Warning, TEXT("DestructibleIsland [%s]: DESTROYED by %s"),
		*IslandID.ToString(), Destroyer ? *Destroyer->GetName() : TEXT("Unknown"));
}
