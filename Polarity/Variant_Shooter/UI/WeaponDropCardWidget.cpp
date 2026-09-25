// WeaponDropCardWidget.cpp

#include "WeaponDropCardWidget.h"
#include "Variant_Shooter/Weapons/DroppedRangedWeapon.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "PolarityPalette.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "WeaponDropCard"

FWeaponDropCardData UWeaponDropCardWidget::BuildData(const ADroppedRangedWeapon* Drop,
	const AShooterCharacter* Viewer, const FText& KeyLabel)
{
	FWeaponDropCardData Data;
	Data.KeyLabel = KeyLabel;

	const AShooterWeapon* Weapon = (Drop && Drop->WeaponClass)
		? Drop->WeaponClass->GetDefaultObject<AShooterWeapon>() : nullptr;
	if (!Weapon)
	{
		return Data;
	}

	// Read off the class default, which is the gun as authored. The live copy the player would get
	// can differ slightly (a pack profile may resize the magazine in BeginPlay), and the card is a
	// promise about the gun, not a readout of one that does not exist yet.
	Data.WeaponName    = Weapon->GetWeaponDisplayName();
	Data.Description   = Weapon->WeaponDescription;
	Data.Icon          = Weapon->GetIcon();
	Data.AmmoBadge     = Weapon->AmmoBadge;
	Data.AmmoColorTag  = Weapon->AmmoColorTag;
	Data.Damage        = Weapon->GetShotDamage();
	Data.MagazineSize  = Weapon->GetMagazineSize();
	Data.bFullAuto     = Weapon->IsFullAuto();
	const float Refire = Weapon->GetActualRefireRate();
	Data.RoundsPerMinute = Refire > KINDA_SMALL_NUMBER ? 60.0f / Refire : 0.0f;

	// The rounds the drop carries, read the way ADroppedRangedWeapon::CompletePull hands them out:
	// -1 in either field means "nobody set a number" and falls back to the drop's own defaults.
	const int32 Mag = FMath::Max(1, Data.MagazineSize);
	if (Weapon->IsEnergyClass())
	{
		const int32 Loaded = Drop->SpawnedBulletCount >= 0
			? FMath::Clamp(Drop->SpawnedBulletCount, 0, Mag)
			: FMath::Clamp(FMath::RoundToInt(Drop->EnergyMagazineFill * Mag), 0, Mag);
		const int32 Spare = Drop->CarriedEnergyReserve >= 0
			? Drop->CarriedEnergyReserve
			: FMath::RoundToInt(Drop->EnergyReserveMagazines * Mag);
		Data.Rounds = Loaded + FMath::Max(0, Spare);
	}
	else
	{
		Data.Rounds = Drop->SpawnedBulletCount >= 0 ? Drop->SpawnedBulletCount : Mag;
	}

	// What pressing the key would do, by the same rules the grant follows: a gun already carried is
	// worth its rounds, and a new one lands in its hotkey slot and pushes out whatever was there.
	// @see AShooterCharacter::ResolveHotkeySlotForWeaponClass
	if (Viewer)
	{
		if (Viewer->FindWeaponOfType(Drop->WeaponClass))
		{
			Data.TakeKind = EWeaponDropTakeKind::AmmoOnly;
		}
		else if (const AShooterWeapon* Occupant = Viewer->FindOwnedWeaponInHotkeySlot(
			Viewer->ResolveHotkeySlotForWeaponClass(Drop->WeaponClass)))
		{
			Data.TakeKind = EWeaponDropTakeKind::Replace;
			Data.ReplacedWeaponName = Occupant->GetWeaponDisplayName();
		}
	}

	return Data;
}

void UWeaponDropCardWidget::ShowForDrop(const ADroppedRangedWeapon* Drop, const AShooterCharacter* Viewer,
	const FVector2D& ScreenPosition, float BracketPixelRadius, bool bShowFullCard, const FText& KeyLabel)
{
	if (!Drop)
	{
		HideCard();
		return;
	}

	// Rebuilt every frame (it is a handful of reads) but only pushed on change, so a Blueprint may do
	// real work in the event and text is not re-laid out each frame.
	FWeaponDropCardData NewData = BuildData(Drop, Viewer, KeyLabel);
	const bool bChanged = ShownDrop.Get() != Drop
		|| NewData.TakeKind != CardData.TakeKind
		|| !NewData.ReplacedWeaponName.EqualTo(CardData.ReplacedWeaponName)
		|| !NewData.KeyLabel.EqualTo(CardData.KeyLabel);
	if (bChanged)
	{
		ShownDrop = Drop;
		CardData = MoveTemp(NewData);
		ApplyData();
		BP_OnCardDataChanged(CardData);
	}

	if (!bShown || bShowFullCard != bFullCard)
	{
		bFullCard = bShowFullCard;
		if (CompactPanel)
		{
			CompactPanel->SetVisibility(bFullCard ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		}
		if (CardPanel)
		{
			CardPanel->SetVisibility(bFullCard ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
		BP_OnFullCardChanged(bFullCard);
	}

	// The collapsed panel takes no space, so the widget's size is the visible one's and one alignment
	// per mode is enough: the name centred above the brackets, the card to their right, centred on
	// them vertically.
	const float BracketHalf = BracketPixelRadius * BracketPadding;
	if (bFullCard)
	{
		SetAlignmentInViewport(FVector2D(0.0f, 0.5f));
		SetPositionInViewport(ScreenPosition + FVector2D(BracketHalf + CardGap, 0.0f), true);
	}
	else
	{
		SetAlignmentInViewport(FVector2D(0.5f, 1.0f));
		SetPositionInViewport(ScreenPosition - FVector2D(0.0f, BracketHalf + CompactGap), true);
	}

	if (!bShown)
	{
		bShown = true;
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UWeaponDropCardWidget::HideCard()
{
	if (!bShown)
	{
		return;
	}
	bShown = false;
	ShownDrop.Reset();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UWeaponDropCardWidget::ApplyData()
{
	const FText& Name = CardData.WeaponName;

	if (CompactNameText)
	{
		CompactNameText->SetText(Name);
	}
	if (NameText)
	{
		NameText->SetText(Name);
	}
	if (DescriptionText)
	{
		DescriptionText->SetText(CardData.Description);
		DescriptionText->SetVisibility(CardData.Description.IsEmpty()
			? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	if (ActionText)
	{
		FText Verb;
		switch (CardData.TakeKind)
		{
		case EWeaponDropTakeKind::Replace:
			Verb = FText::Format(LOCTEXT("VerbReplace", "REPLACE {0}"), CardData.ReplacedWeaponName);
			break;
		case EWeaponDropTakeKind::AmmoOnly:
			Verb = LOCTEXT("VerbAmmo", "TAKE AMMO");
			break;
		default:
			Verb = LOCTEXT("VerbPickUp", "PICK UP");
			break;
		}
		ActionText->SetText(CardData.KeyLabel.IsEmpty()
			? Verb
			: FText::Format(LOCTEXT("ActionWithKey", "[{0}] {1}"), CardData.KeyLabel, Verb));
	}

	if (FireModeText)
	{
		FireModeText->SetText(CardData.bFullAuto ? LOCTEXT("ModeAuto", "AUTO") : LOCTEXT("ModeSemi", "SEMI"));
	}
	if (DamageText)
	{
		DamageText->SetText(FText::AsNumber(FMath::RoundToInt(CardData.Damage)));
	}
	if (FireRateText)
	{
		FireRateText->SetText(FText::Format(LOCTEXT("Rpm", "{0} RPM"), FText::AsNumber(FMath::RoundToInt(CardData.RoundsPerMinute))));
	}
	if (MagazineText)
	{
		MagazineText->SetText(FText::AsNumber(CardData.MagazineSize));
	}
	if (RoundsText)
	{
		RoundsText->SetText(FText::AsNumber(CardData.Rounds));
	}

	if (IconImage)
	{
		if (CardData.Icon)
		{
			// Matched size so a ScaleBox around it keeps the silhouette's proportions.
			IconImage->SetBrushFromTexture(CardData.Icon, true);
			IconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			IconImage->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	if (AmmoBadgeImage)
	{
		if (CardData.AmmoBadge)
		{
			AmmoBadgeImage->SetBrushFromTexture(CardData.AmmoBadge, true);
			AmmoBadgeImage->SetColorAndOpacity(UPolarityPalette::GetColor(CardData.AmmoColorTag));
			AmmoBadgeImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			AmmoBadgeImage->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

#undef LOCTEXT_NAMESPACE
