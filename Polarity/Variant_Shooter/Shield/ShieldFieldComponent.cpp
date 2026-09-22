// ShieldFieldComponent.cpp
// See the header for what this component is for and what Phase 0 means.

#include "Variant_Shooter/Shield/ShieldFieldComponent.h"

// PHASE 0/1 ONLY: the adapter below reads the charge meter, and this include goes away with it in
// Phase 2. Nothing else in this file touches the plugin.
#include "EMFVelocityModifier.h"

#include "GameFramework/Actor.h"

UShieldFieldComponent::UShieldFieldComponent()
{
	// Nothing to tick. Every answer here is derived on demand, and the pool that will need a clock
	// (regeneration, "when was it last hit" bookkeeping) arrives in Phase 1 together with the state
	// it is for. A component that ticks for nothing is a permanent cost paid for a temporary reason.
	PrimaryComponentTick.bCanEverTick = false;
}

UEMFVelocityModifier* UShieldFieldComponent::ResolveChargeSource() const
{
	const AActor* const Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UEMFVelocityModifier>() : nullptr;
}

float UShieldFieldComponent::GetStrippedFraction() const
{
	// PHASE 0 PARITY: the shield is the charge meter, read the way the gates read it before this
	// component existed - magnitude, against the owner's own ceiling.
	const UEMFVelocityModifier* const Modifier = ResolveChargeSource();
	if (!Modifier)
	{
		// No charge meter on this owner: the mechanic never applied to it, so nothing has been
		// stripped. This is also the answer the AI condition already gives for such an NPC (it keeps
		// pushing), and the weapon gate's own reading of one: a component that is not there cannot
		// be full.
		return 0.0f;
	}

	const float Cap = Modifier->MaxBaseCharge;
	if (Cap <= KINDA_SMALL_NUMBER)
	{
		// No ceiling configured: there is nothing to be stripped relative to, which reads as a whole
		// shield. Same as the AI condition's "Cap <= KINDA_SMALL_NUMBER -> shield is up".
		return 0.0f;
	}

	// Magnitude, not the signed value: a negatively electrified enemy has had exactly as much shield
	// taken off it as a positive one, and the sign only says which way it was pushed.
	return FMath::Clamp(FMath::Abs(Modifier->GetCharge()) / Cap, 0.0f, 1.0f);
}

bool UShieldFieldComponent::IsShieldUp() const
{
	// PHASE 0 PARITY, asked the same way round as the gate it replaces: the ceiling is where the
	// shield BREAKS, so "up" is IsAtMaxCharge inverted. Deliberately not derived from the fraction
	// above - IsAtMaxCharge carries its own tolerance (>= Cap - KINDA_SMALL_NUMBER) and this answer
	// has to agree with it on the very frame the meter fills, which is the frame the whole fight
	// turns on.
	if (const UEMFVelocityModifier* const Modifier = ResolveChargeSource())
	{
		return !Modifier->IsAtMaxCharge();
	}

	return true;
}

UShieldFieldComponent* UShieldFieldStatics::GetShieldField(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UShieldFieldComponent>() : nullptr;
}

bool UShieldFieldStatics::IsShieldUp(const AActor* Actor)
{
	if (const UShieldFieldComponent* const Field = GetShieldField(Actor))
	{
		return Field->IsShieldUp();
	}

	return false;
}
