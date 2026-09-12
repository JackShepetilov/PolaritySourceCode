// InventoryComponent.cpp

#include "Variant_Shooter/Inventory/InventoryComponent.h"

#include "Coop/CoopPlayers.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "Variant_Shooter/Pickups/InventoryPickup.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Upgrades/UpgradeManagerComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogInventory, Log, All);

UInventoryComponent::UInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Owner-only, like the ability component next door. What a teammate is carrying is not
	// something this screen can act on, and four grids replicated to four players would be pure
	// bandwidth for a HUD nobody draws.
	DOREPLIFETIME_CONDITION(UInventoryComponent, Slots, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UInventoryComponent, FreeAttachmentSlots, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UInventoryComponent, MaxAmmoCells, COND_OwnerOnly);
}

void UInventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	// Only the server seeds the array; the client receives it. Seeding on both would let a client
	// briefly draw a grid the server has not agreed to yet.
	if (GetOwnerRole() == ROLE_Authority)
	{
		SetSlotCount(StartingSlotCount);
		SetFreeAttachmentSlots(StartingFreeAttachmentSlots);
	}
}

void UInventoryComponent::OnRep_Slots()
{
	OnInventoryChanged.Broadcast();
}

// ==================== Reading ====================

int32 UInventoryComponent::GetEmptyCellCount() const
{
	int32 Count = 0;
	for (const FInventorySlot& Slot : Slots)
	{
		if (Slot.IsEmpty())
		{
			++Count;
		}
	}
	return Count;
}

int32 UInventoryComponent::GetAmmo() const
{
	// The ammo a gun has IS the grid. A weapon knows its magazine and nothing else, so the number
	// under the magazine count can only come from here, and that is the point rather than a
	// shortcut: rounds occupy cells that could have carried money out of the run instead.
	//
	// Per weapon, never pooled. The class weapon is energy and owns no cells at all; a looted gun
	// brings its own rounds, and they are worth nothing to anything else the player is carrying.
	int32 Total = 0;
	for (const FInventorySlot& Slot : Slots)
	{
		if (Slot.Kind == EInventorySlotKind::Ammo)
		{
			Total += Slot.Count;
		}
	}
	return Total;
}

int32 UInventoryComponent::ConsumeAmmo(int32 Count)
{
	// Server only: the array is replicated to the owner, and a client that subtracted here would be
	// corrected on the next update anyway.
	if (Count <= 0 || GetOwnerRole() != ROLE_Authority)
	{
		return 0;
	}

	int32 Remaining = Count;

	// Emptiest cell first, so partial stacks are consolidated by spending rather than by tidying.
	// A cell that reaches zero is freed outright: an empty ammo cell holding a weapon binding would
	// keep costing capacity for rounds the player no longer has.
	for (int32 Index = 0; Index < Slots.Num() && Remaining > 0; ++Index)
	{
		FInventorySlot& Slot = Slots[Index];
		if (Slot.Kind != EInventorySlotKind::Ammo)
		{
			continue;
		}

		const int32 Taken = FMath::Min(Slot.Count, Remaining);
		Slot.Count -= Taken;
		Remaining -= Taken;

		if (Slot.Count <= 0)
		{
			Slot = FInventorySlot();
		}
	}

	const int32 Consumed = Count - Remaining;
	if (Consumed > 0)
	{
		OnInventoryChanged.Broadcast();
	}
	return Consumed;
}

int32 UInventoryComponent::GetAmmoCellCount() const
{
	int32 Cells = 0;
	for (const FInventorySlot& Slot : Slots)
	{
		if (Slot.Kind == EInventorySlotKind::Ammo)
		{
			++Cells;
		}
	}
	return Cells;
}

int32 UInventoryComponent::GetMaxAmmoCells() const
{
	// Asked live rather than pushed on an event. The Bandolier upgrade is the only thing that
	// raises this, and reading it here means there is no second copy of the number to fall out of
	// step with the upgrade the moment it levels.
	int32 Allowed = MaxAmmoCells;

	if (const AActor* Owner = GetOwner())
	{
		if (const UUpgradeManagerComponent* Upgrades = Owner->FindComponentByClass<UUpgradeManagerComponent>())
		{
			Allowed = FMath::Max(Allowed, Upgrades->GetBandolierMaxCopies());
		}
	}

	return FMath::Max(1, Allowed);
}

void UInventoryComponent::SetMaxAmmoCells(int32 NewCount)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	// Only ever raised. Lowering it would have to decide which magazine to throw away, and that is
	// a decision nobody has asked for: the meta buys capacity, it never takes it back.
	const int32 Clamped = FMath::Max(1, NewCount);
	if (Clamped == MaxAmmoCells)
	{
		return;
	}

	MaxAmmoCells = Clamped;
	OnInventoryChanged.Broadcast();
}

int32 UInventoryComponent::TakeAllAmmo()
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return 0;
	}

	int32 Total = 0;
	bool bChanged = false;
	for (FInventorySlot& Slot : Slots)
	{
		if (Slot.Kind == EInventorySlotKind::Ammo)
		{
			Total += Slot.Count;
			Slot = FInventorySlot();
			bChanged = true;
		}
	}

	if (bChanged)
	{
		OnInventoryChanged.Broadcast();
	}
	return Total;
}

// ==================== Attachments ====================
//
// The weapon records what is fitted; this file decides who pays. The first FreeAttachmentSlots
// parts on a gun are free and the rest keep holding the cell they came out of, and which is which
// is never stored -- it is the same subtraction every time, done from the weapon's own list, so
// there is no second answer to fall out of step with it.

int32 UInventoryComponent::FindHeldAttachmentCell(const AShooterWeapon* Weapon,
	EWeaponAttachmentType InType) const
{
	if (!Weapon)
	{
		return INDEX_NONE;
	}

	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FInventorySlot& Slot = Slots[Index];
		if (Slot.Kind != EInventorySlotKind::Attachment || !Slot.bInstalled
			|| Slot.InstalledOnWeapon != Weapon)
		{
			continue;
		}

		const UWeaponAttachmentDefinition* Def = Cast<UWeaponAttachmentDefinition>(Slot.Payload);
		if (Def && Def->Type == InType)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 UInventoryComponent::GetHeldAttachmentCellCount(const AShooterWeapon* Weapon) const
{
	if (!Weapon)
	{
		return 0;
	}

	int32 Count = 0;
	for (const FInventorySlot& Slot : Slots)
	{
		if (Slot.Kind == EInventorySlotKind::Attachment && Slot.bInstalled
			&& Slot.InstalledOnWeapon == Weapon)
		{
			++Count;
		}
	}
	return Count;
}

void UInventoryComponent::RebalanceAttachmentCells(AShooterWeapon* Weapon)
{
	if (!Weapon || GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	// How many of this gun's attachments should be paid for right now. Everything above the free
	// slots, and nothing else.
	const int32 Mounted = Weapon->GetInstalledAttachmentCount();
	const int32 ShouldPay = FMath::Max(0, Mounted - FreeAttachmentSlots);

	bool bChanged = false;

	// Only ever releases. The other direction cannot be fixed here: taking a cell requires a cell
	// to be free, and that has to be answered where the player can be told no -- at the moment of
	// mounting, in InstallAttachmentFromSlot.
	//
	// Releasing is what happens when a free slot opens up: a part that was removed leaves its free
	// slot to whichever paid part is still on the gun, and the meta selling another free slot does
	// the same thing for every weapon at once.
	int32 Held = GetHeldAttachmentCellCount(Weapon);
	for (int32 Index = 0; Index < Slots.Num() && Held > ShouldPay; ++Index)
	{
		FInventorySlot& Slot = Slots[Index];
		if (Slot.Kind != EInventorySlotKind::Attachment || !Slot.bInstalled
			|| Slot.InstalledOnWeapon != Weapon)
		{
			continue;
		}

		// The cell is emptied, not the attachment removed: the weapon holds the part, and this
		// array only ever held the bill for it.
		Slot = FInventorySlot();
		--Held;
		bChanged = true;
	}

	if (bChanged)
	{
		OnInventoryChanged.Broadcast();
	}
}

void UInventoryComponent::ReleaseAttachmentCellsFor(const AShooterWeapon* Weapon)
{
	if (!Weapon || GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	bool bChanged = false;
	for (FInventorySlot& Slot : Slots)
	{
		if (Slot.Kind == EInventorySlotKind::Attachment && Slot.bInstalled
			&& Slot.InstalledOnWeapon == Weapon)
		{
			// Attachments travel with the gun (contract section 4), so handing the weapon over
			// frees its cells as well. The parts themselves stay bolted to the weapon that left.
			Slot = FInventorySlot();
			bChanged = true;
		}
	}

	if (bChanged)
	{
		OnInventoryChanged.Broadcast();
	}
}

bool UInventoryComponent::InstallAttachmentFromSlot(int32 SlotIndex, AShooterWeapon* Weapon)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		UE_LOG(LogInventory, Warning, TEXT("[INV_DEBUG] InstallAttachmentFromSlot without authority - ignored"));
		return false;
	}

	if (!Slots.IsValidIndex(SlotIndex) || !Weapon)
	{
		return false;
	}

	FInventorySlot& Slot = Slots[SlotIndex];
	if (Slot.Kind != EInventorySlotKind::Attachment || Slot.bInstalled)
	{
		return false;
	}

	UWeaponAttachmentDefinition* Def = Cast<UWeaponAttachmentDefinition>(Slot.Payload);
	if (!Def)
	{
		UE_LOG(LogInventory, Warning, TEXT("[INV_DEBUG] cell %d says attachment but carries %s"),
			SlotIndex, *GetNameSafe(Slot.Payload));
		return false;
	}

	// Counted BEFORE the weapon takes it: whether this one is free depends on how many were already
	// on the gun, not on how many there are afterwards.
	const int32 MountedBefore = Weapon->GetInstalledAttachmentCount();

	if (!Weapon->InstallAttachment(Def))
	{
		// The weapon refused: it already carries that type, or the attachment does not fit this gun
		// (UWeaponAttachmentDefinition::FitsWeapon). Nothing has changed on either side, so the cell
		// is left exactly as it was.
		return false;
	}

	if (MountedBefore < FreeAttachmentSlots)
	{
		// Free: the cell goes back to the player. The attachment is not lost by this -- the weapon
		// is holding it now.
		Slot = FInventorySlot();
	}
	else
	{
		// Paid: the same cell keeps the same item, and is marked as spoken for so the HUD can draw
		// it as the ghost the contract asks for.
		Slot.bInstalled = true;
		Slot.InstalledOnWeapon = Weapon;
	}

	OnInventoryChanged.Broadcast();

	UE_LOG(LogInventory, Log, TEXT("[INV_DEBUG] mounted %s on %s (%s), cell %d"),
		*GetNameSafe(Def), *Weapon->GetName(),
		MountedBefore < FreeAttachmentSlots ? TEXT("free") : TEXT("paid"), SlotIndex);
	return true;
}

void UInventoryComponent::Server_InstallAttachmentFromSlot_Implementation(int32 SlotIndex, AShooterWeapon* Weapon)
{
	InstallAttachmentFromSlot(SlotIndex, Weapon);
}

bool UInventoryComponent::UninstallAttachment(AShooterWeapon* Weapon, EWeaponAttachmentType InType)
{
	if (GetOwnerRole() != ROLE_Authority || !Weapon)
	{
		return false;
	}

	UWeaponAttachmentDefinition* Def = Weapon->GetAttachmentOfType(InType);
	if (!Def)
	{
		return false;
	}

	const int32 HeldCell = FindHeldAttachmentCell(Weapon, InType);

	// A part mounted for free is not in the bag at all and needs somewhere to land. Checked BEFORE
	// anything is taken off, so a refusal leaves the gun exactly as it was rather than half undone.
	int32 LandingCell = INDEX_NONE;
	if (HeldCell == INDEX_NONE)
	{
		LandingCell = FindEmptyCell();
		if (LandingCell == INDEX_NONE)
		{
			UE_LOG(LogInventory, Warning, TEXT("[INV_DEBUG] cannot take %s off %s: no empty cell to "
				"put it in. The attachment stays fitted."), *GetNameSafe(Def), *Weapon->GetName());
			return false;
		}
	}

	Weapon->UninstallAttachmentOfType(InType);

	if (HeldCell != INDEX_NONE)
	{
		// It was already paying for this cell; it just stops being fitted and stays where it is.
		Slots[HeldCell].bInstalled = false;
		Slots[HeldCell].InstalledOnWeapon = nullptr;
	}
	else
	{
		FInventorySlot& Landing = Slots[LandingCell];
		Landing = FInventorySlot();
		Landing.Kind = EInventorySlotKind::Attachment;
		Landing.Payload = Def;
		Landing.Count = 1;
		Landing.StackMax = 1;
	}

	// A free slot has just opened on this weapon, so whichever part was paying for a cell should
	// stop.
	RebalanceAttachmentCells(Weapon);

	OnInventoryChanged.Broadcast();
	return true;
}

void UInventoryComponent::Server_UninstallAttachment_Implementation(AShooterWeapon* Weapon, EWeaponAttachmentType InType)
{
	UninstallAttachment(Weapon, InType);
}

bool UInventoryComponent::CanMerge(const FInventorySlot& Slot, const FInventoryItem& Item)
{
	// Currency carries no definition, so every unit of it merges with every other unit, and since
	// 2026-09-01 so does ammo: one pool for every weapon means one cell of rounds is the same thing
	// as any other.
	return Slot.Kind == Item.Kind
		&& Slot.Payload == Item.Payload;
}

int32 UInventoryComponent::FindStackWithRoom(const FInventoryItem& Item) const
{
	if (!Item.IsStackable())
	{
		return INDEX_NONE;
	}

	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (Slots[Index].HasRoom() && CanMerge(Slots[Index], Item))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 UInventoryComponent::FindEmptyCell() const
{
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (Slots[Index].IsEmpty())
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool UInventoryComponent::CanAccept(const FInventoryItem& Item, int32& OutSlotIndex) const
{
	OutSlotIndex = INDEX_NONE;

	if (!Item.IsValid())
	{
		return false;
	}

	// Topping up an existing stack comes first: it costs no capacity at all, so a player with a
	// full bag and a half-filled money cell can still pick money up.
	OutSlotIndex = FindStackWithRoom(Item);
	if (OutSlotIndex != INDEX_NONE)
	{
		return true;
	}

	OutSlotIndex = FindEmptyCell();
	return OutSlotIndex != INDEX_NONE;
}

// ==================== Writing ====================

int32 UInventoryComponent::TryAdd(const FInventoryItem& Item)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		UE_LOG(LogInventory, Warning,
			TEXT("[INV_DEBUG] TryAdd called without authority on %s - ignored"),
			*GetNameSafe(GetOwner()));
		return Item.Count;
	}

	if (!Item.IsValid())
	{
		return 0;
	}

	int32 Remaining = Item.Count;
	bool bChanged = false;

	// Partial stacks first, then empty cells. The loop keeps going while anything still fits,
	// because one currency drop can finish a half-full cell and start the next one.
	while (Remaining > 0)
	{
		int32 TargetIndex = FindStackWithRoom(Item);

		if (TargetIndex != INDEX_NONE)
		{
			FInventorySlot& Slot = Slots[TargetIndex];
			const int32 Room = Slot.StackMax - Slot.Count;
			const int32 Moved = FMath::Min(Room, Remaining);

			Slot.Count += Moved;
			Remaining -= Moved;
			bChanged = true;
			continue;
		}

		// Opening a NEW cell for ammo is capped per weapon: a magazine costs a cell, and the player
		// may only carry so many magazines for one gun until the meta says otherwise. Topping up a
		// magazine that already exists is never capped, which is what makes a half-empty gun worth
		// walking over to a body for.
		if (Item.Kind == EInventorySlotKind::Ammo && GetAmmoCellCount() >= GetMaxAmmoCells())
		{
			break;
		}

		TargetIndex = FindEmptyCell();
		if (TargetIndex == INDEX_NONE)
		{
			// Out of capacity. Whatever is left goes back to the caller, which is what lets a
			// pickup stay in the world holding the remainder instead of vanishing.
			break;
		}

		FInventorySlot& Slot = Slots[TargetIndex];
		Slot.Kind = Item.Kind;
		Slot.Payload = Item.Payload;
		Slot.StackMax = Item.IsStackable() ? Item.StackMax : 1;
		Slot.bInstalled = false;
		Slot.InstalledOnWeapon = nullptr;
		// What it came in as, so throwing it away puts the same actor back on the floor rather
		// than a stand-in. Only the cell that OPENS records it: pouring more money into a cell that
		// already exists does not change what that cell is, and the first pickup's class is as
		// good an answer as the second's.
		Slot.PickupClass = Item.PickupClass;

		const int32 Moved = FMath::Min(Slot.StackMax, Remaining);
		Slot.Count = Moved;
		Remaining -= Moved;
		bChanged = true;
	}

	if (bChanged)
	{
		// The server does not get OnRep, so it broadcasts for itself. On a listen server this is
		// the host's own HUD update.
		OnInventoryChanged.Broadcast();
	}

	return Remaining;
}

bool UInventoryComponent::ClearSlot(int32 SlotIndex)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return false;
	}

	if (!Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].IsEmpty())
	{
		return false;
	}

	Slots[SlotIndex] = FInventorySlot();
	OnInventoryChanged.Broadcast();
	return true;
}

bool UInventoryComponent::CanMergeSlots(const FInventorySlot& Into, const FInventorySlot& From)
{
	return !Into.IsEmpty()
		&& !From.IsEmpty()
		&& Into.IsStackable()
		&& Into.Kind == From.Kind
		&& Into.Payload == From.Payload;
}

bool UInventoryComponent::MoveSlot(int32 FromIndex, int32 ToIndex)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return false;
	}

	if (FromIndex == ToIndex
		|| !Slots.IsValidIndex(FromIndex)
		|| !Slots.IsValidIndex(ToIndex)
		|| Slots[FromIndex].IsEmpty())
	{
		return false;
	}

	// Pouring one stack into another rather than swapping them: dragging half a cell of money onto
	// another half fills one and leaves the rest behind, the same arithmetic a pickup gets from
	// TryAdd. Anything that does not stack falls through to the swap below, which is also what
	// happens when the two cells hold different things.
	if (CanMergeSlots(Slots[ToIndex], Slots[FromIndex]))
	{
		const int32 Room = FMath::Max(0, Slots[ToIndex].StackMax - Slots[ToIndex].Count);
		const int32 Moved = FMath::Min(Room, Slots[FromIndex].Count);
		if (Moved <= 0)
		{
			return false;
		}

		Slots[ToIndex].Count += Moved;
		Slots[FromIndex].Count -= Moved;
		if (Slots[FromIndex].Count <= 0)
		{
			Slots[FromIndex] = FInventorySlot();
		}

		OnInventoryChanged.Broadcast();
		return true;
	}

	Slots.Swap(FromIndex, ToIndex);
	OnInventoryChanged.Broadcast();
	return true;
}

void UInventoryComponent::Server_MoveSlot_Implementation(int32 FromIndex, int32 ToIndex)
{
	MoveSlot(FromIndex, ToIndex);
}

FTransform UInventoryComponent::GetDropTransform() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FTransform::Identity;
	}

	FVector EyeLoc;
	FRotator EyeRot;
	Owner->GetActorEyesViewPoint(EyeLoc, EyeRot);

	// In front of where the player is actually looking, not at their feet: a dropped item that
	// lands underneath the character is a thing the player has to step off to see.
	return FTransform(EyeRot, EyeLoc + EyeRot.Vector() * DropForwardDistance);
}

bool UInventoryComponent::DropSlotToWorld(int32 SlotIndex)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return false;
	}

	if (!Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].IsEmpty())
	{
		return false;
	}

	// A cell held by a fitted attachment is a BILL, not the thing itself: the attachment is on the
	// gun. Throwing this away would put a second copy on the floor while the first stays bolted on,
	// which is a duplication bug rather than a drop. Take the part off the weapon first.
	if (Slots[SlotIndex].bInstalled)
	{
		UE_LOG(LogInventory, Warning, TEXT("[INV_DEBUG] cell %d is held by an attachment fitted to "
			"%s. Take it off the weapon before throwing it away."),
			SlotIndex, *GetNameSafe(Slots[SlotIndex].InstalledOnWeapon));
		return false;
	}

	const FInventorySlot& Source = Slots[SlotIndex];

	// What the player picked up is what the player throws away. Only when the cell was never filled
	// by a pickup at all -- rounds handed over by a weapon drop, a console command -- is there
	// nothing to put back, and then the per-kind class answers instead.
	TSubclassOf<AInventoryPickup> SpawnClass = Source.PickupClass;

	if (!SpawnClass)
	{
		if (const TSubclassOf<AInventoryPickup>* ByKind = DropClassByKind.Find(Source.Kind))
		{
			SpawnClass = *ByKind;
		}
	}
	if (!SpawnClass)
	{
		SpawnClass = DroppedItemClass;
	}

	if (!SpawnClass)
	{
		// Loudly, and without emptying the cell: an item that vanished because a class reference
		// was unset is the worst possible outcome of a mis-click.
		UE_LOG(LogInventory, Warning,
			TEXT("[INV_DEBUG] DropSlotToWorld: cell %d has no pickup to become (kind %d, no per-kind class, no fallback) on %s - nothing dropped, cell kept"),
			SlotIndex, static_cast<int32>(Source.Kind), *GetNameSafe(GetOwner()));
		return false;
	}

	FInventoryItem Dropped;
	Dropped.Kind = Source.Kind;
	Dropped.Payload = Source.Payload;
	Dropped.Count = Source.Count;
	Dropped.StackMax = FMath::Max(1, Source.StackMax);
	Dropped.PickupClass = SpawnClass;

	if (!AInventoryPickup::SpawnForItem(this, SpawnClass, GetDropTransform(), Dropped, 0.0f))
	{
		UE_LOG(LogInventory, Warning, TEXT("[INV_DEBUG] DropSlotToWorld: spawn failed - cell kept"));
		return false;
	}

	// Only after the world actor exists. Clearing first and failing to spawn would delete the item.
	Slots[SlotIndex] = FInventorySlot();
	OnInventoryChanged.Broadcast();

	UE_LOG(LogInventory, Log, TEXT("[INV_DEBUG] dropped cell %d (%d units) into the world"),
		SlotIndex, Dropped.Count);
	return true;
}

void UInventoryComponent::Server_DropSlotToWorld_Implementation(int32 SlotIndex)
{
	DropSlotToWorld(SlotIndex);
}

void UInventoryComponent::SetSlotCount(int32 NewCount)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	const int32 Clamped = FMath::Clamp(NewCount, 1, MaxSlotCount);
	if (Clamped == Slots.Num())
	{
		return;
	}

	Slots.SetNum(Clamped);
	OnInventoryChanged.Broadcast();
}

void UInventoryComponent::SetFreeAttachmentSlots(int32 NewCount)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	const int32 Clamped = FMath::Clamp(NewCount, 0, MaxFreeAttachmentSlots);
	if (Clamped == FreeAttachmentSlots)
	{
		return;
	}

	FreeAttachmentSlots = Clamped;

	// Buying a free slot has to give the cells back that were paying for parts already fitted.
	// Without this the upgrade would only apply to attachments mounted after it was bought, which
	// is the kind of rule nobody can see and everybody feels.
	if (const AShooterCharacter* Character = Cast<AShooterCharacter>(GetOwner()))
	{
		for (AShooterWeapon* Weapon : Character->GetOwnedWeapons())
		{
			RebalanceAttachmentCells(Weapon);
		}
	}

	OnInventoryChanged.Broadcast();
}

// ==================== Debug commands ====================
//
// These exist so the grid can be seen and poked before pickups exist. They are local by nature, so
// they resolve the pawn through CoopPlayers::GetLocalController, and they only do anything on a
// machine with authority - on a listen server that is the host, which is where the grid is being
// looked at during bring-up anyway.

namespace InventoryDebug
{
	static UInventoryComponent* FindLocalInventory(UWorld* World)
	{
		const APlayerController* PC = CoopPlayers::GetLocalController(World);
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		return Pawn ? Pawn->FindComponentByClass<UInventoryComponent>() : nullptr;
	}

	static EInventorySlotKind ParseKind(const FString& Token)
	{
		if (Token.Equals(TEXT("currency"), ESearchCase::IgnoreCase))   { return EInventorySlotKind::Currency; }
		if (Token.Equals(TEXT("ammo"), ESearchCase::IgnoreCase))       { return EInventorySlotKind::Ammo; }
		if (Token.Equals(TEXT("upgrade"), ESearchCase::IgnoreCase))    { return EInventorySlotKind::AbilityUpgrade; }
		if (Token.Equals(TEXT("attachment"), ESearchCase::IgnoreCase)) { return EInventorySlotKind::Attachment; }
		return EInventorySlotKind::Empty;
	}

	static const TCHAR* KindName(EInventorySlotKind Kind)
	{
		switch (Kind)
		{
		case EInventorySlotKind::Currency:       return TEXT("Currency");
		case EInventorySlotKind::Ammo:           return TEXT("Ammo");
		case EInventorySlotKind::AbilityUpgrade: return TEXT("AbilityUpgrade");
		case EInventorySlotKind::Attachment:     return TEXT("Attachment");
		default:                                 return TEXT("Empty");
		}
	}

	static void CmdAdd(const TArray<FString>& Args, UWorld* World)
	{
		UInventoryComponent* Inventory = FindLocalInventory(World);
		if (!Inventory)
		{
			UE_LOG(LogInventory, Warning, TEXT("[INV_DEBUG] no local pawn with an inventory"));
			return;
		}

		if (Args.Num() < 1)
		{
			UE_LOG(LogInventory, Warning, TEXT("[INV_DEBUG] usage: polarity.inv.add <currency|ammo|upgrade|attachment> [count]"));
			return;
		}

		FInventoryItem Item;
		Item.Kind = ParseKind(Args[0]);
		if (Item.Kind == EInventorySlotKind::Empty)
		{
			UE_LOG(LogInventory, Warning, TEXT("[INV_DEBUG] unknown kind '%s'"), *Args[0]);
			return;
		}

		Item.Count = Args.Num() > 1 ? FMath::Max(1, FCString::Atoi(*Args[1])) : 1;
		Item.StackMax = Item.IsStackable() ? Inventory->GetCurrencyStackSize() : 1;

		const int32 Left = Inventory->TryAdd(Item);
		UE_LOG(LogInventory, Log, TEXT("[INV_DEBUG] added %s x%d, %d did not fit, %d empty cells left"),
			KindName(Item.Kind), Item.Count, Left, Inventory->GetEmptyCellCount());
	}

	static void CmdSlots(const TArray<FString>& Args, UWorld* World)
	{
		UInventoryComponent* Inventory = FindLocalInventory(World);
		if (!Inventory || Args.Num() < 1)
		{
			UE_LOG(LogInventory, Warning, TEXT("[INV_DEBUG] usage: polarity.inv.slots <n>"));
			return;
		}

		Inventory->SetSlotCount(FCString::Atoi(*Args[0]));
		UE_LOG(LogInventory, Log, TEXT("[INV_DEBUG] slots now %d of %d"),
			Inventory->GetSlotCount(), Inventory->GetMaxSlotCount());
	}

	static void CmdFreeAttach(const TArray<FString>& Args, UWorld* World)
	{
		UInventoryComponent* Inventory = FindLocalInventory(World);
		if (!Inventory || Args.Num() < 1)
		{
			UE_LOG(LogInventory, Warning, TEXT("[INV_DEBUG] usage: polarity.inv.freeattach <n>"));
			return;
		}

		Inventory->SetFreeAttachmentSlots(FCString::Atoi(*Args[0]));
		UE_LOG(LogInventory, Log, TEXT("[INV_DEBUG] free attachment slots now %d of %d"),
			Inventory->GetFreeAttachmentSlots(), Inventory->GetMaxFreeAttachmentSlots());
	}

	static void CmdClear(const TArray<FString>& Args, UWorld* World)
	{
		UInventoryComponent* Inventory = FindLocalInventory(World);
		if (!Inventory)
		{
			UE_LOG(LogInventory, Warning, TEXT("[INV_DEBUG] no local pawn with an inventory"));
			return;
		}

		if (Args.Num() > 0)
		{
			Inventory->ClearSlot(FCString::Atoi(*Args[0]));
		}
		else
		{
			for (int32 Index = 0; Index < Inventory->GetSlotCount(); ++Index)
			{
				Inventory->ClearSlot(Index);
			}
		}

		UE_LOG(LogInventory, Log, TEXT("[INV_DEBUG] cleared, %d empty cells"), Inventory->GetEmptyCellCount());
	}

	static void CmdDrop(const TArray<FString>& Args, UWorld* World)
	{
		UInventoryComponent* Inventory = FindLocalInventory(World);
		if (!Inventory || Args.Num() < 1)
		{
			UE_LOG(LogInventory, Warning, TEXT("[INV_DEBUG] usage: polarity.inv.drop <index>"));
			return;
		}

		const int32 Index = FCString::Atoi(*Args[0]);
		const bool bDropped = Inventory->DropSlotToWorld(Index);
		UE_LOG(LogInventory, Log, TEXT("[INV_DEBUG] drop of cell %d %s"),
			Index, bDropped ? TEXT("spawned a pickup") : TEXT("did nothing"));
	}

	/** The local player's weapon by slot, so the attachment commands can name one without a UI. */
	static AShooterWeapon* FindLocalWeapon(UWorld* World, int32 WeaponIndex)
	{
		const APlayerController* PC = CoopPlayers::GetLocalController(World);
		const AShooterCharacter* Character = PC ? Cast<AShooterCharacter>(PC->GetPawn()) : nullptr;
		if (!Character)
		{
			return nullptr;
		}

		const TArray<AShooterWeapon*>& Weapons = Character->GetOwnedWeapons();
		return Weapons.IsValidIndex(WeaponIndex) ? Weapons[WeaponIndex] : nullptr;
	}

	/** Put an attachment asset straight in the bag, so the whole chain can be walked before a
	 *  single pickup Blueprint exists. Takes the asset path the content browser shows. */
	static void CmdAttachGive(const TArray<FString>& Args, UWorld* World)
	{
		UInventoryComponent* Inventory = FindLocalInventory(World);
		if (!Inventory || Args.Num() < 1)
		{
			UE_LOG(LogInventory, Warning, TEXT("[INV_DEBUG] usage: polarity.attach.give <asset path>"));
			return;
		}

		UWeaponAttachmentDefinition* Def = LoadObject<UWeaponAttachmentDefinition>(nullptr, *Args[0]);
		if (!Def)
		{
			UE_LOG(LogInventory, Warning, TEXT("[INV_DEBUG] no attachment definition at '%s'"), *Args[0]);
			return;
		}

		FInventoryItem Item;
		Item.Kind = EInventorySlotKind::Attachment;
		Item.Payload = Def;
		Item.Count = 1;
		Item.StackMax = 1;

		const int32 Left = Inventory->TryAdd(Item);
		UE_LOG(LogInventory, Log, TEXT("[INV_DEBUG] gave %s, %s"),
			*GetNameSafe(Def), Left == 0 ? TEXT("taken") : TEXT("no room"));
	}

	static void CmdAttachInstall(const TArray<FString>& Args, UWorld* World)
	{
		UInventoryComponent* Inventory = FindLocalInventory(World);
		if (!Inventory || Args.Num() < 2)
		{
			UE_LOG(LogInventory, Warning, TEXT("[INV_DEBUG] usage: polarity.attach.install <cell> <weapon 0|1>"));
			return;
		}

		AShooterWeapon* Weapon = FindLocalWeapon(World, FCString::Atoi(*Args[1]));
		const bool bDone = Inventory->InstallAttachmentFromSlot(FCString::Atoi(*Args[0]), Weapon);
		UE_LOG(LogInventory, Log, TEXT("[INV_DEBUG] install on %s: %s"),
			*GetNameSafe(Weapon), bDone ? TEXT("done") : TEXT("refused"));
	}

	static void CmdAttachRemove(const TArray<FString>& Args, UWorld* World)
	{
		UInventoryComponent* Inventory = FindLocalInventory(World);
		if (!Inventory || Args.Num() < 2)
		{
			UE_LOG(LogInventory, Warning,
				TEXT("[INV_DEBUG] usage: polarity.attach.remove <weapon 0|1> <optic|magazine|muzzle|stock>"));
			return;
		}

		EWeaponAttachmentType Type = EWeaponAttachmentType::Optic;
		if (Args[1].Equals(TEXT("magazine"), ESearchCase::IgnoreCase)) { Type = EWeaponAttachmentType::Magazine; }
		else if (Args[1].Equals(TEXT("muzzle"), ESearchCase::IgnoreCase)) { Type = EWeaponAttachmentType::Muzzle; }
		else if (Args[1].Equals(TEXT("stock"), ESearchCase::IgnoreCase)) { Type = EWeaponAttachmentType::Stock; }

		AShooterWeapon* Weapon = FindLocalWeapon(World, FCString::Atoi(*Args[0]));
		const bool bDone = Inventory->UninstallAttachment(Weapon, Type);
		UE_LOG(LogInventory, Log, TEXT("[INV_DEBUG] remove from %s: %s"),
			*GetNameSafe(Weapon), bDone ? TEXT("done") : TEXT("refused"));
	}

	static void CmdAttachList(UWorld* World)
	{
		const UInventoryComponent* Inventory = FindLocalInventory(World);
		if (!Inventory)
		{
			UE_LOG(LogInventory, Warning, TEXT("[INV_DEBUG] no local pawn with an inventory"));
			return;
		}

		UE_LOG(LogInventory, Log, TEXT("[INV_DEBUG] %d free attachment slots per weapon"),
			Inventory->GetFreeAttachmentSlots());

		for (int32 WeaponIndex = 0; WeaponIndex < 2; ++WeaponIndex)
		{
			const AShooterWeapon* Weapon = FindLocalWeapon(World, WeaponIndex);
			if (!Weapon)
			{
				continue;
			}

			UE_LOG(LogInventory, Log, TEXT("[INV_DEBUG] weapon %d (%s): %d fitted, %d cells held, zoom %.2f"),
				WeaponIndex, *Weapon->GetName(), Weapon->GetInstalledAttachmentCount(),
				Inventory->GetHeldAttachmentCellCount(Weapon), Weapon->GetADSZoom());

			for (const TObjectPtr<UWeaponAttachmentDefinition>& Def : Weapon->GetInstalledAttachments())
			{
				UE_LOG(LogInventory, Log, TEXT("[INV_DEBUG]     %s (%s)"),
					*GetNameSafe(Def), Def ? *UEnum::GetValueAsString(Def->Type) : TEXT("?"));
			}
		}
	}

	static void CmdDump(UWorld* World)
	{
		UInventoryComponent* Inventory = FindLocalInventory(World);
		if (!Inventory)
		{
			UE_LOG(LogInventory, Warning, TEXT("[INV_DEBUG] no local pawn with an inventory"));
			return;
		}

		UE_LOG(LogInventory, Log, TEXT("[INV_DEBUG] %d/%d cells, %d/%d free attachment slots"),
			Inventory->GetSlotCount(), Inventory->GetMaxSlotCount(),
			Inventory->GetFreeAttachmentSlots(), Inventory->GetMaxFreeAttachmentSlots());

		const TArray<FInventorySlot>& Slots = Inventory->GetSlots();
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			UE_LOG(LogInventory, Log, TEXT("[INV_DEBUG]   [%d] %s %d/%d%s"),
				Index, KindName(Slots[Index].Kind), Slots[Index].Count, Slots[Index].StackMax,
				Slots[Index].bInstalled ? TEXT(" (installed)") : TEXT(""));
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs CmdInvAdd(
	TEXT("polarity.inv.add"),
	TEXT("Put something in the grid. Usage: polarity.inv.add <currency|ammo|upgrade|attachment> [count]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&InventoryDebug::CmdAdd)
);

static FAutoConsoleCommandWithWorldAndArgs CmdInvSlots(
	TEXT("polarity.inv.slots"),
	TEXT("Set how many cells are unlocked. Usage: polarity.inv.slots <n>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&InventoryDebug::CmdSlots)
);

static FAutoConsoleCommandWithWorldAndArgs CmdInvFreeAttach(
	TEXT("polarity.inv.freeattach"),
	TEXT("Set free attachment slots per weapon. Usage: polarity.inv.freeattach <n>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&InventoryDebug::CmdFreeAttach)
);

static FAutoConsoleCommandWithWorldAndArgs CmdInvClear(
	TEXT("polarity.inv.clear"),
	TEXT("Empty one cell, or all of them. Usage: polarity.inv.clear [index]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&InventoryDebug::CmdClear)
);

static FAutoConsoleCommandWithWorldAndArgs CmdInvDrop(
	TEXT("polarity.inv.drop"),
	TEXT("Throw one cell into the world as a pickup. Usage: polarity.inv.drop <index>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&InventoryDebug::CmdDrop)
);

static FAutoConsoleCommandWithWorld CmdInvDump(
	TEXT("polarity.inv.dump"),
	TEXT("Print the grid."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&InventoryDebug::CmdDump)
);

static FAutoConsoleCommandWithWorldAndArgs CmdAttachGiveCmd(
	TEXT("polarity.attach.give"),
	TEXT("Put an attachment asset in the bag. Usage: polarity.attach.give /Game/Path/DA_Scope.DA_Scope"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&InventoryDebug::CmdAttachGive)
);

static FAutoConsoleCommandWithWorldAndArgs CmdAttachInstallCmd(
	TEXT("polarity.attach.install"),
	TEXT("Fit the attachment in a cell to a weapon. Usage: polarity.attach.install <cell> <weapon 0|1>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&InventoryDebug::CmdAttachInstall)
);

static FAutoConsoleCommandWithWorldAndArgs CmdAttachRemoveCmd(
	TEXT("polarity.attach.remove"),
	TEXT("Take an attachment off. Usage: polarity.attach.remove <weapon 0|1> <optic|magazine|muzzle|stock>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&InventoryDebug::CmdAttachRemove)
);

static FAutoConsoleCommandWithWorld CmdAttachListCmd(
	TEXT("polarity.attach.list"),
	TEXT("Print what is fitted to each weapon, and the resulting ADS zoom."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&InventoryDebug::CmdAttachList)
);
