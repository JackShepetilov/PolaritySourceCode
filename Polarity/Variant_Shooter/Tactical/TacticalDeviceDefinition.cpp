// TacticalDeviceDefinition.cpp

#include "Variant_Shooter/Tactical/TacticalDeviceDefinition.h"
#include "Variant_Shooter/Tactical/TacticalDeviceComponent.h"
#include "Variant_Shooter/Tactical/TacticalDeviceHandlers.h"

namespace
{
	template <class THandler>
	UTacticalDeviceHandler* TacticalMakeHandler(const UTacticalDeviceDefinition* Definition, UTacticalDeviceComponent* Owner)
	{
		if (!Owner)
		{
			return nullptr;
		}
		THandler* const Handler = NewObject<THandler>(Owner);
		Handler->Init(Owner, Definition);
		return Handler;
	}
}

UTacticalDevice_Shield::UTacticalDevice_Shield()
{
	// The pool is spent by hits, not by time.
	ActiveDuration = 0.0f;
	RechargeDuration = 5.0f;
	RechargeDelay = 2.0f;
	MinChargeToActivate = 0.3f;
	Color = FLinearColor(0.3f, 0.6f, 1.0f, 1.0f);
}

UTacticalDeviceHandler* UTacticalDevice_Shield::CreateHandler(UTacticalDeviceComponent* Owner) const
{
	return TacticalMakeHandler<UTacticalDeviceHandler_Shield>(this, Owner);
}

UTacticalDevice_Light::UTacticalDevice_Light()
{
	ActiveDuration = 6.0f;
	RechargeDuration = 4.0f;
	RechargeDelay = 1.0f;
	MinChargeToActivate = 0.2f;
	Color = FLinearColor(1.0f, 0.95f, 0.8f, 1.0f);

	// Mannequin names first; the others cover skeletons that spell them differently.
	FaceBoneNames = { FName("head"), FName("neck_01"), FName("neck_02"), FName("Head"), FName("neck"), FName("Neck") };
}

UTacticalDeviceHandler* UTacticalDevice_Light::CreateHandler(UTacticalDeviceComponent* Owner) const
{
	return TacticalMakeHandler<UTacticalDeviceHandler_Light>(this, Owner);
}

UTacticalDevice_Freeze::UTacticalDevice_Freeze()
{
	ActiveDuration = 4.0f;
	RechargeDuration = 5.0f;
	RechargeDelay = 1.0f;
	MinChargeToActivate = 0.25f;
	Color = FLinearColor(0.5f, 0.9f, 1.0f, 1.0f);
}

UTacticalDeviceHandler* UTacticalDevice_Freeze::CreateHandler(UTacticalDeviceComponent* Owner) const
{
	return TacticalMakeHandler<UTacticalDeviceHandler_Freeze>(this, Owner);
}
