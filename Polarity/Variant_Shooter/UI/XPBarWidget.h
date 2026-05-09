// XPBarWidget.h
// HUD widget showing one skill's XP progress and level.
// Set CategoryToShow in WBP class defaults — widget filters subsystem events to that category.
// To show all 4 skills, build a container WBP that statically embeds 4 instances of WBP_XPBar,
// each with a different CategoryToShow.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SkillTypes.h"
#include "XPBarWidget.generated.h"

class UXPSubsystem;

UCLASS(Abstract, Blueprintable)
class POLARITY_API UXPBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "XP")
	int32 GetCurrentXP() const { return CachedCurrentXP; }

	UFUNCTION(BlueprintPure, Category = "XP")
	int32 GetCurrentLevel() const { return CachedCurrentLevel; }

	UFUNCTION(BlueprintPure, Category = "XP")
	int32 GetXPToNextLevel() const { return CachedXPToNext; }

	UFUNCTION(BlueprintPure, Category = "XP")
	float GetProgress01() const { return CachedProgress01; }

	UFUNCTION(BlueprintPure, Category = "XP")
	ESkillCategory GetCategoryToShow() const { return CategoryToShow; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ==================== Blueprint Events ====================

	UFUNCTION(BlueprintImplementableEvent, Category = "XP",
		meta = (DisplayName = "On XP Changed"))
	void BP_OnXPChanged(int32 CurrentXP, int32 XPToNext, float Progress01);

	UFUNCTION(BlueprintImplementableEvent, Category = "XP",
		meta = (DisplayName = "On Level Changed"))
	void BP_OnLevelChanged(int32 NewLevel);

	// ==================== Internal ====================

	UFUNCTION()
	void HandleSkillXPGained(ESkillCategory Category, int32 Amount, int32 NewTotalXP);

	UFUNCTION()
	void HandleSkillLevelUp(ESkillCategory Category, int32 NewLevel);

	void RefreshFromSubsystem();
	UXPSubsystem* GetXPSubsystem() const;

	/** Which skill this bar shows. Set per-instance in the parent WBP_RunHUD (or in Class Defaults). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "XP")
	ESkillCategory CategoryToShow = ESkillCategory::Movement;

	UPROPERTY(BlueprintReadOnly, Category = "XP")
	int32 CachedCurrentXP = 0;

	UPROPERTY(BlueprintReadOnly, Category = "XP")
	int32 CachedCurrentLevel = 0;

	UPROPERTY(BlueprintReadOnly, Category = "XP")
	int32 CachedXPToNext = 0;

	UPROPERTY(BlueprintReadOnly, Category = "XP")
	float CachedProgress01 = 0.f;
};
