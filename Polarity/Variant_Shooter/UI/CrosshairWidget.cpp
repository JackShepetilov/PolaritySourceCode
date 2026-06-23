// CrosshairWidget.cpp

#include "CrosshairWidget.h"
#include "ShooterWeapon.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

float UCrosshairWidget::ComputeBaseSizePixels() const
{
	// Size from viewport HEIGHT so the crosshair keeps the same proportion at any resolution.
	float ViewportY = 1080.0f;
	if (APlayerController* PC = GetOwningPlayer())
	{
		int32 SizeX = 0, SizeY = 0;
		PC->GetViewportSize(SizeX, SizeY);
		if (SizeY > 0)
		{
			ViewportY = static_cast<float>(SizeY);
		}
	}
	return ViewportY * BaseHeightFraction * (bArmed ? ActiveConfig.Scale : 1.0f);
}

void UCrosshairWidget::SetActiveWeapon(AShooterWeapon* Weapon)
{
	ActiveWeapon = Weapon;
	bArmed = (Weapon != nullptr);

	if (Weapon)
	{
		ActiveConfig = Weapon->GetCrosshairConfig();
	}

	// Snap bloom back to rest on every weapon change.
	CurrentBloom = 0.0f;
	CurrentSizePixels = ComputeBaseSizePixels();

	BP_OnCrosshairChanged(bArmed, ActiveConfig, CurrentSizePixels);
}

void UCrosshairWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Unarmed: nothing animates — the BP shows a static dot.
	if (!bArmed)
	{
		return;
	}

	// ---- Build the bloom target (0..1) from observable state ----
	float Target = 0.0f;

	if (const AShooterWeapon* Weapon = ActiveWeapon.Get())
	{
		if (Weapon->IsFiring())
		{
			Target += ActiveConfig.FireBloom;   // grow while shooting
		}
	}

	if (const APawn* Pawn = GetOwningPlayerPawn())
	{
		const FVector Vel = Pawn->GetVelocity();
		const float Speed2D = FVector(Vel.X, Vel.Y, 0.0f).Size();

		if (const ACharacter* Char = Cast<ACharacter>(Pawn))
		{
			if (const UCharacterMovementComponent* Move = Char->GetCharacterMovement())
			{
				const float MaxSpeed = FMath::Max(1.0f, Move->GetMaxSpeed());
				Target += ActiveConfig.MoveBloom * FMath::Clamp(Speed2D / MaxSpeed, 0.0f, 1.0f);

				if (Move->IsFalling())
				{
					Target += ActiveConfig.AirBloom;
				}
			}
		}
	}

	Target = FMath::Clamp(Target, 0.0f, 1.0f);

	// ---- Chase the target: snappy grow, gentler settle ----
	const float InterpSpeed = (Target > CurrentBloom) ? ActiveConfig.BloomAttackSpeed : ActiveConfig.BloomRecoverySpeed;
	CurrentBloom = FMath::FInterpTo(CurrentBloom, Target, InDeltaTime, InterpSpeed);

	const float NewSize = ComputeBaseSizePixels() * (1.0f + CurrentBloom * ActiveConfig.BloomScaleAdd);

	// Only ping the BP when the size actually moved (skips idle frames once settled).
	if (!FMath::IsNearlyEqual(NewSize, CurrentSizePixels, 0.05f))
	{
		CurrentSizePixels = NewSize;
		BP_OnCrosshairResized(CurrentSizePixels, CurrentBloom);
	}
}
