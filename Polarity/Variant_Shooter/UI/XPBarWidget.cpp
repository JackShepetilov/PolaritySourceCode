// XPBarWidget.cpp

#include "XPBarWidget.h"

#include "XPSubsystem.h"

#include "Engine/GameInstance.h"

void UXPBarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UXPSubsystem* XP = GetXPSubsystem())
	{
		XP->OnSkillXPGained.AddDynamic(this, &UXPBarWidget::HandleSkillXPGained);
		XP->OnSkillLevelUp.AddDynamic(this, &UXPBarWidget::HandleSkillLevelUp);
	}

	RefreshFromSubsystem();
	BP_OnXPChanged(CachedCurrentXP, CachedXPToNext, CachedProgress01);
	BP_OnLevelChanged(CachedCurrentLevel);
}

void UXPBarWidget::NativeDestruct()
{
	if (UXPSubsystem* XP = GetXPSubsystem())
	{
		XP->OnSkillXPGained.RemoveDynamic(this, &UXPBarWidget::HandleSkillXPGained);
		XP->OnSkillLevelUp.RemoveDynamic(this, &UXPBarWidget::HandleSkillLevelUp);
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

	CachedCurrentXP = XP->GetCurrentXP(CategoryToShow);
	CachedCurrentLevel = XP->GetCurrentLevel(CategoryToShow);
	CachedXPToNext = XP->GetXPToNextLevel(CategoryToShow);
	CachedProgress01 = XP->GetProgressToNextLevel01(CategoryToShow);
}

void UXPBarWidget::HandleSkillXPGained(ESkillCategory Category, int32 Amount, int32 NewTotalXP)
{
	if (Category != CategoryToShow) return;
	RefreshFromSubsystem();
	BP_OnXPChanged(CachedCurrentXP, CachedXPToNext, CachedProgress01);
}

void UXPBarWidget::HandleSkillLevelUp(ESkillCategory Category, int32 NewLevel)
{
	if (Category != CategoryToShow) return;
	RefreshFromSubsystem();
	BP_OnLevelChanged(NewLevel);
}
