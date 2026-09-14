// TurretFeedWidget.cpp

#include "TurretFeedWidget.h"

#include "BuildableDefinition.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "PolarityPalette.h"
#include "Sound/SoundBase.h"
#include "TurretBuildable.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/UI/Hud/HudShapeWidget.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

// ==================== Entry ====================

void UTurretFeedEntryWidget::Setup(const AShooterWeapon* Weapon, int32 KeyNumber)
{
	if (KeyText)
	{
		KeyText->SetText(FText::AsNumber(KeyNumber));
	}
	if (NameText)
	{
		NameText->SetText(Weapon ? Weapon->GetWeaponDisplayName() : FText::GetEmpty());
	}
	if (AmmoText)
	{
		AmmoText->SetText(Weapon
			? FText::Format(NSLOCTEXT("TurretFeed", "Ammo", "{0} / {1}"), FText::AsNumber(Weapon->GetBulletCount()), FText::AsNumber(Weapon->GetMagazineSize()))
			: FText::GetEmpty());
	}
	if (Icon)
	{
		if (Weapon && Weapon->GetIcon())
		{
			Icon->SetBrushFromTexture(Weapon->GetIcon(), false);
			Icon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			Icon->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UTurretFeedEntryWidget::SetState(ETurretFeedEntryState State, const FText& Status)
{
	const FLinearColor Color = State == ETurretFeedEntryState::Available ? AvailableColor : UnavailableColor;
	const FSlateColor SlateColor(Color);
	if (KeyText)
	{
		KeyText->SetColorAndOpacity(SlateColor);
	}
	if (NameText)
	{
		NameText->SetColorAndOpacity(SlateColor);
	}
	if (AmmoText)
	{
		AmmoText->SetColorAndOpacity(SlateColor);
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
		Plate->SetFillColor(PlateRestColor);
	}
	BP_OnStateChanged(State);
}

void UTurretFeedEntryWidget::FlashRefused(FLinearColor Color)
{
	if (Plate)
	{
		Plate->Flash(Color, 0.35f);
	}
	BP_OnRefused();
}

// ==================== Menu ====================

void UTurretFeedWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::Collapsed);
	bMenuVisible = false;
}

void UTurretFeedWidget::NativeBind(AShooterCharacter* Character)
{
	UBuilderComponent* const Found = Character ? Character->GetBuilderComponent() : nullptr;
	if (Character)
	{
		Character->OnWeaponInventoryChanged.AddUniqueDynamic(this, &UTurretFeedWidget::HandleInventoryChanged);
	}
	if (Found)
	{
		Builder = Found;
		Found->OnModeChanged.AddUniqueDynamic(this, &UTurretFeedWidget::HandleModeChanged);
		Found->OnFeedRefused.AddUniqueDynamic(this, &UTurretFeedWidget::HandleFeedRefused);
		ApplyMode(Found->GetMode());
	}
}

void UTurretFeedWidget::NativeUnbind()
{
	if (AShooterCharacter* const Bound = GetBoundCharacter())
	{
		Bound->OnWeaponInventoryChanged.RemoveDynamic(this, &UTurretFeedWidget::HandleInventoryChanged);
	}
	if (UBuilderComponent* const BoundBuilder = Builder.Get())
	{
		BoundBuilder->OnModeChanged.RemoveDynamic(this, &UTurretFeedWidget::HandleModeChanged);
		BoundBuilder->OnFeedRefused.RemoveDynamic(this, &UTurretFeedWidget::HandleFeedRefused);
	}
	Builder.Reset();
	WatchTurret(nullptr);
	ApplyMode(EBuilderMode::Idle);
}

void UTurretFeedWidget::WatchTurret(ATurretBuildable* Turret)
{
	// The rows answer for one turret's vices; when they fill or the level rises, the answers move.
	if (ATurretBuildable* const Old = WatchedTurret.Get())
	{
		Old->OnBuildableChanged.RemoveDynamic(this, &UTurretFeedWidget::HandleTurretChanged);
	}
	WatchedTurret = Turret;
	if (Turret)
	{
		Turret->OnBuildableChanged.AddUniqueDynamic(this, &UTurretFeedWidget::HandleTurretChanged);
	}
}

void UTurretFeedWidget::RebuildEntries()
{
	for (UTurretFeedEntryWidget* Entry : Entries)
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
	const FLinearColor Unavailable = UPolarityPalette::GetColor(UnavailableColorTag, FLinearColor::Gray);
	const FLinearColor PlateRest = UPolarityPalette::GetColor(PlateColorTag, FLinearColor::Black);

	TArray<AShooterWeapon*> Weapons;
	Bound->GetFeedWeapons(Weapons);
	for (int32 Index = 0; Index < Weapons.Num(); ++Index)
	{
		UTurretFeedEntryWidget* const Entry = CreateWidget<UTurretFeedEntryWidget>(this, EntryClass);
		if (!Entry)
		{
			continue;
		}
		Entry->AvailableColor = Available;
		Entry->UnavailableColor = Unavailable;
		Entry->PlateRestColor = PlateRest;
		Entry->Setup(Weapons[Index], FirstKeyNumber + Index);
		EntryPanel->AddChild(Entry);
		Entries.Add(Entry);
	}
	RefreshEntries();
}

void UTurretFeedWidget::RefreshEntries()
{
	UBuilderComponent* const Bound = Builder.Get();
	// A standing turret while feeding; the class defaults (fresh, level 1, empty) while picking the
	// gun for one about to be placed.
	const ATurretBuildable* const Turret = Bound ? Bound->GetFeedTurretDefaults() : nullptr;
	if (!Bound || !Turret)
	{
		return;
	}

	if (TitleText)
	{
		if (Bound->GetMode() == EBuilderMode::PickingWeapon)
		{
			TitleText->SetText(NSLOCTEXT("TurretFeed", "PickTitle", "Turret: pick the gun to give it"));
		}
		else
		{
			const UBuildableDefinition* const Definition = Turret->GetDefinition();
			int32 Free = 0;
			for (int32 Vice = 0; Vice < Turret->GetUnlockedViceCount(); ++Vice)
			{
				Free += Turret->GetViceWeapon(Vice) ? 0 : 1;
			}
			TitleText->SetText(FText::Format(NSLOCTEXT("TurretFeed", "Title", "{0}, level {1}: {2} of {3} vices free"),
				Definition ? Definition->DisplayName : NSLOCTEXT("TurretFeed", "Turret", "Turret"),
				FText::AsNumber(Turret->GetBuildLevel()), FText::AsNumber(Free), FText::AsNumber(Turret->GetUnlockedViceCount())));
		}
	}

	TArray<AShooterWeapon*> Weapons;
	Bound->GetFeedWeapons(Weapons);
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		UTurretFeedEntryWidget* const Entry = Entries[Index];
		const AShooterWeapon* const Weapon = Weapons.IsValidIndex(Index) ? Weapons[Index] : nullptr;
		if (!Entry || !Weapon)
		{
			continue;
		}
		int32 ViceIndex = INDEX_NONE;
		if (Turret->FindViceFor(Weapon->GetClass(), ViceIndex))
		{
			Entry->SetState(ETurretFeedEntryState::Available,
				FText::Format(NSLOCTEXT("TurretFeed", "ToVice", "vice {0}"), FText::AsNumber(ViceIndex + 1)));
		}
		else if (Turret->IsRocketClass(Weapon->GetClass()))
		{
			const bool bLocked = Turret->GetUnlockedViceCount() <= Turret->RocketViceIndex;
			Entry->SetState(ETurretFeedEntryState::Unavailable, bLocked
				? FText::Format(NSLOCTEXT("TurretFeed", "NeedsLevel", "needs level {0}"), FText::AsNumber(Turret->RocketViceIndex + 1))
				: NSLOCTEXT("TurretFeed", "HeavyTaken", "heavy vice taken"));
		}
		else
		{
			Entry->SetState(ETurretFeedEntryState::Unavailable, NSLOCTEXT("TurretFeed", "NoVice", "no free vice"));
		}
	}
}

void UTurretFeedWidget::ApplyMode(EBuilderMode Mode)
{
	const bool bWantShown = Mode == EBuilderMode::Feeding || Mode == EBuilderMode::PickingWeapon;
	if (bWantShown != bMenuVisible)
	{
		bMenuVisible = bWantShown;
		SetVisibility(bMenuVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		BP_OnMenuShown(bMenuVisible);
	}
	UBuilderComponent* const Bound = Builder.Get();
	WatchTurret(bMenuVisible && Bound ? Bound->GetFeedTarget() : nullptr);
	if (bMenuVisible)
	{
		// Rebuilt on every opening: the guns change between visits (one was just given away).
		RebuildEntries();
	}
}

void UTurretFeedWidget::HandleModeChanged(EBuilderMode Mode)
{
	ApplyMode(Mode);
}

void UTurretFeedWidget::HandleFeedRefused(int32 WeaponIndex)
{
	if (Entries.IsValidIndex(WeaponIndex) && Entries[WeaponIndex])
	{
		Entries[WeaponIndex]->FlashRefused(UPolarityPalette::GetColor(RefusedFlashTag, FLinearColor::Red));
	}
	// 2D and local by construction: this widget only exists on the screen of the player who pressed.
	if (RefusedSound)
	{
		UGameplayStatics::PlaySound2D(this, RefusedSound);
	}
}

void UTurretFeedWidget::HandleInventoryChanged()
{
	if (bMenuVisible)
	{
		RebuildEntries();
	}
}

void UTurretFeedWidget::HandleTurretChanged(ABuildableActor* Buildable)
{
	if (bMenuVisible)
	{
		RefreshEntries();
	}
}
