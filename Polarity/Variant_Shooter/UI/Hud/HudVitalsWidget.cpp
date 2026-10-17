// HudVitalsWidget.cpp

#include "HudVitalsWidget.h"

#include "Components/TextBlock.h"
#include "HudBarWidget.h"
#include "HudShapeWidget.h"
#include "PolarityPalette.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Health/FieldCareComponent.h"
#include "Variant_Shooter/Grenades/GrenadeComponent.h"
#include "Variant_Shooter/Grenades/GrenadeDefinition.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#include "Variant_Shooter/Progression/RunLevelComponent.h"
#include "Variant_Shooter/Progression/WeaponMasteryComponent.h"
#include "Variant_Shooter/Progression/WeaponMasteryDefinition.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "ApexMovementComponent.h"

namespace
{
	// Two optional widgets of WBP_Vitals, found by name so the header stays as it is: a UHudBarWidget
	// named ArmorBar and a text block named MedkitText. Renaming either in the blueprint switches
	// it off without a word.
	const FName ArmorBarName(TEXT("ArmorBar"));
	const FName MedkitTextName(TEXT("MedkitText"));

	// A debug line of grenades under the vitals, found by name like the two above: which type G
	// throws and how many, the others H cycles to, and the throw while it runs.
	const FName GrenadeTextName(TEXT("GrenadeText"));

	FString DescribeGrenades(const AShooterCharacter* Character)
	{
		const UGrenadeComponent* const Grenades = Character ? Character->FindComponentByClass<UGrenadeComponent>() : nullptr;
		const UInventoryComponent* const Bag = Character ? Character->FindComponentByClass<UInventoryComponent>() : nullptr;
		if (!Grenades || !Bag)
		{
			return FString();
		}

		const UGrenadeDefinition* const Selected = Grenades->GetSelectedType();
		auto Name = [](const UGrenadeDefinition* Type)
		{
			return Type->DisplayName.IsEmpty() ? Type->GetName() : Type->DisplayName.ToString();
		};

		if (Grenades->IsThrowing() && Selected)
		{
			return FString::Printf(TEXT("THROWING %s"), *Name(Selected));
		}
		if (!Selected)
		{
			return TEXT("NO GRENADES");
		}

		FString Line = FString::Printf(TEXT("[G] %s x%d/%d"), *Name(Selected), Bag->GetGrenadeCount(Selected), Bag->GetGadgetsPerKind());
		TArray<UGrenadeDefinition*> Types;
		Bag->GetGrenadeTypes(Types);
		for (const UGrenadeDefinition* const Type : Types)
		{
			if (Type != Selected)
			{
				Line += FString::Printf(TEXT("   [H] %s x%d/%d"), *Name(Type), Bag->GetGrenadeCount(Type), Bag->GetGadgetsPerKind());
			}
		}
		return Line;
	}
}

void UHudVitalsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (Caption && CaptionColorTag.IsValid())
	{
		Caption->SetColorAndOpacity(FSlateColor(UPolarityPalette::GetColor(CaptionColorTag, FLinearColor::White)));
	}
}

void UHudVitalsWidget::NativeBind(AShooterCharacter* Character)
{
	Character->OnHealthChanged.AddUniqueDynamic(this, &UHudVitalsWidget::HandleHealthChanged);
	if (Plate)
	{
		PlateRest = Plate->GetFillColor();
	}
	Apply(Character->GetCurrentHP(), Character->GetMaxHP(), true);

	if (UHudBarWidget* const ArmorBar = Cast<UHudBarWidget>(GetWidgetFromName(ArmorBarName)))
	{
		ArmorBar->SetFraction(Character->GetMaxArmor() > 0.0f ? Character->GetCurrentArmor() / Character->GetMaxArmor() : 0.0f, true);
	}
}

void UHudVitalsWidget::NativeUnbind()
{
	if (AShooterCharacter* Character = GetBoundCharacter())
	{
		Character->OnHealthChanged.RemoveDynamic(this, &UHudVitalsWidget::HandleHealthChanged);
	}
}

void UHudVitalsWidget::HandleHealthChanged(float CurrentHP, float MaxHP, float LifePercent, float ArmorPercent)
{
	Apply(CurrentHP, MaxHP, false);

	if (UHudBarWidget* const ArmorBar = Cast<UHudBarWidget>(GetWidgetFromName(ArmorBarName)))
	{
		ArmorBar->SetFraction(FMath::Clamp(ArmorPercent, 0.0f, 1.0f), false);
	}
}

void UHudVitalsWidget::Apply(float CurrentHP, float MaxHP, bool bInstant)
{
	const float Fraction = MaxHP > 0.0f ? FMath::Clamp(CurrentHP / MaxHP, 0.0f, 1.0f) : 0.0f;
	if (HealthBar)
	{
		HealthBar->SetFraction(Fraction, bInstant);
	}
	Number.Set(FMath::CeilToInt(CurrentHP), bInstant);

	if (!bInstant && Plate)
	{
		if (Fraction < LastFraction - 0.001f)
		{
			Plate->Flash(UPolarityPalette::GetColor(HitFlashTag, PlateRest), 0.25f);
		}
		else if (Fraction > LastFraction + 0.001f)
		{
			Plate->Flash(UPolarityPalette::GetColor(HealFlashTag, PlateRest), 0.4f);
		}
	}
	LastFraction = Fraction;

	const bool bLow = LowHealthFraction > 0.0f && Fraction > 0.0f && Fraction < LowHealthFraction;
	if (bLow != bPulsing)
	{
		bPulsing = bLow;
		PulseTime = 0.0f;
		if (!bLow && Plate)
		{
			Plate->SetFillColor(PlateRest);
		}
	}
}

void UHudVitalsWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Number.Update(InDeltaTime, HealthText);

	// Medkits in the bag. Asked every frame and written only when the number changes.
	if (UTextBlock* const MedkitText = Cast<UTextBlock>(GetWidgetFromName(MedkitTextName)))
	{
		const AShooterCharacter* const Character = GetBoundCharacter();
		const UFieldCareComponent* const Care = Character ? Character->FindComponentByClass<UFieldCareComponent>() : nullptr;
		const FString Wanted = FString::FromInt(Care ? Care->GetMedkitCount() : 0);
		if (MedkitText->GetText().ToString() != Wanted)
		{
			MedkitText->SetText(FText::FromString(Wanted));
		}
	}

	if (UTextBlock* const GrenadeText = Cast<UTextBlock>(GetWidgetFromName(GrenadeTextName)))
	{
		// The run level rides on the same debug line until it has a place of its own on the HUD.
		FString Wanted = DescribeGrenades(GetBoundCharacter());
		if (const URunLevelComponent* const RunLevel = URunLevelComponent::FindFor(GetBoundCharacter()))
		{
			const int32 Next = RunLevel->GetXPForNextLevel();
			Wanted = FString::Printf(TEXT("LVL %d  XP %d/%s     %s"), RunLevel->GetLevel(), RunLevel->GetXP(),
				Next < 0 ? TEXT("MAX") : *FString::FromInt(Next), *Wanted);
		}

		// The mastery of the gun in hand, the arms' fatigue and the tactical sprint, on the same line.
		const AShooterCharacter* const Holder = GetBoundCharacter();
		const AShooterWeapon* const HeldGun = Holder ? Holder->GetCurrentWeapon() : nullptr;
		const UWeaponMasteryComponent* const Mastery = UWeaponMasteryComponent::FindFor(Holder);
		if (HeldGun && HeldGun->MasteryDefinition && Mastery)
		{
			const int32 GunLevel = Mastery->GetLevel(HeldGun->MasteryDefinition);
			const int32 GunNext = HeldGun->MasteryDefinition->GetXPToReach(GunLevel + 1);
			Wanted += FString::Printf(TEXT("     GUN %d  %d/%s"), GunLevel, Mastery->GetXP(HeldGun->MasteryDefinition),
				GunNext < 0 ? TEXT("MAX") : *FString::FromInt(GunNext));
		}
		if (Holder && Holder->GetADSFatigue() > 0.0f)
		{
			Wanted += FString::Printf(TEXT("     TIRED %d%%"), FMath::RoundToInt(Holder->GetADSFatigue() * 100.0f));
		}
		if (const UApexMovementComponent* const Apex = Holder ? Holder->GetApexMovement() : nullptr)
		{
			if (Apex->IsTacticalSprinting())
			{
				Wanted += TEXT("     TAC SPRINT");
			}
			else if (Apex->GetTacticalSprintCooldownRemaining() > 0.0f)
			{
				Wanted += FString::Printf(TEXT("     TAC %.0fs"), Apex->GetTacticalSprintCooldownRemaining());
			}
		}
		if (GrenadeText->GetText().ToString() != Wanted)
		{
			GrenadeText->SetText(FText::FromString(Wanted));
		}
	}

	if (bPulsing && Plate)
	{
		PulseTime += InDeltaTime;
		const float Wave = 0.5f + 0.5f * FMath::Sin(PulseTime * LowHealthPulseHz * 2.0f * PI);
		Plate->SetFillColor(FLinearColor::LerpUsingHSV(PlateRest, UPolarityPalette::GetColor(LowHealthPulseTag, PlateRest), Wave));
	}
}
