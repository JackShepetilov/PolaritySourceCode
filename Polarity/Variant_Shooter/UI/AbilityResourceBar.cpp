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
#include "TutorialSubsystem.h"
#include "InputAction.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "InputCoreTypes.h"
#include "TimerManager.h"

const FName UAbilityResourceBar::Key_Ability = FName("ability.active");
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
		// Heal-entry visibility depends on owning a consumer upgrade — re-evaluate on grant/remove.
		Upgrades->OnUpgradeGranted.AddDynamic(this, &UAbilityResourceBar::HandleUpgradeGranted);
		Upgrades->OnUpgradeRemoved.AddDynamic(this, &UAbilityResourceBar::HandleUpgradeRemoved);
	}

	// --- Weapon: ammo count + melee durability + inventory changes ---
	InCharacter->OnBulletCountUpdated.AddDynamic(this, &UAbilityResourceBar::HandleBulletCount);
	InCharacter->OnMeleeWeaponEquipped.AddDynamic(this, &UAbilityResourceBar::HandleMeleeEquipped);
	InCharacter->OnWeaponInventoryChanged.AddDynamic(this, &UAbilityResourceBar::HandleWeaponInventoryChanged);

	// --- Seed initial state so already-owned things show immediately ---
	RefreshAbilityEntry();
	RefreshHealEntry();
	RefreshWeaponEntries();

	// Keybind hints resolve to nothing until the player's input mapping contexts are added AND rebuilt
	// (AShooterPlayerController::BeginPlay adds the IMCs slightly AFTER this widget is created, and the
	// rebuild itself is deferred). QueryKeysMappedToAction returns empty until then, which made the
	// hints appear with very inconsistent success. Re-resolve the hints a couple of times shortly after
	// init — by then the mappings are live. Idempotent: re-running the refreshes only re-applies hints
	// to existing entries (no re-create, no re-celebration). Weak self-check keeps it safe across respawn
	// / widget teardown (mirrors the outro timer in RemoveEntry). On respawn the IMCs are already live,
	// so these passes are simply harmless no-op re-applies.
	if (UWorld* World = GetWorld())
	{
		TWeakObjectPtr<UAbilityResourceBar> WeakThis(this);
		FTimerDelegate Reresolve = FTimerDelegate::CreateLambda([WeakThis]()
		{
			if (UAbilityResourceBar* Bar = WeakThis.Get())
			{
				Bar->RefreshAbilityEntry();
				Bar->RefreshHealEntry();
				Bar->RefreshWeaponEntries();
			}
		});
		FTimerHandle WarmupHandle1, WarmupHandle2;
		World->GetTimerManager().SetTimer(WarmupHandle1, Reresolve, 0.15f, false);
		World->GetTimerManager().SetTimer(WarmupHandle2, Reresolve, 0.6f,  false);
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
		BoundUpgradeManager->OnUpgradeGranted.RemoveDynamic(this, &UAbilityResourceBar::HandleUpgradeGranted);
		BoundUpgradeManager->OnUpgradeRemoved.RemoveDynamic(this, &UAbilityResourceBar::HandleUpgradeRemoved);
	}
	if (BoundCharacter.IsValid())
	{
		BoundCharacter->OnBulletCountUpdated.RemoveDynamic(this, &UAbilityResourceBar::HandleBulletCount);
		BoundCharacter->OnMeleeWeaponEquipped.RemoveDynamic(this, &UAbilityResourceBar::HandleMeleeEquipped);
		BoundCharacter->OnWeaponInventoryChanged.RemoveDynamic(this, &UAbilityResourceBar::HandleWeaponInventoryChanged);
	}

	BoundAbilityComp = nullptr;
	BoundUpgradeManager = nullptr;
	BoundCharacter = nullptr;
	LastSeenActiveWeapon = nullptr;
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

	UBarEntryWidget* Entry = AddOrGetEntry(Key_Ability, AbilityEntryClass, FName("barfirst.ability"));
	if (!Entry)
	{
		return;
	}

	Entry->SetIcon(Active->Icon);

	// Keybind hint: the key that activates the ability (resolved live, reflects rebinds).
	ApplyKeybindHint(Entry, BoundCharacter.IsValid() ? BoundCharacter->GetAbilityAction() : nullptr);

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
	// Fires for the ACTIVE weapon: on activation, on every shot, and (for melee) on durability change.
	AShooterWeapon* Weapon = BoundCharacter.IsValid() ? BoundCharacter->GetCurrentWeapon() : nullptr;
	if (!Weapon)
	{
		return;
	}

	// On a weapon SWITCH, rebuild the whole weapon row so switch-to-equip hints toggle correctly
	// (the active weapon hides its hint, the one we left now shows its). On a plain shot (same weapon)
	// just update that weapon's count cheaply.
	const bool bActiveChanged = (LastSeenActiveWeapon.Get() != Weapon);
	LastSeenActiveWeapon = Weapon;
	if (bActiveChanged)
	{
		RefreshWeaponEntries();
		return;
	}

	const bool bMelee = Weapon->IsMeleeWeapon();
	if (!bMelee && !Weapon->bHasLimitedAmmo)
	{
		return; // starter / auto-refill weapon — no tracked entry
	}

	const FName Key = WeaponEntryKey(Weapon);
	if (TObjectPtr<UBarEntryWidget>* Found = Entries.Find(Key))
	{
		if (*Found)
		{
			// Melee: Mag(==MaxHitCount)>0 means limited durability -> show number. Limited ammo: always.
			(*Found)->SetCount(Bullets, bMelee ? (MagazineSize > 0) : true);
		}
	}
}

void UAbilityResourceBar::HandleMeleeEquipped(bool /*bEquipped*/, int32 /*RemainingHits*/, int32 /*MaxHits*/)
{
	// Equip/unequip changes which weapon is active (affects switch-to-equip hints) and can create or
	// remove the melee entry (e.g. weapon break). A full resync against the inventory covers every case.
	RefreshWeaponEntries();
}

void UAbilityResourceBar::HandleWeaponInventoryChanged()
{
	// A weapon was added or removed — re-enumerate and resync the per-weapon entries.
	RefreshWeaponEntries();
}

// ==================== Resource (heal charges) ====================

void UAbilityResourceBar::HandleHealPoolChanged(int32 CurrentCount, int32 /*MaxCount*/)
{
	// Show the heal-charge entry only while a consumer upgrade (HealthBlast / ChargedPunch / …) is owned —
	// otherwise the count has nothing to spend it on.
	const bool bHasConsumer = BoundUpgradeManager.IsValid() && BoundUpgradeManager->HasStoredHealthPickupConsumer();
	if (CurrentCount > 0 && bHasConsumer)
	{
		if (UBarEntryWidget* Entry = AddOrGetEntry(Key_Heal, CountEntryClass, FName("barfirst.heal")))
		{
			Entry->SetIcon(HealIcon);
			Entry->SetCount(CurrentCount, true);
			// Hint: the key that SPENDS the charges, per the currently-owned consumer (null -> no hint).
			ApplyKeybindHint(Entry, BoundUpgradeManager->GetHealSpendInputAction());
		}
	}
	else
	{
		RemoveEntry(Key_Heal);
	}
}

void UAbilityResourceBar::HandleUpgradeGranted(UUpgradeDefinition* /*Definition*/)
{
	// A consumer upgrade may have just been gained — a nonzero pool can now reveal the heal entry.
	RefreshHealEntry();
}

void UAbilityResourceBar::HandleUpgradeRemoved(UUpgradeDefinition* /*Definition*/)
{
	// Losing the last consumer should hide the heal entry.
	RefreshHealEntry();
}

void UAbilityResourceBar::RefreshHealEntry()
{
	if (BoundUpgradeManager.IsValid())
	{
		HandleHealPoolChanged(BoundUpgradeManager->GetStoredHealthPickups(),
			BoundUpgradeManager->GetMaxStoredHealthPickups());
	}
	else
	{
		RemoveEntry(Key_Heal);
	}
}

// ==================== Entry management ====================

UBarEntryWidget* UAbilityResourceBar::AddOrGetEntry(FName Key, TSubclassOf<UBarEntryWidget> EntryClass, FName FirstTimeCategory)
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
	Entries.Add(Key, Entry);

	// First-time-acquisition celebration (per category, per session): play the louder intro the very
	// first time this category appears; otherwise the normal intro.
	bool bFirstTime = false;
	if (!FirstTimeCategory.IsNone())
	{
		if (UTutorialSubsystem* Tut = GetTutorialSubsystem())
		{
			bFirstTime = Tut->MarkBarEntryFirstShownIfNew(FirstTimeCategory);
		}
	}
	if (bFirstTime)
	{
		Entry->PlayFirstTimeIntro();
	}
	else
	{
		Entry->PlayIntro();
	}

	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_BAR] Added entry '%s' (firstTime=%d, container now has %d children)"),
		*Key.ToString(), bFirstTime ? 1 : 0, EntryContainer->GetChildrenCount());
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

// ==================== Per-weapon entries ====================

FName UAbilityResourceBar::WeaponEntryKey(const AShooterWeapon* Weapon)
{
	return Weapon ? FName(*FString::Printf(TEXT("weapon.%u"), Weapon->GetUniqueID())) : NAME_None;
}

bool UAbilityResourceBar::IsWeaponEntryKey(FName Key)
{
	return Key.ToString().StartsWith(TEXT("weapon."));
}

void UAbilityResourceBar::RefreshWeaponEntries()
{
	if (!BoundCharacter.IsValid())
	{
		return;
	}
	AShooterCharacter* Char = BoundCharacter.Get();
	const AShooterWeapon* Current = Char->GetCurrentWeapon();

	// Build the set of per-weapon keys that SHOULD exist after this pass.
	TSet<FName> DesiredKeys;

	for (AShooterWeapon* W : Char->GetOwnedWeapons())
	{
		if (!W)
		{
			continue;
		}
		const bool bMelee = W->IsMeleeWeapon();
		const bool bLimited = W->bHasLimitedAmmo;
		if (!bMelee && !bLimited)
		{
			continue; // starter / auto-refill weapon — intentionally not tracked
		}

		const FName Key = WeaponEntryKey(W);
		DesiredKeys.Add(Key);

		const FName Category = bMelee ? FName("barfirst.melee") : FName("barfirst.ammo");
		UBarEntryWidget* Entry = AddOrGetEntry(Key, CountEntryClass, Category);
		if (!Entry)
		{
			continue;
		}

		Entry->SetIcon(bMelee ? MeleeIcon : AmmoIcon);
		// Melee: Mag(==MaxHitCount)>0 means limited durability -> show number. Limited ammo: always show.
		Entry->SetCount(W->GetBulletCount(), bMelee ? (W->GetMagazineSize() > 0) : true);

		// "Press X to equip" hint only on NON-active weapons (no point pointing at the one you hold).
		ApplyKeybindHint(Entry, (W == Current) ? nullptr : Char->GetSwitchInputActionForWeapon(W));
	}

	// Remove entries for weapons no longer owned (or no longer qualifying).
	TArray<FName> ToRemove;
	for (const auto& Pair : Entries)
	{
		if (IsWeaponEntryKey(Pair.Key) && !DesiredKeys.Contains(Pair.Key))
		{
			ToRemove.Add(Pair.Key);
		}
	}
	for (const FName& Key : ToRemove)
	{
		RemoveEntry(Key);
	}
}

// ==================== Keybind hint / tutorial subsystem ====================

void UAbilityResourceBar::ApplyKeybindHint(UBarEntryWidget* Entry, UInputAction* Action)
{
	if (!Entry)
	{
		return;
	}
	if (!Action)
	{
		// Hide the hint (e.g. on the active weapon, or when no consumer/action is set).
		UE_LOG(LogTemp, Warning, TEXT("[ABILITY_BAR_HINT] entry='%s' -> HIDDEN (active weapon, or no action assigned)"),
			*Entry->GetEntryKey().ToString());
		Entry->SetKeybindHint(FText::GetEmpty(), nullptr);
		return;
	}

	APlayerController* PC = GetOwningPlayer();
	FText KeyText = FText::GetEmpty();
	UTexture2D* Icon = nullptr;
	FKey ResolvedKey = EKeys::Invalid;
	if (UTutorialSubsystem* Tut = GetTutorialSubsystem())
	{
		ResolvedKey = Tut->GetFirstKeyForInputAction(Action, PC);
		if (ResolvedKey.IsValid())
		{
			KeyText = ResolvedKey.GetDisplayName();
		}
		Icon = Tut->GetIconForInputAction(Action, PC);
	}

	// Diagnostic: shows WHY a hint is empty. firstKey='None' means the action resolves to NO key in the
	// active mapping contexts (unmapped or broken/duplicated InputAction) — the code DID apply the hint,
	// there's simply no key to show. This is the per-weapon counterpart to the [ABILITY_INPUT_DEBUG] dump.
	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_BAR_HINT] entry='%s' action='%s' firstKey='%s'"),
		*Entry->GetEntryKey().ToString(), *GetNameSafe(Action), *ResolvedKey.ToString());

	Entry->SetKeybindHint(KeyText, Icon);
}

UTutorialSubsystem* UAbilityResourceBar::GetTutorialSubsystem() const
{
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<UTutorialSubsystem>();
		}
	}
	return nullptr;
}
