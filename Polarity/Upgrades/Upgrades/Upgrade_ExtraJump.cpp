// Upgrade_ExtraJump.cpp

#include "Upgrade_ExtraJump.h"
#include "UpgradeDefinition_ExtraJump.h"
#include "JumpSlotParams.h"

UUpgrade_ExtraJump::UUpgrade_ExtraJump()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UUpgrade_ExtraJump::GetJumpSlotParams(FJumpSlotParams& Out) const
{
	const UUpgradeDefinition_ExtraJump* Def = Cast<UUpgradeDefinition_ExtraJump>(UpgradeDefinition);
	if (!Def)
	{
		return false;
	}

	const FExtraJumpLevelData& Data = Def->GetLevelData(CurrentLevel);
	Out.Mode = EJumpSlotMode::ExtraJump;
	Out.ExtraJumps = FMath::Max(1, Data.ExtraJumps);
	Out.Cooldown = FMath::Max(0.0f, Data.Cooldown);
	return true;
}
