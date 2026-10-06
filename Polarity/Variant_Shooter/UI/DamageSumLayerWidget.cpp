// DamageSumLayerWidget.cpp

#include "Variant_Shooter/UI/DamageSumLayerWidget.h"
#include "Variant_Shooter/UI/DamageNumbersSubsystem.h"
#include "Variant_Shooter/UI/EnemyHudSettings.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace DamageSum
{
	/** Same shape of ease the mockup uses for the pop: fast out, settles. */
	float EaseOutCubic(float T)
	{
		const float Inv = 1.0f - FMath::Clamp(T, 0.0f, 1.0f);
		return 1.0f - Inv * Inv * Inv;
	}
}

void UDamageSumLayerWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetVisibility(ESlateVisibility::HitTestInvisible);

	const UEnemyHudSettings* const S = UEnemyHudSettings::Get();
	FontObject = S->NumberFont.IsNull() ? nullptr : S->NumberFont.LoadSynchronous();
	if (!S->NumberFont.IsNull() && !FontObject)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ENEMY_HUD] NumberFont %s did not load, using the default bold font"),
			*S->NumberFont.ToString());
	}

	KillIconTexture = S->KillIcon.IsNull() ? nullptr : S->KillIcon.LoadSynchronous();
	if (KillIconTexture)
	{
		KillIconBrush.SetResourceObject(KillIconTexture);
		KillIconBrush.ImageSize = FVector2D(KillIconTexture->GetSizeX(), KillIconTexture->GetSizeY());
	}

	FleshColor = UEnemyHudSettings::PaletteColor(TEXT("Palette.HUD.Damage.Flesh"), FLinearColor(FColor(0xFF, 0x2B, 0x3A)));
	HeadColor = UEnemyHudSettings::PaletteColor(TEXT("Palette.HUD.Damage.Head"), FLinearColor(FColor(0xFF, 0xB6, 0x1F)));
	WhiteArmorOutlineColor = UEnemyHudSettings::PaletteColor(TEXT("Palette.HUD.Damage.WhiteArmorOutline"), FLinearColor(FColor(0x2B, 0x2B, 0x30)));
	RemoteColor = UEnemyHudSettings::PaletteColor(TEXT("Palette.HUD.Damage.Remote"), FLinearColor(FColor(0xB8, 0xB0, 0xA0)));
	OutlineColor = UEnemyHudSettings::PaletteColor(TEXT("Palette.HUD.Damage.Outline"), FLinearColor(FColor(0x1A, 0x0F, 0x08)));
}

void UDamageSumLayerWidget::AddReport(const FDamageNumberReport& Report)
{
	if (!Report.Target.IsValid() || Report.Damage <= 0.0f)
	{
		return;
	}

	const UEnemyHudSettings* const S = UEnemyHudSettings::Get();

	// Apex's stacking rule: a hit within the window of this enemy's last hit adds to its number; a
	// slower one starts a new number, and the old one runs out its own life. A kill closes a number.
	const float Window = S->StackWindowSeconds;
	int32 Index = Entries.IndexOfByPredicate([&Report, Window](const FEntry& Entry)
	{
		return Entry.Target == Report.Target && !Entry.bKilled && Entry.Age <= Window;
	});
	if (Index == INDEX_NONE)
	{
		Index = Entries.AddDefaulted();
		Entries[Index].Target = Report.Target;
		Entries[Index].Serial = NextSerial++;
	}
	FEntry& Entry = Entries[Index];

	Entry.Total += Report.Damage;
	Entry.Age = 0.0f;
	Entry.PopAge = 0.0f;

	if (Report.bRemote)
	{
		// The turret's hits add to the sum, but only colour it while the player has not hit this
		// enemy directly: then the number is a report, not a reward.
		if (!Entry.bDirect)
		{
			Entry.Kind = EKind::Remote;
		}
		Entry.PopFrom = FMath::Lerp(1.0f, S->PopScale, 0.5f);
	}
	else
	{
		Entry.bDirect = true;
		if (Report.ArmorDamage > 0.0f)
		{
			Entry.Kind = EKind::Armor;
			Entry.ArmorTier = Report.ArmorTier;
			Entry.PopFrom = Report.bArmorBroken ? S->ArmorBreakPopScale : S->PopScale;
		}
		else
		{
			Entry.Kind = EKind::Flesh;
			Entry.PopFrom = S->PopScale;
		}
		if (Report.bHeadshot)
		{
			Entry.PopFrom = S->HeadshotPopScale;
		}
	}
	// AUTHOR 2026-10-04: a kill does not recolour the number. It keeps the killing hit's look and only
	// pops harder and stays longer.
	if (Report.bKilled)
	{
		Entry.bKilled = true;
		Entry.PopFrom = FMath::Max(Entry.PopFrom, S->KillPopScale);
	}

	// A turret's hit says nothing about the player's aim, so it leaves the headshot look as it was.
	if (!Report.bRemote)
	{
		Entry.bHead = Report.bHeadshot;
	}
}

FLinearColor UDamageSumLayerWidget::ColorFor(const FEntry& Entry) const
{
	switch (Entry.Kind)
	{
	case EKind::Armor:
		return UEnemyHudSettings::ArmorTierColor(Entry.ArmorTier);
	case EKind::Remote:
		return RemoteColor;
	default:
		return FleshColor;
	}
}

float UDamageSumLayerWidget::GetLife(const FEntry& Entry) const
{
	const UEnemyHudSettings* const S = UEnemyHudSettings::Get();
	return Entry.bKilled ? S->KillHoldSeconds : S->StackDurationSeconds;
}

void UDamageSumLayerWidget::UpdateAnchor(FEntry& Entry, APlayerController* PC, float ViewportScale, float LayerHeight) const
{
	const UEnemyHudSettings* const S = UEnemyHudSettings::Get();
	AActor* const Target = Entry.Target.Get();
	const AShooterNPC* const NPC = Cast<AShooterNPC>(Target);
	const bool bAlive = Target && !(NPC && NPC->IsDead());

	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);

	// Apex's anchor: the body centre point-blank, the top of the head from AnchorBlendDistance on.
	// Remembered while there is a body, so a kill's number stays where the enemy was.
	if (bAlive)
	{
		FVector Center = Target->GetActorLocation();
		float HalfHeight = 90.0f;
		if (const ACharacter* const AsCharacter = Cast<ACharacter>(Target))
		{
			if (const UCapsuleComponent* const Capsule = AsCharacter->GetCapsuleComponent())
			{
				Center = Capsule->GetComponentLocation();
				HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
			}
		}
		const FVector Top = Center + FVector(0.0f, 0.0f, HalfHeight);
		const float Alpha = FMath::Clamp(FVector::Dist(CameraLocation, Center) / FMath::Max(S->AnchorBlendDistance, 1.0f), 0.0f, 1.0f);
		Entry.LastWorldAnchor = FMath::Lerp(Center, Top, Alpha);
		Entry.bHasWorldAnchor = true;
	}

	FVector2D Pixels;
	if (!Entry.bHasWorldAnchor || !PC->ProjectWorldLocationToScreen(Entry.LastWorldAnchor, Pixels, false))
	{
		Entry.bOnScreen = false;
		return;
	}

	Entry.Anchor = FVector2f(Pixels / ViewportScale) - FVector2f(S->ScreenOffsetLeft, S->ScreenOffsetUp) * LayerHeight;
	Entry.bOnScreen = true;
}

void UDamageSumLayerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (Entries.Num() == 0)
	{
		return;
	}

	APlayerController* const PC = GetOwningPlayer();
	const float ViewportScale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), 0.01f);
	const float LayerHeight = MyGeometry.GetLocalSize().Y;

	for (FEntry& Entry : Entries)
	{
		Entry.Age += InDeltaTime;
		Entry.PopAge += InDeltaTime;

		if (PC)
		{
			UpdateAnchor(Entry, PC, ViewportScale, LayerHeight);
		}
		else
		{
			Entry.bOnScreen = false;
		}
	}

	Entries.RemoveAll([this](const FEntry& Entry) { return Entry.Age >= GetLife(Entry); });

	// The column over each enemy: the newest number at the anchor, every older one a place higher,
	// sliding there rather than jumping.
	const float PushSpeed = UEnemyHudSettings::Get()->PushSpeed;
	for (FEntry& Entry : Entries)
	{
		int32 Newer = 0;
		for (const FEntry& Other : Entries)
		{
			if (Other.Target == Entry.Target && Other.Serial > Entry.Serial)
			{
				++Newer;
			}
		}
		Entry.Slot = FMath::FInterpTo(Entry.Slot, static_cast<float>(Newer), InDeltaTime, PushSpeed);
	}

	// Slate caches paint: every frame with a number on screen is a frame that moved.
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 UDamageSumLayerWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	if (Entries.Num() == 0 || !FSlateApplication::IsInitialized())
	{
		return Layer;
	}

	const UEnemyHudSettings* const S = UEnemyHudSettings::Get();
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();

	for (const FEntry& Entry : Entries)
	{
		if (!Entry.bOnScreen)
		{
			continue;
		}

		// Fades over the end of its life.
		const float Life = GetLife(Entry);
		const float Fade = FMath::Clamp(S->FadeSeconds, 0.01f, Life);
		const float FadeT = FMath::Clamp((Entry.Age - (Life - Fade)) / Fade, 0.0f, 1.0f);
		const float Alpha = 1.0f - FadeT;
		if (Alpha <= 0.001f)
		{
			continue;
		}

		// Size: growth with the sum, then the pop of the last hit. The same on screen at any distance.
		const float Growth = FMath::Min(1.0f + Entry.Total * S->GrowthPerDamage, S->MaxGrowth);
		const float PopT = Entry.PopAge / FMath::Max(S->PopSeconds, 0.01f);
		const float Pop = PopT < 1.0f ? FMath::Lerp(Entry.PopFrom, 1.0f, DamageSum::EaseOutCubic(PopT)) : 1.0f;
		const float KindScale = (Entry.Kind == EKind::Remote) ? S->RemoteScale : 1.0f;
		const float Scale = Growth * Pop * KindScale;

		// The font is built at its base size and the whole number scaled by the layout transform,
		// which Slate rasterises at the final size, so the number stays sharp at any scale.
		FFontOutlineSettings Outline;
		Outline.OutlineSize = S->OutlineSize;
		Outline.OutlineColor = OutlineColor;
		Outline.OutlineColor.A *= Alpha;

		// White-hot on the hit, cooling to the number's colour over the pop.
		const float Heat = PopT < 1.0f ? S->HitFlashWhite * (1.0f - DamageSum::EaseOutCubic(PopT)) : 0.0f;
		const FLinearColor Base = ColorFor(Entry);
		const FLinearColor Hot = FMath::Lerp(Base, FLinearColor::White, Heat).CopyWithNewOpacity(Alpha);

		// Apex: white digits, the kind's colour as the outline. Otherwise coloured digits on dark. A
		// headshot turns the digits yellow-orange in either style and leaves the outline alone.
		const FLinearColor Digits = Entry.bHead ? HeadColor : (S->bColoredOutline ? FLinearColor::White : Base);
		const FLinearColor Color = FMath::Lerp(Digits, FLinearColor::White, Heat).CopyWithNewOpacity(Alpha);
		if (S->bColoredOutline)
		{
			// White digits on a white outline would vanish: the white armour tier gets a dark grey one.
			const bool bWhiteArmor = Entry.Kind == EKind::Armor && Entry.ArmorTier == 0;
			Outline.OutlineColor = bWhiteArmor ? WhiteArmorOutlineColor.CopyWithNewOpacity(Alpha) : Hot;
		}
		const FSlateFontInfo Font = FontObject
			? FSlateFontInfo(FontObject, S->FontSize, NAME_None, Outline)
			: [&]() { FSlateFontInfo Bold = FCoreStyle::GetDefaultFontStyle("Bold", S->FontSize); Bold.OutlineSettings = Outline; return Bold; }();

		const FString Text = FString::FromInt(FMath::RoundToInt(Entry.Total));
		const FVector2f TextSize = Measure->Measure(Text, Font);

		// Slow drift up since the last hit, plus the place in the column over this enemy.
		const float LayerHeight = AllottedGeometry.GetLocalSize().Y;
		const FVector2f Rise(0.0f, -(S->RiseSpeed * Entry.Age + Entry.Slot * S->StackSpacing * LayerHeight));
		const float IconSize = (Entry.bKilled && KillIconTexture) ? TextSize.Y * 0.8f * Scale : 0.0f;
		const float IconGap = IconSize > 0.0f ? 2.0f * Scale : 0.0f;
		const float Width = TextSize.X * Scale + IconSize + IconGap;
		FVector2f Origin = Entry.Anchor + Rise - FVector2f(Width * 0.5f, TextSize.Y * Scale * 0.5f);

		// Kill icon, if one is set, to the left of the number; the number moves right to make room.
		if (IconSize > 0.0f)
		{
			const FVector2f IconOrigin(Origin.X, Entry.Anchor.Y + Rise.Y - IconSize * 0.5f);
			FSlateDrawElement::MakeBox(OutDrawElements, ++Layer,
				AllottedGeometry.ToPaintGeometry(FVector2f(IconSize, IconSize), FSlateLayoutTransform(IconOrigin)),
				&KillIconBrush, ESlateDrawEffect::None, ColorFor(Entry).CopyWithNewOpacity(Alpha) * Tint);
			Origin.X += IconSize + IconGap;
		}

		// Glow: the same text with a wide outline in the number's own colour, faint, under everything.
		if (S->GlowSize > 0 && S->GlowAlpha > 0.0f)
		{
			FSlateFontInfo GlowFont = Font;
			GlowFont.OutlineSettings.OutlineSize = S->OutlineSize + S->GlowSize;
			GlowFont.OutlineSettings.OutlineColor = Base.CopyWithNewOpacity(S->GlowAlpha * Alpha);
			FSlateDrawElement::MakeText(OutDrawElements, ++Layer,
				AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Scale, Origin)),
				// Not fully transparent: Slate culls text whose tint alpha is zero, outline and all.
				Text, GlowFont, ESlateDrawEffect::None, Base.CopyWithNewOpacity(0.01f));
		}

		// A drop shadow under the outline, the way the HUD text in TF2 sits on the scene.
		FLinearColor Shadow = OutlineColor;
		Shadow.A = 0.5f * Alpha;
		FSlateFontInfo ShadowFont = Font;
		ShadowFont.OutlineSettings.OutlineColor = Shadow;
		FSlateDrawElement::MakeText(OutDrawElements, ++Layer,
			AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Scale, Origin + FVector2f(0.0f, 2.0f * Scale))),
			Text, ShadowFont, ESlateDrawEffect::None, Shadow * Tint);

		FSlateDrawElement::MakeText(OutDrawElements, ++Layer,
			AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Scale, Origin)),
			Text, Font, ESlateDrawEffect::None, Color * Tint);
	}

	return Layer;
}
