// HitFeedbackSet.cpp

#include "Variant_Shooter/Feedback/HitFeedbackSet.h"

EHitFeedbackCue UHitFeedbackSet::ResolveCue(const FHitFeedbackContext& Context)
{
	// Ordered by what the player most needs to know, not by what physically happened. A shot that
	// kills is a kill first and a headshot second; a shot that takes the shield down is a shield
	// break even though it also landed on a shield.
	if (Context.bKilled)
	{
		return Context.bHeadshot ? EHitFeedbackCue::HeadshotKill : EHitFeedbackCue::Kill;
	}

	if (Context.bShieldBroken)
	{
		return EHitFeedbackCue::ShieldBreak;
	}

	// A shield that was still holding is what the shot hit, and that is true whether or not any
	// health was written. In this game a held shield absorbs the damage completely, so a shield hit
	// is ALWAYS a zero-damage hit: testing bZeroDamage before this made HitShield unreachable in
	// every case that matters, which is the only case there is.
	if (Context.bShieldHit)
	{
		return EHitFeedbackCue::HitShield;
	}

	if (Context.bHeadshot)
	{
		return EHitFeedbackCue::Headshot;
	}

	// Connected and charged, but there was no shield to speak of -- a prop, or a target that carries
	// no charge at all. Below the shield, never above it.
	if (Context.bZeroDamage)
	{
		return EHitFeedbackCue::ZeroDamage;
	}

	return EHitFeedbackCue::HitFlesh;
}

int32 UHitFeedbackSet::GetCueRank(EHitFeedbackCue Cue)
{
	switch (Cue)
	{
	case EHitFeedbackCue::HeadshotKill:	return 5;
	case EHitFeedbackCue::Kill:			return 4;
	case EHitFeedbackCue::ShieldBreak:	return 3;
	case EHitFeedbackCue::Headshot:		return 2;
	case EHitFeedbackCue::HitShield:	return 1;
	case EHitFeedbackCue::HitFlesh:		return 1;
	case EHitFeedbackCue::ZeroDamage:	return 0;
	case EHitFeedbackCue::None:
	default:							return -1;
	}
}

const FHitFeedbackCue* UHitFeedbackSet::FindCue(EHitFeedbackCue Cue) const
{
	const FHitFeedbackCue* Found = nullptr;

	switch (Cue)
	{
	case EHitFeedbackCue::HitFlesh:		Found = &HitFlesh;		break;
	case EHitFeedbackCue::HitShield:	Found = &HitShield;		break;
	case EHitFeedbackCue::Headshot:		Found = &Headshot;		break;
	case EHitFeedbackCue::ShieldBreak:	Found = &ShieldBreak;	break;
	case EHitFeedbackCue::Kill:			Found = &Kill;			break;
	case EHitFeedbackCue::HeadshotKill:	Found = &HeadshotKill;	break;
	case EHitFeedbackCue::ZeroDamage:	Found = &ZeroDamage;	break;
	default:							return nullptr;
	}

	// An empty slot is not a configured silence: it means this set has nothing to say about that
	// cue, and the caller should fall back to whatever it used before this asset existed.
	return (Found && Found->IsSet()) ? Found : nullptr;
}

const FImpactFeedback& UHitFeedbackSet::FindImpact(EPhysicalSurface Surface) const
{
	if (const FImpactFeedback* Found = Impacts.Find(Surface))
	{
		return *Found;
	}

	return DefaultImpact;
}
