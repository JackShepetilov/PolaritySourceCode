// AimGlintComponent.cpp

#include "Variant_Shooter/Weapons/AimGlintComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Coop/CoopPlayers.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

UAimGlintComponent::UAimGlintComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCastShadow(false);
	SetVisibleFlag(false);
}

void UAimGlintComponent::BeginPlay()
{
	Super::BeginPlay();
	SetVisibility(false);
}

void UAimGlintComponent::EnsureInstance(const AShooterWeapon* Weapon)
{
	if (GlintInstance)
	{
		return;
	}
	UMaterialInterface* const Base = GlintMaterial ? GlintMaterial.Get() : (Weapon ? Weapon->AIAimGlintMaterial.Get() : nullptr);
	if (!Base)
	{
		return;
	}
	GlintInstance = UMaterialInstanceDynamic::Create(Base, this);
	Elements.Reset();
	AddElement(GlintInstance, nullptr, /*bSizeIsInScreenSpace*/ true, StartSize, StartSize, nullptr);
}

void UAimGlintComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const AShooterWeapon* const Weapon = Cast<AShooterWeapon>(GetOwner());
	UWorld* const World = GetWorld();
	if (!Weapon || !World || !Weapon->bAIAimHolding || World->GetNetMode() == NM_DedicatedServer)
	{
		if (IsVisible())
		{
			SetVisibility(false);
		}
		return;
	}

	// The player at THIS screen: the glint is for whoever is looking, and is not gameplay.
	const APlayerController* const Viewer = CoopPlayers::GetLocalController(World);
	const APawn* const Holder = Cast<APawn>(Weapon->GetOwner());
	if (!Viewer || !Viewer->PlayerCameraManager || !Holder)
	{
		SetVisibility(false);
		return;
	}
	EnsureInstance(Weapon);
	if (!GlintInstance)
	{
		SetVisibility(false);
		return;
	}

	const FVector Eye = Viewer->PlayerCameraManager->GetCameraLocation();
	const FVector Muzzle = Weapon->GetMuzzleWorldLocation();
	// Where the holder is aiming, not where the mesh of the gun happens to point this frame.
	const FVector Aim = Holder->GetBaseAimRotation().Vector();
	const FVector Scope = Muzzle - Aim * BackFromMuzzle + FVector(0.0f, 0.0f, AboveBarrel);

	const FVector ToEye = (Eye - Scope).GetSafeNormal();
	const float OffLine = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Aim, ToEye), -1.0f, 1.0f)));
	const float Facing = ZeroAngle > FullAngle
		? 1.0f - FMath::SmoothStep(FullAngle, ZeroAngle, OffLine)
		: (OffLine <= FullAngle ? 1.0f : 0.0f);

	// A wall between the scope and the eye. The material draws over the gun's own mesh (no depth
	// test, or the star would be cut by the barrel it sits on), so walls are asked about here.
	bool bBlocked = false;
	if (Facing > 0.0f)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(AimGlint), false);
		Params.AddIgnoredActor(Weapon);
		Params.AddIgnoredActor(Holder);
		if (const APawn* const ViewerPawn = Viewer->GetPawn())
		{
			Params.AddIgnoredActor(ViewerPawn);
		}
		FHitResult Hit;
		bBlocked = World->LineTraceSingleByChannel(Hit, Eye, Scope, ECC_Visibility, Params);
	}

	const float Elapsed = World->GetTimeSeconds() - Weapon->AIAimHoldStartTime;
	const float Progress = Weapon->AIAimHoldDuration > KINDA_SMALL_NUMBER
		? FMath::Clamp(Elapsed / Weapon->AIAimHoldDuration, 0.0f, 1.0f) : 1.0f;
	// Squared: it stays small for most of the hold and comes up fast at the end, which is what
	// reads as "now".
	const float Rise = FMath::Lerp(StartIntensityShare, 1.0f, Progress * Progress);
	const float Intensity = bBlocked ? 0.0f : MaxIntensity * Facing * Rise;

	if (Intensity <= 0.01f)
	{
		SetVisibility(false);
		return;
	}

	SetWorldLocation(Scope);
	GlintInstance->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	GlintInstance->SetScalarParameterValue(TEXT("Rotation"), FMath::Frac(Elapsed * SpinDegreesPerSecond / 360.0f));

	const float Size = FMath::Lerp(StartSize, EndSize, Progress);
	if (Elements.Num() > 0 && !FMath::IsNearlyEqual(Elements[0].BaseSizeX, Size, 0.0005f))
	{
		Elements[0].BaseSizeX = Size;
		Elements[0].BaseSizeY = Size;
		MarkRenderStateDirty();
	}
	SetVisibility(true);
}
