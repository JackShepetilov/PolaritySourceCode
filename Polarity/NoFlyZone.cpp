// NoFlyZone.cpp

#include "NoFlyZone.h"
#include "Components/BoxComponent.h"

TArray<TWeakObjectPtr<ANoFlyZone>> ANoFlyZone::ActiveZones;

ANoFlyZone::ANoFlyZone()
{
	PrimaryActorTick.bCanEverTick = false;

	ZoneBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ZoneBox"));
	RootComponent = ZoneBox;
	ZoneBox->SetBoxExtent(FVector(500.0f, 500.0f, 300.0f));
	ZoneBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ZoneBox->SetCanEverAffectNavigation(false);

#if WITH_EDITORONLY_DATA
	ZoneBox->SetLineThickness(3.0f);
	ZoneBox->ShapeColor = FColor::Red;
#endif
}

void ANoFlyZone::BeginPlay()
{
	Super::BeginPlay();
	ActiveZones.AddUnique(this);
}

void ANoFlyZone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ActiveZones.Remove(this);
	Super::EndPlay(EndPlayReason);
}

bool ANoFlyZone::IsPointInNoFlyZone(const UObject* WorldContextObject, const FVector& Point)
{
	for (int32 i = ActiveZones.Num() - 1; i >= 0; --i)
	{
		ANoFlyZone* Zone = ActiveZones[i].Get();
		if (!Zone)
		{
			ActiveZones.RemoveAt(i);
			continue;
		}

		const UBoxComponent* Box = Zone->ZoneBox;
		if (!Box)
		{
			continue;
		}

		// Transform point to local space and check against box extents
		const FVector LocalPoint = Box->GetComponentTransform().InverseTransformPosition(Point);
		const FVector Extent = Box->GetUnscaledBoxExtent();

		if (FMath::Abs(LocalPoint.X) <= Extent.X &&
			FMath::Abs(LocalPoint.Y) <= Extent.Y &&
			FMath::Abs(LocalPoint.Z) <= Extent.Z)
		{
			return true;
		}
	}

	return false;
}
