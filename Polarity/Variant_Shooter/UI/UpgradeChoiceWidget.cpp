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
	// own PendingCategories queue still handles ordering once popups are released.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UDeferredUpgradeQueueSubsystem* Deferred = GI->GetSubsystem<UDeferredUpgradeQueueSubsystem>())
		{
			Deferred->OnDeferredLevelUpReleased.AddDynamic(this, &UUpgradeChoiceWidget::HandleSkillLevelUp);
			UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] UpgradeChoiceWidget::NativeConstruct — subscribed to DeferredQueue::OnDeferredLevelUpReleased"));
		}
		else if (UXPSubsystem* XP = GetXPSubsystem())
		{
			// Fallback: subsystem absent (shouldn't happen at runtime, but keeps editor / tests sane)
			UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] UpgradeChoiceWidget::NativeConstruct — DeferredQueue unavailable, FALLBACK to direct XP binding"));
			XP->OnSkillLevelUp.AddDynamic(this, &UUpgradeChoiceWidget::HandleSkillLevelUp);
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
			Deferred->OnDeferredLevelUpReleased.RemoveDynamic(this, &UUpgradeChoiceWidget::HandleSkillLevelUp);
		}
	}
	if (UXPSubsystem* XP = GetXPSubsystem())
	{
		// Safe even if we never subscribed via XP — RemoveDynamic is a no-op for missing bindings
		XP->OnSkillLevelUp.RemoveDynamic(this, &UUpgradeChoiceWidget::HandleSkillLevelUp);
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
	PendingCategories.Reset();

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

void UUpgradeChoiceWidget::HandleSkillLevelUp(ESkillCategory Category, int32 NewLevel)
{
	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] Widget::HandleSkillLevelUp — RECEIVED cat=%d, lvl=%d, bIsOpen=%d, IsInViewport=%d"),
		(int32)Category, NewLevel, bIsOpen ? 1 : 0, IsInViewport() ? 1 : 0);

	if (bIsOpen)
	{
		PendingCategories.Add(Category);
		UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] Widget::HandleSkillLevelUp — Choice already open, queued (%d pending)"), PendingCategories.Num());
		return;
	}

	RollChoicesForCategory(Category);
	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] Widget::HandleSkillLevelUp — RollChoices returned %d choices (Registry=%s)"),
		CurrentChoices.Num(), Registry ? *Registry->GetName() : TEXT("NULL"));

	if (CurrentChoices.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[UPGRADE_DEBUG] Widget::HandleSkillLevelUp — NO upgrades available for skill %d, skipping (Registry pool empty / wrong category / all maxed?)"), (int32)Category);
		TryProcessNextPending();
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] Widget::HandleSkillLevelUp — Opening choice popup"));
	OpenChoice(Category);
}

void UUpgradeChoiceWidget::RollChoicesForCategory(ESkillCategory Category)
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
	int32 SkippedCategory = 0;
	int32 SkippedOwned = 0;

	TArray<UUpgradeDefinition*> Pool;
	Pool.Reserve(TotalInRegistry);
	for (UUpgradeDefinition* Def : Registry->AllUpgrades)
	{
		if (!Def) { ++SkippedNull; continue; }
		if (Def->Category != Category)
		{
			++SkippedCategory;
			UE_LOG(LogTemp, Verbose, TEXT("[XP_DEBUG]   skip %s — category %d != %d"),
				*Def->GetName(), (int32)Def->Category, (int32)Category);
			continue;
		}
		if (Manager && Manager->IsUpgradeMaxedOut(Def))
		{
			++SkippedOwned;
			continue;
		}
		Pool.Add(Def);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[XP_DEBUG] RollChoices skill=%d: registry=%d -> pool=%d (skipped: null=%d, wrong-category=%d, maxed=%d)"),
		(int32)Category, TotalInRegistry, Pool.Num(), SkippedNull, SkippedCategory, SkippedOwned);

	const int32 N = FMath::Min(ChoiceCount, Pool.Num());
	for (int32 i = 0; i < N; ++i)
	{
		const int32 Idx = FMath::RandRange(0, Pool.Num() - 1);
		CurrentChoices.Add(Pool[Idx]);
		Pool.RemoveAtSwap(Idx);
	}
}

void UUpgradeChoiceWidget::OpenChoice(ESkillCategory Category)
{
	UE_LOG(LogTemp, Warning, TEXT("[UPGRADE_DEBUG] Widget::OpenChoice — opening for cat=%d, IsInViewport=%d, OwningPlayer=%s"),
		(int32)Category, IsInViewport() ? 1 : 0, GetOwningPlayer() ? *GetOwningPlayer()->GetName() : TEXT("NULL"));

	bIsOpen = true;
	CurrentCategory = Category;
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
	BP_OnChoiceOpened(Category);

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
	while (PendingCategories.Num() > 0 && !bIsOpen)
	{
		const ESkillCategory NextCat = PendingCategories[0];
		PendingCategories.RemoveAt(0);

		RollChoicesForCategory(NextCat);
		if (CurrentChoices.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[XP_DEBUG] Queued skill %d has no upgrades — skipping"), (int32)NextCat);
			continue;
		}
		OpenChoice(NextCat);
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
