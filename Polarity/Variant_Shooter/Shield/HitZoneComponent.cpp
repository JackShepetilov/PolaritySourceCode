// HitZoneComponent.cpp

#include "HitZoneComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Variant_Shooter/DamageTypes/DamageType_Heavy.h"

static TAutoConsoleVariable<int32> CVarDrawHitZones(
	TEXT("polarity.debug.hitzones"),
	0,
	TEXT("1: draw every enemy's hit zone boxes (green whole, red broken); each hit flashes its zone."),
	ECVF_Cheat);

UHitZoneComponent::UHitZoneComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);
}

void UHitZoneComponent::BeginPlay()
{
	Super::BeginPlay();
	BuildZoneCollision();
	if (GetOwner() && GetOwner()->HasAuthority()) { ResetZones(); }
	else { PresentBrokenZones(); }
}

void UHitZoneComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UHitZoneComponent, BrokenMask);
	DOREPLIFETIME(UHitZoneComponent, PoolLeft);
}

void UHitZoneComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (CVarDrawHitZones.GetValueOnGameThread() != 0)
	{
		DrawZones(0.0f);
	}
}

void UHitZoneComponent::ResetZones()
{
	SetZoneCollisionEnabled(true);
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		PoolLeft.SetNum(Zones.Num());
		for (int32 Index = 0; Index < Zones.Num(); ++Index) { PoolLeft[Index] = Zones[Index].Pool; }
		BrokenMask = 0;
		GetOwner()->ForceNetUpdate();
	}
	PresentBrokenZones();
}

void UHitZoneComponent::BuildZoneCollision()
{
	AActor* const OwnerActor = GetOwner();
	USkeletalMeshComponent* const Mesh = GetOwnerMesh();
	if (!bUseZoneCollision || !OwnerActor || !Mesh) { return; }
	for (int32 Index = 0; Index < Zones.Num(); ++Index)
	{
		const FHitZone& Zone = Zones[Index];
		UBoxComponent* const Box = NewObject<UBoxComponent>(OwnerActor,
			FName(*FString::Printf(TEXT("HitZoneCollision_%d"), Index)));
		OwnerActor->AddInstanceComponent(Box);
		Box->SetupAttachment(Mesh, Zone.Bone);
		Box->SetRelativeLocation(Zone.Center);
		Box->SetBoxExtent(Zone.Extent);
		Box->SetCollisionObjectType(ECC_Pawn);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		// Rounds are WorldDynamic. Pawn object queries (hitscan, aim, blasts) find it by type.
		Box->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetGenerateOverlapEvents(true);
		Box->SetCanEverAffectNavigation(false);
		Box->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;
		Box->RegisterComponent();
		ZoneColliders.Add(Box);
	}
}

void UHitZoneComponent::SetZoneCollisionEnabled(bool bEnabled)
{
	for (UBoxComponent* const Box : ZoneColliders)
	{
		if (IsValid(Box))
		{
			Box->SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		}
	}
}

bool UHitZoneComponent::IsHeavyDamage(const FDamageEvent& DamageEvent)
{
	return IsHeavyDamageType(DamageEvent.DamageTypeClass);
}

int32 UHitZoneComponent::FindZone(FName ZoneName) const
{
	if (ZoneName.IsNone())
	{
		return INDEX_NONE;
	}
	return Zones.IndexOfByPredicate([ZoneName](const FHitZone& Zone) { return Zone.Name == ZoneName; });
}

bool UHitZoneComponent::IsZoneBroken(FName ZoneName) const
{
	const int32 Index = FindZone(ZoneName);
	return Index != INDEX_NONE && Index < 32 && (BrokenMask & (1 << Index)) != 0;
}

bool UHitZoneComponent::GetZoneWorldCenter(FName ZoneName, FVector& Out) const
{
	const int32 Index = FindZone(ZoneName);
	if (!Zones.IsValidIndex(Index)) { return false; }
	Out = GetZoneTransform(Zones[Index]).TransformPosition(Zones[Index].Center);
	return true;
}

float UHitZoneComponent::GetZonePoolFraction(FName ZoneName) const
{
	const int32 Index = FindZone(ZoneName);
	if (Index == INDEX_NONE || Zones[Index].Pool <= 0.0f)
	{
		return 1.0f;
	}
	if (IsZoneBroken(ZoneName))
	{
		return 0.0f;
	}
	return PoolLeft.IsValidIndex(Index) ? FMath::Clamp(PoolLeft[Index] / Zones[Index].Pool, 0.0f, 1.0f) : 1.0f;
}

USkeletalMeshComponent* UHitZoneComponent::GetOwnerMesh() const
{
	if (const ACharacter* const Character = Cast<ACharacter>(GetOwner()))
	{
		return Character->GetMesh();
	}
	return GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

FTransform UHitZoneComponent::GetZoneTransform(const FHitZone& Zone) const
{
	if (const USkeletalMeshComponent* const Mesh = GetOwnerMesh())
	{
		if (!Zone.Bone.IsNone() && Mesh->DoesSocketExist(Zone.Bone))
		{
			return Mesh->GetSocketTransform(Zone.Bone, RTS_World);
		}
		return Mesh->GetComponentTransform();
	}
	return GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity;
}

bool UHitZoneComponent::IsHeavyDamageType(const UClass* DamageType)
{
	return DamageType && DamageType->IsChildOf(UDamageType_Heavy::StaticClass());
}

int32 UHitZoneComponent::ResolveZone(const FDamageEvent& DamageEvent, AActor* DamageCauser) const
{
	if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
	{
		// A rocket's direct contact keeps its flight line; splash entries have no such line.
		const FRadialDamageEvent& Blast = static_cast<const FRadialDamageEvent&>(DamageEvent);
		for (const FHitResult& Hit : Blast.ComponentHits)
		{
			if (!(Hit.TraceEnd - Hit.TraceStart).IsNearlyZero()) { return ResolveZoneForHit(Hit, DamageCauser); }
		}
		return FindZone(FallbackZone);
	}
	if (!DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		return FindZone(FallbackZone);
	}
	return ResolveZoneForHit(static_cast<const FPointDamageEvent&>(DamageEvent).HitInfo, DamageCauser);
}

FHitZoneVerdict UHitZoneComponent::PreviewHit(const FHitResult& Hit, const AActor* DamageCauser, bool bHeavy) const
{
	FHitZoneVerdict Verdict;
	Verdict.Zone = ResolveZoneForHit(Hit, DamageCauser);
	if (!Zones.IsValidIndex(Verdict.Zone))
	{
		return Verdict;
	}
	const FHitZone& Zone = Zones[Verdict.Zone];
	const bool bBroken = Verdict.Zone < 32 && (BrokenMask & (1 << Verdict.Zone)) != 0;
	Verdict.bDeflected = Zone.Accepts == EHitZoneAccepts::HeavyOnly && !bHeavy;
	Verdict.bDeadPart = !Verdict.bDeflected && bBroken && Zone.Pool > 0.0f && !Zone.bHealthTakesHits
		&& !(bHeavy && Zone.bHeavyReachesHealth);
	Verdict.Surface = (bBroken && Zone.BrokenSurface != SurfaceType_Default) ? Zone.BrokenSurface.GetValue() : Zone.Surface.GetValue();
	return Verdict;
}

int32 UHitZoneComponent::ResolveZoneForHit(const FHitResult& Hit, const AActor* DamageCauser) const
{
	const int32 Fallback = FindZone(FallbackZone);
	if (!GetOwner())
	{
		return Fallback;
	}

	// A swept sphere really contacted this box. Do not reclassify its contact along a
	// different line through the body, especially where a track meets the hull.
	if (bUseZoneCollision && Hit.GetComponent())
	{
		for (int32 Index = 0; Index < ZoneColliders.Num(); ++Index)
		{
			if (ZoneColliders[Index] == Hit.GetComponent()) { return Index; }
		}
	}

	// The shot's line. The trace that produced the hit when it carries one (hitscan, a projectile's
	// sweep), else from the causer to the impact. It hit the capsule, which on a tank is a sphere
	// around the hull, so the line is carried through the whole body and the boxes decide.
	FVector Direction = Hit.TraceEnd - Hit.TraceStart;
	if (Direction.IsNearlyZero() && DamageCauser)
	{
		Direction = Hit.ImpactPoint - DamageCauser->GetActorLocation();
	}
	if (!Direction.Normalize() || Hit.ImpactPoint.IsZero())
	{
		return Fallback;
	}

	FVector Origin;
	FVector BoxExtent;
	GetOwner()->GetActorBounds(true, Origin, BoxExtent);
	const float Reach = FMath::Max(BoxExtent.Size() * 2.0f, 200.0f);
	const FVector Start = Hit.ImpactPoint - Direction * Reach;
	const FVector End = Hit.ImpactPoint + Direction * Reach;

	int32 Best = INDEX_NONE;
	float BestTime = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Zones.Num(); ++Index)
	{
		const FHitZone& Zone = Zones[Index];
		const FTransform Space = GetZoneTransform(Zone);
		const FVector LocalStart = Space.InverseTransformPosition(Start);
		const FVector LocalEnd = Space.InverseTransformPosition(End);
		const FBox Box(Zone.Center - Zone.Extent, Zone.Center + Zone.Extent);

		FVector HitLocation;
		FVector HitNormal;
		float HitTime = 0.0f;
		if (FMath::LineExtentBoxIntersection(Box, LocalStart, LocalEnd, FVector::ZeroVector, HitLocation, HitNormal, HitTime)
			&& HitTime < BestTime)
		{
			BestTime = HitTime;
			Best = Index;
		}
	}
	return Best != INDEX_NONE ? Best : Fallback;
}

FHitZoneOutcome UHitZoneComponent::ProcessHit(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	FHitZoneOutcome Out;
	Out.BodyDamage = Damage;
	if (!GetOwner() || !GetOwner()->HasAuthority() || Damage <= 0.0f)
	{
		return Out;
	}

	const int32 Index = ResolveZone(DamageEvent, DamageCauser);
	if (!Zones.IsValidIndex(Index))
	{
		return Out;
	}
	if (PoolLeft.Num() != Zones.Num())
	{
		ResetZones();
	}

	const FHitZone& Zone = Zones[Index];
	const bool bHeavy = IsHeavyDamage(DamageEvent);
	const float Scaled = Damage * Zone.Multiplier;
	Out.Zone = Index;
	Out.Name = Zone.Name;
	Out.BodyDamage = Scaled;

	if (Zone.Accepts == EHitZoneAccepts::HeavyOnly && !bHeavy)
	{
		Out.bBlocked = true;
		Out.BodyDamage = 0.0f;
	}
	else if (Zone.Pool > 0.0f)
	{
		const bool bWasBroken = Index < 32 && (BrokenMask & (1 << Index)) != 0;
		if (!bWasBroken)
		{
			Out.bPoolHit = true;
			PoolLeft[Index] -= Scaled;
			AActor* const Attacker = (EventInstigator && EventInstigator->GetPawn()) ? EventInstigator->GetPawn() : DamageCauser;
			OnZonePoolHit.Broadcast(this, Zone.Name, Scaled, Attacker);
			if (PoolLeft[Index] <= 0.0f)
			{
				Out.bBrokeZone = true;
				BreakZone(Index);
			}
		}
		if (!Zone.bHealthTakesHits)
		{
			Out.BodyDamage = (bHeavy && Zone.bHeavyReachesHealth) ? Scaled : 0.0f;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[ZONE_DEBUG] %s hit in %s: %.1f%s -> body %.1f%s%s (pool %.0f%%)"),
		*GetNameSafe(GetOwner()), *Zone.Name.ToString(), Damage, bHeavy ? TEXT(" heavy") : TEXT(""),
		Out.BodyDamage, Out.bBlocked ? TEXT(", REFUSED") : TEXT(""), Out.bBrokeZone ? TEXT(", BROKEN") : TEXT(""),
		GetZonePoolFraction(Zone.Name) * 100.0f);

	if (CVarDrawHitZones.GetValueOnGameThread() != 0)
	{
		const FTransform Space = GetZoneTransform(Zone);
		DrawDebugBox(GetWorld(), Space.TransformPosition(Zone.Center), Zone.Extent * Space.GetScale3D(),
			Space.GetRotation(), Out.bBlocked ? FColor::Yellow : FColor::Orange, false, 0.3f, 0, 3.0f);
	}
	return Out;
}

void UHitZoneComponent::BreakZone(int32 Index)
{
	if (Index < 0 || Index >= 32)
	{
		return;
	}
	BrokenMask |= (1 << Index);
	PresentBrokenZones();
	if (AActor* const Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

void UHitZoneComponent::OnRep_BrokenMask()
{
	PresentBrokenZones();
}

void UHitZoneComponent::PresentBrokenZones()
{
	const int32 NewlyBroken = BrokenMask & ~SeenBrokenMask;
	const int32 Restored = SeenBrokenMask & ~BrokenMask;
	for (int32 Index = 0; Index < BrokenEffects.Num() && Index < 32; ++Index)
	{
		if ((Restored & (1 << Index)) != 0 && IsValid(BrokenEffects[Index]))
		{
			BrokenEffects[Index]->DestroyComponent();
			BrokenEffects[Index] = nullptr;
		}
	}
	BrokenEffects.SetNum(Zones.Num());
	SeenBrokenMask = BrokenMask;
	for (int32 Index = 0; Index < Zones.Num() && Index < 32; ++Index)
	{
		if ((NewlyBroken & (1 << Index)) == 0)
		{
			continue;
		}
		const FHitZone& Zone = Zones[Index];
		const FTransform Space = GetZoneTransform(Zone);
		const FVector Where = Space.TransformPosition(Zone.Center + Zone.BrokenEffectOffset);
		const FRotator Outward = Zone.BrokenEffectOffset.IsNearlyZero() ? FRotator::ZeroRotator
			: Space.TransformVectorNoScale(Zone.BrokenEffectOffset).Rotation();
		if (Zone.BrokenEffect)
		{
			if (USkeletalMeshComponent* const Mesh = GetOwnerMesh())
			{
				// Attached, so a track keeps smoking where it is if the wreck is shoved.
				BrokenEffects[Index] = UNiagaraFunctionLibrary::SpawnSystemAttached(Zone.BrokenEffect, Mesh, Zone.Bone, Where, Outward,
					EAttachLocation::KeepWorldPosition, true);
			}
		}
		if (Zone.BrokenSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, Zone.BrokenSound, Where);
		}
		UE_LOG(LogTemp, Log, TEXT("[ZONE_DEBUG] %s: %s broken"), *GetNameSafe(GetOwner()), *Zone.Name.ToString());
		OnZoneBroken.Broadcast(this, Zone.Name);
	}
}

void UHitZoneComponent::DrawZones(float Duration) const
{
	const UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}
	for (int32 Index = 0; Index < Zones.Num(); ++Index)
	{
		const FHitZone& Zone = Zones[Index];
		const FTransform Space = GetZoneTransform(Zone);
		const bool bBroken = Index < 32 && (BrokenMask & (1 << Index)) != 0;
		const FColor Color = bBroken ? FColor::Red : (Zone.Accepts == EHitZoneAccepts::HeavyOnly ? FColor::Cyan : FColor::Green);
		DrawDebugBox(World, Space.TransformPosition(Zone.Center), Zone.Extent * Space.GetScale3D(), Space.GetRotation(),
			Color, false, Duration, 0, 1.5f);
	}
}
