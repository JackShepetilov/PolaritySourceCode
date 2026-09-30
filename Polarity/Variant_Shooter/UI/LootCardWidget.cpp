// LootCardWidget.cpp

#include "LootCardWidget.h"
#include "Variant_Shooter/Weapons/DroppedRangedWeapon.h"
#include "Variant_Shooter/Weapons/DroppedMeleeWeapon.h"
#include "Variant_Shooter/Weapons/RiotShieldPickup.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Variant_Shooter/Weapons/ShooterWeapon_Melee.h"
#include "Variant_Shooter/Weapons/WeaponAttachmentDefinition.h"
#include "Variant_Shooter/Pickups/UpgradePickup.h"
#include "Variant_Shooter/Pickups/AbilityPickup.h"
#include "Variant_Shooter/Pickups/ScriptedPickup.h"
#include "Variant_Shooter/Pickups/InventoryPickup.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/Inventory/InventoryIconSettings.h"
#include "Variant_Shooter/Abilities/AbilityDefinition.h"
#include "Variant_Shooter/Buildables/DispenserCardPickup.h"
#include "Variant_Shooter/Buildables/DispenserSlotMachineComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Polarity/Upgrades/DispenserUpgradePool.h"
#include "Polarity/Upgrades/UpgradeManagerComponent.h"
#include "PolarityPalette.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Brushes/SlateColorBrush.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "LootCard"

namespace LootCardFill
{
	FText RarityName(EUpgradeRarity Rarity)
	{
		switch (Rarity)
		{
		case EUpgradeRarity::Rare:      return LOCTEXT("RarityRare", "RARE");
		case EUpgradeRarity::Epic:      return LOCTEXT("RarityEpic", "EPIC");
		case EUpgradeRarity::Legendary: return LOCTEXT("RarityLegendary", "LEGENDARY");
		default:                        return LOCTEXT("RarityCommon", "COMMON");
		}
	}

	FText AttachmentTypeName(EWeaponAttachmentType Type)
	{
		switch (Type)
		{
		case EWeaponAttachmentType::Magazine: return LOCTEXT("TypeMagazine", "MAGAZINE");
		case EWeaponAttachmentType::Muzzle:   return LOCTEXT("TypeMuzzle", "MUZZLE");
		case EWeaponAttachmentType::Stock:    return LOCTEXT("TypeStock", "STOCK");
		default:                              return LOCTEXT("TypeOptic", "OPTIC");
		}
	}

	FText OrClassName(const FText& Name, const UObject* Object)
	{
		return (Name.IsEmpty() && Object) ? FText::FromString(Object->GetName()) : Name;
	}

	void SetRarity(FLootCardData& Data, EUpgradeRarity Rarity)
	{
		Data.bHasRarity = true;
		Data.Rarity = Rarity;
		Data.RarityColor = ULootCardWidget::GetRarityColor(Rarity);
	}

	/** "WEAPON", "UPGRADE | RARE": the category word, then any extra words, then the rarity. */
	FText MakeCategoryLine(const FLootCardData& Data, const FText& Extra = FText::GetEmpty())
	{
		FText Word;
		switch (Data.Category)
		{
		case ELootCardCategory::Weapon:     Word = LOCTEXT("CatWeapon", "WEAPON"); break;
		case ELootCardCategory::Melee:      Word = LOCTEXT("CatMelee", "MELEE"); break;
		case ELootCardCategory::Upgrade:    Word = LOCTEXT("CatUpgrade", "UPGRADE"); break;
		case ELootCardCategory::Ability:    Word = LOCTEXT("CatAbility", "ABILITY"); break;
		case ELootCardCategory::Attachment: Word = LOCTEXT("CatAttachment", "ATTACHMENT"); break;
		case ELootCardCategory::Ammo:       Word = LOCTEXT("CatAmmo", "AMMO"); break;
		case ELootCardCategory::Money:      Word = LOCTEXT("CatMoney", "MONEY"); break;
		case ELootCardCategory::Shield:     Word = LOCTEXT("CatShield", "SHIELD"); break;
		case ELootCardCategory::Buff:       Word = LOCTEXT("CatBuff", "BUFF"); break;
		default:                            Word = LOCTEXT("CatItem", "ITEM"); break;
		}
		FText Line = Word;
		if (!Extra.IsEmpty())
		{
			Line = FText::Format(LOCTEXT("CatJoin", "{0} | {1}"), Line, Extra);
		}
		if (Data.bHasRarity)
		{
			Line = FText::Format(LOCTEXT("CatJoin", "{0} | {1}"), Line, RarityName(Data.Rarity));
		}
		return Line;
	}

	void AddStat(FLootCardData& Data, const FText& Label, const FText& Value)
	{
		FUpgradeStat& Row = Data.Stats.AddDefaulted_GetRef();
		Row.Label = Label;
		Row.Value = Value;
	}

	/** The chip for a gun class: its type's last word ("Weapon.Type.AR" -> "AR"), or its name when
	 *  nobody has set a type yet. */
	FText WeaponChip(TSubclassOf<AShooterWeapon> WeaponClass)
	{
		const AShooterWeapon* const Weapon = WeaponClass ? WeaponClass->GetDefaultObject<AShooterWeapon>() : nullptr;
		if (!Weapon)
		{
			return FText::GetEmpty();
		}
		if (Weapon->WeaponType.IsValid())
		{
			FString Leaf = Weapon->WeaponType.GetTagName().ToString();
			int32 Dot = INDEX_NONE;
			if (Leaf.FindLastChar(TEXT('.'), Dot))
			{
				Leaf.RightChopInline(Dot + 1);
			}
			return FText::FromString(Leaf);
		}
		return Weapon->GetWeaponDisplayName();
	}

	void AddChipOnce(FLootCardData& Data, const FText& Chip)
	{
		if (Chip.IsEmpty())
		{
			return;
		}
		for (const FText& Have : Data.Chips)
		{
			if (Have.EqualTo(Chip))
			{
				return;
			}
		}
		Data.Chips.Add(Chip);
	}

	FText ZoomText(float Zoom)
	{
		FNumberFormattingOptions Format;
		Format.MinimumFractionalDigits = 0;
		Format.MaximumFractionalDigits = 1;
		return FText::Format(LOCTEXT("ZoomX", "{0}x"), FText::AsNumber(Zoom, &Format));
	}

	FText Change(const FText& From, const FText& To)
	{
		return FText::Format(LOCTEXT("StatChange", "{0} > {1}"), From, To);
	}

	void SetOptionalText(UTextBlock* Text, const FText& Value)
	{
		if (Text)
		{
			Text->SetText(Value);
			Text->SetVisibility(Value.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		}
	}

	// ---------- Look ----------
	// Widgets the card looks up by name, so a Blueprint that has them gets the full look and one that
	// does not still works: KeyChip/KeyText (the key in its own chip), BodyBorder (the rarity flood,
	// M_UI_LootCardBody), CompareBorder (the panel under the card), StatsBox (stat rows built here).

	const FLinearColor GainColor(0.49f, 1.0f, 0.54f, 1.0f);
	const FLinearColor LossColor(1.0f, 0.36f, 0.3f, 1.0f);
	const FLinearColor DimColor(0.55f, 0.58f, 0.63f, 1.0f);
	const FLinearColor TextColor(0.91f, 0.92f, 0.94f, 1.0f);

	/** A value of the form "A > B" (LootCardFill::Change) split in two. False for a plain value. */
	bool SplitChange(const FText& Value, FString& OutFrom, FString& OutTo)
	{
		return Value.ToString().Split(TEXT(" > "), &OutFrom, &OutTo);
	}

	UTextBlock* MakeText(UWidgetTree* Tree, const FText& Text, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		UTextBlock* const Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Block->SetText(Text);
		Block->SetFont(Font);
		Block->SetColorAndOpacity(FSlateColor(Color));
		return Block;
	}

	/** One row per stat: the label, then the value; a change shows the old value dim, an arrow and the
	 *  new one in green, with a bar as long as the gain (a doubling or more fills it). */
	void BuildStatRows(UWidgetTree* Tree, UVerticalBox* Box, const TArray<FUpgradeStat>& Stats, const FSlateFontInfo& Font)
	{
		Box->ClearChildren();
		FSlateFontInfo ValueFont = Font;
		ValueFont.TypefaceFontName = TEXT("Bold");
		ValueFont.Size = Font.Size + 3;

		for (const FUpgradeStat& Stat : Stats)
		{
			UHorizontalBox* const Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			if (UVerticalBoxSlot* const RowSlot = Box->AddChildToVerticalBox(Row))
			{
				RowSlot->SetPadding(FMargin(0.0f, 1.0f));
			}

			if (UHorizontalBoxSlot* const LabelSlot = Row->AddChildToHorizontalBox(MakeText(Tree, Stat.Label, Font, TextColor)))
			{
				LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				LabelSlot->SetVerticalAlignment(VAlign_Center);
				LabelSlot->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));
			}

			FString From, To;
			if (!SplitChange(Stat.Value, From, To))
			{
				if (UHorizontalBoxSlot* const ValueSlot = Row->AddChildToHorizontalBox(MakeText(Tree, Stat.Value, ValueFont, TextColor)))
				{
					ValueSlot->SetVerticalAlignment(VAlign_Center);
				}
				continue;
			}

			// The bar and the colour only when both ends are numbers ("11", "1.5x", "25%").
			const bool bNumbers = From.Len() > 0 && To.Len() > 0 && FChar::IsDigit(From[0]) && FChar::IsDigit(To[0]);
			const float A = bNumbers ? FCString::Atof(*From) : 0.0f;
			const float B = bNumbers ? FCString::Atof(*To) : 0.0f;
			const FLinearColor ToColor = (!bNumbers || B >= A) ? GainColor : LossColor;

			const FText Parts[] = { FText::FromString(From), FText::FromString(TEXT(" \u203A ")), FText::FromString(To) };
			const FLinearColor Colors[] = { DimColor, DimColor, ToColor };
			for (int32 Index = 0; Index < 3; ++Index)
			{
				if (UHorizontalBoxSlot* const PartSlot = Row->AddChildToHorizontalBox(MakeText(Tree, Parts[Index], ValueFont, Colors[Index])))
				{
					PartSlot->SetVerticalAlignment(VAlign_Center);
				}
			}

			if (bNumbers)
			{
				UProgressBar* const Bar = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
				FProgressBarStyle Style = Bar->GetWidgetStyle();
				Style.BackgroundImage = FSlateColorBrush(FLinearColor(1.0f, 1.0f, 1.0f, 0.08f));
				Style.FillImage = FSlateColorBrush(FLinearColor::White);
				Bar->SetWidgetStyle(Style);
				Bar->SetFillColorAndOpacity(ToColor);
				const float Gain = A > KINDA_SMALL_NUMBER ? FMath::Abs(B / A - 1.0f) : 1.0f;
				Bar->SetPercent(FMath::Clamp(Gain, 0.08f, 1.0f));

				USizeBox* const Size = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
				Size->SetWidthOverride(110.0f);
				Size->SetHeightOverride(8.0f);
				Size->AddChild(Bar);
				if (UHorizontalBoxSlot* const BarSlot = Row->AddChildToHorizontalBox(Size))
				{
					BarSlot->SetVerticalAlignment(VAlign_Center);
					BarSlot->SetPadding(FMargin(12.0f, 0.0f, 0.0f, 0.0f));
				}
			}
		}
	}

	/** Once a frame while the card shows: the pop-in settles and the light sweep runs. The state lives
	 *  in the panel's own render transform and in the body material, so the widget needs no fields. */
	void Animate(UWidget* Card, UBorder* Body, const FLootCardData& Data, float Delta)
	{
		if (Card)
		{
			// Pop: from 94% and invisible to full size in about a fifth of a second.
			const float Scale = Card->GetRenderTransform().Scale.X;
			if (Scale < 0.999f || Card->GetRenderOpacity() < 0.999f)
			{
				const float NewScale = FMath::FInterpTo(Scale, 1.0f, Delta, 16.0f);
				Card->SetRenderScale(FVector2D(NewScale, NewScale));
				Card->SetRenderOpacity(FMath::FInterpTo(Card->GetRenderOpacity(), 1.0f, Delta, 22.0f));
			}
		}
		UMaterialInstanceDynamic* const Look = Body ? Body->GetDynamicMaterial() : nullptr;
		if (!Look)
		{
			return;
		}
		// Sweep: one band of light across the body; gold keeps sweeping every couple of seconds.
		float Sweep = Look->K2_GetScalarParameterValue(TEXT("Sweep"));
		if (Sweep < 1.4f)
		{
			Sweep += Delta / 0.6f;
		}
		else if (Data.bHasRarity && Data.Rarity == EUpgradeRarity::Legendary)
		{
			Sweep = -1.8f;
		}
		Look->SetScalarParameterValue(TEXT("Sweep"), Sweep);
	}

	// ---------- One branch per kind ----------

	void BuildWeapon(const ADroppedRangedWeapon* Drop, const AShooterCharacter* Viewer, FLootCardData& Data)
	{
		const AShooterWeapon* Weapon = Drop->WeaponClass ? Drop->WeaponClass->GetDefaultObject<AShooterWeapon>() : nullptr;
		if (!Weapon)
		{
			return;
		}

		// Read off the class default, which is the gun as authored. The live copy the player would get
		// can differ slightly (a pack profile may resize the magazine in BeginPlay), and the card is a
		// promise about the gun, not a readout of one that does not exist yet.
		Data.Category      = ELootCardCategory::Weapon;
		Data.Title         = Weapon->GetWeaponDisplayName();
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
		// worth its rounds, and a new one takes an empty slot or pushes out the one in hand.
		// @see AShooterCharacter::ChooseSlotForIncomingWeapon
		if (Viewer)
		{
			if (Viewer->FindWeaponOfType(Drop->WeaponClass))
			{
				Data.TakeKind = ELootTakeKind::AmmoOnly;
			}
			else if (const AShooterWeapon* Occupant = Viewer->FindOwnedWeaponInHotkeySlot(
				Viewer->ChooseSlotForIncomingWeapon()))
			{
				Data.TakeKind = ELootTakeKind::Replace;
				Data.ReplacedName = Occupant->GetWeaponDisplayName();
			}
		}
		// The gun's numbers have their own row on the card (WeaponStatsPanel), so no stat rows here, and
		// its type is the category line itself ("AR"), Apex-style, rather than a chip under it.
		const FText Type = WeaponChip(Drop->WeaponClass);
		Data.CategoryLine = (Weapon->WeaponType.IsValid() && !Type.IsEmpty()) ? Type : MakeCategoryLine(Data);
	}

	void BuildMelee(const ADroppedMeleeWeapon* Drop, FLootCardData& Data)
	{
		const AShooterWeapon_Melee* const Weapon = Drop->MeleeWeaponClass
			? Drop->MeleeWeaponClass->GetDefaultObject<AShooterWeapon_Melee>() : nullptr;
		if (!Weapon)
		{
			return;
		}
		Data.Category    = ELootCardCategory::Melee;
		Data.Title       = Weapon->GetWeaponDisplayName();
		Data.Description = Weapon->WeaponDescription;
		Data.Icon        = Weapon->GetIcon();
		Data.Damage      = Weapon->MeleeDamage;
		AddStat(Data, LOCTEXT("StatDamage", "Damage"), FText::AsNumber(FMath::RoundToInt(Data.Damage)));
		AddStat(Data, LOCTEXT("StatHits", "Hits"), FText::AsNumber(Drop->GrantedHitCount));
		Data.CategoryLine = MakeCategoryLine(Data);
	}

	/** An upgrade and the level it would come at. FromLevel 0 means new. Replaces is the upgrade that
	 *  leaves its slot, when one does. */
	void BuildUpgrade(const UUpgradeDefinition* Def, int32 FromLevel, int32 ToLevel, const UUpgradeDefinition* Replaces,
		bool bMaxed, FLootCardData& Data)
	{
		Data.Category    = ELootCardCategory::Upgrade;
		Data.Title       = OrClassName(Def->DisplayName, Def);
		Data.Description = Def->GetDescriptionForLevel(ToLevel);
		Data.Icon        = Def->Icon;
		Data.Stats       = Def->GetDisplayedStats(ToLevel);
		SetRarity(Data, Def->GetLevelRarity(ToLevel));

		const FText LevelWord = FText::Format(LOCTEXT("UpgradeLevel", "LV {0}"), FText::AsNumber(ToLevel));
		if (bMaxed)
		{
			Data.TakeKind = ELootTakeKind::NoRoom;
		}
		else if (Replaces)
		{
			Data.TakeKind = ELootTakeKind::Replace;
			Data.ReplacedName = OrClassName(Replaces->DisplayName, Replaces);
		}
		else if (FromLevel > 0)
		{
			Data.TakeKind = ELootTakeKind::LevelUp;
		}
		else
		{
			Data.TakeKind = ELootTakeKind::Take;
		}
		if (Data.TakeKind == ELootTakeKind::LevelUp)
		{
			Data.CompareLine = FText::Format(LOCTEXT("CompareLevel", "Level {0} > {1}"),
				FText::AsNumber(FromLevel), FText::AsNumber(ToLevel));
		}
		Data.CategoryLine = MakeCategoryLine(Data, LevelWord);
	}

	void BuildWorldUpgrade(const AUpgradePickup* Pickup, const AShooterCharacter* Viewer, FLootCardData& Data)
	{
		UUpgradeDefinition* const Def = Pickup->UpgradeDefinition;
		if (!Def)
		{
			return;
		}
		// A world pickup grants one level on top of what the player has (UUpgradeManagerComponent::
		// GrantUpgrade). The owning client applies grants too, so its manager knows the level.
		const UUpgradeManagerComponent* const Manager = Viewer ? Viewer->GetUpgradeManager() : nullptr;
		const int32 Have = Manager ? Manager->GetUpgradeLevel(Def->UpgradeTag) : 0;
		const bool bMaxed = Manager && Manager->IsUpgradeMaxedOut(Def);
		BuildUpgrade(Def, Have, bMaxed ? Have : Have + 1, nullptr, bMaxed, Data);
	}

	void BuildAttachment(const UWeaponAttachmentDefinition* Attachment, const AShooterCharacter* Viewer,
		EUpgradeRarity Rarity, FLootCardData& Data)
	{
		Data.Category = ELootCardCategory::Attachment;
		Data.Title    = OrClassName(Attachment->DisplayName, Attachment);
		Data.Icon     = Attachment->GetDisplayIcon();
		SetRarity(Data, Rarity);

		// The kinds of gun it fits, as chips. A magazine keeps its list in its size table.
		if (Attachment->Type == EWeaponAttachmentType::Magazine)
		{
			for (const TPair<TSubclassOf<AShooterWeapon>, int32>& Entry : Attachment->MagazineSizeByWeapon)
			{
				AddChipOnce(Data, WeaponChip(Entry.Key));
			}
		}
		else
		{
			for (const TSubclassOf<AShooterWeapon>& WeaponClass : Attachment->CompatibleWeapons)
			{
				AddChipOnce(Data, WeaponChip(WeaponClass));
			}
		}

		// Against the player's own guns, the ones it fits only: what it would change on each, and what
		// already sits in its place on the first. No gun stats: the card is about the attachment.
		bool bFitsAny = false;
		if (Viewer)
		{
			for (const AShooterWeapon* const Weapon : Viewer->GetOwnedWeapons())
			{
				if (!Weapon || !Attachment->FitsWeapon(Weapon->GetClass()))
				{
					continue;
				}
				const FText GunName = Weapon->GetWeaponDisplayName();
				switch (Attachment->Type)
				{
				// Only what actually changes: a red dot leaves the zoom where it was, and a row saying
				// "1.5x > 1.5x" in gain green would promise something it does not give.
				case EWeaponAttachmentType::Magazine:
				{
					const int32 Now = Weapon->GetMagazineSize();
					const int32 With = Attachment->GetMagazineSizeFor(Weapon->GetClass());
					if (With != Now)
					{
						AddStat(Data, FText::Format(LOCTEXT("StatMagFor", "{0} mag size"), GunName),
							Change(FText::AsNumber(Now), FText::AsNumber(With)));
					}
					break;
				}
				case EWeaponAttachmentType::Optic:
				{
					const float Now = Weapon->GetADSZoom();
					const float With = Weapon->GetADSZoomWith(Attachment);
					if (!FMath::IsNearlyEqual(Now, With, 0.01f))
					{
						AddStat(Data, FText::Format(LOCTEXT("StatZoomFor", "{0} zoom"), GunName),
							Change(ZoomText(Now), ZoomText(With)));
					}
					break;
				}
				default:
					break;
				}
				if (Attachment->bReloadsWhileHolstered)
				{
					AddStat(Data, FText::Format(LOCTEXT("StatHolsterFor", "{0} reloads holstered"), GunName),
						FText::Format(LOCTEXT("StatSeconds", "{0} s"), FText::AsNumber(FMath::RoundToInt(Attachment->HolsteredReloadDelay))));
				}
				if (!bFitsAny)
				{
					bFitsAny = true;
					const UWeaponAttachmentDefinition* const Current = Weapon->GetAttachmentOfType(Attachment->Type);
					Data.ReplacedName = Current ? OrClassName(Current->DisplayName, Current) : FText::GetEmpty();
					Data.CompareLine = Current
						? FText::Format(LOCTEXT("CompareReplaces", "Replace: {0}"), Data.ReplacedName)
						: LOCTEXT("CompareEmpty", "Replace: EMPTY");
				}
			}
		}
		if (Viewer && !bFitsAny)
		{
			Data.CompareLine = LOCTEXT("CompareNoFit", "Fits none of your guns");
		}
		Data.CategoryLine = MakeCategoryLine(Data, AttachmentTypeName(Attachment->Type));
	}

	/** How many units of Item the bag would take right now, the way UInventoryComponent::TryAdd pours
	 *  them: partial stacks of the same thing first, then new cells (ammo capped per weapon). */
	int32 CountFit(const UInventoryComponent& Inventory, const FInventoryItem& Item, int32 StackMax)
	{
		int32 Room = 0;
		int32 Empty = 0;
		for (const FInventorySlot& Cell : Inventory.GetSlots())
		{
			if (Cell.IsEmpty())
			{
				++Empty;
			}
			else if (Cell.Kind == Item.Kind && Cell.Payload == Item.Payload && Cell.HasRoom())
			{
				Room += Cell.StackMax - Cell.Count;
			}
		}
		if (Item.Kind == EInventorySlotKind::Ammo)
		{
			Empty = FMath::Min(Empty, FMath::Max(0, Inventory.GetMaxAmmoCells() - Inventory.GetAmmoCellCount()));
		}
		return Room + Empty * FMath::Max(1, StackMax);
	}

	void BuildInventory(const AInventoryPickup* Pickup, const AShooterCharacter* Viewer, FLootCardData& Data)
	{
		const FInventoryItem& Item = Pickup->Item;
		const UInventoryComponent* const Inventory = Viewer ? Viewer->GetInventoryComponent() : nullptr;

		switch (Item.Kind)
		{
		case EInventorySlotKind::Attachment:
			if (const UWeaponAttachmentDefinition* const Attachment = Cast<UWeaponAttachmentDefinition>(Item.Payload))
			{
				BuildAttachment(Attachment, Viewer, Attachment->Rarity, Data);
			}
			break;
		case EInventorySlotKind::AbilityUpgrade:
			if (const UUpgradeDefinition* const Def = Cast<UUpgradeDefinition>(Item.Payload))
			{
				BuildUpgrade(Def, 0, 1, nullptr, false, Data);
				Data.TakeKind = ELootTakeKind::PickUp;
			}
			break;
		case EInventorySlotKind::Currency:
		case EInventorySlotKind::Ammo:
		{
			const bool bMoney = Item.Kind == EInventorySlotKind::Currency;
			Data.Category = bMoney ? ELootCardCategory::Money : ELootCardCategory::Ammo;
			// The amount is the name ("AMMO x60"); a category line would only repeat the word.
			Data.Title = FText::Format(LOCTEXT("TitleCount", "{0} ×{1}"),
				bMoney ? LOCTEXT("TitleMoney", "Money") : LOCTEXT("TitleAmmo", "Ammo"), FText::AsNumber(Item.Count));
			Data.Count = Item.Count;
			if (Inventory)
			{
				const int32 StackMax = bMoney ? Inventory->GetCurrencyStackSize() : Item.StackMax;
				const int32 Fits = FMath::Min(Item.Count, CountFit(*Inventory, Item, StackMax));
				Data.CompareLine = FText::Format(LOCTEXT("CompareFits", "Fits: {0} of {1}"),
					FText::AsNumber(Fits), FText::AsNumber(Item.Count));
			}
			break;
		}
		default:
			return;
		}

		// Whatever it is, it goes into the bag, and a full bag is the one thing that stops it.
		int32 Unused = INDEX_NONE;
		if (Inventory && Item.IsValid() && !Inventory->CanAccept(Item, Unused))
		{
			Data.TakeKind = ELootTakeKind::NoRoom;
		}
	}

	void BuildDispenserCard(const ADispenserCardPickup* Box, const AShooterCharacter* Viewer, FLootCardData& Data)
	{
		const UDispenserSlotMachineComponent* const Machine = Box->GetMachine();
		const int32 Index = Box->GetCardIndex();
		if (!Machine || !Machine->GetSpin().Cards.IsValidIndex(Index))
		{
			return;
		}
		const FDispenserCard& Card = Machine->GetSpin().Cards[Index];
		switch (Card.Outcome)
		{
		case EDispenserCardOutcome::Upgrade:
			if (const UUpgradeDefinition* const Def = Card.Upgrade.Definition)
			{
				// The machine already worked out the level and the slot when it rolled.
				BuildUpgrade(Def, Card.Upgrade.FromLevel, Card.Upgrade.ToLevel,
					Card.Upgrade.Kind == EUpgradeOfferKind::Replace ? Card.Upgrade.Replaces.Get() : nullptr, false, Data);
				SetRarity(Data, Card.Upgrade.Rarity);
				Data.CategoryLine = MakeCategoryLine(Data,
					FText::Format(LOCTEXT("UpgradeLevel", "LV {0}"), FText::AsNumber(Card.Upgrade.ToLevel)));
			}
			break;
		case EDispenserCardOutcome::Attachment:
			if (const UWeaponAttachmentDefinition* const Attachment = Machine->GetCardAttachment(Index))
			{
				BuildAttachment(Attachment, Viewer, Card.Rarity, Data);
				Data.TakeKind = ELootTakeKind::PickUp;
			}
			break;
		case EDispenserCardOutcome::Buff:
			if (const UDispenserBuffDefinition* const Buff = Machine->GetCardBuff(Index))
			{
				Data.Category = ELootCardCategory::Buff;
				Data.Title = OrClassName(Buff->DisplayName, Buff);
				Data.Icon = Buff->Icon;
				Data.TakeKind = ELootTakeKind::Take;
				FText Label;
				switch (Buff->Kind)
				{
				case EDispenserBuffKind::Armor: Label = LOCTEXT("BuffArmor", "Armor"); break;
				case EDispenserBuffKind::Ammo:  Label = LOCTEXT("BuffAmmo", "Magazines"); break;
				default:                        Label = LOCTEXT("BuffHeal", "Health"); break;
				}
				AddStat(Data, Label, FText::Format(LOCTEXT("BuffAmount", "+{0}"), FText::AsNumber(FMath::RoundToInt(Buff->Amount))));
				Data.CategoryLine = MakeCategoryLine(Data);
			}
			break;
		default:
			// A blank box: no card.
			break;
		}
	}
}

FLinearColor ULootCardWidget::GetRarityColor(EUpgradeRarity Rarity)
{
	// One source for rarity colours, shared with the inventory's icons.
	return UInventoryIconSettings::GetRarityColor(Rarity);
}

bool ULootCardWidget::BuildData(const AActor* Target, const AShooterCharacter* Viewer, const FText& KeyLabel,
	FLootCardData& OutData)
{
	OutData = FLootCardData();
	OutData.KeyLabel = KeyLabel;
	if (!Target)
	{
		return false;
	}

	if (const ADroppedRangedWeapon* const Drop = Cast<ADroppedRangedWeapon>(Target))
	{
		LootCardFill::BuildWeapon(Drop, Viewer, OutData);
	}
	else if (const ADroppedMeleeWeapon* const Melee = Cast<ADroppedMeleeWeapon>(Target))
	{
		LootCardFill::BuildMelee(Melee, OutData);
	}
	else if (const AUpgradePickup* const Upgrade = Cast<AUpgradePickup>(Target))
	{
		LootCardFill::BuildWorldUpgrade(Upgrade, Viewer, OutData);
	}
	else if (const AAbilityPickup* const AbilityPickup = Cast<AAbilityPickup>(Target))
	{
		if (const UAbilityDefinition* const Def = AbilityPickup->AbilityDefinition)
		{
			OutData.Category    = ELootCardCategory::Ability;
			OutData.Title       = LootCardFill::OrClassName(Def->DisplayName, Def);
			OutData.Description = Def->Description;
			OutData.Icon        = Def->Icon;
			OutData.TakeKind    = ELootTakeKind::Take;
			LootCardFill::AddStat(OutData, LOCTEXT("StatLevel", "Level"), FText::AsNumber(AbilityPickup->GrantedLevel));
			OutData.CategoryLine = LootCardFill::MakeCategoryLine(OutData);
		}
	}
	else if (const AScriptedPickup* const Scripted = Cast<AScriptedPickup>(Target))
	{
		OutData.Category    = ELootCardCategory::Item;
		OutData.Title       = LootCardFill::OrClassName(Scripted->DisplayName, Scripted);
		OutData.Description = Scripted->Description;
		OutData.Icon        = Scripted->Icon;
		OutData.TakeKind    = ELootTakeKind::Take;
		OutData.CategoryLine = LootCardFill::MakeCategoryLine(OutData);
	}
	else if (const AInventoryPickup* const Inventory = Cast<AInventoryPickup>(Target))
	{
		LootCardFill::BuildInventory(Inventory, Viewer, OutData);
	}
	else if (Cast<ARiotShieldPickup>(Target))
	{
		OutData.Category = ELootCardCategory::Shield;
		OutData.Title    = LOCTEXT("TitleShield", "Riot Shield");
		OutData.CategoryLine = LootCardFill::MakeCategoryLine(OutData);
	}
	else if (const ADispenserCardPickup* const Box = Cast<ADispenserCardPickup>(Target))
	{
		LootCardFill::BuildDispenserCard(Box, Viewer, OutData);
	}

	if (OutData.Title.IsEmpty())
	{
		return false;
	}

	switch (OutData.TakeKind)
	{
	case ELootTakeKind::Replace:
		OutData.ActionVerb = FText::Format(LOCTEXT("VerbReplace", "REPLACE {0}"), OutData.ReplacedName);
		break;
	case ELootTakeKind::AmmoOnly:
		OutData.ActionVerb = LOCTEXT("VerbAmmo", "TAKE AMMO");
		break;
	case ELootTakeKind::Take:
		OutData.ActionVerb = LOCTEXT("VerbTake", "TAKE");
		break;
	case ELootTakeKind::LevelUp:
		OutData.ActionVerb = LOCTEXT("VerbLevelUp", "LEVEL UP");
		break;
	case ELootTakeKind::NoRoom:
		OutData.ActionVerb = OutData.Category == ELootCardCategory::Upgrade
			? LOCTEXT("VerbMaxed", "MAX LEVEL") : LOCTEXT("VerbFull", "BAG FULL");
		break;
	default:
		OutData.ActionVerb = LOCTEXT("VerbPickUp", "PICK UP");
		break;
	}
	return true;
}

void ULootCardWidget::ShowFor(const AActor* Target, const AShooterCharacter* Viewer,
	const FVector2D& ScreenPosition, float BracketPixelRadius, bool bShowFullCard, const FText& KeyLabel)
{
	// Rebuilt every frame (a handful of reads) but only pushed on change, so a Blueprint may do real
	// work in the event and text is not re-laid out each frame.
	FLootCardData NewData;
	if (!BuildData(Target, Viewer, KeyLabel, NewData))
	{
		HideCard();
		return;
	}

	const bool bChanged = ShownTarget.Get() != Target
		|| NewData.TakeKind != CardData.TakeKind
		|| !NewData.Title.EqualTo(CardData.Title)
		|| !NewData.ActionVerb.EqualTo(CardData.ActionVerb)
		|| !NewData.CompareLine.EqualTo(CardData.CompareLine)
		|| NewData.Count != CardData.Count
		|| !NewData.KeyLabel.EqualTo(CardData.KeyLabel);
	// A new item (or the card coming back) pops in and sweeps a light across; a changed verb on the same
	// item only updates the text.
	const bool bNewItem = !bShown || ShownTarget.Get() != Target || (bShowFullCard && !bFullCard);
	if (bChanged)
	{
		ShownTarget = Target;
		CardData = MoveTemp(NewData);
		ApplyData();
		BP_OnCardDataChanged(CardData);
	}
	UBorder* const Body = Cast<UBorder>(GetWidgetFromName(TEXT("BodyBorder")));
	if (bNewItem && CardPanel)
	{
		CardPanel->SetRenderScale(FVector2D(0.94f, 0.94f));
		CardPanel->SetRenderOpacity(0.0f);
		if (UMaterialInstanceDynamic* const Look = Body ? Body->GetDynamicMaterial() : nullptr)
		{
			Look->SetScalarParameterValue(TEXT("Sweep"), -0.2f);
		}
	}
	if (const UWorld* const World = GetWorld())
	{
		LootCardFill::Animate(CardPanel, Body, CardData, World->GetDeltaSeconds());
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

	// The collapsed panel takes no space, so the widget's size is the visible one's. Both sit centred
	// over the brackets, the name or the whole card, the way Apex puts its card over the item. A card
	// too tall for the room above goes under the item instead, and neither leaves the screen sideways.
	const float BracketHalf = BracketPixelRadius * BracketPadding;
	// A big item makes big brackets; the card still stays close to it.
	const float Gap = FMath::Min(BracketHalf, 140.0f) + (bFullCard ? CardGap : CompactGap);
	const float Scale = FMath::Max(0.01f, UWidgetLayoutLibrary::GetViewportScale(this));
	const FVector2D Size = GetDesiredSize() * Scale;
	const FVector2D Screen = UWidgetLayoutLibrary::GetViewportSize(this);
	const float Margin = 24.0f;
	FVector2D Position(ScreenPosition.X, ScreenPosition.Y - Gap);
	float AlignY = 1.0f;
	if (Position.Y - Size.Y < Margin && ScreenPosition.Y + Gap + Size.Y < Screen.Y - Margin)
	{
		Position.Y = ScreenPosition.Y + Gap;
		AlignY = 0.0f;
	}
	if (Screen.X > Size.X + 2.0f * Margin)
	{
		Position.X = FMath::Clamp(Position.X, Margin + Size.X * 0.5f, Screen.X - Margin - Size.X * 0.5f);
	}
	if (Screen.Y > Size.Y + 2.0f * Margin)
	{
		// Room neither above nor below: the whole card on screen matters more than clearing the item.
		Position.Y = AlignY > 0.5f
			? FMath::Clamp(Position.Y, Margin + Size.Y, Screen.Y - Margin)
			: FMath::Clamp(Position.Y, Margin, Screen.Y - Margin - Size.Y);
	}
	SetAlignmentInViewport(FVector2D(0.5f, AlignY));
	SetPositionInViewport(Position, true);

	if (!bShown)
	{
		bShown = true;
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void ULootCardWidget::HideCard()
{
	if (!bShown)
	{
		return;
	}
	bShown = false;
	ShownTarget.Reset();
	SetVisibility(ESlateVisibility::Collapsed);
}

void ULootCardWidget::ApplyData()
{
	const bool bWeapon = CardData.Category == ELootCardCategory::Weapon;

	if (CompactNameText)
	{
		CompactNameText->SetText(CardData.Title);
	}
	if (NameText)
	{
		NameText->SetText(CardData.Title);
	}
	LootCardFill::SetOptionalText(DescriptionText, CardData.Description);
	LootCardFill::SetOptionalText(CompareText, CardData.CompareLine);

	if (CategoryText)
	{
		LootCardFill::SetOptionalText(CategoryText, CardData.CategoryLine);
		CategoryText->SetColorAndOpacity(FSlateColor(CardData.bHasRarity
			? FMath::Lerp(CardData.RarityColor, LootCardFill::TextColor, 0.45f) : LootCardFill::DimColor));
	}
	if (UBorder* const Background = Cast<UBorder>(CardPanel))
	{
		if (!bCardBaseColorRead)
		{
			bCardBaseColorRead = true;
			CardBaseColor = Background->GetBrushColor();
		}
		FLinearColor Color = CardBaseColor;
		if (CardData.bHasRarity)
		{
			Color = FMath::Lerp(CardBaseColor, CardData.RarityColor, RarityTint);
			Color.A = CardBaseColor.A;
		}
		Background->SetBrushColor(Color);
	}
	if (WeaponStatsPanel)
	{
		WeaponStatsPanel->SetVisibility(bWeapon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (RarityBar)
	{
		RarityBar->SetColorAndOpacity(CardData.RarityColor);
		// Hidden rather than collapsed: the stripe also holds the card's width.
		RarityBar->SetVisibility(CardData.bHasRarity ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}

	// The key in its own chip when the Blueprint has one, else inline: "[Q] PICK UP".
	UTextBlock* const KeyText = Cast<UTextBlock>(GetWidgetFromName(TEXT("KeyText")));
	if (UWidget* const KeyChip = GetWidgetFromName(TEXT("KeyChip")))
	{
		KeyChip->SetVisibility(CardData.KeyLabel.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (KeyText)
	{
		KeyText->SetText(CardData.KeyLabel);
	}
	if (ActionText)
	{
		ActionText->SetText((KeyText || CardData.KeyLabel.IsEmpty())
			? CardData.ActionVerb
			: FText::Format(LOCTEXT("ActionWithKey", "[{0}] {1}"), CardData.KeyLabel, CardData.ActionVerb));
	}

	// The name in the rarity's own colour; white for things without one.
	if (NameText)
	{
		NameText->SetColorAndOpacity(FSlateColor(CardData.bHasRarity ? CardData.RarityColor : LootCardFill::TextColor));
	}

	// The rarity floods the body from the left (M_UI_LootCardBody); nothing floods a plain item.
	if (UBorder* const Body = Cast<UBorder>(GetWidgetFromName(TEXT("BodyBorder"))))
	{
		if (UMaterialInstanceDynamic* const Look = Body->GetDynamicMaterial())
		{
			Look->SetVectorParameterValue(TEXT("RarityColor"), CardData.RarityColor);
			float Flood = 0.0f;
			if (CardData.bHasRarity)
			{
				switch (CardData.Rarity)
				{
				case EUpgradeRarity::Rare:      Flood = 0.40f; break;
				case EUpgradeRarity::Epic:      Flood = 0.46f; break;
				case EUpgradeRarity::Legendary: Flood = 0.50f; break;
				default:                        Flood = 0.0f; break;
				}
			}
			Look->SetScalarParameterValue(TEXT("Flood"), Flood);
			Look->SetScalarParameterValue(TEXT("SweepStrength"),
				CardData.bHasRarity && CardData.Rarity == EUpgradeRarity::Legendary ? 0.32f : 0.16f);
		}
	}

	// Stat rows as real rows when the Blueprint has a box for them; the plain text is the fallback.
	UVerticalBox* const StatsBox = Cast<UVerticalBox>(GetWidgetFromName(TEXT("StatsBox")));
	if (StatsBox && WidgetTree)
	{
		const FSlateFontInfo RowFont = StatsText ? StatsText->GetFont() : FSlateFontInfo();
		LootCardFill::BuildStatRows(WidgetTree, StatsBox, CardData.Stats, RowFont);
		StatsBox->SetVisibility(CardData.Stats.Num() > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (UWidget* const ComparePanel = GetWidgetFromName(TEXT("CompareBorder")))
	{
		const bool bAnything = !CardData.CompareLine.IsEmpty() || CardData.Stats.Num() > 0;
		ComparePanel->SetVisibility(bAnything ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (StatsText)
	{
		TArray<FText> Lines;
		for (const FUpgradeStat& Row : CardData.Stats)
		{
			Lines.Add(Row.Label.IsEmpty() ? Row.Value
				: FText::Format(LOCTEXT("StatRow", "{0}  {1}"), Row.Label, Row.Value));
		}
		LootCardFill::SetOptionalText(StatsText, StatsBox ? FText::GetEmpty() : FText::Join(FText::FromString(TEXT("\n")), Lines));
	}
	if (ChipsText)
	{
		LootCardFill::SetOptionalText(ChipsText, FText::Join(FText::FromString(TEXT(" | ")), CardData.Chips));
	}

	// The weapon's own fields, for a layout built around them before the card knew other kinds.
	LootCardFill::SetOptionalText(FireModeText, bWeapon ? (CardData.bFullAuto ? LOCTEXT("ModeAuto", "AUTO") : LOCTEXT("ModeSemi", "SEMI")) : FText::GetEmpty());
	LootCardFill::SetOptionalText(DamageText, bWeapon ? FText::AsNumber(FMath::RoundToInt(CardData.Damage)) : FText::GetEmpty());
	LootCardFill::SetOptionalText(FireRateText, bWeapon ? FText::AsNumber(FMath::RoundToInt(CardData.RoundsPerMinute)) : FText::GetEmpty());
	LootCardFill::SetOptionalText(MagazineText, bWeapon ? FText::AsNumber(CardData.MagazineSize) : FText::GetEmpty());
	LootCardFill::SetOptionalText(RoundsText, bWeapon ? FText::AsNumber(CardData.Rounds) : FText::GetEmpty());

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
		if (UWidget* const IconBox = GetWidgetFromName(TEXT("IconBox")))
		{
			IconBox->SetVisibility(CardData.Icon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
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
