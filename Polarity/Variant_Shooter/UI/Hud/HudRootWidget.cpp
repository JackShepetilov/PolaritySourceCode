// HudRootWidget.cpp

#include "HudRootWidget.h"

#include "Blueprint/WidgetTree.h"
#include "HudSlot.h"

UHudSlot* UHudRootWidget::FindSlot(FGameplayTag SlotTag) const
{
	TArray<UHudSlot*> Slots;
	GetSlots(Slots);
	for (UHudSlot* Found : Slots)
	{
		if (Found->SlotTag == SlotTag)
		{
			return Found;
		}
	}
	return nullptr;
}

void UHudRootWidget::GetSlots(TArray<UHudSlot*>& OutSlots) const
{
	OutSlots.Reset();
	if (!WidgetTree)
	{
		return;
	}
	WidgetTree->ForEachWidget([&OutSlots](UWidget* Widget)
	{
		if (UHudSlot* Found = Cast<UHudSlot>(Widget))
		{
			OutSlots.Add(Found);
		}
	});
}
