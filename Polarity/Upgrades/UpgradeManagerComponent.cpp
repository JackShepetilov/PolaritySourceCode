// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "UpgradeManagerComponent.h"
#include "UpgradeDefinition.h"
#include "UpgradeComponent.h"
#include "UpgradeRegistry.h"
#include "ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "Upgrades/Upgrade_Bandolier.h"
#include "Net/UnrealNetwork.h"

UUpgradeManagerComponent::UUpgradeManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// For the dispenser offer only: its cards replicate to the owner and its pick is an RPC.
	SetIsReplicatedByDefault(true);
}

void UUpgradeManagerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UUpgradeManagerComponent, PendingOffer, COND_OwnerOnly);
}

// ==================== Dispenser offers ====================

UUpgradeDefinition* UUpgradeManagerComponent::GetOwnedInSlot(EUpgradeSlot Slot) const
{
	if (Slot == EUpgradeSlot::None)
	{
		return nullptr;
	}
	for (const TPair<FGameplayTag, TObjectPtr<UUpgradeComponent>>& Pair : ActiveUpgrades)
	{
		UUpgradeDefinition* const Def = Pair.Value ? Pair.Value->UpgradeDefinition.Get() : nullptr;
		if (Def && Def->Slot == Slot)
		{
			return Def;
		}
	}
	return nullptr;
}

void UUpgradeManagerComponent::GatherOfferCandidates(const UUpgradeRegistry* Registry, int32 Wave, int32 LevelCap,
	TArray<FUpgradeOfferCard>& OutCandidates) const
{
	OutCandidates.Reset();
	if (!Registry)
	{
		return;
	}
	for (UUpgradeDefinition* const Def : Registry->AllUpgrades)
	{
		if (!Def || !Def->bInDispenserPool || !Def->UpgradeTag.IsValid() || !Def->ComponentClass || Def->MinWave > Wave)
		{
			continue;
		}

		// The level this upgrade may reach right now: its own ceiling, and the wave's.
		const int32 Cap = FMath::Min(Def->MaxLevel, FMath::Max(1, LevelCap));
		const int32 Owned = GetUpgradeLevel(Def->UpgradeTag);

		FUpgradeOfferCard Card;
		Card.Definition = Def;
		if (Owned > 0)
		{
			if (Owned >= Cap)
			{
				continue;
			}
			Card.Kind = EUpgradeOfferKind::LevelUp;
			Card.FromLevel = Owned;
		}
		else
		{
			if (OwnsConflicting(Def))
			{
				continue;
			}
			if (UUpgradeDefinition* const Holder = GetOwnedInSlot(Def->Slot))
			{
				Card.Kind = EUpgradeOfferKind::Replace;
				Card.Replaces = Holder;
			}
		}
		OutCandidates.Add(Card);
	}
}

void UUpgradeManagerComponent::BuildOffer(const UUpgradeRegistry* Registry, int32 Wave, int32 LevelCap,
	const TArray<EUpgradeRarity>& Rarities, TArray<FUpgradeOfferCard>& OutOffer) const
{
	OutOffer.Reset();
	TArray<FUpgradeOfferCard> Candidates;
	GatherOfferCandidates(Registry, Wave, LevelCap, Candidates);

	for (const EUpgradeRarity Rarity : Rarities)
	{
		if (Candidates.Num() == 0)
		{
			break;
		}

		// Weighted draw, and the drawn one leaves the bag: no upgrade twice in one offer.
		float Total = 0.0f;
		for (const FUpgradeOfferCard& Candidate : Candidates)
		{
			Total += FMath::Max(0.01f, Candidate.Definition->OfferWeight);
		}
		float Pick = FMath::FRand() * Total;
		int32 Chosen = Candidates.Num() - 1;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			Pick -= FMath::Max(0.01f, Candidates[Index].Definition->OfferWeight);
			if (Pick <= 0.0f)
			{
				Chosen = Index;
				break;
			}
		}
		FUpgradeOfferCard Card = Candidates[Chosen];
		Candidates.RemoveAtSwap(Chosen);

		// Rarity is levels: Common one, Rare two, Epic three, Legendary four, then clipped to what
		// the upgrade and the wave allow. A replacement comes at least one above what it replaces.
		Card.Rarity = Rarity;
		const int32 Levels = static_cast<int32>(Rarity) + 1;
		const int32 Cap = FMath::Min(Card.Definition->MaxLevel, FMath::Max(1, LevelCap));
		switch (Card.Kind)
		{
		case EUpgradeOfferKind::LevelUp:
			Card.ToLevel = FMath::Min(Cap, Card.FromLevel + Levels);
			break;
		case EUpgradeOfferKind::Replace:
		{
			const int32 ReplacedLevel = Card.Replaces ? GetUpgradeLevel(Card.Replaces->UpgradeTag) : 0;
			Card.ToLevel = FMath::Min(Cap, FMath::Max(Levels, ReplacedLevel + 1));
			break;
		}
		default:
			Card.ToLevel = FMath::Min(Cap, Levels);
			break;
		}
		OutOffer.Add(Card);
	}
}

void UUpgradeManagerComponent::SetPendingOffer(const TArray<FUpgradeOfferCard>& Offer)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}
	PendingOffer = Offer;
	OnOfferChanged.Broadcast();
}

void UUpgradeManagerComponent::OnRep_PendingOffer()
{
	OnOfferChanged.Broadcast();
}

void UUpgradeManagerComponent::Server_PickOffer_Implementation(int32 Index)
{
	if (!PendingOffer.IsValidIndex(Index))
	{
		UE_LOG(LogTemp, Log, TEXT("[CASINO_DEBUG] %s picked card %d of %d: no such card"), *GetNameSafe(GetOwner()), Index, PendingOffer.Num());
		return;
	}
	const FUpgradeOfferCard Card = PendingOffer[Index];
	PendingOffer.Reset();
	OnOfferChanged.Broadcast();

	const bool bApplied = ApplyOfferCard(Card);
	UE_LOG(LogTemp, Log, TEXT("[CASINO_DEBUG] %s took %s (kind %d, %s, Lv %d -> %d%s): %s"),
		*GetNameSafe(GetOwner()), Card.Definition ? *Card.Definition->DisplayName.ToString() : TEXT("?"),
		static_cast<int32>(Card.Kind), *UEnum::GetDisplayValueAsText(Card.Rarity).ToString(), Card.FromLevel, Card.ToLevel,
		Card.Replaces ? *FString::Printf(TEXT(", replaces %s"), *Card.Replaces->DisplayName.ToString()) : TEXT(""),
		bApplied ? TEXT("applied") : TEXT("FAILED"));

	// The owner's copy of the upgrade. On a listen host the owner is this very machine and the
	// server's grant above already is its copy.
	const APawn* const Pawn = Cast<APawn>(GetOwner());
	if (bApplied && Pawn && !Pawn->IsLocallyControlled())
	{
		Client_ApplyOfferCard(Card);
	}
}

void UUpgradeManagerComponent::Client_ApplyOfferCard_Implementation(const FUpgradeOfferCard& Card)
{
	if (GetOwnerRole() == ROLE_Authority)
	{
		return;
	}
	ApplyOfferCard(Card);
}

bool UUpgradeManagerComponent::ApplyOfferCard(const FUpgradeOfferCard& Card)
{
	UUpgradeDefinition* const Def = Card.Definition;
	if (!Def)
	{
		return false;
	}
	if (Card.Kind == EUpgradeOfferKind::Replace && Card.Replaces)
	{
		RemoveUpgrade(Card.Replaces->UpgradeTag);
	}
	// GrantUpgrade adds one level per call; the guard stops a definition that refuses to level.
	for (int32 Guard = 0; GetUpgradeLevel(Def->UpgradeTag) < Card.ToLevel && Guard < 16; ++Guard)
	{
		if (!GrantUpgrade(Def))
		{
			break;
		}
	}
	return GetUpgradeLevel(Def->UpgradeTag) > 0;
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

			UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] '%s' LEVEL_UP %d -> %d (tag=%s)"),
				*Definition->DisplayName.ToString(), OldLevel, Existing->CurrentLevel, *Definition->UpgradeTag.ToString());

			OnUpgradeLeveledUp.Broadcast(Definition, Existing->CurrentLevel);
			return true;
		}
	}

	// Brand-new grant: refuse it if it is mutually exclusive with an already-owned upgrade.
	// (Covers level-up choice, world pickups, and save-restore — all route through here.)
	if (OwnsConflicting(Definition))
	{
		UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] '%s' GRANT REFUSED — mutually exclusive with an owned upgrade"),
			*Definition->DisplayName.ToString());
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
	NewComponent->CurrentLevel = 1;
	NewComponent->RegisterComponent();

	// Track it
	ActiveUpgrades.Add(Definition->UpgradeTag, NewComponent);

	// Activate the upgrade logic (handles level-1 setup)
	NewComponent->OnUpgradeActivated();

	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] '%s' GRANTED Lv 1/%d (tag=%s, class=%s)"),
		*Definition->DisplayName.ToString(), Definition->MaxLevel,
		*Definition->UpgradeTag.ToString(),
		*Definition->ComponentClass->GetName());

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

bool UUpgradeManagerComponent::OwnsConflicting(const UUpgradeDefinition* Candidate) const
{
	if (!Candidate)
	{
		return false;
	}

	for (const auto& Pair : ActiveUpgrades)
	{
		const UUpgradeComponent* Comp = Pair.Value;
		if (!Comp)
		{
			continue;
		}
		const UUpgradeDefinition* Owned = Comp->UpgradeDefinition;
		if (!Owned || Owned == Candidate)
		{
			continue;
		}

		// Bidirectional — a conflict declared on either definition counts.
		if (Candidate->MutuallyExclusiveWith.Contains(Owned->UpgradeTag) ||
			Owned->MutuallyExclusiveWith.Contains(Candidate->UpgradeTag))
		{
			return true;
		}
	}

	return false;
}

UUpgradeComponent* UUpgradeManagerComponent::GetUpgradeComponent(FGameplayTag UpgradeTag) const
{
	const TObjectPtr<UUpgradeComponent>* Found = ActiveUpgrades.Find(UpgradeTag);
	return Found ? Found->Get() : nullptr;
}

bool UUpgradeManagerComponent::HasStoredHealthPickupConsumer() const
{
	for (const auto& Pair : ActiveUpgrades)
	{
		const UUpgradeComponent* Comp = Pair.Value;
		if (!Comp)
		{
			continue;
		}
		const UUpgradeDefinition* Owned = Comp->UpgradeDefinition;
		if (Owned && Owned->bUsesStoredHealthPickups)
		{
			return true;
		}
	}
	return false;
}

UInputAction* UUpgradeManagerComponent::GetHealSpendInputAction() const
{
	// Consumers are mutually exclusive, so at most one is owned — return the first found.
	for (const auto& Pair : ActiveUpgrades)
	{
		const UUpgradeComponent* Comp = Pair.Value;
		if (!Comp)
		{
			continue;
		}
		const UUpgradeDefinition* Owned = Comp->UpgradeDefinition;
		if (Owned && Owned->bUsesStoredHealthPickups && Owned->HealSpendInputAction)
		{
			return Owned->HealSpendInputAction;
		}
	}
	return nullptr;
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

void UUpgradeManagerComponent::RestoreUpgrades(const TMap<FGameplayTag, int32>& TagToLevel, const UUpgradeRegistry* Registry)
{
	if (!Registry)
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradeManager: RestoreUpgrades called with null registry"));
		return;
	}

	// Drop any owned upgrades that aren't in the saved set.
	TArray<FGameplayTag> CurrentTags;
	ActiveUpgrades.GetKeys(CurrentTags);
	for (const FGameplayTag& Tag : CurrentTags)
	{
		if (!TagToLevel.Contains(Tag))
		{
			RemoveUpgrade(Tag);
		}
	}

	// Grant / level each saved upgrade up to its stored level.
	// GrantUpgrade lifts the level by 1 each call (first call creates at Lv 1), so we loop.
	for (const TPair<FGameplayTag, int32>& Pair : TagToLevel)
	{
		UUpgradeDefinition* Definition = Registry->FindByTag(Pair.Key);
		if (!Definition)
		{
			UE_LOG(LogTemp, Warning, TEXT("UpgradeManager: RestoreUpgrades — no definition for tag '%s'"), *Pair.Key.ToString());
			continue;
		}

		const int32 TargetLevel = FMath::Clamp(Pair.Value, 1, FMath::Max(1, Definition->MaxLevel));

		// Guard caps iterations at TargetLevel so a misbehaving GrantUpgrade can't spin forever.
		int32 Guard = 0;
		while (GetUpgradeLevel(Pair.Key) < TargetLevel && Guard++ < TargetLevel)
		{
			if (!GrantUpgrade(Definition))
			{
				break; // maxed out or refused (e.g. mutually exclusive)
			}
		}

		UE_LOG(LogTemp, Log, TEXT("[UPGRADE_DEBUG] RestoreUpgrades: '%s' -> Lv %d (wanted %d)"),
			*Definition->DisplayName.ToString(), GetUpgradeLevel(Pair.Key), TargetLevel);
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

void UUpgradeManagerComponent::NotifyWeaponDealtDamage(AShooterWeapon* Weapon, AActor* Target, float Damage, bool bKilled)
{
	for (auto& Pair : ActiveUpgrades)
	{
		if (Pair.Value)
		{
			Pair.Value->OnWeaponDealtDamage(Weapon, Target, Damage, bKilled);
		}
	}

	NotifyOwnerDealtDamage(Target, Damage, bKilled);
}

void UUpgradeManagerComponent::NotifyEnemyDroppedRangedWeapon(ADroppedRangedWeapon* DroppedWeapon, AActor* DroppingEnemy)
{
	if (!DroppedWeapon)
	{
		return;
	}

	for (auto& Pair : ActiveUpgrades)
	{
		if (Pair.Value)
		{
			Pair.Value->OnEnemyDroppedRangedWeapon(DroppedWeapon, DroppingEnemy);
		}
	}
}

void UUpgradeManagerComponent::NotifyOwnerHitscanIonized(AActor* Target)
{
	for (auto& Pair : ActiveUpgrades)
	{
		if (Pair.Value)
		{
			Pair.Value->OnHitscanIonized(Target);
		}
	}
}

void UUpgradeManagerComponent::NotifyHealthPickupCollectedAtFullHP()
{
	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_POOL] Pickup collected at full HP — pool before: %d/%d"),
		StoredHealthPickups, MaxStoredHealthPickups);

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

bool UUpgradeManagerComponent::HandleWeaponSecondaryAction(AShooterWeapon* Weapon)
{
	for (auto& Pair : ActiveUpgrades)
	{
		if (Pair.Value && Pair.Value->OnWeaponSecondaryAction(Weapon))
		{
			return true;
		}
	}

	return false;
}

void UUpgradeManagerComponent::HandleWeaponSecondaryActionReleased(AShooterWeapon* Weapon)
{
	for (auto& Pair : ActiveUpgrades)
	{
		if (Pair.Value)
		{
			Pair.Value->OnWeaponSecondaryActionReleased(Weapon);
		}
	}
}

bool UUpgradeManagerComponent::AddStoredHealthPickup()
{
	if (StoredHealthPickups >= MaxStoredHealthPickups)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_POOL] Add rejected — pool at cap %d/%d"),
			StoredHealthPickups, MaxStoredHealthPickups);
		return false;
	}

	StoredHealthPickups++;
	OnStoredHealthPickupsChanged.Broadcast(StoredHealthPickups, MaxStoredHealthPickups);

	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_POOL] Stored health pickup: %d/%d"),
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

	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_POOL] Consumed %d pickups (%d remaining of %d)"),
		Consumed, StoredHealthPickups, MaxStoredHealthPickups);

	return Consumed;
}

void UUpgradeManagerComponent::ResetStoredHealthPickups()
{
	if (StoredHealthPickups == 0)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_POOL] Pool RESET — was %d"), StoredHealthPickups);

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

float UUpgradeManagerComponent::GetCombinedMeleeDamageMultiplier(AActor* Target) const
{
	float Combined = 1.0f;

	for (const auto& Pair : ActiveUpgrades)
	{
		if (Pair.Value)
		{
			Combined *= Pair.Value->GetMeleeDamageMultiplier(Target);
		}
	}

	return Combined;
}

float UUpgradeManagerComponent::GetCombinedMeleeKnockbackDistanceMultiplier(AActor* Target) const
{
	float Combined = 1.0f;

	for (const auto& Pair : ActiveUpgrades)
	{
		if (Pair.Value)
		{
			Combined *= Pair.Value->GetMeleeKnockbackDistanceMultiplier(Target);
		}
	}

	return Combined;
}

int32 UUpgradeManagerComponent::GetBandolierMaxCopies() const
{
	for (const auto& Pair : ActiveUpgrades)
	{
		if (const UUpgrade_Bandolier* Bandolier = Cast<UUpgrade_Bandolier>(Pair.Value))
		{
			return Bandolier->GetMaxCopiesForCurrentLevel();
		}
	}
	return 1;
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
