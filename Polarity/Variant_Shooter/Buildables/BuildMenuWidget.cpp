// BuildMenuWidget.cpp

#include "BuildMenuWidget.h"

#include "BuildableDefinition.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "PolarityPalette.h"
#include "Sound/SoundBase.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/ShooterPlayerState.h"
#include "Variant_Shooter/UI/Hud/HudShapeWidget.h"

// ==================== Entry ====================

void UBuildMenuEntryWidget::Setup(const UBuildableDefinition* Definition, int32 KeyNumber)
{
	if (KeyText)
	{
		KeyText->SetText(FText::AsNumber(KeyNumber));
	}
	if (NameText)
	{
		NameText->SetText(Definition ? Definition->DisplayName : FText::GetEmpty());
	}
	if (CostText)
	{
		CostText->SetText(Definition ? FText::AsNumber(Definition->MetalCost) : FText::GetEmpty());
	}
	if (Icon)
	{
		if (Definition && Definition->Icon)
		{
			Icon->SetBrushFromTexture(Definition->Icon, false);
			Icon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			Icon->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UBuildMenuEntryWidget::SetState(EBuildMenuEntryState State, int32 BuiltCount, int32 MaxCount, bool bSelected)
{
	FLinearColor Color = AvailableColor;
	FText Status;
	switch (State)
	{
	case EBuildMenuEntryState::Unaffordable:
		Color = UnaffordableColor;
		break;
	case EBuildMenuEntryState::Built:
		Color = BuiltColor;
		Status = NSLOCTEXT("BuildMenu", "Built", "built");
		break;
	default:
		break;
	}
	if (MaxCount > 1)
	{
		// A kind with several copies shows the count whatever its state: "1 / 2" says a teleporter
		// still wants its other end.
		Status = FText::Format(NSLOCTEXT("BuildMenu", "CountOfMax", "{0} / {1}"), FText::AsNumber(BuiltCount), FText::AsNumber(MaxCount));
	}

	const FSlateColor SlateColor(Color);
	if (NameText)
	{
		NameText->SetColorAndOpacity(SlateColor);
	}
	if (CostText)
	{
		CostText->SetColorAndOpacity(SlateColor);
	}
	if (KeyText)
	{
		KeyText->SetColorAndOpacity(SlateColor);
	}
	if (StatusText)
	{
		StatusText->SetText(Status);
		StatusText->SetColorAndOpacity(SlateColor);
	}
	if (Icon)
	{
		Icon->SetColorAndOpacity(Color);
	}
	if (Plate)
	{
		Plate->SetFillColor(bSelected ? PlateSelectedColor : PlateRestColor);
	}
	BP_OnStateChanged(State, BuiltCount, MaxCount, bSelected);
}

void UBuildMenuEntryWidget::FlashRefused(FLinearColor Color)
{
	if (Plate)
	{
		Plate->Flash(Color, 0.35f);
	}
	BP_OnRefused();
}

// ==================== Menu ====================

void UBuildMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::Collapsed);
	bMenuVisible = false;
}

void UBuildMenuWidget::NativeBind(AShooterCharacter* Character)
{
	UBuilderComponent* const Found = Character ? Character->GetBuilderComponent() : nullptr;
	if (Found)
	{
		Builder = Found;
		Found->OnModeChanged.AddUniqueDynamic(this, &UBuildMenuWidget::HandleModeChanged);
		Found->OnSlotRefused.AddUniqueDynamic(this, &UBuildMenuWidget::HandleSlotRefused);
		RebuildEntries();
		ApplyMode(Found->GetMode());
	}
	TryBindState();
}

void UBuildMenuWidget::NativeUnbind()
{
	if (UBuilderComponent* const Bound = Builder.Get())
	{
		Bound->OnModeChanged.RemoveDynamic(this, &UBuildMenuWidget::HandleModeChanged);
		Bound->OnSlotRefused.RemoveDynamic(this, &UBuildMenuWidget::HandleSlotRefused);
	}
	Builder.Reset();
	if (AShooterPlayerState* const BoundState = State.Get())
	{
		BoundState->OnMetalChanged.RemoveDynamic(this, &UBuildMenuWidget::HandleMetalChanged);
		BoundState->OnOwnedBuildablesChanged.RemoveDynamic(this, &UBuildMenuWidget::HandleOwnedBuildablesChanged);
	}
	State.Reset();
	ApplyMode(EBuilderMode::Idle);
}

void UBuildMenuWidget::TryBindState()
{
	// The PlayerState can arrive after the character on a client; the tick keeps asking.
	AShooterCharacter* const Character = GetBoundCharacter();
	AShooterPlayerState* const Found = Character ? Character->GetPlayerState<AShooterPlayerState>() : nullptr;
	if (!Found || Found == State.Get())
	{
		return;
	}
	if (AShooterPlayerState* const Old = State.Get())
	{
		Old->OnMetalChanged.RemoveDynamic(this, &UBuildMenuWidget::HandleMetalChanged);
		Old->OnOwnedBuildablesChanged.RemoveDynamic(this, &UBuildMenuWidget::HandleOwnedBuildablesChanged);
	}
	State = Found;
	Found->OnMetalChanged.AddUniqueDynamic(this, &UBuildMenuWidget::HandleMetalChanged);
	Found->OnOwnedBuildablesChanged.AddUniqueDynamic(this, &UBuildMenuWidget::HandleOwnedBuildablesChanged);
	RefreshEntries();
}

void UBuildMenuWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!State.IsValid() && GetBoundCharacter())
	{
		TryBindState();
	}
}

void UBuildMenuWidget::RebuildEntries()
{
	for (UBuildMenuEntryWidget* Entry : Entries)
	{
		if (Entry)
		{
			Entry->RemoveFromParent();
		}
	}
	Entries.Reset();

	UBuilderComponent* const Bound = Builder.Get();
	if (!Bound || !EntryPanel || !EntryClass)
	{
		return;
	}

	const FLinearColor Available = UPolarityPalette::GetColor(AvailableColorTag, FLinearColor::White);
	const FLinearColor Unaffordable = UPolarityPalette::GetColor(UnaffordableColorTag, FLinearColor::Gray);
	const FLinearColor Built = UPolarityPalette::GetColor(BuiltColorTag, Available);
	const FLinearColor PlateRest = UPolarityPalette::GetColor(PlateColorTag, FLinearColor::Black);
	const FLinearColor PlateSelected = UPolarityPalette::GetColor(PlateSelectedColorTag, PlateRest);

	for (int32 SlotIndex = 0; SlotIndex < Bound->Buildables.Num(); ++SlotIndex)
	{
		UBuildMenuEntryWidget* const Entry = CreateWidget<UBuildMenuEntryWidget>(this, EntryClass);
		if (!Entry)
		{
			continue;
		}
		Entry->AvailableColor = Available;
		Entry->UnaffordableColor = Unaffordable;
		Entry->BuiltColor = Built;
		Entry->PlateRestColor = PlateRest;
		Entry->PlateSelectedColor = PlateSelected;
		Entry->Setup(Bound->GetDefinition(SlotIndex), FirstKeyNumber + SlotIndex);
		EntryPanel->AddChild(Entry);
		Entries.Add(Entry);
	}
	RefreshEntries();
}

void UBuildMenuWidget::RefreshEntries()
{
	UBuilderComponent* const Bound = Builder.Get();
	if (!Bound)
	{
		return;
	}
	for (int32 SlotIndex = 0; SlotIndex < Entries.Num(); ++SlotIndex)
	{
		UBuildMenuEntryWidget* const Entry = Entries[SlotIndex];
		if (!Entry)
		{
			continue;
		}
		const int32 BuiltCount = Bound->CountBuilt(SlotIndex);
		const int32 MaxCount = Bound->GetMaxCount(SlotIndex);
		EBuildMenuEntryState EntryState = EBuildMenuEntryState::Available;
		if (BuiltCount >= MaxCount)
		{
			EntryState = EBuildMenuEntryState::Built;
		}
		else if (!Bound->CanAfford(SlotIndex))
		{
			EntryState = EBuildMenuEntryState::Unaffordable;
		}
		const bool bSelected = Bound->IsPlacing() && Bound->GetPlacingSlot() == SlotIndex;
		Entry->SetState(EntryState, BuiltCount, MaxCount, bSelected);
	}
}

void UBuildMenuWidget::ApplyMode(EBuilderMode Mode)
{
	const bool bWantShown = Mode == EBuilderMode::Menu || (Mode == EBuilderMode::Placing && bShowWhilePlacing);
	if (bWantShown != bMenuVisible)
	{
		bMenuVisible = bWantShown;
		SetVisibility(bMenuVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		BP_OnMenuShown(bMenuVisible);
	}
	if (bMenuVisible)
	{
		RefreshEntries();
	}
}

void UBuildMenuWidget::HandleModeChanged(EBuilderMode Mode)
{
	ApplyMode(Mode);
}

void UBuildMenuWidget::HandleMetalChanged(int32 Metal, int32 MaxMetal, int32 Delta)
{
	RefreshEntries();
}

void UBuildMenuWidget::HandleOwnedBuildablesChanged()
{
	RefreshEntries();
}

void UBuildMenuWidget::HandleSlotRefused(int32 SlotIndex, EBuildablePlacementResult Result)
{
	// Only rows have a plate to flash; a refusal from the ghost (bad spot on confirm) lands on the
	// row of the kind being placed, which is the same index.
	if (Entries.IsValidIndex(SlotIndex) && Entries[SlotIndex])
	{
		Entries[SlotIndex]->FlashRefused(UPolarityPalette::GetColor(RefusedFlashTag, FLinearColor::Red));
	}
	// 2D and local by construction: this widget only exists on the screen of the player who pressed.
	if (RefusedSound)
	{
		UGameplayStatics::PlaySound2D(this, RefusedSound);
	}
}
