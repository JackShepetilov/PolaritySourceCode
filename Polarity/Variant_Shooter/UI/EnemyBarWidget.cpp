// EnemyBarWidget.cpp

#include "Variant_Shooter/UI/EnemyBarWidget.h"
#include "Variant_Shooter/UI/EnemyHudSettings.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/AI/TrackedTankNPC.h"
#include "Variant_Shooter/Shield/ShieldFieldComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/SizeBox.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace EnemyBar
{
	/** Armour row flash after a break. */
	constexpr float FlashSeconds = 0.24f;

	/** Shard gravity and launch, in bar widths per second, so a far (small) bar throws small. */
	constexpr float ShardGravity = 5.0f;
	constexpr float ShardSpeedX = 1.1f;
	constexpr float ShardSpeedUpMin = 0.6f;
	constexpr float ShardSpeedUpMax = 1.6f;

	FLinearColor Brighten(const FLinearColor& C, float Amount)
	{
		return FLinearColor(
			FMath::Lerp(C.R, 1.0f, Amount),
			FMath::Lerp(C.G, 1.0f, Amount),
			FMath::Lerp(C.B, 1.0f, Amount),
			C.A);
	}

	/** Shear along x by y and scale along y, about the box's centre. Row-vector convention:
	 *  x' = x + Shear * y, y' = ScaleY * y. */
	FSlateRenderTransform ShearScale(float Shear, float ScaleY)
	{
		return FSlateRenderTransform(FMatrix2x2f(1.0f, 0.0f, Shear, ScaleY));
	}
}

bool UEnemyBarWidget::Initialize()
{
	const bool bSuperInitialized = Super::Initialize();

	// No designer tree: the widget is drawn in NativePaint. A size box gives it a real desired size,
	// so the viewport slot centres it on the anchor and nothing culls a zero-sized widget.
	if (bSuperInitialized && WidgetTree && !WidgetTree->RootWidget)
	{
		USizeBox* const Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BarSize"));
		const FVector2f Size = GetBarSize();
		Box->SetWidthOverride(Size.X);
		Box->SetHeightOverride(Size.Y);
		WidgetTree->RootWidget = Box;
	}

	if (const UEnemyHudSettings* const Settings = UEnemyHudSettings::Get())
	{
		ShowAfterHitSeconds = Settings->ShowAfterHitSeconds;
		PlateBrush = FSlateRoundedBoxBrush(FLinearColor::White, Settings->PlateCornerRadius);
	}

	return bSuperInitialized;
}

void UEnemyBarWidget::ResetWidget()
{
	Super::ResetWidget();

	TrackedNPC.Reset();
	Shards.Reset();
	CellPopAge.Reset();
	PartRows.Reset();
	GhostHoldLeft = 0.0f;
	JoltAge = 1000.0f;
	ArmorFlashAge = 1000.0f;
}

FVector2f UEnemyBarWidget::GetBarSize() const
{
	const UEnemyHudSettings* const S = UEnemyHudSettings::Get();
	return FVector2f(
		S->BarWidth + S->Padding * 2.0f + (PartRows.IsEmpty() ? 0.0f : S->PartIconWidth + S->PartIconGap),
		PartRows.IsEmpty() ? S->ArmorHeight + S->RowGap + S->HealthHeight + S->Padding * 2.0f
			: PartRows.Num() * (FMath::Max(S->PartRowHeight, S->ArmorHeight) + S->RowGap)
				+ FMath::Max(S->HealthHeight, S->PartRowHeight) + S->Padding * 2.0f);
}

float UEnemyBarWidget::GetFillX() const
{
	const UEnemyHudSettings* S = UEnemyHudSettings::Get();
	return S->Padding + (PartRows.IsEmpty() ? 0.0f : S->PartIconWidth + S->PartIconGap);
}

int32 UEnemyBarWidget::GetCellCount() const
{
	const float Per = FMath::Max(UEnemyHudSettings::Get()->ArmorPerCell, 1.0f);
	return MaxArmor > KINDA_SMALL_NUMBER ? FMath::Max(1, FMath::CeilToInt(MaxArmor / Per - 0.001f)) : 0;
}

void UEnemyBarWidget::ResolveColors()
{
	PlateColor = UEnemyHudSettings::PaletteColor(TEXT("Palette.HUD.Enemy.Plate"), FLinearColor(FColor(0x17, 0x0F, 0x0B, 0xA8)));
	TrackColor = UEnemyHudSettings::PaletteColor(TEXT("Palette.HUD.Enemy.Track"), FLinearColor(1.0f, 1.0f, 1.0f, 0.08f));
	HealthColor = UEnemyHudSettings::PaletteColor(TEXT("Palette.HUD.Enemy.Health"), FLinearColor(FColor(0xC8, 0x47, 0x3F)));
	GhostColor = UEnemyHudSettings::PaletteColor(TEXT("Palette.HUD.Enemy.Ghost"), FLinearColor(FColor(0xFF, 0xE9, 0xC2)));
	TierColor = UEnemyHudSettings::ArmorTierColor(Tier);
	PartColor = UEnemyHudSettings::PaletteColor(TEXT("Palette.HUD.Enemy.Part"), FLinearColor(FColor(0xF0, 0x8A, 0x3C)));
}

void UEnemyBarWidget::SnapToTarget()
{
	const AShooterNPC* const NPC = BoundNPC.Get();
	const UShieldFieldComponent* const Field = BoundShieldField.Get();
	PartRows.Reset();
	if (const UHitZoneComponent* Zones = NPC ? NPC->FindComponentByClass<UHitZoneComponent>() : nullptr)
	{
		for (const FHitZoneBarRow& Config : Zones->BarRows)
		{
			if (Config.Zones.IsEmpty()) { continue; }
			FPartRow& Row = PartRows.AddDefaulted_GetRef();
			Row.Icon = Config.Icon;
			for (FName Name : Config.Zones)
			{
				FPartSegment& Segment = Row.Segments.AddDefaulted_GetRef();
				Segment.Zone = Name;
				Segment.Shown = Segment.Ghost = Zones->GetZonePoolFraction(Name);
				Segment.bBroken = Zones->IsZoneBroken(Name);
			}
		}
	}
	if (USizeBox* Box = WidgetTree ? Cast<USizeBox>(WidgetTree->RootWidget) : nullptr)
	{
		const FVector2f Size = GetBarSize();
		Box->SetWidthOverride(Size.X);
		Box->SetHeightOverride(Size.Y);
	}

	MaxArmor = Field ? Field->GetMaxShield() : 0.0f;
	MaxHP = FMath::Max(ResolveMaxHP(), 1.0f);
	ShownArmor = GhostArmor = Field ? FMath::Clamp(Field->GetCurrentShield(), 0.0f, MaxArmor) : 0.0f;
	ShownHP = GhostHP = NPC ? FMath::Clamp(NPC->CurrentHP, 0.0f, MaxHP) : 0.0f;

	Tier = UEnemyHudSettings::ArmorTierFromMax(MaxArmor);
	CellPopAge.Init(1000.0f, GetCellCount());
	Shards.Reset();
	GhostHoldLeft = 0.0f;
	JoltAge = 1000.0f;
	ArmorFlashAge = 1000.0f;
	ResolveColors();
}

void UEnemyBarWidget::SpawnShards(FVector2f Origin, float Width, FLinearColor Color)
{
	const UEnemyHudSettings* const S = UEnemyHudSettings::Get();
	const float W = Width;

	for (int32 Index = 0; Index < S->BreakShardCount; ++Index)
	{
		FShard& Shard = Shards.AddDefaulted_GetRef();
		const float Length = W / 8.0f * FMath::FRandRange(0.6f, 1.2f);
		Shard.Size = FVector2f(Length, Length * 0.45f);
		Shard.Position = Origin + FVector2f(FMath::FRand() * W, 0.0f);
		Shard.Color = Color;
		Shard.Velocity = FVector2f(
			FMath::FRandRange(-1.0f, 1.0f) * W * EnemyBar::ShardSpeedX,
			-FMath::FRandRange(EnemyBar::ShardSpeedUpMin, EnemyBar::ShardSpeedUpMax) * W);
		Shard.Angle = FMath::FRandRange(-0.5f, 0.5f);
		Shard.Spin = FMath::FRandRange(-9.0f, 9.0f);
	}
}

void UEnemyBarWidget::TickWidgetState(float DeltaTime)
{
	AShooterNPC* const NPC = BoundNPC.Get();
	if (!NPC)
	{
		return;
	}

	const UEnemyHudSettings* const S = UEnemyHudSettings::Get();

	if (TrackedNPC.Get() != NPC)
	{
		TrackedNPC = NPC;
		SnapToTarget();
	}

	Clock += DeltaTime;

	const UShieldFieldComponent* const Field = BoundShieldField.Get();
	const float NewMaxArmor = Field ? Field->GetMaxShield() : 0.0f;
	if (!FMath::IsNearlyEqual(NewMaxArmor, MaxArmor, 0.01f))
	{
		MaxArmor = NewMaxArmor;
		Tier = UEnemyHudSettings::ArmorTierFromMax(MaxArmor);
		TierColor = UEnemyHudSettings::ArmorTierColor(Tier);
		CellPopAge.Init(1000.0f, GetCellCount());
	}
	MaxHP = FMath::Max(ResolveMaxHP(), 1.0f);

	const float NewArmor = Field ? FMath::Clamp(Field->GetCurrentShield(), 0.0f, MaxArmor) : 0.0f;
	const float NewHP = FMath::Clamp(NPC->CurrentHP, 0.0f, MaxHP);

	// A hit is a number going DOWN. Read here rather than off a damage event, so the host and a
	// client see the same thing: both watch replicated values fall.
	bool bHit = false;
	bool bBroke = false;
	bool bPartAnimating = false;
	const UHitZoneComponent* const Zones = NPC->FindComponentByClass<UHitZoneComponent>();
	for (int32 RowIndex = 0; Zones && RowIndex < PartRows.Num(); ++RowIndex)
	{
		FPartRow& Row = PartRows[RowIndex];
		const float Width = (S->BarWidth - (Row.Segments.Num() - 1) * S->CellGap) / Row.Segments.Num();
		for (int32 Index = 0; Index < Row.Segments.Num(); ++Index)
		{
			FPartSegment& Part = Row.Segments[Index];
			const float Value = Zones->GetZonePoolFraction(Part.Zone);
			const bool bBroken = Zones->IsZoneBroken(Part.Zone);
			if (Value < Part.Shown - 0.001f)
			{
				bHit = true;
				Part.Hold = S->GhostHoldSeconds;
				Part.Drain = (Part.Ghost - Value) / FMath::Max(S->GhostDrainSeconds, 0.01f);
			}
			if (bBroken && !Part.bBroken)
			{
				Part.FlashAge = 0.0f;
				SpawnShards(FVector2f(GetFillX() + Index * (Width + S->CellGap),
					S->Padding + RowIndex * (FMath::Max(S->PartRowHeight, S->ArmorHeight) + S->RowGap)
						+ FMath::Max(S->PartRowHeight, S->ArmorHeight) * 0.5f), Width, PartColor);
			}
			Part.Shown = Value;
			Part.Ghost = FMath::Max(Part.Ghost, Value);
			Part.bBroken = bBroken;
			Part.Hold -= DeltaTime;
			if (Part.Hold <= 0.0f) { Part.Ghost = FMath::FInterpConstantTo(Part.Ghost, Value, DeltaTime, Part.Drain); }
			Part.FlashAge += DeltaTime;
			bPartAnimating |= Part.Ghost > Value + 0.001f || Part.FlashAge < EnemyBar::FlashSeconds;
		}
	}
	if (NewArmor < ShownArmor - 0.01f)
	{
		bHit = true;
		const float Per = FMath::Max(S->ArmorPerCell, 1.0f);
		for (int32 Cell = 0; Cell < CellPopAge.Num(); ++Cell)
		{
			const float CellFloor = Cell * Per + 0.01f;
			if (ShownArmor > CellFloor && NewArmor <= CellFloor)
			{
				CellPopAge[Cell] = 0.0f;
			}
		}
		bBroke = (NewArmor <= 0.01f);
	}
	if (NewHP < ShownHP - 0.01f)
	{
		bHit = true;
	}

	// Rising numbers (regen, a heal, a recycled body) carry the ghost up with them: nothing to show.
	ShownArmor = NewArmor;
	ShownHP = NewHP;
	GhostArmor = FMath::Max(GhostArmor, ShownArmor);
	GhostHP = FMath::Max(GhostHP, ShownHP);

	if (bHit)
	{
		GhostHoldLeft = S->GhostHoldSeconds;
		JoltAge = 0.0f;
		JoltStrength = 1.0f;
	}
	if (bBroke)
	{
		SpawnShards(FVector2f(S->Padding, S->Padding + S->ArmorHeight * 0.5f), S->BarWidth, TierColor);
		ArmorFlashAge = 0.0f;
		JoltStrength = 2.0f;
	}

	// The ghost holds, then drains at a speed fixed when it starts, so a big chunk and a small one
	// take the same time to go.
	if (GhostHoldLeft > 0.0f)
	{
		GhostHoldLeft -= DeltaTime;
		if (GhostHoldLeft <= 0.0f)
		{
			const float Drain = FMath::Max(S->GhostDrainSeconds, 0.01f);
			ArmorDrainSpeed = FMath::Max((GhostArmor - ShownArmor) / Drain, 1.0f);
			HPDrainSpeed = FMath::Max((GhostHP - ShownHP) / Drain, 1.0f);
		}
	}
	else
	{
		GhostArmor = FMath::FInterpConstantTo(GhostArmor, ShownArmor, DeltaTime, ArmorDrainSpeed);
		GhostHP = FMath::FInterpConstantTo(GhostHP, ShownHP, DeltaTime, HPDrainSpeed);
	}

	JoltAge += DeltaTime;
	ArmorFlashAge += DeltaTime;
	for (float& Age : CellPopAge)
	{
		Age += DeltaTime;
	}

	const float Gravity = S->BarWidth * EnemyBar::ShardGravity;
	for (FShard& Shard : Shards)
	{
		Shard.Age += DeltaTime;
		Shard.Velocity.Y += Gravity * DeltaTime;
		Shard.Position += Shard.Velocity * DeltaTime;
		Shard.Angle += Shard.Spin * DeltaTime;
	}
	const float ShardLife = S->BreakShardSeconds;
	Shards.RemoveAll([ShardLife](const FShard& Shard) { return Shard.Age >= ShardLife; });

	// Slate caches paint. Anything moving, and the low-health pulse, needs a repaint every frame;
	// a quiet bar costs nothing.
	const bool bLow = ShownHP > 0.0f && ShownHP / MaxHP < S->LowHealthFraction;
	const ATrackedTankNPC* const Tank = Cast<ATrackedTankNPC>(NPC);
	const bool bAnimating = bHit || bLow || bPartAnimating || (Tank && Tank->IsBlind()) || Shards.Num() > 0
		|| JoltAge < S->JoltSeconds || ArmorFlashAge < EnemyBar::FlashSeconds
		|| GhostArmor > ShownArmor + 0.01f || GhostHP > ShownHP + 0.01f
		|| CellPopAge.ContainsByPredicate([S](float Age) { return Age < S->CellPopSeconds; });
	if (bAnimating && bWasVisibleLastFrame)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

int32 UEnemyBarWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	if (!BoundNPC.IsValid())
	{
		return Layer;
	}

	const UEnemyHudSettings* const S = UEnemyHudSettings::Get();
	const FSlateBrush* const White = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	const FVector2f BarSize = GetBarSize();

	// Shake: two sines at different rates, dying out over the jolt.
	FVector2f Jolt = FVector2f::ZeroVector;
	if (JoltAge < S->JoltSeconds)
	{
		const float T = JoltAge / S->JoltSeconds;
		const float Amp = S->JoltPixels * JoltStrength * (1.0f - T);
		Jolt = FVector2f(FMath::Sin(T * PI * 3.0f) * Amp, -FMath::Sin(T * PI * 2.0f) * Amp * 0.35f);
	}

	auto DrawBox = [&](const FVector2f& Position, const FVector2f& Size, const FLinearColor& Color,
		float Shear = 0.0f, float ScaleY = 1.0f, const FSlateBrush* Brush = nullptr)
	{
		if (Size.X <= 0.01f || Size.Y <= 0.01f || Color.A <= 0.001f)
		{
			return;
		}
		const FGeometry Child = AllottedGeometry.MakeChild(Size, FSlateLayoutTransform(Position + Jolt),
			EnemyBar::ShearScale(Shear, ScaleY), FVector2f(0.5f, 0.5f));
		FSlateDrawElement::MakeBox(OutDrawElements, ++Layer, Child.ToPaintGeometry(), Brush ? Brush : White,
			ESlateDrawEffect::None, Color * Tint);
	};

	// Plate.
	DrawBox(FVector2f::ZeroVector, BarSize, PlateColor, 0.0f, 1.0f, &PlateBrush);

	// Armour cells. The row is left out on an enemy without armour; health keeps its place at the
	// bottom so the bar does not jump when the pool is configured away.
	const int32 Cells = GetCellCount();
	const float Per = FMath::Max(S->ArmorPerCell, 1.0f);
	if (Cells > 0 && PartRows.IsEmpty())
	{
		const float CellWidth = (S->BarWidth - (Cells - 1) * S->CellGap) / Cells;
		const float RowY = S->Padding;
		for (int32 Cell = 0; Cell < Cells; ++Cell)
		{
			const float X = S->Padding + Cell * (CellWidth + S->CellGap);
			const float PopT = CellPopAge.IsValidIndex(Cell) ? FMath::Clamp(CellPopAge[Cell] / S->CellPopSeconds, 0.0f, 1.0f) : 1.0f;
			const float Pop = 1.0f - PopT;
			const float ScaleY = 1.0f + 1.2f * Pop;

			// An emptied cell flashes in the tier colour and fades back to the empty track.
			const FLinearColor Track = FMath::Lerp(TrackColor, EnemyBar::Brighten(TierColor, 0.5f), Pop);
			DrawBox(FVector2f(X, RowY), FVector2f(CellWidth, S->ArmorHeight), Track, S->CellShear, ScaleY);

			const float GhostFrac = FMath::Clamp((GhostArmor - Cell * Per) / Per, 0.0f, 1.0f);
			const float FillFrac = FMath::Clamp((ShownArmor - Cell * Per) / Per, 0.0f, 1.0f);
			DrawBox(FVector2f(X, RowY), FVector2f(CellWidth * GhostFrac, S->ArmorHeight), GhostColor, S->CellShear, ScaleY);
			DrawBox(FVector2f(X, RowY), FVector2f(CellWidth * FillFrac, S->ArmorHeight), TierColor, S->CellShear, ScaleY);
		}

		if (ArmorFlashAge < EnemyBar::FlashSeconds)
		{
			const float Alpha = 0.8f * (1.0f - ArmorFlashAge / EnemyBar::FlashSeconds);
			DrawBox(FVector2f(S->Padding, RowY), FVector2f(S->BarWidth, S->ArmorHeight), FLinearColor(1.0f, 1.0f, 1.0f, Alpha), S->CellShear);
		}
	}

	// Health.
	// Icons follow the approved 21 x 13 SVG silhouettes, scaled to the bar.
	auto DrawIcon = [&](float Y, EHitZoneBarIcon Icon, bool bRocket, FLinearColor Color)
	{
		auto Rect = [&](float X, float DY, float W, float H)
		{
			const float Scale = S->PartIconWidth / 21.0f;
			DrawBox(FVector2f(S->Padding + X * Scale, Y + S->ArmorHeight * 0.5f + (DY - 6.5f) * Scale),
				FVector2f(W * Scale, H * Scale), Color);
		};
		if (bRocket)
		{
			Rect(3, 5, 11, 3); Rect(6, 4, 7, 5); Rect(10, 3, 2, 7);
			Rect(14, 6, 6, 1); Rect(1, 4, 2, 2); Rect(1, 7, 2, 2);
		}
		else if (Icon == EHitZoneBarIcon::Turret)
		{
			Rect(4, 4, 11, 6); Rect(14, 5.8f, 7, 2); Rect(6, 1.5f, 6, 2.5f); Rect(2, 10, 15, 2.5f);
		}
		else if (Icon == EHitZoneBarIcon::Tracks)
		{
			Rect(3, 3, 15, 2); Rect(3, 9, 15, 2); Rect(1, 5, 2, 4); Rect(18, 5, 2, 4);
			for (float X : {4.0f, 9.0f, 14.0f}) { Rect(X, 6, 3, 3); }
		}
	};
	const ATrackedTankNPC* const Tank = Cast<ATrackedTankNPC>(BoundNPC.Get());
	for (int32 RowIndex = 0; RowIndex < PartRows.Num(); ++RowIndex)
	{
		const FPartRow& Row = PartRows[RowIndex];
		const float RowHeight = FMath::Max(S->PartRowHeight, S->ArmorHeight);
		const float Y = S->Padding + RowIndex * (RowHeight + S->RowGap) + (RowHeight - S->ArmorHeight) * 0.5f;
		const float Width = (S->BarWidth - (Row.Segments.Num() - 1) * S->CellGap) / Row.Segments.Num();
		bool bRowBroken = false;
		float Flash = 0.0f;
		for (int32 Index = 0; Index < Row.Segments.Num(); ++Index)
		{
			const FPartSegment& Part = Row.Segments[Index];
			const float X = GetFillX() + Index * (Width + S->CellGap);
			bRowBroken |= Part.bBroken;
			Flash = FMath::Max(Flash, 1.0f - FMath::Clamp(Part.FlashAge / EnemyBar::FlashSeconds, 0.0f, 1.0f));
			DrawBox(FVector2f(X, Y), FVector2f(Width, S->ArmorHeight), TrackColor, S->CellShear);
			DrawBox(FVector2f(X, Y), FVector2f(Width * Part.Ghost, S->ArmorHeight), GhostColor, S->CellShear);
			if (Part.bBroken)
			{
				FLinearColor Hatch = HealthColor; Hatch.A = 0.6f;
				for (float Stripe = 0; Stripe < Width - 2.0f; Stripe += 6.0f)
				{
					DrawBox(FVector2f(X + Stripe + 1.0f, Y), FVector2f(2, S->ArmorHeight), Hatch, -0.3f);
				}
			}
			else
			{
				FLinearColor Fill = PartColor;
				if (Tank && Tank->IsBlind() && Part.Zone == Tank->GetOpticsZoneName()
					&& FMath::Fmod(Clock, 0.35f) >= 0.175f) { Fill.A *= 0.35f; }
				DrawBox(FVector2f(X, Y), FVector2f(Width * Part.Shown, S->ArmorHeight), Fill, S->CellShear);
			}
		}
		DrawIcon(Y, Row.Icon, false, bRowBroken ? HealthColor : GhostColor);
		DrawBox(FVector2f(GetFillX(), Y), FVector2f(S->BarWidth, S->ArmorHeight), FLinearColor(1, 1, 1, 0.8f * Flash), S->CellShear);
	}
	const float FillX = GetFillX();
	const float HealthRowHeight = PartRows.IsEmpty() ? S->HealthHeight : FMath::Max(S->PartRowHeight, S->HealthHeight);
	const float HealthY = BarSize.Y - S->Padding - HealthRowHeight + (HealthRowHeight - S->HealthHeight) * 0.5f;
	if (!PartRows.IsEmpty()) { DrawIcon(HealthY + (S->HealthHeight - S->ArmorHeight) * 0.5f, EHitZoneBarIcon::None, true, GhostColor); }
	const float HealthFrac = FMath::Clamp(ShownHP / MaxHP, 0.0f, 1.0f);
	const float GhostFrac = FMath::Clamp(GhostHP / MaxHP, 0.0f, 1.0f);
	FLinearColor Health = HealthColor;
	if (ShownHP > 0.0f && HealthFrac < S->LowHealthFraction)
	{
		const float Pulse = 0.5f + 0.5f * FMath::Sin(Clock * 2.0f * PI * S->LowHealthPulseHz);
		Health = EnemyBar::Brighten(HealthColor, 0.35f * Pulse);

		// A ring in the health colour around the row, breathing with the fill.
		const float Ring = 1.5f;
		FLinearColor RingColor = HealthColor;
		RingColor.A = 0.45f + 0.45f * Pulse;
		DrawBox(FVector2f(FillX - Ring, HealthY - Ring), FVector2f(S->BarWidth + Ring * 2.0f, S->HealthHeight + Ring * 2.0f), RingColor);
	}
	DrawBox(FVector2f(FillX, HealthY), FVector2f(S->BarWidth, S->HealthHeight), TrackColor);
	DrawBox(FVector2f(FillX, HealthY), FVector2f(S->BarWidth * GhostFrac, S->HealthHeight), GhostColor);
	DrawBox(FVector2f(FillX, HealthY), FVector2f(S->BarWidth * HealthFrac, S->HealthHeight), Health);

	// Shards from a break fly out of the armour row, under gravity, spinning and fading.
	for (const FShard& Shard : Shards)
	{
		const float T = FMath::Clamp(Shard.Age / S->BreakShardSeconds, 0.0f, 1.0f);
		FLinearColor Color = Shard.Color;
		Color.A *= 1.0f - T;
		const FGeometry Child = AllottedGeometry.MakeChild(Shard.Size,
			FSlateLayoutTransform(Shard.Position - Shard.Size * 0.5f + Jolt),
			FSlateRenderTransform(TQuat2<float>(Shard.Angle)), FVector2f(0.5f, 0.5f));
		FSlateDrawElement::MakeBox(OutDrawElements, ++Layer, Child.ToPaintGeometry(), White, ESlateDrawEffect::None, Color * Tint);
	}

	return Layer;
}
