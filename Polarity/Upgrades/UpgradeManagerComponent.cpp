// Copyright 2025 Suspended Caterpillar. All Rights Reserved.

#include "UpgradeManagerComponent.h"
#include "UpgradeDefinition.h"
#include "UpgradeComponent.h"
#include "UpgradeRegistry.h"
#include "ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "Upgrades/Upgrade_Bandolier.h"
#include "DispenserUpgradePool.h"
#include "JumpSlotParams.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"

UUpgradeManagerComponent::UUpgradeManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// A server change reaches the owning client through Client_SetUpgradeLevel, and a component
	// RPC needs a replicated component.
	SetIsReplicatedByDefault(true);

	SlotLayout = TSoftObjectPtr<UDispenserUpgradePool>(FSoftObjectPath(
		TEXT("/Game/Variant_Shooter/Blueprints/Upgrades/DA_DispenserUpgradePool.DA_DispenserUpgradePool")));
}

// ==================== Dispenser cards ====================

namespace
{
	/** An upgrade as a bag cell. PickupClass stays empty: a dropped cell of it becomes the
	 *  inventory's per-kind pickup. */
	FInventoryItem MakeUpgradeItem(UUpgradeDefinition* Definition, int32 Level)
	{
		FInventoryItem Item;
		Item.Kind = EInventorySlotKind::AbilityUpgrade;
		Item.Payload = Definition;
		Item.Count = 1;
		Item.StackMax = 1;
		Item.Level = FMath::Max(1, Level);
		return Item;
	}
}

UUpgradeDefinition* UUpgradeManagerComponent::GetOwnedInSlot(const UDispenserUpgradePool* Pool, int32 SlotIndex, int32& OutLevel) const
{
	OutLevel = 0;
	if (!Pool || !Pool->Slots.IsValidIndex(SlotIndex) || !Pool->Slots[SlotIndex].bExclusive)
	{
		return nullptr;
	}
	for (const FDispenserUpgradeEntry& Entry : Pool->Slots[SlotIndex].Upgrades)
	{
		UUpgradeDefinition* const Def = Entry.Upgrade;
		const int32 Level = Def ? GetUpgradeLevel(Def->UpgradeTag) : 0;
		if (Level > 0)
		{
			OutLevel = Level;
			return Def;
		}
	}
	return nullptr;
}

bool UUpgradeManagerComponent::BuildUpgradeCard(const UDispenserUpgradePool* Pool, EUpgradeRarity MaxRarity,
	const TArray<const UUpgradeDefinition*>& Exclude, FUpgradeOfferCard& OutCard) const
{
	if (!Pool)
	{
		return false;
	}

	// One candidate per upgrade: the best level it can offer at this rarity, with its pool weight.
	TArray<FUpgradeOfferCard> Candidates;
	TArray<float> Weights;
	for (int32 SlotIndex = 0; SlotIndex < Pool->Slots.Num(); ++SlotIndex)
	{
		int32 HeldLevel = 0;
		UUpgradeDefinition* const Held = GetOwnedInSlot(Pool, SlotIndex, HeldLevel);

		for (const FDispenserUpgradeEntry& Entry : Pool->Slots[SlotIndex].Upgrades)
		{
			UUpgradeDefinition* const Def = Entry.Upgrade;
			if (!Def || Entry.Weight <= 0.0f || !Def->UpgradeTag.IsValid() || !Def->ComponentClass || Exclude.Contains(Def))
			{
				continue;
			}
			// A copy waiting in the bag counts as owned: offering a level the player already
			// carries would be a card worth nothing.
			const int32 Equipped = GetUpgradeLevel(Def->UpgradeTag);
			const int32 Bagged = GetBaggedLevel(Def);
			const int32 Owned = FMath::Max(Equipped, Bagged);
			if (Owned == 0 && OwnsConflicting(Def) && Def != Held)
			{
				continue;
			}

			// The level to beat: its own when owned, the held one's when it would replace it.
			FUpgradeOfferCard Card;
			Card.Definition = Def;
			int32 Floor = 0;
			if (Equipped > 0)
			{
				Card.Kind = EUpgradeOfferKind::LevelUp;
				Card.FromLevel = Owned;
				Floor = Owned;
			}
			else if (Held)
			{
				Card.Kind = EUpgradeOfferKind::Replace;
				Card.Replaces = Held;
				Card.FromLevel = Bagged;
				Floor = FMath::Max(HeldLevel, Bagged);
			}
			else if (Bagged > 0)
			{
				Card.Kind = EUpgradeOfferKind::LevelUp;
				Card.FromLevel = Bagged;
				Floor = Bagged;
			}

			int32 BestLevel = 0;
			EUpgradeRarity BestRarity = EUpgradeRarity::Common;
			for (int32 Level = Floor + 1; Level <= Def->MaxLevel; ++Level)
			{
				const EUpgradeRarity Rarity = Def->GetLevelRarity(Level);
				if (Rarity > MaxRarity)
				{
					continue;
				}
				// Highest rarity wins; for the same rarity the lowest level, so nothing is skipped
				// without a reason.
				if (BestLevel == 0 || Rarity > BestRarity)
				{
					BestLevel = Level;
					BestRarity = Rarity;
				}
			}
			if (BestLevel == 0)
			{
				continue;
			}
			Card.ToLevel = BestLevel;
			Card.Rarity = BestRarity;
			Candidates.Add(Card);
			Weights.Add(Entry.Weight);
		}
	}
	if (Candidates.Num() == 0)
	{
		return false;
	}

	// Only the rarest that fits: a legendary roll offers a legendary level when there is one.
	EUpgradeRarity Top = EUpgradeRarity::Common;
	for (const FUpgradeOfferCard& Candidate : Candidates)
	{
		Top = FMath::Max(Top, Candidate.Rarity);
	}
	float Total = 0.0f;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		Total += Candidates[Index].Rarity == Top ? Weights[Index] : 0.0f;
	}
	float Pick = FMath::FRand() * Total;
	OutCard = Candidates.Last();
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (Candidates[Index].Rarity != Top)
		{
			continue;
		}
		OutCard = Candidates[Index];
		Pick -= Weights[Index];
		if (Pick <= 0.0f)
		{
			break;
		}
	}
	return true;
}

bool UUpgradeManagerComponent::GrantOfferCard(const FUpgradeOfferCard& Card)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return false;
	}
	// Card.Replaces needs no handling of its own: AcquireUpgrade moves whatever holds the slot to
	// the bag, which is the same upgrade the card named when it was built.
	return AcquireUpgrade(Card.Definition, Card.ToLevel);
}

bool UUpgradeManagerComponent::GrantUpgradeEverywhere(UUpgradeDefinition* Definition)
{
	if (!Definition || GetOwnerRole() != ROLE_Authority)
	{
		return GrantUpgrade(Definition);
	}
	return AcquireUpgrade(Definition, GetOwnedLevelAnywhere(Definition) + 1);
}

// ==================== Action slots and the bag ====================

const UDispenserUpgradePool* UUpgradeManagerComponent::GetSlotLayout() const
{
	return SlotLayout.LoadSynchronous();
}

UInventoryComponent* UUpgradeManagerComponent::GetInventory() const
{
	const AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwner());
	return Character ? Character->GetInventoryComponent() : nullptr;
}

int32 UUpgradeManagerComponent::FindExclusiveSlotOf(const UUpgradeDefinition* Definition) const
{
	const UDispenserUpgradePool* Layout = GetSlotLayout();
	const int32 SlotIndex = (Layout && Definition) ? Layout->FindSlotOf(Definition) : INDEX_NONE;
	return (SlotIndex != INDEX_NONE && Layout->Slots[SlotIndex].bExclusive) ? SlotIndex : INDEX_NONE;
}

int32 UUpgradeManagerComponent::GetBaggedLevel(const UUpgradeDefinition* Definition, int32* OutCell) const
{
	if (OutCell)
	{
		*OutCell = INDEX_NONE;
	}
	const UInventoryComponent* Inventory = GetInventory();
	if (!Definition || !Inventory)
	{
		return 0;
	}

	int32 Best = 0;
	const TArray<FInventorySlot>& Cells = Inventory->GetSlots();
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		const FInventorySlot& Cell = Cells[Index];
		if (Cell.Kind == EInventorySlotKind::AbilityUpgrade && Cell.Payload == Definition)
		{
			const int32 Level = FMath::Max(1, Cell.Level);
			if (Level > Best)
			{
				Best = Level;
				if (OutCell)
				{
					*OutCell = Index;
				}
			}
		}
	}
	return Best;
}

int32 UUpgradeManagerComponent::GetOwnedLevelAnywhere(const UUpgradeDefinition* Definition) const
{
	if (!Definition)
	{
		return 0;
	}
	return FMath::Max(GetUpgradeLevel(Definition->UpgradeTag), GetBaggedLevel(Definition));
}

void UUpgradeManagerComponent::SetUpgradeLevelLocal(UUpgradeDefinition* Definition, int32 Level)
{
	if (!Definition || !Definition->UpgradeTag.IsValid())
	{
		return;
	}

	Level = FMath::Clamp(Level, 0, FMath::Max(1, Definition->MaxLevel));
	const int32 Current = GetUpgradeLevel(Definition->UpgradeTag);
	if (Level == Current)
	{
		return;
	}

	// Levels only go up on a live component, so a lower level is a fresh grant.
	if (Level == 0 || Current > Level)
	{
		RemoveUpgrade(Definition->UpgradeTag);
		if (Level == 0)
		{
			return;
		}
	}

	// GrantUpgrade adds one level per call; the guard stops a definition that refuses to level.
	for (int32 Guard = 0; GetUpgradeLevel(Definition->UpgradeTag) < Level && Guard < 16; ++Guard)
	{
		if (!GrantUpgrade(Definition))
		{
			break;
		}
	}
}

void UUpgradeManagerComponent::SetUpgradeLevelEverywhere(UUpgradeDefinition* Definition, int32 Level)
{
	SetUpgradeLevelLocal(Definition, Level);

	// The owner's copy of the upgrade. On a listen host the owner is this very machine and the call
	// above already is its copy. Reliable RPCs on one actor arrive in order, so a remove followed by
	// a grant in the same swap lands on the client in the same order.
	const APawn* const Pawn = Cast<APawn>(GetOwner());
	if (GetOwnerRole() == ROLE_Authority && Pawn && !Pawn->IsLocallyControlled())
	{
		Client_SetUpgradeLevel(Definition, Level);
	}
}

void UUpgradeManagerComponent::Client_SetUpgradeLevel_Implementation(UUpgradeDefinition* Definition, int32 Level)
{
	if (GetOwnerRole() == ROLE_Authority)
	{
		return;
	}
	SetUpgradeLevelLocal(Definition, Level);
}

void UUpgradeManagerComponent::StashUpgrade(UUpgradeDefinition* Definition, int32 Level)
{
	UInventoryComponent* Inventory = GetInventory();
	if (!Definition || Level <= 0 || !Inventory)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] StashUpgrade: '%s' Lv %d has no bag to go to - lost"),
			*GetNameSafe(Definition), Level);
		return;
	}

	const FInventoryItem Item = MakeUpgradeItem(Definition, Level);
	if (Inventory->TryAdd(Item) == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[UPGRADE_DEBUG] '%s' Lv %d went to the bag"), *GetNameSafe(Definition), Level);
		return;
	}
	if (Inventory->DropItemToWorld(Item))
	{
		UE_LOG(LogTemp, Log, TEXT("[UPGRADE_DEBUG] bag full: '%s' Lv %d dropped on the floor"), *GetNameSafe(Definition), Level);
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] bag full and no drop class for upgrades: '%s' Lv %d lost. "
		"Set DropClassByKind[AbilityUpgrade] on the inventory."), *GetNameSafe(Definition), Level);
}

bool UUpgradeManagerComponent::AcquireUpgrade(UUpgradeDefinition* Definition, int32 Level)
{
	if (!Definition || GetOwnerRole() != ROLE_Authority)
	{
		return false;
	}

	// A copy in the bag merges into the one being taken: one upgrade, the higher level.
	int32 BagCell = INDEX_NONE;
	const int32 Bagged = GetBaggedLevel(Definition, &BagCell);
	if (Bagged > 0)
	{
		if (UInventoryComponent* Inventory = GetInventory())
		{
			Inventory->ClearSlot(BagCell);
		}
	}
	Level = FMath::Max3(Level, Bagged, GetUpgradeLevel(Definition->UpgradeTag));

	// An exclusive slot holds one: the new one goes in, the old one to the bag.
	const int32 SlotIndex = FindExclusiveSlotOf(Definition);
	if (SlotIndex != INDEX_NONE)
	{
		int32 HeldLevel = 0;
		UUpgradeDefinition* const Held = GetOwnedInSlot(GetSlotLayout(), SlotIndex, HeldLevel);
		if (Held && Held != Definition)
		{
			SetUpgradeLevelEverywhere(Held, 0);
			StashUpgrade(Held, HeldLevel);
		}
	}

	SetUpgradeLevelEverywhere(Definition, Level);
	const bool bOk = GetUpgradeLevel(Definition->UpgradeTag) > 0;
	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] Acquire '%s' Lv %d (slot %d) -> %s"),
		*GetNameSafe(Definition), Level, SlotIndex, bOk ? TEXT("equipped") : TEXT("REFUSED"));
	return bOk;
}

bool UUpgradeManagerComponent::EquipFromBag(int32 CellIndex)
{
	UInventoryComponent* Inventory = GetInventory();
	if (GetOwnerRole() != ROLE_Authority || !Inventory || !Inventory->GetSlots().IsValidIndex(CellIndex))
	{
		return false;
	}

	const FInventorySlot Cell = Inventory->GetSlots()[CellIndex];
	UUpgradeDefinition* const Definition = Cast<UUpgradeDefinition>(Cell.Payload);
	const int32 SlotIndex = FindExclusiveSlotOf(Definition);
	if (Cell.Kind != EInventorySlotKind::AbilityUpgrade || !Definition || SlotIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] EquipFromBag: cell %d holds no upgrade with an action slot"), CellIndex);
		return false;
	}

	int32 HeldLevel = 0;
	UUpgradeDefinition* const Held = GetOwnedInSlot(GetSlotLayout(), SlotIndex, HeldLevel);
	const int32 Level = FMath::Max(FMath::Max(1, Cell.Level), Held == Definition ? HeldLevel : 0);

	// The held one takes the cell the new one leaves: a swap, so a full bag never blocks it.
	FInventoryItem Back;
	if (Held && Held != Definition)
	{
		Back = MakeUpgradeItem(Held, HeldLevel);
		SetUpgradeLevelEverywhere(Held, 0);
	}
	Inventory->ReplaceSlot(CellIndex, Back);
	SetUpgradeLevelEverywhere(Definition, Level);

	if (GetUpgradeLevel(Definition->UpgradeTag) == 0)
	{
		// Refused (mutually exclusive with something else owned). Put both back as they were.
		UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] EquipFromBag: '%s' refused, swap undone"), *GetNameSafe(Definition));
		Inventory->ReplaceSlot(CellIndex, MakeUpgradeItem(Definition, Cell.Level));
		if (Held && Held != Definition)
		{
			SetUpgradeLevelEverywhere(Held, HeldLevel);
		}
		return false;
	}

	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] EquipFromBag: cell %d '%s' Lv %d into slot %d, '%s' back to the cell"),
		CellIndex, *GetNameSafe(Definition), Level, SlotIndex, *GetNameSafe(Held != Definition ? Held : nullptr));
	return true;
}

void UUpgradeManagerComponent::Server_EquipFromBag_Implementation(int32 CellIndex)
{
	EquipFromBag(CellIndex);
}

bool UUpgradeManagerComponent::UnequipToBag(int32 SlotIndex, int32 CellIndex)
{
	UInventoryComponent* Inventory = GetInventory();
	if (GetOwnerRole() != ROLE_Authority || !Inventory)
	{
		return false;
	}

	int32 HeldLevel = 0;
	UUpgradeDefinition* const Held = GetOwnedInSlot(GetSlotLayout(), SlotIndex, HeldLevel);
	if (!Held)
	{
		return false;
	}

	const TArray<FInventorySlot>& Cells = Inventory->GetSlots();
	if (CellIndex == INDEX_NONE)
	{
		CellIndex = Cells.IndexOfByPredicate([](const FInventorySlot& Cell) { return Cell.IsEmpty(); });
	}
	if (!Cells.IsValidIndex(CellIndex))
	{
		UE_LOG(LogTemp, Log, TEXT("[UPGRADE_DEBUG] UnequipToBag: no free cell for '%s'"), *GetNameSafe(Held));
		return false;
	}

	// Dropped onto another upgrade of the same slot: that one goes in, this one takes its cell.
	if (Cells[CellIndex].Kind == EInventorySlotKind::AbilityUpgrade)
	{
		return FindExclusiveSlotOf(Cast<UUpgradeDefinition>(Cells[CellIndex].Payload)) == SlotIndex
			&& EquipFromBag(CellIndex);
	}
	if (!Cells[CellIndex].IsEmpty())
	{
		return false;
	}

	Inventory->ReplaceSlot(CellIndex, MakeUpgradeItem(Held, HeldLevel));
	SetUpgradeLevelEverywhere(Held, 0);
	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] UnequipToBag: '%s' Lv %d from slot %d to cell %d"),
		*GetNameSafe(Held), HeldLevel, SlotIndex, CellIndex);
	return true;
}

void UUpgradeManagerComponent::Server_UnequipToBag_Implementation(int32 SlotIndex, int32 CellIndex)
{
	UnequipToBag(SlotIndex, CellIndex);
}

bool UUpgradeManagerComponent::GetJumpSlotParams(FJumpSlotParams& Out) const
{
	for (const TPair<FGameplayTag, TObjectPtr<UUpgradeComponent>>& Pair : ActiveUpgrades)
	{
		if (Pair.Value && Pair.Value->GetJumpSlotParams(Out))
		{
			return true;
		}
	}
	return false;
}

bool UUpgradeManagerComponent::TryTakeShotFromReserve(const AShooterWeapon* Weapon)
{
	for (const TPair<FGameplayTag, TObjectPtr<UUpgradeComponent>>& Pair : ActiveUpgrades)
	{
		if (Pair.Value && Pair.Value->TryTakeShotFromReserve(Weapon))
		{
			return true;
		}
	}
	return false;
}

float UUpgradeManagerComponent::GetSlideFireFrictionScale() const
{
	float Scale = 1.0f;
	for (const TPair<FGameplayTag, TObjectPtr<UUpgradeComponent>>& Pair : ActiveUpgrades)
	{
		if (Pair.Value)
		{
			Scale *= Pair.Value->GetSlideFireFrictionScale();
		}
	}
	return Scale;
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
