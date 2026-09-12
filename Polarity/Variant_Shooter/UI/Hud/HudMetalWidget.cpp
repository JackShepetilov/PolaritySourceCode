// HudMetalWidget.cpp

#include "HudMetalWidget.h"

#include "Components/TextBlock.h"
#include "HudShapeWidget.h"
#include "PolarityPalette.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/ShooterPlayerState.h"

void UHudMetalWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (MetalText && NumberColorTag.IsValid())
	{
		MetalText->SetColorAndOpacity(FSlateColor(UPolarityPalette::GetColor(NumberColorTag, FLinearColor::White)));
	}
	if (Caption && CaptionColorTag.IsValid())
	{
		Caption->SetColorAndOpacity(FSlateColor(UPolarityPalette::GetColor(CaptionColorTag, FLinearColor::White)));
	}
}

void UHudMetalWidget::NativeBind(AShooterCharacter* Character)
{
	if (Plate)
	{
		PlateRest = Plate->GetFillColor();
	}
	TryBindState();
}

void UHudMetalWidget::NativeUnbind()
{
	if (AShooterPlayerState* Bound = State.Get())
	{
		Bound->OnMetalChanged.RemoveDynamic(this, &UHudMetalWidget::HandleMetalChanged);
	}
	State.Reset();
}

void UHudMetalWidget::TryBindState()
{
	AShooterCharacter* const Character = GetBoundCharacter();
	AShooterPlayerState* const Found = Character ? Character->GetPlayerState<AShooterPlayerState>() : nullptr;
	if (!Found || Found == State.Get())
	{
		return;
	}
	NativeUnbind();
	State = Found;
	Found->OnMetalChanged.AddUniqueDynamic(this, &UHudMetalWidget::HandleMetalChanged);
	Apply(Found->GetMetal(), Found->GetMaxMetal(), 0, true);
}

void UHudMetalWidget::HandleMetalChanged(int32 Metal, int32 MaxMetal, int32 Delta)
{
	Apply(Metal, MaxMetal, Delta, false);
}

void UHudMetalWidget::Apply(int32 Metal, int32 MaxMetal, int32 Delta, bool bInstant)
{
	Number.Set(Metal, bInstant);
	if (MetalMaxText)
	{
		MetalMaxText->SetText(FText::AsNumber(MaxMetal));
	}
	if (Plate && !bInstant)
	{
		if (Delta > 0)
		{
			Plate->Flash(UPolarityPalette::GetColor(GainFlashTag, PlateRest), 0.35f);
		}
		else if (Delta < 0)
		{
			Plate->Flash(UPolarityPalette::GetColor(SpendFlashTag, PlateRest), 0.3f);
		}
	}
	const bool bFull = MaxMetal > 0 && Metal >= MaxMetal;
	if (bFull != bAtCap)
	{
		bAtCap = bFull;
		if (Plate)
		{
			Plate->SetFillColor(bFull ? UPolarityPalette::GetColor(FullTintTag, PlateRest) : PlateRest);
		}
	}
}

void UHudMetalWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!State.IsValid() && GetBoundCharacter())
	{
		TryBindState();
	}
	Number.Update(InDeltaTime, MetalText);
}
