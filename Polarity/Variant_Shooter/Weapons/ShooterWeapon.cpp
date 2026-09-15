// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterWeapon.h"
#include "AI/PolarityTeams.h"
#include "Coop/CoopPlayers.h"
#include "PolarityPalette.h"
#include "Variant_Shooter/Inventory/InventoryComponent.h"
#if WITH_EDITOR
// Icon capture only. See GenerateIconFromMesh at the bottom of this file.
#include "Editor.h"
#include "Animation/SkeletalMeshActor.h"
#include "Engine/SceneCapture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInterface.h"
#include "TextureResource.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#endif
#include "PolarityCharacter.h"
#include "ApexMovementComponent.h"
#include "Variant_Shooter/AI/NPCRiotShieldComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "ShooterProjectile.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/SphereComponent.h"
#include "EMFProjectile.h"
#include "ProjectilePoolSubsystem.h"
#include "ShooterWeaponHolder.h"
#include "EMF_FieldComponent.h"
#include "EMFVelocityModifier.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "Camera/CameraComponent.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

// polarity.debug.novfx - глушилка боевых эффектов для отладки. Объявлена здесь, а не в отдельном
// заголовке, потому что новый файл потребовал бы полной пересборки; остальные места читают её через
// IConsoleManager::FindConsoleVariable по имени.
static TAutoConsoleVariable<int32> CVarNoVFX(
	TEXT("polarity.debug.novfx"),
	0,
	TEXT("1 - не спавнить боевые эффекты (попадания, вспышки). Для наблюдения за ИИ."),
	ECVF_Cheat);
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimationAsset.h"
#include "Engine/DataAsset.h"
#include "Curves/CurveVector.h"
#include "Sound/SoundBase.h"
#include "UObject/UnrealType.h"
// PRAS, the recoil half of the FPS Animation Pack. Only the data asset is needed here; the
// component that plays it lives on the character.
#include "RecoilData.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Engine/DamageEvents.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "DrawDebugHelpers.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundAttenuation.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/AI/SniperTurretNPC.h"
#include "Variant_Shooter/AI/Boss/BossCharacter.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/HitMarkerComponent.h"
#include "TutorialSubsystem.h"
#include "Variant_Shooter/ShooterDummy.h"
#include "EMFPhysicsProp.h"
#include "Foliage/FoliageConversionLibrary.h"
#include "Upgrades/UpgradeManagerComponent.h"
#include "Variant_Shooter/Abilities/AbilityComponent.h"
#include "EnemyBeamBoltSubsystem.h"
#include "VFX/VFXVariantSequenceSubsystem.h"

void AShooterWeapon::PlayFireEffectsLocally(bool bLastRound)
{
	SpawnMuzzleFlashEffect();
	PlayFireSound();

	// The gun's own moving parts. Here rather than in Fire() because this function is what every
	// machine runs -- the shooter directly and everybody else through Multicast_PlayFireEffects --
	// so the action of the weapon is seen by the people watching it too, not only by its owner.
	//
	// The shot that empties the magazine gets its own animation on a weapon that has one, because
	// the ordinary one closes the action again and the gun would then stand there looking loaded
	// with nothing in it. Missing asset falls back to the ordinary one, so this changes nothing for
	// a weapon whose slide does not lock back.
	UAnimationAsset* const FireAnimation = (bLastRound && WeaponMeshLastShotAnimation)
		? WeaponMeshLastShotAnimation
		: WeaponMeshFireAnimation;

	PlayWeaponMeshAnimation(FireAnimation);
}

void AShooterWeapon::PlayWeaponMeshAnimation(UAnimationAsset* Animation)
{
	if (!Animation)
	{
		return;
	}

	USkeletalMeshComponent* Meshes[] = { FirstPersonMesh, ThirdPersonMesh };
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (!Mesh || !Mesh->GetSkeletalMeshAsset())
		{
			continue;
		}

		// A weapon that runs an anim blueprint of its own keeps it: play the montage through the
		// instance so the graph can blend it and its notifies still fire. PlayAnimation would throw
		// the graph away for a single-node player and the weapon would freeze in that pose.
		if (UAnimMontage* AsMontage = Cast<UAnimMontage>(Animation))
		{
			if (UAnimInstance* MeshAnimInstance = Mesh->GetAnimInstance())
			{
				MeshAnimInstance->Montage_Play(AsMontage);
				continue;
			}
		}

		// No graph, or a plain sequence: play it straight on the component. This is the usual case
		// for a weapon mesh, which has nothing else to animate it.
		Mesh->PlayAnimation(Animation, /*bLooping*/ false);
	}
}

void AShooterWeapon::ResolveADSAnchorAttachment()
{
	if (!ADSCameraComponent || !FirstPersonMesh)
	{
		return;
	}

	const FAttachmentTransformRules Rules = FAttachmentTransformRules::SnapToTargetNotIncludingScale;

	// --- 0. The optic this weapon has actually been given ---
	// Checked before the generic search, and that order is the whole point. A weapon whose default
	// sight is its own Blueprint component ALSO answers to SOCKET_Aim, so once a real optic is
	// mounted the subtree holds two candidates and only child order separates them -- an order
	// nobody chose and which changes with how the components were made. Naming the mounted one here
	// makes the answer deterministic.
	if (MountedOpticMesh && MountedOpticMesh->DoesSocketExist(SightAimSocketName))
	{
		ADSCameraComponent->AttachToComponent(MountedOpticMesh, Rules, SightAimSocketName);
		UE_LOG(LogTemp, Log, TEXT("[ADS] %s: anchor on the mounted optic '%s', socket '%s'."),
			*GetName(), *MountedOpticMesh->GetName(), *SightAimSocketName.ToString());
		return;
	}

	// A real optic is fitted but its mesh has no eye point. Worth saying out loud: the scope will be
	// drawn and the player will aim down the weapon's own iron sights through it.
	if (GetAttachmentOfType(EWeaponAttachmentType::Optic) && !MountedOpticMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ADS] %s: an optic is mounted but has no mesh on the first "
			"person weapon, so the aim falls back to the weapon's own sights."), *GetName());
	}

	// While a real optic is on, the built-in sight is hidden and must not answer the search below
	// either, or the anchor would land on a scope the player cannot see.
	const USceneComponent* SkipComponent = (GetAttachmentOfType(EWeaponAttachmentType::Optic) != nullptr)
		? DefaultOpticComponent.Get()
		: nullptr;

	// --- 1. A sight attachment that carries its own eye point ---
	// The best of the three by a distance, and the only one authored FOR aiming: the socket sits
	// behind the glass, oriented down the sight line, and it travels with the scope when the scope
	// is swapped, so nothing needs re-tuning per attachment. This is the Low Poly Shooter Pack
	// convention (SOCKET_Aim on SM_*_Scope_Default and on every SM_ATT_Scope_*), and iron sights
	// there are simply "scope number zero" rather than a separate case.
	//
	// The whole subtree is searched, not just direct children, so a sight mounted on a rail that is
	// itself mounted on the weapon still counts.
	if (!SightAimSocketName.IsNone())
	{
		// Not "Children": AActor already has a member by that name and warnings are errors here.
		TArray<USceneComponent*> AttachedChildren;
		FirstPersonMesh->GetChildrenComponents(/*bIncludeAllDescendants*/ true, AttachedChildren);
		for (USceneComponent* Child : AttachedChildren)
		{
			if (!Child || Child == ADSCameraComponent || (SkipComponent && Child == SkipComponent))
			{
				continue;
			}

			if (Child->DoesSocketExist(SightAimSocketName))
			{
				ADSCameraComponent->AttachToComponent(Child, Rules, SightAimSocketName);
				UE_LOG(LogTemp, Log, TEXT("[ADS] %s: anchor on sight attachment '%s', socket '%s'."),
					*GetName(), *Child->GetName(), *SightAimSocketName.ToString());
				return;
			}
		}
	}

	// --- 2. A socket on the weapon mesh itself ---
	// ADSSocketName first: that one is meant to BE the eye point, so if it exists it is as good as
	// case 1. ScopeMountSocketName is the consolation prize and it is worth being clear about why:
	// a mount socket sits on the rail, below the sight line and oriented to the rail rather than
	// down the barrel. It puts the anchor in roughly the right place and almost certainly the wrong
	// rotation, so expect to need SightRotationOffset, or to turn bAlignSightRotation off, on any
	// weapon that lands here.
	// PackAimSocketName sits between the two on purpose. It is an eye point exactly like
	// ADSSocketName, so it belongs above the mount; and it is second rather than first so that a
	// weapon carrying BOTH names still answers to the one our own artists placed.
	const FName Candidates[] = { ADSSocketName, PackAimSocketName, ScopeMountSocketName };
	for (const FName& SocketName : Candidates)
	{
		if (SocketName.IsNone() || !FirstPersonMesh->DoesSocketExist(SocketName))
		{
			continue;
		}

		ADSCameraComponent->AttachToComponent(FirstPersonMesh, Rules, SocketName);

		if (SocketName == ScopeMountSocketName)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ADS] %s: no eye-point socket found, falling back to the "
				"MOUNT socket '%s' on the weapon mesh. That is a rail position, not a sight line: "
				"check the aim with a trace and expect to need SightRotationOffset."),
				*GetName(), *SocketName.ToString());
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[ADS] %s: anchor on weapon mesh socket '%s'."),
				*GetName(), *SocketName.ToString());
		}
		return;
	}

	// --- 3. Nothing authored: keep whatever the Blueprint set ---
	// Deliberately no attach call. The ADS camera keeps the parent and relative transform it was
	// given in the Blueprint, which for a hand-placed anchor is exactly what the designer meant.
	UE_LOG(LogTemp, Log, TEXT("[ADS] %s: no sight socket anywhere, keeping the anchor where the "
		"Blueprint placed it (parent '%s')."),
		*GetName(), *GetNameSafe(ADSCameraComponent->GetAttachParent()));
}

void AShooterWeapon::PropagateRenderVisibilityToChildren()
{
	// A sight, suppressor or laser added in the Blueprint as a child of one of the weapon meshes
	// keeps the default render visibility, which means the first person pass and the world pass
	// disagree about it: bolted to the FP gun it would still be drawn with the WORLD field of view
	// and world depth, so it swims against the weapon it is attached to and clips into the scene.
	// Nothing attached under a weapon mesh ever wants to differ from that mesh here, so it is
	// inherited rather than left as a checkbox to remember on every new attachment.
	auto Inherit = [](USkeletalMeshComponent* Parent)
	{
		if (!Parent)
		{
			return;
		}

		TArray<USceneComponent*> AttachedChildren;
		Parent->GetChildrenComponents(/*bIncludeAllDescendants*/ true, AttachedChildren);
		for (USceneComponent* Child : AttachedChildren)
		{
			UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Child);
			if (!Prim || Prim == Parent)
			{
				continue;
			}

			// Type first: SetFirstPersonPrimitiveType(WorldSpaceRepresentation) forces bOwnerNoSee
			// on internally, so setting the see flags afterwards is what makes ours the last word.
			Prim->SetFirstPersonPrimitiveType(Parent->FirstPersonPrimitiveType);
			Prim->SetOnlyOwnerSee(Parent->bOnlyOwnerSee != 0);
			Prim->SetOwnerNoSee(Parent->bOwnerNoSee != 0);
		}
	};

	Inherit(FirstPersonMesh);
	Inherit(ThirdPersonMesh);
}

void AShooterWeapon::ApplyChildComponentSetup()
{
	PropagateRenderVisibilityToChildren();

	// Force every component attached to the weapon's meshes (sights, suppressors, lasers,
	// rails, etc.) to tick AFTER the mesh's animation has been evaluated AND in a later tick
	// group, so they cannot read stale bone/socket transforms and lag a frame behind the weapon.
	auto ForceLateTickOnChildren = [](USkeletalMeshComponent* Parent)
	{
		if (!Parent) return;

		TArray<USceneComponent*> AttachedChildren;
		Parent->GetChildrenComponents(/*bIncludeAllDescendants*/ true, AttachedChildren);
		for (USceneComponent* Child : AttachedChildren)
		{
			if (!Child || Child == Parent) continue;

			// 1. Hard prerequisite — child can never tick before the parent.
			Child->AddTickPrerequisiteComponent(Parent);

			// 2. Push tick into TG_PostPhysics so it runs after PrePhysics anim work AND
			//    any DuringPhysics simulation. If the component doesn't tick at all this is
			//    harmless; if it does (animated, particle, dynamic), it picks up the latest pose.
			if (Child->PrimaryComponentTick.bCanEverTick)
			{
				Child->PrimaryComponentTick.TickGroup = TG_PostPhysics;
			}
		}
	};
	ForceLateTickOnChildren(FirstPersonMesh);
	ForceLateTickOnChildren(ThirdPersonMesh);
}

// ==================== Attachments ====================
//
// The weapon is the record of what is fitted. The inventory only knows about the CELL an
// attachment costs once the free slots are spent, and that cell points at this weapon rather than
// holding a second copy of the attachment, so the two cannot drift apart.
//
// Every machine builds its own meshes from the replicated array. Nothing about a mounted part
// travels as an RPC: the array is the message.

void AShooterWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// To everybody, not owner-only: what is bolted to this gun is precisely what the other three
	// players see on it in third person.
	DOREPLIFETIME(AShooterWeapon, InstalledAttachments);

	// The owning client presses the key and resolves it against its replicated copy of OwnedWeapons,
	// so the slot has to travel with the weapon or number keys do nothing for a client.
	DOREPLIFETIME(AShooterWeapon, HotkeySlot);

	// Owner only: the reserve is read by the reload and by the HUD, and both live on the machine of
	// whoever is holding the gun. Nobody else has a reason to know it.
	DOREPLIFETIME_CONDITION(AShooterWeapon, EnergyReserve, COND_OwnerOnly);
}

UWeaponAttachmentDefinition* AShooterWeapon::GetAttachmentOfType(EWeaponAttachmentType InType) const
{
	for (const TObjectPtr<UWeaponAttachmentDefinition>& Def : InstalledAttachments)
	{
		if (Def && Def->Type == InType)
		{
			return Def;
		}
	}
	return nullptr;
}

bool AShooterWeapon::InstallAttachment(UWeaponAttachmentDefinition* Attachment)
{
	if (!HasAuthority() || !Attachment)
	{
		return false;
	}

	// One of each type. Replacing is done by taking the old one off first, on purpose: the cell the
	// old attachment was paying for has to be settled before the new one can claim anything, and a
	// silent swap here would leave the inventory holding a cell for a part that is no longer on the
	// gun.
	if (GetAttachmentOfType(Attachment->Type))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ATTACH] %s already carries a %s attachment. Take it off first."),
			*GetName(), *UEnum::GetValueAsString(Attachment->Type));
		return false;
	}

	// The attachment says which guns it fits, and an empty list fits none. Refused here rather than
	// only greyed out in the inventory screen, because this is the one door every mount goes through.
	if (!Attachment->FitsWeapon(GetClass()))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ATTACH] %s does not fit %s: the weapon is not on its list "
			"(CompatibleWeapons, or MagazineSizeByWeapon for a magazine)."),
			*GetNameSafe(Attachment), *GetClass()->GetName());
		return false;
	}

	InstalledAttachments.Add(Attachment);

	// The server does not get OnRep, so it does its own rebuild. Both paths end in the same call,
	// which is what keeps the host's gun and a client's copy of it identical.
	RebuildAttachmentMeshes();
	ApplyMagazineModifiers();

	UE_LOG(LogTemp, Log, TEXT("[ATTACH] %s: mounted %s (%s)."),
		*GetName(), *GetNameSafe(Attachment), *UEnum::GetValueAsString(Attachment->Type));
	return true;
}

UWeaponAttachmentDefinition* AShooterWeapon::UninstallAttachmentOfType(EWeaponAttachmentType InType)
{
	if (!HasAuthority())
	{
		return nullptr;
	}

	UWeaponAttachmentDefinition* Removed = GetAttachmentOfType(InType);
	if (!Removed)
	{
		return nullptr;
	}

	InstalledAttachments.Remove(Removed);
	RebuildAttachmentMeshes();
	ApplyMagazineModifiers();

	UE_LOG(LogTemp, Log, TEXT("[ATTACH] %s: removed %s."), *GetName(), *GetNameSafe(Removed));
	return Removed;
}

void AShooterWeapon::OnRep_InstalledAttachments()
{
	RebuildAttachmentMeshes();
	ApplyMagazineModifiers();

	// The HUD redraws from the inventory's delegate, and the two halves of a mount arrive as two
	// separate replicated properties -- this array, and the cell that is or is not paying for it.
	// Their order is not guaranteed, so whichever lands second has to ask for the redraw or the
	// screen keeps whatever the first one left behind. @see Source/CLAUDE.md, phase 1 rule.
	if (const AShooterCharacter* Character = Cast<AShooterCharacter>(PawnOwner))
	{
		if (UInventoryComponent* Inventory = Character->GetInventoryComponent())
		{
			Inventory->OnInventoryChanged.Broadcast();
		}
	}
}

FName AShooterWeapon::ResolveAttachmentSocket(EWeaponAttachmentType InType, const USkeletalMeshComponent* Mesh) const
{
	if (!Mesh)
	{
		return NAME_None;
	}

	FName Base = AttachmentSockets.FindRef(InType);

	// The optic rail already had a name before attachments existed: ADS falls back to it as an aim
	// anchor. Reusing it here rather than requiring a duplicate entry keeps one name for one rail.
	if (Base.IsNone() && InType == EWeaponAttachmentType::Optic)
	{
		Base = ScopeMountSocketName;
	}

	if (Base.IsNone())
	{
		return NAME_None;
	}

	// Third person takes the _TP variant when the artist authored one, exactly as the grip does
	// (see PickThirdPersonSocket). Without a _TP socket both meshes use the same name, which is the
	// common case and needs no extra work from anybody.
	if (Mesh == ThirdPersonMesh)
	{
		const FName ThirdPersonVariant(*(Base.ToString() + TEXT("_TP")));
		if (Mesh->DoesSocketExist(ThirdPersonVariant))
		{
			return ThirdPersonVariant;
		}
	}

	// A missing socket is NOT a reason to fall back to the component origin: that mounts the part
	// inside the receiver, which reads as a broken mesh rather than as a missing socket.
	return Mesh->DoesSocketExist(Base) ? Base : NAME_None;
}

void AShooterWeapon::RebuildAttachmentMeshes()
{
	// Destroy exactly what a previous rebuild made. A sight or laser a Blueprint attached by hand
	// is not in this array and is deliberately left where it is.
	for (UStaticMeshComponent* Old : AttachmentMeshComponents)
	{
		if (Old)
		{
			Old->DestroyComponent();
		}
	}
	AttachmentMeshComponents.Reset();
	MountedOpticMesh = nullptr;

	auto MountOn = [this](UWeaponAttachmentDefinition* Def, USkeletalMeshComponent* Parent) -> UStaticMeshComponent*
	{
		if (!Def || !Def->Mesh || !Parent || !Parent->GetSkeletalMeshAsset())
		{
			return nullptr;
		}

		const FName Socket = ResolveAttachmentSocket(Def->Type, Parent);
		if (Socket.IsNone())
		{
			UE_LOG(LogTemp, Error, TEXT("[ATTACH] %s: no mount socket for %s on mesh '%s'. The part "
				"is NOT mounted. Author the socket, or clear that type from AttachmentSockets so "
				"this weapon refuses it up front."),
				*GetName(), *UEnum::GetValueAsString(Def->Type), *Parent->GetName());
			return nullptr;
		}

		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
		if (!Comp)
		{
			return nullptr;
		}

		Comp->SetStaticMesh(Def->Mesh);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetupAttachment(Parent, Socket);
		Comp->RegisterComponent();

		// Rotation and offset from the asset, scale from the asset's own field: MountOffset is a
		// nudge along the rail, and letting it carry a scale as well would give two places to set
		// the same thing.
		Comp->SetRelativeTransform(FTransform(Def->MountOffset.GetRotation(),
			Def->MountOffset.GetLocation(), Def->MeshScale));

		AttachmentMeshComponents.Add(Comp);
		return Comp;
	};

	for (const TObjectPtr<UWeaponAttachmentDefinition>& Def : InstalledAttachments)
	{
		UStaticMeshComponent* FirstPersonPart = MountOn(Def, FirstPersonMesh);
		MountOn(Def, ThirdPersonMesh);

		// Remembered rather than searched for later. The eye point is found by walking the subtree
		// for SOCKET_Aim, and on a weapon that carries a built-in scope component there would be
		// two matches with nothing but child order between them.
		if (Def && Def->Type == EWeaponAttachmentType::Optic && FirstPersonPart)
		{
			MountedOpticMesh = FirstPersonPart;
		}
	}

	// A real optic replaces the built-in one rather than sitting next to it.
	if (DefaultOpticComponent)
	{
		const bool bHasRealOptic = GetAttachmentOfType(EWeaponAttachmentType::Optic) != nullptr;
		DefaultOpticComponent->SetVisibility(!bHasRealOptic, /*bPropagateToChildren*/ true);
	}

	// The eye point may have just moved, and the new components start with default render
	// visibility and tick order, which would draw a first person scope in the world pass.
	ResolveADSAnchorAttachment();
	ApplyChildComponentSetup();
}

void AShooterWeapon::PlayReloadEffectsLocally(EWeaponReloadStage Stage)
{
	// Both meshes: PlayWeaponMeshAnimation already covers first and third person, so the machine
	// that runs this shows the reload on whichever copy of the weapon it can see.
	//
	// Fallbacks rather than a table, because an unfilled slot has to mean "use the one this weapon
	// always used" and not "play nothing": that is what keeps every weapon written before the pack
	// reloading exactly as it did.
	UAnimationAsset* WeaponAnimation = WeaponMeshReloadAnimation;

	switch (Stage)
	{
	case EWeaponReloadStage::Secondary:
	case EWeaponReloadStage::ShellLoop:
		if (WeaponMeshSecondaryReloadAnimation)
		{
			WeaponAnimation = WeaponMeshSecondaryReloadAnimation;
		}
		break;

	case EWeaponReloadStage::ShellEnd:
		if (WeaponMeshReloadEndAnimation)
		{
			WeaponAnimation = WeaponMeshReloadEndAnimation;
		}
		break;

	default:
		break;
	}

	PlayWeaponMeshAnimation(WeaponAnimation);

	// One sound per reload, not one per shell: the loop stage runs once for every round going in,
	// and firing the magazine cue eight times over is a rattle rather than a reload. The shells
	// themselves are in the animation.
	if (ReloadSound && Stage != EWeaponReloadStage::ShellLoop && Stage != EWeaponReloadStage::ShellEnd)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ReloadSound, GetActorLocation());
	}
}

void AShooterWeapon::Multicast_PlayReloadEffects_Implementation(EWeaponReloadStage Stage)
{
	// Whoever started the reload already played these the moment they started it. For an NPC that
	// is the server, which is where its AI lives.
	const bool bIsReloader = PawnOwner && PawnOwner->IsLocallyControlled();
	if (!bIsReloader)
	{
		PlayReloadEffectsLocally(Stage);
	}
}

void AShooterWeapon::PlayReloadStage(EWeaponReloadStage Stage)
{
	// The arms. Which montage a stage means is decided here and nowhere else, so the two halves of
	// a stage can never disagree about which stage they are in.
	UAnimMontage* ArmsMontage = nullptr;

	switch (Stage)
	{
	case EWeaponReloadStage::Primary:
	case EWeaponReloadStage::ShellStart:
		ArmsMontage = ReloadMontage;
		break;

	case EWeaponReloadStage::Secondary:
	case EWeaponReloadStage::ShellLoop:
		ArmsMontage = SecondaryReloadMontage ? SecondaryReloadMontage : ReloadMontage;
		break;

	case EWeaponReloadStage::ShellEnd:
		ArmsMontage = ReloadEndMontage;
		break;
	}

	if (ArmsMontage && WeaponOwner)
	{
		WeaponOwner->PlayReloadMontage(ArmsMontage);
	}

	// The gun's own moving parts, locally first so the reloading player waits for nothing, then to
	// everyone else, whose only copy of this weapon is the third person mesh.
	PlayReloadEffectsLocally(Stage);

	if (HasAuthority())
	{
		Multicast_PlayReloadEffects(Stage);

		// The host's own reload. A client's arrives in AShooterCharacter::Server_ReportWeaponReloaded
		// and pauses there. The reload length is only an estimate on that side, which is fine: the
		// draw at the end of the reload pauses the refill again anyway.
		PauseEnergyRegen(GetActiveReloadTime());
	}
	else if (AShooterCharacter* OwnerCharacter = Cast<AShooterCharacter>(PawnOwner))
	{
		// A client reloads on its own copy of the weapon (ammo is counted by whoever pulls the
		// trigger), so the server has to be told before it can show anyone else. The stage travels
		// with it rather than being recomputed there: the server's copy of the ammo count is not
		// necessarily the one this decision was made from.
		OwnerCharacter->Server_ReportWeaponReloaded(this, Stage);
	}
}

float AShooterWeapon::GetActiveReloadTime() const
{
	// UsesSecondaryReload asks "is there a round in the chamber", and on a per round weapon the
	// secondary slot holds the LOOP instead, so that question does not apply and its override must
	// not be consulted either. What this function answers for such a weapon is the length of the
	// opening stage, which is all that is scheduled when the reload starts.
	const bool bSecondary = !bPerRoundReload && UsesSecondaryReload() && SecondaryReloadMontage != nullptr;

	// An explicit number wins: some weapons want the magazine to land earlier than the animation
	// ends, and that is a deliberate feel decision rather than a mistake.
	const float Override = bSecondary ? SecondaryReloadTime : 0.0f;
	if (Override > 0.0f)
	{
		return Override;
	}

	// Otherwise measure the montage that will actually play. This is the point of the whole field:
	// a duration typed by hand drifts away from the animation the first time an animator retimes it,
	// and the weapon then fires out of a reload that is still running.
	if (const UAnimMontage* Montage = GetActiveReloadMontage())
	{
		const float Length = Montage->GetPlayLength();
		if (Length > 0.0f)
		{
			return Length;
		}
	}

	return ReloadTime;
}

void AShooterWeapon::Multicast_PlayFireEffects_Implementation(bool bLastRound)
{
	// The shooter already played these locally the moment they pulled the trigger.
	const bool bIsShooter = PawnOwner && PawnOwner->IsLocallyControlled();
	if (!bIsShooter)
	{
		PlayFireEffectsLocally(bLastRound);
	}
}

void AShooterWeapon::Multicast_SpawnCosmeticProjectile_Implementation(
	FVector_NetQuantize100 MuzzleLocation, FVector_NetQuantizeNormal Direction)
{
	// The authority is already holding the round this is a picture of. On a listen server that is
	// the host's own screen, and it draws the real one.
	if (HasAuthority())
	{
		return;
	}

	// The shooter fired its own stand-in the instant it pulled the trigger, without waiting for this
	// to come back. Same guard, and for the same reason, as the muzzle flash above.
	if (PawnOwner && PawnOwner->IsLocallyControlled())
	{
		return;
	}

	if (!ProjectileClass || !GetWorld())
	{
		return;
	}

	const FVector Dir = FVector(Direction).GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return;
	}

	SpawnProjectileAtTransform(FTransform(Dir.Rotation(), FVector(MuzzleLocation), FVector::OneVector),
		/*ChargeMultiplier*/ 1.0f, /*bCosmeticOnly*/ true);
}

void AShooterWeapon::PrewarmProjectilePool()
{
	if (!ProjectileClass || bUseHitscan)
	{
		return;
	}

	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	// A replicating class is never pooled (see SpawnProjectileAtTransform), so warming it would
	// build a pool nothing ever draws from.
	const AShooterProjectile* const ClassCDO = ProjectileClass->GetDefaultObject<AShooterProjectile>();
	if (!ClassCDO || ClassCDO->GetIsReplicated())
	{
		return;
	}

	if (UProjectilePoolSubsystem* Pool = World->GetSubsystem<UProjectilePoolSubsystem>())
	{
		// Idempotent: every weapon of a class asks, and the first one to ask is the one that pays.
		const int32 Existing = Pool->GetPoolSize(ProjectileClass);
		const int32 Wanted = ClassCDO->GetDefaultPoolSize();
		if (Existing < Wanted)
		{
			Pool->PrewarmPool(ProjectileClass, Wanted - Existing);
		}
	}
}

const AShooterProjectile* AShooterWeapon::GetShotPayload() const
{
	// A weapon set to hitscan puts no round in the air, and its ProjectileClass may still be filled
	// in from the other half of its configuration -- the shotgun fires either way and its blueprint
	// keeps both. Reading a rocket's overrides onto a trace is exactly the drift this gate exists to
	// stop, and it is the only place the question is asked.
	if (bUseHitscan || !ProjectileClass)
	{
		return nullptr;
	}

	return ProjectileClass->GetDefaultObject<AShooterProjectile>();
}

float AShooterWeapon::GetShotDamage() const
{
	// The projectile class gets the last word, and only if it asked for one. A negative override is
	// the ordinary case and means "the gun decides", so balance stays in one field per weapon.
	if (const AShooterProjectile* const Payload = GetShotPayload())
	{
		const float Override = Payload->GetDirectHitDamageOverride();
		if (Override >= 0.0f)
		{
			return Override;
		}
	}

	return HitscanDamage;
}

float AShooterWeapon::GetShotHeadshotMultiplier() const
{
	if (const AShooterProjectile* const Payload = GetShotPayload())
	{
		const float Override = Payload->GetHeadshotMultiplierOverride();
		if (Override >= 0.0f)
		{
			return Override;
		}
	}

	return HeadshotMultiplier;
}

bool AShooterWeapon::GetShotIonization(float& OutChargePerHit) const
{
	OutChargePerHit = IonizationChargePerHit;

	if (const AShooterProjectile* const Payload = GetShotPayload())
	{
		switch (Payload->GetIonizationOverride())
		{
		case EProjectileIonization::Never:
			OutChargePerHit = 0.0f;
			return false;

		case EProjectileIonization::Override:
			OutChargePerHit = Payload->GetIonizationChargeOverride();
			// The round carries the charge, so the gun's own checkbox is not consulted: that is the
			// whole point of an ionizing payload in a launcher whose other rounds are inert.
			return true;

		case EProjectileIonization::FromWeapon:
		default:
			break;
		}
	}

	if (!bUseHitscanIonization)
	{
		OutChargePerHit = 0.0f;
		return false;
	}

	return true;
}

bool AShooterWeapon::DoesShotIonize() const
{
	float Unused = 0.0f;
	return GetShotIonization(Unused);
}

FName AShooterWeapon::ResolveHitBone(const AActor* Target, const FVector& Start, const FVector& End) const
{
	const ACharacter* const AsCharacter = Cast<ACharacter>(Target);
	USkeletalMeshComponent* const Mesh = AsCharacter ? AsCharacter->GetMesh() : nullptr;
	if (!Mesh)
	{
		return NAME_None;
	}

	// The component directly, not a channel trace. CharacterMesh ignores ECC_Visibility, so a normal
	// trace looks straight through the body; and going by object type just finds the capsule again,
	// which is the shape that started this.
	FHitResult MeshHit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ResolveHitBone), /*bTraceComplex*/ false);
	if (Mesh->LineTraceComponent(MeshHit, Start, End, Params))
	{
		return MeshHit.BoneName;
	}

	return NAME_None;
}

float AShooterWeapon::GetShotDamageMultiplierAgainst(AActor* Target) const
{
	const float HeatMult = bUseHeatSystem ? CalculateHeatDamageMultiplier() : 1.0f;

	float ZFactorMult = 1.0f;
	if (bUseZFactor && PawnOwner)
	{
		ZFactorMult = CalculateZFactorMultiplier(PawnOwner->GetActorLocation().Z,
			IsValid(Target) ? Target->GetActorLocation().Z : PawnOwner->GetActorLocation().Z);
	}

	const float TagMult = GetTagDamageMultiplier(Target);

	float UpgradeMult = 1.0f;
	if (PawnOwner)
	{
		if (UUpgradeManagerComponent* UpgradeMgr = PawnOwner->FindComponentByClass<UUpgradeManagerComponent>())
		{
			UpgradeMult = UpgradeMgr->GetCombinedDamageMultiplier(Target);
		}
	}

	return HeatMult * ZFactorMult * TagMult * UpgradeMult;
}

float AShooterWeapon::GetMaxReportedSingleHitDamage() const
{
	// Projectile weapons report through the projectile, which the server owns, so the hitscan number
	// is the only one a client ever hands over. A weapon with no hitscan damage configured still
	// needs a non-zero ceiling or every reported hit would clamp to nothing.
	// No class-passive term here on purpose. The Sniper's passive does not scale this number at all:
	// it deals its OWN damage, decided and applied on the server, and never travels in a client's
	// report. @see AShooterWeapon::ApplyPassivePierceDamage.
	const float ShotDamage = GetShotDamage();
	const float BaseDamage = ShotDamage > 0.0f ? ShotDamage : 1.0f;
	return BaseDamage * FMath::Max(GetShotHeadshotMultiplier(), 1.0f) * FMath::Max(MaxReportedDamageMultiplier, 1.0f);
}

float AShooterWeapon::PredictDamageAgainst(AActor* Target) const
{
	// GetShotDamage, not HitscanDamage. A projectile weapon leaves the hitscan field at zero, and
	// this used to answer a flat zero for every one of them: the readout said the gun did nothing.
	const float ShotDamage = GetShotDamage();
	if (!IsValid(Target) || ShotDamage <= 0.0f)
	{
		return 0.0f;
	}

	// The same product every real shot assembles, out of the same function, so the readout cannot
	// drift away from what the gun does. Missing from it are the two factors only a real shot can
	// know: whether it lands on a head, and how much energy is left after passing through anything.
	// So this is a body shot at full energy -- the honest baseline, and the one the player can
	// compare two of against each other, which is the whole point of showing it.
	float Total = ShotDamage * GetShotDamageMultiplierAgainst(Target);

	// The shield gate IS applied, unlike an earlier version of this: the readout answers "what will
	// this shot do to that enemy", and a finisher weapon against an intact shield does nothing at
	// all with its own damage. Saying otherwise would be a lie the player then has to unlearn.
	if (bRequiresBrokenShieldToDamage && !IsTargetShieldDown(Target))
	{
		Total = 0.0f;
	}

	// The class passive's own damage is added on top, and it is NOT gated: passing through a shield
	// that is still up is the entire point of it. @see UAbilityHandler::GetBonusPierceDamage.
	if (PawnOwner)
	{
		if (const UAbilityComponent* Abilities = PawnOwner->FindComponentByClass<UAbilityComponent>())
		{
			Total += Abilities->GetPredictedPierceDamage(Target);
		}
	}

	return Total;
}

bool AShooterWeapon::IsTargetShieldDown(AActor* Target) const
{
	if (!IsValid(Target))
	{
		return false;
	}

	// Same components, same order and same ceilings as ApplyHitscanIonization uses to FILL the
	// meter, so the thing that charges a target and the gate that opens when it is full can never
	// be reading two different numbers.
	if (const UEMFVelocityModifier* TargetModifier = Target->FindComponentByClass<UEMFVelocityModifier>())
	{
		return TargetModifier->IsAtMaxCharge();
	}

	if (const AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(Target))
	{
		return Prop->IsAtMaxCharge();
	}

	if (UEMF_FieldComponent* TargetField = Target->FindComponentByClass<UEMF_FieldComponent>())
	{
		const float CurrentCharge = TargetField->GetSourceDescription().PointChargeParams.Charge;
		return IsIonizationCapReached(CurrentCharge, MaxIonizationCharge);
	}

	// Carries no charge at all, so it has no shield to be down. An ordinary target, hurt normally.
	return true;
}

float AShooterWeapon::ApplyDamageToTarget(AActor* HitActor, float FinalDamage, const FDamageEvent& DamageEvent)
{
	if (!IsValid(HitActor) || FinalDamage <= 0.0f)
	{
		return 0.0f;
	}

	// A finisher weapon: nothing it hits loses health from the WEAPON until that target's shield is
	// down. The hit still happened -- ionization, knockback and the hit marker all run in the
	// callers, which is what makes charging a target up feel like progress rather than like missing.
	//
	// Read here and acted on twice below, because a class passive may have damage of its own that
	// goes past this gate, and that damage still has to be applied and still has to be reported.
	const bool bShieldGated = IsShieldGateBlocking(HitActor);

	// A player firing from a client cannot write health itself: AShooterCharacter::TakeDamage is
	// authority-only now, so the direct call below would silently do nothing on that machine while
	// still looking like a hit locally. Route it through the character, which reports it upstream.
	// NPC weapons are unaffected: AI runs on the server, where this branch is never taken.
	if (AShooterCharacter* OwnerCharacter = Cast<AShooterCharacter>(PawnOwner))
	{
		if (!OwnerCharacter->HasAuthority())
		{
			// Reported even when the gate is closed, which it did NOT used to be. The report is what
			// tells the server a hit landed at all, and the server has its own reason to care about
			// one that the weapon cannot pay for: the shield-piercing half of a class passive. The
			// server re-checks the gate itself, so nothing is granted by reporting.
			OwnerCharacter->DealDamage(HitActor, FinalDamage, DamageEvent.DamageTypeClass, this);

			// Report the requested damage so local hit feedback still fires immediately. Kill
			// feedback will not, because the client cannot know yet: it learns the outcome from
			// replicated health a round trip later.
			return bShieldGated ? 0.0f : FinalDamage;
		}
	}

	// Authority. The passive's own damage first and unconditionally: it is the half that is supposed
	// to reach health through a shield that is still up.
	const float PierceDamage = ApplyPassivePierceDamage(HitActor);

	if (bShieldGated)
	{
		return PierceDamage;
	}

	return HitActor->TakeDamage(FinalDamage, DamageEvent,
		PawnOwner ? PawnOwner->GetController() : nullptr, this) + PierceDamage;
}

bool AShooterWeapon::IsShieldGateBlocking(AActor* HitActor) const
{
	return bRequiresBrokenShieldToDamage && !IsTargetShieldDown(HitActor);
}

float AShooterWeapon::ApplyPassivePierceDamage(AActor* HitActor)
{
	// Authority only, and said out loud: this writes health, and a client that ran it would change
	// nothing anywhere else while looking to itself like it had.
	if (!IsValid(HitActor) || !PawnOwner || !PawnOwner->HasAuthority())
	{
		return 0.0f;
	}

	UAbilityComponent* Abilities = PawnOwner->FindComponentByClass<UAbilityComponent>();
	if (!Abilities)
	{
		return 0.0f;
	}

	const float Amount = Abilities->GetPierceDamageForShot(HitActor);
	if (Amount <= 0.0f)
	{
		return 0.0f;
	}

	// Deliberately NOT routed through the gate above, and deliberately its own damage event: this is
	// the passive's damage, not the weapon's, and the whole mechanic is that it does not wait for
	// the shield to come off. AShooterNPC::TakeDamage subtracts from health directly and has no
	// shield term of its own, so this arrives where it is meant to.
	FPointDamageEvent PierceEvent;
	PierceEvent.DamageTypeClass = HitscanDamageType ? HitscanDamageType : TSubclassOf<UDamageType>(UDamageType::StaticClass());

	return HitActor->TakeDamage(Amount, PierceEvent, PawnOwner->GetController(), this);
}

namespace
{
	/** Check if actor is dead after TakeDamage (synchronous check via HP/bIsDead flags) */
	bool IsActorDeadAfterDamage(AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return true;
		}

		// ShooterNPC covers ShooterNPC, FlyingDrone, MeleeNPC, BossCharacter
		if (AShooterNPC* NPC = Cast<AShooterNPC>(Actor))
		{
			return NPC->IsDead();
		}

		// Player character
		if (AShooterCharacter* ShooterChar = Cast<AShooterCharacter>(Actor))
		{
			return ShooterChar->IsDead();
		}

		// Training dummies
		if (AShooterDummy* Dummy = Cast<AShooterDummy>(Actor))
		{
			return Dummy->IsDead();
		}

		// Physics props
		if (AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(Actor))
		{
			return Prop->IsDead();
		}

		// Fallback for unknown actor types
		return Actor->IsPendingKillPending();
	}

	/** Above this launch speed a grounded character is knocked off its feet on purpose.
	 *  Below it an ordinary bullet must not lift anyone. @see ApplyHitscanKnockback. */
	constexpr float HitscanGroundedLaunchThreshold = 400.0f;

	/**
	 * Single entrance for hitscan knockback on characters.
	 *
	 * LaunchCharacter ALWAYS forces MOVE_Falling (CharacterMovementComponent::HandlePendingLaunch),
	 * even for a purely horizontal impulse. For a character standing on the ground that reads to the
	 * anim graph as "airborne", so every bullet made the NPC hop instead of playing its flinch.
	 * Rule: a grounded character only gets launched by a deliberately strong impulse; ordinary
	 * bullet forces are dropped and the hit shows up as the flinch reaction alone. A character
	 * already in the air is launched as before — it is falling anyway, nothing to break.
	 */
	void ApplyHitscanKnockback(ACharacter* HitCharacter, const FVector& LaunchVelocity, bool bIonizerWeapon)
	{
		if (!IsValid(HitCharacter))
		{
			return;
		}

		// Stationary turret never takes hit impulses: with its GravityScale=0 the forced
		// MOVE_Falling would push it into permanent flight.
		if (Cast<ASniperTurretNPC>(HitCharacter))
		{
			return;
		}

		// The boss opts out of the ionizer weapon's knockback (it still takes damage + ionization).
		if (bIonizerWeapon && Cast<ABossCharacter>(HitCharacter))
		{
			return;
		}

		const UCharacterMovementComponent* Movement = HitCharacter->GetCharacterMovement();
		if (Movement && Movement->IsMovingOnGround() && LaunchVelocity.Size() < HitscanGroundedLaunchThreshold)
		{
			return;
		}

		HitCharacter->LaunchCharacter(LaunchVelocity, false, false);
	}
}

AShooterWeapon::AShooterWeapon()
{
	PrimaryActorTick.bCanEverTick = true;

	// Teammates have to see what you are holding. Without this the weapon actor simply does not
	// exist on anyone else's machine: the third-person mesh has nothing to attach to, so a remote
	// player stands there in a pistol pose with empty hands. Movement is not replicated because the
	// weapon is attached to the character and rides along with it.
	bReplicates = true;
	SetReplicatingMovement(false);

	// Mount points, seeded with the Low Poly Shooter Pack names so a weapon built on that art works
	// with no setup at all. Per weapon, because the socket is authored on the weapon's mesh: the
	// same scope fits two rifles whose rails are named differently. Clear an entry and that type
	// simply cannot be mounted on this weapon, which is a legitimate answer for a gun with no rail.
	AttachmentSockets.Add(EWeaponAttachmentType::Optic, FName("SOCKET_Scope"));
	AttachmentSockets.Add(EWeaponAttachmentType::Magazine, FName("SOCKET_Magazine"));
	AttachmentSockets.Add(EWeaponAttachmentType::Muzzle, FName("SOCKET_Muzzle"));
	AttachmentSockets.Add(EWeaponAttachmentType::Stock, FName("SOCKET_Stock"));

	// create the root
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// create the first person mesh
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
	FirstPersonMesh->SetupAttachment(RootComponent);

	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	FirstPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	FirstPersonMesh->bOnlyOwnerSee = true;

	// Always tick pose & refresh bones every frame, and disable Update Rate Optimizations.
	// Default settings can skip bone updates when the mesh isn't on screen / not playing montage,
	// which makes child components (sights, suppressors, lasers attached to sockets) lag a frame
	// behind the weapon's animated pose.
	FirstPersonMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	FirstPersonMesh->bEnableUpdateRateOptimizations = false;

	// create the third person mesh
	ThirdPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Third Person Mesh"));
	ThirdPersonMesh->SetupAttachment(RootComponent);

	ThirdPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	ThirdPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
	ThirdPersonMesh->bOwnerNoSee = true;

	// Create ADS camera component on the first person mesh
	// It will be attached to the Sight socket in BeginPlay after meshes are set up
	ADSCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("ADS Camera"));
	ADSCameraComponent->SetupAttachment(FirstPersonMesh);
}

// ==================== FPS Animation Pack profile ====================
//
// Everything the pack ships as data lives in Blueprint-only classes: WeaponSettings_C,
// ViewmodelSettings_C and the user struct F_ViewmodelAnimations. None of them has a native parent,
// so there is nothing to cast to and reflection by property name is the only way in. Same channel
// AShooterCharacter already uses to push Gait and ActiveAimPoint into their controller.
//
// The one trap worth naming: members of a user-defined STRUCT are not called what the editor shows.
// "PrimaryReload" is stored as "PrimaryReload_9_A1B2...GUID", and the GUID differs per struct, so
// members are matched on the prefix up to the first underscore-digit run. Object properties are
// matched by prefix too; the top-level properties of a Blueprint CLASS keep their plain names.
namespace PackProfile
{
	FProperty* FindByName(const UStruct* Owner, const TCHAR* Name)
	{
		return Owner ? Owner->FindPropertyByName(FName(Name)) : nullptr;
	}

	/** User struct member lookup: exact name first, then the "Name_<index>_<GUID>" form. */
	FProperty* FindMember(const UStruct* Owner, const TCHAR* Name)
	{
		if (!Owner)
		{
			return nullptr;
		}

		if (FProperty* Exact = Owner->FindPropertyByName(FName(Name)))
		{
			return Exact;
		}

		const FString Prefix = FString(Name) + TEXT("_");
		for (TFieldIterator<FProperty> It(Owner); It; ++It)
		{
			if (It->GetName().StartsWith(Prefix, ESearchCase::CaseSensitive))
			{
				return *It;
			}
		}

		return nullptr;
	}

	/** Reads an object pointer, following a soft reference if that is how it was stored. */
	UObject* ReadObject(const void* Container, const UStruct* Owner, const TCHAR* Name)
	{
		FProperty* Prop = FindMember(Owner, Name);
		if (!Prop || !Container)
		{
			return nullptr;
		}

		// Soft first, and that order is not cosmetic: FSoftObjectProperty also derives from
		// FObjectPropertyBase, so testing the base first would swallow soft references and hand
		// back null for any asset that simply had not been loaded yet.
		if (const FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(Prop))
		{
			return SoftProp->GetPropertyValue_InContainer(Container).LoadSynchronous();
		}

		if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
		{
			return ObjProp->GetObjectPropertyValue_InContainer(Container);
		}

		return nullptr;
	}

	UClass* ReadClass(const void* Container, const UStruct* Owner, const TCHAR* Name)
	{
		FProperty* Prop = FindMember(Owner, Name);
		if (!Prop || !Container)
		{
			return nullptr;
		}

		// Soft first here too, for the same reason.
		if (const FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Prop))
		{
			return Cast<UClass>(SoftClassProp->GetPropertyValue_InContainer(Container).LoadSynchronous());
		}

		if (const FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
		{
			return Cast<UClass>(ClassProp->GetObjectPropertyValue_InContainer(Container));
		}

		return nullptr;
	}

	/** Blueprint numbers are doubles as often as floats, so both shapes are accepted. */
	bool ReadNumber(const void* Container, const UStruct* Owner, const TCHAR* Name, double& Out)
	{
		FProperty* Prop = FindMember(Owner, Name);
		if (!Prop || !Container)
		{
			return false;
		}

		if (const FDoubleProperty* D = CastField<FDoubleProperty>(Prop))
		{
			Out = D->GetPropertyValue_InContainer(Container);
			return true;
		}
		if (const FFloatProperty* F = CastField<FFloatProperty>(Prop))
		{
			Out = F->GetPropertyValue_InContainer(Container);
			return true;
		}
		if (const FIntProperty* I = CastField<FIntProperty>(Prop))
		{
			Out = I->GetPropertyValue_InContainer(Container);
			return true;
		}

		return false;
	}

	bool ReadVector2D(const void* Container, const UStruct* Owner, const TCHAR* Name, FVector2D& Out)
	{
		FStructProperty* Prop = CastField<FStructProperty>(FindMember(Owner, Name));
		if (!Prop || !Container || Prop->Struct != TBaseStructure<FVector2D>::Get())
		{
			return false;
		}

		if (const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Container))
		{
			Out = *static_cast<const FVector2D*>(ValuePtr);
			return true;
		}

		return false;
	}

	/** Address of a nested struct plus its layout, so its members can be read the same way. */
	bool OpenStruct(const void* Container, const UStruct* Owner, const TCHAR* Name,
		const void*& OutAddr, const UStruct*& OutStruct)
	{
		FStructProperty* Prop = CastField<FStructProperty>(FindByName(Owner, Name));
		if (!Prop || !Container)
		{
			return false;
		}

		OutAddr = Prop->ContainerPtrToValuePtr<void>(Container);
		OutStruct = Prop->Struct;
		return OutAddr != nullptr && OutStruct != nullptr;
	}

	/** Writes an object pointer by name, refusing a value the property cannot legally hold.
	 *
	 *  The type check is the whole safety of this: the target is a Blueprint class we do not
	 *  compile against, so a renamed or retyped field must come back as "did nothing" rather than
	 *  as a pointer of the wrong class sitting in a slot the pack will dereference. */
	bool WriteObject(UObject* Target, const TCHAR* Name, UObject* Value)
	{
		if (!Target)
		{
			return false;
		}

		FObjectProperty* Prop = CastField<FObjectProperty>(FindByName(Target->GetClass(), Name));
		if (!Prop)
		{
			return false;
		}

		if (Value && !Value->IsA(Prop->PropertyClass))
		{
			return false;
		}

		Prop->SetObjectPropertyValue_InContainer(Target, Value);
		return true;
	}

	/** The pack's controller on a pawn, found by class NAME because the class is Blueprint only.
	 *
	 *  Same lookup AShooterCharacter does for Gait and ActiveAimPoint. Duplicated rather than
	 *  shared because the copy there is file local, and one six line loop in two files is cheaper
	 *  than a header that exists only to hold it. */
	UActorComponent* FindViewmodelController(const AActor* Owner)
	{
		if (!Owner)
		{
			return nullptr;
		}

		for (UActorComponent* Component : Owner->GetComponents())
		{
			if (Component && Component->GetClass()->GetName().StartsWith(TEXT("ViewmodelController")))
			{
				return Component;
			}
		}

		return nullptr;
	}
}

void AShooterWeapon::PushPackViewmodelSettings()
{
	if (!PackWeaponSettings || !PawnOwner)
	{
		return;
	}

	UActorComponent* Viewmodel = PackProfile::FindViewmodelController(PawnOwner);
	if (!Viewmodel)
	{
		// Not an error. A character that was never migrated has no such component, and this weapon
		// is then simply held by our own arms graph, which is what every pre-pack weapon does.
		return;
	}

	// WeaponSettings is the whole profile; ActiveSettings is the UE5 viewmodel inside it, and the
	// second one is the one that matters. Their own WeaponManager sets both on every swap, which is
	// the behaviour being reproduced here.
	PackProfile::WriteObject(Viewmodel, TEXT("WeaponSettings"), PackWeaponSettings);

	UObject* const Viewmodel5 = PackProfile::ReadObject(
		PackWeaponSettings, PackWeaponSettings->GetClass(), TEXT("UE5"));

	if (!Viewmodel5)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PACK] %s: profile %s has no UE5 viewmodel settings, so the arms "
			"keep the pose of whatever weapon was held before this one."),
			*GetName(), *PackWeaponSettings->GetName());
		return;
	}

	if (PackProfile::WriteObject(Viewmodel, TEXT("ActiveSettings"), Viewmodel5))
	{
		UE_LOG(LogTemp, Log, TEXT("[PACK] %s: ActiveSettings <- %s"), *GetName(), *Viewmodel5->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[PACK] %s: could not write ActiveSettings on %s. The arms will hold "
			"this weapon in the previous weapon's pose."), *GetName(), *Viewmodel->GetClass()->GetName());
	}
}

void AShooterWeapon::ApplyPackWeaponSettings()
{
	// The override alone is a valid setup: a weapon can take PRAS recoil without taking the rest
	// of the profile, which is how an existing gun gets their recoil without losing its own tuning.
	ResolvedPackRecoilData = PackRecoilData;

	if (!PackWeaponSettings)
	{
		return;
	}

	const UObject* Settings = PackWeaponSettings;
	const UStruct* SettingsClass = Settings->GetClass();

	// The profile overwrites. Assigning it is the statement "this weapon is theirs", so a value it
	// carries replaces ours rather than deferring to it -- otherwise a gun that was set up by hand
	// once could never be moved onto a profile without emptying eight fields first.
	//
	// An EMPTY slot still leaves ours alone, and that is a different rule, not an exception to this
	// one: their MX16A4 has no hands Fire montage because PRAS does that shake, and treating the
	// gap as "clear it" would remove a working animation in exchange for nothing.
	auto TakeMontage = [&](TObjectPtr<UAnimMontage>& Ours, const void* Container,
		const UStruct* Layout, const TCHAR* Name, const TCHAR* Label)
	{
		if (UAnimMontage* Found = Cast<UAnimMontage>(PackProfile::ReadObject(Container, Layout, Name)))
		{
			if (Ours != Found)
			{
				UE_LOG(LogTemp, Log, TEXT("[PACK] %s: %s <- %s (was %s)"),
					*GetName(), Label, *Found->GetName(), *GetNameSafe(Ours));
			}
			Ours = Found;
		}
	};

	auto TakeAnimation = [&](TObjectPtr<UAnimationAsset>& Ours, const void* Container,
		const UStruct* Layout, const TCHAR* Name, const TCHAR* Label)
	{
		if (UAnimationAsset* Found = Cast<UAnimationAsset>(PackProfile::ReadObject(Container, Layout, Name)))
		{
			if (Ours != Found)
			{
				UE_LOG(LogTemp, Log, TEXT("[PACK] %s: %s <- %s (was %s)"),
					*GetName(), Label, *Found->GetName(), *GetNameSafe(Ours));
			}
			Ours = Found;
		}
	};

	// --- The hands. Lives one level down, in the nested ViewmodelSettings asset. ---
	// UE5 and UE4 are two separate pointers on their side because the pack ships both rigs; we are
	// on the UE5 one and read only that. The UE4 slot is deliberately ignored rather than used as
	// a fallback: its montages are built for the other skeleton and would look wrong, not merely
	// different.
	if (const UObject* Viewmodel = PackProfile::ReadObject(Settings, SettingsClass, TEXT("UE5")))
	{
		const UStruct* ViewmodelClass = Viewmodel->GetClass();
		const void* AnimsAddr = nullptr;
		const UStruct* AnimsLayout = nullptr;

		if (PackProfile::OpenStruct(Viewmodel, ViewmodelClass, TEXT("CharacterAnims"), AnimsAddr, AnimsLayout))
		{
			TakeMontage(DrawMontage, AnimsAddr, AnimsLayout, TEXT("Equip"), TEXT("DrawMontage"));
			TakeMontage(HolsterMontage, AnimsAddr, AnimsLayout, TEXT("UnEquip"), TEXT("HolsterMontage"));
			TakeMontage(ReloadMontage, AnimsAddr, AnimsLayout, TEXT("PrimaryReload"), TEXT("ReloadMontage"));
			TakeMontage(SecondaryReloadMontage, AnimsAddr, AnimsLayout, TEXT("SecondaryReload"),
				TEXT("SecondaryReloadMontage"));
			TakeMontage(ReloadEndMontage, AnimsAddr, AnimsLayout, TEXT("AdditionalReload"),
				TEXT("ReloadEndMontage"));

			// Their "Fire" on the HANDS is not the shot, it is the hands working the action after
			// it: the bolt on a Kar98K, the pump on a KXG12. Only the three manual guns fill it,
			// and on everything else the shake of firing comes from the PRAS node instead, which is
			// why this slot is empty on every automatic weapon they ship.
			TakeMontage(CycleActionMontage, AnimsAddr, AnimsLayout, TEXT("Fire"),
				TEXT("CycleActionMontage"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[PACK] %s: profile %s has no UE5 viewmodel settings, so the hands "
			"keep whatever the Blueprint gave them."), *GetName(), *PackWeaponSettings->GetName());
	}

	// --- The gun's own meshes. ---
	{
		const void* AnimsAddr = nullptr;
		const UStruct* AnimsLayout = nullptr;

		if (PackProfile::OpenStruct(Settings, SettingsClass, TEXT("WeaponAnims"), AnimsAddr, AnimsLayout))
		{
			TakeAnimation(WeaponMeshFireAnimation, AnimsAddr, AnimsLayout, TEXT("Fire"),
				TEXT("WeaponMeshFireAnimation"));
			TakeAnimation(WeaponMeshReloadAnimation, AnimsAddr, AnimsLayout, TEXT("PrimaryReload"),
				TEXT("WeaponMeshReloadAnimation"));
			TakeAnimation(WeaponMeshSecondaryReloadAnimation, AnimsAddr, AnimsLayout, TEXT("SecondaryReload"),
				TEXT("WeaponMeshSecondaryReloadAnimation"));
			TakeAnimation(WeaponMeshReloadEndAnimation, AnimsAddr, AnimsLayout, TEXT("AdditionalReload"),
				TEXT("WeaponMeshReloadEndAnimation"));
			TakeAnimation(WeaponMeshLastShotAnimation, AnimsAddr, AnimsLayout, TEXT("FireOut"),
				TEXT("WeaponMeshLastShotAnimation"));
		}
	}

	// --- The shape of the reload, taken from the class their own data asks for. ---
	//
	// Read rather than configured because they already answered it: BP_ManualAction is the class
	// they give to exactly the two guns that load one round at a time, and nothing else. Deciding
	// it from the animation slots instead would be guesswork, since MGX5 also fills
	// AdditionalReload and it means a third magazine variant there, not a closing stage.
	if (const UClass* PackWeaponClass = PackProfile::ReadClass(Settings, SettingsClass, TEXT("WeaponClass")))
	{
		const bool bManual = PackWeaponClass->GetName().StartsWith(TEXT("BP_ManualAction"));
		if (bManual != bPerRoundReload)
		{
			UE_LOG(LogTemp, Log, TEXT("[PACK] %s: bPerRoundReload <- %s (their class is %s)"),
				*GetName(), bManual ? TEXT("true") : TEXT("false"), *PackWeaponClass->GetName());
		}
		bPerRoundReload = bManual;
	}

	// --- The anim blueprint the gun mesh runs (their ABP_<gun>). ---
	// Set on the components rather than stored, because that is where it lives for the weapons that
	// were wired by hand.
	if (UClass* WeaponAnimClass = PackProfile::ReadClass(Settings, SettingsClass, TEXT("WeaponAnimInstance")))
	{
		USkeletalMeshComponent* const PackMeshes[] = { FirstPersonMesh, ThirdPersonMesh };

		for (USkeletalMeshComponent* Mesh : PackMeshes)
		{
			if (Mesh && Mesh->GetAnimClass() != WeaponAnimClass)
			{
				UE_LOG(LogTemp, Log, TEXT("[PACK] %s: %s anim class <- %s (was %s)"),
					*GetName(), *Mesh->GetName(), *WeaponAnimClass->GetName(),
					*GetNameSafe(Mesh->GetAnimClass()));
				Mesh->SetAnimInstanceClass(WeaponAnimClass);
			}
		}
	}

	// --- Sound. ---
	if (USoundBase* Found = Cast<USoundBase>(PackProfile::ReadObject(Settings, SettingsClass, TEXT("FireSound"))))
	{
		if (FireSound != Found)
		{
			UE_LOG(LogTemp, Log, TEXT("[PACK] %s: FireSound <- %s (was %s)"),
				*GetName(), *Found->GetName(), *GetNameSafe(FireSound));
		}
		FireSound = Found;
	}

	// --- Recoil. Their property is called RecoilSettings, not RecoilData. ---
	// PackRecoilData is the one field the profile does NOT overrule, because it is not one of our
	// legacy fields: it is a deliberate pack-side override, set on the same panel and for exactly
	// this purpose.
	if (!ResolvedPackRecoilData)
	{
		ResolvedPackRecoilData =
			Cast<URecoilData>(PackProfile::ReadObject(Settings, SettingsClass, TEXT("RecoilSettings")));

		if (ResolvedPackRecoilData)
		{
			UE_LOG(LogTemp, Log, TEXT("[PACK] %s: PRAS recoil <- %s"),
				*GetName(), *ResolvedPackRecoilData->GetName());
		}
	}

	// --- The camera jolt. Their CameraAnimator plays this; we have no CameraAnimator and must not
	// take one, because the same component also drives THEIR ads FOV and would fight our zoom. So
	// only the three numbers are lifted and the playback lives on our side.
	if (const UObject* Shake = PackProfile::ReadObject(Settings, SettingsClass, TEXT("RecoilShake")))
	{
		const UStruct* ShakeClass = Shake->GetClass();
		PackShakeCurve = Cast<UCurveVector>(PackProfile::ReadObject(Shake, ShakeClass, TEXT("RotationCurve")));

		double ShakeNumber = 0.0;
		if (PackProfile::ReadNumber(Shake, ShakeClass, TEXT("PlayRate"), ShakeNumber) && ShakeNumber > KINDA_SMALL_NUMBER)
		{
			PackShakePlayRate = static_cast<float>(ShakeNumber);
		}
		if (PackProfile::ReadNumber(Shake, ShakeClass, TEXT("Smoothing"), ShakeNumber) && ShakeNumber > KINDA_SMALL_NUMBER)
		{
			PackShakeSmoothing = static_cast<float>(ShakeNumber);
		}

		PackProfile::ReadVector2D(Shake, ShakeClass, TEXT("Pitch"), PackShakePitchRange);
		PackProfile::ReadVector2D(Shake, ShakeClass, TEXT("Yaw"), PackShakeYawRange);
		PackProfile::ReadVector2D(Shake, ShakeClass, TEXT("Roll"), PackShakeRollRange);

		UE_LOG(LogTemp, Log, TEXT("[PACK] %s: camera shake <- %s (curve %s, rate %.2f, smoothing %.1f, "
			"pitch %.2f..%.2f yaw %.2f..%.2f roll %.2f..%.2f)"),
			*GetName(), *Shake->GetName(), *GetNameSafe(PackShakeCurve), PackShakePlayRate, PackShakeSmoothing,
			PackShakePitchRange.X, PackShakePitchRange.Y, PackShakeYawRange.X, PackShakeYawRange.Y,
			PackShakeRollRange.X, PackShakeRollRange.Y);
	}

	// --- The two numbers, each behind its own switch (see the header for why). ---
	double Number = 0.0;

	if (PackProfile::ReadNumber(Settings, SettingsClass, TEXT("FireRate"), Number) && Number > KINDA_SMALL_NUMBER)
	{
		PackFireRateRPM = static_cast<float>(Number);

		if (bPackSetsFireRate)
		{
			RefireRate = static_cast<float>(60.0 / Number);
			UE_LOG(LogTemp, Log, TEXT("[PACK] %s: RefireRate <- %.4f s (%.0f rounds per minute)"),
				*GetName(), RefireRate, Number);
		}
	}

	if (bPackSetsMagazine && PackProfile::ReadNumber(Settings, SettingsClass, TEXT("Ammo"), Number) && Number >= 1.0)
	{
		MagazineSize = FMath::RoundToInt(Number);
		UE_LOG(LogTemp, Log, TEXT("[PACK] %s: MagazineSize <- %d"), *GetName(), MagazineSize);
	}
}

FRotator AShooterWeapon::RollPackShakeAmplitude() const
{
	// Their ranges are written MIN..MAX with the sign carried by both ends (a rifle's Roll is
	// -1.3..-1.5), so RandRange handles the ordering rather than us assuming X < Y.
	const float Pitch = FMath::RandRange(PackShakePitchRange.X, PackShakePitchRange.Y);
	const float Yaw = FMath::RandRange(PackShakeYawRange.X, PackShakeYawRange.Y);
	const float Roll = FMath::RandRange(PackShakeRollRange.X, PackShakeRollRange.Y);

	return FRotator(Pitch, Yaw, Roll) * PackCameraShakeScale;
}

float AShooterWeapon::GetPackFireRateRPM() const
{
	if (PackFireRateRPM > KINDA_SMALL_NUMBER)
	{
		return PackFireRateRPM;
	}

	// No profile, only a hand-assigned recoil asset. PRAS still needs a rate, and our own interval
	// is the same fact written the other way round.
	return (RefireRate > KINDA_SMALL_NUMBER) ? (60.0f / RefireRate) : 600.0f;
}

void AShooterWeapon::BeginPlay()
{
	Super::BeginPlay();

	// Before anything reads a montage, a sound or the magazine size: the profile only fills what
	// was left empty, so running it first costs nothing and running it late would be a race.
	ApplyPackWeaponSettings();

	// The magazine size as authored, with the profile applied and no attachment yet. Taken here and
	// not earlier: on a client the attachment array can arrive before BeginPlay, and a base taken
	// from a size that was already multiplied would multiply twice. ApplyMagazineModifiers ran as a
	// no-op in that case and gets its real run now, before the first magazine is filled below.
	BaseMagazineSize = MagazineSize;
	ApplyMagazineModifiers();

	// Pay for the pool here rather than on the first trigger pull. Above the owner check on purpose:
	// a weapon with no owner still knows what it fires, and warming the pool is the one useful thing
	// it can do. @see PrewarmProjectilePool.
	PrewarmProjectilePool();

	// A weapon belongs to whoever is holding it, and every spawn path sets that owner. One thing
	// does not: a weapon actor dragged straight into a level. It has nobody to attach its meshes
	// to, nobody to fire it, and no HUD to update, so the only sane thing it can do is sit there
	// and say so instead of taking the editor down on the null owner.
	AActor* OwningActor = GetOwner();
	if (!OwningActor)
	{
		UE_LOG(LogTemp, Error, TEXT("[WEAPON] %s has no owner. A weapon has to be given to a character, "
			"not placed in the level: use a pickup for that. This one will do nothing."), *GetName());
		return;
	}

	// subscribe to the owner's destroyed delegate
	OwningActor->OnDestroyed.AddDynamic(this, &AShooterWeapon::OnOwnerDestroyed);

	// cast the weapon owner
	WeaponOwner = Cast<IShooterWeaponHolder>(OwningActor);
	PawnOwner = Cast<APawn>(OwningActor);

	// Cache movement component for Heat System speed calculations
	if (ACharacter* CharOwner = Cast<ACharacter>(GetOwner()))
	{
		CachedMovementComponent = CharOwner->GetCharacterMovement();
	}

	// NPC optimization: hide first person mesh for non-player owners
	if (!PawnOwner || !PawnOwner->IsPlayerControlled())
	{
		if (FirstPersonMesh)
		{
			FirstPersonMesh->SetVisibility(false);
			FirstPersonMesh->SetComponentTickEnabled(false);
		}
	}

	// fill the first ammo clip
	CurrentBullets = MagazineSize;

	// A gun comes into the world with a full energy reserve. A pickup that means to hand over less
	// overwrites it straight after the spawn (ADroppedRangedWeapon::GrantEnergyAmmo). Server only:
	// the owning client gets the number by replication.
	if (HasAuthority() && UsesEnergyReserve())
	{
		SetEnergyReserve(GetEnergyReserveCapacity());
	}

	// attach the meshes to the owner. An owner that is not a weapon holder at all (a prop, a
	// spawner) fails the cast above and would crash here the same way the null one did.
	if (!WeaponOwner)
	{
		UE_LOG(LogTemp, Error, TEXT("[WEAPON] %s is owned by %s, which is not a weapon holder. "
			"Nothing to attach to."), *GetName(), *GetNameSafe(OwningActor));
		return;
	}

	WeaponOwner->AttachWeaponMeshes(this);

	// The weapon's own built-in sight, when it carries one as a component rather than as part of
	// the mesh. Found once, by name, BEFORE anything can be mounted: both the eye-point search and
	// the optic mount need to know which component that is, and neither may guess.
	if (!DefaultOpticComponentName.IsNone())
	{
		TArray<USceneComponent*> AllChildren;
		if (FirstPersonMesh)
		{
			FirstPersonMesh->GetChildrenComponents(/*bIncludeAllDescendants*/ true, AllChildren);
		}
		for (USceneComponent* Child : AllChildren)
		{
			if (Child && Child->GetFName() == DefaultOpticComponentName)
			{
				DefaultOpticComponent = Child;
				break;
			}
		}

		if (!DefaultOpticComponent)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ATTACH] %s: DefaultOpticComponentName is '%s', but no "
				"component under the first person mesh is called that. Mounting an optic will "
				"leave the built-in sight visible and both will offer an eye point."),
				*GetName(), *DefaultOpticComponentName.ToString());
		}
	}

	// Builds the mounted meshes, then resolves the ADS anchor and inherits render visibility and
	// tick order onto everything under the weapon meshes. On a fresh weapon there is nothing
	// mounted and this is just those last two steps, which is what used to be written out here.
	// A client whose weapon arrived with attachments already on it gets them built here too.
	RebuildAttachmentMeshes();

	// === Diagnostic dump of attachment transforms after equip ===
	// Filter Output Log by [ATTACH_DEBUG] to read it.
	if (FirstPersonMesh)
	{
		const FTransform MeshWorld = FirstPersonMesh->GetComponentTransform();
		const FTransform MeshRelative = FirstPersonMesh->GetRelativeTransform();
		UE_LOG(LogTemp, Warning, TEXT("[ATTACH_DEBUG] === %s FirstPersonMesh ==="), *GetName());
		UE_LOG(LogTemp, Warning, TEXT("[ATTACH_DEBUG]   AttachSocket=%s"), *FirstPersonMesh->GetAttachSocketName().ToString());
		UE_LOG(LogTemp, Warning, TEXT("[ATTACH_DEBUG]   Rel  T=%s R=%s S=%s"),
			*MeshRelative.GetLocation().ToString(),
			*MeshRelative.GetRotation().Rotator().ToString(),
			*MeshRelative.GetScale3D().ToString());
		UE_LOG(LogTemp, Warning, TEXT("[ATTACH_DEBUG]   World T=%s R=%s S=%s"),
			*MeshWorld.GetLocation().ToString(),
			*MeshWorld.GetRotation().Rotator().ToString(),
			*MeshWorld.GetScale3D().ToString());

		TArray<USceneComponent*> AllChildren;
		FirstPersonMesh->GetChildrenComponents(true, AllChildren);
		for (USceneComponent* Child : AllChildren)
		{
			if (!Child) continue;
			const FTransform Rel = Child->GetRelativeTransform();
			const FTransform World = Child->GetComponentTransform();
			UE_LOG(LogTemp, Warning,
				TEXT("[ATTACH_DEBUG] Child=%s class=%s ParentSocket=%s | Rel T=%s R=%s S=%s | World T=%s"),
				*Child->GetName(),
				*Child->GetClass()->GetName(),
				*Child->GetAttachSocketName().ToString(),
				*Rel.GetLocation().ToString(),
				*Rel.GetRotation().Rotator().ToString(),
				*Rel.GetScale3D().ToString(),
				*World.GetLocation().ToString());
		}
	}
}

void AShooterWeapon::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the refire timer
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);

	// and the reload timer, which would otherwise fire into a destroyed weapon
	GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);
}

void AShooterWeapon::PushLeftHandIK(UAnimInstance* AnimInstance, const FTransform& Transform, float Alpha)
{
	if (!AnimInstance)
	{
		return;
	}

	static const FName LeftHandIKTransformName(TEXT("LeftHandIKTransform"));
	if (FProperty* TransformProperty = AnimInstance->GetClass()->FindPropertyByName(LeftHandIKTransformName))
	{
		FStructProperty* StructProp = CastField<FStructProperty>(TransformProperty);
		if (StructProp && StructProp->Struct == TBaseStructure<FTransform>::Get())
		{
			if (void* ValuePtr = StructProp->ContainerPtrToValuePtr<void>(AnimInstance))
			{
				*static_cast<FTransform*>(ValuePtr) = Transform;
			}
		}
	}

	static const FName LeftHandIKAlphaName(TEXT("LeftHandIKAlpha"));
	FProperty* AlphaProperty = AnimInstance->GetClass()->FindPropertyByName(LeftHandIKAlphaName);
	if (!AlphaProperty)
	{
		return;
	}

	// Blueprint "float" is a double in UE5, but hand-authored C++ AnimBPs may still use float.
	if (FFloatProperty* FloatProp = CastField<FFloatProperty>(AlphaProperty))
	{
		if (void* ValuePtr = FloatProp->ContainerPtrToValuePtr<void>(AnimInstance))
		{
			*static_cast<float*>(ValuePtr) = Alpha;
		}
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(AlphaProperty))
	{
		if (void* ValuePtr = DoubleProp->ContainerPtrToValuePtr<void>(AnimInstance))
		{
			*static_cast<double*>(ValuePtr) = static_cast<double>(Alpha);
		}
	}
}

// ==================== Grip alignment ====================

const FName AShooterWeapon::OptionalGripSocketName(TEXT("OptionalGrip"));
const FName AShooterWeapon::ThirdPersonSocketSuffix(TEXT("_TP"));

// The engine's own weapon bone, present on every mannequin skeleton and used by nothing until now.
// It sits under ik_hand_root at the top of the hierarchy rather than inside an arm, which is what
// makes it a weapon transform rather than a hand: an animation can move the gun without moving the
// hand that holds it, and both hands are then keyed against it.
const FName AShooterWeapon::AnimatedWeaponSocketName(TEXT("ik_hand_gun"));

FName AShooterWeapon::PickThirdPersonSocket(const USkeletalMeshComponent* WeaponMesh, const FName BaseSocket)
{
	if (!WeaponMesh || BaseSocket.IsNone())
	{
		return BaseSocket;
	}

	const FName ThirdPersonSocket(*(BaseSocket.ToString() + ThirdPersonSocketSuffix.ToString()));
	return WeaponMesh->DoesSocketExist(ThirdPersonSocket) ? ThirdPersonSocket : BaseSocket;
}

void AShooterWeapon::AlignMeshToGripSocket(USkeletalMeshComponent* WeaponMesh, const FName GripSocket)
{
	const FString OwnerNameForLog = WeaponMesh && WeaponMesh->GetOwner()
		? WeaponMesh->GetOwner()->GetName()
		: FString(TEXT("<no owner>"));

	if (!WeaponMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG] AlignMeshToGripSocket: WeaponMesh is null — skipping"));
		return;
	}

	const FString MeshNameForLog = WeaponMesh->GetName();

	if (!WeaponMesh->DoesSocketExist(GripSocket))
	{
		UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG] %s %s: grip socket '%s' NOT FOUND — alignment skipped"),
			*OwnerNameForLog, *MeshNameForLog, *GripSocket.ToString());
		return;
	}

	const FTransform SocketComponent = WeaponMesh->GetSocketTransform(GripSocket, RTS_Component);
	const FQuat    InverseRotation = SocketComponent.GetRotation().Inverse();
	const FVector  MeshScale       = WeaponMesh->GetRelativeScale3D();

	// Scaled socket offset in mesh-local axes, then unrotated into mesh-relative axes.
	const FVector ScaledSocketLocation = SocketComponent.GetLocation() * MeshScale;
	const FVector NewRelativeLocation  = -InverseRotation.RotateVector(ScaledSocketLocation);

	// === BEFORE state ===
	const FTransform MeshRelBefore = WeaponMesh->GetRelativeTransform();
	const FVector    OptionalGripWorldBefore = WeaponMesh->GetSocketLocation(GripSocket);

	// Where the hand socket lives in world (the attach parent + attach socket on it).
	FVector HandSocketWorld = FVector::ZeroVector;
	FString HandSocketLog = TEXT("<no parent>");
	if (USceneComponent* AttachParent = WeaponMesh->GetAttachParent())
	{
		const FName AttachSock = WeaponMesh->GetAttachSocketName();
		HandSocketWorld = AttachSock.IsNone() ? AttachParent->GetComponentLocation()
		                                     : AttachParent->GetSocketLocation(AttachSock);
		HandSocketLog = FString::Printf(TEXT("%s.%s"), *AttachParent->GetName(), *AttachSock.ToString());
	}

	UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG] === %s %s (grip socket '%s') ==="),
		*OwnerNameForLog, *MeshNameForLog, *GripSocket.ToString());
	UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG]   Attached to: %s"), *HandSocketLog);
	UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG]   Socket S (component space):  Loc=%s  Rot=%s  Scale=%s"),
		*SocketComponent.GetLocation().ToString(),
		*SocketComponent.GetRotation().Rotator().ToString(),
		*SocketComponent.GetScale3D().ToString());
	UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG]   Mesh BEFORE: RelLoc=%s  RelRot=%s  RelScale=%s"),
		*MeshRelBefore.GetLocation().ToString(),
		*MeshRelBefore.GetRotation().Rotator().ToString(),
		*MeshScale.ToString());
	UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG]   Computed:    NewRelLoc=%s  NewRelRot=%s"),
		*NewRelativeLocation.ToString(),
		*InverseRotation.Rotator().ToString());
	UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG]   Hand world = %s"), *HandSocketWorld.ToString());
	UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG]   OptGrip world BEFORE = %s  (delta=%s)"),
		*OptionalGripWorldBefore.ToString(),
		*(OptionalGripWorldBefore - HandSocketWorld).ToString());

	// Apply location + rotation atomically; leave scale untouched (KeepRelative scale
	// from AttachmentRule preserved the BP-set scale and we don't want to clobber it).
	WeaponMesh->SetRelativeLocationAndRotation(NewRelativeLocation, InverseRotation);

	// === AFTER state — verify OptionalGrip actually lands at hand socket ===
	const FVector OptionalGripWorldAfter = WeaponMesh->GetSocketLocation(GripSocket);
	const FVector DeltaAfter = OptionalGripWorldAfter - HandSocketWorld;
	UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG]   OptGrip world AFTER  = %s  (delta=%s  len=%.4f)"),
		*OptionalGripWorldAfter.ToString(),
		*DeltaAfter.ToString(),
		DeltaAfter.Size());

	// Log every child's world position so we can see if scope/sight ended up where BP intended.
	TArray<USceneComponent*> Children;
	WeaponMesh->GetChildrenComponents(/*bIncludeAllDescendants*/ true, Children);
	for (USceneComponent* Child : Children)
	{
		if (!Child || Child == WeaponMesh) continue;
		UE_LOG(LogTemp, Warning, TEXT("[GRIP_DEBUG]   Child=%s ParentSocket=%s  RelLoc=%s  WorldLoc=%s"),
			*Child->GetName(),
			*Child->GetAttachSocketName().ToString(),
			*Child->GetRelativeLocation().ToString(),
			*Child->GetComponentLocation().ToString());
	}
}

void AShooterWeapon::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Update Heat System
	if (bUseHeatSystem)
	{
		UpdateHeat(DeltaTime);
	}

	UpdateSpread(DeltaTime);

	// The third person weapon is not steered from here any more. It used to have its world rotation
	// rebuilt from GetBaseAimRotation every frame, because the character's body faced its movement
	// direction and the gun in its hand therefore pointed anywhere but at the target. That was
	// papering over the real problem: the body was never turned. The character now yaws with the
	// camera (bUseControllerRotationYaw in AShooterCharacter) and pitches through the aim offset, so
	// the weapon points where the owner aims for the same reason a real one does, by being held by
	// someone facing that way.
}

// ==================== Spread ====================
//
// One number, read by the bullets and by the crosshair, so the ring on screen is the region the
// shot can land in rather than a decoration that happens to grow at the same time.

float AShooterWeapon::ResolveStateSpreadMultiplier() const
{
	const ACharacter* OwnerCharacter = Cast<ACharacter>(PawnOwner);
	if (!OwnerCharacter)
	{
		// Turrets, drones, anything that is not a character: no states to read, so the weapon
		// simply shoots at its base spread.
		return SpreadConfig.StillMultiplier;
	}

	const UCharacterMovementComponent* Move = OwnerCharacter->GetCharacterMovement();
	if (!Move)
	{
		return SpreadConfig.StillMultiplier;
	}

	// Exactly ONE state wins, resolved by priority. Multiplying them together would make
	// crouch-sprinting quieter than sprinting for no reason a player could read off the screen.
	float Multiplier = SpreadConfig.StillMultiplier;

	const UApexMovementComponent* Apex = Cast<UApexMovementComponent>(Move);
	const bool bSliding = Apex && Apex->IsSliding();
	const bool bSprinting = Apex && Apex->IsSprinting();

	if (Move->IsFalling())
	{
		// Airborne covers the jump, the fall and the wall-run: the feet are not planted.
		Multiplier = SpreadConfig.AirMultiplier;
	}
	else if (bSliding)
	{
		Multiplier = SpreadConfig.SlideMultiplier;
	}
	else if (bSprinting)
	{
		Multiplier = SpreadConfig.SprintMultiplier;
	}
	else if (Move->IsCrouching())
	{
		Multiplier = SpreadConfig.CrouchMultiplier;
	}
	else
	{
		// On foot: interpolate between standing still and walking by how fast the owner actually
		// is, so a nudge of the stick is not the full walking penalty.
		const FVector Velocity = OwnerCharacter->GetVelocity();
		const float Speed2D = FVector(Velocity.X, Velocity.Y, 0.0f).Size();
		const float SpeedAlpha = FMath::Clamp(Speed2D / FMath::Max(1.0f, SpreadConfig.WalkFullSpeed), 0.0f, 1.0f);
		Multiplier = FMath::Lerp(SpreadConfig.StillMultiplier, SpreadConfig.WalkMultiplier, SpeedAlpha);
	}

	// Aiming is deliberately NOT applied here. It is the last thing multiplied in, over state and
	// bloom together, so AdsMultiplier = 0 means zero degrees rather than "zero, plus whatever the
	// trigger added". See GetCurrentSpreadDegrees.
	return FMath::Max(0.0f, Multiplier);
}

void AShooterWeapon::UpdateSpread(float DeltaTime)
{
	// The state part chases its target: standing still after a sprint has to settle, which is the
	// whole reason a player stops before shooting.
	const float TargetMultiplier = ResolveStateSpreadMultiplier();
	CurrentStateMultiplier = FMath::FInterpTo(CurrentStateMultiplier, TargetMultiplier, DeltaTime, SpreadConfig.StateInterpSpeed);

	// The firing bloom bleeds off, but only after a quiet moment: a delay of about one refire
	// interval keeps a held trigger from recovering between its own shots.
	if (CurrentBloomDegrees > 0.0f)
	{
		const UWorld* World = GetWorld();
		const float Now = World ? World->GetTimeSeconds() : 0.0f;
		if (Now - TimeOfLastSpreadShot >= SpreadConfig.BloomRecoveryDelay)
		{
			CurrentBloomDegrees = FMath::Max(0.0f, CurrentBloomDegrees - SpreadConfig.BloomRecoveryRate * DeltaTime);
		}
	}
}

void AShooterWeapon::AddShotSpread()
{
	float PerShot = SpreadConfig.PerShotDegrees;

	if (const AShooterCharacter* ShooterOwner = Cast<AShooterCharacter>(PawnOwner))
	{
		const float ADSAlpha = FMath::Clamp(ShooterOwner->GetADSAlpha(), 0.0f, 1.0f);
		PerShot *= FMath::Lerp(1.0f, SpreadConfig.AdsBloomMultiplier, ADSAlpha);
	}

	CurrentBloomDegrees = FMath::Min(CurrentBloomDegrees + PerShot, SpreadConfig.MaxBloomDegrees);

	const UWorld* World = GetWorld();
	TimeOfLastSpreadShot = World ? World->GetTimeSeconds() : 0.0f;
}

float AShooterWeapon::GetCurrentSpreadDegrees() const
{
	// Clamped into a turret: a vice does not shake. The mount limits the gun by range instead, and
	// that range is read off the spread the gun would have in a player's hands (GetAimVariance).
	if (bMounted)
	{
		return 0.0f;
	}

	// An AI holding this weapon shoots at the plain base spread unless the weapon opts in: enemy
	// accuracy is tuned through the NPC's own aim variance, and moving it from here would be a
	// difficulty change wearing a crosshair feature's clothes.
	if (!SpreadConfig.bApplyToAIOwners && PawnOwner && !PawnOwner->IsPlayerControlled())
	{
		return AimVariance;
	}

	const float Total = AimVariance * CurrentStateMultiplier + CurrentBloomDegrees;

	// Aiming comes LAST and scales everything, state and bloom alike, blended by the ADS alpha so
	// the sight tightens over the same time it comes up. Applying it to the state part alone left
	// the firing bloom untouched, which is why a weapon with AdsMultiplier = 0 still sprayed: the
	// per-shot degrees were added after the multiplication. A scope that promises "no spread" has
	// to beat the trigger too, so anything that must survive aiming belongs in AimVariance.
	float AdsFactor = 1.0f;
	if (const AShooterCharacter* ShooterOwner = Cast<AShooterCharacter>(PawnOwner))
	{
		const float ADSAlpha = FMath::Clamp(ShooterOwner->GetADSAlpha(), 0.0f, 1.0f);
		AdsFactor = FMath::Lerp(1.0f, SpreadConfig.AdsMultiplier, ADSAlpha);
	}

	return FMath::Clamp(Total * AdsFactor, 0.0f, SpreadConfig.MaxSpreadDegrees);
}

void AShooterWeapon::OnOwnerDestroyed(AActor* DestroyedActor)
{
	// ensure this weapon is destroyed when the owner is destroyed
	Destroy();
}

void AShooterWeapon::ActivateWeapon()
{
	// unhide this weapon
	SetActorHiddenInGame(false);

	// Before the owner is told, because OnWeaponActivated is what swaps the arms anim class over,
	// and the graph reads the hold pose out of ActiveSettings on its first update.
	PushPackViewmodelSettings();

	// notify the owner
	WeaponOwner->OnWeaponActivated(this);

	// Show first-equip tutorial slide (if configured and not yet completed)
	// Skip if owner has tutorial debug mode enabled
	if (!FirstEquipTutorialID.IsNone())
	{
		bool bSkip = false;
		if (AShooterCharacter* ShooterOwner = Cast<AShooterCharacter>(PawnOwner))
		{
			bSkip = ShooterOwner->bTutorialDebugMode;
		}

		if (!bSkip)
		{
			if (UGameInstance* GI = GetGameInstance())
			{
				if (UTutorialSubsystem* TutorialSub = GI->GetSubsystem<UTutorialSubsystem>())
				{
					APlayerController* PC = PawnOwner ? Cast<APlayerController>(PawnOwner->GetController()) : nullptr;
					TutorialSub->ShowSlide(FirstEquipTutorialID, FirstEquipSlideData, PC);
				}
			}
		}
	}
}

float AShooterWeapon::GetSwitchPlayRate(const UAnimMontage* Montage, float Duration)
{
	// No montage, or no duration asked for, means "play it as the animator made it". A duration of
	// zero must never read as "instant": that would make an unfilled field silently delete the
	// animation instead of leaving it alone.
	if (!Montage || Duration <= KINDA_SMALL_NUMBER)
	{
		return 1.0f;
	}

	const float Length = Montage->GetPlayLength();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return 1.0f;
	}

	return Length / Duration;
}

float AShooterWeapon::GetHolsterLength() const
{
	if (!HolsterMontage)
	{
		return 0.0f;
	}

	const float Rate = GetHolsterPlayRate();
	return Rate > KINDA_SMALL_NUMBER ? HolsterMontage->GetPlayLength() / Rate : 0.0f;
}

float AShooterWeapon::GetDrawLength() const
{
	if (!DrawMontage)
	{
		return 0.0f;
	}

	const float Rate = GetDrawPlayRate();
	return Rate > KINDA_SMALL_NUMBER ? DrawMontage->GetPlayLength() / Rate : 0.0f;
}

void AShooterWeapon::DeactivateWeapon()
{
	// ensure we're no longer firing this weapon while deactivated
	StopFiring();

	SuspendReloadForHolster();

	// hide the weapon
	SetActorHiddenInGame(true);

	// notify the owner
	WeaponOwner->OnWeaponDeactivated(this);
}

void AShooterWeapon::StartFiring()
{
	// raise the firing flag
	bIsFiring = true;

	// ==================== Sprint-out gate ====================
	// Coming out of a sprint the weapon is still being raised, so the shot waits for the raise to
	// finish instead of going off from the sprint pose. It is deferred rather than dropped: hold
	// the trigger through the raise and it fires the moment the gate opens, release and StopFiring
	// clears the timer. Deliberately measured from when sprinting *ended*, which the movement
	// component stamps, and not from this call: the two differ when the player was already out of
	// the sprint before pulling the trigger.
	if (SprintToFireTime > 0.0f)
	{
		if (const APolarityCharacter* PolarityOwner = Cast<APolarityCharacter>(PawnOwner))
		{
			if (const UApexMovementComponent* Apex = PolarityOwner->GetApexMovement())
			{
				const float GateOpensAt = Apex->GetSprintEndTime() + SprintToFireTime;
				const float Remaining = GateOpensAt - GetWorld()->GetTimeSeconds();
				if (Remaining > 0.0f)
				{
					GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, Remaining, false);
					return;
				}
			}
		}
	}

	// check how much time has passed since we last shot
	// this may be under the refire rate if the weapon shoots slow enough and the player is spamming the trigger
	const float TimeSinceLastShot = GetWorld()->GetTimeSeconds() - TimeOfLastShot;
	const float CurrentRefireRate = GetCurrentRefireRate();

	UE_LOG(LogTemp, Verbose, TEXT("[Weapon:%s] StartFiring: TimeSinceLastShot=%.3f, RefireRate=%.3f, bFullAuto=%d, Owner=%s"),
		*GetName(), TimeSinceLastShot, CurrentRefireRate, bFullAuto,
		PawnOwner ? *PawnOwner->GetName() : TEXT("NULL"));

	if (TimeSinceLastShot > CurrentRefireRate)
	{
		// fire the weapon right away
		UE_LOG(LogTemp, Verbose, TEXT("[Weapon:%s]   -> Firing immediately"), *GetName());
		Fire();

	}
	else {

		// if we're full auto, schedule the next shot
		if (bFullAuto)
		{
			UE_LOG(LogTemp, Verbose, TEXT("[Weapon:%s]   -> Deferred (full auto): scheduling in %.3f sec"), *GetName(), TimeSinceLastShot);
			GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, TimeSinceLastShot, false);
		}
		else
		{
			UE_LOG(LogTemp, Verbose, TEXT("[Weapon:%s]   -> SKIPPED: not full auto and refire rate not met!"), *GetName());
		}

	}
}

void AShooterWeapon::StopFiring()
{
	const bool bHadPendingRefire = GetWorld()->GetTimerManager().IsTimerActive(RefireTimer);
	UE_LOG(LogTemp, Verbose, TEXT("[Weapon:%s] StopFiring: bIsFiring was %d, hadPendingRefire=%d"),
		*GetName(), bIsFiring, bHadPendingRefire);

	// lower the firing flag
	bIsFiring = false;

	// clear the refire timer
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
}

bool AShooterWeapon::OnSecondaryAction()
{
	return false;
}

void AShooterWeapon::OnSecondaryActionReleased()
{
}

void AShooterWeapon::FireOnce()
{
	// Single shot through the normal Fire() path (handles aim/ammo/charge/OnShotFired), then clear
	// the scheduled refire so the firing cadence is driven entirely by the animation notify.
	bIsFiring = true;
	Fire();
	StopFiring();
}

void AShooterWeapon::Fire()
{
	UE_LOG(LogTemp, Verbose, TEXT("[Weapon:%s] Fire() called: bIsFiring=%d, bUseChargeFiring=%d, WeaponOwner=%d"),
		*GetName(), bIsFiring, bUseChargeFiring, WeaponOwner != nullptr);

	// ensure the player still wants to fire. They may have let go of the trigger
	if (!bIsFiring)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[Weapon:%s]   Fire() ABORTED: bIsFiring is false!"), *GetName());
		return;
	}

	// A weapon with a real magazine cannot fire while it is being filled, and an empty one starts
	// filling itself rather than clicking forever. bIsFiring is deliberately left alone: holding the
	// trigger through a reload should resume fire when it finishes, which FinishReload does.
	if (UsesReload())
	{
		if (bIsReloading)
		{
			// A magazine reload cannot be walked out of: there is no half-swapped magazine, so the
			// trigger simply does nothing until it is over.
			//
			// A per round reload can, and that is the point of loading one round at a time. The
			// player takes the shot with three in the tube instead of watching the animation finish.
			// Only with something actually loaded, though: an empty gun interrupting its own reload
			// to dry fire would be a way to never reload at all.
			if (!bPerRoundReload || CurrentBullets <= 0)
			{
				return;
			}

			InterruptPerRoundReload();
		}

		if (CurrentBullets <= 0)
		{
			// Empty. That is all this is: the trigger was pulled and there was nothing to fire.
			// Turning it into a reload is a separate decision the weapon only makes if it was told
			// to -- otherwise the gun clicks and stays up, and the player chooses when to reload.
			if (DryFireSound)
			{
				UGameplayStatics::PlaySoundAtLocation(this, DryFireSound, GetActorLocation());
			}

			if (bReloadOnEmptyTriggerPull)
			{
				StartReload();
			}

			return;
		}
	}

	// Check charge requirements if enabled
	float ChargeMultiplier = 1.0f;
	if (bUseChargeFiring)
	{
		if (!TryConsumeCharge(ChargeMultiplier))
		{
			// Not enough charge - play dry fire click sound
			if (DryFireSound)
			{
				UGameplayStatics::PlaySoundAtLocation(this, DryFireSound, GetActorLocation());
			}
			UE_LOG(LogTemp, Verbose, TEXT("[Weapon:%s]   Fire() ABORTED: charge requirements not met!"), *GetName());
			StopFiring();
			return;
		}
	}

	if (!WeaponOwner)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[Weapon:%s]   Fire() ABORTED: WeaponOwner is NULL!"), *GetName());
		return;
	}

	UE_LOG(LogTemp, Verbose, TEXT("[Weapon:%s]   Fire() PROCEEDING: spawning effects, getting target..."), *GetName());

	// Muzzle flash and fire sound.
	//
	// Always play them here so the shooter sees and hears the shot at once, with no round trip.
	// Then tell everyone else: the effects used to be purely local, which is why a teammate's gun
	// fired in total silence with no tracer.
	// Tell the class passive that a shot is leaving, BEFORE any of this shot's damage is worked out.
	// Ordering is the whole point and it is not obvious: on the host, Fire() computes the damage and
	// only then broadcasts OnShotFired, while a client's shot reaches the server as two separate
	// RPCs with the fire report arriving FIRST. A passive that spends something on the shot can only
	// be right on both routes if the spending happens here, at the start, on every machine that
	// runs Fire. @see UAbilityHandler::OnOwnerFiredWeapon
	if (PawnOwner)
	{
		if (UAbilityComponent* Abilities = PawnOwner->FindComponentByClass<UAbilityComponent>())
		{
			Abilities->NotifyOwnerFiredWeapon();
		}
	}

	// Worked out here, before the round is spent: ConsumeRoundAfterShot runs later in this same call,
	// so CurrentBullets is still the count BEFORE this shot. One left means this shot empties it.
	// Only a weapon with a real magazine can run out; an energy weapon refills itself and never
	// locks its action back.
	const bool bLastRound = UsesReload() && CurrentBullets <= 1;

	PlayFireEffectsLocally(bLastRound);

	if (HasAuthority())
	{
		Multicast_PlayFireEffects(bLastRound);
	}
	else
	{
		// A client's shot reaches the server through AShooterCharacter::Server_ReportDamage, but a
		// missed shot has no damage to report, so the effects need their own path upstream.
		if (AShooterCharacter* OwnerCharacter = Cast<AShooterCharacter>(PawnOwner))
		{
			OwnerCharacter->Server_ReportWeaponFired(this, bLastRound);
		}
	}

	// Add heat from firing
	if (bUseHeatSystem)
	{
		AddHeat(HeatPerShot);
	}

	// Get target location
	const FVector TargetLocation = WeaponOwner->GetWeaponTargetLocation();

	UE_LOG(LogTemp, Verbose, TEXT("[Weapon:%s]   TargetLocation: (%.1f, %.1f, %.1f), bUseHitscan=%d"),
		*GetName(), TargetLocation.X, TargetLocation.Y, TargetLocation.Z, bUseHitscan);

	// Fire based on mode
	if (bUseHitscan)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[Weapon:%s]   >>> FireHitscan <<<"), *GetName());
		FireHitscan(TargetLocation);
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("[Weapon:%s]   >>> FireProjectile <<<"), *GetName());
		FireProjectile(TargetLocation, ChargeMultiplier);
	}

	// update the time of our last shot
	TimeOfLastShot = GetWorld()->GetTimeSeconds();
	UE_LOG(LogTemp, Verbose, TEXT("[Weapon:%s]   Shot complete. TimeOfLastShot=%.2f"), *GetName(), TimeOfLastShot);

	// One trigger pull opens the spread once. Deliberately here and not in the Fire* functions: a
	// shotgun puts several pellets in the air per pull, and charging them each with a full bloom
	// would make it the widest weapon in the game after two shots.
	AddShotSpread();

	// Notify listeners that a shot was fired (for NPC burst counting)
	OnShotFired.Broadcast();

	// make noise so the AI perception system can hear us
	MakeNoise(ShotLoudness, PawnOwner, PawnOwner->GetActorLocation(), ShotNoiseRange, ShotNoiseTag);

	// are we full auto?
	// Use current refire rate which factors in heat
	const float ActualRefireRate = GetCurrentRefireRate();

	if (bFullAuto)
	{
		// schedule the next shot
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, ActualRefireRate, false);
	}
	else {

		// for semi-auto weapons, schedule the cooldown notification
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::FireCooldownExpired, ActualRefireRate, false);

	}
}

void AShooterWeapon::FireCooldownExpired()
{
	// notify the owner
	WeaponOwner->OnSemiWeaponRefire();
}

AShooterProjectile* AShooterWeapon::SpawnProjectileAtTransform(const FTransform& ProjectileTransform,
	float ChargeMultiplier, bool bCosmeticOnly)
{
	AShooterProjectile* Projectile = nullptr;

	// Both copies come from the pool now, the real one included.
	//
	// The restriction that used to be here was a consequence of replication, not of pooling: a
	// pooled actor is reused rather than destroyed, so a client holding its channel open would have
	// watched it teleport back to a muzzle on the next shot. Nobody holds a channel on an
	// unreplicated round, so the objection is gone -- and a class that still replicates (AEMFProjectile)
	// opts out below, where it would otherwise hit exactly that problem.
	const AShooterProjectile* const ClassCDO = ProjectileClass
		? ProjectileClass->GetDefaultObject<AShooterProjectile>() : nullptr;
	const bool bClassReplicates = ClassCDO && ClassCDO->GetIsReplicated();

	UProjectilePoolSubsystem* Pool = (bClassReplicates || !AShooterProjectile::IsPoolingEnabled())
		? nullptr : GetWorld()->GetSubsystem<UProjectilePoolSubsystem>();
	// Who answers for this round. The pawn holding the gun, and when nothing is holding it (a
	// turret), whoever the mount named as the weapon's instigator: that is how a turret's kill
	// reaches its owner's tally (UXPSubsystem::WasKillCausedByPlayer walks the instigator).
	APawn* const RoundInstigator = PawnOwner ? PawnOwner : GetInstigator();
	if (Pool)
	{
		Projectile = Pool->GetProjectile(ProjectileClass, ProjectileTransform, GetOwner(), RoundInstigator);
	}
	else
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::OverrideRootScale;
		SpawnParams.Owner = GetOwner();
		SpawnParams.Instigator = RoundInstigator;
		Projectile = GetWorld()->SpawnActor<AShooterProjectile>(ProjectileClass, ProjectileTransform, SpawnParams);
	}

	if (Projectile && bCosmeticOnly)
	{
		Projectile->SetCosmeticOnly();
	}

	// Which gun this round came out of. Set here, before it can touch anything, because every rule
	// the hit obeys is read off the weapon: damage, shield gate, ionization, feedback set, upgrades.
	// Both routes come through this function, so the authority's projectile and the shooter's local
	// stand-in are told the same thing.
	if (Projectile)
	{
		Projectile->SetSourceWeapon(this);
	}

	// Everyone else's copy of this shot.
	//
	// The authority's round is invisible to other machines now, so the only thing that reaches them
	// is this: the same muzzle transform, and each client builds its own decoration from it. The
	// shooter is skipped inside the multicast because it already fired its own the instant it pulled
	// the trigger, and the authority is skipped because the round it is holding is the one it draws.
	//
	// Skipped entirely for a class that still replicates: those arrive on their own, and adding a
	// decoration on top would show every observer two rounds for one shot.
	if (!bCosmeticOnly && HasAuthority() && Projectile && !bClassReplicates)
	{
		Multicast_SpawnCosmeticProjectile(ProjectileTransform.GetLocation(),
			ProjectileTransform.GetRotation().GetForwardVector());
	}

	// If charge-based firing, scale projectile charge and match player polarity
	if (bUseChargeFiring && Projectile)
	{
		if (AEMFProjectile* EMFProj = Cast<AEMFProjectile>(Projectile))
		{
			// Get player's charge sign
			AActor* WeaponOwnerActor = GetOwner();
			if (WeaponOwnerActor)
			{
				UEMFVelocityModifier* EMFMod = WeaponOwnerActor->FindComponentByClass<UEMFVelocityModifier>();
				if (EMFMod)
				{
					float PlayerCharge = EMFMod->GetCharge();
					float PlayerSign = FMath::Sign(PlayerCharge);

					// Set projectile charge with same sign as player
					float BaseCharge = FMath::Abs(EMFProj->GetProjectileCharge());
					EMFProj->SetProjectileCharge(PlayerSign * BaseCharge * ChargeMultiplier);

					UE_LOG(LogTemp, Log, TEXT("ShooterWeapon: Projectile charge set to %.2f (player sign: %.0f, multiplier: %.2f)"),
						PlayerSign * BaseCharge * ChargeMultiplier, PlayerSign, ChargeMultiplier);
				}
			}
		}
	}

	return Projectile;
}

void AShooterWeapon::FireProjectile(const FVector& TargetLocation, float ChargeMultiplier)
{
	const FTransform ProjectileTransform = CalculateProjectileSpawnTransform(TargetLocation);

	if (HasAuthority())
	{
		SpawnProjectileAtTransform(ProjectileTransform, ChargeMultiplier, /*bCosmeticOnly*/ false);
	}
	else
	{
		// The shot leaves the barrel now, on this screen, and asks the server for the real one in the
		// same breath. Waiting for the round trip instead would put half a ping between the trigger
		// and the projectile, which is the one thing a player notices immediately.
		SpawnProjectileAtTransform(ProjectileTransform, ChargeMultiplier, /*bCosmeticOnly*/ true);

		if (AShooterCharacter* OwnerCharacter = Cast<AShooterCharacter>(PawnOwner))
		{
			OwnerCharacter->Server_FireProjectile(this, ProjectileTransform, ChargeMultiplier);
		}
	}

	ConsumeRoundAfterShot();
}

FVector AShooterWeapon::GetMuzzleWorldLocation() const
{
	// Same mesh choice the projectile spawn makes, so "where the shot comes from" cannot mean two
	// different places depending on who asks.
	USkeletalMeshComponent* const MuzzleMesh =
		(PawnOwner && PawnOwner->IsPlayerControlled()) ? FirstPersonMesh : ThirdPersonMesh;

	if (MuzzleMesh && MuzzleMesh->DoesSocketExist(MuzzleSocketName))
	{
		return MuzzleMesh->GetSocketLocation(MuzzleSocketName);
	}

	return GetActorLocation();
}

bool AShooterWeapon::CanShotReach(const FVector& TargetLocation) const
{
	const UWorld* const World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector Start = GetMuzzleWorldLocation();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShotClearance), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(this);
	if (PawnOwner)
	{
		Params.AddIgnoredActor(PawnOwner);
	}

	// The shell's own radius, taken from the class that will fly. Zero for hitscan, which turns the
	// sweep below back into the line trace it used to be.
	float ShotRadius = 0.0f;
	if (!bUseHitscan && ProjectileClass)
	{
		if (const AShooterProjectile* const ProjectileCDO = ProjectileClass->GetDefaultObject<AShooterProjectile>())
		{
			if (const USphereComponent* const Collision = ProjectileCDO->FindComponentByClass<USphereComponent>())
			{
				ShotRadius = Collision->GetUnscaledSphereRadius();
			}
		}
	}

	FHitResult Blocked;

	if (ShotRadius > KINDA_SMALL_NUMBER)
	{
		return !World->SweepSingleByChannel(Blocked, Start, TargetLocation, FQuat::Identity,
			ECC_Visibility, FCollisionShape::MakeSphere(ShotRadius), Params);
	}

	return !World->LineTraceSingleByChannel(Blocked, Start, TargetLocation, ECC_Visibility, Params);
}

FVector AShooterWeapon::SolveBallisticAim(const FVector& LaunchLocation, const FVector& TargetLocation) const
{
	// Players are left alone. See the header: correcting their aim is aim assist, not ballistics.
	// Everything else, an AI pawn or a turret with no pawn at all, is handed the solved arc.
	if (PawnOwner && PawnOwner->IsPlayerControlled())
	{
		return FVector::ZeroVector;
	}

	if (!ProjectileClass)
	{
		return FVector::ZeroVector;
	}

	// Asked of the projectile class itself rather than configured on the weapon. The two numbers
	// that decide whether a shot needs an arc - does it fall, and how fast does it go - already live
	// on the thing that flies, and a second copy on the weapon is a second copy to get wrong.
	const AShooterProjectile* const ProjectileCDO = ProjectileClass->GetDefaultObject<AShooterProjectile>();
	if (!ProjectileCDO)
	{
		return FVector::ZeroVector;
	}

	// Found on the CDO rather than read off a member: AShooterProjectile keeps its movement
	// component protected, and a getter added purely so the weapon could peek at two numbers would
	// be public API bought for one caller. The default subobject is on the CDO exactly as it will be
	// on the spawned actor, so this reads the same values the projectile will fly with.
	const UProjectileMovementComponent* const ProjectileMove =
		ProjectileCDO->FindComponentByClass<UProjectileMovementComponent>();
	if (!ProjectileMove)
	{
		return FVector::ZeroVector;
	}

	const float GravityScale = ProjectileMove->ProjectileGravityScale;
	const float LaunchSpeed = ProjectileMove->InitialSpeed;

	// No gravity means the straight line already hits. This is the whole switch: a rocket configured
	// to fly flat never enters the solver, and nothing anywhere has to be told that it is a rocket.
	if (FMath::IsNearlyZero(GravityScale) || LaunchSpeed <= KINDA_SMALL_NUMBER)
	{
		return FVector::ZeroVector;
	}

	const UWorld* const World = GetWorld();
	if (!World)
	{
		return FVector::ZeroVector;
	}

	const float GravityZ = World->GetGravityZ() * GravityScale;

	// Aim at the BODY, not at the aim point that was passed in.
	//
	// The caller's TargetLocation comes from GetWeaponTargetLocation, which for an AI is the far end
	// of an accuracy-spread ray traced out to AimRange - and pawns do not block that channel here,
	// so it lands on a wall behind the enemy or in open space thousands of units past them. Solving
	// an arc onto that point yields the launch angle that REACHES that point, so the shell flies
	// over the target and comes down somewhere in the distance. Every "стреляет в воздух далеко за
	// игрока" was this, and every "стреляет в стену" was the same ray ending on a wall instead.
	//
	// A hitscan does not care, because it only wants the direction and stops at the first thing it
	// hits. A ballistic shot is the one caller that needs the real distance.
	FVector BodyLocation = TargetLocation;
	if (const AActor* const AimActor = WeaponOwner ? WeaponOwner->GetWeaponAimActor() : nullptr)
	{
		// Only the DISTANCE comes from the body. The direction stays the one that was passed in:
		// that ray already carries the NPC's accuracy spread (UAIAccuracyComponent) and the
		// weapon's own cone, and taking the body's location outright threw both away, so every
		// ballistic shot solved straight into the centre of the capsule no matter what the spread
		// was set to. That was "сбривают с средней дистанции" at 15 degrees of BaseSpread.
		const float BodyReach = FVector::Dist(LaunchLocation, AimActor->GetActorLocation());
		const FVector AimRay = (TargetLocation - LaunchLocation).GetSafeNormal();
		BodyLocation = AimRay.IsNearlyZero()
			? AimActor->GetActorLocation()
			: LaunchLocation + AimRay * BodyReach;

		// Where the target WILL be. A projectile that takes half a second to arrive and is aimed at
		// where somebody is standing hits the ground they left, and at sniper ranges that is every
		// shot at anybody moving at all.
		//
		// Only AI ever gets here: GetWeaponAimActor is the AI aim path, and a player aims with the
		// mouse. So this is a property of the shooter, not of the gun.
		const FVector TargetVelocity = AimActor->GetVelocity();
		if (AILeadFraction > 0.0f && !TargetVelocity.IsNearlyZero())
		{
			const float Reach = FVector::Dist(LaunchLocation, BodyLocation);
			const float Flight = FMath::Min(Reach / LaunchSpeed, AIMaxLeadSeconds);

			FVector Lead = TargetVelocity * (Flight * AILeadFraction);
			Lead.Z = 0.0f;   // jumps reverse inside the flight time; leading them aims at the floor

			// The error rides on the lead, so it is zero against a standing target and largest
			// against a sprinting one. That is the shape that keeps movement worth doing.
			if (AILeadErrorFraction > 0.0f)
			{
				FVector Scatter = FMath::VRand() * (Lead.Size() * AILeadErrorFraction);
				Scatter.Z = 0.0f;
				Lead += Scatter;
			}

			BodyLocation += Lead;
		}
	}

	if (bLogBallistics)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BALLISTIC_DEBUG] %s solve: passedTarget=%s bodyTarget=%s dist=%.0f speed=%.0f gravScale=%.2f lob=%d"),
			*GetNameSafe(PawnOwner), *TargetLocation.ToCompactString(), *BodyLocation.ToCompactString(),
			FVector::Dist(LaunchLocation, BodyLocation), LaunchSpeed, GravityScale, bLobProjectiles ? 1 : 0);
	}

	// The engine's own solver, and the reason not to write one: it takes the speed as FIXED, which
	// is the constraint that actually applies here. AShooterProjectile launches at InitialSpeed
	// along its spawn rotation, so the only free variable is the angle - exactly the problem
	// SuggestProjectileVelocity solves, and it answers false when the target is simply out of range
	// rather than returning a direction that quietly falls short.
	UGameplayStatics::FSuggestProjectileVelocityParameters SolverParams(
		this, LaunchLocation, BodyLocation, LaunchSpeed);

	SolverParams.bFavorHighArc = bLobProjectiles;
	SolverParams.OverrideGravityZ = GravityZ;

	// No trace along the path. The solver's tracing modes are for picking a trajectory that misses
	// the scenery, which is a different question from "what angle reaches this point" and costs a
	// sweep of the whole arc per shot. Whether the shell clears the wall in front of it is already
	// decided by the cover the NPC chose to fire from.
	SolverParams.TraceOption = ESuggestProjVelocityTraceOption::DoNotTrace;

	FVector TossVelocity = FVector::ZeroVector;
	bool bSolved = UGameplayStatics::SuggestProjectileVelocity(SolverParams, TossVelocity);

	// A high arc only means "lob" while the projectile is slow enough for the range to be a real
	// constraint. Give the solver far more speed than the shot needs and the high solution walks
	// towards vertical, because straight up and straight down also reaches a target a thousand units
	// away - so a 3000 u/s shell fired at a nearby corner came out as a mortar round aimed at the
	// sky, which is what "стреляют вверх в воздух" was.
	//
	// So the lob is a preference, not an instruction: if the high answer comes out steeper than a
	// shot anybody would recognise, take the flat one instead. The flat solution still arcs - the
	// projectile still falls - it simply stops pretending the weapon is artillery.
	if (bSolved && bLobProjectiles)
	{
		const float LaunchPitchDeg = FMath::RadiansToDegrees(FMath::Asin(
			FMath::Clamp(TossVelocity.GetSafeNormal().Z, -1.0f, 1.0f)));

		if (LaunchPitchDeg > MaxLobPitchDegrees)
		{
			SolverParams.bFavorHighArc = false;
			bSolved = UGameplayStatics::SuggestProjectileVelocity(SolverParams, TossVelocity);
		}
	}

	// Out of range. Falling back to the straight line is deliberate: the shot still leaves, still
	// lands somewhere short, and still makes noise and splash. An enemy that silently refuses to
	// fire at a target it can see reads as broken, and the splash radius is generous enough that a
	// short round is not a wasted one.
	if (!bSolved)
	{
		return FVector::ZeroVector;
	}

	const FVector LaunchDir = TossVelocity.GetSafeNormal();

	if (bLogBallistics)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BALLISTIC_DEBUG] %s launch pitch %.1f deg"),
			*GetNameSafe(PawnOwner),
			FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(LaunchDir.Z, -1.0f, 1.0f))));
	}

	// Is there room to launch at all. An arc leaves the muzzle at an angle, so a shooter tucked under
	// a lintel or hugging the inside of a corner can have a clean line to the target and still put
	// the shell into the ceiling half a metre up - which looks exactly like the enemy shooting the
	// wall for no reason.
	//
	// Short trace, because this is asking "can the round get out", not "does the whole trajectory
	// clear". The rest of the flight is the arc's business.
	if (const UWorld* const TraceWorld = GetWorld())
	{
		FCollisionQueryParams ClearanceParams(SCENE_QUERY_STAT(BallisticMuzzleClearance), /*bTraceComplex*/ false);
		ClearanceParams.AddIgnoredActor(this);
		ClearanceParams.AddIgnoredActor(PawnOwner);

		FHitResult Blocked;
		if (TraceWorld->LineTraceSingleByChannel(Blocked, LaunchLocation,
			LaunchLocation + LaunchDir * MuzzleClearanceDistance, ECC_Visibility, ClearanceParams))
		{
			// Blocked. Falling back to the straight line rather than refusing the shot: the straight
			// line is what this weapon did before ballistics existed, it is aimed at something the
			// NPC can actually see, and a silent enemy reads worse than a low round.
			if (bLogBallistics)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[BALLISTIC_DEBUG] %s muzzle BLOCKED by %s -> straight line"),
					*GetNameSafe(PawnOwner), *GetNameSafe(Blocked.GetActor()));
			}

			return FVector::ZeroVector;
		}
	}

	return LaunchDir;
}

FTransform AShooterWeapon::CalculateProjectileSpawnTransform(const FVector& TargetLocation) const
{
	// Use ThirdPersonMesh for NPCs, FirstPersonMesh for players
	USkeletalMeshComponent* MuzzleMesh = (PawnOwner && PawnOwner->IsPlayerControlled()) ? FirstPersonMesh : ThirdPersonMesh;

	// find the muzzle location
	const FVector MuzzleLoc = MuzzleMesh->GetSocketLocation(MuzzleSocketName);

	// calculate the spawn location ahead of the muzzle
	const FVector SpawnLoc = MuzzleLoc + ((TargetLocation - MuzzleLoc).GetSafeNormal() * MuzzleOffset);

	// Turn the aim line inside the spread cone. This used to nudge the TARGET POINT by AimVariance
	// centimetres, which is not a spread at all: the same number was a wide scatter at arm's length
	// and nothing at range, and it could not agree with a crosshair drawn from an angle. The cone is
	// the same one the hitscan path uses, so a projectile weapon and a hitscan weapon of equal
	// spread now shoot equally wide.
	//
	// The straight line is the PLAYER's answer and the fallback. An AI firing a projectile that
	// falls gets the ballistic one instead, below.
	const float ConeDegrees = GetAimConeDegrees();

	// Ballistic shots scatter their TARGET, everything else scatters its DIRECTION, and the two are
	// not interchangeable once gravity is involved.
	//
	// Range off a ballistic launch goes as sin(2*theta), so pitch error turns into range error
	// nonlinearly and brutally. Measured from this log: a 2750 unit shot at 3300 u/s solves to a
	// 7.2 degree launch, and tilting that by five degrees of ordinary cone spread lands the round at
	//
	//     2.2 deg -> 852 units        12.2 deg -> 4591 units
	//
	// against a target at 2750. The short end of that is not a miss, it is the shell going off
	// practically at the shooter's feet - in the corner it just leaned out of. Every "стреляет в
	// стену" that survived the aim fix was one of those short rounds.
	//
	// Displacing the aim POINT instead keeps the range honest: the arc is solved to wherever the
	// scatter put the point, so the round always travels about the right distance and lands around
	// the target rather than somewhere between here and there. The cone is then deliberately NOT
	// applied on top - it has already been spent.
	FVector AimDir = FVector::ZeroVector;
	bool bUsedBallistic = false;

	{
		FVector ScatteredTarget = TargetLocation;
		if (ConeDegrees > 0.0f)
		{
			// The cone half-angle read as a lateral offset at this range, which is the same spread a
			// hitscan of equal cone would show on the same target.
			const float Reach = FVector::Dist(SpawnLoc, TargetLocation);
			const float Radius = Reach * FMath::Tan(FMath::DegreesToRadians(ConeDegrees));

			ScatteredTarget += FMath::VRand() * FMath::FRand() * Radius;
		}

		if (const FVector BallisticDir = SolveBallisticAim(SpawnLoc, ScatteredTarget); !BallisticDir.IsNearlyZero())
		{
			AimDir = BallisticDir;
			bUsedBallistic = true;
		}
	}

	if (!bUsedBallistic)
	{
		// The straight line is the PLAYER's answer and the fallback for anything that does not fall.
		// Here the cone is the right tool, for the reason the original comment gives: it is
		// range-independent, unlike nudging a point by a fixed number of centimetres.
		const FVector StraightDir = (TargetLocation - SpawnLoc).GetSafeNormal();
		AimDir = (ConeDegrees > 0.0f)
			? UKismetMathLibrary::RandomUnitVectorInConeInDegrees(StraightDir, ConeDegrees)
			: StraightDir;
	}

	const FRotator AimRot = AimDir.Rotation();

	// return the built transform
	return FTransform(AimRot, SpawnLoc, FVector::OneVector);
}

// ==================== Hitscan Implementation ====================

void AShooterWeapon::ResolveHitscanRay(const FVector& TargetLocation, FVector& OutStart, FVector& OutDirection) const
{
	// Use ThirdPersonMesh for NPCs, FirstPersonMesh for players
	USkeletalMeshComponent* MuzzleMesh = (PawnOwner && PawnOwner->IsPlayerControlled()) ? FirstPersonMesh : ThirdPersonMesh;
	const FVector MuzzleLocation = MuzzleMesh->GetSocketLocation(MuzzleSocketName);

	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°)
	FVector ViewDir = FVector::ForwardVector;
	FVector ViewLocation = MuzzleLocation; // Fallback

	if (PawnOwner)
	{
		ViewDir = PawnOwner->GetBaseAimRotation().Vector();
		ViewLocation = PawnOwner->GetPawnViewLocation();
	}

	// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸, ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°)
	FVector ToTargetVector = TargetLocation - MuzzleLocation;
	float DistanceToTarget = ToTargetVector.Size();
	FVector ToTargetDir = ToTargetVector.GetSafeNormal();

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
	// 1.0 = ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾, 0.0 = 90 ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â², -1.0 = ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´
	float DotP = FVector::DotProduct(ToTargetDir, ViewDir);

	// === ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â§ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ ===

	// 1. ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¯ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â (ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â) - ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â· ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
	//DrawDebugLine(GetWorld(), ViewLocation, TargetLocation, FColor::Green, false, 3.0f, 0, 1.0f);

	// 2. ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ "ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾" ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â«ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â (ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â) - ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
	//DrawDebugLine(GetWorld(), MuzzleLocation, TargetLocation, FColor::Red, false, 3.0f, 0, 1.0f);

	// 3. ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°), ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢, ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡
	//DrawDebugSphere(GetWorld(), MuzzleLocation, 10.0f, 12, FColor::Blue, false, 3.0f);

	// 4. ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½
	FString DebugMsg = FString::Printf(TEXT("Dist: %.1f | Dot: %.3f | Fix Applied: %s"),
		DistanceToTarget,
		DotP,
		(DistanceToTarget < 100.0f || DotP < 0.5f) ? TEXT("YES") : TEXT("NO"));

	//GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow, DebugMsg);

	// === ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¦ ===

	FVector Direction;

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â£ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸:
	// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ 100 ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ (1 ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬) ÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ (< 0.5 ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ 60 ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²)
	if (DistanceToTarget < 100.0f || DotP < 0.5f)
	{
		// FIX: ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹
		Direction = ViewDir;
		//UE_LOG(LogTemp, Warning, TEXT("FireHitscan: FIXED direction used (Too close or bad angle)"));
	}
	else
	{
		// STANDARD: ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âº ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		Direction = ToTargetDir;
	}

	// === ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¤ÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ===
	// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ 2 ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
	//DrawDebugLine(GetWorld(), MuzzleLocation, MuzzleLocation + (Direction * 200.0f), FColor::Yellow, false, 3.0f, 0, 2.0f);


	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â (AimVariance)
	// === Classic hitscan (WaveDivergence == 0): trace from the CAMERA, not the muzzle ===
	// The aim point from GetWeaponTargetLocation() lies BEHIND the enemy (pawn profiles ignore
	// ECC_Visibility), so a muzzle-based ray keeps up to the full camera->muzzle offset of
	// parallax at the enemy's depth — a thin ray can miss a capsule the crosshair is dead on.
	// Re-basing to the camera viewpoint makes crosshair == bullet path; the tracer is still
	// drawn from the muzzle (see PerformClassicHitscan).
	FVector HitscanStart = MuzzleLocation;
	const bool bClassicHitscan = (WaveDivergence * MaxDivergenceAngle <= KINDA_SMALL_NUMBER);
	if (bClassicHitscan && PawnOwner && PawnOwner->IsPlayerControlled())
	{
		if (AController* OwnerController = PawnOwner->GetController())
		{
			FVector ViewPointLoc;
			FRotator ViewPointRot;
			OwnerController->GetPlayerViewPoint(ViewPointLoc, ViewPointRot);

			const FVector RebasedDir = (TargetLocation - ViewPointLoc).GetSafeNormal();
			HitscanStart = ViewPointLoc;
			Direction = RebasedDir.IsNearlyZero() ? ViewPointRot.Vector() : RebasedDir;
		}
	}

	// Spread: turn the aim line inside a cone of the weapon's CURRENT spread, not its authored base
	// value. A uniform direction in the cone rather than a uniform vector added to it, so the shot
	// is even across the circle instead of piling up in the middle.
	const float AimConeDegrees = GetAimConeDegrees();
	if (AimConeDegrees > 0.0f)
	{
		Direction = UKismetMathLibrary::RandomUnitVectorInConeInDegrees(Direction, AimConeDegrees);
	}

	// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»
	// [HITSCAN_DEBUG] Inputs of the shot: where the muzzle is, where the camera-aim point landed,
	// and how far it is. AimDist ~= MaxAimDistance means the aim trace hit NOTHING (open area) —
	// worst case for muzzle parallax.
	UE_LOG(LogTemp, Verbose, TEXT("[HITSCAN_DEBUG] FireHitscan: Muzzle=%s AimPoint=%s AimDist=%.0f DotP=%.3f dirMode=%s"),
		*MuzzleLocation.ToCompactString(), *TargetLocation.ToCompactString(), DistanceToTarget, DotP,
		(DistanceToTarget < 100.0f || DotP < 0.5f) ? TEXT("ViewDir(override)") : TEXT("Muzzle->AimPoint"));

	OutStart = HitscanStart;
	OutDirection = Direction;
}

FVector AShooterWeapon::GetFirstPersonMuzzleRenderLocation() const
{
	const FVector MuzzleWorld = FirstPersonMesh
		? FirstPersonMesh->GetSocketLocation(MuzzleSocketName)
		: GetActorLocation();

	const APlayerController* PC = PawnOwner ? Cast<APlayerController>(PawnOwner->GetController()) : nullptr;
	if (!PC || !PC->PlayerCameraManager)
	{
		return MuzzleWorld;
	}

	const FMinimalViewInfo& POV = PC->PlayerCameraManager->GetCameraCacheView();

	const float Scale = (POV.FirstPersonScale > 0.0f) ? POV.FirstPersonScale : 1.0f;
	const float FOVCorrection = POV.CalculateFirstPersonFOVCorrectionFactor();

	// Nothing to correct: the weapon is drawn with the same field of view as the world and at its
	// own size, so where it stands is where it is seen.
	if (FMath::IsNearlyEqual(Scale, 1.0f) && FMath::IsNearlyEqual(FOVCorrection, 1.0f))
	{
		return MuzzleWorld;
	}

	const FQuat ViewRotation = POV.Rotation.Quaternion();
	const FVector Forward = ViewRotation.GetForwardVector();
	const FVector Right = ViewRotation.GetRightVector();
	const FVector Up = ViewRotation.GetUpVector();

	const FVector Relative = MuzzleWorld - POV.Location;

	// The renderer's ScaleVector, rebuilt in world space: depth takes the plain scale, the screen
	// plane takes the FOV correction on top of it.
	const double Depth = FVector::DotProduct(Relative, Forward) * Scale;
	const double Lateral = FVector::DotProduct(Relative, Right) * Scale * FOVCorrection;
	const double Vertical = FVector::DotProduct(Relative, Up) * Scale * FOVCorrection;

	return POV.Location + Forward * Depth + Right * Lateral + Up * Vertical;
}

void AShooterWeapon::FireHitscan(const FVector& TargetLocation)
{
	FVector Start;
	FVector Direction;
	ResolveHitscanRay(TargetLocation, Start, Direction);

	// NPC: simple line trace instead of cone hitscan.
	// The cone system was designed for the player (camera and muzzle co-located in FPS).
	// For NPCs, camera (eyes) and muzzle (weapon) have ~40-50u parallax offset,
	// exceeding the 5deg cone half-angle, causing valid hits to be rejected.
	if (PawnOwner && !PawnOwner->IsPlayerControlled())
	{
		PerformSimpleHitscan(Start, Direction, 1.0f);
	}
	else
	{
		PerformHitscan(Start, Direction, 1.0f, 0);
	}

	ConsumeRoundAfterShot();
}

void AShooterWeapon::ConsumeRoundAfterShot()
{

	//ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
	WeaponOwner->PlayFiringMontage(FiringMontage);

	// The hands working the action: the bolt on a Kar98K, the pump on a KXG12. Only manual weapons
	// have one, and having one is what makes a weapon manual -- GetCurrentRefireRate takes this
	// montage's length as a floor, so the gun cannot fire again until the bolt is closed.
	//
	// After PlayFiringMontage rather than instead of it: they are different slots on purpose, so a
	// weapon can keep a firing flourish and still cycle.
	if (CycleActionMontage)
	{
		WeaponOwner->PlayReloadMontage(CycleActionMontage);
	}

	WeaponOwner->AddWeaponRecoil(FiringRecoil);

	--CurrentBullets;

	// The round leaves the magazine cells too. The cells hold everything the player owns for this
	// gun and the magazine is the loaded part of it, so a shot has to come off both or the reserve
	// would never move. Server side only; the client's own copy arrives by replication.
	SpendPooledRound();

	if (CurrentBullets <= 0)
	{
		// An empty gun is just empty. It used to throw itself away here when it was a yanked
		// weapon, which is now wrong: a dropped gun keeps its rounds and can be filled from another
		// one of its kind, so running dry is a reason to click, not to lose the weapon.
		if (UsesReload())
		{
			// The magazine is real: it stays empty until somebody fills it, and by default that
			// somebody is the player. This branch is the weapon taking that decision for them, which
			// it only does when explicitly told to.
			if (bAutoReloadWhenEmpty)
			{
				StartReload();
			}
		}
		else
		{
			CurrentBullets = MagazineSize;  // Auto-refill: starter weapons, NPC drops, NPC owners
		}
	}

	WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
}

// ========================================
// DEBUG: ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
// (compile-time DEBUG_CONE_HITSCAN replaced by the runtime bDrawHitscanDebug weapon flag)
// ========================================

void AShooterWeapon::PerformHitscan(const FVector& Start, const FVector& Direction, float RemainingEnergy, int32 ReflectionCount)
{
	// Zero divergence: the cone math degenerates and its filter rejects legitimate hits —
	// route to the classic thin-ray path (see PerformClassicHitscan for details).
	if (WaveDivergence * MaxDivergenceAngle <= KINDA_SMALL_NUMBER)
	{
		PerformClassicHitscan(Start, Direction, RemainingEnergy, ReflectionCount);
		return;
	}

	float SegmentMaxDistance = MaxHitscanRange * RemainingEnergy;
	FVector End = Start + Direction * SegmentMaxDistance;

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â² ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦)
	float DivergenceAngle = WaveDivergence * MaxDivergenceAngle;
	float ConeHalfAngleRad = FMath::DegreesToRadians(DivergenceAngle);
	float CosHalfAngle = FMath::Cos(ConeHalfAngleRad);

	// ===== ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¨ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ 1: Line trace ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹) =====
	FHitResult WallHitResult;
	FCollisionQueryParams WallQueryParams;
	WallQueryParams.AddIgnoredActor(this);
	WallQueryParams.AddIgnoredActor(GetOwner());
	WallQueryParams.bReturnPhysicalMaterial = true;

	// Используем ECC_Visibility channel вместо ObjectType - 
	// он корректно учитывает collision responses и игнорирует triggers/overlaps
	bool bHitWall = GetWorld()->LineTraceSingleByChannel(
		WallHitResult,
		Start,
		End,
		ECC_Visibility,
		WallQueryParams
	);

	float MaxDistance = bHitWall ? WallHitResult.Distance : SegmentMaxDistance;
	FVector BeamEnd = bHitWall ? WallHitResult.ImpactPoint : End;

	// [HITSCAN_DEBUG] Shot summary: divergence + sweep sphere radius + what the Visibility (wall) trace hit.
	// SweepR is the sphere radius used by the pawn sweep below — if it's tiny (divergence ~0) the
	// sweep behaves like a thin ray and muzzle parallax can make it miss entirely.
	UE_LOG(LogTemp, Verbose, TEXT("[HITSCAN_DEBUG] === Shot: Start=%s Dir=%s | Diverg=%.2fdeg SweepR=%.1f MaxDist=%.0f | Wall=%s comp=%s dist=%.0f"),
		*Start.ToCompactString(), *Direction.ToCompactString(),
		DivergenceAngle, CalculateWaveRadius(MaxDistance), MaxDistance,
		bHitWall ? *GetNameSafe(WallHitResult.GetActor()) : TEXT("none"),
		bHitWall ? *GetNameSafe(WallHitResult.GetComponent()) : TEXT("-"),
		bHitWall ? WallHitResult.Distance : SegmentMaxDistance);

	// DEBUG: Log what the Visibility line trace hit (helps diagnose EMFPhysicsProp hits)
	if (bHitWall && WallHitResult.GetActor())
	{
		UE_LOG(LogTemp, Verbose, TEXT("[Hitscan DEBUG] Visibility trace hit: %s (Class: %s) at dist=%.0f"),
			*WallHitResult.GetActor()->GetName(),
			*WallHitResult.GetActor()->GetClass()->GetName(),
			WallHitResult.Distance);
	}

	// Direct damage to non-Pawn physics actors hit by Visibility trace (e.g. EMFPhysicsProp).
	// The cone sweep only queries ECC_Pawn, so PhysicsActor objects are invisible to it.
	// Apply damage via the existing ApplyHitscanDamage path for these actors.
	if (bHitWall && WallHitResult.GetActor() && !Cast<APawn>(WallHitResult.GetActor()))
	{
		AActor* WallActor = WallHitResult.GetActor();
		if (WallActor->CanBeDamaged())
		{
			ApplyHitscanDamage(WallHitResult, RemainingEnergy, WallHitResult.Distance, 0.0f);
		}
	}

	if (bDrawHitscanDebug)
	{
	// ===== DEBUG: ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° =====
	const float DebugDuration = 2.0f;
	const bool bPersistent = false;

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â)
	DrawDebugLine(GetWorld(), Start, BeamEnd, FColor::Green, bPersistent, DebugDuration, 0, 2.0f);

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° (ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°)
	DrawDebugSphere(GetWorld(), Start, 5.0f, 8, FColor::Blue, bPersistent, DebugDuration);

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°, ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦)
	DrawDebugSphere(GetWorld(), BeamEnd, 10.0f, 8, bHitWall ? FColor::Red : FColor::Green, bPersistent, DebugDuration);

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
	const int32 NumConeLines = 16; // ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
	FVector Right = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal();
	if (Right.IsNearlyZero())
	{
		Right = FVector::CrossProduct(Direction, FVector::RightVector).GetSafeNormal();
	}
	FVector Up = FVector::CrossProduct(Right, Direction).GetSafeNormal();

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦
	TArray<float> DebugDistances = { 100.0f, 500.0f, 1000.0f, MaxDistance * 0.5f, MaxDistance };

	for (float DebugDist : DebugDistances)
	{
		if (DebugDist > MaxDistance) continue;

		float ConeRadius = CalculateWaveRadius(DebugDist);
		FVector ConeCenter = Start + Direction * DebugDist;

		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		FVector PrevPoint = ConeCenter + Right * ConeRadius;
		for (int32 i = 1; i <= NumConeLines; i++)
		{
			float Angle = (float)i / (float)NumConeLines * 2.0f * PI;
			FVector PointOnCircle = ConeCenter + (Right * FMath::Cos(Angle) + Up * FMath::Sin(Angle)) * ConeRadius;

			// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â)
			DrawDebugLine(GetWorld(), PrevPoint, PointOnCircle, FColor::Yellow, bPersistent, DebugDuration, 0, 1.0f);

			PrevPoint = PointOnCircle;
		}

		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âº ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ 4-ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½)
		for (int32 i = 0; i < NumConeLines; i += 4)
		{
			float Angle = (float)i / (float)NumConeLines * 2.0f * PI;
			FVector PointOnCircle = ConeCenter + (Right * FMath::Cos(Angle) + Up * FMath::Sin(Angle)) * ConeRadius;

			// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ)
			DrawDebugLine(GetWorld(), Start, PointOnCircle, FColor::Orange, bPersistent, DebugDuration, 0, 0.5f);
		}

		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ (wireframe)
		DrawDebugCircle(GetWorld(), ConeCenter, ConeRadius, 32, FColor::Cyan, bPersistent, DebugDuration, 0, 1.0f, Up, Right, false);
	}
	// ===== END DEBUG =====
	}

	// ===== ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¨ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ 2: Multi Sweep ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¥ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° =====
	// ÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
	TArray<FHitResult> SweepHits;
	TArray<AActor*> HitTargets;

	FCollisionQueryParams SweepQueryParams;
	SweepQueryParams.AddIgnoredActor(this);
	SweepQueryParams.AddIgnoredActor(GetOwner());
	SweepQueryParams.bReturnPhysicalMaterial = true;

	FCollisionObjectQueryParams PawnObjectParams;
	PawnObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â sweep = ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
	float MaxConeRadius = CalculateWaveRadius(MaxDistance);

	// Multi sweep ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â
	GetWorld()->SweepMultiByObjectType(
		SweepHits,
		Start,
		BeamEnd,
		FQuat::Identity,
		PawnObjectParams,
		FCollisionShape::MakeSphere(MaxConeRadius),
		SweepQueryParams
	);

	UE_LOG(LogTemp, Verbose, TEXT("Cone Hitscan: Sweep found %d hits, MaxRadius=%.1f, MaxDist=%.0f, Angle=%.1f"),
		SweepHits.Num(), MaxConeRadius, MaxDistance, DivergenceAngle);

	// ===== ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¨ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ 3: ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¤ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° =====
	// Best target tracking (single-target: only damage the most central enemy)
	AActor* BestTarget = nullptr;
	FHitResult BestHit;
	FVector BestHitLocation = FVector::ZeroVector;
	float BestHitDistance = 0.0f;
	float BestAngle = MAX_FLT;
	bool bBestIsHeadshot = false;
	FVector BestToHitDir = FVector::ZeroVector;

	for (const FHitResult& Hit : SweepHits)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || HitTargets.Contains(HitActor))
		{
			continue;
		}

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ
		FVector HitLocation = Hit.ImpactPoint;
		float HitDistance = Hit.Distance;

		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â sweep ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ Distance ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ 0 ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
		if (HitDistance < 1.0f)
		{
			HitDistance = FVector::Dist(Start, HitActor->GetActorLocation());
			HitLocation = HitActor->GetActorLocation();
		}

		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âº ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â
		FVector ToHit = HitLocation - Start;
		FVector ToHitDir = ToHit.GetSafeNormal();

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» - ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
		float DotProduct = FVector::DotProduct(Direction, ToHitDir);
		float AngleToHit = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(DotProduct, -1.0f, 1.0f)));

		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		float ConeRadiusAtDistance = CalculateWaveRadius(HitDistance);

		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â
		FVector PointOnAxis = Start + Direction * HitDistance;
		float DistanceFromAxis = FVector::Dist(HitLocation, PointOnAxis);

		UE_LOG(LogTemp, Verbose, TEXT("  - %s: Dist=%.0f, Angle=%.1fÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â°, DistFromAxis=%.1f, ConeRadius=%.1f"),
			*HitActor->GetName(), HitDistance, AngleToHit, DistanceFromAxis, ConeRadiusAtDistance);

		// [HITSCAN_DEBUG] Full candidate info: which component was swept (capsule vs mesh), bone,
		// raw sweep distance (can be << real distance for fat sweep spheres) and both cone-filter inputs.
		UE_LOG(LogTemp, Verbose, TEXT("[HITSCAN_DEBUG]   cand=%s comp=%s bone=%s | rawDist=%.0f fixDist=%.0f | dot=%.4f cosHalf=%.4f | axisDist=%.1f coneR=%.1f"),
			*HitActor->GetName(), *GetNameSafe(Hit.GetComponent()), *Hit.BoneName.ToString(),
			Hit.Distance, HitDistance, DotProduct, CosHalfAngle, DistanceFromAxis, ConeRadiusAtDistance);

	if (bDrawHitscanDebug)
	{
		// ===== DEBUG: ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ =====
		const float DebugDuration = 2.0f;
		const bool bPersistent = false;
		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âº ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		DrawDebugLine(GetWorld(), Start, HitLocation, FColor::White, bPersistent, DebugDuration, 0, 1.0f);
		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		DrawDebugSphere(GetWorld(), PointOnAxis, 8.0f, 6, FColor::Magenta, bPersistent, DebugDuration);
		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ DistanceFromAxis)
		DrawDebugLine(GetWorld(), PointOnAxis, HitLocation, FColor::Magenta, bPersistent, DebugDuration, 0, 2.0f);
		// ===== END DEBUG =====
	}

		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸:
		// 1) ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â£ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œ
		// 2) ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		bool bInsideCone = (DotProduct >= CosHalfAngle) || (DistanceFromAxis <= ConeRadiusAtDistance);

		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â² ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬)
		if (HitDistance < 200.0f)
		{
			bInsideCone = true; // ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼
		}

		if (!bInsideCone)
		{
			UE_LOG(LogTemp, Verbose, TEXT("[HITSCAN_DEBUG]     -> REJECTED: OUTSIDE CONE (dot=%.4f < cosHalf=%.4f AND axisDist=%.1f > coneR=%.1f)"),
				DotProduct, CosHalfAngle, DistanceFromAxis, ConeRadiusAtDistance);
	if (bDrawHitscanDebug)
	{
			// DEBUG: ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
			DrawDebugSphere(GetWorld(), HitLocation, 20.0f, 8, FColor::Red, false, 2.0f);
	}
			continue;
		}

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½
		FHitResult BlockCheck;
		FCollisionQueryParams BlockQueryParams;
		BlockQueryParams.AddIgnoredActor(this);
		BlockQueryParams.AddIgnoredActor(GetOwner());
		BlockQueryParams.AddIgnoredActor(HitActor);

		bool bBlocked = GetWorld()->LineTraceSingleByChannel(
			BlockCheck,
			Start,
			HitLocation,
			ECC_Visibility,
			BlockQueryParams
		);

		if (bBlocked && BlockCheck.Distance < HitDistance - 50.0f)
		{
			UE_LOG(LogTemp, Verbose, TEXT("[HITSCAN_DEBUG]     -> REJECTED: BLOCKED by %s comp=%s at %.0f (threshold %.0f)"),
				*GetNameSafe(BlockCheck.GetActor()), *GetNameSafe(BlockCheck.GetComponent()),
				BlockCheck.Distance, HitDistance - 50.0f);
	if (bDrawHitscanDebug)
	{
			// DEBUG: ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
			DrawDebugSphere(GetWorld(), HitLocation, 20.0f, 8, FColor::Orange, false, 2.0f);
	}
			continue;
		}

		// === ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¦ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¬ ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ===
		HitTargets.Add(HitActor);

	if (bDrawHitscanDebug)
	{
		// DEBUG: ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		DrawDebugSphere(GetWorld(), HitLocation, 25.0f, 12, FColor::Green, false, 2.0f);
	}

		// Track best (most central) target
		if (AngleToHit < BestAngle)
		{
			BestAngle = AngleToHit;
			BestTarget = HitActor;
			BestHit = Hit;
			BestHitLocation = HitLocation;
			BestHitDistance = HitDistance;
			bBestIsHeadshot = (Hit.BoneName == FName("head") || Hit.BoneName == FName("Head"));
			BestToHitDir = ToHitDir;
		}
	}

	// ===== PASS 2: Apply damage to the best (most central) target only =====
	if (BestTarget)
	{
		// The winning candidate is very probably the CAPSULE, and a capsule carries no bone. The
		// loop above takes the first hit per actor and skips the rest, the sweep returns them
		// nearest-first, and the capsule encloses the body -- so the mesh hit that knew it was a
		// head was thrown away one line into the loop. Ask the body itself, once, now that there is
		// exactly one target to ask about.
		if (BestHit.BoneName.IsNone())
		{
			BestHit.BoneName = ResolveHitBone(BestTarget, Start, Start + Direction * (BestHitDistance + 200.0f));
			bBestIsHeadshot = (BestHit.BoneName == FName("head") || BestHit.BoneName == FName("Head"));
		}

		// Calculate wave radius at target distance
		float WaveRadiusAtTarget = CalculateWaveRadius(BestHitDistance);
		float TotalDistance = BestHitDistance;

		if (ReflectionCount > 0)
		{
			float OriginalEnergy = 1.0f;
			for (int32 i = 0; i < ReflectionCount; i++)
			{
				OriginalEnergy *= (1.0f - ReflectionEnergyLoss);
			}
			float PreviousDistance = MaxHitscanRange * (1.0f - RemainingEnergy / OriginalEnergy);
			TotalDistance = PreviousDistance + BestHitDistance;
		}

		float AreaMultiplier = CalculateDamageMultiplier(TotalDistance, WaveRadiusAtTarget);

		// Headshot check
		float HeadshotMult = bBestIsHeadshot ? GetShotHeadshotMultiplier() : 1.0f;

		// Heat System multiplier
		float HeatMult = bUseHeatSystem ? CalculateHeatDamageMultiplier() : 1.0f;

		// Z-Factor multiplier
		float ZFactorMult = 1.0f;
		if (bUseZFactor && PawnOwner)
		{
			float ShooterZ = PawnOwner->GetActorLocation().Z;
			float TargetZ = BestTarget->GetActorLocation().Z;
			ZFactorMult = CalculateZFactorMultiplier(ShooterZ, TargetZ);
		}

		// Tag-based damage multiplier
		float TagMult = GetTagDamageMultiplier(BestTarget);

		// Upgrade damage multiplier (e.g. Forward Momentum)
		float UpgradeMult = 1.0f;
		if (PawnOwner)
		{
			if (UUpgradeManagerComponent* UpgradeMgr = PawnOwner->FindComponentByClass<UUpgradeManagerComponent>())
			{
				UpgradeMult = UpgradeMgr->GetCombinedDamageMultiplier(BestTarget);
			}
		}

		float FinalDamage = HitscanDamage * RemainingEnergy * AreaMultiplier * HeadshotMult * HeatMult * ZFactorMult * TagMult * UpgradeMult;

		UE_LOG(LogTemp, Verbose, TEXT("    BEST TARGET HIT: %s | Damage: %.1f x Energy:%.2f x Area:%.2f x HS:%.1f x Heat:%.2f x Z:%.2f x Tag:%.2f x Upg:%.2f = %.1f"),
			*BestTarget->GetName(), HitscanDamage, RemainingEnergy, AreaMultiplier, HeadshotMult, HeatMult, ZFactorMult, TagMult, UpgradeMult, FinalDamage);

		// The shield as it stood when the bullet arrived, read before anything touches the target.
		// @see ApplyHitscanDamage.
		const bool bShieldDownBefore = IsTargetShieldDown(BestTarget);

		// Apply damage
		FDamageEvent DamageEvent;
		if (HitscanDamageType)
		{
			DamageEvent.DamageTypeClass = HitscanDamageType;
		}

		float ActualDamage = ApplyDamageToTarget(BestTarget, FinalDamage, DamageEvent);
		const bool bKilled = IsActorDeadAfterDamage(BestTarget);

		// [HITSCAN_DEBUG] dealt = what we sent into TakeDamage, applied = what TakeDamage returned.
		// applied=0 with dealt>0 means the TARGET swallowed it (friendly-fire guard / dead / immune).
		UE_LOG(LogTemp, Verbose, TEXT("[HITSCAN_DEBUG] APPLIED: target=%s dealt=%.1f applied=%.1f killed=%d"),
			*BestTarget->GetName(), FinalDamage, ActualDamage, bKilled ? 1 : 0);

		// Feedback goes out once, at the end of this block, when the shield reading after the shot
		// is known. @see ApplyHitscanDamage for why it is not sent here.

		// Notify upgrade system on every successful hit, incl. 0-damage ionizer hits.
		// Suppression Fire / future hitscan-on-hit upgrades depend on this firing for the pistol.
		// Per-upgrade filters (e.g. SF's IsHitscan + ShooterNPC check) live in OnOwnerDealtDamage.
		if (PawnOwner)
		{
			if (UUpgradeManagerComponent* UpgradeMgr = PawnOwner->FindComponentByClass<UUpgradeManagerComponent>())
			{
				UpgradeMgr->NotifyWeaponDealtDamage(this, BestTarget, ActualDamage, bKilled);
			}
			// And the class passive, which is the other thing on this pawn with an interest in what
			// its shots land on. @see UAbilityHandler::OnOwnerDealtDamage.
			if (UAbilityComponent* Abilities = PawnOwner->FindComponentByClass<UAbilityComponent>())
			{
				Abilities->NotifyOwnerDealtDamage(BestTarget, ActualDamage, bKilled);
			}
		}

		// Apply physics impulse / knockback
		FVector ImpulseDirection = BestToHitDir;
		float ImpulseForce = HitscanPhysicsForce * RemainingEnergy * AreaMultiplier;
		if (ACharacter* HitCharacter = Cast<ACharacter>(BestTarget))
		{
			// Exceptions (turret, ionizer vs boss) and the grounded rule live in the helper.
			ApplyHitscanKnockback(HitCharacter, ImpulseDirection * ImpulseForce, DoesShotIonize());
		}
		else if (UPrimitiveComponent* HitComp = BestHit.GetComponent())
		{
			if (HitComp->IsSimulatingPhysics())
			{
				HitComp->AddImpulseAtLocation(ImpulseDirection * ImpulseForce, BestHitLocation);
			}
		}

		// Apply ionization (add positive charge to target). HitComponent gates the NPC-shield rule.
		const bool bIonized = ApplyHitscanIonization(BestTarget, BestHit.GetComponent());

		if (WeaponOwner && (ActualDamage > 0.0f || bIonized || !bShieldDownBefore))
		{
			const bool bShieldDownAfter = IsTargetShieldDown(BestTarget);

			FHitFeedbackContext Feedback;
			Feedback.HitLocation = BestHitLocation;
			Feedback.HitDirection = BestToHitDir;
			Feedback.Damage = ActualDamage;
			Feedback.bHeadshot = bBestIsHeadshot;
			Feedback.bKilled = bKilled;
			Feedback.bShieldHit = !bShieldDownBefore;
			Feedback.bShieldBroken = !bShieldDownBefore && bShieldDownAfter;
			Feedback.bZeroDamage = (ActualDamage <= 0.0f);
			Feedback.HitActor = BestTarget;
			Feedback.FeedbackSet = FeedbackSet;

			WeaponOwner->OnWeaponHitFeedback(Feedback);
		}
	}

	// ===== ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¨ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ 4: ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ =====
	UE_LOG(LogTemp, Verbose, TEXT("Cone Hitscan RESULT: %d targets hit"), HitTargets.Num());

	// [HITSCAN_DEBUG] The "tracer hit but no damage" case lands exactly here:
	// sweepHits=0           -> pawn sweep never found the enemy (parallax / object type / collision off)
	// sweepHits>0 passed=0  -> all candidates rejected (see REJECTED lines above for the reason)
	if (!BestTarget)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[HITSCAN_DEBUG] NO DAMAGE THIS SHOT: sweepHits=%d passedFilter=%d (beam drawn to %s)"),
			SweepHits.Num(), HitTargets.Num(),
			bHitWall ? *GetNameSafe(WallHitResult.GetActor()) : TEXT("max range"));
	}

	// If we hit a pawn, shorten beam to the pawn hit location
	FVector EffectiveBeamEnd = BeamEnd;
	if (BestTarget)
	{
		// Use actor location as fallback if hit location is zero
		FVector PawnEndPoint = BestHitLocation.IsNearlyZero() ? BestTarget->GetActorLocation() : BestHitLocation;
		float DistToPawn = FVector::Dist(Start, PawnEndPoint);
		float DistToWall = FVector::Dist(Start, BeamEnd);

		if (DistToPawn < DistToWall)
		{
			EffectiveBeamEnd = PawnEndPoint;
		}

		UE_LOG(LogTemp, Verbose, TEXT("Pawn beam end: Target=%s, PawnDist=%.0f, WallDist=%.0f, Using=%s"),
			*BestTarget->GetName(), DistToPawn, DistToWall,
			DistToPawn < DistToWall ? TEXT("PAWN") : TEXT("WALL"));
	}

	// --- Visuals: the trace starts at the socket's REAL position, the beam must start where the
	// barrel is SEEN. The first-person mesh is drawn through a transform of its own (FP FOV
	// correction + FirstPersonScale), so a tracer put at the raw socket location leaves the gun
	// from somewhere off to the side. Same correction the classic path already applies; see
	// GetFirstPersonMuzzleRenderLocation. Reflected segments start at the bounce point instead.
	FVector VisualStart = Start;
	if (ReflectionCount == 0 && PawnOwner && PawnOwner->IsPlayerControlled() && FirstPersonMesh)
	{
		VisualStart = GetFirstPersonMuzzleRenderLocation();
	}

	// Where the tracer is claimed to start vs where the socket actually is. If the yellow sphere
	// sits on the barrel tip on screen but the tracer leaves from somewhere else, the offset is
	// inside the Niagara asset (local-space beam), not here. If the yellow sphere is ALSO in the
	// wrong place, MuzzleSocketName does not exist on this mesh and GetSocketLocation quietly
	// returned the component origin.
	if (bDrawHitscanDebug && FirstPersonMesh)
	{
		const bool bSocketExists = FirstPersonMesh->DoesSocketExist(MuzzleSocketName);
		DrawDebugSphere(GetWorld(), FirstPersonMesh->GetSocketLocation(MuzzleSocketName), 3.0f, 8,
			bSocketExists ? FColor::Yellow : FColor::Red, false, 5.0f);
		DrawDebugSphere(GetWorld(), VisualStart, 2.0f, 8, FColor::Magenta, false, 5.0f);
		DrawDebugLine(GetWorld(), VisualStart, EffectiveBeamEnd, FColor::White, false, 5.0f, 0, 0.25f);
		UE_LOG(LogTemp, Warning, TEXT("[MUZZLE_DEBUG] socketExists=%d socket=%s visualStart=%s comp=%s beamEnd=%s"),
			bSocketExists ? 1 : 0,
			*FirstPersonMesh->GetSocketLocation(MuzzleSocketName).ToCompactString(),
			*VisualStart.ToCompactString(),
			*FirstPersonMesh->GetComponentLocation().ToCompactString(),
			*EffectiveBeamEnd.ToCompactString());
	}

	SpawnBeamEffect(VisualStart, EffectiveBeamEnd, RemainingEnergy);

	if (bUseWaveVisualization)
	{
		SpawnWaveFronts(VisualStart, EffectiveBeamEnd);
	}

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â­ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ
	// Decide impact target: pawn (if hit closer than wall) or wall.
	// The wall trace uses ECC_Visibility which passes through pawns, so without this check
	// the impact would always appear on the surface BEHIND a hit pawn.
	bool bImpactOnPawn = false;
	if (BestTarget)
	{
		const FVector PawnEndPoint = BestHitLocation.IsNearlyZero() ? BestTarget->GetActorLocation() : BestHitLocation;
		const float DistToPawn = FVector::Dist(Start, PawnEndPoint);
		const float DistToWall = bHitWall ? FVector::Dist(Start, WallHitResult.ImpactPoint) : TNumericLimits<float>::Max();
		bImpactOnPawn = (DistToPawn < DistToWall);
	}

	if (bImpactOnPawn)
	{
		SpawnImpactEffect(BestHit);
	}
	else if (bHitWall)
	{
		SpawnImpactEffect(WallHitResult);
	}

	// Reflection happens only off a wall, and only if the shot wasn't intercepted by a pawn.
	if (bHitWall && !bImpactOnPawn)
	{
		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ
		if (MaxReflections > 0 && IsMetal(WallHitResult) && ReflectionCount < MaxReflections)
		{
			FVector ReflectedDir = CalculateReflection(Direction, WallHitResult.ImpactNormal);
			float NewEnergy = RemainingEnergy * (1.0f - ReflectionEnergyLoss);

			UE_LOG(LogTemp, Verbose, TEXT("Cone Hitscan: Reflecting off %s (NewEnergy: %.2f)"),
				*WallHitResult.GetActor()->GetName(), NewEnergy);

			SpawnReflectionEffect(WallHitResult.ImpactPoint, Direction, ReflectedDir);

			if (ReflectionSound)
			{
				UGameplayStatics::PlaySoundAtLocation(this, ReflectionSound, WallHitResult.ImpactPoint, NewEnergy);
			}

			FVector ReflectionStart = WallHitResult.ImpactPoint + ReflectedDir * 1.0f;
			PerformHitscan(ReflectionStart, ReflectedDir, NewEnergy, ReflectionCount + 1);
		}
	}

	// === DEBUG: ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ===
	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸:
	/*
	DrawDebugCone(GetWorld(), Start, Direction, MaxDistance, ConeHalfAngleRad, ConeHalfAngleRad,
		12, FColor::Yellow, false, 2.0f, 0, 1.0f);
	DrawDebugSphere(GetWorld(), BeamEnd, ConeRadiusAtEnd, 16, FColor::Cyan, false, 2.0f);
	*/
}

bool AShooterWeapon::IsMetal(const FHitResult& Hit) const
{
	if (MetalMaterials.Num() == 0)
	{
		return false;
	}

	UPhysicalMaterial* PhysMat = Hit.PhysMaterial.Get();
	if (!PhysMat)
	{
		return false;
	}

	return MetalMaterials.Contains(PhysMat);
}

FVector AShooterWeapon::CalculateReflection(const FVector& Direction, const FVector& Normal) const
{
	// R = D - 2(DÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â·N)N
	return Direction - 2.0f * FVector::DotProduct(Direction, Normal) * Normal;
}

void AShooterWeapon::ApplyHitscanDamage(const FHitResult& Hit, float EnergyMultiplier, float Distance, float WaveRadius,
	float ExtraDamageMultiplier)
{
	// A thin wrapper over the shared funnel now. What stays here is the part only a TRACE knows: the
	// beam's remaining energy and the falloff across the wave's radius. Everything a hit of this
	// weapon means -- shield, ionization, upgrades, knockback, the marker -- is shared with the
	// projectile and lives in ApplyWeaponHit.
	const float AreaMultiplier = CalculateDamageMultiplier(Distance, WaveRadius);
	const FVector HitDirection = (Hit.ImpactPoint - GetActorLocation()).GetSafeNormal();

	UE_LOG(LogTemp, Verbose, TEXT("Hitscan: Base=%.1f x Energy=%.2f x Area=%.2f (WaveR=%.1f, TargetR=%.1f) -> %s"),
		HitscanDamage, EnergyMultiplier, AreaMultiplier, WaveRadius, TargetEffectiveRadius,
		*GetNameSafe(Hit.GetActor()));

	ApplyWeaponHit(Hit, HitscanDamage * EnergyMultiplier * AreaMultiplier, HitDirection,
		HitscanPhysicsForce * EnergyMultiplier * AreaMultiplier, ExtraDamageMultiplier);
}

float AShooterWeapon::ApplyWeaponHit(const FHitResult& Hit, float BaseDamage, const FVector& HitDirection,
	float ImpulseForce, float ExtraDamageMultiplier, TSubclassOf<UDamageType> OverrideDamageType,
	bool bAllowOwnerDamage, const FDamageEvent* OverrideDamageEvent)
{
	AActor* HitActor = Hit.GetActor();
	if (!HitActor)
	{
		return 0.0f;
	}

	// EMF Foliage->Prop conversion: if the trace struck a UEMFConvertibleFoliageType
	// instance, swap the foliage instance for a freshly spawned EMFPhysicsProp
	// before any damage/ionization runs. From here on HitActor refers to the new prop.
	// BaseDamage, not HitscanDamage: this is what THIS shot carries, and a projectile weapon leaves
	// the hitscan field at zero, which would have snapped the sapling for free.
	if (AEMFPhysicsProp* ConvertedProp = UFoliageConversionLibrary::TryConvertFoliageInstance(Hit, BaseDamage))
	{
		HitActor = ConvertedProp;
	}

	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢, ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢
	// bAllowOwnerDamage is the caller's own answer to the same question: a projectile carries its
	// bDamageOwner, and a rocket that is supposed to hurt the person who fired it must not be
	// silenced by a checkbox that belongs to the trace path.
	if (!bHitscanDamageOwner && !bAllowOwnerDamage && HitActor == GetOwner())
	{
		return 0.0f;
	}

	// Read before anything touches the target: the shield's state at the moment the bullet arrived
	// is what the feedback has to describe. Compared against the same reading taken after damage and
	// ionization have run, it also identifies the single shot that took the shield down -- which is
	// the one event in a firefight that tells the player their job just changed.
	const bool bShieldDownBefore = IsTargetShieldDown(HitActor);

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
	// Wave falloff and beam energy were folded into BaseDamage and ImpulseForce by the caller: they
	// mean nothing to a projectile, which arrives with one number and no beam behind it.

	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° headshot
	bool bIsHeadshot = (Hit.BoneName == FName("head") || Hit.BoneName == FName("Head"));
	float HeadshotMult = bIsHeadshot ? GetShotHeadshotMultiplier() : 1.0f;

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¤ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½
	const float FinalDamage = BaseDamage * HeadshotMult * ExtraDamageMultiplier;

	UE_LOG(LogTemp, Verbose, TEXT("[HIT] %s: Base=%.1f x HS=%.1f x Extra=%.2f = %.1f to %s"),
		*GetName(), BaseDamage, HeadshotMult, ExtraDamageMultiplier, FinalDamage, *HitActor->GetName());


	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½
	FDamageEvent PointDamageEvent;
	// A projectile carries its own damage type (fire, explosion) and it must survive the trip through
	// a funnel whose default was written for bullets.
	if (const TSubclassOf<UDamageType> DamageType = OverrideDamageType ? OverrideDamageType : HitscanDamageType)
	{
		PointDamageEvent.DamageTypeClass = DamageType;
	}

	// An explosion arrives with its event already assembled, and it is a RADIAL one: replacing it
	// would erase the blast origin and radius that every reaction downstream reads back out.
	const FDamageEvent& DamageEvent = OverrideDamageEvent ? *OverrideDamageEvent : PointDamageEvent;

	float ActualDamage = ApplyDamageToTarget(HitActor, FinalDamage, DamageEvent);

	bool bKilled = IsActorDeadAfterDamage(HitActor);


	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â£ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°)
	// Feedback is sent once, at the end of this function, when the shield reading after the shot is
	// known. It used to fire here and again further down for the zero-damage case, which is how the
	// ionizer ended up with a second entrance into the hit marker that no other caller went through.

	// Notify upgrade system on every successful hit, incl. 0-damage ionizer hits.
	// Suppression Fire / future hitscan-on-hit upgrades depend on this firing for the pistol.
	if (PawnOwner)
	{
		if (UUpgradeManagerComponent* UpgradeMgr = PawnOwner->FindComponentByClass<UUpgradeManagerComponent>())
		{
			UpgradeMgr->NotifyWeaponDealtDamage(this, HitActor, ActualDamage, bKilled);
		}
		if (UAbilityComponent* Abilities = PawnOwner->FindComponentByClass<UAbilityComponent>())
		{
			Abilities->NotifyOwnerDealtDamage(HitActor, ActualDamage, bKilled);
		}
	}

	//ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â
	// The direction comes from the caller now. A trace hands over the line from muzzle to impact; a
	// projectile hands over the direction it was actually flying, which is the honest one for an arc.
	const FVector ImpulseDirection = HitDirection.GetSafeNormal();

	// Zero means the caller pushes for itself, and an explosion does: its launch is scaled by the
	// splash falloff and multiplied again for a rocket jump, none of which a per-bullet shove knows
	// about. Without this guard a zero-length launch would still be handed to LaunchCharacter, which
	// forces MOVE_Falling whatever it is given.
	if (ImpulseForce <= 0.0f)
	{
		// nothing to push with
	}
	else if (ACharacter* HitCharacter = Cast<ACharacter>(HitActor))
	{
		// Exceptions (turret, ionizer vs boss) and the grounded rule live in the helper.
		ApplyHitscanKnockback(HitCharacter, ImpulseDirection * ImpulseForce, DoesShotIonize());
	}
	else
	{
		// After foliage->prop conversion HitActor != Hit.GetActor(), and the original
		// component is the foliage HISM (no physics). Route the impulse to the freshly
		// spawned prop's PropMesh instead. For non-converted hits this stays Hit.GetComponent().
		UPrimitiveComponent* ImpulseTarget = Hit.GetComponent();
		if (HitActor != Hit.GetActor())
		{
			if (AEMFPhysicsProp* AsProp = Cast<AEMFPhysicsProp>(HitActor))
			{
				ImpulseTarget = AsProp->PropMesh;
			}
		}
		if (ImpulseTarget && ImpulseTarget->IsSimulatingPhysics())
		{
			ImpulseTarget->AddImpulseAtLocation(ImpulseDirection * ImpulseForce, Hit.ImpactPoint);
		}
	}

	// Apply ionization (add positive charge to target). HitComponent gates the NPC-shield rule.
	const bool bIonized = ApplyHitscanIonization(HitActor, Hit.GetComponent());

	// One door for every kind of connection this shot could have been: damage, a headshot, a kill,
	// a shield taken down, or the ionizer's zero-damage charge transfer. Landing on a shield counts
	// on its own: a held shield absorbs the damage entirely, so a weapon that neither hurt nor
	// charged would otherwise hit a shield in total silence.
	if (WeaponOwner && (ActualDamage > 0.0f || bIonized || !bShieldDownBefore))
	{
		const bool bShieldDownAfter = IsTargetShieldDown(HitActor);

		FHitFeedbackContext Feedback;
		Feedback.HitLocation = Hit.ImpactPoint;
		Feedback.HitDirection = ImpulseDirection;
		Feedback.Damage = ActualDamage;
		Feedback.bHeadshot = bIsHeadshot;
		Feedback.bKilled = bKilled;
		Feedback.bShieldHit = !bShieldDownBefore;
		Feedback.bShieldBroken = !bShieldDownBefore && bShieldDownAfter;
		Feedback.bZeroDamage = (ActualDamage <= 0.0f);
		Feedback.HitActor = HitActor;
		Feedback.FeedbackSet = FeedbackSet;

		// Who is standing where the confirmation has to be heard.
		//
		// A trace is resolved on the shooter's own machine, so this call has always been a local one
		// and the hit marker component's IsLocalFeedback guard was enough. A PROJECTILE is resolved
		// by the authority, and when the shooter is a client that is a different computer entirely:
		// calling straight through here would hand the marker to a machine whose guard correctly
		// throws it away, and the shooter would hear nothing. So the confirmation is sent down to
		// the one connection that earned it.
		if (PawnOwner && HasAuthority() && !PawnOwner->IsLocallyControlled())
		{
			Client_ReportHitFeedback(Feedback);
		}
		else
		{
			WeaponOwner->OnWeaponHitFeedback(Feedback);
		}
	}

	return ActualDamage;
}

void AShooterWeapon::Client_ReportHitFeedback_Implementation(const FHitFeedbackContext& Context)
{
	if (WeaponOwner)
	{
		WeaponOwner->OnWeaponHitFeedback(Context);
	}
}

void AShooterWeapon::PerformSimpleHitscan(const FVector& Start, const FVector& Direction, float EnergyMultiplier)
{
	float TraceDistance = MaxHitscanRange * EnergyMultiplier;
	FVector End = Start + Direction * TraceDistance;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(GetOwner());
	QueryParams.bReturnPhysicalMaterial = true;

	// --- Trace 1: World geometry (walls, floors, damageable props) ---
	// Pawn profile ignores ECC_Visibility, so this only finds world geometry
	FHitResult WallHit;
	bool bHitWall = GetWorld()->LineTraceSingleByChannel(
		WallHit, Start, End, ECC_Visibility, QueryParams);

	float WallDistance = bHitWall ? WallHit.Distance : TraceDistance;

	// Damage non-Pawn damageable actors (e.g. EMFPhysicsProp)
	if (bHitWall && WallHit.GetActor() && !Cast<APawn>(WallHit.GetActor()) && WallHit.GetActor()->CanBeDamaged())
	{
		ApplyHitscanDamage(WallHit, EnergyMultiplier, WallHit.Distance, 0.0f);
	}

	// --- Trace 2: Pawns (player) via ObjectType query ---
	// Pawn collision profile blocks ObjectType queries for ECC_Pawn
	FHitResult PawnHit;
	FCollisionObjectQueryParams PawnObjectParams;
	PawnObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	FVector PawnTraceEnd = Start + Direction * WallDistance; // only trace up to wall
	bool bHitPawn = GetWorld()->LineTraceSingleByObjectType(
		PawnHit, Start, PawnTraceEnd, PawnObjectParams, QueryParams);

	// The Pawn object query answers with the capsule, which has no bone on it, so every shot down
	// this path was a body shot by construction. Ask the mesh once, here, before anything reads the
	// bone off this result. @see ResolveHitBone.
	if (bHitPawn && PawnHit.BoneName.IsNone())
	{
		PawnHit.BoneName = ResolveHitBone(PawnHit.GetActor(), Start, PawnTraceEnd + Direction * 200.0f);
	}

	// --- Always fire a dodgeable traveling BOLT (down the aim line) instead of an instant hitscan ---
	// EVERY enemy hitscan shot becomes a projectile-like bolt travelling down the aim line at
	// HitscanBoltSpeed (fast by default). Damage lands only if the player's CURRENT position is
	// inside the moving window when it passes — so the player can dodge by stepping off the line.
	// The Low-Health Defense upgrade slows the bolt via the player's EnemyBoltSlowMultiplier
	// (curve-scaled), making it progressively dodgeable as HP drops.
	// The bolt belongs to the player being shot at: prefer whoever the pawn trace actually hit,
	// and fall back to the player closest to where the shot lands. Using player 0 would apply one
	// teammate's Low-Health Defense to bolts aimed at everybody.
	// Shot an NPC: it lands, now, on that NPC. The bolt below is a PLAYER-facing mechanic - a window
	// travelling down the aim line that can be stepped out of, slowed by the Low-Health Defense
	// upgrade - and none of it means anything between two AI. Without this branch the shot resolved
	// as "no player hit", built a bolt aimed at whatever player was nearest, and the NPC actually
	// standing in the line took nothing: which is why rifles, LMGs and the tank's machine gun could
	// fire at each other all day and every kill in the battle log came from a grenade or a drone.
	if (APawn* const HitPawn = Cast<APawn>(PawnHit.GetActor());
		bHitPawn && HitPawn && !CoopPlayers::IsPlayer(HitPawn) && HitPawn->CanBeDamaged())
	{
		ApplyHitscanDamage(PawnHit, EnergyMultiplier, PawnHit.Distance, 0.0f);

		SpawnBeamEffect(Start, bHitWall ? WallHit.ImpactPoint : End, EnergyMultiplier);
		SpawnImpactEffect(PawnHit);
		return;
	}

	AShooterCharacter* TargetPlayer = Cast<AShooterCharacter>(PawnHit.GetActor());
	if (!TargetPlayer)
	{
		TargetPlayer = Cast<AShooterCharacter>(
			CoopPlayers::GetNearest(GetWorld(), bHitWall ? WallHit.ImpactPoint : End));
	}

	if (TargetPlayer)
	{
		const float SpeedMult = FMath::Max(TargetPlayer->GetEnemyBoltSlowMultiplier(), 0.01f);
		const float EffSpeed = HitscanBoltSpeed * SpeedMult;
		const float EffVariance = HitscanBoltSpeedVariance * SpeedMult;

		const FVector BoltBeamEnd = bHitWall ? WallHit.ImpactPoint : End;
		const float RandomSeed = FMath::FRand() * 1000.0f;
		const float RandSpeed = FMath::Max(EffSpeed + EffVariance * FMath::Sin(RandomSeed), 1.0f);

		if (UEnemyBeamBoltSubsystem* BoltSys = GetWorld() ? GetWorld()->GetSubsystem<UEnemyBeamBoltSubsystem>() : nullptr)
		{
			BoltSys->RegisterBolt(this, TargetPlayer, Start, Direction, WallDistance,
				RandSpeed, HitscanBoltLength, HitscanBoltRadius, EnergyMultiplier);
		}

		// Tracer matches the bolt exactly (same effective Speed/Variance + RandomSeed pushed to Niagara).
		SpawnBeamEffect(Start, BoltBeamEnd, EnergyMultiplier,
			EffSpeed, EffVariance, HitscanBoltLength, RandomSeed);

		if (bHitWall)
		{
			SpawnImpactEffect(WallHit);
		}

		UE_LOG(LogTemp, Verbose, TEXT("[BOLT_DEBUG] Enemy bolt down aim line — speedMult=%.2f randSpeed=%.0f maxDist=%.0f"),
			SpeedMult, RandSpeed, WallDistance);
		return; // damage is deferred to the bolt subsystem (dodgeable)
	}
	// (No local player resolved → fall through to the instant hitscan path below as a safety fallback.)

	// --- Determine beam endpoint and apply pawn damage ---
	FVector BeamEnd;
	bool bPawnWasHit = false;

	if (bHitPawn && PawnHit.GetActor() && PawnHit.GetActor()->CanBeDamaged())
	{
		// Pawn hit is always closer than wall (we traced up to wall distance)
		ApplyHitscanDamage(PawnHit, EnergyMultiplier, PawnHit.Distance, 0.0f);
		bPawnWasHit = true;

		// Beam goes to wall (or max range), not stopping at pawn
		BeamEnd = bHitWall ? WallHit.ImpactPoint : End;

		UE_LOG(LogTemp, Verbose, TEXT("[NPC Hitscan] HIT PAWN: %s at dist=%.0f"),
			*PawnHit.GetActor()->GetName(), PawnHit.Distance);
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green,
			FString::Printf(TEXT("[NPC] DMG -> %s (%.0f)"), *PawnHit.GetActor()->GetName(), PawnHit.Distance));
	}
	else
	{
		BeamEnd = bHitWall ? WallHit.ImpactPoint : End;

		if (bHitWall)
		{
			UE_LOG(LogTemp, Verbose, TEXT("[NPC Hitscan] Hit wall: %s at dist=%.0f (no pawn hit)"),
				*WallHit.GetActor()->GetName(), WallHit.Distance);
		}
		else
		{
			UE_LOG(LogTemp, Verbose, TEXT("[NPC Hitscan] MISS: nothing hit"));
		}
	}

	// Visual effects
	SpawnBeamEffect(Start, BeamEnd, EnergyMultiplier);

	// Spawn impact: prefer pawn hit (closer), otherwise the wall behind it.
	// Without this, impact would always appear on the wall — even when a pawn intercepted the shot.
	if (bPawnWasHit)
	{
		SpawnImpactEffect(PawnHit);
	}
	else if (bHitWall)
	{
		SpawnImpactEffect(WallHit);
	}
}

// ==================== Mounted: a gun clamped into a building ====================

void AShooterWeapon::FireMounted(const FVector& TargetLocation)
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}
	if (!ThirdPersonMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TURRET_DEBUG] %s: FireMounted with no third person mesh, no muzzle to fire from"), *GetName());
		return;
	}

	// The shot is seen and heard everywhere. There is no shooter to skip inside the multicast
	// (PawnOwner is null, so bIsShooter is false on every machine) and the authority plays its own
	// copy here, the way Fire() does.
	PlayFireEffectsLocally(false);
	Multicast_PlayFireEffects(false);

	if (bUseHeatSystem)
	{
		AddHeat(HeatPerShot);
	}

	if (bUseHitscan)
	{
		// Straight from the muzzle at the point the mount chose. No cone (GetCurrentSpreadDegrees
		// answers zero while mounted) and no camera re-basing: there is no camera.
		const FVector Start = ThirdPersonMesh->GetSocketLocation(MuzzleSocketName);
		const FVector Direction = (TargetLocation - Start).GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			return;
		}
		PerformMountedHitscan(Start, Direction);
	}
	else
	{
		// The same transform a pawn's shot would get: the third person muzzle, the ballistic arc
		// for a round that falls, the straight line for one that does not.
		const FTransform ProjectileTransform = CalculateProjectileSpawnTransform(TargetLocation);
		SpawnProjectileAtTransform(ProjectileTransform, 1.0f, /*bCosmeticOnly*/ false);
	}

	TimeOfLastShot = GetWorld()->GetTimeSeconds();
	OnShotFired.Broadcast();

	// Loud enough for the AI to hear, from where the gun actually is.
	MakeNoise(ShotLoudness, nullptr, GetActorLocation(), ShotNoiseRange, ShotNoiseTag);
}

void AShooterWeapon::PerformMountedHitscan(const FVector& Start, const FVector& Direction)
{
	const float TraceDistance = MaxHitscanRange;
	const FVector End = Start + Direction * TraceDistance;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(GetOwner());
	QueryParams.bReturnPhysicalMaterial = true;

	// Wall first, then the first pawn before it: the same two traces as the NPC path, because
	// pawns do not block the visibility channel here and a pawn is found by object type only.
	FHitResult WallHit;
	const bool bHitWall = GetWorld()->LineTraceSingleByChannel(WallHit, Start, End, ECC_Visibility, QueryParams);
	const float WallDistance = bHitWall ? WallHit.Distance : TraceDistance;

	FHitResult PawnHit;
	FCollisionObjectQueryParams PawnObjectParams;
	PawnObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	const FVector PawnTraceEnd = Start + Direction * WallDistance;
	const bool bHitPawn = GetWorld()->LineTraceSingleByObjectType(PawnHit, Start, PawnTraceEnd, PawnObjectParams, QueryParams);

	if (bHitPawn && PawnHit.BoneName.IsNone())
	{
		PawnHit.BoneName = ResolveHitBone(PawnHit.GetActor(), Start, PawnTraceEnd + Direction * 200.0f);
	}

	// Only an enemy of the mount takes the round. A teammate standing in the line simply stops it,
	// which is what a body in front of a gun does; the NPC path would have fired a bolt at them.
	AActor* const PawnActor = bHitPawn ? PawnHit.GetActor() : nullptr;
	const bool bHostilePawn = PawnActor && PawnActor->CanBeDamaged() && PolarityTeams::AreHostile(GetOwner(), PawnActor);

	FVector BeamEnd = bHitWall ? WallHit.ImpactPoint : End;
	if (bHostilePawn)
	{
		ApplyHitscanDamage(PawnHit, 1.0f, PawnHit.Distance, 0.0f);
		UE_LOG(LogTemp, Verbose, TEXT("[TURRET_DEBUG] %s hit %s at %.0f"), *GetName(), *PawnActor->GetName(), PawnHit.Distance);
	}
	else if (bHitPawn)
	{
		// Stopped by a body that is not a target: the tracer ends there and nothing is hurt.
		BeamEnd = PawnHit.ImpactPoint;
	}
	else if (bHitWall && WallHit.GetActor() && !Cast<APawn>(WallHit.GetActor()) && WallHit.GetActor()->CanBeDamaged())
	{
		// A prop, a crate, an enemy building: the same as any other gun's round landing on it. A
		// friendly building answers its own TakeDamage with nothing, so this cannot hurt the base.
		ApplyHitscanDamage(WallHit, 1.0f, WallHit.Distance, 0.0f);
	}

	SpawnBeamEffect(Start, BeamEnd, 1.0f);
	if (bHitPawn)
	{
		SpawnImpactEffect(PawnHit);
	}
	else if (bHitWall)
	{
		SpawnImpactEffect(WallHit);
	}
}

void AShooterWeapon::PerformClassicHitscan(const FVector& Start, const FVector& Direction, float RemainingEnergy, int32 ReflectionCount)
{
	const float SegmentMaxDistance = MaxHitscanRange * RemainingEnergy;
	const FVector End = Start + Direction * SegmentMaxDistance;
	const float SweepRadius = FMath::Max(InitialWaveRadius, 1.0f);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(GetOwner());
	QueryParams.bReturnPhysicalMaterial = true;

	// --- Trace 1: world geometry (pawn profiles ignore ECC_Visibility) ---
	FHitResult WallHit;
	const bool bHitWall = GetWorld()->LineTraceSingleByChannel(WallHit, Start, End, ECC_Visibility, QueryParams);
	const float WallDistance = bHitWall ? WallHit.Distance : SegmentMaxDistance;

	// Damage non-pawn damageable actors (EMFPhysicsProp, convertible foliage) — same rule as the cone path.
	// A travelling shot does not do this here: the prop is only hit when the bolt reaches it, so both
	// the damage and the impact effect wait and are done by the bolt on arrival.
	if (!bHitscanTravelsAsBolt
		&& bHitWall && WallHit.GetActor() && !Cast<APawn>(WallHit.GetActor()) && WallHit.GetActor()->CanBeDamaged())
	{
		ApplyHitscanDamage(WallHit, RemainingEnergy, WallHit.Distance, 0.0f);
	}

	// --- Trace 2: thin pawn sweep up to the wall ---
	// The swept volume is a thin capsule along the ray: SweepRadius units of forgiveness.
	TArray<FHitResult> PawnHits;
	FCollisionObjectQueryParams PawnObjectParams;
	PawnObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	GetWorld()->SweepMultiByObjectType(
		PawnHits,
		Start,
		Start + Direction * WallDistance,
		FQuat::Identity,
		PawnObjectParams,
		FCollisionShape::MakeSphere(SweepRadius),
		QueryParams);

	UE_LOG(LogTemp, Verbose, TEXT("[HITSCAN_DEBUG] === ClassicShot: Start=%s Dir=%s | SweepR=%.1f | Wall=%s dist=%.0f | pawnHits=%d refl=%d"),
		*Start.ToCompactString(), *Direction.ToCompactString(), SweepRadius,
		bHitWall ? *GetNameSafe(WallHit.GetActor()) : TEXT("none"),
		WallDistance, PawnHits.Num(), ReflectionCount);

	// --- Pick the NEAREST pawn on the ray: a bullet stops at the first body ---
	int32 BestIndex = INDEX_NONE;
	float BestDistance = MAX_FLT;
	for (int32 i = 0; i < PawnHits.Num(); ++i)
	{
		const FHitResult& Hit = PawnHits[i];
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || !HitActor->CanBeDamaged())
		{
			continue;
		}

		// Initial-overlap sweep hits report Distance = 0 — use the actor location instead
		float Dist = Hit.Distance;
		if (Dist < 1.0f)
		{
			Dist = FVector::Dist(Start, HitActor->GetActorLocation());
		}

		UE_LOG(LogTemp, Verbose, TEXT("[HITSCAN_DEBUG]   cand=%s comp=%s bone=%s dist=%.0f"),
			*HitActor->GetName(), *GetNameSafe(Hit.GetComponent()), *Hit.BoneName.ToString(), Dist);

		if (Dist < BestDistance)
		{
			BestDistance = Dist;
			BestIndex = i;
		}
	}

	// One seed for this shot. The bolt's speed and the tracer's speed are derived from it the same
	// way (Speed + Variance * sin(Seed)), which is what keeps the streak sitting on the damage
	// region instead of merely resembling it. Negative means this weapon does not travel.
	const float BoltRandomSeed = bHitscanTravelsAsBolt ? FMath::FRand() * 1000.0f : -1.0f;

	// Filled in below when the shot travels, then handed to the bolt in one place, so a shot on
	// course to hit nobody is registered the same way as one that is: it still has to arrive
	// somewhere before it is allowed to mark the wall.
	AActor* BoltVictim = nullptr;
	float BoltDamageMultiplier = 1.0f;
	FName BoltHitBone = NAME_None;

	// --- Apply damage to the nearest pawn with the full player multiplier stack (as in the cone path) ---
	bool bPawnWasHit = false;
	FVector PawnHitLocation = FVector::ZeroVector;
	FHitResult PawnHit;
	if (BestIndex != INDEX_NONE)
	{
		PawnHit = PawnHits[BestIndex];
		AActor* HitActor = PawnHit.GetActor();
		bPawnWasHit = true;
		PawnHitLocation = PawnHit.ImpactPoint.IsNearlyZero() ? HitActor->GetActorLocation() : FVector(PawnHit.ImpactPoint);

		// A bullet stops at the first body, and the first shape of that body on the ray is the
		// CAPSULE, which carries no bone. Ask the mesh itself before deciding this was not a head.
		// @see ResolveHitBone.
		if (PawnHit.BoneName.IsNone())
		{
			PawnHit.BoneName = ResolveHitBone(HitActor, Start, Start + Direction * (WallDistance + 200.0f));
		}

		const bool bIsHeadshot = (PawnHit.BoneName == FName("head") || PawnHit.BoneName == FName("Head"));
		const float HeadshotMult = bIsHeadshot ? GetShotHeadshotMultiplier() : 1.0f;
		const float HeatMult = bUseHeatSystem ? CalculateHeatDamageMultiplier() : 1.0f;

		float ZFactorMult = 1.0f;
		if (bUseZFactor && PawnOwner)
		{
			ZFactorMult = CalculateZFactorMultiplier(PawnOwner->GetActorLocation().Z, HitActor->GetActorLocation().Z);
		}

		const float TagMult = GetTagDamageMultiplier(HitActor);

		float UpgradeMult = 1.0f;
		if (PawnOwner)
		{
			if (UUpgradeManagerComponent* UpgradeMgr = PawnOwner->FindComponentByClass<UUpgradeManagerComponent>())
			{
				UpgradeMult = UpgradeMgr->GetCombinedDamageMultiplier(HitActor);
			}
		}

		// A weapon whose hits travel does not land this one now. Everything the shot knows and the
		// arrival cannot work out for itself is written down here and handed to the bolt below: the
		// multiplier stack, and the bone the pellet was on course for so a headshot is still a
		// headshot when it gets there.
		if (bHitscanTravelsAsBolt)
		{
			BoltVictim = HitActor;
			BoltDamageMultiplier = HeatMult * ZFactorMult * TagMult * UpgradeMult;
			BoltHitBone = PawnHit.BoneName;
		}
		else
		{
			// No distance falloff: at zero divergence the wave radius never exceeds the target
			// radius, so the cone path's area multiplier would be 1.0 anyway.
			const float FinalDamage = HitscanDamage * RemainingEnergy * HeadshotMult * HeatMult * ZFactorMult * TagMult * UpgradeMult;

			FDamageEvent DamageEvent;
			if (HitscanDamageType)
			{
				DamageEvent.DamageTypeClass = HitscanDamageType;
			}

			// The shield as it stood when the bullet arrived. @see ApplyHitscanDamage.
			const bool bShieldDownBefore = IsTargetShieldDown(HitActor);

			const float ActualDamage = ApplyDamageToTarget(HitActor, FinalDamage, DamageEvent);
			const bool bKilled = IsActorDeadAfterDamage(HitActor);

			UE_LOG(LogTemp, Verbose, TEXT("[HITSCAN_DEBUG] APPLIED(classic): target=%s dist=%.0f dealt=%.1f applied=%.1f killed=%d"),
				*HitActor->GetName(), BestDistance, FinalDamage, ActualDamage, bKilled ? 1 : 0);

			// Feedback goes out once, at the end of this block. @see ApplyHitscanDamage.

			// Notify upgrade system on every successful hit, incl. 0-damage ionizer hits
			if (PawnOwner)
			{
				if (UUpgradeManagerComponent* UpgradeMgr = PawnOwner->FindComponentByClass<UUpgradeManagerComponent>())
				{
					UpgradeMgr->NotifyWeaponDealtDamage(this, HitActor, ActualDamage, bKilled);
				}
				if (UAbilityComponent* Abilities = PawnOwner->FindComponentByClass<UAbilityComponent>())
				{
					Abilities->NotifyOwnerDealtDamage(HitActor, ActualDamage, bKilled);
				}
			}

			// Knockback / physics impulse — same rules as the cone path, see ApplyHitscanKnockback.
			const float ImpulseForce = HitscanPhysicsForce * RemainingEnergy;
			if (ACharacter* HitCharacter = Cast<ACharacter>(HitActor))
			{
				ApplyHitscanKnockback(HitCharacter, Direction * ImpulseForce, DoesShotIonize());
			}
			else if (UPrimitiveComponent* HitComp = PawnHit.GetComponent())
			{
				if (HitComp->IsSimulatingPhysics())
				{
					HitComp->AddImpulseAtLocation(Direction * ImpulseForce, PawnHitLocation);
				}
			}

			// Ionization (charge transfer); HitComponent gates the NPC riot-shield rule
			const bool bIonized = ApplyHitscanIonization(HitActor, PawnHit.GetComponent());

			if (WeaponOwner && (ActualDamage > 0.0f || bIonized || !bShieldDownBefore))
			{
				const bool bShieldDownAfter = IsTargetShieldDown(HitActor);

				FHitFeedbackContext Feedback;
				Feedback.HitLocation = PawnHitLocation;
				Feedback.HitDirection = Direction;
				Feedback.Damage = ActualDamage;
				Feedback.bHeadshot = bIsHeadshot;
				Feedback.bKilled = bKilled;
				Feedback.bShieldHit = !bShieldDownBefore;
				Feedback.bShieldBroken = !bShieldDownBefore && bShieldDownAfter;
				Feedback.bZeroDamage = (ActualDamage <= 0.0f);
				Feedback.HitActor = HitActor;
				Feedback.FeedbackSet = FeedbackSet;

				WeaponOwner->OnWeaponHitFeedback(Feedback);
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("[HITSCAN_DEBUG] NO DAMAGE THIS SHOT (classic): pawnHits=%d (beam to %s)"),
			PawnHits.Num(), bHitWall ? *GetNameSafe(WallHit.GetActor()) : TEXT("max range"));
	}

	// --- Visuals: the trace runs from the camera, but the beam must leave the MUZZLE ---
	// Reflected segments (ReflectionCount > 0) start at the bounce point instead.
	FVector BeamStart = Start;
	if (ReflectionCount == 0 && PawnOwner && PawnOwner->IsPlayerControlled() && FirstPersonMesh)
	{
		// Where the barrel LOOKS like it is, not where it stands: the first-person mesh is rendered
		// through a transform of its own, and a tracer put at the socket's real position starts off
		// to the side of the gun you can see. See GetFirstPersonMuzzleRenderLocation.
		BeamStart = GetFirstPersonMuzzleRenderLocation();
	}

	// Does the socket's WORLD position land on the barrel you can see? The yellow sphere is drawn
	// as ordinary world geometry, so if it sits on the barrel tip on screen, the first-person mesh
	// is rendered where its transform says and the tracer start is right; if the sphere is off to
	// the side of the gun, the mesh is being drawn through a transform this code does not know
	// about. Red sphere = MuzzleSocketName does not exist on this mesh and GetSocketLocation
	// quietly handed back the component origin.
	if (bDrawHitscanDebug && FirstPersonMesh)
	{
		const bool bSocketExists = FirstPersonMesh->DoesSocketExist(MuzzleSocketName);
		const FVector SocketWorld = FirstPersonMesh->GetSocketLocation(MuzzleSocketName);
		DrawDebugSphere(GetWorld(), SocketWorld, 3.0f, 8,
			bSocketExists ? FColor::Yellow : FColor::Red, false, 5.0f);
		DrawDebugSphere(GetWorld(), BeamStart, 2.0f, 8, FColor::Magenta, false, 5.0f);

		const APlayerController* DebugPC = PawnOwner ? Cast<APlayerController>(PawnOwner->GetController()) : nullptr;
		const FMinimalViewInfo DebugPOV = (DebugPC && DebugPC->PlayerCameraManager)
			? DebugPC->PlayerCameraManager->GetCameraCacheView()
			: FMinimalViewInfo();
		UE_LOG(LogTemp, Warning, TEXT("[MUZZLE_DEBUG] socketExists=%d socket=%s beamStart=%s | POV loc=%s useFPParams=%d fpScale=%.3f fov=%.1f fpFov=%.1f"),
			bSocketExists ? 1 : 0,
			*SocketWorld.ToCompactString(),
			*BeamStart.ToCompactString(),
			*DebugPOV.Location.ToCompactString(),
			DebugPOV.bUseFirstPersonParameters ? 1 : 0,
			DebugPOV.FirstPersonScale,
			DebugPOV.FOV,
			DebugPOV.FirstPersonFOV);
	}

	// A travelling shot is drawn along its whole line for the same reason it flies it: where it
	// actually stops is not decided yet. An instant one still stops at the body it hit.
	const FVector BeamEnd = (bPawnWasHit && !bHitscanTravelsAsBolt)
		? PawnHitLocation
		: (bHitWall ? FVector(WallHit.ImpactPoint) : End);

	// --- Visual debug: the real bullet path vs the visual tracer ---
	if (bDrawHitscanDebug)
	{
		const float DebugDuration = 5.0f;

		// Actual trace ray from the camera (this is where damage is decided)
		DrawDebugLine(GetWorld(), Start, BeamEnd, FColor::Cyan, false, DebugDuration, 0, 0.5f);

		// Thin sweep corridor: green = pawn damaged, red = nothing damaged.
		// Capped in length on purpose. Drawn to the full trace distance it is a 12-sided cylinder
		// a kilometre long whose near end sits a metre from the eye, and its side lines fill the
		// whole screen with a red starburst that hides everything else in here.
		const float DebugCorridorLength = FMath::Min(WallDistance, 1000.0f);
		DrawDebugCylinder(GetWorld(), Start, Start + Direction * DebugCorridorLength, SweepRadius, 12,
			bPawnWasHit ? FColor::Green : FColor::Red, false, DebugDuration, 0, 0.75f);

		// Visual tracer line from the muzzle — the gap to the cyan ray is the muzzle parallax
		DrawDebugLine(GetWorld(), BeamStart, BeamEnd, FColor::White, false, DebugDuration, 0, 0.25f);

		// Wall hit point
		if (bHitWall)
		{
			DrawDebugSphere(GetWorld(), WallHit.ImpactPoint, 8.0f, 8, FColor::Red, false, DebugDuration);
		}

		// Pawn candidates: orange = found by the sweep, green = the one that took the damage
		for (int32 i = 0; i < PawnHits.Num(); ++i)
		{
			const FHitResult& Hit = PawnHits[i];
			if (!Hit.GetActor())
			{
				continue;
			}
			const FVector Loc = Hit.ImpactPoint.IsNearlyZero() ? Hit.GetActor()->GetActorLocation() : FVector(Hit.ImpactPoint);
			if (i == BestIndex)
			{
				DrawDebugSphere(GetWorld(), Loc, 14.0f, 12, FColor::Green, false, DebugDuration);
			}
			else
			{
				DrawDebugSphere(GetWorld(), Loc, 8.0f, 8, FColor::Orange, false, DebugDuration);
			}
		}
	}

	// --- Send the shot on its way, tracer and damage as one thing ---
	//
	// The tracer is timed off the bolt: same speed, same variance, same length, same seed, so it is
	// not a streak that resembles the pellet, it IS the pellet. The bolt is then handed that streak
	// and puts it out where the pellet actually stops, which is the only place that knows.
	//
	// The line it flies is the whole line, not just as far as whoever happens to be standing on it:
	// that pawn may step aside before it arrives, and then the pellet carries on into the wall
	// behind them and marks that instead.
	if (bHitscanTravelsAsBolt)
	{
		const float RandSpeed = FMath::Max(
			HitscanBoltSpeed + HitscanBoltSpeedVariance * FMath::Sin(BoltRandomSeed), 1.0f);

		UNiagaraComponent* Tracer = SpawnBeamEffect(BeamStart, BeamEnd, RemainingEnergy,
			HitscanBoltSpeed, HitscanBoltSpeedVariance, HitscanBoltLength, BoltRandomSeed);

		if (UEnemyBeamBoltSubsystem* BoltSys = GetWorld()->GetSubsystem<UEnemyBeamBoltSubsystem>())
		{
			BoltSys->RegisterBolt(this, BoltVictim, Start, Direction, WallDistance, RandSpeed,
				HitscanBoltLength, HitscanBoltRadius, RemainingEnergy,
				BoltDamageMultiplier, BoltHitBone, WallHit, bHitWall, Tracer);
		}

		UE_LOG(LogTemp, Verbose, TEXT("[BOLT_DEBUG] %s: pellet away, target=%s line=%.0f speed=%.0f arrives in %.3fs"),
			*GetName(), *GetNameSafe(BoltVictim), WallDistance, RandSpeed, WallDistance / RandSpeed);
	}
	else
	{
		SpawnBeamEffect(BeamStart, BeamEnd, RemainingEnergy);
	}

	if (bUseWaveVisualization)
	{
		SpawnWaveFronts(BeamStart, BeamEnd);
	}

	// The impact of a travelling shot is the bolt's business: it plays where the pellet stops and
	// at the moment it stops there. Playing it now would put a hole in a wall the pellet has not
	// reached, on a target that may yet step out of the way.
	if (!bHitscanTravelsAsBolt)
	{
		if (bPawnWasHit)
		{
			SpawnImpactEffect(PawnHit);
		}
		else if (bHitWall)
		{
			SpawnImpactEffect(WallHit);
		}
	}

	// --- Metal reflection: only off a wall and only if no pawn intercepted the ray ---
	if (bHitWall && !bPawnWasHit && MaxReflections > 0 && IsMetal(WallHit) && ReflectionCount < MaxReflections)
	{
		const FVector ReflectedDir = CalculateReflection(Direction, WallHit.ImpactNormal);
		const float NewEnergy = RemainingEnergy * (1.0f - ReflectionEnergyLoss);

		UE_LOG(LogTemp, Verbose, TEXT("[HITSCAN_DEBUG] Classic reflection off %s (NewEnergy: %.2f)"),
			*GetNameSafe(WallHit.GetActor()), NewEnergy);

		SpawnReflectionEffect(WallHit.ImpactPoint, Direction, ReflectedDir);

		if (ReflectionSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, ReflectionSound, WallHit.ImpactPoint, NewEnergy);
		}

		PerformClassicHitscan(WallHit.ImpactPoint + ReflectedDir * 1.0f, ReflectedDir, NewEnergy, ReflectionCount + 1);
	}
}

float AShooterWeapon::GetTagDamageMultiplier(AActor* Target) const
{
	if (!Target || TagDamageMultipliers.Num() == 0)
	{
		return 1.0f;
	}

	float Multiplier = 1.0f;

	for (const auto& Pair : TagDamageMultipliers)
	{
		if (Target->ActorHasTag(Pair.Key))
		{
			Multiplier *= Pair.Value;
		}
	}

	return Multiplier;
}

// ==================== Reload ====================
//
// Ammunition is counted by whichever machine pulls the trigger: CurrentBullets is not replicated,
// and the server's copy of a client's weapon never decrements (see Server_ReportDamage). A reload
// follows the same rule -- it runs where the shooting is being counted, and needs no RPC of its own.
// What the authority alone decides, a granted magazine, still comes down through Client_SyncAmmoState.

bool AShooterWeapon::CanReload() const
{
	// A yanked weapon is thrown away when it runs dry rather than reloaded, whatever bUseReload says.
	return UsesReload()
		&& !bIsReloading
		&& CurrentBullets < MagazineSize
		// Nothing spare to load. Without this the gun would play the whole animation and come back
		// with the same rounds it started with.
		&& GetPooledAmmo() > CurrentBullets;
}

bool AShooterWeapon::StartReload()
{
	if (!CanReload())
	{
		return false;
	}

	bIsReloading = true;
	bReloadCommitted = false;

	// The refire timer would fire mid-reload and Fire() would bounce off bIsReloading, but a pending
	// shot surviving the reload is confusing to debug. Clear it and let FinishReload restart fire.
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);

	if (bPerRoundReload)
	{
		BeginPerRoundReload();
		return true;
	}

	// Decided once, here, and carried everywhere else. Recomputing it further down would read an
	// ammo count that the reload is in the middle of changing, and the two halves of the animation
	// could then disagree about which reload this is.
	const bool bSecondary = UsesSecondaryReload() && SecondaryReloadMontage != nullptr;
	const float ThisReloadTime = GetActiveReloadTime();

	ShellStage = bSecondary ? EWeaponReloadStage::Secondary : EWeaponReloadStage::Primary;
	PlayReloadStage(ShellStage);

	GetWorld()->GetTimerManager().SetTimer(ReloadTimer, this, &AShooterWeapon::FinishReload, ThisReloadTime, false);

	UE_LOG(LogTemp, Warning, TEXT("[RELOAD_DEBUG] %s: %s reload started, %d/%d rounds, %.2fs"),
		*GetName(), bSecondary ? TEXT("secondary") : TEXT("primary"),
		CurrentBullets, MagazineSize, ThisReloadTime);

	return true;
}

// ==================== Per round reload ====================
//
// Start, then Loop once per round, then End. Every step is scheduled off the length of the montage
// it just played, for the same reason the magazine reload is: a hand-typed duration drifts away
// from the animation the first time somebody retimes it, and the gun then loads a round while the
// hand is still reaching for it.

void AShooterWeapon::BeginPerRoundReload()
{
	ShellStage = EWeaponReloadStage::ShellStart;
	PlayReloadStage(ShellStage);

	const float OpeningTime = ReloadMontage ? ReloadMontage->GetPlayLength() : 0.0f;

	UE_LOG(LogTemp, Warning, TEXT("[RELOAD_DEBUG] %s: per round reload opening, %d/%d rounds, %.2fs"),
		*GetName(), CurrentBullets, MagazineSize, OpeningTime);

	GetWorld()->GetTimerManager().SetTimer(
		ReloadTimer, this, &AShooterWeapon::AdvancePerRoundReload, FMath::Max(0.01f, OpeningTime), false);
}

void AShooterWeapon::AdvancePerRoundReload()
{
	if (!bIsReloading)
	{
		return;
	}

	// Credit the round the loop that just ended put in. At the END rather than at the start, so an
	// interrupted loop gives nothing: the shell was still in the hand when the trigger was pulled.
	if (ShellStage == EWeaponReloadStage::ShellLoop)
	{
		const int32 Before = CurrentBullets;
		CurrentBullets = FMath::Min(MagazineSize, CurrentBullets + 1);

		// The shell came out of the energy reserve. The cells need nothing here: they count the
		// loaded rounds too, so moving one into the tube does not change what they hold.
		DrawEnergyReserve(CurrentBullets - Before);

		if (WeaponOwner)
		{
			WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
		}
	}

	// Full, or nothing left in the pouch. GetPooledAmmo answers with a full magazine for a weapon
	// that owns no cells, so an infinite-reserve gun simply stops when the tube is full.
	if (CurrentBullets >= MagazineSize || GetPooledAmmo() <= CurrentBullets)
	{
		EndPerRoundReload();
		return;
	}

	ShellStage = EWeaponReloadStage::ShellLoop;
	PlayReloadStage(ShellStage);

	const UAnimMontage* const LoopMontage = SecondaryReloadMontage ? SecondaryReloadMontage : ReloadMontage;
	const float LoopTime = LoopMontage ? LoopMontage->GetPlayLength() : 0.0f;

	GetWorld()->GetTimerManager().SetTimer(
		ReloadTimer, this, &AShooterWeapon::AdvancePerRoundReload, FMath::Max(0.01f, LoopTime), false);
}

void AShooterWeapon::EndPerRoundReload()
{
	ShellStage = EWeaponReloadStage::ShellEnd;

	const float ClosingTime = ReloadEndMontage ? ReloadEndMontage->GetPlayLength() : 0.0f;

	// No closing animation is a legitimate setup, and then the reload is simply over. Going through
	// FinishReload either way keeps the "gun is usable again" moment in one place.
	if (ClosingTime <= KINDA_SMALL_NUMBER)
	{
		FinishReload();
		return;
	}

	PlayReloadStage(ShellStage);

	UE_LOG(LogTemp, Warning, TEXT("[RELOAD_DEBUG] %s: per round reload closing, %d/%d rounds, %.2fs"),
		*GetName(), CurrentBullets, MagazineSize, ClosingTime);

	GetWorld()->GetTimerManager().SetTimer(
		ReloadTimer, this, &AShooterWeapon::FinishReload, ClosingTime, false);
}

void AShooterWeapon::InterruptPerRoundReload()
{
	if (!bIsReloading)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[RELOAD_DEBUG] %s: per round reload interrupted at %d/%d rounds"),
		*GetName(), CurrentBullets, MagazineSize);

	bIsReloading = false;
	ShellStage = EWeaponReloadStage::Primary;
	GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);

	// Deliberately no closing animation. The shot is what the player asked for and it has its own
	// animation; playing the bolt being closed first would put a beat between the trigger and the
	// bullet, which is exactly the delay interrupting was meant to remove.
}

int32 AShooterWeapon::GetPooledAmmo() const
{
	// Energy first: it owns no cells either, but unlike the two below its supply has an end. The
	// reserve holds only the unloaded rounds, so the whole supply is the two added together, which
	// is the shape CanReload and FinishReload already expect from the cells.
	if (UsesEnergyReserve())
	{
		return CurrentBullets + EnergyReserve;
	}

	// Two weapons own no cells and both answer the same way: the energy one that never reloads, and
	// the granted one whose magazine is real but whose reserve is endless. A full magazine is the
	// right answer for both, because it is what CanReload compares against and what FinishReload
	// clamps to, so the gun always has exactly one magazine available and never more.
	if (!OwnsAmmoCells())
	{
		return MagazineSize;
	}

	if (const AShooterCharacter* Character = Cast<AShooterCharacter>(PawnOwner))
	{
		if (const UInventoryComponent* Inventory = Character->GetInventoryComponent())
		{
			return Inventory->GetAmmo();
		}
	}

	// No inventory at all - an NPC holding this gun. They are not on the cell economy, so the
	// magazine behaves as it always did.
	return MagazineSize;
}

void AShooterWeapon::SpendPooledRound()
{
	// Server only. On a listen server the host's own Fire() reaches here directly; a remote
	// client's shot reaches the server through AShooterCharacter::Server_ReportWeaponFired, which
	// calls this for the same reason the ability passives are notified there.
	if (!HasAuthority())
	{
		return;
	}

	// An energy shot spends nothing from the reserve (the round was already loaded), but it is the
	// one moment the server hears about every shot on both routes, which makes it the place to hold
	// the refill back.
	if (UsesEnergyReserve())
	{
		PauseEnergyRegen();
		return;
	}

	if (!OwnsAmmoCells())
	{
		return;
	}

	if (AShooterCharacter* Character = Cast<AShooterCharacter>(PawnOwner))
	{
		if (UInventoryComponent* Inventory = Character->GetInventoryComponent())
		{
			Inventory->ConsumeAmmo(1);
		}
	}
}

void AShooterWeapon::FinishReload()
{
	if (bReloadCommitted)
	{
		return;
	}
	bReloadCommitted = true;
	bIsReloading = false;

	// A per round reload has already counted itself, one round per completed loop, and that count is
	// the whole point: filling the magazine here would hand back the rounds an interrupted reload
	// deliberately did not load.
	if (!bPerRoundReload)
	{
		// Topped up out of the magazine cells, not conjured. With one magazine allowed the pool IS what
		// is loaded, so this changes nothing until the meta grants a second one - which is exactly what
		// makes that upgrade worth buying.
		const int32 Before = CurrentBullets;
		CurrentBullets = FMath::Min(MagazineSize, GetPooledAmmo());

		// The cells count loaded rounds too, so for them this was only a move. The energy reserve
		// does not, so what went into the magazine has to come out of it.
		DrawEnergyReserve(CurrentBullets - Before);
	}

	ShellStage = EWeaponReloadStage::Primary;

	if (WeaponOwner)
	{
		WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
	}

	UE_LOG(LogTemp, Warning, TEXT("[RELOAD_DEBUG] %s: reload finished, %d rounds"), *GetName(), CurrentBullets);

	// The trigger was never released, so the weapon picks up where it left off. Semi-automatic
	// weapons deliberately do not: one pull is one shot, reload or no reload.
	if (bIsFiring && bFullAuto)
	{
		Fire();
	}
}

void AShooterWeapon::CancelReload()
{
	if (!bIsReloading)
	{
		return;
	}

	bIsReloading = false;
	ShellStage = EWeaponReloadStage::Primary;
	GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);

	UE_LOG(LogTemp, Warning, TEXT("[RELOAD_DEBUG] %s: reload cancelled at %d/%d rounds"),
		*GetName(), CurrentBullets, MagazineSize);
}

void AShooterWeapon::SuspendReloadForHolster()
{
	if (!bIsReloading) return;
	const UAnimMontage* Montage = GetActiveReloadMontage();
	float Progress = GetReloadProgress();
	if (AShooterCharacter* Character = Cast<AShooterCharacter>(PawnOwner))
	{
		if (USkeletalMeshComponent* Mesh = Character->GetFirstPersonMesh())
		{
			if (UAnimInstance* Anim = Mesh->GetAnimInstance(); Anim && Montage && Anim->Montage_IsPlaying(Montage))
			{
				Progress = FMath::Clamp(Anim->Montage_GetPosition(Montage) / FMath::Max(Montage->GetPlayLength(), KINDA_SMALL_NUMBER), 0.0f, 1.0f);
			}
		}
	}
	SuspendedReloadProgress = Progress;
	bReloadResumePending = true;
	bIsReloading = false;
	GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);
}

void AShooterWeapon::ResumeReloadAfterEquip()
{
	if (!bReloadResumePending || bReloadCommitted || !CanReload())
	{
		bReloadResumePending = false;
		return;
	}
	bReloadResumePending = false;
	bIsReloading = true;
	bReloadCommitted = false;
	PlayReloadStage(ShellStage);
	const float Remaining = FMath::Max(0.01f, GetActiveReloadTime() * (1.0f - SuspendedReloadProgress));
	GetWorld()->GetTimerManager().SetTimer(ReloadTimer, this, &AShooterWeapon::FinishReload, Remaining, false);
	if (AShooterCharacter* Character = Cast<AShooterCharacter>(PawnOwner))
	{
		if (USkeletalMeshComponent* Mesh = Character->GetFirstPersonMesh())
		{
			if (UAnimInstance* Anim = Mesh->GetAnimInstance())
			{
				if (UAnimMontage* Montage = GetActiveReloadMontage())
				{
					Anim->Montage_SetPosition(Montage, SuspendedReloadProgress * Montage->GetPlayLength());
				}
			}
		}
	}
}

void AShooterWeapon::CommitReloadFromNotify()
{
	// The named bolt-click notify is authoritative for the visual reload moment. The timer remains
	// a fallback for old montages, but this guard makes notify + fallback idempotent.
	if (!bIsReloading || bReloadCommitted)
	{
		return;
	}

	if (bPerRoundReload)
	{
		// Per-round montages already credit each shell at their loop boundary. Keep the named notify
		// harmless on those assets rather than filling the whole tube a second time.
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);
	FinishReload();
	UE_LOG(LogTemp, Log, TEXT("[RELOAD_DEBUG] %s: readiness notify committed reload once"), *GetName());
}

float AShooterWeapon::GetReloadProgress() const
{
	if (!bIsReloading || ReloadTime <= 0.0f)
	{
		return 0.0f;
	}

	const float Remaining = GetWorld()->GetTimerManager().GetTimerRemaining(ReloadTimer);
	return FMath::Clamp(1.0f - (Remaining / ReloadTime), 0.0f, 1.0f);
}

void AShooterWeapon::SetBulletCount(int32 NewCount)
{
	CurrentBullets = FMath::Clamp(NewCount, 0, MagazineSize);

	// The authority handing out a magazine is the one case the owning client cannot work out for
	// itself. Push it, along with whether this weapon refills or runs dry.
	if (HasAuthority())
	{
		Client_SyncAmmoState(CurrentBullets, bHasLimitedAmmo);
	}
}

void AShooterWeapon::Client_SyncAmmoState_Implementation(int32 InBullets, bool bInHasLimitedAmmo)
{
	CurrentBullets = FMath::Clamp(InBullets, 0, MagazineSize);
	bHasLimitedAmmo = bInHasLimitedAmmo;

	// The HUD reads this through the character, and only for the weapon actually in hand.
	if (PawnOwner)
	{
		if (AShooterCharacter* ShooterOwner = Cast<AShooterCharacter>(PawnOwner))
		{
			if (ShooterOwner->GetCurrentWeapon() == this)
			{
				ShooterOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
			}
		}
	}
}

// ==================== Energy reserve ====================
//
// The server owns the number and runs the refill. The owning client gets it by replication and
// takes rounds out of it the moment its own reload finishes, telling the server as it does. A shot
// never touches the reserve, so there is no traffic per shot beyond the report that already exists.

bool AShooterWeapon::UsesEnergyReserve() const
{
	// The owner decides, not the class: the same rifle is energy in a player's hands and endless in
	// an NPC's. By type rather than IsPlayerControlled, which is false on the server between spawn
	// and possession and would let a freshly granted gun miss its reserve.
	return (bRegeneratingReserve || bFiniteEnergyReserve) && !IsMeleeWeapon() && Cast<AShooterCharacter>(PawnOwner) != nullptr;
}

void AShooterWeapon::SetEnergyReserve(int32 Rounds)
{
	if (!HasAuthority() || !UsesEnergyReserve())
	{
		return;
	}

	const int32 Capacity = GetEnergyReserveCapacity();
	EnergyReserve = FMath::Clamp(Rounds, 0, Capacity);

	// Room left means the refill runs, and without the quiet period: nothing was just fired.
	if (EnergyReserve < Capacity)
	{
		ArmEnergyRegen(0.0f);
	}
	else
	{
		GetWorldTimerManager().ClearTimer(EnergyRegenTimer);
	}

	RefreshOwnerAmmoHUD();

	UE_LOG(LogTemp, Log, TEXT("[ENERGY_AMMO] %s: reserve set to %d/%d"), *GetName(), EnergyReserve, Capacity);
}

void AShooterWeapon::ConfigureFiniteEnergyReserve()
{
	if (!HasAuthority() || IsMeleeWeapon())
	{
		return;
	}

	bRegeneratingReserve = false;
	bFiniteEnergyReserve = true;
	GetWorldTimerManager().ClearTimer(EnergyRegenTimer);
}

int32 AShooterWeapon::GetDispenserFuelValue(int32 LoadedRounds, int32 ReserveRounds) const
{
	const int32 Rounds = FMath::Max(0, LoadedRounds) + FMath::Max(0, ReserveRounds);
	const int32 FullRounds = FMath::Max(1, GetMagazineSize() + GetEnergyReserveCapacity());
	const float AmmoFraction = FMath::Clamp(static_cast<float>(Rounds) / FullRounds, 0.0f, 1.0f);
	return FMath::Max(0, EmptyWeaponFuelValue + FMath::RoundToInt(FullAmmoFuelValue * AmmoFraction));
}

void AShooterWeapon::PauseEnergyRegen(float ExtraSeconds)
{
	if (!HasAuthority() || !UsesEnergyReserve())
	{
		return;
	}

	// Full: nothing to hold back. The next reload that takes rounds out arms the refill itself.
	if (EnergyReserve >= GetEnergyReserveCapacity())
	{
		GetWorldTimerManager().ClearTimer(EnergyRegenTimer);
		return;
	}

	ArmEnergyRegen(EnergyRegenDelay + FMath::Max(0.0f, ExtraSeconds));
}

void AShooterWeapon::ArmEnergyRegen(float FirstDelay)
{
	if (!bRegeneratingReserve)
	{
		return;
	}
	// One round at a time, at whatever rate refills a whole magazine in EnergySecondsPerMagazine.
	// Worked out from the live MagazineSize, so a bigger magazine refills faster in rounds per
	// second and takes the same time per magazine, which is the Apex rule.
	const float Interval = FMath::Max(0.01f, EnergySecondsPerMagazine / FMath::Max(1, MagazineSize));

	GetWorldTimerManager().SetTimer(
		EnergyRegenTimer, this, &AShooterWeapon::TickEnergyRegen, Interval, true, FirstDelay + Interval);
}

void AShooterWeapon::TickEnergyRegen()
{
	const int32 Capacity = GetEnergyReserveCapacity();
	if (!bRegeneratingReserve || !UsesEnergyReserve() || EnergyReserve >= Capacity)
	{
		GetWorldTimerManager().ClearTimer(EnergyRegenTimer);
		return;
	}

	++EnergyReserve;
	RefreshOwnerAmmoHUD();

	if (EnergyReserve >= Capacity)
	{
		GetWorldTimerManager().ClearTimer(EnergyRegenTimer);
	}
}

void AShooterWeapon::DrawEnergyReserve(int32 Rounds)
{
	if (Rounds <= 0 || !UsesEnergyReserve())
	{
		return;
	}

	// Taken here at once, on whichever machine reloaded, so the HUD and the next CanReload are right
	// without waiting a round trip. The server does the same sum and replicates the same answer.
	EnergyReserve = FMath::Max(0, EnergyReserve - Rounds);

	if (HasAuthority())
	{
		PauseEnergyRegen();
	}
	else
	{
		Server_DrawEnergyReserve(Rounds);
	}
}

void AShooterWeapon::Server_DrawEnergyReserve_Implementation(int32 Rounds)
{
	// Clamped, and it can only ever lower the number: the worst a lying client can do is empty its
	// own gun.
	DrawEnergyReserve(FMath::Clamp(Rounds, 0, MagazineSize));
}

void AShooterWeapon::OnRep_EnergyReserve()
{
	RefreshOwnerAmmoHUD();
}

void AShooterWeapon::RefreshOwnerAmmoHUD()
{
	// Widgets exist only on the machine the player is sitting at. The server's copy of a client's
	// gun refilling has nobody to show it to.
	if (!PawnOwner || !PawnOwner->IsLocallyControlled())
	{
		return;
	}

	// The weapon in hand, not this one: the event carries a single pair of numbers, and every
	// listener reads it as the gun the player is holding.
	if (AShooterCharacter* ShooterOwner = Cast<AShooterCharacter>(PawnOwner))
	{
		if (const AShooterWeapon* Held = ShooterOwner->GetCurrentWeapon())
		{
			ShooterOwner->UpdateWeaponHUD(Held->GetBulletCount(), Held->GetMagazineSize());
		}
	}
}

// ==================== Magazine attachment ====================

void AShooterWeapon::ApplyMagazineModifiers()
{
	// Too early (BeginPlay has not taken the base yet, and will call this itself), or a weapon whose
	// "magazine" is something else entirely: melee keeps its hit count there.
	if (BaseMagazineSize <= 0 || IsMeleeWeapon())
	{
		return;
	}

	const UWeaponAttachmentDefinition* Mag = GetAttachmentOfType(EWeaponAttachmentType::Magazine);

	// The gold perk's poll runs only while a magazine that has it is fitted.
	if (Mag && Mag->bReloadsWhileHolstered)
	{
		if (!GetWorldTimerManager().IsTimerActive(HolsteredReloadTimer))
		{
			HolsteredSince = -1.0f;
			GetWorldTimerManager().SetTimer(
				HolsteredReloadTimer, this, &AShooterWeapon::PollHolsteredReload, 0.25f, true);
		}
	}
	else
	{
		GetWorldTimerManager().ClearTimer(HolsteredReloadTimer);
	}

	// The magazine's own number for this gun, or the gun's own size with none fitted. Never derived
	// from the current size, so swapping a blue mag for a purple one lands on the purple number.
	// Zero from the table means this gun is not listed; InstallAttachment refuses that, so it only
	// happens for a table edited while the magazine was already on, and then the base is the answer.
	const int32 Listed = Mag ? Mag->GetMagazineSizeFor(GetClass()) : 0;
	const int32 NewSize = FMath::Clamp(Listed > 0 ? Listed : BaseMagazineSize, 1, 999);
	if (NewSize == MagazineSize)
	{
		return;
	}

	const int32 OldSize = MagazineSize;
	MagazineSize = NewSize;

	// A smaller magazine cannot keep what the bigger one held. The spare rounds are lost rather than
	// handed back: the loaded count lives on the shooter's machine and the reserve on the server, and
	// a client ADDING to its reserve is the one direction the server never takes its word for.
	CurrentBullets = FMath::Min(CurrentBullets, MagazineSize);

	// The energy reserve is counted in magazines, so its capacity just moved with the magazine.
	// Going through the setter clamps a shrunk reserve and restarts the refill, which also picks up
	// the new per-round interval.
	if (HasAuthority() && UsesEnergyReserve())
	{
		SetEnergyReserve(EnergyReserve);
	}

	RefreshOwnerAmmoHUD();

	UE_LOG(LogTemp, Log, TEXT("[ATTACH] %s: magazine %d -> %d (base %d, %s)"),
		*GetName(), OldSize, MagazineSize, BaseMagazineSize, Mag ? *Mag->GetName() : TEXT("no magazine"));
}

void AShooterWeapon::PollHolsteredReload()
{
	// Rounds are counted on the machine that pulls the trigger, so that is the only one allowed to
	// load them. Everywhere else this poll has nothing to do.
	if (!PawnOwner || !PawnOwner->IsLocallyControlled())
	{
		return;
	}

	const AShooterCharacter* ShooterOwner = Cast<AShooterCharacter>(PawnOwner);
	if (!ShooterOwner)
	{
		return;
	}

	// In hand, including while stowed for the grapple: that is not a holster, the gun comes straight
	// back. Empty hands by the player's own choice DO count, the way holstering both guns does in
	// Apex: CurrentWeapon keeps pointing at the put-away gun then, so it needs its own test.
	const bool bInHand = ShooterOwner->GetCurrentWeapon() == this
		&& ShooterOwner->GetWeaponSwitchPhase() != EWeaponSwitchPhase::StowedByPlayer;
	if (bInHand)
	{
		HolsteredSince = -1.0f;
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (HolsteredSince < 0.0f)
	{
		HolsteredSince = Now;
		return;
	}

	const UWeaponAttachmentDefinition* Mag = GetAttachmentOfType(EWeaponAttachmentType::Magazine);
	const float Delay = Mag ? Mag->HolsteredReloadDelay : 0.0f;
	if (Now - HolsteredSince < Delay || !CanReload())
	{
		return;
	}

	// The same sum as FinishReload, minus the animation: nobody is looking at this gun.
	const int32 Before = CurrentBullets;
	CurrentBullets = FMath::Min(MagazineSize, GetPooledAmmo());
	DrawEnergyReserve(CurrentBullets - Before);
	RefreshOwnerAmmoHUD();

	UE_LOG(LogTemp, Log, TEXT("[ATTACH] %s: reloaded in the holster, %d -> %d rounds"),
		*GetName(), Before, CurrentBullets);
}

bool AShooterWeapon::IsIonizationCapReached(float CurrentCharge, float Cap) const
{
	// GetShotIonization, not the raw field: which DIRECTION this weapon drives a target's charge is
	// what decides whether the cap has been reached, and a payload that overrides the amount can
	// flip that sign. Reading the gun's number here while the round applied its own is how the
	// shield gate and the thing filling the meter would come apart.
	float ChargePerHit = 0.0f;
	GetShotIonization(ChargePerHit);
	return IsIonizationCapReached(CurrentCharge, Cap, ChargePerHit);
}

bool AShooterWeapon::IsIonizationCapReached(float CurrentCharge, float Cap, float ChargePerHit) const
{
	// A cap is a MAGNITUDE, and this used to be tested as "CurrentCharge >= Max". With a negative
	// IonizationChargePerHit -- the default for the electrifying weapons -- a target's charge only
	// ever went down, so that test was never true and the Min() alongside it never clamped anything:
	// the charge ran away with no ceiling at all. "Fully charged" was therefore not a state anything
	// could reach, which is what made props grabbable at charges nowhere near maximum.
	const float AbsCap = FMath::Abs(Cap);
	if (AbsCap <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const bool bSameDirection = (CurrentCharge * ChargePerHit) > 0.0f;
	return bSameDirection && FMath::Abs(CurrentCharge) >= AbsCap;
}

float AShooterWeapon::ApplyIonizationStep(float CurrentCharge, float Cap) const
{
	// Same reason as IsIonizationCapReached above: the amount comes from whatever this weapon is
	// actually putting in the air, which a payload may have overridden.
	float ChargePerHit = 0.0f;
	GetShotIonization(ChargePerHit);
	return ApplyIonizationStep(CurrentCharge, Cap, ChargePerHit);
}

float AShooterWeapon::ApplyIonizationStep(float CurrentCharge, float Cap, float ChargePerHit) const
{
	const float AbsCap = FMath::Abs(Cap);
	const float Stepped = CurrentCharge + ChargePerHit;
	return AbsCap > KINDA_SMALL_NUMBER ? FMath::Clamp(Stepped, -AbsCap, AbsCap) : Stepped;
}

bool AShooterWeapon::ShouldWithholdDamageForShield(AActor* Target) const
{
	return bRequiresBrokenShieldToDamage && !IsTargetShieldDown(Target);
}

bool AShooterWeapon::ApplyHitscanIonization(AActor* Target, UPrimitiveComponent* HitComponent)
{
	// The round has the last word on whether this shot charges anything and by how much, so the
	// question is asked once, in GetShotIonization, and the weapon's own checkbox is only part of
	// the answer. A trace has no payload and falls straight through to the weapon's numbers.
	float ChargePerHit = 0.0f;
	const bool bIonizes = GetShotIonization(ChargePerHit);

	UE_LOG(LogTemp, Verbose, TEXT("[ION_DEBUG] ApplyHitscanIonization called: target=%s hitComp=%s ionizes=%d charge=%.2f"),
		*GetNameSafe(Target),
		HitComponent ? *HitComponent->GetName() : TEXT("null"),
		bIonizes, ChargePerHit);

	if (!bIonizes)
	{
		return false;
	}

	return ApplyIonizationToTarget(Target, HitComponent, ChargePerHit);
}

bool AShooterWeapon::ApplyIonizationToTarget(AActor* Target, UPrimitiveComponent* HitComponent, float ChargePerHit)
{
	if (!Target)
	{
		return false;
	}

	// NPC riot-shield rule: hit on body while shield is up → no charge transfer.
	// Player must hit the shield mesh to electrify the NPC behind it.
	if (UNPCRiotShieldComponent::ShouldBlockBodyIonization(Target, HitComponent))
	{
		UE_LOG(LogTemp, Verbose, TEXT("[ION_DEBUG] ApplyHitscanIonization: BLOCKED by shield rule"));
		return false;
	}

	UE_LOG(LogTemp, Verbose, TEXT("[ION_DEBUG] ApplyHitscanIonization: PASSED, applying charge to %s"), *Target->GetName());

	// Charging something is a change to the world, and until now a client only ever made it to its
	// own copy: ionization carries no damage, so it never travelled with a damage report, and the
	// starting weapon deals no damage at all. Tell the server. The local application below still
	// runs, so the shooter sees the prop light up immediately, and the authority's value replicates
	// back over the top of it a round trip later.
	if (PawnOwner && !PawnOwner->HasAuthority())
	{
		if (AShooterCharacter* ShooterOwner = Cast<AShooterCharacter>(PawnOwner))
		{
			ShooterOwner->Server_ReportIonization(Target, this);
		}
	}

	// Notify upgrade system of ionization-eligible hit. Fires once per valid hit regardless
	// of whether the target was already at max charge — upgrades (e.g. PistolStun) gate
	// per-target spam themselves via their own cooldowns.
	if (PawnOwner)
	{
		if (UUpgradeManagerComponent* UpgradeMgr = PawnOwner->FindComponentByClass<UUpgradeManagerComponent>())
		{
			UpgradeMgr->NotifyOwnerHitscanIonized(Target);
		}
	}

	// While an enemy is opened by the Wizard's bolt, the ionization this shot would have put into its
	// shield goes into its health instead.
	//
	// This is the SECOND place that has to know: the laser has its own per-second ionization path and
	// every other weapon comes through here per hit. Hooking only the beam meant the mechanic worked
	// for exactly one weapon nobody was holding.
	if (AShooterNPC* OpenedNPC = Cast<AShooterNPC>(Target))
	{
		if (OpenedNPC->IsShieldBypassed())
		{
			if (HasAuthority())
			{
				// Magnitude: ionization is signed, and a negative rate multiplied through produces
				// negative damage, which TakeDamage silently discards.
				const float RedirectedDamage = FMath::Abs(ChargePerHit)
					* OpenedNPC->ShieldBypassDamageMultiplier;

				FPointDamageEvent DamageEvent;
				DamageEvent.DamageTypeClass = UDamageType::StaticClass();
				OpenedNPC->TakeDamage(RedirectedDamage, DamageEvent, GetInstigatorController(), this);

				UE_LOG(LogTemp, Verbose, TEXT("[ABILITY_DEBUG] Redirected %.1f ionization into health on %s"),
					RedirectedDamage, *OpenedNPC->GetName());
			}
			return true;
		}
	}

	// Try UEMFVelocityModifier first (for characters/NPCs)
	if (UEMFVelocityModifier* TargetModifier = Target->FindComponentByClass<UEMFVelocityModifier>())
	{
		// Use GetCharge() to read actual FieldComponent charge (not BaseCharge which may be stale
		// after melee's SetCharge() calls that bypass BaseCharge tracking)
		const float CurrentCharge = TargetModifier->GetCharge();

		// The NPC's own ceiling, the same one IsAtMaxCharge() and the grab gate read.
		const float Cap = TargetModifier->MaxBaseCharge;
		if (IsIonizationCapReached(CurrentCharge, Cap, ChargePerHit))
		{
			return false;
		}

		TargetModifier->SetCharge(ApplyIonizationStep(CurrentCharge, Cap, ChargePerHit));
		return true;
	}

	// Route through SetCharge() for props (enables physics on first charge)
	if (AEMFPhysicsProp* Prop = Cast<AEMFPhysicsProp>(Target))
	{
		// The prop's own ceiling. What the weapon can push to is the prop's business, not the weapon's.
		const float CurrentCharge = Prop->GetCharge();
		if (IsIonizationCapReached(CurrentCharge, Prop->MaxCharge, ChargePerHit))
		{
			return false;
		}
		Prop->SetCharge(ApplyIonizationStep(CurrentCharge, Prop->MaxCharge, ChargePerHit));
		return true;
	}

	// Generic fallback: raw UEMF_FieldComponent
	if (UEMF_FieldComponent* TargetField = Target->FindComponentByClass<UEMF_FieldComponent>())
	{
		FEMSourceDescription Desc = TargetField->GetSourceDescription();
		const float CurrentCharge = Desc.PointChargeParams.Charge;

		// A bare field component has no cap of its own, so the weapon's number is the only ceiling
		// available on this path.
		if (IsIonizationCapReached(CurrentCharge, MaxIonizationCharge, ChargePerHit))
		{
			return false;
		}

		Desc.PointChargeParams.Charge = ApplyIonizationStep(CurrentCharge, MaxIonizationCharge, ChargePerHit);
		TargetField->SetSourceDescription(Desc);
		return true;
	}

	return false;
}

float AShooterWeapon::CalculateWaveRadius(float Distance) const
{
	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â£ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ WaveDivergence
	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ WaveDivergence = 0, ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» = 0 (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â)
	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ WaveDivergence = 1, ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» = MaxDivergenceAngle
	float DivergenceAngle = WaveDivergence * MaxDivergenceAngle;
	float TangentAngle = FMath::Tan(FMath::DegreesToRadians(DivergenceAngle));

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â = ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â + ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼
	float Radius = InitialWaveRadius + Distance * TangentAngle;

	return Radius;
}

float AShooterWeapon::CalculateDamageMultiplier(float Distance, float WaveRadius) const
{
	// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ <= ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸, ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â² ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢
	if (WaveRadius <= TargetEffectiveRadius)
	{
		return 1.0f;
	}

	// ÃƒÆ’Ã‚ÂÃƒâ€¹Ã…â€œÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ = (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ / ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹)
	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ~ RÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â², ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢: ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ = (TargetRadius / WaveRadius)ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â²
	float AreaRatio = (TargetEffectiveRadius * TargetEffectiveRadius) / (WaveRadius * WaveRadius);

	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼
	return FMath::Max(AreaRatio, MinDamageMultiplier);
}

// ==================== VFX ====================

float AShooterWeapon::GetOwnerCharge() const
{
	if (!PawnOwner)
	{
		return 0.0f;
	}

	UEMF_FieldComponent* FieldComp = PawnOwner->FindComponentByClass<UEMF_FieldComponent>();
	if (!FieldComp)
	{
		return 0.0f;
	}

	return FieldComp->GetSourceDescription().PointChargeParams.Charge;
}

void AShooterWeapon::SpawnMuzzleFlashEffect()
{
	if (CVarNoVFX.GetValueOnGameThread() != 0)
	{
		return;
	}

	// Determine which VFX to use
	UNiagaraSystem* VFXToSpawn = MuzzleFlashFX;

	// Check if charge-based muzzle flash is enabled
	if (bUseChargeMuzzleFlash)
	{
		float OwnerCharge = GetOwnerCharge();

		if (OwnerCharge > 0.0f && PositiveMuzzleFlashFX)
		{
			VFXToSpawn = PositiveMuzzleFlashFX;
		}
		else if (OwnerCharge < 0.0f && NegativeMuzzleFlashFX)
		{
			VFXToSpawn = NegativeMuzzleFlashFX;
		}
		// If charge is neutral or appropriate VFX is not set, fall back to default MuzzleFlashFX
	}

	if (!VFXToSpawn)
	{
		return;
	}

	// Spawn attached to muzzle socket so VFX follows weapon movement
	UNiagaraComponent* MuzzleComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
		VFXToSpawn,
		FirstPersonMesh,
		MuzzleSocketName,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		FVector(MuzzleFlashScale),
		EAttachLocation::SnapToTarget,
		true,
		ENCPoolMethod::None
	);

	if (MuzzleComp)
	{
		// Set muzzle flash parameters
		MuzzleComp->SetColorParameter(FName("FlashColor"), MuzzleFlashColor);
		MuzzleComp->SetFloatParameter(FName("Intensity"), MuzzleFlashIntensity);
		MuzzleComp->SetFloatParameter(FName("Duration"), MuzzleFlashDuration);

		// Pass wave-specific parameters if using wave visualization
		if (bUseWaveVisualization)
		{
			MuzzleComp->SetFloatParameter(FName("Wavelength"), Wavelength);
			MuzzleComp->SetFloatParameter(FName("Amplitude"), Amplitude);
			MuzzleComp->SetColorParameter(FName("EFieldColor"), EFieldColor);
			MuzzleComp->SetColorParameter(FName("BFieldColor"), BFieldColor);
		}

		// Pass beam color for consistency
		MuzzleComp->SetColorParameter(FName("BeamColor"), BeamColor);
	}
}

UNiagaraComponent* AShooterWeapon::SpawnBeamEffect(const FVector& Start, const FVector& End, float EnergyMultiplier,
	float OverrideBoltSpeed, float OverrideBoltSpeedVariance, float OverrideBoltLength, float OverrideRandomSeed)
{
	// Draw it here immediately, then make sure everyone else draws it too. Same split as the
	// muzzle flash: the shooter must not wait a round trip to see their own tracer.
	UNiagaraComponent* LocalBeam = SpawnBeamEffectLocally(Start, End, EnergyMultiplier,
		OverrideBoltSpeed, OverrideBoltSpeedVariance, OverrideBoltLength, OverrideRandomSeed);

	if (HasAuthority())
	{
		Multicast_PlayBeamEffect(Start, End, EnergyMultiplier,
			OverrideBoltSpeed, OverrideBoltSpeedVariance, OverrideBoltLength, OverrideRandomSeed);
	}
	else if (AShooterCharacter* OwnerCharacter = Cast<AShooterCharacter>(PawnOwner))
	{
		OwnerCharacter->Server_ReportBeamEffect(this, Start, End, EnergyMultiplier,
			OverrideBoltSpeed, OverrideBoltSpeedVariance, OverrideBoltLength, OverrideRandomSeed);
	}

	return LocalBeam;
}

void AShooterWeapon::Multicast_PlayBeamEffect_Implementation(const FVector& Start, const FVector& End,
	float EnergyMultiplier, float OverrideBoltSpeed, float OverrideBoltSpeedVariance,
	float OverrideBoltLength, float OverrideRandomSeed)
{
	// The shooter already drew it the instant they fired.
	const bool bIsShooter = PawnOwner && PawnOwner->IsLocallyControlled();
	if (bIsShooter)
	{
		return;
	}

	// Start over from OUR muzzle. The incoming Start came from the shooter's FIRST-person mesh,
	// which hangs off their camera and is only ever visible to them: replayed here it puts the
	// tracer somewhere around a teammate's head instead of at the gun we can actually see. The
	// endpoint is genuine world data and is kept as sent.
	FVector ObserverStart = Start;
	if (ThirdPersonMesh)
	{
		ObserverStart = ThirdPersonMesh->GetSocketLocation(MuzzleSocketName);
	}

	SpawnBeamEffectLocally(ObserverStart, End, EnergyMultiplier,
		OverrideBoltSpeed, OverrideBoltSpeedVariance, OverrideBoltLength, OverrideRandomSeed);
}

UNiagaraComponent* AShooterWeapon::SpawnBeamEffectLocally(const FVector& Start, const FVector& End, float EnergyMultiplier,
	float OverrideBoltSpeed, float OverrideBoltSpeedVariance, float OverrideBoltLength, float OverrideRandomSeed)
{
	if (CVarNoVFX.GetValueOnGameThread() != 0)
	{
		return nullptr;
	}

	if (!BeamFX)
	{
		return nullptr;
	}

	// Spawned INACTIVE on purpose. Activating first and setting the endpoints afterwards is what the
	// engine turns into SetVariable_Deferred (NiagaraComponent.cpp): once a system instance exists,
	// a parameter write only lands on the NEXT tick, so the first simulated frame runs on the
	// asset's defaults -- BeamStart and BeamEnd both zero. That is the stray tracer that starts
	// nowhere near the muzzle and runs off to the horizon, and it shows up on some shots and not
	// others because it depends on where the frame boundary falls. Set everything, then activate.
	UNiagaraComponent* BeamComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		BeamFX,
		Start,
		(End - Start).Rotation(),
		FVector::OneVector,
		/*bAutoDestroy*/ true,
		/*bAutoActivate*/ false,
		ENCPoolMethod::None
	);

	if (BeamComp)
	{
		// ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‹Å“ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹
		BeamComp->SetVectorParameter(FName("BeamStart"), Start);
		BeamComp->SetVectorParameter(FName("BeamEnd"), End);
		BeamComp->SetFloatParameter(FName("Energy"), EnergyMultiplier);
		BeamComp->SetColorParameter(FName("BeamColor"), BeamColor);
		BeamComp->SetFloatParameter(FName("Distance"), FVector::Dist(Start, End));

		const float UsedRandomSeed = (OverrideRandomSeed >= 0.0f) ? OverrideRandomSeed : (FMath::FRand() * 1000.0f);
		BeamComp->SetFloatParameter(FName("RandomSeed"), UsedRandomSeed);

		// Low-HP dodgeable bolt: override the tracer's Speed / SpeedVariance / beamLength so the
		// visible bolt matches the C++ damage region (UEnemyBeamBoltSubsystem). Requires the enemy
		// beam Niagara asset to read these as User parameters and feed them into its HLSL node.
		if (OverrideBoltSpeed >= 0.0f)
		{
			BeamComp->SetFloatParameter(FName("Speed"), OverrideBoltSpeed);
			BeamComp->SetFloatParameter(FName("SpeedVariance"), OverrideBoltSpeedVariance);
			BeamComp->SetFloatParameter(FName("beamLength"), OverrideBoltLength);

			// And it goes out when it gets there. The streak flies at the speed the HLSL will
			// compute from this seed, so how long the flight takes is known here: past that moment
			// the bolt has either hit or been dodged, and a streak still crawling along an empty
			// line is a lie either way. In the tracer asset "BeamFadeTime" is the particle lifetime
			// and "FadeTime" the emitter's loop duration, so both are the flight.
			const float RandSpeed = FMath::Max(
				OverrideBoltSpeed + OverrideBoltSpeedVariance * FMath::Sin(UsedRandomSeed), 1.0f);
			const float FlightTime = FVector::Dist(Start, End) / RandSpeed;

			BeamComp->SetFloatParameter(FName("BeamFadeTime"), FlightTime);
			BeamComp->SetFloatParameter(FName("FadeTime"), FlightTime);
		}
		UE_LOG(LogTemp, Verbose, TEXT("BeamFX Distance: %.1f, Start: %s, End: %s"), FVector::Dist(Start, End), *Start.ToString(), *End.ToString());

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½
		FVector UpVector = FVector::UpVector;
		FVector RightVector = FVector::RightVector;

		if (PawnOwner)
		{
			if (AController* Controller = PawnOwner->GetController())
			{
				FRotator CameraRotation;
				FVector CameraLocation;
				Controller->GetPlayerViewPoint(CameraLocation, CameraRotation);

				UpVector = CameraRotation.Quaternion().GetUpVector();
				RightVector = CameraRotation.Quaternion().GetRightVector();
			}
		}

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		BeamComp->SetVectorParameter(FName("UpVector"), UpVector);
		BeamComp->SetVectorParameter(FName("RightVector"), RightVector);

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€¹Ã¢â‚¬Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
		float BeamDistance = FVector::Distance(Start, End);
		float StartRadius = CalculateWaveRadius(0.0f);
		float EndRadius = CalculateWaveRadius(BeamDistance);

		BeamComp->SetFloatParameter(FName("StartRadius"), StartRadius);
		BeamComp->SetFloatParameter(FName("EndRadius"), EndRadius);
		BeamComp->SetFloatParameter(FName("MaxDivergenceAngle"), MaxDivergenceAngle);
		BeamComp->SetFloatParameter(FName("TargetRadius"), TargetEffectiveRadius);

		// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â)
		BeamComp->SetFloatParameter(FName("WaveDivergence"), WaveDivergence);
		BeamComp->SetFloatParameter(FName("MaxRange"), MaxHitscanRange);
		BeamComp->SetFloatParameter(FName("MinEnergy"), MinDamageMultiplier);

		// Wave-ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹
		if (bUseWaveVisualization)
		{
			BeamComp->SetFloatParameter(FName("Wavelength"), Wavelength);
			BeamComp->SetFloatParameter(FName("Amplitude"), Amplitude);
			BeamComp->SetFloatParameter(FName("FadeTime"), BeamFadeTime);
			BeamComp->SetFloatParameter(FName("WavePacketLength"), WavePacketLength);
			BeamComp->SetFloatParameter(FName("WavePacketDelay"), WavePacketDelay);
			BeamComp->SetFloatParameter(FName("WavePacketSpeed"), WavePacketSpeed);
			BeamComp->SetColorParameter(FName("EFieldColor"), EFieldColor);
			BeamComp->SetColorParameter(FName("BFieldColor"), BFieldColor);
		}

		// Everything is set, so now it may run. Activating any earlier is what turns these writes
		// into deferred ones (see the spawn call above).
		BeamComp->Activate(true);
	}

	return BeamComp;
}

void AShooterWeapon::SpawnWaveFronts(const FVector& Start, const FVector& End)
{
	if (CVarNoVFX.GetValueOnGameThread() != 0)
	{
		return;
	}

	if (!WaveFrontFX)
	{
		return;
	}

	FVector Direction = (End - Start).GetSafeNormal();
	float Distance = FVector::Distance(Start, End);

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°
	float StartRadius = CalculateWaveRadius(0.0f);  // ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â² ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ (InitialWaveRadius)
	float EndRadius = CalculateWaveRadius(Distance); // ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â² ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ
	float DivergenceAngle = WaveDivergence * MaxDivergenceAngle;

	// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ Niagara ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â² ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ
	UNiagaraComponent* ConeComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		WaveFrontFX,
		Start,
		Direction.Rotation(),
		FVector::OneVector,
		true,
		true,
		ENCPoolMethod::None
	);

	if (ConeComp)
	{
		// === ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã…â€œÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ===
		ConeComp->SetVectorParameter(FName("BeamStart"), Start);
		ConeComp->SetVectorParameter(FName("BeamEnd"), End);
		ConeComp->SetVectorParameter(FName("BeamDirection"), Direction);
		ConeComp->SetFloatParameter(FName("MaxDistance"), Distance);
		ConeComp->SetFloatParameter(FName("InitialRadius"), StartRadius);
		ConeComp->SetFloatParameter(FName("EndRadius"), EndRadius);
		ConeComp->SetFloatParameter(FName("DivergenceAngle"), DivergenceAngle);

		// === ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ===
		ConeComp->SetFloatParameter(FName("TravelSpeed"), WavePacketSpeed);
		ConeComp->SetFloatParameter(FName("Lifetime"), BeamFadeTime);
		ConeComp->SetFloatParameter(FName("ExpansionSpeed"), WaveFrontExpansionSpeed);

		// === ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â·ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â» ===
		ConeComp->SetColorParameter(FName("WaveColor"), EFieldColor);
		ConeComp->SetFloatParameter(FName("Wavelength"), Wavelength);
		ConeComp->SetFloatParameter(FName("Energy"), 1.0f);

		// === ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â (ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â²ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂºÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°) ===
		FVector RightVector = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal();
		if (RightVector.IsNearlyZero())
		{
			RightVector = FVector::CrossProduct(Direction, FVector::RightVector).GetSafeNormal();
		}
		FVector UpVector = FVector::CrossProduct(RightVector, Direction).GetSafeNormal();

		ConeComp->SetVectorParameter(FName("UpVector"), UpVector);
		ConeComp->SetVectorParameter(FName("RightVector"), RightVector);

		// === ÃƒÆ’Ã‚ÂÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ ===
		ConeComp->SetFloatParameter(FName("WaveDivergence"), WaveDivergence);
		ConeComp->SetFloatParameter(FName("MinDamageMultiplier"), MinDamageMultiplier);
	}
}

EPhysicalSurface AShooterWeapon::ResolveImpactSurface(const FHitResult& Hit) const
{
	// Resolve surface type from hit's physical material (null-safe).
	// Requires the trace to be done with bReturnPhysicalMaterial = true.
	EPhysicalSurface Surface = SurfaceType_Default;
	if (UPhysicalMaterial* PhysMat = Hit.PhysMaterial.Get())
	{
		Surface = PhysMat->SurfaceType;
	}

	// An NPC that opted in answers for its own surface, so an enemy sounds like a shield while the
	// shield holds and like a body once it is down -- without a physical material per body part.
	// Which of the two it is comes from IsTargetShieldDown, the same gate ApplyDamageToTarget uses,
	// so the effect the player sees can never disagree with whether the shot actually hurt.
	if (AActor* HitActor = Hit.GetActor())
	{
		if (const AShooterNPC* HitNPC = Cast<AShooterNPC>(HitActor))
		{
			if (HitNPC->UsesImpactSurfaceOverride())
			{
				Surface = HitNPC->GetImpactSurface(IsTargetShieldDown(HitActor));
			}
		}
	}

	return Surface;
}

void AShooterWeapon::SpawnImpactEffect(const FHitResult& Hit)
{
	const EPhysicalSurface Surface = ResolveImpactSurface(Hit);

	// Here first, so the shooter sees their own bullet land with no round trip, then everyone else.
	// Identical split to the muzzle flash and the tracer above.
	SpawnImpactEffectLocally(Hit.ImpactPoint, Hit.ImpactNormal, Surface);

	if (HasAuthority())
	{
		Multicast_PlayImpactEffect(Hit.ImpactPoint, Hit.ImpactNormal, static_cast<uint8>(Surface));
	}
	else if (AShooterCharacter* OwnerCharacter = Cast<AShooterCharacter>(PawnOwner))
	{
		// A client's shot only reaches the server as damage, and a shot that hit a wall has no
		// damage to report -- so the impact needs its own way upstream, like the muzzle flash does.
		OwnerCharacter->Server_ReportImpactEffect(this, Hit.ImpactPoint, Hit.ImpactNormal,
			static_cast<uint8>(Surface));
	}
}

void AShooterWeapon::Multicast_PlayImpactEffect_Implementation(FVector_NetQuantize100 Location,
	FVector_NetQuantizeNormal Normal, uint8 SurfaceByte)
{
	// The shooter already played it the instant the trace came back.
	const bool bIsShooter = PawnOwner && PawnOwner->IsLocallyControlled();
	if (bIsShooter)
	{
		return;
	}

	SpawnImpactEffectLocally(Location, Normal, static_cast<EPhysicalSurface>(SurfaceByte));
}

void AShooterWeapon::SpawnImpactEffectLocally(const FVector& Location, const FVector& Normal, EPhysicalSurface Surface)
{
	// Отладочное глушение эффектов. В бою на шестнадцать точек попадания и трассеры идут сотнями в
	// секунду, и смотреть за поведением ИИ сквозь эту метель невозможно.
	//
	// Гасятся точечно, а не глобально: у Niagara нет выключателя, есть только снижение качества
	// (fx.Niagara.QualityLevel), а оно эффекты не убирает. Зато достаточно закрыть два самых
	// шумных источника - попадания оружия и вспышки дронов, - чтобы картинка стала читаемой.
	if (CVarNoVFX.GetValueOnGameThread() != 0)
	{
		return;
	}

	// This weapon's own per-surface entry wins where it is filled in, so a gun that was set up by
	// hand keeps exactly what it had. The set answers for everything the weapon says nothing about.
	const FImpactFeedback* SetEntry = FeedbackSet ? &FeedbackSet->FindImpact(Surface) : nullptr;

	// Pick VFX: per-surface override, otherwise the set, otherwise default ImpactFX.
	UNiagaraSystem* ResolvedFX = ImpactFX;
	if (SetEntry && SetEntry->HasVFX())
	{
		ResolvedFX = SetEntry->VFX;
	}
	if (TObjectPtr<UNiagaraSystem>* FoundFX = ImpactFXBySurface.Find(Surface))
	{
		if (*FoundFX)
		{
			ResolvedFX = *FoundFX;
		}
	}

	if (ResolvedFX)
	{
		UNiagaraComponent* ImpactComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			ResolvedFX,
			Location,
			Normal.Rotation(),
			FVector::OneVector,
			true,
			false,
			ENCPoolMethod::None
		);

		if (ImpactComp)
		{
			ImpactComp->SetColorParameter(FName("ImpactColor"), BeamColor);

			if (bUseWaveVisualization)
			{
				ImpactComp->SetFloatParameter(FName("Wavelength"), Wavelength);
			}

			if (UWorld* World = GetWorld())
			{
				if (UVFXVariantSequenceSubsystem* VariantSubsystem =
					World->GetSubsystem<UVFXVariantSequenceSubsystem>())
				{
					VariantSubsystem->ConfigureVariantForComponent(ImpactComp);
				}
			}

			ImpactComp->Activate(true);
		}
	}

	// Pick sound: per-surface override, otherwise the set, otherwise DefaultImpactSound. The pitch,
	// volume and attenuation travel with whichever of the three answered, so a set's entry is not
	// left being mixed by numbers that belong to a different sound.
	USoundBase* ResolvedSound = DefaultImpactSound;
	float PitchMin = ImpactSoundPitchMin;
	float PitchMax = ImpactSoundPitchMax;
	float Volume = ImpactSoundVolume;
	USoundAttenuation* Attenuation = ImpactSoundAttenuation;

	if (SetEntry && SetEntry->HasSound())
	{
		ResolvedSound = SetEntry->Sound;
		PitchMin = SetEntry->PitchMin;
		PitchMax = SetEntry->PitchMax;
		Volume = SetEntry->Volume;
		Attenuation = SetEntry->Attenuation ? SetEntry->Attenuation.Get() : ImpactSoundAttenuation.Get();
	}

	if (TObjectPtr<USoundBase>* FoundSound = ImpactSoundBySurface.Find(Surface))
	{
		if (*FoundSound)
		{
			ResolvedSound = *FoundSound;
			PitchMin = ImpactSoundPitchMin;
			PitchMax = ImpactSoundPitchMax;
			Volume = ImpactSoundVolume;
			Attenuation = ImpactSoundAttenuation;
		}
	}

	if (ResolvedSound)
	{
		const float Pitch = FMath::FRandRange(FMath::Min(PitchMin, PitchMax), FMath::Max(PitchMin, PitchMax));
		UGameplayStatics::PlaySoundAtLocation(
			this,
			ResolvedSound,
			Location,
			Volume,
			Pitch,
			0.0f,
			Attenuation
		);
	}
}

void AShooterWeapon::SpawnReflectionEffect(const FVector& Location, const FVector& IncomingDirection, const FVector& ReflectedDirection)
{
	if (CVarNoVFX.GetValueOnGameThread() != 0)
	{
		return;
	}

	if (!ReflectionFX)
	{
		return;
	}

	UNiagaraComponent* ReflectionComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		GetWorld(),
		ReflectionFX,
		Location,
		FRotator::ZeroRotator,
		FVector::OneVector,
		true,
		true,
		ENCPoolMethod::None
	);

	if (ReflectionComp)
	{
		ReflectionComp->SetVectorParameter(FName("IncomingDirection"), IncomingDirection);
		ReflectionComp->SetVectorParameter(FName("ReflectedDirection"), ReflectedDirection);
		ReflectionComp->SetColorParameter(FName("FlashColor"), BeamColor);
	}
}

// ==================== SFX ====================

void AShooterWeapon::PlayFireSound()
{
	if (!FireSound)
	{
		return;
	}

	// Get muzzle location for 3D sound
	// Use ThirdPersonMesh for NPCs (visible to player), FirstPersonMesh for local player
	FVector MuzzleLocation;

	bool bIsLocalPlayer = false;
	if (PawnOwner)
	{
		APlayerController* PC = Cast<APlayerController>(PawnOwner->GetController());
		bIsLocalPlayer = PC && PC->IsLocalController();
	}

	if (bIsLocalPlayer && FirstPersonMesh)
	{
		MuzzleLocation = FirstPersonMesh->GetSocketLocation(MuzzleSocketName);
	}
	else if (ThirdPersonMesh)
	{
		MuzzleLocation = ThirdPersonMesh->GetSocketLocation(MuzzleSocketName);
	}
	else
	{
		// Fallback to owner location
		MuzzleLocation = GetOwner()->GetActorLocation();
	}

	// Calculate random pitch within specified range
	const float RandomPitch = FMath::RandRange(FireSoundPitchMin, FireSoundPitchMax);

	// Play sound at muzzle location with attenuation for proper 3D spatialization
	UGameplayStatics::SpawnSoundAtLocation(
		this,
		FireSound,
		MuzzleLocation,
		FRotator::ZeroRotator,
		FireSoundVolume,
		RandomPitch,
		0.0f,  // StartTime
		FireSoundAttenuation
	);
}

float AShooterWeapon::GetOptimalDamageRange() const
{
	// ÃƒÆ’Ã‚ÂÃƒâ€¦Ã‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¼ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã¢â‚¬â„¢ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â³ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Âµ WaveRadius == TargetEffectiveRadius
	// WaveRadius = InitialWaveRadius + Distance * tan(DivergenceAngle)
	// TargetRadius = InitialRadius + OptimalDistance * tan(Angle)
	// OptimalDistance = (TargetRadius - InitialRadius) / tan(Angle)

	float DivergenceAngle = WaveDivergence * MaxDivergenceAngle;
	float TangentAngle = FMath::Tan(FMath::DegreesToRadians(DivergenceAngle));

	if (TangentAngle <= KINDA_SMALL_NUMBER)
	{
		// ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¶ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚ÂµÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¿ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¹ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã¢â‚¬ËœÃƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â° ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â»ÃƒÆ’Ã¢â‚¬ËœÃƒâ€¦Ã‚Â½ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â±ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¾ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¹ ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â´ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã¢â‚¬ËœÃƒâ€šÃ‚ÂÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â°ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â½ÃƒÆ’Ã¢â‚¬ËœÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸ÃƒÆ’Ã‚ÂÃƒâ€šÃ‚Â¸
		return MaxHitscanRange;
	}

	float OptimalDistance = (TargetEffectiveRadius - InitialWaveRadius) / TangentAngle;
	return FMath::Max(0.0f, OptimalDistance);
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetFirstPersonAnimInstanceClass() const
{
	return FirstPersonAnimInstanceClass;
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetThirdPersonAnimInstanceClass() const
{
	return ThirdPersonAnimInstanceClass;
}

void AShooterWeapon::PlayADSInSound()
{
	if (!ADSInSound)
	{
		return;
	}

	// Get weapon location for 3D sound
	const FVector WeaponLocation = FirstPersonMesh->GetComponentLocation();

	// Calculate random pitch within specified range
	const float RandomPitch = FMath::RandRange(ADSSoundPitchMin, ADSSoundPitchMax);

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		ADSInSound,
		WeaponLocation,
		FRotator::ZeroRotator,
		ADSSoundVolume,
		RandomPitch
	);
}

void AShooterWeapon::PlayADSOutSound()
{
	if (!ADSOutSound)
	{
		return;
	}

	// Get weapon location for 3D sound
	const FVector WeaponLocation = FirstPersonMesh->GetComponentLocation();

	// Calculate random pitch within specified range
	const float RandomPitch = FMath::RandRange(ADSSoundPitchMin, ADSSoundPitchMax);

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		ADSOutSound,
		WeaponLocation,
		FRotator::ZeroRotator,
		ADSSoundVolume,
		RandomPitch
	);
}

// ==================== Heat System ====================

void AShooterWeapon::UpdateHeat(float DeltaTime)
{
	if (CurrentHeat <= 0.0f)
	{
		// Deactivate VFX when cold
		if (HeatVFXComponent && HeatVFXComponent->IsActive())
		{
			HeatVFXComponent->Deactivate();
		}
		return;
	}

	// Calculate decay rate based on owner speed
	float SpeedRatio = FMath::Clamp(GetOwnerSpeed() / MaxSpeedForHeatBonus, 0.0f, 1.0f);
	float SpeedBonus = 1.0f + (SpeedHeatDecayBonus * SpeedRatio);
	float DecayRate = BaseHeatDecayRate * SpeedBonus;

	// Apply decay
	CurrentHeat = FMath::Max(0.0f, CurrentHeat - DecayRate * DeltaTime);

	// Update Heat VFX
	UpdateHeatVFX();
}

void AShooterWeapon::UpdateHeatVFX()
{
	// Skip if no VFX system configured
	if (!HeatVFX)
	{
		return;
	}

	// Check if heat is above threshold
	if (CurrentHeat >= HeatVFXThreshold)
	{
		// Spawn VFX if not active
		if (!HeatVFXComponent)
		{
			USkeletalMeshComponent* AttachMesh = FirstPersonMesh;

			HeatVFXComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
				HeatVFX,
				AttachMesh,
				HeatVFXSocket,
				FVector::ZeroVector,
				FRotator::ZeroRotator,
				EAttachLocation::SnapToTarget,
				false // Don't auto-destroy, we manage lifecycle
			);
		}
		else if (!HeatVFXComponent->IsActive())
		{
			HeatVFXComponent->Activate();
		}

		// Update heat parameter
		if (HeatVFXComponent)
		{
			HeatVFXComponent->SetFloatParameter(HeatParameterName, CurrentHeat);
		}
	}
	else
	{
		// Below threshold - deactivate VFX
		if (HeatVFXComponent && HeatVFXComponent->IsActive())
		{
			HeatVFXComponent->Deactivate();
		}
	}
}

void AShooterWeapon::AddHeat(float Amount)
{
	CurrentHeat = FMath::Clamp(CurrentHeat + Amount, 0.0f, 1.0f);
}

float AShooterWeapon::GetOwnerSpeed() const
{
	if (CachedMovementComponent)
	{
		return CachedMovementComponent->Velocity.Size();
	}

	if (PawnOwner)
	{
		return PawnOwner->GetVelocity().Size();
	}

	return 0.0f;
}

float AShooterWeapon::CalculateHeatDamageMultiplier() const
{
	// Lerp from 1.0 (no heat) to MinHeatDamageMultiplier (max heat)
	return FMath::Lerp(1.0f, MinHeatDamageMultiplier, CurrentHeat);
}

float AShooterWeapon::CalculateHeatFireRateMultiplier() const
{
	if (!bUseHeatSystem)
	{
		return 1.0f;
	}
	// Lerp from 1.0 (no heat, normal fire rate) to MaxHeatFireRateMultiplier (max heat, slower fire rate)
	return FMath::Lerp(1.0f, MaxHeatFireRateMultiplier, CurrentHeat);
}

float AShooterWeapon::GetCycleActionSeconds() const
{
	// Out of line rather than in the header: reading a montage's length needs the complete type,
	// and the header only ever holds pointers to it.
	return CycleActionMontage ? CycleActionMontage->GetPlayLength() : 0.0f;
}

float AShooterWeapon::GetCurrentRefireRate() const
{
	// Base refire rate multiplied by heat penalty and any external multiplier (e.g. turret spin-up)
	const float Base = RefireRate * CalculateHeatFireRateMultiplier() * ExternalFireRateMultiplier;

	// A manual action cannot be outrun. The bolt has to close before the next round is under the
	// firing pin, so the animation is a FLOOR on the interval rather than something played over the
	// top of it: a rifle that fired through its own bolt would be lying about what it shows.
	//
	// A floor rather than a replacement, so a designer can still slow such a gun down further, and
	// so the heat and turret multipliers keep working on a weapon that has no cycle at all.
	//
	// This is the only gate needed for both fire modes: the automatic path schedules the next shot
	// off this number, and StartFiring compares against it before letting a semi-automatic weapon
	// fire at all.
	return FMath::Max(Base, GetCycleActionSeconds());
}

// ==================== Z-Factor ====================

float AShooterWeapon::CalculateZFactorMultiplier(float ShooterZ, float TargetZ) const
{
	// Calculate height difference (positive = shooter is above)
	float HeightDiff = ShooterZ - TargetZ;

	// No bonus if shooter is below or at same level
	if (HeightDiff <= ZFactorMinHeightDiff)
	{
		return 1.0f;
	}

	// Calculate normalized height difference
	float EffectiveHeightDiff = HeightDiff - ZFactorMinHeightDiff;
	float MaxEffectiveHeightDiff = ZFactorMaxHeightDiff - ZFactorMinHeightDiff;
	float HeightRatio = FMath::Clamp(EffectiveHeightDiff / MaxEffectiveHeightDiff, 0.0f, 1.0f);

	// Lerp from 1.0 to ZFactorMaxMultiplier based on height
	return FMath::Lerp(1.0f, ZFactorMaxMultiplier, HeightRatio);
}

// ==================== ADS Camera ====================

void AShooterWeapon::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	// This is called by PlayerCameraManager when this weapon is the ViewTarget (during ADS).
	// We provide the sight socket's WORLD POSITION but use ControlRotation for camera direction.
	// This way the camera sits at the weapon's sight, but does NOT inherit visual recoil kick
	// from the hands mesh — only the spring-smoothed camera recoil via AddPitchInput affects it.

	if (!ADSCameraComponent || !PawnOwner)
	{
		Super::CalcCamera(DeltaTime, OutResult);
		return;
	}

	// Use the ADS camera component's world position (attached to Sight socket on FP mesh).
	// This position includes the recoil visual kick (since FP mesh is moved by it).
	// We subtract the recoil offset to get the "clean" sight position.
	FVector SightWorldLocation = ADSCameraComponent->GetComponentLocation();

	// Subtract recoil visual kick from the sight position.
	// The weapon owner's RecoilComponent applies offsets to the FP Mesh,
	// which moves the ADS camera too. We want the camera without that kick.
	if (ACharacter* CharOwner = Cast<ACharacter>(PawnOwner))
	{
		if (UWeaponRecoilComponent* Recoil = CharOwner->FindComponentByClass<UWeaponRecoilComponent>())
		{
			// GetWeaponOffset returns offset in world-logical space (X=forward, Y=right, Z=up)
			FVector RecoilWorldOffset = Recoil->GetWeaponOffset();
			SightWorldLocation -= RecoilWorldOffset;
		}
	}

	// Use ControlRotation — this includes spring camera recoil (via AddPitchInput) but NOT
	// the visual weapon kick (which only affects FP Mesh relative transform).
	FRotator CameraRotation = PawnOwner->GetControlRotation();

	OutResult.Location = SightWorldLocation;
	OutResult.Rotation = CameraRotation;

	// FOV — the same ADSZoom the normal ADS path uses, applied to whatever the view is currently at.
	// NOTE this whole function is dormant: it only runs while the weapon is the ViewTarget, and
	// nothing sets that any more (ADS stopped moving the camera, see AShooterCharacter::UpdateADS).
	// It is kept correct rather than deleted so reviving SetViewTarget(Weapon) does not silently
	// resurrect a second, disagreeing zoom.
	OutResult.FOV = ApplyZoomToFOV(OutResult.FOV, GetADSZoom());
}

float AShooterWeapon::GetADSZoom() const
{
	float Zoom = ADSZoom;

	// The mounted optic multiplies the weapon's own magnification rather than replacing it: a 2x
	// scope on a marksman rifle that already aims at 1.5x is 3x, which is what putting a scope on
	// that rifle means. A red dot leaves the multiplier at 1 and changes only the picture, which
	// comes from its SOCKET_Aim and not from any number.
	//
	// Unless the optic says otherwise. A scope whose identity IS a number ("this is the 4x") has to
	// give the same 4x on every rifle, and a multiplier cannot: it would be 4x on one gun and 6x on
	// the next. Such an optic sets bOverrideADSZoom and owns the magnification outright.
	if (const UWeaponAttachmentDefinition* Optic = GetAttachmentOfType(EWeaponAttachmentType::Optic))
	{
		Zoom = Optic->bOverrideADSZoom
			? Optic->ADSZoomOverride
			: Zoom * Optic->ADSZoomMultiplier;
	}

	// Below 1 is not zoom, it is a wide angle, and nothing in the game means to ask for one.
	return FMath::Max(1.0f, Zoom);
}

float AShooterWeapon::ApplyZoomToFOV(float BaseFOVDegrees, float Zoom)
{
	const float SafeZoom = FMath::Max(Zoom, 0.01f);
	const float BaseTan = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(BaseFOVDegrees, 1.0f, 179.0f) * 0.5f));
	return FMath::RadiansToDegrees(2.0f * FMath::Atan(BaseTan / SafeZoom));
}

// ==================== Charge-Based Firing ====================

bool AShooterWeapon::TryConsumeCharge(float& OutChargeMultiplier)
{
	OutChargeMultiplier = 1.0f;

	if (!bUseChargeFiring)
	{
		return true; // Not using charge system
	}

	// Find owner's EMFVelocityModifier
	AActor* WeaponOwnerActor = GetOwner();
	if (!WeaponOwnerActor)
	{
		return false;
	}

	UEMFVelocityModifier* EMFMod = WeaponOwnerActor->FindComponentByClass<UEMFVelocityModifier>();
	if (!EMFMod)
	{
		UE_LOG(LogTemp, Warning, TEXT("ShooterWeapon: Owner has no UEMFVelocityModifier for charge-based firing"));
		return false;
	}

	// Get current total charge (base + bonus)
	float ChargeModule = FMath::Abs(EMFMod->GetCharge());

	// Check if we can afford full shot
	if (ChargeModule >= ChargePerShot + MinimumBaseCharge)
	{
		// Full power shot - deduct charge (bonus first, then base)
		OutChargeMultiplier = 1.0f;
		EMFMod->DeductCharge(ChargePerShot);

		UE_LOG(LogTemp, Log, TEXT("ShooterWeapon: Full power shot, charge module: %.2f -> %.2f"),
			ChargeModule, FMath::Abs(EMFMod->GetCharge()));
		return true;
	}
	else
	{
		// Not enough for full shot
		float AvailableCharge = FMath::Max(0.0f, ChargeModule - MinimumBaseCharge);

		if (AvailableCharge <= 0.0f || bBlockFiringBelowMinimum)
		{
			// Can't fire at all
			UE_LOG(LogTemp, Warning, TEXT("ShooterWeapon: Not enough charge to fire (have %.2f, need %.2f + %.2f minimum)"),
				ChargeModule, ChargePerShot, MinimumBaseCharge);
			return false;
		}

		// Fire weakened shot
		OutChargeMultiplier = AvailableCharge / ChargePerShot;

		// Deduct all available charge (bonus first, then base, down to minimum)
		EMFMod->DeductCharge(AvailableCharge);

		UE_LOG(LogTemp, Log, TEXT("ShooterWeapon: Weakened shot (%.1f%% power), charge module: %.2f -> %.2f"),
			OutChargeMultiplier * 100.0f, ChargeModule, FMath::Abs(EMFMod->GetCharge()));
		return true;
	}
}

// ==================== Presentation ====================

FLinearColor AShooterWeapon::GetAmmoColor() const
{
	// White is the identity for a tint, so a weapon with no tag draws its badge exactly as it was
	// authored rather than disappearing into black.
	return UPolarityPalette::GetColor(AmmoColorTag, FLinearColor::White);
}

FText AShooterWeapon::GetWeaponDisplayName() const
{
	if (!WeaponDisplayName.IsEmpty())
	{
		return WeaponDisplayName;
	}

	// A half-configured weapon reads as its class rather than as nothing. The "_C" Blueprint suffix
	// is dropped because it is noise to a player and the only reason it is here at all.
	FString Fallback = GetClass()->GetName();
	Fallback.RemoveFromEnd(TEXT("_C"));
	return FText::FromString(Fallback);
}

#if WITH_EDITOR

namespace
{
	/** Unlit, pure white, two sided. Everything the icon capture draws wears this, which is what
	 *  makes the result a silhouette instead of a small photograph of a gun. */
	const TCHAR* WeaponSilhouetteMaterialPath =
		TEXT("/Game/Variant_Shooter/UI/Widgets/HUD/Inventory/M_WeaponSilhouette.M_WeaponSilhouette");
}

void AShooterWeapon::GenerateIconFromMesh()
{
	USkeletalMesh* SourceMesh = FirstPersonMesh ? FirstPersonMesh->GetSkeletalMeshAsset() : nullptr;
	if (!SourceMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("[WEAPON_ICON] %s has no first person mesh, nothing to capture."),
			*GetClass()->GetName());
		return;
	}

	UMaterialInterface* Silhouette = LoadObject<UMaterialInterface>(nullptr, WeaponSilhouetteMaterialPath);
	if (!Silhouette)
	{
		// Deliberately a hard stop. Without the override the capture would still produce a picture,
		// just a lit one of the gun's own textures, and that is worse than no icon because it looks
		// like it worked.
		UE_LOG(LogTemp, Error, TEXT("[WEAPON_ICON] Missing %s. The icon must be rendered white, so this is not optional."),
			WeaponSilhouetteMaterialPath);
		return;
	}

	// The capture happens in the open editor world, not in an FPreviewScene.
	//
	// A preview scene was the obvious choice and it does not work: a USceneCaptureComponent2D there
	// renders nothing at all and hands back the cleared target. Measured, not guessed - the same
	// mesh, material and capture settings give 6.8% coverage in the editor world and 0.0% in a
	// preview scene.
	//
	// Nothing of the level leaks in, because the capture is put in ShowOnly mode with just this one
	// mesh in the list. That is also why the two actors below can sit anywhere.
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("[WEAPON_ICON] No editor world. Open a level and try again."));
		return;
	}

	// Transient and temporary, so building an icon does not mark the level dirty.
	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags = RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;
	SpawnParams.bHideFromSceneOutliner = true;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FVector Stage(0.0f, 0.0f, 200000.0f);

	ASkeletalMeshActor* MeshActor = World->SpawnActor<ASkeletalMeshActor>(Stage, FRotator::ZeroRotator, SpawnParams);
	ASceneCapture2D* CaptureActor = World->SpawnActor<ASceneCapture2D>(Stage, FRotator::ZeroRotator, SpawnParams);
	if (!MeshActor || !CaptureActor)
	{
		UE_LOG(LogTemp, Error, TEXT("[WEAPON_ICON] Could not spawn the capture rig."));
		if (MeshActor) { MeshActor->Destroy(); }
		if (CaptureActor) { CaptureActor->Destroy(); }
		return;
	}

	// Everything from here on must reach the cleanup at the bottom, so there are no early returns
	// between this point and it.
	USkeletalMeshComponent* MeshComp = MeshActor->GetSkeletalMeshComponent();
	MeshComp->SetSkeletalMeshAsset(SourceMesh);
	for (int32 Index = 0; Index < MeshComp->GetNumMaterials(); ++Index)
	{
		MeshComp->SetMaterial(Index, Silhouette);
	}

	const FBoxSphereBounds Bounds = SourceMesh->GetBounds();
	const float Radius = FMath::Max(Bounds.SphereRadius, 1.0f);

	// Built through the helper rather than by hand, because the helper ends with
	// UpdateResourceImmediate, which actually CLEARS the target. A hand-rolled NewObject +
	// InitAutoFormat leaves it uninitialised, and the capture does not overwrite every pixel, so
	// the background came back a uniform mid grey - measured at alpha 134 across the whole frame,
	// which then reads as a fully opaque icon.
	//
	// Plain RGBA8, not the _SRGB variant: the pixels are read back by hand below and everything
	// drawn is either pure white or pure black, so gamma cannot change the result.
	UTextureRenderTarget2D* RenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(
		World, IconResolution, IconResolution, RTF_RGBA8, FLinearColor::Black);
	if (!RenderTarget)
	{
		UE_LOG(LogTemp, Error, TEXT("[WEAPON_ICON] Could not create the render target."));
		MeshActor->Destroy();
		CaptureActor->Destroy();
		return;
	}

	// Orthographic on purpose: perspective would foreshorten the barrel and the same gun would come
	// out a different shape depending on how long it is.
	USceneCaptureComponent2D* Capture = CaptureActor->GetCaptureComponent2D();
	Capture->TextureTarget = RenderTarget;
	Capture->CaptureSource = SCS_FinalColorLDR;
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	// Clamped only against zero. Values below 1 are the point: they zoom in, which is how a long
	// thin gun is made to carry its square icon as well as a stubby one does.
	Capture->OrthoWidth = Radius * 2.0f * FMath::Max(IconCapturePadding, 0.05f);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	// Only the gun. This is what keeps the open level out of the picture, and it is why the rig can
	// be parked anywhere.
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->ShowOnlyComponent(MeshComp);
	// No show flag overrides. Turning eye adaptation off looked like the careful thing to do and it
	// dimmed the gun to a fifth of its brightness: the render target holds 245 on the silhouette
	// with the flags left alone, and 46 with them set. Nothing needs disabling anyway, because the
	// ShowOnly list already means the only thing in frame is one unlit white mesh.

	const FRotator ViewRotation(0.0f, IconCaptureYaw, 0.0f);
	const FVector ViewDirection = ViewRotation.Vector();
	CaptureActor->SetActorLocationAndRotation(
		Stage + Bounds.Origin - ViewDirection * Radius * 4.0f, ViewRotation);

	// Push the components' render state across before asking for a frame. Skipping this reads back
	// the cleared target, which is black, which silently becomes a fully transparent icon.
	// Waiting for the frame needs no explicit flush: ReadPixels below blocks on the render thread,
	// and the capture is already ahead of it in the same queue.
	World->SendAllEndOfFrameUpdates();
	Capture->CaptureScene();

	// Read the frame back and turn it into a real silhouette: white everywhere, coverage in the
	// alpha.
	//
	// Two reasons this is done by hand rather than through RenderTargetCreateStaticTexture2DEditorOnly.
	// First, that path infers the texture's gamma from the render target and gets it wrong, which
	// is not a warning but an assert inside the texture builder:
	// "MipView.GammaSpace == LayerData.SourceGammaSpace", and the editor dies. Second, even when it
	// survives, what it produces is a white gun on an OPAQUE BLACK SQUARE - fine as a thumbnail,
	// useless on a HUD plate, because the icon has to composite over whatever is behind it.
	FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
	TArray<FColor> Pixels;
	const bool bRead = Resource && Resource->ReadPixels(Pixels) && Pixels.Num() == IconResolution * IconResolution;

	// The rig has done its job. Torn down here rather than at the end so none of the failure exits
	// below can leave two invisible actors parked in the level.
	MeshActor->Destroy();
	CaptureActor->Destroy();

	if (!bRead)
	{
		UE_LOG(LogTemp, Error, TEXT("[WEAPON_ICON] Could not read the capture back."));
		return;
	}

	// Everything drawn wore the white unlit material, so brightness IS coverage. It is NOT reliably
	// full brightness though: the capture goes through the tonemapper and whatever exposure the
	// scene happens to be sitting at, and the same gun came back with a peak alpha of 46, 142 and
	// 245 on three runs that differed in nothing but when they were taken.
	//
	// So the frame is normalised instead of trusted. The silhouette is a flat shape, so scaling it
	// until its brightest pixel is opaque costs nothing, keeps the soft edges in proportion, and
	// makes the result independent of exposure, of the material's brightness, and of how much of
	// the frame the gun happens to fill.
	int32 CoveredPixels = 0;
	uint8 PeakCoverage = 0;
	for (FColor& Pixel : Pixels)
	{
		const uint8 Coverage = FMath::Max3(Pixel.R, Pixel.G, Pixel.B);
		CoveredPixels += (Coverage > 8) ? 1 : 0;
		PeakCoverage = FMath::Max(PeakCoverage, Coverage);
		Pixel = FColor(255, 255, 255, Coverage);
	}

	if (PeakCoverage > 0 && PeakCoverage < 255)
	{
		const float Gain = 255.0f / static_cast<float>(PeakCoverage);
		for (FColor& Pixel : Pixels)
		{
			Pixel.A = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Pixel.A * Gain), 0, 255));
		}
	}

	// A capture that renders nothing produces a perfectly valid, perfectly invisible icon, and that
	// is the worst possible outcome: the button reports success and the HUD shows a blank. Refuse
	// instead. Anything under a twentieth of a percent is not a gun, it is a failed capture.
	const float CoveredFraction = static_cast<float>(CoveredPixels) / static_cast<float>(Pixels.Num());
	if (CoveredFraction < 0.0005f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WEAPON_ICON] %s captured an empty frame (%.4f%% covered). Nothing written. ")
			TEXT("Check that the mesh renders and that IconCaptureYaw points at it."),
			*GetClass()->GetName(), CoveredFraction * 100.0f);
		return;
	}

	// Saved next to the Blueprint that owns it, under a name derived from the class, so pressing
	// the button again after a mesh swap overwrites the same asset instead of littering.
	FString ClassName = GetClass()->GetName();
	ClassName.RemoveFromEnd(TEXT("_C"));
	const FString PackagePath = FPackageName::GetLongPackagePath(GetClass()->GetOutermost()->GetName());
	const FString AssetName = TEXT("T_WeaponIcon_") + ClassName;
	const FString PackageName = PackagePath / AssetName;

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("[WEAPON_ICON] Could not create package %s."), *PackageName);
		return;
	}
	Package->FullyLoad();

	UTexture2D* NewIcon = FindObject<UTexture2D>(Package, *AssetName);
	const bool bCreated = (NewIcon == nullptr);
	if (bCreated)
	{
		NewIcon = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
	}
	if (!NewIcon)
	{
		UE_LOG(LogTemp, Error, TEXT("[WEAPON_ICON] Could not create %s."), *PackageName);
		return;
	}

	NewIcon->PreEditChange(nullptr);
	// Declared rather than inferred. This pair is exactly what the crashed path was guessing at.
	// Square, the full captured frame. The gun sits in a mostly transparent square and that is
	// deliberate: the HUD and the inventory both give the icon a square slot, so a texture cropped
	// to the gun would change shape from weapon to weapon and never line up. Cropping happens
	// outside the game, only where a tight silhouette is actually wanted.
	NewIcon->Source.Init(IconResolution, IconResolution, 1, 1, TSF_BGRA8,
		reinterpret_cast<const uint8*>(Pixels.GetData()));
	NewIcon->SRGB = true;
	// Uncompressed: the silhouette is one hard edge between white and nothing, and DXT fringes
	// exactly that.
	NewIcon->CompressionSettings = TC_EditorIcon;
	NewIcon->MipGenSettings = TMGS_NoMipmaps;
	NewIcon->NeverStream = true;
	NewIcon->PostEditChange();
	NewIcon->UpdateResource();

	if (bCreated)
	{
		FAssetRegistryModule::AssetCreated(NewIcon);
	}
	Package->MarkPackageDirty();

	Modify();
	Icon = NewIcon;
	MarkPackageDirty();

	UE_LOG(LogTemp, Log,
		TEXT("[WEAPON_ICON] %s -> %s (%s), %dx%d, peak alpha %d normalised to 255. ")
		TEXT("Save both assets to keep it."),
		*GetClass()->GetName(), *PackageName, bCreated ? TEXT("new") : TEXT("overwritten"),
		IconResolution, IconResolution, PeakCoverage);
}

#endif // WITH_EDITOR
