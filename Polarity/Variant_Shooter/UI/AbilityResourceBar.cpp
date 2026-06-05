// AbilityResourceBar.cpp

#include "AbilityResourceBar.h"
#include "BarEntryWidget.h"
#include "Components/HorizontalBox.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/BorderSlot.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Abilities/AbilityComponent.h"
#include "Variant_Shooter/Abilities/AbilityDefinition.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Polarity/Upgrades/UpgradeManagerComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

const FName UAbilityResourceBar::Key_Ability = FName("ability.active");
const FName UAbilityResourceBar::Key_Ammo    = FName("weapon.ammo");
const FName UAbilityResourceBar::Key_Melee   = FName("weapon.melee");
const FName UAbilityResourceBar::Key_Heal    = FName("resource.heal");

void UAbilityResourceBar::InitializeFor(AShooterCharacter* InCharacter)
{
	// Rebind cleanly (respawn re-uses the same bar widget).
	UnbindAll();

	if (!InCharacter)
	{
		return;
	}
	BoundCharacter = InCharacter;

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_BAR] InitializeFor Char=%s | EntryContainer=%s | AbilityEntryClass=%s | CountEntryClass=%s"),
		*GetNameSafe(InCharacter),
		EntryContainer ? TEXT("OK") : TEXT("NULL <- BindWidget: Horizontal Box must be named exactly 'EntryContainer'"),
		*GetNameSafe(AbilityEntryClass),
		*GetNameSafe(CountEntryClass));

	// --- Pin the entry row to the RIGHT edge of its parent (code-side, not via WBP slot setup) ---
	// The row's right edge stays fixed; it grows leftward as entries are added (new entries go to
	// index 0 in AddOrGetEntry). Handles the common parent slot types for a HUD widget.
	if (EntryContainer)
	{
		// We want the FIRST entry to sit on the RIGHT and each NEW entry to extend the row LEFTWARD.
		// A live HorizontalBox always builds new Slate slots by APPENDING to the right
		// (UHorizontalBoxSlot::BuildSlot -> SHorizontalBox::AddSlot), so InsertChildAt(0) does NOT
		// visually reorder at runtime — it only touches the Slots array. Instead we flip the whole
		// arrangement with Right-To-Left flow: the logical-first child renders rightmost, and each
		// appended (newer) child renders further to the left. Entries are added with AddChild below.
		EntryContainer->SetFlowDirectionPreference(EFlowDirectionPreference::RightToLeft);

		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_BAR] EntryContainer parent slot class = %s"),
			EntryContainer->Slot ? *EntryContainer->Slot->GetClass()->GetName() : TEXT("NULL"));

		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(EntryContainer->Slot))
		{
			CanvasSlot->SetAutoSize(true);                 // hug content instead of stretching
			FAnchors Anchors = CanvasSlot->GetAnchors();
			Anchors.Minimum.X = 1.0f;                       // pin to the RIGHT edge, keep current Y
			Anchors.Maximum.X = 1.0f;
			CanvasSlot->SetAnchors(Anchors);
			FVector2D Align = CanvasSlot->GetAlignment();
			Align.X = 1.0f;                                 // box's own right edge sits on the anchor
			CanvasSlot->SetAlignment(Align);
			FVector2D Pos = CanvasSlot->GetPosition();
			Pos.X = 0.0f;                                   // glue the right edge exactly to screen-right
			CanvasSlot->SetPosition(Pos);
		}
		else if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(EntryContainer->Slot))
		{
			OverlaySlot->SetHorizontalAlignment(HAlign_Right);
		}
		else if (UBorderSlot* BorderSlot = Cast<UBorderSlot>(EntryContainer->Slot))
		{
			BorderSlot->SetHorizontalAlignment(HAlign_Right);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[ABILITY_BAR] Parent slot not Canvas/Overlay/Border - right-pin NOT applied. Put the Horizontal Box directly inside a Canvas Panel."));
		}
	}

	// --- Abilities (active ability + shared cooldown) ---
	if (UAbilityComponent* Ability = InCharacter->GetAbilityComponent())
	{
		BoundAbilityComp = Ability;
		Ability->OnAbilityAdded.AddDynamic(this, &UAbilityResourceBar::HandleAbilityAdded);
		Ability->OnAbilityRemoved.AddDynamic(this, &UAbilityResourceBar::HandleAbilityRemoved);
		Ability->OnAbilitySwitched.AddDynamic(this, &UAbilityResourceBar::HandleAbilitySwitched);
		Ability->OnCooldownStarted.AddDynamic(this, &UAbilityResourceBar::HandleCooldownStarted);
		Ability->OnCooldownEnded.AddDynamic(this, &UAbilityResourceBar::HandleCooldownEnded);
	}

	// --- Heal charges (shared Health-Blast / Charged-Punch pool) ---
	if (UUpgradeManagerComponent* Upgrades = InCharacter->GetUpgradeManager())
	{
		BoundUpgradeManager = Upgrades;
		Upgrades->OnStoredHealthPickupsChanged.AddDynamic(this, &UAbilityResourceBar::HandleHealPoolChanged);
	}

	// --- Weapon: ammo count + melee durability ---
	InCharacter->OnBulletCountUpdated.AddDynamic(this, &UAbilityResourceBar::HandleBulletCount);
	InCharacter->OnMeleeWeaponEquipped.AddDynamic(this, &UAbilityResourceBar::HandleMeleeEquipped);

	// --- Seed initial state so already-owned things show immediately ---
	RefreshAbilityEntry();

	if (BoundUpgradeManager.IsValid())
	{
		HandleHealPoolChanged(BoundUpgradeManager->GetStoredHealthPickups(),
			BoundUpgradeManager->GetMaxStoredHealthPickups());
	}

	// Ammo seed (melee is seeded by the OnMeleeWeaponEquipped event path when a melee is equipped).
	if (AShooterWeapon* Weapon = InCharacter->GetCurrentWeapon())
	{
		if (!Weapon->IsMeleeWeapon())
		{
			HandleBulletCount(Weapon->GetMagazineSize(), Weapon->GetBulletCount());
		}
	}
}

void UAbilityResourceBar::Shutdown()
{
	UnbindAll();

	for (auto& Pair : Entries)
	{
		if (Pair.Value)
		{
			Pair.Value->RemoveFromParent();
		}
	}
	Entries.Empty();

	if (EntryContainer)
	{
		EntryContainer->ClearChildren();
	}
}

void UAbilityResourceBar::NativeDestruct()
{
	Shutdown();
	Super::NativeDestruct();
}

void UAbilityResourceBar::UnbindAll()
{
	if (BoundAbilityComp.IsValid())
	{
		BoundAbilityComp->OnAbilityAdded.RemoveDynamic(this, &UAbilityResourceBar::HandleAbilityAdded);
		BoundAbilityComp->OnAbilityRemoved.RemoveDynamic(this, &UAbilityResourceBar::HandleAbilityRemoved);
		BoundAbilityComp->OnAbilitySwitched.RemoveDynamic(this, &UAbilityResourceBar::HandleAbilitySwitched);
		BoundAbilityComp->OnCooldownStarted.RemoveDynamic(this, &UAbilityResourceBar::HandleCooldownStarted);
		BoundAbilityComp->OnCooldownEnded.RemoveDynamic(this, &UAbilityResourceBar::HandleCooldownEnded);
	}
	if (BoundUpgradeManager.IsValid())
	{
		BoundUpgradeManager->OnStoredHealthPickupsChanged.RemoveDynamic(this, &UAbilityResourceBar::HandleHealPoolChanged);
	}
	if (BoundCharacter.IsValid())
	{
		BoundCharacter->OnBulletCountUpdated.RemoveDynamic(this, &UAbilityResourceBar::HandleBulletCount);
		BoundCharacter->OnMeleeWeaponEquipped.RemoveDynamic(this, &UAbilityResourceBar::HandleMeleeEquipped);
	}

	BoundAbilityComp = nullptr;
	BoundUpgradeManager = nullptr;
	BoundCharacter = nullptr;
	LastCooldownDuration = 0.0f;
}

// ==================== Ability ====================

void UAbilityResourceBar::HandleAbilityAdded(int32 /*SlotIndex*/)    { RefreshAbilityEntry(); }
void UAbilityResourceBar::HandleAbilityRemoved(int32 /*SlotIndex*/)  { RefreshAbilityEntry(); }
void UAbilityResourceBar::HandleAbilitySwitched(int32 /*NewActiveSlot*/) { RefreshAbilityEntry(); }

void UAbilityResourceBar::HandleCooldownStarted(float Duration)
{
	LastCooldownDuration = Duration;
	if (TObjectPtr<UBarEntryWidget>* Found = Entries.Find(Key_Ability))
	{
		if (*Found)
		{
			(*Found)->SetCooldown(Duration, Duration);
		}
	}
}

void UAbilityResourceBar::HandleCooldownEnded()
{
	LastCooldownDuration = 0.0f;
	if (TObjectPtr<UBarEntryWidget>* Found = Entries.Find(Key_Ability))
	{
		if (*Found)
		{
			(*Found)->SetCooldown(0.0f, 0.0f);
		}
	}
}

void UAbilityResourceBar::RefreshAbilityEntry()
{
	if (!BoundAbilityComp.IsValid())
	{
		RemoveEntry(Key_Ability);
		return;
	}

	UAbilityDefinition* Active = BoundAbilityComp->GetActiveAbility();
	if (!Active)
	{
		RemoveEntry(Key_Ability);
		return;
	}

	UBarEntryWidget* Entry = AddOrGetEntry(Key_Ability, AbilityEntryClass);
	if (!Entry)
	{
		return;
	}

	Entry->SetIcon(Active->Icon);

	// Re-apply the shared cooldown if one is currently running (e.g. switched slots mid-cooldown).
	if (BoundAbilityComp->IsOnCooldown())
	{
		const float Remaining = BoundAbilityComp->GetCooldownRemaining();
		const float Total = (LastCooldownDuration > 0.0f) ? LastCooldownDuration : Remaining;
		Entry->SetCooldown(Remaining, Total);
	}
	else
	{
		Entry->SetCooldown(0.0f, 0.0f);
	}
}

// ==================== Weapon ====================

void UAbilityResourceBar::HandleBulletCount(int32 MagazineSize, int32 Bullets)
{
	// Resolve the weapon this broadcast belongs to. OnBulletCountUpdated fires on weapon activation
	// (so CurrentWeapon is the just-equipped weapon) and on every shot.
	AShooterWeapon* Weapon = BoundCharacter.IsValid() ? BoundCharacter->GetCurrentWeapon() : nullptr;

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_BAR] BulletCount: Weapon=%s IsMelee=%d bHasLimitedAmmo=%d Mag=%d Bullets=%d"),
		*GetNameSafe(Weapon), Weapon ? (int32)Weapon->IsMeleeWeapon() : -1, Weapon ? (int32)Weapon->bHasLimitedAmmo : -1, MagazineSize, Bullets);

	// Melee weapons reuse this broadcast (via AShooterWeapon_Melee::BroadcastDurabilityUpdate) to
	// deliver their remaining-hit count. On first pickup that count arrives AFTER OnMeleeWeaponEquipped
	// (which carried 0/0), so route it to the melee entry here to fill in the number. The entry itself
	// is created/removed by HandleMeleeEquipped — we only refresh its count.
	if (Weapon && Weapon->IsMeleeWeapon())
	{
		if (TObjectPtr<UBarEntryWidget>* Found = Entries.Find(Key_Melee))
		{
			if (*Found)
			{
				(*Found)->SetCount(Bullets, MagazineSize > 0); // Mag>0 == limited durability
			}
		}
		RemoveEntry(Key_Ammo);
		return;
	}

	// Non-melee: only limited-ammo weapons get an ammo entry. Starter/NPC-drop weapons auto-refill
	// (bHasLimitedAmmo == false) and are intentionally excluded.
	if (Weapon && Weapon->bHasLimitedAmmo)
	{
		if (UBarEntryWidget* Entry = AddOrGetEntry(Key_Ammo, CountEntryClass))
		{
			Entry->SetIcon(AmmoIcon);
			Entry->SetCount(Bullets, true);
		}
	}
	else
	{
		RemoveEntry(Key_Ammo);
	}
}

void UAbilityResourceBar::HandleMeleeEquipped(bool bEquipped, int32 RemainingHits, int32 MaxHits)
{
	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_BAR] MeleeEquipped: bEquipped=%d RemainingHits=%d MaxHits=%d"),
		(int32)bEquipped, RemainingHits, MaxHits);

	if (bEquipped)
	{
		if (UBarEntryWidget* Entry = AddOrGetEntry(Key_Melee, CountEntryClass))
		{
			Entry->SetIcon(MeleeIcon);
			// MaxHits == 0 means infinite durability — show the icon without a number.
			Entry->SetCount(RemainingHits, MaxHits > 0);
		}
	}
	else
	{
		RemoveEntry(Key_Melee);
	}
}

// ==================== Resource (heal charges) ====================

void UAbilityResourceBar::HandleHealPoolChanged(int32 CurrentCount, int32 /*MaxCount*/)
{
	if (CurrentCount > 0)
	{
		if (UBarEntryWidget* Entry = AddOrGetEntry(Key_Heal, CountEntryClass))
		{
			Entry->SetIcon(HealIcon);
			Entry->SetCount(CurrentCount, true);
		}
	}
	else
	{
		RemoveEntry(Key_Heal);
	}
}

// ==================== Entry management ====================

int32 UAbilityResourceBar::GetEntryPriority(FName Key)
{
	if (Key == Key_Ability) return 0;
	if (Key == Key_Ammo || Key == Key_Melee) return 1;
	if (Key == Key_Heal) return 2;
	return 10;
}

UBarEntryWidget* UAbilityResourceBar::AddOrGetEntry(FName Key, TSubclassOf<UBarEntryWidget> EntryClass)
{
	if (TObjectPtr<UBarEntryWidget>* Found = Entries.Find(Key))
	{
		return *Found;
	}

	if (!EntryClass || !EntryContainer)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_BAR] AddOrGetEntry(%s) ABORT: EntryClass=%s EntryContainer=%s"),
			*Key.ToString(), *GetNameSafe(EntryClass), EntryContainer ? TEXT("OK") : TEXT("NULL"));
		return nullptr;
	}

	UBarEntryWidget* Entry = CreateWidget<UBarEntryWidget>(GetOwningPlayer(), EntryClass);
	if (!Entry)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_BAR] AddOrGetEntry(%s) CreateWidget returned NULL"), *Key.ToString());
		return nullptr;
	}
	Entry->InitEntry(Key);

	// Append to the logical END of the row. Because the container uses Right-To-Left flow (set in
	// InitializeFor), the FIRST entry renders on the RIGHT and each new appended entry extends the row
	// LEFTWARD. We deliberately AddChild (append) rather than InsertChildAt(0): a live HorizontalBox
	// builds new Slate slots at the end regardless of index, so InsertChildAt would not reorder visuals.
	EntryContainer->AddChild(Entry);
	Entry->PlayIntro();
	Entries.Add(Key, Entry);

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_BAR] Added entry '%s' at left end (container now has %d children)"),
		*Key.ToString(), EntryContainer->GetChildrenCount());
	return Entry;
}

void UAbilityResourceBar::RemoveEntry(FName Key)
{
	TObjectPtr<UBarEntryWidget>* Found = Entries.Find(Key);
	if (!Found || !*Found)
	{
		return;
	}

	UBarEntryWidget* Entry = *Found;
	Entries.Remove(Key);

	const float OutroLen = Entry->PlayOutro();
	if (OutroLen <= 0.0f)
	{
		Entry->RemoveFromParent();
		return;
	}

	// Keep the entry alive for the outro animation, then unparent it.
	if (UWorld* World = GetWorld())
	{
		TWeakObjectPtr<UBarEntryWidget> WeakEntry = Entry;
		FTimerHandle TimerHandle;
		World->GetTimerManager().SetTimer(TimerHandle, FTimerDelegate::CreateLambda([WeakEntry]()
		{
			if (WeakEntry.IsValid())
			{
				WeakEntry->RemoveFromParent();
			}
		}), OutroLen, false);
	}
	else
	{
		Entry->RemoveFromParent();
	}
}
