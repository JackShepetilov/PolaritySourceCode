// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "UpgradeManagerComponent.h"
#include "UpgradeDefinition.h"
#include "UpgradeComponent.h"
#include "UpgradeRegistry.h"

UUpgradeManagerComponent::UUpgradeManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UUpgradeManagerComponent::GrantUpgrade(const UUpgradeDefinition* Definition)
{
	if (!Definition)
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradeManager: GrantUpgrade called with null definition"));
		return false;
	}

	if (!Definition->UpgradeTag.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradeManager: GrantUpgrade called with invalid tag on '%s'"), *Definition->DisplayName.ToString());
		return false;
	}

	// Already owned?
	if (ActiveUpgrades.Contains(Definition->UpgradeTag))
	{
		UE_LOG(LogTemp, Log, TEXT("UpgradeManager: Already has upgrade '%s'"), *Definition->DisplayName.ToString());
		return false;
	}

	if (!Definition->ComponentClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradeManager: No ComponentClass set on upgrade '%s'"), *Definition->DisplayName.ToString());
		return false;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	// Create the upgrade component dynamically
	UUpgradeComponent* NewComponent = NewObject<UUpgradeComponent>(Owner, Definition->ComponentClass);
	if (!NewComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeManager: Failed to create component for upgrade '%s'"), *Definition->DisplayName.ToString());
		return false;
	}

	NewComponent->UpgradeDefinition = Definition;
	NewComponent->RegisterComponent();

	// Track it
	ActiveUpgrades.Add(Definition->UpgradeTag, NewComponent);

	// Activate the upgrade logic
	NewComponent->OnUpgradeActivated();

	UE_LOG(LogTemp, Log, TEXT("UpgradeManager: Granted upgrade '%s'"), *Definition->DisplayName.ToString());

	// Broadcast
	OnUpgradeGranted.Broadcast(Definition);

	return true;
}

bool UUpgradeManagerComponent::RemoveUpgrade(FGameplayTag UpgradeTag)
{
	TObjectPtr<UUpgradeComponent>* Found = ActiveUpgrades.Find(UpgradeTag);
	if (!Found || !(*Found))
	{
		return false;
	}

	UUpgradeComponent* Component = *Found;
	const UUpgradeDefinition* Definition = Component->UpgradeDefinition;

	// Deactivate
	Component->OnUpgradeDeactivated();

	// Remove and destroy
	ActiveUpgrades.Remove(UpgradeTag);
	Component->DestroyComponent();

	UE_LOG(LogTemp, Log, TEXT("UpgradeManager: Removed upgrade '%s'"), *Definition->DisplayName.ToString());

	// Broadcast
	OnUpgradeRemoved.Broadcast(Definition);

	return true;
}

bool UUpgradeManagerComponent::HasUpgrade(FGameplayTag UpgradeTag) const
{
	return ActiveUpgrades.Contains(UpgradeTag);
}

TArray<const UUpgradeDefinition*> UUpgradeManagerComponent::GetAcquiredUpgrades() const
{
	TArray<const UUpgradeDefinition*> Result;
	Result.Reserve(ActiveUpgrades.Num());

	for (const auto& Pair : ActiveUpgrades)
	{
		if (Pair.Value)
		{
			Result.Add(Pair.Value->UpgradeDefinition);
		}
	}

	return Result;
}

UUpgradeComponent* UUpgradeManagerComponent::GetUpgradeComponent(FGameplayTag UpgradeTag) const
{
	const TObjectPtr<UUpgradeComponent>* Found = ActiveUpgrades.Find(UpgradeTag);
	return Found ? Found->Get() : nullptr;
}

TArray<FGameplayTag> UUpgradeManagerComponent::GetUpgradeTagsForSave() const
{
	TArray<FGameplayTag> Tags;
	Tags.Reserve(ActiveUpgrades.Num());

	for (const auto& Pair : ActiveUpgrades)
	{
		Tags.Add(Pair.Key);
	}

	return Tags;
}

void UUpgradeManagerComponent::RestoreUpgradesFromTags(const TArray<FGameplayTag>& Tags, const UUpgradeRegistry* Registry)
{
	if (!Registry)
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradeManager: RestoreUpgradesFromTags called with null registry"));
		return;
	}

	// Remove any upgrades that aren't in the saved tags
	TArray<FGameplayTag> CurrentTags;
	ActiveUpgrades.GetKeys(CurrentTags);

	for (const FGameplayTag& Tag : CurrentTags)
	{
		if (!Tags.Contains(Tag))
		{
			RemoveUpgrade(Tag);
		}
	}

	// Grant any upgrades from saved tags that we don't have yet
	for (const FGameplayTag& Tag : Tags)
	{
		if (!ActiveUpgrades.Contains(Tag))
		{
			const UUpgradeDefinition* Definition = Registry->FindByTag(Tag);
			if (Definition)
			{
				GrantUpgrade(Definition);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("UpgradeManager: Could not find definition for saved tag '%s'"), *Tag.ToString());
			}
		}
	}
}

void UUpgradeManagerComponent::NotifyWeaponFired()
{
	for (auto& Pair : ActiveUpgrades)
	{
		if (Pair.Value)
		{
			Pair.Value->OnWeaponFired();
		}
	}
}

void UUpgradeManagerComponent::NotifyWeaponChanged(AShooterWeapon* OldWeapon, AShooterWeapon* NewWeapon)
{
	for (auto& Pair : ActiveUpgrades)
	{
		if (Pair.Value)
		{
			Pair.Value->OnWeaponChanged(OldWeapon, NewWeapon);
		}
	}
}

void UUpgradeManagerComponent::NotifyOwnerTookDamage(float Damage, AActor* DamageCauser)
{
	for (auto& Pair : ActiveUpgrades)
	{
		if (Pair.Value)
		{
			Pair.Value->OnOwnerTookDamage(Damage, DamageCauser);
		}
	}
}

void UUpgradeManagerComponent::NotifyOwnerDealtDamage(AActor* Target, float Damage, bool bKilled)
{
	for (auto& Pair : ActiveUpgrades)
	{
		if (Pair.Value)
		{
			Pair.Value->OnOwnerDealtDamage(Target, Damage, bKilled);
		}
	}
}
