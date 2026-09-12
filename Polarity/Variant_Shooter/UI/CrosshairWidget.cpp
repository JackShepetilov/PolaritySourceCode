// CrosshairWidget.cpp

#include "CrosshairWidget.h"
#include "ShooterWeapon.h"
#include "Variant_Shooter/HitMarkerComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Components/Image.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

float UCrosshairWidget::ComputeBaseSizePixels() const
{
	// Size from viewport HEIGHT so the crosshair keeps the same proportion at any resolution.
	float ViewportY = 1080.0f;
	if (APlayerController* PC = GetOwningPlayer())
	{
		int32 SizeX = 0, SizeY = 0;
		PC->GetViewportSize(SizeX, SizeY);
		if (SizeY > 0)
		{
			ViewportY = static_cast<float>(SizeY);
		}
	}
	return ViewportY * BaseHeightFraction * (bArmed ? ActiveConfig.Scale : 1.0f);
}

float UCrosshairWidget::ComputeSpreadSizePixels(float SpreadDegrees) const
{
	// A cone of half-angle A, seen down its own axis, covers a circle whose half-width on screen is
	//     tan(A) / tan(HalfFOV) * (viewport width / 2)
	// because the projection maps tan(angle from the axis) linearly to distance from the centre of
	// the screen. Both the FOV and the viewport are read live, so the crosshair stays truthful when
	// the player changes resolution or the FOV moves (sprint kick, zoom).
	float ViewportX = 1920.0f;
	float ViewportY = 1080.0f;
	float FOVDegrees = 90.0f;

	if (const APlayerController* PC = GetOwningPlayer())
	{
		int32 SizeX = 0, SizeY = 0;
		PC->GetViewportSize(SizeX, SizeY);
		if (SizeX > 0 && SizeY > 0)
		{
			ViewportX = static_cast<float>(SizeX);
			ViewportY = static_cast<float>(SizeY);
		}

		if (const APlayerCameraManager* CamMgr = PC->PlayerCameraManager)
		{
			// The WORLD field of view, not the first-person one: the crosshair marks where the shot
			// lands in the world, and the weapon being drawn at its own FOV does not move that.
			FOVDegrees = CamMgr->GetFOVAngle();
		}
	}

	FOVDegrees = FMath::Clamp(FOVDegrees, 1.0f, 170.0f);

	// UE's FOV is horizontal, so the projection is measured against the viewport WIDTH.
	const float HalfFOVTangent = FMath::Tan(FMath::DegreesToRadians(FOVDegrees * 0.5f));
	const float SpreadTangent = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(SpreadDegrees, 0.0f, 80.0f)));

	// Full width, not radius: the BP resizes one square Image, and its side is the diameter of the
	// region.
	const float WidthPixels = (SpreadTangent / FMath::Max(HalfFOVTangent, KINDA_SMALL_NUMBER)) * ViewportX;

	return FMath::Clamp(
		WidthPixels * ActiveConfig.SpreadSizeMultiplier * ActiveConfig.Scale,
		ActiveConfig.MinSizePixels,
		FMath::Max(ActiveConfig.MaxSizePixels, ViewportY));
}

namespace
{
	/** The hit confirmation lives on the character, not here: UHitMarkerComponent keeps the timer,
	 *  the colour and the size, and the crosshair only shows what it reports. */
	const UHitMarkerComponent* FindHitMarker(const TWeakObjectPtr<AShooterCharacter>& Character)
	{
		const AShooterCharacter* Bound = Character.Get();
		return Bound ? Bound->GetHitMarkerComponent() : nullptr;
	}

	/** One of the two hit marker pictures: shown at the component's colour and fade, scaled by its
	 *  pulse (and the kill multiplier), or collapsed. */
	void DriveHitMarkerImage(UImage* Image, bool bShow, const UHitMarkerComponent* Marker)
	{
		if (!Image)
		{
			return;
		}
		if (!bShow || !Marker)
		{
			if (Image->GetVisibility() != ESlateVisibility::Collapsed)
			{
				Image->SetVisibility(ESlateVisibility::Collapsed);
			}
			return;
		}

		Image->SetVisibility(ESlateVisibility::HitTestInvisible);
		Image->SetColorAndOpacity(Marker->GetHitMarkerColor().CopyWithNewOpacity(Marker->GetHitMarkerAlpha()));

		// GetHitMarkerSize is the resting size times the kill multiplier times a pulse that grows
		// as the marker fades. Dividing the resting size back out leaves that as a plain scale, so
		// the picture keeps the size the designer gave it and only breathes.
		const float RestingSize = FMath::Max(Marker->Settings.HitMarkerSize, KINDA_SMALL_NUMBER);
		const float Scale = Marker->GetHitMarkerSize() / RestingSize;
		Image->SetRenderScale(FVector2D(Scale, Scale));
	}
}

int32 UCrosshairWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 BaseLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	if (!bArmed || !ActiveConfig.bDrawProceduralTicks || bHiddenByAiming)
	{
		return BaseLayer;
	}

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
	if (!WhiteBrush)
	{
		return BaseLayer;
	}

	const FVector2D LocalSize(AllottedGeometry.GetLocalSize());
	if (LocalSize.X <= 0.0f || LocalSize.Y <= 0.0f)
	{
		return BaseLayer;
	}

	const FVector2D Center = LocalSize * 0.5;

	// CurrentSizePixels is in real SCREEN pixels, because that is the only unit an angle can be
	// projected into honestly. Slate draws in layout units, which the UI's DPI scaling has already
	// divided down, and the geometry we are painting into carries exactly that scale. This used to
	// be derived from the widget's own width over the viewport's, which quietly assumed the widget
	// spans the whole viewport: in a 500x500 HUD slot that shrank the gap to a quarter, and that is
	// how the crosshair "broke" when it moved under the registry.
	const float LayoutScale = AllottedGeometry.GetAccumulatedLayoutTransform().GetScale();
	const float PixelsToLocal = LayoutScale > KINDA_SMALL_NUMBER ? 1.0f / LayoutScale : 1.0f;

	// The gap: distance from the centre to the INNER end of each bar. This is the whole contract:
	// the inner ends sit on the edge of the spread cone, so the square they bound is the region a
	// shot can land in.
	const float Gap = FMath::Max(CurrentSizePixels * 0.5f * PixelsToLocal, 0.0f);

	// Bar length and weight are NOT converted: they are art, and art should follow the UI's scaling
	// like every other widget. Only the gap is an angle, and only the gap must ignore DPI.
	const float Length = FMath::Max(ActiveConfig.TickLength, 1.0f);
	const float Thickness = FMath::Max(ActiveConfig.TickThickness, 1.0f);
	const float Outline = FMath::Max(ActiveConfig.OutlineThickness, 0.0f);

	// Whatever opacity our parents are drawn with. Custom-drawn elements do not inherit it on
	// their own. (Our own RenderOpacity is already blended into the style by the time Slate calls
	// us.)
	const float ParentAlpha = InWidgetStyle.GetColorAndOpacityTint().A;

	FLinearColor TickColor = ActiveConfig.Color;
	TickColor.A *= ParentAlpha;

	FLinearColor BorderColor = ActiveConfig.OutlineColor;
	BorderColor.A *= ParentAlpha;

	const int32 OutlineLayer = BaseLayer;
	const int32 TickLayer = BaseLayer + 1;

	auto DrawRect = [&](const FVector2D& TopLeft, const FVector2D& Size, const FLinearColor& Color, int32 Layer)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			Layer,
			AllottedGeometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(TopLeft))),
			WhiteBrush,
			ESlateDrawEffect::None,
			Color);
	};

	// One bar, plus the dark border under it. The border is the same rectangle grown on every side,
	// drawn a layer below, which is cheaper and steadier than four separate edge strips.
	auto DrawBar = [&](const FVector2D& TopLeft, const FVector2D& Size)
	{
		if (Outline > 0.0f && BorderColor.A > 0.0f)
		{
			DrawRect(TopLeft - FVector2D(Outline, Outline), Size + FVector2D(Outline * 2.0f, Outline * 2.0f), BorderColor, OutlineLayer);
		}
		DrawRect(TopLeft, Size, TickColor, TickLayer);
	};

	const FVector2D VerticalSize(Thickness, Length);
	const FVector2D HorizontalSize(Length, Thickness);
	const float HalfThickness = Thickness * 0.5f;

	if (ActiveConfig.bTickTop)
	{
		DrawBar(FVector2D(Center.X - HalfThickness, Center.Y - Gap - Length), VerticalSize);
	}
	if (ActiveConfig.bTickBottom)
	{
		DrawBar(FVector2D(Center.X - HalfThickness, Center.Y + Gap), VerticalSize);
	}
	if (ActiveConfig.bTickLeft)
	{
		DrawBar(FVector2D(Center.X - Gap - Length, Center.Y - HalfThickness), HorizontalSize);
	}
	if (ActiveConfig.bTickRight)
	{
		DrawBar(FVector2D(Center.X + Gap, Center.Y - HalfThickness), HorizontalSize);
	}

	// The dot stays put: it marks where the barrel points, and that is the one thing opening the
	// spread does not move.
	if (ActiveConfig.CenterDotSize > 0.0f)
	{
		const float DotSize = ActiveConfig.CenterDotSize;
		DrawBar(Center - FVector2D(DotSize * 0.5f, DotSize * 0.5f), FVector2D(DotSize, DotSize));
	}

	return TickLayer;
}

bool UCrosshairWidget::UpdateAimingVisibility()
{
	bool bShouldHide = false;

	if (bArmed && ActiveConfig.bHideWhileAiming)
	{
		if (const AShooterCharacter* ShooterOwner = Cast<AShooterCharacter>(GetOwningPlayerPawn()))
		{
			bShouldHide = ShooterOwner->GetADSAlpha() >= ActiveConfig.HideAtADSAlpha;
		}
	}

	if (bShouldHide != bHiddenByAiming)
	{
		bHiddenByAiming = bShouldHide;

		// Only the bars go: NativePaint skips them while this is set. Neither Visibility nor
		// RenderOpacity is touched. A collapsed widget is not ticked, so it could never bring itself
		// back, and a transparent one takes the hit marker down with it, which is the one thing
		// the player still needs to see while aiming.
		Invalidate(EInvalidateWidgetReason::Paint);

		BP_OnCrosshairVisibilityChanged(!bHiddenByAiming);
	}

	return bHiddenByAiming;
}

void UCrosshairWidget::BindCharacter(AShooterCharacter* Character)
{
	UnbindCharacter();
	if (!Character)
	{
		return;
	}
	BoundCharacter = Character;
	Character->OnActiveWeaponChanged.AddUniqueDynamic(this, &UCrosshairWidget::HandleActiveWeaponChanged);
	SetActiveWeapon(Character->GetCurrentWeapon());
}

void UCrosshairWidget::UnbindCharacter()
{
	if (AShooterCharacter* Character = BoundCharacter.Get())
	{
		Character->OnActiveWeaponChanged.RemoveDynamic(this, &UCrosshairWidget::HandleActiveWeaponChanged);
	}
	BoundCharacter.Reset();
	SetActiveWeapon(nullptr);
}

void UCrosshairWidget::HandleActiveWeaponChanged(AShooterWeapon* NewWeapon)
{
	SetActiveWeapon(NewWeapon);
}

void UCrosshairWidget::NativeDestruct()
{
	UnbindCharacter();
	Super::NativeDestruct();
}

void UCrosshairWidget::SetActiveWeapon(AShooterWeapon* Weapon)
{
	ActiveWeapon = Weapon;
	bArmed = (Weapon != nullptr);

	if (Weapon)
	{
		ActiveConfig = Weapon->GetCrosshairConfig();
	}

	// Snap back to rest on every weapon change: a crosshair that grew on the old gun has nothing to
	// say about the new one.
	CurrentBloom = 0.0f;
	CurrentSpreadDegrees = Weapon ? Weapon->GetCrosshairSpreadDegrees() : 0.0f;
	CurrentSizePixels = (bArmed && ActiveConfig.bSizeFromSpread)
		? ComputeSpreadSizePixels(CurrentSpreadDegrees)
		: ComputeBaseSizePixels();

	// A new weapon starts visible; the next tick hides it again if the player is still aiming.
	if (bHiddenByAiming)
	{
		bHiddenByAiming = false;
		BP_OnCrosshairVisibilityChanged(true);
	}

	Invalidate(EInvalidateWidgetReason::Paint);

	BP_OnCrosshairChanged(bArmed, ActiveConfig, CurrentSizePixels);
}

void UCrosshairWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// ---- Hit marker: the two pictures the Blueprint places, driven from the component ----
	// Found by name rather than BindWidgetOptional so this stays a .cpp-only change; the lookup is
	// a map read. Shown while aiming down sights and while unarmed too: the sight replaces the
	// crosshair, not the confirmation, and a punch lands without a gun in hand.
	{
		const UHitMarkerComponent* Marker = FindHitMarker(BoundCharacter);
		FHitMarkerEvent Event;
		const bool bUp = Marker && Marker->GetActiveHitMarker(Event);
		const bool bIonized = bUp && Event.HitType == EHitMarkerType::Ionized;
		DriveHitMarkerImage(Cast<UImage>(GetWidgetFromName(TEXT("HitMarkerImage"))), bUp && !bIonized, Marker);
		DriveHitMarkerImage(Cast<UImage>(GetWidgetFromName(TEXT("IonizedHitMarkerImage"))), bIonized, Marker);
	}

	// Unarmed: nothing else animates.
	if (!bArmed)
	{
		return;
	}

	// ---- Aiming down sights: the sight IS the aim, so the crosshair leaves ----
	if (UpdateAimingVisibility())
	{
		// Keep following the spread while hidden so coming out of ADS does not pop a stale size.
		if (const AShooterWeapon* AimedWeapon = ActiveWeapon.Get())
		{
			CurrentSpreadDegrees = AimedWeapon->GetCrosshairSpreadDegrees();
			if (ActiveConfig.bSizeFromSpread)
			{
				CurrentSizePixels = ComputeSpreadSizePixels(CurrentSpreadDegrees);
			}
		}
		return;
	}

	// ---- Spread mode: the crosshair IS the cone the bullets leave in ----
	if (ActiveConfig.bSizeFromSpread)
	{
		if (const AShooterWeapon* SpreadWeapon = ActiveWeapon.Get())
		{
			CurrentSpreadDegrees = SpreadWeapon->GetCrosshairSpreadDegrees();

			const float TargetSize = ComputeSpreadSizePixels(CurrentSpreadDegrees);
			const float PreviousSize = CurrentSizePixels;
			CurrentSizePixels = FMath::FInterpTo(CurrentSizePixels, TargetSize, InDeltaTime, ActiveConfig.SpreadFollowSpeed);

			// The bars are drawn in NativePaint from this number, and Slate caches paint: without
			// asking for a repaint the gap would only move when something else happened to dirty
			// the widget.
			if (ActiveConfig.bDrawProceduralTicks && !FMath::IsNearlyEqual(PreviousSize, CurrentSizePixels, 0.05f))
			{
				Invalidate(EInvalidateWidgetReason::Paint);
			}

			// Report the spread as a 0..1 amount too, so anything the BP already drove off Bloom01
			// (opacity, colour) keeps working and now means something real: how far open the weapon
			// is, relative to its own ceiling.
			const float MaxSpread = FMath::Max(SpreadWeapon->GetSpreadConfig().MaxSpreadDegrees, KINDA_SMALL_NUMBER);
			CurrentBloom = FMath::Clamp(CurrentSpreadDegrees / MaxSpread, 0.0f, 1.0f);

			BP_OnCrosshairResized(CurrentSizePixels, CurrentBloom);
			return;
		}
	}

	// ---- Build the bloom target (0..1) from observable state ----
	float Target = 0.0f;

	if (const AShooterWeapon* Weapon = ActiveWeapon.Get())
	{
		if (Weapon->IsFiring())
		{
			Target += ActiveConfig.FireBloom;   // grow while shooting
		}
	}

	if (const APawn* Pawn = GetOwningPlayerPawn())
	{
		const FVector Vel = Pawn->GetVelocity();
		const float Speed2D = FVector(Vel.X, Vel.Y, 0.0f).Size();

		if (const ACharacter* Char = Cast<ACharacter>(Pawn))
		{
			if (const UCharacterMovementComponent* Move = Char->GetCharacterMovement())
			{
				const float MaxSpeed = FMath::Max(1.0f, Move->GetMaxSpeed());
				Target += ActiveConfig.MoveBloom * FMath::Clamp(Speed2D / MaxSpeed, 0.0f, 1.0f);

				if (Move->IsFalling())
				{
					Target += ActiveConfig.AirBloom;
				}
			}
		}
	}

	Target = FMath::Clamp(Target, 0.0f, 1.0f);

	// ---- Chase the target: snappy grow, gentler settle ----
	const float InterpSpeed = (Target > CurrentBloom) ? ActiveConfig.BloomAttackSpeed : ActiveConfig.BloomRecoverySpeed;
	CurrentBloom = FMath::FInterpTo(CurrentBloom, Target, InDeltaTime, InterpSpeed);

	const float NewSize = ComputeBaseSizePixels() * (1.0f + CurrentBloom * ActiveConfig.BloomScaleAdd);

	// Only ping the BP when the size actually moved (skips idle frames once settled).
	if (!FMath::IsNearlyEqual(NewSize, CurrentSizePixels, 0.05f))
	{
		CurrentSizePixels = NewSize;
		BP_OnCrosshairResized(CurrentSizePixels, CurrentBloom);
	}
}
