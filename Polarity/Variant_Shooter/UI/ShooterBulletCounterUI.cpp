// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterBulletCounterUI.h"
#include "Variant_Shooter/HitMarkerComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"

void UShooterBulletCounterUI::NativeConstruct()
{
	Super::NativeConstruct();

	BindHitMarkerToCharacter(Cast<AShooterCharacter>(GetOwningPlayerPawn()));

	if (IonizedHitMarkerImage)
	{
		IonizedHitMarkerImage->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UShooterBulletCounterUI::BindHitMarkerToCharacter(AShooterCharacter* NewCharacter)
{
	UHitMarkerComponent* NewHitMarkerComponent = NewCharacter ? NewCharacter->GetHitMarkerComponent() : nullptr;
	if (BoundHitMarkerComponent == NewHitMarkerComponent)
	{
		if (BoundHitMarkerComponent)
		{
			BoundHitMarkerComponent->OnHitMarker.AddUniqueDynamic(this, &UShooterBulletCounterUI::HandleHitMarkerEvent);
			BoundHitMarkerComponent->OnIonizedHitMarker.AddUniqueDynamic(this, &UShooterBulletCounterUI::HandleHitMarkerEvent);
		}
		return;
	}

	if (BoundHitMarkerComponent)
	{
		BoundHitMarkerComponent->OnHitMarker.RemoveDynamic(this, &UShooterBulletCounterUI::HandleHitMarkerEvent);
		BoundHitMarkerComponent->OnIonizedHitMarker.RemoveDynamic(this, &UShooterBulletCounterUI::HandleHitMarkerEvent);
	}

	BoundHitMarkerComponent = NewHitMarkerComponent;
	if (BoundHitMarkerComponent)
	{
		BoundHitMarkerComponent->OnHitMarker.AddUniqueDynamic(this, &UShooterBulletCounterUI::HandleHitMarkerEvent);
		BoundHitMarkerComponent->OnIonizedHitMarker.AddUniqueDynamic(this, &UShooterBulletCounterUI::HandleHitMarkerEvent);
	}

	if (IonizedHitMarkerImage)
	{
		IonizedHitMarkerImage->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UShooterBulletCounterUI::NativeDestruct()
{
	if (BoundHitMarkerComponent)
	{
		BoundHitMarkerComponent->OnHitMarker.RemoveDynamic(this, &UShooterBulletCounterUI::HandleHitMarkerEvent);
		BoundHitMarkerComponent->OnIonizedHitMarker.RemoveDynamic(this, &UShooterBulletCounterUI::HandleHitMarkerEvent);
	}
	BoundHitMarkerComponent = nullptr;

	Super::NativeDestruct();
}

void UShooterBulletCounterUI::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!IonizedHitMarkerImage || !BoundHitMarkerComponent)
	{
		return;
	}

	FHitMarkerEvent ActiveEvent;
	const bool bShowingIonized = BoundHitMarkerComponent->GetActiveHitMarker(ActiveEvent)
		&& ActiveEvent.HitType == EHitMarkerType::Ionized;

	if (bShowingIonized)
	{
		IonizedHitMarkerImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		IonizedHitMarkerImage->SetColorAndOpacity(
			BoundHitMarkerComponent->GetHitMarkerColor().CopyWithNewOpacity(BoundHitMarkerComponent->GetHitMarkerAlpha()));
		if (HitMarkerImage)
		{
			HitMarkerImage->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	else if (IonizedHitMarkerImage->GetVisibility() != ESlateVisibility::Collapsed)
	{
		IonizedHitMarkerImage->SetVisibility(ESlateVisibility::Collapsed);
		if (HitMarkerImage)
		{
			HitMarkerImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
}

void UShooterBulletCounterUI::HandleHitMarkerEvent(const FHitMarkerEvent& HitEvent)
{
	if (!IonizedHitMarkerImage)
	{
		return;
	}

	const bool bIonized = HitEvent.HitType == EHitMarkerType::Ionized;
	IonizedHitMarkerImage->SetVisibility(bIonized ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (HitMarkerImage)
	{
		HitMarkerImage->SetVisibility(bIonized ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

void UShooterBulletCounterUI::BP_OnHealthChanged_Implementation(float CurrentHP, float MaxHP, float LifePercent, float ArmorPercent)
{
	// Preserve existing Blueprint HUD bars/effects while exposing exact HP values through the new event.
	BP_Damaged(LifePercent, ArmorPercent);

	if (!WidgetTree)
	{
		return;
	}

	if (UProgressBar* HPProgress = Cast<UProgressBar>(WidgetTree->FindWidget(TEXT("HP_Progress"))))
	{
		HPProgress->SetPercent(LifePercent);
	}

	UTextBlock* HPText = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HPText_1")));
	if (!HPText)
	{
		HPText = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HPText")));
	}

	if (HPText)
	{
		const int32 RoundedCurrentHP = FMath::RoundToInt(CurrentHP);
		const int32 RoundedMaxHP = FMath::RoundToInt(MaxHP);
		HPText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), RoundedCurrentHP, RoundedMaxHP)));
	}
}
