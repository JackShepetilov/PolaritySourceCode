// MovementSettings.cpp
// The readouts: what the movement numbers come to. @see UMovementSettings, "Readout"

#include "MovementSettings.h"

namespace
{
	/** One flat-ground slide from EntrySpeed, by the sums of UApexMovementComponent::UpdateSlide. */
	struct FSlideRun
	{
		float Boost = 0.0f;
		float Length = 0.0f;
		float Duration = 0.0f;
		float LengthFast = 0.0f;
		float TimeFast = 0.0f;
		float SpeedAfter1s = 0.0f;
	};

	FSlideRun RunSlide(const UMovementSettings& Settings, float EntrySpeed, float Gate, float FastAbove)
	{
		FSlideRun Run;

		// StartSlide: the boost is largest at the gate and smallest 500 cm/s above it.
		const float Ratio = FMath::Clamp((EntrySpeed - Gate) / 500.0f, 0.0f, 1.0f);
		Run.Boost = FMath::Lerp(Settings.SlideMaxSpeedBurst, Settings.SlideMinSpeedBurst, Ratio);

		float Speed = EntrySpeed + Run.Boost;
		const float Floor = FMath::Max(1.0f, Settings.SlideMinSpeed);
		const float Step = 1.0f / 240.0f;
		bool bSampled = false;

		// With no bleed at all the slide never ends; 60 s says so plainly enough.
		while (Speed > Floor && Run.Duration < 60.0f)
		{
			const float Drop = (Settings.SlideFlatDeceleration + Settings.SlideFrictionPerSecond * FMath::Max(Speed, Floor)) * Step;
			Run.Length += Speed * Step;
			Run.Duration += Step;
			if (Speed > FastAbove)
			{
				Run.LengthFast += Speed * Step;
				Run.TimeFast += Step;
			}
			Speed -= Drop;
			if (!bSampled && Run.Duration >= 1.0f)
			{
				bSampled = true;
				Run.SpeedAfter1s = FMath::Max(Speed, 0.0f);
			}
			if (Drop <= 0.0f)
			{
				Run.Duration = 60.0f;
				break;
			}
		}
		return Run;
	}
}

void UMovementSettings::RefreshReadouts()
{
	// 980 is the engine's default world gravity; a level that overrides it changes all of this.
	const float Gravity = FMath::Max(1.0f, 980.0f * DefaultGravityScale);

	ReadoutJumpHeightTap = FMath::Square(JumpZVelocity) / (2.0f * Gravity);
	ReadoutSlidehopHeight = FMath::Square(SlidehopJumpZVelocity) / (2.0f * Gravity);

	// Held: JumpHoldForce on top of gravity for JumpHoldTime, while still going up (UpdateJumpHold).
	{
		const float Step = 1.0f / 240.0f;
		float Height = 0.0f;
		float Top = 0.0f;
		float Up = JumpZVelocity;
		float Time = 0.0f;
		while (Time < 20.0f)
		{
			if (Time < JumpHoldTime && Up > 0.0f)
			{
				Up += JumpHoldForce * Step;
			}
			Up -= Gravity * Step;
			Height += Up * Step;
			Time += Step;
			Top = FMath::Max(Top, Height);
			if (Height <= 0.0f)
			{
				break;
			}
		}
		// Never under the tapped one: that is exact and this is summed in steps.
		ReadoutJumpHeightHeld = FMath::Max(Top, ReadoutJumpHeightTap);
		ReadoutJumpAirTime = Time;
		ReadoutJumpLengthAtSprint = SprintSpeed * Time;
	}

	// The air jump sets the same JumpZVelocity and gets no hold.
	ReadoutDoubleJumpHeight = ReadoutJumpHeightHeld + ReadoutJumpHeightTap;

	ReadoutSlideStartGate = SlideStartSprintFraction > 0.0f
		? FMath::Min(SlideMinStartSpeed, SprintSpeed * SlideStartSprintFraction) : SlideMinStartSpeed;

	const FSlideRun FromSprint = RunSlide(*this, SprintSpeed, ReadoutSlideStartGate, SprintSpeed);
	ReadoutSlideBoostAtSprint = FromSprint.Boost;
	ReadoutSlidePeakSpeed = SprintSpeed + FromSprint.Boost;
	ReadoutSlideLength = FromSprint.Length;
	ReadoutSlideDuration = FromSprint.Duration;
	ReadoutSlideLengthFasterThanSprint = FromSprint.LengthFast;
	ReadoutSlideTimeFasterThanSprint = FromSprint.TimeFast;
	ReadoutSlideSpeedAfter1s = FromSprint.SpeedAfter1s;

	const FSlideRun FromGate = RunSlide(*this, ReadoutSlideStartGate, ReadoutSlideStartGate, SprintSpeed);
	ReadoutSlideLengthFromGate = FromGate.Length;
	ReadoutSlideDurationFromGate = FromGate.Duration;

	ReadoutSlideClearance = CrouchingCapsuleHalfHeight * 2.0f;
}

void UMovementSettings::PostLoad()
{
	Super::PostLoad();
	RefreshReadouts();
}

#if WITH_EDITOR
void UMovementSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshReadouts();
}
#endif
