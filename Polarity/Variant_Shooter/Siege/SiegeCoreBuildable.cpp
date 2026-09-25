// SiegeCoreBuildable.cpp

#include "SiegeCoreBuildable.h"

ASiegeCoreBuildable::ASiegeCoreBuildable()
{
	// A base, not a sentry: it has to survive a wave on its own.
	MaxHealth = 1000.0f;
	bSiegeCore = true;
}
