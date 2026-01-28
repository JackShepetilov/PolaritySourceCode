// DamageNumberWidget.cpp
// Base class for floating damage number widgets

#include "DamageNumberWidget.h"

void UDamageNumberWidget::Initialize(float InDamage, EPlayerDamageCategory InCategory, const FVector& InWorldLocation)
{
	DamageValue = InDamage;
	DamageCategory = InCategory;
	WorldLocation = InWorldLocation;
	bIsActive = true;

	// Call Blueprint event to play animation
	BP_PlayDamageAnimation(DamageValue, DamageCategory, CategoryColor);
}

void UDamageNumberWidget::OnAnimationFinished()
{
	bIsActive = false;

	// Notify subsystem to return this widget to pool
	OnFinished.ExecuteIfBound();
}

void UDamageNumberWidget::ResetWidget()
{
	DamageValue = 0.0f;
	DamageCategory = EPlayerDamageCategory::Base;
	WorldLocation = FVector::ZeroVector;
	bIsActive = false;
	CategoryColor = FLinearColor::White;

	// Clear delegate
	OnFinished.Unbind();

	// Notify Blueprint
	BP_OnWidgetReset();
}
