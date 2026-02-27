// HealthPickup.cpp
// HP pickup that spawns on non-weapon NPC kills and magnetically flies to the player

#include "HealthPickup.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/DamageTypes/DamageType_Ranged.h"
#include "Variant_Shooter/DamageTypes/DamageType_EMFWeapon.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"

AHealthPickup::AHealthPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	// Pickup collision (small sphere for actual collection)
	PickupCollision = CreateDefaultSubobject<USphereComponent>(TEXT("PickupCollision"));
	SetRootComponent(PickupCollision);
	PickupCollision->SetSphereRadius(50.0f);
	PickupCollision->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	PickupCollision->SetGenerateOverlapEvents(true);

	// Visual mesh
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(PickupCollision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Magnet trigger (large sphere for attraction)
	MagnetTrigger = CreateDefaultSubobject<USphereComponent>(TEXT("MagnetTrigger"));
	MagnetTrigger->SetupAttachment(PickupCollision);
	MagnetTrigger->SetSphereRadius(500.0f);
	MagnetTrigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	MagnetTrigger->SetGenerateOverlapEvents(true);
}

void AHealthPickup::BeginPlay()
{
	Super::BeginPlay();

	// Sync magnet trigger radius with property
	MagnetTrigger->SetSphereRadius(MagnetRadius);

	// Bind overlap events
	PickupCollision->OnComponentBeginOverlap.AddDynamic(this, &AHealthPickup::OnPickupOverlap);
	MagnetTrigger->OnComponentBeginOverlap.AddDynamic(this, &AHealthPickup::OnMagnetOverlap);

	// Start lifetime timer
	GetWorld()->GetTimerManager().SetTimer(
		LifetimeTimer, this, &AHealthPickup::OnLifetimeExpired, Lifetime, false);
}

void AHealthPickup::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!MagnetTarget.IsValid())
	{
		return;
	}

	// Track elapsed time since magnet activation (reuse CurrentVelocity.X as timer)
	CurrentVelocity.X += DeltaTime;

	// Direct pursuit: always move straight toward the player, no inertia
	const FVector TargetLocation = MagnetTarget->GetActorLocation();
	const FVector ToTarget = TargetLocation - GetActorLocation();
	const float Distance = ToTarget.Size();

	if (Distance < 1.0f)
	{
		return;
	}

	// Speed ramps up over time: starts slow, reaches MagnetSpeed after ~0.5s
	const float SpeedAlpha = FMath::Clamp(CurrentVelocity.X * MagnetAcceleration / MagnetSpeed, 0.0f, 1.0f);
	const float CurrentSpeed = FMath::Lerp(MagnetSpeed * 0.1f, MagnetSpeed, SpeedAlpha * SpeedAlpha);

	// Move directly toward player, clamped to not overshoot
	const float MoveDistance = FMath::Min(CurrentSpeed * DeltaTime, Distance);
	const FVector NewLocation = GetActorLocation() + (ToTarget / Distance) * MoveDistance;
	SetActorLocation(NewLocation);
}

// ==================== Overlap Callbacks ====================

void AHealthPickup::OnMagnetOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (MagnetTarget.IsValid())
	{
		return; // Already tracking
	}

	AShooterCharacter* Player = Cast<AShooterCharacter>(OtherActor);
	if (Player && !Player->IsDead())
	{
		MagnetTarget = Player;
	}
}

void AHealthPickup::OnPickupOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	AShooterCharacter* Player = Cast<AShooterCharacter>(OtherActor);
	if (!Player || Player->IsDead())
	{
		return;
	}

	// Restore health
	Player->RestoreHealth(HealAmount);

	// Effects
	if (PickupSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, PickupSound, GetActorLocation());
	}

	if (PickupVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), PickupVFX, GetActorLocation(),
			FRotator::ZeroRotator, FVector::OneVector,
			true, true, ENCPoolMethod::None);
	}

	Destroy();
}

// ==================== Lifetime ====================

void AHealthPickup::OnLifetimeExpired()
{
	Destroy();
}

// ==================== Static Helpers ====================

bool AHealthPickup::ShouldDropHealth(TSubclassOf<UDamageType> KillingDamageType)
{
	// No damage type info (e.g. wallslam self-damage) — still drop
	if (!KillingDamageType)
	{
		return true;
	}

	// Weapon kills don't drop: Ranged (rifle) and EMFWeapon (charger)
	if (KillingDamageType->IsChildOf(UDamageType_Ranged::StaticClass()))
	{
		return false;
	}

	if (KillingDamageType->IsChildOf(UDamageType_EMFWeapon::StaticClass()))
	{
		return false;
	}

	// Everything else drops: Melee, Wallslam, Dropkick, MomentumBonus, EMFProximity,
	// base UDamageType (drone explosion), prop damage, etc.
	return true;
}
