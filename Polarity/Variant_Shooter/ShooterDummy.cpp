// ShooterDummy.cpp
// Training dummy implementation

#include "ShooterDummy.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "TimerManager.h"
#include "DamageTypes/DamageType_Melee.h"
#include "DamageTypes/DamageType_Ranged.h"

AShooterDummy::AShooterDummy()
{
	PrimaryActorTick.bCanEverTick = false;

	// Create hitbox capsule as root
	HitboxComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Hitbox"));
	HitboxComponent->InitCapsuleSize(HitboxRadius, HitboxHalfHeight);
	HitboxComponent->SetCollisionProfileName(TEXT("Pawn"));
	HitboxComponent->SetGenerateOverlapEvents(true);
	RootComponent = HitboxComponent;

	// Create visual mesh
	DummyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DummyMesh"));
	DummyMesh->SetupAttachment(RootComponent);
	DummyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AShooterDummy::BeginPlay()
{
	Super::BeginPlay();

	// Initialize HP
	CurrentHP = MaxHP;

	// Apply hitbox size from properties
	UpdateHitboxSize();
}

float AShooterDummy::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (bIsDead)
	{
		return 0.0f;
	}

	// Check damage type filtering
	if (DamageEvent.DamageTypeClass)
	{
		// Block melee damage if disabled
		if (DamageEvent.DamageTypeClass->IsChildOf(UDamageType_Melee::StaticClass()) && !bCanBeHitByMelee)
		{
			return 0.0f;
		}

		// Block ranged damage if disabled
		if (DamageEvent.DamageTypeClass->IsChildOf(UDamageType_Ranged::StaticClass()) && !bCanBeHitByRanged)
		{
			return 0.0f;
		}
	}

	const float ActualDamage = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);

	if (ActualDamage > 0.0f)
	{
		// Apply damage
		CurrentHP = FMath::Max(0.0f, CurrentHP - ActualDamage);

		// Play impact sound
		PlayImpactSound();

		// Broadcast damage event
		OnDummyDamaged.Broadcast(this, ActualDamage, DamageCauser);

		// Check for death
		if (CurrentHP <= 0.0f)
		{
			Die(DamageCauser);
		}
	}

	return ActualDamage;
}

// ==================== IShooterDummyTarget Interface ====================

bool AShooterDummy::GrantsStableCharge_Implementation() const
{
	return bGrantsStableCharge;
}

float AShooterDummy::GetStableChargeAmount_Implementation() const
{
	return StableChargePerHit;
}

float AShooterDummy::GetKillChargeBonus_Implementation() const
{
	return KillChargeBonus;
}

bool AShooterDummy::IsDummyDead_Implementation() const
{
	return bIsDead;
}

// ==================== Public API ====================

void AShooterDummy::ResetHealth()
{
	CurrentHP = MaxHP;
	bIsDead = false;

	// Re-enable collision
	if (HitboxComponent)
	{
		HitboxComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	// Show mesh
	if (DummyMesh)
	{
		DummyMesh->SetVisibility(true);
	}
}

void AShooterDummy::SetHitboxSize(float NewRadius, float NewHalfHeight)
{
	HitboxRadius = FMath::Max(10.0f, NewRadius);
	HitboxHalfHeight = FMath::Max(10.0f, NewHalfHeight);
	UpdateHitboxSize();
}

// ==================== Protected ====================

void AShooterDummy::Die(AActor* Killer)
{
	if (bIsDead)
	{
		return;
	}

	bIsDead = true;

	// Play death effects
	PlayDeathSound();
	SpawnDeathVFX();

	// Disable collision
	if (HitboxComponent)
	{
		HitboxComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// Hide mesh (or play death animation in Blueprint)
	if (DummyMesh)
	{
		DummyMesh->SetVisibility(false);
	}

	// Broadcast death event for Level Blueprint
	OnDummyDeath.Broadcast(this, Killer);

	// Schedule respawn if enabled
	if (bRespawnAfterDeath && RespawnDelay > 0.0f)
	{
		GetWorld()->GetTimerManager().SetTimer(
			RespawnTimer,
			this,
			&AShooterDummy::Respawn,
			RespawnDelay,
			false
		);
	}
	else if (bRespawnAfterDeath)
	{
		// Immediate respawn
		Respawn();
	}
}

void AShooterDummy::Respawn()
{
	ResetHealth();
	PlayRespawnSound();
	SpawnRespawnVFX();
}

void AShooterDummy::PlayImpactSound()
{
	if (ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			ImpactSound,
			GetActorLocation(),
			FRotator::ZeroRotator,
			ImpactSoundVolume
		);
	}
}

void AShooterDummy::PlayDeathSound()
{
	if (DeathSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			DeathSound,
			GetActorLocation(),
			FRotator::ZeroRotator,
			DeathSoundVolume
		);
	}
}

void AShooterDummy::PlayRespawnSound()
{
	if (RespawnSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			RespawnSound,
			GetActorLocation()
		);
	}
}

void AShooterDummy::UpdateHitboxSize()
{
	if (HitboxComponent)
	{
		HitboxComponent->SetCapsuleSize(HitboxRadius, HitboxHalfHeight);
	}
}

void AShooterDummy::SpawnDeathVFX()
{
	if (DeathVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this,
			DeathVFX,
			GetActorLocation(),
			GetActorRotation(),
			DeathVFXScale,
			true,
			true,
			ENCPoolMethod::None,
			true
		);
	}
}

void AShooterDummy::SpawnRespawnVFX()
{
	if (RespawnVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			this,
			RespawnVFX,
			GetActorLocation(),
			GetActorRotation(),
			FVector(1.0f),
			true,
			true,
			ENCPoolMethod::None,
			true
		);
	}
}