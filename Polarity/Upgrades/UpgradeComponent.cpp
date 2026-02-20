// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "UpgradeComponent.h"
#include "ShooterCharacter.h"
#include "ShooterWeapon.h"

UUpgradeComponent::UUpgradeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = false;
}

AShooterCharacter* UUpgradeComponent::GetShooterCharacter() const
{
	if (CachedOwner.IsValid())
	{
		return CachedOwner.Get();
	}
	return Cast<AShooterCharacter>(GetOwner());
}

AShooterWeapon* UUpgradeComponent::GetCurrentWeapon() const
{
	if (AShooterCharacter* Character = GetShooterCharacter())
	{
		return Character->GetCurrentWeapon();
	}
	return nullptr;
}

void UUpgradeComponent::BeginPlay()
{
	Super::BeginPlay();
	CachedOwner = Cast<AShooterCharacter>(GetOwner());
}

void UUpgradeComponent::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	CachedOwner.Reset();
}

void UUpgradeComponent::OnUpgradeActivated()
{
	// Override in subclasses
}

void UUpgradeComponent::OnUpgradeDeactivated()
{
	// Override in subclasses
}
