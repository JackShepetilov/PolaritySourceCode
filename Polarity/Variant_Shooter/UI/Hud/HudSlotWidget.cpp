// HudSlotWidget.cpp

#include "HudSlotWidget.h"

#include "Variant_Shooter/ShooterCharacter.h"

void UHudSlotWidget::BindCharacter(AShooterCharacter* Character)
{
	if (BoundCharacter.IsValid())
	{
		UnbindCharacter();
	}
	BoundCharacter = Character;
	if (Character)
	{
		NativeBind(Character);
		BP_OnBound(Character);
	}
}

void UHudSlotWidget::UnbindCharacter()
{
	if (!BoundCharacter.IsValid())
	{
		BoundCharacter.Reset();
		return;
	}
	NativeUnbind();
	BP_OnUnbound();
	BoundCharacter.Reset();
}

void UHudSlotWidget::NativeDestruct()
{
	UnbindCharacter();
	Super::NativeDestruct();
}
