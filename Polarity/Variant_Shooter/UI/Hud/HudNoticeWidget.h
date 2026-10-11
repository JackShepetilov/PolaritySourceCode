// HudNoticeWidget.h
// HUD.Slot.Notice: short lines under the crosshair that say what just started working for you.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "GameplayTagContainer.h"
#include "HudSlotWidget.h"
#include "HudNoticeWidget.generated.h"

class UPanelWidget;
class UTextBlock;
class UUpgradeManagerComponent;

/**
 * Inherit in Blueprint (WBP_Notice) with a VerticalBox named Lines. Each notice is one line: it
 * appears when UUpgradeManagerComponent::OnUpgradeNotice says so, stays for the time it was given
 * and fades. The same key again restarts its line instead of adding a second one.
 *
 * The text comes from whoever raised the notice (an upgrade's own asset); nothing is written here.
 */
UCLASS(Abstract, Blueprintable)
class POLARITY_API UHudNoticeWidget : public UHudSlotWidget
{
	GENERATED_BODY()

public:
	/** Font of a line. Unset = the engine default at NoticeFontSize. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notice")
	FSlateFontInfo NoticeFont;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notice", meta = (ClampMin = "6"))
	int32 NoticeFontSize = 18;

	/** Palette colour of a line whose notice names none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notice", meta = (Categories = "Palette"))
	FGameplayTag DefaultColorTag;

	/** Seconds the fade at the end of a line takes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notice", meta = (ClampMin = "0.01"))
	float FadeTime = 0.3f;

protected:
	virtual void NativeBind(AShooterCharacter* Character) override;
	virtual void NativeUnbind() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION()
	void HandleNotice(FName Key, FText Text, FGameplayTag ColorTag, float Duration);

	UPROPERTY(BlueprintReadOnly, Category = "Notice", meta = (BindWidget))
	TObjectPtr<UPanelWidget> Lines;

private:
	struct FNoticeLine
	{
		FName Key;
		TWeakObjectPtr<UTextBlock> Text;
		float TimeLeft = 0.0f;
	};

	TArray<FNoticeLine> Shown;
	TWeakObjectPtr<UUpgradeManagerComponent> Upgrades;

	void RemoveLine(int32 Index);
};
