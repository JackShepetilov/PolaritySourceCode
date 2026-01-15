// Copyright Epic Games, Inc. All Rights Reserved.

#include "DamageVolume.h"
#include "GameFramework/Pawn.h"
#include "Engine/DamageEvents.h"
#include "TimerManager.h"

ADamageVolume::ADamageVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	DamageBox = CreateDefaultSubobject<UBoxComponent>(TEXT("DamageBox"));
	RootComponent = DamageBox;

	DamageBox->SetBoxExtent(FVector(200.0f, 200.0f, 50.0f));
	DamageBox->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	DamageBox->SetGenerateOverlapEvents(true);
}

void ADamageVolume::BeginPlay()
{
	Super::BeginPlay();

	DamageBox->OnComponentBeginOverlap.AddDynamic(this, &ADamageVolume::OnBeginOverlap);
	DamageBox->OnComponentEndOverlap.AddDynamic(this, &ADamageVolume::OnEndOverlap);

	// Start damage timer
	if (bDamageEnabled)
	{
		GetWorldTimerManager().SetTimer(DamageTimerHandle, this, &ADamageVolume::DealDamage, DamageInterval, true);
	}
}

void ADamageVolume::OnBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor || OtherActor == this)
	{
		return;
	}

	// Only damage pawns
	if (!Cast<APawn>(OtherActor))
	{
		return;
	}

	OverlappingActors.AddUnique(OtherActor);
}

void ADamageVolume::OnEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	OverlappingActors.Remove(OtherActor);
}

void ADamageVolume::DealDamage()
{
	if (!bDamageEnabled)
	{
		return;
	}

	// Clean up invalid references and deal damage
	for (int32 i = OverlappingActors.Num() - 1; i >= 0; --i)
	{
		AActor* Actor = OverlappingActors[i].Get();
		if (!Actor)
		{
			OverlappingActors.RemoveAt(i);
			continue;
		}

		// Create damage event
		FDamageEvent DamageEvent;
		if (DamageTypeClass)
		{
			DamageEvent.DamageTypeClass = DamageTypeClass;
		}

		// Apply damage - this calls ShooterCharacter::TakeDamage
		Actor->TakeDamage(DamagePerTick, DamageEvent, nullptr, this);
	}
}

void ADamageVolume::SetDamageEnabled(bool bEnabled)
{
	bDamageEnabled = bEnabled;

	if (bEnabled && !DamageTimerHandle.IsValid())
	{
		GetWorldTimerManager().SetTimer(DamageTimerHandle, this, &ADamageVolume::DealDamage, DamageInterval, true);
	}
	else if (!bEnabled && DamageTimerHandle.IsValid())
	{
		GetWorldTimerManager().ClearTimer(DamageTimerHandle);
	}
}