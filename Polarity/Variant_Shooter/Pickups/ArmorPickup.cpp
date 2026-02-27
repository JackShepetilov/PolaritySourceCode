// ArmorPickup.cpp
// Armor pickup that spawns on channeling kills and magnetically flies to the player

#include "ArmorPickup.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"

AArmorPickup::AArmorPickup()
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

void AArmorPickup::BeginPlay()
{
	Super::BeginPlay();

	// Sync magnet trigger radius with property
	MagnetTrigger->SetSphereRadius(MagnetRadius);

	// Bind overlap events
	PickupCollision->OnComponentBeginOverlap.AddDynamic(this, &AArmorPickup::OnPickupOverlap);
	MagnetTrigger->OnComponentBeginOverlap.AddDynamic(this, &AArmorPickup::OnMagnetOverlap);

	// Start lifetime timer
	GetWorld()->GetTimerManager().SetTimer(
		LifetimeTimer, this, &AArmorPickup::OnLifetimeExpired, Lifetime, false);
}

void AArmorPickup::Tick(float DeltaTime)
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

void AArmorPickup::OnMagnetOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
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

void AArmorPickup::OnPickupOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	AShooterCharacter* Player = Cast<AShooterCharacter>(OtherActor);
	if (!Player || Player->IsDead())
	{
		return;
	}

	// Restore armor
	Player->RestoreArmor(ArmorAmount);

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

void AArmorPickup::OnLifetimeExpired()
{
	Destroy();
}

// ==================== Static Helpers ====================

bool AArmorPickup::ShouldDropArmor(const AShooterNPC* DyingNPC)
{
	if (!DyingNPC)
	{
		return false;
	}

	// Armor drops when NPC was ever captured/launched by channeling
	return DyingNPC->WasChannelingTarget();
}
