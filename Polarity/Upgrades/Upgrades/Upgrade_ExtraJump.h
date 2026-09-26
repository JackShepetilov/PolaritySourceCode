// Upgrade_ExtraJump.h

#pragma once

#include "CoreMinimal.h"
#include "UpgradeComponent.h"
#include "Upgrade_ExtraJump.generated.h"

/** Jump-slot upgrade: extra jumps in the air. Stateless; the movement reads it through
 *  GetJumpSlotParams. */
UCLASS(BlueprintType, meta = (DisplayName = "Extra Jump"))
class POLARITY_API UUpgrade_ExtraJump : public UUpgradeComponent
{
	GENERATED_BODY()

public:

	UUpgrade_ExtraJump();

	virtual bool GetJumpSlotParams(FJumpSlotParams& Out) const override;
};
