#include "IslandLandscapeStamp.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapePatchEditLayer.h"
#include "LandscapeTexturePatch.h"
#include "Math/Float16Color.h"
#include "PixelFormat.h"

namespace
{
	float SmoothStep01(float Value)
	{
		const float T = FMath::Clamp(Value, 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	float CubicHermite(float T, float P0, float P1, float M0, float M1)
	{
		const float T2 = T * T;
		const float T3 = T2 * T;
		return (2.f * T3 - 3.f * T2 + 1.f) * P0
			+ (T3 - 2.f * T2 + T) * M0
			+ (-2.f * T3 + 3.f * T2) * P1
			+ (T3 - T2) * M1;
	}
}

AIslandLandscapeStamp::AIslandLandscapeStamp()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	LandscapePatch = CreateDefaultSubobject<ULandscapeTexturePatch>(TEXT("LandscapePatch"));
	LandscapePatch->SetupAttachment(SceneRoot);

	PlateauPreview = CreateDefaultSubobject<UBoxComponent>(TEXT("PlateauPreview"));
	PlateauPreview->SetupAttachment(SceneRoot);
	PlateauPreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlateauPreview->SetHiddenInGame(true);
	PlateauPreview->ShapeColor = FColor(40, 220, 80);

	ArenaAnchorPreview = CreateDefaultSubobject<UArrowComponent>(TEXT("ArenaAnchorPreview"));
	ArenaAnchorPreview->SetupAttachment(SceneRoot);
	ArenaAnchorPreview->SetHiddenInGame(true);
	ArenaAnchorPreview->ArrowColor = FColor(255, 180, 20);
	ArenaAnchorPreview->ArrowSize = 3.f;

#if WITH_EDITORONLY_DATA
	bRunConstructionScriptOnDrag = true;
#endif
}

void AIslandLandscapeStamp::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RegenerateStamp();
}

void AIslandLandscapeStamp::RegenerateStamp()
{
#if WITH_EDITOR
	if (bIsRebuilding || IsTemplate() || !LandscapePatch)
	{
		return;
	}

	TGuardValue<bool> RebuildGuard(bIsRebuilding, true);
	RebuildTextures();
	ConfigurePatch();
	UpdatePreview();
	LandscapePatch->RequestLandscapeUpdate(true);
	Modify();
	UE_LOG(LogTemp, Display, TEXT("IslandLandscapeStamp regenerated: %s Base=%s Size=(%.0f, %.0f) Angle=%.1f Noise=%.0f"),
		*GetActorLabel(), bSeabedBaseStamp ? TEXT("true") : TEXT("false"), SizeX, SizeY, BeachSlopeAngle, NoiseStrength);
#endif
}

void AIslandLandscapeStamp::AssignToLandscape()
{
#if WITH_EDITOR
	if (!TargetLandscape)
	{
		double BestDistanceSq = TNumericLimits<double>::Max();
		for (TActorIterator<ALandscape> It(GetWorld()); It; ++It)
		{
			const double DistanceSq = FVector::DistSquared2D(GetActorLocation(), It->GetActorLocation());
			if (DistanceSq < BestDistanceSq)
			{
				BestDistanceSq = DistanceSq;
				TargetLandscape = *It;
			}
		}
	}

	if (!TargetLandscape || !LandscapePatch)
	{
		UE_LOG(LogTemp, Warning, TEXT("IslandLandscapeStamp %s could not find a Landscape."), *GetActorLabel());
		return;
	}

	if (!TargetLandscape->GetEditLayer(PatchEditLayerName))
	{
		const int32 NewLayerIndex = TargetLandscape->CreateLayer(PatchEditLayerName, ULandscapePatchEditLayer::StaticClass(), true);
		UE_LOG(LogTemp, Display, TEXT("IslandLandscapeStamp created patch edit layer '%s' at index %d."), *PatchEditLayerName.ToString(), NewLayerIndex);
	}

	const bool bAssigned = LandscapePatch->AssignToLandscape(TargetLandscape, PatchEditLayerName);
	if (!bAssigned)
	{
		LandscapePatch->SetLandscape(TargetLandscape);
	}
	LandscapePatch->RequestLandscapeUpdate(true);
	Modify();
	UE_LOG(LogTemp, Display, TEXT("IslandLandscapeStamp %s assigned to %s (layer %s): %s"),
		*GetActorLabel(), *TargetLandscape->GetActorLabel(), *PatchEditLayerName.ToString(), bAssigned ? TEXT("true") : TEXT("fallback"));
#endif
}

float AIslandLandscapeStamp::SampleNoise(const FVector2D& LocalPosition) const
{
	if (NoiseStrength <= KINDA_SMALL_NUMBER || NoiseOctaves <= 0)
	{
		return 0.f;
	}

	float Value = 0.f;
	float Amplitude = 1.f;
	float TotalAmplitude = 0.f;
	float Frequency = 1.f / FMath::Max(NoiseScale, 1.f);
	const FVector2D SeedOffset(NoiseSeed * 17.123f, NoiseSeed * -31.771f);
	for (int32 Octave = 0; Octave < FMath::Clamp(NoiseOctaves, 1, 6); ++Octave)
	{
		Value += FMath::PerlinNoise2D((LocalPosition + SeedOffset) * Frequency) * Amplitude;
		TotalAmplitude += Amplitude;
		Amplitude *= 0.5f;
		Frequency *= 2.f;
	}
	return TotalAmplitude > 0.f ? Value / TotalAmplitude : 0.f;
}

void AIslandLandscapeStamp::RebuildTextures()
{
#if WITH_EDITOR
	const int32 Resolution = FMath::Clamp(TextureResolution, 65, 1025);
	const float RadiusX = FMath::Max(SizeX * 0.5f, 500.f);
	const float RadiusY = FMath::Max(SizeY * 0.5f, 500.f);
	const float UnderwaterLength = FMath::Max(UnderwaterBlendLength, 500.f);
	const float CoverageX = 2.f * (RadiusX + UnderwaterLength + NoiseStrength);
	const float CoverageY = 2.f * (RadiusY + UnderwaterLength + NoiseStrength);
	const float Slope = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(BeachSlopeAngle, 5.f, 60.f)));
	const float ShoreDrop = FMath::Max(PlateauToShoreHeight, 100.f);
	const float RampEnd = ShoreDrop / FMath::Max(Slope, 0.01f);
	const float BlendHalfWidth = FMath::Max(PlateauBlendWidth, 0.f);
	const float BlendStart = FMath::Max(0.f, RampEnd - BlendHalfWidth);
	const float BlendEnd = RampEnd + BlendHalfWidth;

	TArray<FFloat16Color> HeightPixels;
	TArray<FFloat16Color> SandPixels;
	TArray<FFloat16Color> GrassPixels;
	TArray<FFloat16Color> BasePixels;
	const int32 PixelCount = Resolution * Resolution;
	HeightPixels.SetNumUninitialized(PixelCount);
	SandPixels.SetNumUninitialized(PixelCount);
	GrassPixels.SetNumUninitialized(PixelCount);
	BasePixels.SetNumUninitialized(PixelCount);

	if (bSeabedBaseStamp)
	{
		const FFloat16Color FlatHeight(FLinearColor(0.f, 0.f, 0.f, 1.f));
		const FFloat16Color ZeroWeight(FLinearColor(0.f, 0.f, 0.f, 1.f));
		const FFloat16Color FullWeight(FLinearColor(1.f, 0.f, 0.f, 1.f));
		for (int32 Index = 0; Index < PixelCount; ++Index)
		{
			HeightPixels[Index] = FlatHeight;
			SandPixels[Index] = ZeroWeight;
			GrassPixels[Index] = ZeroWeight;
			BasePixels[Index] = FullWeight;
		}
		HeightTexture = CreateFloatTexture(TEXT("IslandSeabedHeight"), HeightPixels, Resolution);
		SandTexture = CreateFloatTexture(TEXT("IslandSeabedSand"), SandPixels, Resolution);
		GrassTexture = CreateFloatTexture(TEXT("IslandSeabedGrass"), GrassPixels, Resolution);
		BaseTexture = CreateFloatTexture(TEXT("IslandSeabedBase"), BasePixels, Resolution);
		LandscapePatch->SetUnscaledCoverage(FVector2D(FMath::Max(SizeX, 1000.f), FMath::Max(SizeY, 1000.f)));
		return;
	}

	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			const float U = static_cast<float>(X) / static_cast<float>(Resolution - 1);
			const float V = static_cast<float>(Y) / static_cast<float>(Resolution - 1);
			const FVector2D Local((U - 0.5f) * CoverageX, (V - 0.5f) * CoverageY);
			const float Normalized = FMath::Sqrt(FMath::Square(Local.X / RadiusX) + FMath::Square(Local.Y / RadiusY));

			float RadialRadius = FMath::Min(RadiusX, RadiusY);
			if (Normalized > KINDA_SMALL_NUMBER)
			{
				const float DirectionX = Local.X / (Normalized * RadiusX);
				const float DirectionY = Local.Y / (Normalized * RadiusY);
				RadialRadius = 1.f / FMath::Sqrt(FMath::Square(DirectionX / RadiusX) + FMath::Square(DirectionY / RadiusY));
			}

			float OutwardDistance = (Normalized - 1.f) * RadialRadius;
			OutwardDistance -= SampleNoise(Local) * NoiseStrength;
			const float InwardDistance = -OutwardDistance;
			float RelativeHeight = -ShoreDrop + Slope * InwardDistance;

			if (InwardDistance >= BlendStart && BlendEnd > BlendStart + KINDA_SMALL_NUMBER)
			{
				const float T = FMath::Clamp((InwardDistance - BlendStart) / (BlendEnd - BlendStart), 0.f, 1.f);
				RelativeHeight = CubicHermite(T, -ShoreDrop + Slope * BlendStart, 0.f,
					Slope * (BlendEnd - BlendStart), 0.f);
			}
			if (InwardDistance >= BlendEnd)
			{
				RelativeHeight = 0.f;
			}

			float Influence = 1.f;
			if (OutwardDistance > 0.f)
			{
				const float T = FMath::Clamp(OutwardDistance / UnderwaterLength, 0.f, 1.f);
				RelativeHeight = CubicHermite(T, -ShoreDrop, -FMath::Max(SeabedDepthBelowPlateau, ShoreDrop + 100.f),
					-Slope * UnderwaterLength, 0.f);
				Influence = 1.f - SmoothStep01(T);
			}

			const float GrassStart = FMath::Max(0.f, GrassInset - GrassBlendWidth * 0.5f);
			const float GrassEnd = GrassInset + GrassBlendWidth * 0.5f;
			const float GrassAmount = SmoothStep01((InwardDistance - GrassStart) / FMath::Max(GrassEnd - GrassStart, 1.f));
			const float SandAmount = 1.f - GrassAmount;

			const int32 Index = Y * Resolution + X;
			HeightPixels[Index] = FFloat16Color(FLinearColor(RelativeHeight, 0.f, 0.f, Influence));
			SandPixels[Index] = FFloat16Color(FLinearColor(SandAmount * Influence, 0.f, 0.f, 1.f));
			GrassPixels[Index] = FFloat16Color(FLinearColor(GrassAmount * Influence, 0.f, 0.f, 1.f));
			BasePixels[Index] = FFloat16Color(FLinearColor(0.f, 0.f, 0.f, 1.f));
		}
	}

	HeightTexture = CreateFloatTexture(TEXT("IslandHeight"), HeightPixels, Resolution);
	SandTexture = CreateFloatTexture(TEXT("IslandSand"), SandPixels, Resolution);
	GrassTexture = CreateFloatTexture(TEXT("IslandGrass"), GrassPixels, Resolution);
	BaseTexture = nullptr;
	LandscapePatch->SetUnscaledCoverage(FVector2D(CoverageX, CoverageY));
#endif
}

UTexture2D* AIslandLandscapeStamp::CreateFloatTexture(const FString& DebugName, const TArray<FFloat16Color>& Pixels, int32 Resolution) const
{
#if WITH_EDITOR
	const FName UniqueName = MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), FName(*DebugName));
	UTexture2D* Texture = UTexture2D::CreateTransient(Resolution, Resolution, PF_FloatRGBA, UniqueName);
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty())
	{
		return nullptr;
	}

	Texture->SRGB = false;
	Texture->NeverStream = true;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* Destination = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Destination, Pixels.GetData(), Pixels.Num() * sizeof(FFloat16Color));
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	return Texture;
#else
	return nullptr;
#endif
}

void AIslandLandscapeStamp::ConfigurePatch()
{
#if WITH_EDITOR
	if (!HeightTexture || !SandTexture || !GrassTexture)
	{
		return;
	}

	LandscapePatch->SetRelativeLocation(FVector::ZeroVector);
	LandscapePatch->SetRelativeRotation(FRotator::ZeroRotator);
	LandscapePatch->SetRelativeScale3D(FVector::OneVector);
	LandscapePatch->SetPriority(PatchPriority);
	LandscapePatch->SetBlendMode(bSeabedBaseStamp ? ELandscapeTexturePatchBlendMode::AlphaBlend : ELandscapeTexturePatchBlendMode::Max);
	LandscapePatch->SetFalloff(0.f);
	LandscapePatch->SetHeightSourceMode(ELandscapeTexturePatchSourceMode::TextureAsset);
	LandscapePatch->SetHeightTextureAsset(HeightTexture);
	LandscapePatch->SetHeightTextureChannel(ELandscapeTexturePatchTextureChannel::Red);
	LandscapePatch->SetHeightAlphaSourceMode(ELandscapeTexturePatchAlphaSourceMode::SourceTextureChannel);
	LandscapePatch->SetHeightAlphaTextureChannel(ELandscapeTexturePatchTextureChannel::Alpha);
	LandscapePatch->ResetHeightEncodingMode(ELandscapeTextureHeightPatchEncoding::WorldUnits);
	LandscapePatch->SetZeroHeightMeaning(ELandscapeTextureHeightPatchZeroHeightMeaning::PatchZ);

	const TArray<FName> ExistingLayers = LandscapePatch->GetAllWeightPatchLayerNames();
	TArray<FName> DesiredLayers = { SandLayerName, GrassLayerName };
	if (bSeabedBaseStamp)
	{
		DesiredLayers.Add(BaseLayerName);
	}
	for (const FName LayerName : DesiredLayers)
	{
		if (!ExistingLayers.Contains(LayerName))
		{
			LandscapePatch->CreateWeightPatch(LayerName, ELandscapeTexturePatchSourceMode::TextureAsset,
				ELandscapeTexturePatchAlphaSourceMode::None);
		}
		LandscapePatch->SetWeightPatchSourceMode(LayerName, ELandscapeTexturePatchSourceMode::TextureAsset);
		LandscapePatch->SetWeightPatchTextureChannel(LayerName, ELandscapeTexturePatchTextureChannel::Red);
		LandscapePatch->SetWeightPatchAlphaSourceMode(LayerName, ELandscapeTexturePatchAlphaSourceMode::None);
		LandscapePatch->SetWeightPatchBlendModeOverride(LayerName, ELandscapeTexturePatchBlendMode::AlphaBlend);
	}
	LandscapePatch->SetWeightPatchTextureAsset(SandLayerName, SandTexture);
	LandscapePatch->SetWeightPatchTextureAsset(GrassLayerName, GrassTexture);
	if (bSeabedBaseStamp && BaseTexture)
	{
		LandscapePatch->SetWeightPatchTextureAsset(BaseLayerName, BaseTexture);
	}

	if (TargetLandscape)
	{
		LandscapePatch->AssignToLandscape(TargetLandscape, PatchEditLayerName);
	}
#endif
}

void AIslandLandscapeStamp::UpdatePreview()
{
	if (bSeabedBaseStamp)
	{
		PlateauPreview->SetBoxExtent(FVector(FMath::Max(SizeX * 0.5f, 100.f), FMath::Max(SizeY * 0.5f, 100.f), 25.f));
		PlateauPreview->SetRelativeLocation(FVector(0.f, 0.f, -25.f));
		ArenaAnchorPreview->SetVisibility(false);
		return;
	}
	ArenaAnchorPreview->SetVisibility(true);
	const float Slope = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(BeachSlopeAngle, 5.f, 60.f)));
	const float RampLength = PlateauToShoreHeight / FMath::Max(Slope, 0.01f);
	PlateauPreview->SetBoxExtent(FVector(
		FMath::Max(100.f, SizeX * 0.5f - RampLength),
		FMath::Max(100.f, SizeY * 0.5f - RampLength),
		50.f));
	PlateauPreview->SetRelativeLocation(FVector(0.f, 0.f, -50.f));
	ArenaAnchorPreview->SetRelativeLocation(ArenaAnchorOffset);
	ArenaAnchorPreview->SetRelativeRotation(FRotator(0.f, ArenaAnchorYaw, 0.f));
}
