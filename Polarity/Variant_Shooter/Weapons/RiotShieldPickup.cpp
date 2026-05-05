// RiotShieldPickup.cpp

#include "RiotShieldPickup.h"
#include "RiotShield.h"
#include "ShooterCharacter.h"

#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

ARiotShieldPickup::ARiotShieldPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	SphereCollision = CreateDefaultSubobject<USphereComponent>(TEXT("SphereCollision"));
	SphereCollision->SetupAttachment(RootComponent);
	SphereCollision->SetSphereRadius(60.0f);
	SphereCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SphereCollision->SetCollisionObjectType(ECC_WorldDynamic);
	SphereCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	SphereCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	SphereCollision->OnComponentBeginOverlap.AddDynamic(this, &ARiotShieldPickup::OnOverlap);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionProfileName(FName("PhysicsActor"));
	Mesh->SetSimulatePhysics(false); // toggled to true in SpawnAsThrown
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
}

void ARiotShieldPickup::BeginPlay()
{
	Super::BeginPlay();
}

void ARiotShieldPickup::SpawnAsThrown(const FVector& WorldLinearImpulse, const FVector& AngularImpulseDeg)
{
	bThrownMode = true;
	TimeUntilReacquireEnabled = ReacquireDelay;
	// GetOwner() was set in SpawnActor params (= the thrower) — used in OnOverlap to block re-pickup.

	if (Mesh)
	{
		Mesh->SetSimulatePhysics(true);
		// Allow the thrown shield to bonk NPCs on impact.
		Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Mesh->AddImpulse(WorldLinearImpulse, NAME_None, true);
		Mesh->AddAngularImpulseInDegrees(AngularImpulseDeg, NAME_None, true);
	}
}

void ARiotShieldPickup::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (TimeUntilReacquireEnabled > 0.0f)
	{
		TimeUntilReacquireEnabled = FMath::Max(0.0f, TimeUntilReacquireEnabled - DeltaTime);
	}
}

void ARiotShieldPickup::OnOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	AShooterCharacter* Char = Cast<AShooterCharacter>(OtherActor);
	if (!Char)
	{
		return;
	}
	if (Char->HasShield())
	{
		return; // already carrying one — ignore (swap behavior is a future enhancement)
	}
	// Lock out the thrower until the reacquire timer elapses (bumping into your own thrown shield
	// or it bouncing back at you within ReacquireDelay seconds doesn't pick it up).
	// GetOwner() was set to the throwing character in SpawnActor params.
	if (TimeUntilReacquireEnabled > 0.0f && OtherActor == GetOwner())
	{
		return;
	}
	if (!ShieldClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SHIELD] Pickup %s has no ShieldClass set"), *GetName());
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = Char;

	ARiotShield* NewShield = World->SpawnActor<ARiotShield>(ShieldClass, Char->GetActorLocation(), Char->GetActorRotation(), Params);
	if (NewShield)
	{
		Char->EquipShield(NewShield);
	}

	Destroy();
}
