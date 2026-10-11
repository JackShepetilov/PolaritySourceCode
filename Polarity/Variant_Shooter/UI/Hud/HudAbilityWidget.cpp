// HudAbilityWidget.cpp

#include "HudAbilityWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "HudBarWidget.h"
#include "HudShapeWidget.h"
#include "PolarityPalette.h"
#include "Variant_Shooter/Abilities/AbilityComponent.h"
#include "Variant_Shooter/Abilities/AbilityDefinition.h"
#include "Variant_Shooter/ShooterCharacter.h"

void UHudAbilityWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (Caption && CaptionColorTag.IsValid())
	{
		Caption->SetColorAndOpacity(FSlateColor(UPolarityPalette::GetColor(CaptionColorTag, FLinearColor::White)));
	}
	if (NumberColorTag.IsValid())
	{
		const FSlateColor Number(UPolarityPalette::GetColor(NumberColorTag, FLinearColor::White));
		if (CooldownText)
		{
			CooldownText->SetColorAndOpacity(Number);
		}
		if (KeyText)
		{
			KeyText->SetColorAndOpacity(Number);
		}
	}
}

void UHudAbilityWidget::NativeBind(AShooterCharacter* Character)
{
	Abilities = Character->FindComponentByClass<UAbilityComponent>();
	if (Plate)
	{
		PlateRest = Plate->GetFillColor();
	}

	// Everything else is read in the tick; these make the first tick draw from scratch.
	ShownAbility.Reset();
	CooldownTotal = 0.0f;
	LastRemaining = 0.0f;
	LastSeconds = -1;
	bShown = false;

	// The CONTENT is hidden, never this widget: a collapsed UserWidget is not ticked, and the tick is
	// what would notice an ability arriving and bring it back.
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UWidget* const Content = GetRootWidget())
	{
		Content->SetVisibility(ESlateVisibility::Hidden);
	}
}

void UHudAbilityWidget::NativeUnbind()
{
	Abilities.Reset();
	ShownAbility.Reset();
}

void UHudAbilityWidget::ShowAbility(const UAbilityDefinition* Ability)
{
	ShownAbility = Ability;

	if (AbilityIcon)
	{
		UTexture2D* const Picture = Ability ? Ability->Icon.Get() : nullptr;
		if (Picture)
		{
			AbilityIcon->SetBrushFromTexture(Picture, false);
		}
		AbilityIcon->SetVisibility(Picture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}

	// The key as the player's bindings have it, so the plate cannot name the wrong one.
	if (KeyText)
	{
		FText Label;
		const AShooterCharacter* const Character = GetBoundCharacter();
		const APlayerController* const PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
		const ULocalPlayer* const LP = PC ? PC->GetLocalPlayer() : nullptr;
		if (const UEnhancedInputLocalPlayerSubsystem* const Input = LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr)
		{
			const TArray<FKey> Keys = Input->QueryKeysMappedToAction(Character->GetAbilityAction());
			if (Keys.Num() > 0)
			{
				Label = Keys[0].GetDisplayName(false);
			}
		}
		KeyText->SetText(Label);
	}
}

void UHudAbilityWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const UAbilityComponent* const Comp = Abilities.Get();
	const UAbilityDefinition* const Active = Comp ? Comp->GetActiveAbility() : nullptr;

	if ((Active != nullptr) != bShown)
	{
		bShown = Active != nullptr;
		if (UWidget* const Content = GetRootWidget())
		{
			Content->SetVisibility(bShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		}
	}
	if (!Active)
	{
		ShownAbility.Reset();
		return;
	}
	if (ShownAbility.Get() != Active)
	{
		ShowAbility(Active);
		LastSeconds = -1;
	}

	// The total is whatever the cooldown was when this machine first saw it running. The component
	// knows the real one only on the authority.
	const float Remaining = Comp->GetCooldownRemaining();
	if (Remaining <= 0.0f)
	{
		if (LastRemaining > 0.0f && Plate)
		{
			Plate->Flash(UPolarityPalette::GetColor(ReadyFlashTag, PlateRest), 0.4f);
		}
		CooldownTotal = 0.0f;
	}
	else if (Remaining > LastRemaining + 0.25f || CooldownTotal <= 0.0f)
	{
		CooldownTotal = FMath::Max(Remaining, Comp->GetCooldownTotal());
	}
	LastRemaining = Remaining;

	const bool bCooling = Remaining > 0.0f && CooldownTotal > 0.0f;
	if (CooldownBar)
	{
		// Instant: the number itself moves every frame, and the bar's own sweep would lag behind it.
		CooldownBar->SetFraction(bCooling ? 1.0f - FMath::Clamp(Remaining / CooldownTotal, 0.0f, 1.0f) : 1.0f, true);
	}

	if (CooldownFill)
	{
		const float Back = bCooling ? 1.0f - FMath::Clamp(Remaining / CooldownTotal, 0.0f, 1.0f) : 0.0f;
		CooldownFill->SetRenderTransformPivot(FVector2D(0.5f, 1.0f));
		CooldownFill->SetRenderScale(FVector2D(1.0f, Back));
		CooldownFill->SetVisibility(bCooling ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	if (KeyText)
	{
		KeyText->SetRenderOpacity(bCooling ? 0.5f : 1.0f);
	}

	const int32 Seconds = bCooling ? FMath::CeilToInt(Remaining) : 0;
	if (Seconds != LastSeconds)
	{
		LastSeconds = Seconds;
		if (CooldownText)
		{
			CooldownText->SetText(Seconds > 0 ? FText::AsNumber(Seconds) : FText::GetEmpty());
		}
	}

	if (AbilityIcon)
	{
		AbilityIcon->SetRenderOpacity((bCooling || Comp->IsCasting()) ? UnavailableIconOpacity : 1.0f);
	}
}
