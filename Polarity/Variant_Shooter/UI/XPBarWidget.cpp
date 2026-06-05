// XPBarWidget.cpp

#include "XPBarWidget.h"

#include "XPSubsystem.h"

#include "Engine/GameInstance.h"

void UXPBarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UXPSubsystem* XP = GetXPSubsystem())
	{
		XP->OnXPGained.AddDynamic(this, &UXPBarWidget::HandleXPGained);
		XP->OnLevelUp.AddDynamic(this, &UXPBarWidget::HandleLevelUp);
	}

	RefreshFromSubsystem();
	BP_OnXPChanged(CachedCurrentXP, CachedXPToNext, CachedProgress01);
	BP_OnLevelChanged(CachedCurrentLevel);
}

void UXPBarWidget::NativeDestruct()
{
	if (UXPSubsystem* XP = GetXPSubsystem())
	{
		XP->OnXPGained.RemoveDynamic(this, &UXPBarWidget::HandleXPGained);
		XP->OnLevelUp.RemoveDynamic(this, &UXPBarWidget::HandleLevelUp);
	}

	Super::NativeDestruct();
}

UXPSubsystem* UXPBarWidget::GetXPSubsystem() const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UXPSubsystem>();
	}
	return nullptr;
}

void UXPBarWidget::RefreshFromSubsystem()
{
	UXPSubsystem* XP = GetXPSubsystem();
	if (!XP) return;

	CachedCurrentXP = XP->GetCurrentXP();
	CachedCurrentLevel = XP->GetCurrentLevel();
	CachedXPToNext = XP->GetXPToNextLevel();
	CachedProgress01 = XP->GetProgressToNextLevel01();
}

void UXPBarWidget::HandleXPGained(int32 Amount, int32 NewTotalXP)
{
	RefreshFromSubsystem();
	BP_OnXPChanged(CachedCurrentXP, CachedXPToNext, CachedProgress01);
}

void UXPBarWidget::HandleLevelUp(int32 NewLevel)
{
	RefreshFromSubsystem();
	BP_OnLevelChanged(NewLevel);
}
