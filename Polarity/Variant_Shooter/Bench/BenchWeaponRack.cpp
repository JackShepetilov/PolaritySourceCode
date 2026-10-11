// BenchWeaponRack.cpp

#include "Variant_Shooter/Bench/BenchWeaponRack.h"

#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Variant_Shooter/Weapons/DroppedRangedWeapon.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

ABenchWeaponRack::ABenchWeaponRack()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(SceneRoot);
	Label->SetRelativeLocation(FVector(0.0f, 0.0f, 55.0f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextBottom);
	Label->SetWorldSize(14.0f);
	Label->SetTextRenderColor(FColor(160, 255, 160));
	Label->SetText(FText::FromString(TEXT("RACK")));
}

void ABenchWeaponRack::BeginPlay()
{
	Super::BeginPlay();

	if (Label)
	{
		FString Shown = Title;
		if (Shown.IsEmpty() && WeaponClass)
		{
			Shown = WeaponClass->GetName();
			Shown.RemoveFromStart(TEXT("BP_"));
			Shown.RemoveFromEnd(TEXT("_C"));
		}
		Label->SetText(FText::FromString(Shown));
	}

	if (HasAuthority() && WeaponClass)
	{
		Refill();
		GetWorldTimerManager().SetTimer(RefillTimer, this, &ABenchWeaponRack::Refill, RefillInterval, true);
	}
}

void ABenchWeaponRack::Refill()
{
	const ADroppedRangedWeapon* const Current = Drop.Get();
	if (Current && !Current->IsHidden()
		&& FVector::Dist(Current->GetActorLocation(), GetActorLocation()) <= TakenDistance)
	{
		return;
	}

	Drop = ADroppedRangedWeapon::SpawnFor(GetWorld(), WeaponClass, GetActorTransform());
}
