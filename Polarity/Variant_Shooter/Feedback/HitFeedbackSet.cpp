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

	// Breaking a part (a track, the sights) is the same news as breaking a shield: the job changed.
	if (Context.bShieldBroken || Context.bPartBroken)
	{
		return EHitFeedbackCue::ShieldBreak;
	}

	// Did nothing, and why: bounced off the plate, or hit a part that is already gone. Above the
	// headshot and the shield on purpose; these are the hits the player has to stop making.
	if (Context.bDeflected)
	{
		return EHitFeedbackCue::Deflected;
	}
	if (Context.bDeadPart)
	{
		return EHitFeedbackCue::DeadPart;
	}

	// A headshot is a headshot whether the head was under armour or not: with armour on every
	// grunt, testing the shield first hid every headshot for as long as the armour held.
	if (Context.bHeadshot)
	{
		return EHitFeedbackCue::Headshot;
	}

	// A shield that was still holding is what the shot hit, and that is true whether or not any
	// health was written. Tested before bZeroDamage: a hit the shield took whole wrote no health.
	if (Context.bShieldHit)
	{
		return EHitFeedbackCue::HitShield;
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
	case EHitFeedbackCue::Deflected:	return 1;
	case EHitFeedbackCue::DeadPart:		return 1;
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
	case EHitFeedbackCue::Deflected:	Found = &Deflected;		break;
	case EHitFeedbackCue::DeadPart:		Found = &DeadPart;		break;
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
