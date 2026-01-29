// DamageNumberWidget.cpp
// Base class for floating damage number widgets

#include "DamageNumberWidget.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

void UDamageNumberWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bIsActive)
	{
		return;
	}

	// Update elapsed time and vertical offset
	ElapsedTime += InDeltaTime;
	CurrentVerticalOffset += FloatSpeed * InDeltaTime;

	// Get player controller for world-to-screen projection
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (!PC)
	{
		return;
	}

	// Calculate current world position (original + floating offset)
	FVector CurrentWorldPos = WorldLocation + FVector(0.0f, 0.0f, CurrentVerticalOffset);

	// Get camera location and forward vector
	FVector CameraLocation;
	FRotator CameraRotation;
	PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
	FVector CameraForward = CameraRotation.Vector();

	// Check if point is in front of camera
	FVector ToPoint = CurrentWorldPos - CameraLocation;
	float DotProduct = FVector::DotProduct(ToPoint.GetSafeNormal(), CameraForward);

	// Only show if point is in front of camera (dot > 0)
	if (DotProduct <= 0.0f)
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	// Project to screen
	FVector2D ScreenPosition;
	bool bOnScreen = PC->ProjectWorldLocationToScreen(CurrentWorldPos, ScreenPosition, false);

	// Check if projection is valid (within reasonable screen bounds)
	int32 ViewportSizeX, ViewportSizeY;
	PC->GetViewportSize(ViewportSizeX, ViewportSizeY);

	bool bValidPosition = bOnScreen &&
		ScreenPosition.X >= -500.0f && ScreenPosition.X <= ViewportSizeX + 500.0f &&
		ScreenPosition.Y >= -500.0f && ScreenPosition.Y <= ViewportSizeY + 500.0f;

	if (bValidPosition)
	{
		// Center the widget on the screen position
		FVector2D CenteredPosition = ScreenPosition - WidgetHalfSize;
		SetPositionInViewport(CenteredPosition, true);
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		// Hide if off screen or invalid
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UDamageNumberWidget::Initialize(float InDamage, EPlayerDamageCategory InCategory, const FVector& InWorldLocation)
{
	DamageValue = InDamage;
	DamageCategory = InCategory;
	WorldLocation = InWorldLocation;
	bIsActive = true;
	CurrentVerticalOffset = 0.0f;
	ElapsedTime = 0.0f;

	// Call Blueprint event to play animation
	BP_PlayDamageAnimation(DamageValue, DamageCategory, CategoryColor);
}

void UDamageNumberWidget::NotifyAnimationFinished()
{
	bIsActive = false;

	// Notify subsystem to return this widget to pool
	OnFinished.ExecuteIfBound();
}

void UDamageNumberWidget::UpdateDamage(float AdditionalDamage)
{
	// Add to accumulated damage
	DamageValue += AdditionalDamage;

	// Reset vertical offset to make the number "pop" back to origin
	CurrentVerticalOffset = 0.0f;
	ElapsedTime = 0.0f;

	// Notify Blueprint to update display
	BP_OnDamageUpdated(DamageValue);
}

void UDamageNumberWidget::ResetWidget()
{
	DamageValue = 0.0f;
	DamageCategory = EPlayerDamageCategory::Base;
	WorldLocation = FVector::ZeroVector;
	bIsActive = false;
	CategoryColor = FLinearColor::White;
	CurrentVerticalOffset = 0.0f;
	ElapsedTime = 0.0f;

	// Clear delegate
	OnFinished.Unbind();

	// Notify Blueprint
	BP_OnWidgetReset();
}
