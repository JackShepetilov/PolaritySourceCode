// HudVitalsWidget.cpp

#include "HudVitalsWidget.h"

#include "Components/TextBlock.h"
#include "HudBarWidget.h"
#include "HudShapeWidget.h"
#include "PolarityPalette.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/Health/FieldCareComponent.h"

namespace
{
	// Two optional widgets of WBP_Vitals, found by name so the header stays as it is: a UHudBarWidget
	// named ArmorBar and a text block named MedkitText. Renaming either in the blueprint switches
	// it off without a word.
	const FName ArmorBarName(TEXT("ArmorBar"));
	const FName MedkitTextName(TEXT("MedkitText"));
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

	if (bPulsing && Plate)
	{
		PulseTime += InDeltaTime;
		const float Wave = 0.5f + 0.5f * FMath::Sin(PulseTime * LowHealthPulseHz * 2.0f * PI);
		Plate->SetFillColor(FLinearColor::LerpUsingHSV(PlateRest, UPolarityPalette::GetColor(LowHealthPulseTag, PlateRest), Wave));
	}
}
