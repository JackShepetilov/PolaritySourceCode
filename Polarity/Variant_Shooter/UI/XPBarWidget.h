// XPBarWidget.h
// HUD widget showing the run's single XP progress and level.
// One instance per run HUD — there is no longer a per-skill split.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
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
	void HandleXPGained(int32 Amount, int32 NewTotalXP);

	UFUNCTION()
	void HandleLevelUp(int32 NewLevel);

	void RefreshFromSubsystem();
	UXPSubsystem* GetXPSubsystem() const;

	UPROPERTY(BlueprintReadOnly, Category = "XP")
	int32 CachedCurrentXP = 0;

	UPROPERTY(BlueprintReadOnly, Category = "XP")
	int32 CachedCurrentLevel = 0;

	UPROPERTY(BlueprintReadOnly, Category = "XP")
	int32 CachedXPToNext = 0;

	UPROPERTY(BlueprintReadOnly, Category = "XP")
	float CachedProgress01 = 0.f;
};
