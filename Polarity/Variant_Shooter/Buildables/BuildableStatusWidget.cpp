// BuildableStatusWidget.cpp

#include "BuildableStatusWidget.h"

#include "BuildableActor.h"
#include "BuildableDefinition.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "PolarityPalette.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/ShooterPlayerState.h"
#include "Variant_Shooter/UI/Hud/HudBarWidget.h"

// ==================== Entry ====================

void UBuildableStatusEntryWidget::Follow(ABuildableActor* InBuildable)
{
	Unfollow();
	Buildable = InBuildable;
	if (InBuildable)
	{
		InBuildable->OnBuildableChanged.AddUniqueDynamic(this, &UBuildableStatusEntryWidget::HandleChanged);
	}

	const UBuildableDefinition* const Def = InBuildable ? InBuildable->GetDefinition() : nullptr;
	if (NameText)
	{
		NameText->SetText(Def ? Def->DisplayName : FText::GetEmpty());
		NameText->SetColorAndOpacity(FSlateColor(TextColor));
	}
	if (Icon)
	{
		if (Def && Def->Icon)
		{
			Icon->SetBrushFromTexture(Def->Icon, false);
			Icon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			Icon->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	Refresh(true);
}

void UBuildableStatusEntryWidget::Unfollow()
{
	if (ABuildableActor* const Followed = Buildable.Get())
	{
		Followed->OnBuildableChanged.RemoveDynamic(this, &UBuildableStatusEntryWidget::HandleChanged);
	}
	Buildable.Reset();
}

void UBuildableStatusEntryWidget::NativeDestruct()
{
	Unfollow();
	Super::NativeDestruct();
}

void UBuildableStatusEntryWidget::HandleChanged(ABuildableActor* Changed)
{
	Refresh(false);
}

void UBuildableStatusEntryWidget::Refresh(bool bInstant)
{
	ABuildableActor* const Followed = Buildable.Get();
	if (!Followed)
	{
		return;
	}

	const float MaxHealth = FMath::Max(1.0f, Followed->GetMaxHealth());
	if (HealthBar)
	{
		HealthBar->SetFraction(Followed->GetHealth() / MaxHealth, bInstant);
	}
	if (HealthText)
	{
		HealthText->SetText(FText::Format(NSLOCTEXT("BuildableStatus", "Health", "{0} / {1}"),
			FText::AsNumber(FMath::RoundToInt(Followed->GetHealth())), FText::AsNumber(FMath::RoundToInt(MaxHealth))));
		HealthText->SetColorAndOpacity(FSlateColor(TextColor));
	}
	if (LevelText)
	{
		LevelText->SetText(FText::AsNumber(Followed->GetBuildLevel()));
		LevelText->SetColorAndOpacity(FSlateColor(TextColor));
	}
	if (StatusText)
	{
		FText Status;
		switch (Followed->GetBuildableState())
		{
		case EBuildableState::Constructing:
			Status = FText::Format(NSLOCTEXT("BuildableStatus", "Building", "building {0}%"),
				FText::AsNumber(FMath::RoundToInt(Followed->GetConstructionProgress() * 100.0f)));
			break;
		case EBuildableState::Disabled:
			Status = NSLOCTEXT("BuildableStatus", "Disabled", "disabled");
			break;
		case EBuildableState::Destroyed:
			Status = NSLOCTEXT("BuildableStatus", "Destroyed", "destroyed");
			break;
		default:
			if (Followed->GetUpgradeCost() > 0 && Followed->GetUpgradeMetal() > 0)
			{
				Status = FText::Format(NSLOCTEXT("BuildableStatus", "Upgrade", "upgrade {0} / {1}"),
					FText::AsNumber(Followed->GetUpgradeMetal()), FText::AsNumber(Followed->GetUpgradeCost()));
			}
			break;
		}
		StatusText->SetText(Status);
		StatusText->SetColorAndOpacity(FSlateColor(DimTextColor));
	}
	BP_OnRefreshed(Followed);
}

// ==================== Panel ====================

void UBuildableStatusWidget::NativeBind(AShooterCharacter* Character)
{
	TryBindState();
}

void UBuildableStatusWidget::NativeUnbind()
{
	if (AShooterPlayerState* const Bound = State.Get())
	{
		Bound->OnOwnedBuildablesChanged.RemoveDynamic(this, &UBuildableStatusWidget::HandleOwnedBuildablesChanged);
	}
	State.Reset();
	RebuildEntries();
}

void UBuildableStatusWidget::TryBindState()
{
	AShooterCharacter* const Character = GetBoundCharacter();
	AShooterPlayerState* const Found = Character ? Character->GetPlayerState<AShooterPlayerState>() : nullptr;
	if (!Found || Found == State.Get())
	{
		return;
	}
	if (AShooterPlayerState* const Old = State.Get())
	{
		Old->OnOwnedBuildablesChanged.RemoveDynamic(this, &UBuildableStatusWidget::HandleOwnedBuildablesChanged);
	}
	State = Found;
	Found->OnOwnedBuildablesChanged.AddUniqueDynamic(this, &UBuildableStatusWidget::HandleOwnedBuildablesChanged);
	RebuildEntries();
}

void UBuildableStatusWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!State.IsValid() && GetBoundCharacter())
	{
		TryBindState();
	}
}

void UBuildableStatusWidget::HandleOwnedBuildablesChanged()
{
	RebuildEntries();
}

void UBuildableStatusWidget::RebuildEntries()
{
	// Rows are cheap and the list is at most a handful long: rebuild rather than diff.
	for (UBuildableStatusEntryWidget* Entry : Entries)
	{
		if (Entry)
		{
			Entry->Unfollow();
			Entry->RemoveFromParent();
		}
	}
	Entries.Reset();

	AShooterPlayerState* const Bound = State.Get();
	if (!Bound || !EntryPanel || !EntryClass)
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	const FLinearColor Text = UPolarityPalette::GetColor(TextColorTag, FLinearColor::White);
	const FLinearColor Dim = UPolarityPalette::GetColor(DimTextColorTag, FLinearColor::Gray);

	for (ABuildableActor* Buildable : Bound->GetOwnedBuildables())
	{
		// A reference that has not resolved yet: skipped, and the list announces again once it has.
		if (!Buildable || Buildable->IsDestroyed())
		{
			continue;
		}
		UBuildableStatusEntryWidget* const Entry = CreateWidget<UBuildableStatusEntryWidget>(this, EntryClass);
		if (!Entry)
		{
			continue;
		}
		Entry->TextColor = Text;
		Entry->DimTextColor = Dim;
		Entry->Follow(Buildable);
		EntryPanel->AddChild(Entry);
		Entries.Add(Entry);
	}

	SetVisibility(Entries.Num() > 0 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
}
