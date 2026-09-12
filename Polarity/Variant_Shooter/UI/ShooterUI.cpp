// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterUI.h"
#include "ShooterPlayerState.h"
#include "Components/TextBlock.h"

void UShooterUI::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// One pointer compare a frame once bound. Rebinds by itself if the PlayerState is ever swapped.
	AShooterPlayerState* State = GetOwningPlayerState<AShooterPlayerState>();
	if (State == BoundPlayerState.Get())
	{
		return;
	}

	if (AShooterPlayerState* Old = BoundPlayerState.Get())
	{
		Old->OnMetalChanged.RemoveDynamic(this, &UShooterUI::HandleMetalChanged);
	}

	BoundPlayerState = State;
	if (State)
	{
		State->OnMetalChanged.AddDynamic(this, &UShooterUI::HandleMetalChanged);
		HandleMetalChanged(State->GetMetal(), State->GetMaxMetal(), 0);
	}
}

void UShooterUI::NativeDestruct()
{
	if (AShooterPlayerState* State = BoundPlayerState.Get())
	{
		State->OnMetalChanged.RemoveDynamic(this, &UShooterUI::HandleMetalChanged);
	}
	BoundPlayerState.Reset();

	Super::NativeDestruct();
}

void UShooterUI::HandleMetalChanged(int32 Metal, int32 MaxMetal, int32 Delta)
{
	if (MetalText)
	{
		MetalText->SetText(FText::Format(NSLOCTEXT("ShooterUI", "MetalCounter", "{0} / {1}"),
			FText::AsNumber(Metal), FText::AsNumber(MaxMetal)));
	}

	BP_OnMetalChanged(Metal, MaxMetal, Delta);
}
