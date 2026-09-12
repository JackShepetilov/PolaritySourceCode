// HudVitalsWidget.cpp

#include "HudVitalsWidget.h"

#include "Components/TextBlock.h"
#include "HudBarWidget.h"
#include "HudShapeWidget.h"
#include "Variant_Shooter/ShooterCharacter.h"

void UHudVitalsWidget::NativeBind(AShooterCharacter* Character)
{
	Character->OnHealthChanged.AddUniqueDynamic(this, &UHudVitalsWidget::HandleHealthChanged);
	if (Plate)
	{
		PlateRest = Plate->FillColor;
	}
	Apply(Character->GetCurrentHP(), Character->GetMaxHP(), true);
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
			Plate->Flash(HitFlash, 0.25f);
		}
		else if (Fraction > LastFraction + 0.001f)
		{
			Plate->Flash(HealFlash, 0.4f);
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

	if (bPulsing && Plate)
	{
		PulseTime += InDeltaTime;
		const float Wave = 0.5f + 0.5f * FMath::Sin(PulseTime * LowHealthPulseHz * 2.0f * PI);
		Plate->SetFillColor(FLinearColor::LerpUsingHSV(PlateRest, LowHealthPulse, Wave));
	}
}
