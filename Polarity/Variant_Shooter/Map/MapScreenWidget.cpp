// MapScreenWidget.cpp

#include "Variant_Shooter/Map/MapScreenWidget.h"

#include "Variant_Shooter/Map/RunDirectorSubsystem.h"
#include "Variant_Shooter/AI/SquadSpawn/SquadSpawnSubsystem.h"
#include "AI/PolarityTeams.h"
#include "Coop/CoopPlayers.h"

#include "Engine/Texture2D.h"
#include "Landscape.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GenericTeamAgentInterface.h"
#include "Fonts/SlateFontInfo.h"
#include "GameFramework/Pawn.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"

UMapScreenWidget::UMapScreenWidget(const FObjectInitializer& Init)
	: Super(Init)
{
	// НЕТ фона по умолчанию, и это осознанно.
	//
	// Здесь стояла картинка стенда, прибитая через ConstructorHelpers. На стенде она совпадала с
	// миром, а на любой другой карте показывала чужую местность под правильными маркерами - то есть
	// врала уверенно. Тёмный квадрат честнее: маркеры несут смысл сами.
	//
	// Фон назначается в блюпринте-наследнике под конкретный уровень, когда для него есть снимок.
}

void UMapScreenWidget::Open()
{
	if (bIsOpen)
	{
		return;
	}

	bIsOpen = true;
	SetVisibility(ESlateVisibility::HitTestInvisible);

	// GameAndUI, and the cursor stays hidden. Unlike the inventory this screen has nothing to click
	// on: it is a thing you glance at with a finger on the key, the way a battle royale map is
	// read. Taking the mouse away would turn a glance into a menu.
	if (APlayerController* const PC = GetOwningPlayer())
	{
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
		Mode.SetHideCursorDuringCapture(true);
		PC->SetInputMode(Mode);
	}
}

void UMapScreenWidget::Close()
{
	if (!bIsOpen)
	{
		return;
	}

	bIsOpen = false;
	SetVisibility(ESlateVisibility::Collapsed);

	if (APlayerController* const PC = GetOwningPlayer())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
}

void UMapScreenWidget::Toggle()
{
	bIsOpen ? Close() : Open();
}

void UMapScreenWidget::MapRect(const FVector2D& WidgetSize, FVector2D& OutOrigin, FVector2D& OutSize)
{
	// Square, centred, as big as fits. A map that stretches with the window is a map whose
	// distances lie, and distance is most of what anybody reads off it.
	const float Side = FMath::Min(WidgetSize.X, WidgetSize.Y);
	OutSize = FVector2D(Side, Side);
	OutOrigin = (WidgetSize - OutSize) * 0.5f;
}

FVector2D UMapScreenWidget::WorldToMap(const FVector& World, const FVector2D& MapSize) const
{
	const float Extent = FMath::Max(WorldExtent, 1.0f);
	const float Half = MapSize.X * 0.5f;

	// World +X goes right, world +Y goes DOWN the page. That puts world -Y at the top, which is
	// where the bench calls north, and matches map_preview.py so the two pictures agree.
	const float X = (static_cast<float>(World.X) - WorldCentre.X) / Extent;
	const float Y = (static_cast<float>(World.Y) - WorldCentre.Y) / Extent;

	return FVector2D(Half + X * Half, Half + Y * Half);
}

FLinearColor UMapScreenWidget::ColourForTeam(uint8 Team) const
{
	switch (Team)
	{
	case PolarityTeams::Players:  return PlayerColour;
	case PolarityTeams::FactionA: return FactionAColour;
	case PolarityTeams::FactionB: return FactionBColour;
	default:                      return NeutralColour;
	}
}

int32 UMapScreenWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId,
		InWidgetStyle, bParentEnabled);

	UWorld* const World = GetWorld();
	if (!World)
	{
		return Layer;
	}

	// Границы берутся у ЛАНДШАФТА, а не из констант.
	//
	// Раньше здесь стояли числа стенда: центр (0,0) и половина 31500. На карте, которая стоит в
	// другом месте и другого размера, маркеры сваливались в угол, а фон показывал чужую местность.
	// Константа, описывающая один конкретный уровень, не может жить в коде виджета.
	//
	// Считается прямо здесь, а не в Open и не в поле класса: поле потребовало бы правки заголовка,
	// то есть полной пересборки, а карта это отладочный экран, который открывают на секунду.
	// Перебор акторов идёт только пока экран открыт.
	FVector2D Centre = WorldCentre;
	float Extent = FMath::Max(WorldExtent, 1.0f);
	for (TActorIterator<ALandscape> It(World); It; ++It)
	{
		if (const ALandscape* const Land = *It)
		{
			FVector BO, BE;
			Land->GetActorBounds(false, BO, BE);
			Centre = FVector2D(BO.X, BO.Y);
			Extent = FMath::Max3(static_cast<float>(BE.X), static_cast<float>(BE.Y), 1000.0f);
			break;
		}
	}

	FVector2D Origin;
	FVector2D MapSize;
	MapRect(AllottedGeometry.GetLocalSize(), Origin, MapSize);

	// Локальный перевод мира в экран: границы взяты у ландшафта выше, поэтому поля виджета здесь
	// уже не при чём.
	auto ToMap = [&](const FVector& W) -> FVector2D
	{
		const float Half = MapSize.X * 0.5f;
		const float X = (static_cast<float>(W.X) - Centre.X) / Extent;
		const float Y = (static_cast<float>(W.Y) - Centre.Y) / Extent;
		return FVector2D(Half + X * Half, Half + Y * Half);
	};

	auto Marker = [&](const FVector2D& At, float Size, const FLinearColor& Colour, int32 OnLayer)
	{
		const FVector2D Half(Size * 0.5f, Size * 0.5f);
		FSlateDrawElement::MakeBox(OutDrawElements, OnLayer,
			AllottedGeometry.ToPaintGeometry(FVector2D(Size, Size),
				FSlateLayoutTransform(Origin + At - Half)),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Colour);
	};

	const FSlateFontInfo SmallFont = FCoreStyle::GetDefaultFontStyle("Regular", 10);
	auto Label = [&](const FVector2D& At, const FString& Text, const FLinearColor& Colour, int32 OnLayer)
	{
		FSlateDrawElement::MakeText(OutDrawElements, OnLayer,
			AllottedGeometry.ToPaintGeometry(MapSize, FSlateLayoutTransform(Origin + At)),
			Text, SmallFont, ESlateDrawEffect::None, Colour);
	};

	// ---- ground ----
	++Layer;
	if (Background)
	{
		FSlateBrush BackgroundBrush;
		BackgroundBrush.SetResourceObject(Background);
		// Field by field. FDeprecateSlateVector2D has no constructor taking a vector, which is
		// written down in Docs/Gotchas/UI_Widgets.md and would have saved this build.
		BackgroundBrush.ImageSize.X = static_cast<float>(MapSize.X);
		BackgroundBrush.ImageSize.Y = static_cast<float>(MapSize.Y);
		FSlateDrawElement::MakeBox(OutDrawElements, Layer,
			AllottedGeometry.ToPaintGeometry(MapSize, FSlateLayoutTransform(Origin)),
			&BackgroundBrush, ESlateDrawEffect::None, FLinearColor::White);
	}
	else
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer,
			AllottedGeometry.ToPaintGeometry(MapSize, FSlateLayoutTransform(Origin)),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, GroundColour);
	}

	const URunDirectorSubsystem* const Director = URunDirectorSubsystem::GetRunDirector(World);

	// ---- units ----
	//
	// Under the points on purpose: a point with a crowd on it should still read as a point.
	if (bRevealEverything)
	{
		++Layer;
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			const APawn* const Pawn = *It;
			if (!Pawn || Pawn->IsPendingKillPending())
			{
				continue;
			}

			uint8 Team = PolarityTeams::Neutral;
			if (CoopPlayers::IsPlayer(Pawn))
			{
				Team = PolarityTeams::Players;
			}
			else if (const IGenericTeamAgentInterface* const Agent =
				Cast<IGenericTeamAgentInterface>(Pawn->GetController()))
			{
				Team = Agent->GetGenericTeamId().GetId();
			}

			if (Team == PolarityTeams::Neutral)
			{
				continue;   // props, spectators, anything without a side
			}

			const FVector2D At = ToMap(Pawn->GetActorLocation());
			const bool bIsPlayer = Team == PolarityTeams::Players;
			Marker(At, bIsPlayer ? UnitMarkerSize * 1.8f : UnitMarkerSize, ColourForTeam(Team), Layer);
		}
	}

	// ---- points ----
	if (Director)
	{
		++Layer;
		for (const FPoiWarState& State : Director->GetAllPoiStates())
		{
			const FVector2D At = ToMap(State.Location);
			const FLinearColor Colour = State.bContested
				? FLinearColor(1.0f, 0.6f, 0.15f, 1.0f)
				: ColourForTeam(State.ControllingTeam);

			Marker(At, PoiMarkerSize, Colour, Layer);

			FString Text = State.PoiTag.ToString();
			if (State.CaptureProgress > 0.0f)
			{
				Text += FString::Printf(TEXT("  %.0f%%"), State.CaptureProgress * 100.0f);
			}
			if (bShowFactionIntent)
			{
				// The two numbers that explain a crowd standing still: how many that side wants
				// here, and how many it believes it is facing.
				const uint8 Holder = State.ControllingTeam;
				const bool bIsA = Holder == PolarityTeams::FactionA;
				const int32 Wants = Director->GetDemandAt(
					Holder == PolarityTeams::Neutral ? PolarityTeams::FactionA : Holder, State.PoiTag);
				const int32 Believes = bIsA ? State.KnownEnemyForA : State.KnownEnemyForB;
				const int32 Here = bIsA ? State.PresentA : State.PresentB;
				Text += FString::Printf(TEXT("\n%d here, wants %d, sees %d"), Here, Wants, Believes);
			}

			Label(At + FVector2D(PoiMarkerSize * 0.7f, -PoiMarkerSize * 0.7f), Text, Colour, Layer + 1);
		}
		++Layer;
	}

	return Layer;
}
