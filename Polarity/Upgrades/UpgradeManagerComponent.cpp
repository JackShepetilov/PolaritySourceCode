// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "UpgradeManagerComponent.h"
#include "UpgradeDefinition.h"
#include "UpgradeComponent.h"
#include "UpgradeRegistry.h"
#include "ShooterCharacter.h"
#include "ShooterWeapon.h"

UUpgradeManagerComponent::UUpgradeManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UUpgradeManagerComponent::BeginPlay()
{
	Super::BeginPlay();

	// Bind to the character's initial weapon (if already equipped)
	if (AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwner()))
	{
		if (AShooterWeapon* Weapon = Character->GetCurrentWeapon())
		{
			BindToWeapon(Weapon);
		}
	}
}

bool UUpgradeManagerComponent::GrantUpgrade(UUpgradeDefinition* Definition)
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

	// Already owned? Try to level up.
	if (TObjectPtr<UUpgradeComponent>* ExistingPtr = ActiveUpgrades.Find(Definition->UpgradeTag))
	{
		UUpgradeComponent* Existing = ExistingPtr->Get();
		if (!Existing)
		{
			UE_LOG(LogTemp, Warning, TEXT("UpgradeManager: '%s' tracked but component is null — clearing"), *Definition->DisplayName.ToString());
			ActiveUpgrades.Remove(Definition->UpgradeTag);
			// fall through to normal grant below
		}
		else
		{
			if (Existing->CurrentLevel >= Definition->MaxLevel)
			{
				UE_LOG(LogTemp, Log, TEXT("UpgradeManager: '%s' already at max level %d/%d"),
					*Definition->DisplayName.ToString(), Existing->CurrentLevel, Definition->MaxLevel);
				return false;
			}

			const int32 OldLevel = Existing->CurrentLevel;
			Existing->CurrentLevel = OldLevel + 1;
			Existing->OnLevelChanged(OldLevel, Existing->CurrentLevel);

			UE_LOG(LogTemp, Log, TEXT("UpgradeManager: '%s' levelled up %d -> %d"),
				*Definition->DisplayName.ToString(), OldLevel, Existing->CurrentLevel);

			OnUpgradeLeveledUp.Broadcast(Definition, Existing->CurrentLevel);
			return true;
		}
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
	NewComponent->CurrentLevel = 1;
	NewComponent->RegisterComponent();

	// Track it
	ActiveUpgrades.Add(Definition->UpgradeTag, NewComponent);

	// Activate the upgrade logic (handles level-1 setup)
	NewComponent->OnUpgradeActivated();

	UE_LOG(LogTemp, Log, TEXT("UpgradeManager: Granted upgrade '%s' (Lv 1/%d)"),
		*Definition->DisplayName.ToString(), Definition->MaxLevel);

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
	UUpgradeDefinition* Definition = Component->UpgradeDefinition;

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

int32 UUpgradeManagerComponent::GetUpgradeLevel(FGameplayTag UpgradeTag) const
{
	if (const TObjectPtr<UUpgradeComponent>* Found = ActiveUpgrades.Find(UpgradeTag))
	{
		if (UUpgradeComponent* Component = Found->Get())
		{
			return Component->CurrentLevel;
		}
	}
	return 0;
}

bool UUpgradeManagerComponent::IsUpgradeMaxedOut(UUpgradeDefinition* Definition) const
{
	if (!Definition) return false;
	if (const TObjectPtr<UUpgradeComponent>* Found = ActiveUpgrades.Find(Definition->UpgradeTag))
	{
		if (UUpgradeComponent* Component = Found->Get())
		{
			return Component->CurrentLevel >= Definition->MaxLevel;
		}
	}
	return false; // Not owned at all — still grantable
}

TArray<UUpgradeDefinition*> UUpgradeManagerComponent::GetAcquiredUpgrades() const
{
	TArray<UUpgradeDefinition*> Result;
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
			UUpgradeDefinition* Definition = Registry->FindByTag(Tag);
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
	// Rebind OnShotFired to the new weapon
	UnbindFromWeapon();
	if (NewWeapon)
	{
		BindToWeapon(NewWeapon);
	}

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

void UUpgradeManagerComponent::NotifyHealthPickupCollectedAtFullHP()
{
	// Step 1: try to top up the shared pool. If at cap, nothing is stored — but
	// upgrades still get the hook for legacy/VFX-only behaviours.
	AddStoredHealthPickup();

	// Step 2: notify each active upgrade so it can react (e.g. play "stored" VFX).
	for (auto& Pair : ActiveUpgrades)
	{
		if (Pair.Value)
		{
			Pair.Value->OnHealthPickupCollectedAtFullHP();
		}
	}
}

bool UUpgradeManagerComponent::AddStoredHealthPickup()
{
	if (StoredHealthPickups >= MaxStoredHealthPickups)
	{
		return false;
	}

	StoredHealthPickups++;
	OnStoredHealthPickupsChanged.Broadcast(StoredHealthPickups, MaxStoredHealthPickups);

	UE_LOG(LogTemp, Log, TEXT("[UPGRADE_POOL] Stored health pickup: %d/%d"),
		StoredHealthPickups, MaxStoredHealthPickups);

	return true;
}

int32 UUpgradeManagerComponent::ConsumeStoredHealthPickups(int32 RequestedCount)
{
	if (RequestedCount <= 0 || StoredHealthPickups <= 0)
	{
		return 0;
	}

	const int32 Consumed = FMath::Min(RequestedCount, StoredHealthPickups);
	StoredHealthPickups -= Consumed;
	OnStoredHealthPickupsChanged.Broadcast(StoredHealthPickups, MaxStoredHealthPickups);

	UE_LOG(LogTemp, Log, TEXT("[UPGRADE_POOL] Consumed %d pickups (%d remaining)"),
		Consumed, StoredHealthPickups);

	return Consumed;
}

void UUpgradeManagerComponent::ResetStoredHealthPickups()
{
	if (StoredHealthPickups == 0)
	{
		return;
	}

	StoredHealthPickups = 0;
	OnStoredHealthPickupsChanged.Broadcast(StoredHealthPickups, MaxStoredHealthPickups);
}

float UUpgradeManagerComponent::GetCombinedDamageMultiplier(AActor* Target) const
{
	float Combined = 1.0f;

	for (const auto& Pair : ActiveUpgrades)
	{
		if (Pair.Value)
		{
			Combined *= Pair.Value->GetDamageMultiplier(Target);
		}
	}

	return Combined;
}

void UUpgradeManagerComponent::BindToWeapon(AShooterWeapon* Weapon)
{
	if (!Weapon)
	{
		return;
	}

	BoundWeapon = Weapon;
	Weapon->OnShotFired.AddDynamic(this, &UUpgradeManagerComponent::OnWeaponShotFiredCallback);
}

void UUpgradeManagerComponent::UnbindFromWeapon()
{
	if (AShooterWeapon* Weapon = BoundWeapon.Get())
	{
		Weapon->OnShotFired.RemoveDynamic(this, &UUpgradeManagerComponent::OnWeaponShotFiredCallback);
	}
	BoundWeapon.Reset();
}

void UUpgradeManagerComponent::OnWeaponShotFiredCallback()
{
	NotifyWeaponFired();
}
