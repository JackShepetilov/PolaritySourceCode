// BenchGrenadeRack.cpp

#include "Variant_Shooter/Bench/BenchGrenadeRack.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Variant_Shooter/Grenades/GrenadeDefinition.h"
#include "Variant_Shooter/Pickups/GrenadePickup.h"

ABenchGrenadeRack::ABenchGrenadeRack()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(SceneRoot);
	Label->SetRelativeLocation(FVector(0.0f, 0.0f, 35.0f));
	Label->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextBottom);
	Label->SetWorldSize(10.0f);
	Label->SetTextRenderColor(FColor(255, 220, 140));
	Label->SetText(FText::FromString(TEXT("GRENADE")));
}

void ABenchGrenadeRack::BeginPlay()
{
	Super::BeginPlay();

	if (Label && GrenadeType)
	{
		const FString Name = GrenadeType->DisplayName.IsEmpty() ? GrenadeType->GetName() : GrenadeType->DisplayName.ToString();
		Label->SetText(FText::FromString(FString::Printf(TEXT("%s x%d"), *Name, Count)));
	}

	if (HasAuthority() && GrenadeType)
	{
		Refill();
		GetWorldTimerManager().SetTimer(RefillTimer, this, &ABenchGrenadeRack::Refill, RefillInterval, true);
	}
}

void ABenchGrenadeRack::Refill()
{
	// Still lying here whole: nothing to do. A pickup that was taken is hidden or gone; one somebody
	// only took part of stays, and is topped up by the next one once it is gone.
	const AGrenadePickup* const Current = Pickup.Get();
	if (IsValid(Current) && !Current->IsHidden())
	{
		return;
	}

	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	// The type and the count are set before BeginPlay: the pickup builds what it offers there.
	const FTransform Transform = GetActorTransform();
	AGrenadePickup* const Fresh = World->SpawnActorDeferred<AGrenadePickup>(AGrenadePickup::StaticClass(), Transform, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Fresh)
	{
		return;
	}
	Fresh->Type = GrenadeType;
	Fresh->Count = Count;
	Fresh->FinishSpawning(Transform);

	if (UStaticMeshComponent* const MeshComp = Fresh->FindComponentByClass<UStaticMeshComponent>())
	{
		if (PickupMesh)
		{
			MeshComp->SetStaticMesh(PickupMesh);
		}
		MeshComp->SetRelativeScale3D(PickupMeshScale);
		MeshComp->SetVisibility(true);
	}
	Pickup = Fresh;
}
