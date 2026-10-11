// HudNoticeWidget.cpp

#include "HudNoticeWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "PolarityPalette.h"
#include "Upgrades/UpgradeManagerComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"

void UHudNoticeWidget::NativeBind(AShooterCharacter* Character)
{
	Upgrades = Character->GetUpgradeManager();
	if (UUpgradeManagerComponent* const Manager = Upgrades.Get())
	{
		Manager->OnUpgradeNotice.AddUniqueDynamic(this, &UHudNoticeWidget::HandleNotice);
	}
}

void UHudNoticeWidget::NativeUnbind()
{
	if (UUpgradeManagerComponent* const Manager = Upgrades.Get())
	{
		Manager->OnUpgradeNotice.RemoveDynamic(this, &UHudNoticeWidget::HandleNotice);
	}
	Upgrades.Reset();

	for (int32 Index = Shown.Num() - 1; Index >= 0; --Index)
	{
		RemoveLine(Index);
	}
}

void UHudNoticeWidget::RemoveLine(int32 Index)
{
	if (UTextBlock* const Line = Shown[Index].Text.Get())
	{
		Line->RemoveFromParent();
	}
	Shown.RemoveAt(Index);
}

void UHudNoticeWidget::HandleNotice(FName Key, FText Text, FGameplayTag ColorTag, float Duration)
{
	const int32 Existing = Shown.IndexOfByPredicate([Key](const FNoticeLine& L) { return L.Key == Key; });

	// No time left to show: the thing it announced has ended.
	if (Duration <= 0.0f || Text.IsEmpty())
	{
		if (Existing != INDEX_NONE)
		{
			Shown[Existing].TimeLeft = FMath::Min(Shown[Existing].TimeLeft, FadeTime);
		}
		return;
	}

	if (!Lines || !WidgetTree)
	{
		return;
	}

	UTextBlock* Line = Existing != INDEX_NONE ? Shown[Existing].Text.Get() : nullptr;
	if (!Line)
	{
		Line = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		if (!Line)
		{
			return;
		}

		FSlateFontInfo Font = NoticeFont.HasValidFont() ? NoticeFont : Line->GetFont();
		if (!NoticeFont.HasValidFont())
		{
			Font.Size = NoticeFontSize;
		}
		Line->SetFont(Font);
		Line->SetJustification(ETextJustify::Center);
		Line->SetShadowOffset(FVector2D(0.0f, 1.0f));
		Line->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.8f));
		Lines->AddChild(Line);

		if (Existing != INDEX_NONE)
		{
			Shown[Existing].Text = Line;
		}
		else
		{
			FNoticeLine& Added = Shown.AddDefaulted_GetRef();
			Added.Key = Key;
			Added.Text = Line;
		}
	}

	const FGameplayTag UseTag = ColorTag.IsValid() ? ColorTag : DefaultColorTag;
	Line->SetText(Text);
	Line->SetColorAndOpacity(FSlateColor(UPolarityPalette::GetColor(UseTag, FLinearColor::White)));
	Line->SetRenderOpacity(1.0f);

	FNoticeLine* const Entry = Shown.FindByPredicate([Key](const FNoticeLine& L) { return L.Key == Key; });
	if (Entry)
	{
		Entry->TimeLeft = Duration;
	}
}

void UHudNoticeWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	for (int32 Index = Shown.Num() - 1; Index >= 0; --Index)
	{
		FNoticeLine& Entry = Shown[Index];
		Entry.TimeLeft -= InDeltaTime;

		UTextBlock* const Line = Entry.Text.Get();
		if (!Line || Entry.TimeLeft <= 0.0f)
		{
			RemoveLine(Index);
			continue;
		}
		Line->SetRenderOpacity(FMath::Clamp(Entry.TimeLeft / FadeTime, 0.0f, 1.0f));
	}
}
