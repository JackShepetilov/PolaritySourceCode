// GrenadeDefinition.cpp

#include "Variant_Shooter/Grenades/GrenadeDefinition.h"
#include "Variant_Shooter/Grenades/AbilityHandler_Grenade.h"

UGrenadeDefinition::UGrenadeDefinition()
{
	HandlerClass = UAbilityHandler_Grenade::StaticClass();

	// The bag is the limit, not a clock.
	ThrowCooldown = 0.0f;

	// A grenade is lobbed: looking level still sends it up a little.
	ThrowPitchOffset = 8.0f;
	PreviewRadius = 8.0f;
}
