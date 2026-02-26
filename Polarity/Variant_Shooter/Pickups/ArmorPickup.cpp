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

	// Fly toward the player with acceleration
	const FVector TargetLocation = MagnetTarget->GetActorLocation();
	const FVector Direction = (TargetLocation - GetActorLocation()).GetSafeNormal();

	// Accelerate toward target
	CurrentVelocity += Direction * MagnetAcceleration * DeltaTime;

	// Clamp to max speed
	if (CurrentVelocity.Size() > MagnetSpeed)
	{
		CurrentVelocity = CurrentVelocity.GetSafeNormal() * MagnetSpeed;
	}

	// Move
	const FVector NewLocation = GetActorLocation() + CurrentVelocity * DeltaTime;
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
