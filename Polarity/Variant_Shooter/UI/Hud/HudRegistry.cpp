// HudRegistry.cpp

#include "HudRegistry.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "HudBindable.h"
#include "HudLayoutAsset.h"
#include "HudRootWidget.h"
#include "HudSlot.h"
#include "Variant_Shooter/ShooterCharacter.h"

void UHudRegistry::Build(APlayerController* Controller, UHudLayoutAsset* Layout)
{
	Teardown();

	if (!Controller || !Controller->IsLocalPlayerController())
	{
		UE_LOG(LogTemp, Warning, TEXT("[HUD_DEBUG] Build called without a local controller, no HUD"));
		return;
	}
	if (!Layout || !Layout->RootClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[HUD_DEBUG] %s has no HUD layout or the layout has no RootClass, no HUD"),
			*GetNameSafe(Controller));
		return;
	}

	Root = CreateWidget<UHudRootWidget>(Controller, Layout->RootClass);
	if (!Root)
	{
		UE_LOG(LogTemp, Error, TEXT("[HUD_DEBUG] could not create %s"), *GetNameSafe(Layout->RootClass));
		return;
	}
	Root->AddToPlayerScreen(0);

	for (const FHudSlotBinding& Binding : Layout->Slots)
	{
		UHudSlot* const Target = Root->FindSlot(Binding.SlotTag);
		if (!Target)
		{
			UE_LOG(LogTemp, Warning, TEXT("[HUD_DEBUG] %s has no slot %s, %s not placed"),
				*GetNameSafe(Layout->RootClass), *Binding.SlotTag.ToString(), *GetNameSafe(Binding.WidgetClass));
			continue;
		}
		if (!Binding.WidgetClass)
		{
			continue;
		}
		if (Widgets.Contains(Binding.SlotTag))
		{
			UE_LOG(LogTemp, Warning, TEXT("[HUD_DEBUG] slot %s listed twice in %s, second entry ignored"),
				*Binding.SlotTag.ToString(), *GetNameSafe(Layout));
			continue;
		}

		UUserWidget* const Widget = CreateWidget<UUserWidget>(Controller, Binding.WidgetClass);
		if (!Widget)
		{
			UE_LOG(LogTemp, Error, TEXT("[HUD_DEBUG] could not create %s for slot %s"),
				*GetNameSafe(Binding.WidgetClass), *Binding.SlotTag.ToString());
			continue;
		}

		Target->SetContent(Widget);
		if (Binding.bStartHidden)
		{
			Widget->SetVisibility(ESlateVisibility::Collapsed);
		}
		Widgets.Add(Binding.SlotTag, Widget);
		if (!Widget->GetClass()->ImplementsInterface(UHudBindable::StaticClass()))
		{
			UE_LOG(LogTemp, Log, TEXT("[HUD_DEBUG] %s in slot %s is not IHudBindable: placed, never bound"),
				*GetNameSafe(Widget), *Binding.SlotTag.ToString());
		}
		OnSlotFilled.Broadcast(Binding.SlotTag, Widget);
	}

	BoundController = Controller;
	Controller->OnPossessedPawnChanged.AddUniqueDynamic(this, &UHudRegistry::HandlePawnChanged);
	UE_LOG(LogTemp, Log, TEXT("[HUD_DEBUG] built %s for %s: %d of %d slots filled"),
		*GetNameSafe(Root), *GetNameSafe(Controller), Widgets.Num(), Layout->Slots.Num());

	// The starting pawn usually possesses before BeginPlay gets here, so the change event has
	// already fired into nothing. Bind whatever the controller has right now.
	if (APawn* Pawn = Controller->GetPawn())
	{
		BindPawn(Pawn);
	}
}

void UHudRegistry::Teardown()
{
	BindPawn(nullptr);

	if (APlayerController* Controller = BoundController.Get())
	{
		Controller->OnPossessedPawnChanged.RemoveDynamic(this, &UHudRegistry::HandlePawnChanged);
	}
	BoundController.Reset();

	for (TPair<FGameplayTag, TObjectPtr<UUserWidget>>& Pair : Widgets)
	{
		if (Pair.Value)
		{
			Pair.Value->RemoveFromParent();
		}
	}
	Widgets.Empty();

	if (Root)
	{
		Root->RemoveFromParent();
		Root = nullptr;
	}
}

UUserWidget* UHudRegistry::GetSlotWidget(const FGameplayTag& SlotTag) const
{
	const TObjectPtr<UUserWidget>* Found = Widgets.Find(SlotTag);
	return Found ? Found->Get() : nullptr;
}

void UHudRegistry::SetSlotVisible(const FGameplayTag& SlotTag, bool bVisible)
{
	if (UUserWidget* Widget = GetSlotWidget(SlotTag))
	{
		Widget->SetVisibility(bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UHudRegistry::BindPawn(APawn* Pawn)
{
	AShooterCharacter* const Character = Cast<AShooterCharacter>(Pawn);
	int32 Bound = 0;
	for (TPair<FGameplayTag, TObjectPtr<UUserWidget>>& Pair : Widgets)
	{
		IHudBindable* const Bindable = Cast<IHudBindable>(Pair.Value.Get());
		if (!Bindable)
		{
			continue;
		}
		Bindable->UnbindCharacter();
		if (Character)
		{
			Bindable->BindCharacter(Character);
			++Bound;
		}
	}
	if (Character)
	{
		UE_LOG(LogTemp, Log, TEXT("[HUD_DEBUG] bound %d slots to %s"), Bound, *GetNameSafe(Character));
	}
}

void UHudRegistry::HandlePawnChanged(APawn* OldPawn, APawn* NewPawn)
{
	BindPawn(NewPawn);
}

void UHudRegistry::Deinitialize()
{
	Teardown();
	Super::Deinitialize();
}
