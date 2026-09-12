// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterUI.generated.h"

class UTextBlock;
class AShooterPlayerState;

/**
 *  Simple scoreboard UI for a first person shooter game
 */
UCLASS(abstract)
class POLARITY_API UShooterUI : public UUserWidget
{
	GENERATED_BODY()

public:

	/** Allows Blueprint to update score sub-widgets */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta = (DisplayName = "Update Score"))
	void BP_UpdateScore(uint8 TeamByte, int32 Score);

	// ==================== Metal ====================

	/** Filled with "Metal / Max" by C++ when the widget blueprint has a text block of this name.
	 *  Optional, so a HUD without one still compiles and simply shows nothing. */
	UPROPERTY(BlueprintReadOnly, Category = "Metal", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MetalText;

	/** For anything fancier than the number: a "+15" popup, a flash when the cap is hit. Delta is
	 *  0 on the first draw and when only the cap changed. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Metal", meta = (DisplayName = "On Metal Changed"))
	void BP_OnMetalChanged(int32 Metal, int32 MaxMetal, int32 Delta);

protected:

	/** Finds this screen's PlayerState and subscribes to its metal. Done here, not in Construct,
	 *  because on a client the PlayerState can arrive after the HUD is built: the HUD would bind
	 *  to nothing once and never try again. */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	virtual void NativeDestruct() override;

private:

	UFUNCTION()
	void HandleMetalChanged(int32 Metal, int32 MaxMetal, int32 Delta);

	TWeakObjectPtr<AShooterPlayerState> BoundPlayerState;
};
