// UpgradeChoiceWidget.cpp

#include "UpgradeChoiceWidget.h"

#include "XPSubsystem.h"
#include "UpgradeRegistry.h"
#include "UpgradeDefinition.h"
#include "UpgradeManagerComponent.h"
#include "DeferredUpgradeQueueSubsystem.h"

#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

void UUpgradeChoiceWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Prefer the deferred queue: it acts as a pass-through when no arena is capturing,
	// and stashes level-ups during combat so popups don't interrupt the fight. The widget's
	// own PendingLevelUps counter still handles ordering once popups are released.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UDeferredUpgradeQueueSubsystem* Deferred = GI->GetSubsystem<UDeferredUpgradeQueueSubsystem>())
		{
			Deferred->OnDeferredLevelUpReleased.AddDynamic(this, &UUpgradeChoiceWidget::HandleLevelUp);
			UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] UpgradeChoiceWidget::NativeConstruct — subscribed to DeferredQueue::OnDeferredLevelUpReleased"));
		}
		else if (UXPSubsystem* XP = GetXPSubsystem())
		{
			// Fallback: subsystem absent (shouldn't happen at runtime, but keeps editor / tests sane)
			UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] UpgradeChoiceWidget::NativeConstruct — DeferredQueue unavailable, FALLBACK to direct XP binding"));
			XP->OnLevelUp.AddDynamic(this, &UUpgradeChoiceWidget::HandleLevelUp);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[UPGRADE_DEBUG] UpgradeChoiceWidget::NativeConstruct — NEITHER DeferredQueue NOR XPSubsystem available!"));
		}
	}

	SetVisibility(ESlateVisibility::Collapsed);
}

void UUpgradeChoiceWidget::NativeDestruct()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UDeferredUpgradeQueueSubsystem* Deferred = GI->GetSubsystem<UDeferredUpgradeQueueSubsystem>())
		{
			Deferred->OnDeferredLevelUpReleased.RemoveDynamic(this, &UUpgradeChoiceWidget::HandleLevelUp);
		}
	}
	if (UXPSubsystem* XP = GetXPSubsystem())
	{
		// Safe even if we never subscribed via XP — RemoveDynamic is a no-op for missing bindings
		XP->OnLevelUp.RemoveDynamic(this, &UUpgradeChoiceWidget::HandleLevelUp);
	}

	// Defensive: if widget gets destroyed mid-choice, restore game state.
	if (bIsOpen)
	{
		UGameplayStatics::SetGamePaused(this, false);
		if (APlayerController* PC = GetOwningPlayer())
		{
			FInputModeGameOnly Mode;
			PC->SetInputMode(Mode);
			PC->SetShowMouseCursor(false);
		}
		bIsOpen = false;
	}
	PendingLevelUps = 0;

	Super::NativeDestruct();
}

UXPSubsystem* UUpgradeChoiceWidget::GetXPSubsystem() const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UXPSubsystem>();
	}
	return nullptr;
}

UUpgradeManagerComponent* UUpgradeChoiceWidget::GetUpgradeManager() const
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			return Pawn->FindComponentByClass<UUpgradeManagerComponent>();
		}
	}
	return nullptr;
}

void UUpgradeChoiceWidget::HandleLevelUp(int32 NewLevel)
{
	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] Widget::HandleLevelUp — RECEIVED lvl=%d, bIsOpen=%d, IsInViewport=%d"),
		NewLevel, bIsOpen ? 1 : 0, IsInViewport() ? 1 : 0);

	if (bIsOpen)
	{
		++PendingLevelUps;
		UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] Widget::HandleLevelUp — Choice already open, queued (%d pending)"), PendingLevelUps);
		return;
	}

	RollChoices();
	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] Widget::HandleLevelUp — RollChoices returned %d choices (Registry=%s)"),
		CurrentChoices.Num(), Registry ? *Registry->GetName() : TEXT("NULL"));

	if (CurrentChoices.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[UPGRADE_DEBUG] Widget::HandleLevelUp — NO upgrades available, skipping (Registry empty / all maxed?)"));
		TryProcessNextPending();
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] Widget::HandleLevelUp — Opening choice popup"));
	OpenChoice();
}

void UUpgradeChoiceWidget::RollChoices()
{
	CurrentChoices.Reset();

	if (!Registry)
	{
		UE_LOG(LogTemp, Warning, TEXT("[XP_DEBUG] UpgradeChoiceWidget has no Registry — set it in WBP defaults"));
		return;
	}

	UUpgradeManagerComponent* Manager = GetUpgradeManager();

	const int32 TotalInRegistry = Registry->AllUpgrades.Num();
	int32 SkippedNull = 0;
	int32 SkippedMaxed = 0;
	int32 SkippedConflicts = 0;

	// Pool = every upgrade in the registry that isn't null, isn't already at max level,
	// and doesn't conflict with an upgrade the player already owns (mutual exclusion).
	// No category filter — a single roll can mix Movement / Melee / EMF / Weapon cards.
	TArray<UUpgradeDefinition*> Pool;
	Pool.Reserve(TotalInRegistry);
	for (UUpgradeDefinition* Def : Registry->AllUpgrades)
	{
		if (!Def) { ++SkippedNull; continue; }
		if (Manager && Manager->IsUpgradeMaxedOut(Def))
		{
			++SkippedMaxed;
			continue;
		}
		if (Manager && Manager->OwnsConflicting(Def))
		{
			++SkippedConflicts;
			continue;
		}
		Pool.Add(Def);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[XP_DEBUG] RollChoices: registry=%d -> pool=%d (skipped: null=%d, maxed=%d, conflicts=%d)"),
		TotalInRegistry, Pool.Num(), SkippedNull, SkippedMaxed, SkippedConflicts);

	const int32 N = FMath::Min(ChoiceCount, Pool.Num());
	for (int32 i = 0; i < N && Pool.Num() > 0; ++i)
	{
		const int32 Idx = FMath::RandRange(0, Pool.Num() - 1);
		UUpgradeDefinition* Picked = Pool[Idx];
		CurrentChoices.Add(Picked);
		Pool.RemoveAtSwap(Idx);

		// Don't also offer an upgrade mutually exclusive with the one just picked — the archetypes
		// (e.g. full-HP vs low-HP) are an either/or choice, never both on the same screen.
		if (Picked)
		{
			Pool.RemoveAll([Picked](UUpgradeDefinition* Other)
			{
				return Other && (Picked->MutuallyExclusiveWith.Contains(Other->UpgradeTag)
					|| Other->MutuallyExclusiveWith.Contains(Picked->UpgradeTag));
			});
		}
	}
}

void UUpgradeChoiceWidget::OpenChoice()
{
	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] Widget::OpenChoice — IsInViewport=%d, OwningPlayer=%s"),
		IsInViewport() ? 1 : 0, GetOwningPlayer() ? *GetOwningPlayer()->GetName() : TEXT("NULL"));

	bIsOpen = true;
	SetVisibility(ESlateVisibility::Visible);

	if (APlayerController* PC = GetOwningPlayer())
	{
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(TakeWidget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(true);
	}

	UGameplayStatics::SetGamePaused(this, true);
	BP_OnChoiceOpened();

	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] Widget::OpenChoice — done (BP_OnChoiceOpened called)"));
}

void UUpgradeChoiceWidget::CloseChoice(UUpgradeDefinition* SelectedDefinition)
{
	UGameplayStatics::SetGamePaused(this, false);

	if (APlayerController* PC = GetOwningPlayer())
	{
		FInputModeGameOnly Mode;
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(false);
	}

	SetVisibility(ESlateVisibility::Collapsed);
	bIsOpen = false;

	BP_OnChoiceClosed(SelectedDefinition);

	TryProcessNextPending();
}

void UUpgradeChoiceWidget::TryProcessNextPending()
{
	while (PendingLevelUps > 0 && !bIsOpen)
	{
		--PendingLevelUps;

		RollChoices();
		if (CurrentChoices.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[XP_DEBUG] Queued level-up has no upgrades — skipping (%d still pending)"), PendingLevelUps);
			continue;
		}
		OpenChoice();
		break;
	}
}

void UUpgradeChoiceWidget::ConfirmChoice(int32 Index)
{
	if (!bIsOpen)
	{
		UE_LOG(LogTemp, Warning, TEXT("[XP_DEBUG] ConfirmChoice called while choice not open — ignored"));
		return;
	}

	if (!CurrentChoices.IsValidIndex(Index))
	{
		UE_LOG(LogTemp, Warning, TEXT("[XP_DEBUG] ConfirmChoice invalid index %d (have %d)"),
			Index, CurrentChoices.Num());
		return;
	}

	UUpgradeDefinition* Selected = CurrentChoices[Index];
	if (!Selected)
	{
		CloseChoice(nullptr);
		return;
	}

	if (UUpgradeManagerComponent* Manager = GetUpgradeManager())
	{
		const bool bGranted = Manager->GrantUpgrade(Selected);
		UE_LOG(LogTemp, Log, TEXT("[XP_DEBUG] ConfirmChoice idx=%d -> %s (granted=%d)"),
			Index, *Selected->GetName(), bGranted ? 1 : 0);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[XP_DEBUG] No UpgradeManagerComponent on player pawn — upgrade not applied"));
	}

	CloseChoice(Selected);
}
