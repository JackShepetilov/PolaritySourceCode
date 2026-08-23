// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterCharacter.h"
#include "Variant_Shooter/AnimNotify_WeaponSwitch.h"
#include "Animation/AnimMontage.h"
#include "Net/UnrealNetwork.h"
#include "Engine/DamageEvents.h"
#include "ShooterWeapon.h"
#include "Upgrades/Upgrades/Upgrade_ChargedPunch.h"
#include "Weapons/ShooterWeapon_Melee.h"
#include "Weapons/DroppedRangedWeapon.h"
#include "Weapons/RiotShield.h"
#include "UI/EMFChargeWidgetSubsystem.h"
// Full types, not forward declarations: TSubclassOf of each is an RPC parameter, and the generated
// thunk needs StaticClass().
#include "UI/EMFChargeWidget.h"
#include "UI/CaptureReticleWidget.h"
#include "Abilities/AbilityDefinition_ShieldBypass.h"
#include "Abilities/AbilityHandler_ShieldBypass.h"
#include "AI/ShooterNPC.h"
#include "ShooterDummyInterface.h"
#include "MovementSettings.h"
#include "CameraShakeComponent.h"
#include "WeaponRecoilComponent.h"
#include "HitMarkerComponent.h"
#include "MeleeAttackComponent.h"
#include "AI/Coordination/ThreatComponent.h"
#include "ChargeAnimationComponent.h"
#include "ApexMovementComponent.h"
#include "EMFVelocityModifier.h"
#include "EMF_FieldComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputCoreTypes.h"
#include "Engine/LocalPlayer.h"
#include "Components/InputComponent.h"
#include "Components/PawnNoiseEmitterComponent.h"
#include "Components/AudioComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "Animation/AnimInstance.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "ShooterGameMode.h"
#include "UI/ShooterUI.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Curves/CurveFloat.h"
#include "Polarity/Checkpoint/CheckpointData.h"
#include "Polarity/Checkpoint/CheckpointSubsystem.h"
#include "Polarity/Upgrades/UpgradeManagerComponent.h"
#include "StyleComponent.h"
#include "StyleAction.h"
#include "Polarity/Upgrades/UpgradeRegistry.h"
#include "Variant_Shooter/Run/RunSubsystem.h"
#include "ShooterSettingsSubsystem.h"
#include "Variant_Shooter/Abilities/AbilityComponent.h"
#include "Variant_Shooter/Abilities/AbilityHandler.h"
#include "Variant_Shooter/Abilities/AbilityDefinition_Grapple.h"
#include "CableComponent.h"
#include "DrawDebugHelpers.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "EMFPhysicsProp.h"
#include "PhysicsEngine/PhysicsHandleComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/DamageEvents.h"
#include "Variant_Shooter/DamageTypes/DamageType_Melee.h"
#include "Variant_Shooter/DamageTypes/DamageType_Ranged.h"
#include "Variant_Shooter/DamageTypes/DamageType_EMFWeapon.h"
#include "TutorialSubsystem.h"
#include "Variant_Shooter/UI/ShooterBulletCounterUI.h"
#include "Variant_Shooter/DamageTypes/DamageType_EMFProximity.h"
#include "PlayerDeathSequenceComponent.h"

static TAutoConsoleVariable<int32> CVarMeleeLungeDebug(
	TEXT("polarity.melee.lungedebug"),
	0,
	TEXT("Trace why a melee swing did or did not lunge.\n")
	TEXT("\n")
	TEXT("There are TWO lunges and they are easy to confuse when one of them silently does nothing:\n")
	TEXT("the bare-handed one in UMeleeAttackComponent, and AShooterWeapon_Melee's own copy, which\n")
	TEXT("takes over the moment a blade is equipped. Every line below says which one is talking.\n")
	TEXT("\n")
	TEXT("Prints, in order: the reach the class passive granted, every candidate the acquisition\n")
	TEXT("swept up with its distance against its own allowed reach, whether the path was blocked,\n")
	TEXT("and -- for the weapon -- whether the entry SPEED gate let the lunge actually fire.\n")
	TEXT("  0 = off\n")
	TEXT("  1 = log every swing"),
	ECVF_Default);

bool AShooterCharacter::IsLungeDebugEnabled()
{
	return CVarMeleeLungeDebug.GetValueOnAnyThread() > 0;
}

static TAutoConsoleVariable<int32> CVarGrappleCableDebug(
	TEXT("polarity.grapple.cabledebug"),
	0,
	TEXT("Watch the grapple line's length, frame by frame.\n")
	TEXT("\n")
	TEXT("Answers one question and no others: does the rope stop growing when the hook lands.\n")
	TEXT("Prints the phase (FLY or HELD), the real gap between the two ends, the CableLength the\n")
	TEXT("component was given, and whether the movement component says the line is attached.\n")
	TEXT("A green line is drawn along the same two points, so a rope that is visibly longer than\n")
	TEXT("the green line is sagging rather than growing -- that is CableSlack, not the length.\n")
	TEXT("  0 = off\n")
	TEXT("  1 = log and draw every frame while a line is out"),
	ECVF_Default);

// File-scope helper (uniquely named — unity-build safe): true while the yank-throw montage is
// actively playing on the FP arms' anim instance.
static bool IsYankThrowMontageActiveOnFPMesh(UChargeAnimationComponent* ChargeComp, USkeletalMeshComponent* FPMesh)
{
	if (!ChargeComp || !ChargeComp->YankThrowMontage || !FPMesh)
	{
		return false;
	}
	UAnimInstance* AnimInst = FPMesh->GetAnimInstance();
	return AnimInst && AnimInst->Montage_IsPlaying(ChargeComp->YankThrowMontage);
}

// File-scope helper (uniquely named — unity-build safe): mark everything hanging under a
// first-person mesh as first-person too.
//
// bOnlyOwnerSee and FirstPersonPrimitiveType are per-component and are NOT inherited by children,
// so a sight, laser or any other mesh a Blueprint attaches under a first-person mesh renders for
// *everyone*: the other players see it floating on the third-person body. This applies the rule to
// every descendant, so new attachments stop needing their own line of code.
// File-scope helper (uniquely named — unity-build safe): hand the left-hand IK target to an
// Animation Blueprint. The AnimBP side is a pair of loose variables rather than an interface, so
// this is reflection by name: an AnimBP without them simply ignores the values, which is what
// keeps unarmed and NPC anim classes from caring.

static void ApplyFirstPersonVisibilityToFPSubtree(USceneComponent* Root)
{
	if (!Root)
	{
		return;
	}

	TArray<USceneComponent*> Children;
	Root->GetChildrenComponents(/*bIncludeAllDescendants*/ true, Children);
	for (USceneComponent* Child : Children)
	{
		if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Child))
		{
			Prim->SetOnlyOwnerSee(true);
			Prim->SetOwnerNoSee(false);
			Prim->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
		}
	}
}

// File-scope helper (uniquely named — unity-build safe): the mirror of the function above. Mark
// everything hanging under a third-person mesh as third-person too.
//
// Same reason, other side: bOwnerNoSee and FirstPersonPrimitiveType are per-component and are NOT
// inherited by children, so a sight, laser or any other mesh a Blueprint attaches under a
// third-person weapon mesh renders for its owner as well, and the local player sees it hanging in
// the air beside their own hands. This applies the rule to every descendant, so new attachments
// stop needing their own line of code.
//
// Branches that the first-person side has already claimed are skipped whole, children included:
// on any pawn whose Blueprint still parents first-person geometry under the body mesh (and on the
// NPC path, which deliberately hangs the weapon's FP mesh on the third-person hand), walking
// straight through would mark the arms world-space and delete them from view.
static void ApplyThirdPersonVisibilityToTPSubtree(USceneComponent* Root)
{
	if (!Root)
	{
		return;
	}

	for (USceneComponent* Child : Root->GetAttachChildren())
	{
		if (!Child)
		{
			continue;
		}

		if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Child))
		{
			if (Prim->bOnlyOwnerSee || Prim->FirstPersonPrimitiveType == EFirstPersonPrimitiveType::FirstPerson)
			{
				continue;
			}

			Prim->SetOnlyOwnerSee(false);
			Prim->SetOwnerNoSee(true);
			Prim->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
		}

		ApplyThirdPersonVisibilityToTPSubtree(Child);
	}
}

AShooterCharacter::AShooterCharacter()
{
	// create the noise emitter component
	PawnNoiseEmitter = CreateDefaultSubobject<UPawnNoiseEmitterComponent>(TEXT("Pawn Noise Emitter"));

	// create the recoil component
	RecoilComponent = CreateDefaultSubobject<UWeaponRecoilComponent>(TEXT("Recoil Component"));

	// create the hit marker component
	HitMarkerComponent = CreateDefaultSubobject<UHitMarkerComponent>(TEXT("Hit Marker Component"));

	// create the melee attack component
	MeleeAttackComponent = CreateDefaultSubobject<UMeleeAttackComponent>(TEXT("Melee Attack Component"));

	// create the charge animation component
	ChargeAnimationComponent = CreateDefaultSubobject<UChargeAnimationComponent>(TEXT("Charge Animation Component"));

	// create the threat component — the AI reads this to decide who to come after
	ThreatComponent = CreateDefaultSubobject<UThreatComponent>(TEXT("Threat Component"));

	// holds a captured prop as a physics constraint rather than a spring force — see the header
	PropPhysicsHandle = CreateDefaultSubobject<UPhysicsHandleComponent>(TEXT("Prop Physics Handle"));

	// create the upgrade manager component
	UpgradeManager = CreateDefaultSubobject<UUpgradeManagerComponent>(TEXT("Upgrade Manager"));

	// create the ability component (multi-slot ability inventory)
	AbilityComponent = CreateDefaultSubobject<UAbilityComponent>(TEXT("Ability Component"));

	// configurable terminal run-death presentation
	PlayerDeathSequenceComponent = CreateDefaultSubobject<UPlayerDeathSequenceComponent>(TEXT("Player Death Sequence"));

	// configure movement
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 600.0f, 0.0f);

	// The body faces where the camera faces, always. Both of these are already the engine defaults
	// for ACharacter; they are written out because the third person aiming depends on them.
	//
	// This is what makes the third person weapon point at what its owner is aiming at: the character
	// turns, not the gun. Yaw comes from the body, pitch from the aim offset, and nothing has to
	// steer the weapon per frame. It is also why no turn-in-place is needed here: the body cannot
	// wind up behind the camera, because it never lags it. The price is that the character moves in
	// every direction relative to its own facing, which is why BS_Rifle_Locomotion is a full
	// eight-way strafe set rather than a forward-only one.
	bUseControllerRotationYaw = true;
	GetCharacterMovement()->bOrientRotationToMovement = false;
}

USkeletalMeshComponent* AShooterCharacter::GetMeleeMesh() const
{
	if (MeleeAttackComponent)
	{
		return MeleeAttackComponent->MeleeMesh;
	}
	return nullptr;
}

void AShooterCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Applied from BOTH ends: here on the authority and on any machine whose archetype already
	// carries the class (a client spawning BP_WizardCharacter has the pointer before this runs), and
	// again from OnRep_ClassDefinition if it arrives later. Depending on only one of the two is the
	// mistake that made HP, death and the HUD each fail silently in turn.
	ApplyClassDefinition();

	// ==================== Restore run-scoped upgrades (cross-level carry) ====================
	// The character is rebuilt on every OpenLevel; the run's upgrade ledger lives on the
	// GameInstance (URunSubsystem) and is re-applied here, then kept in sync as the player
	// gains more upgrades. HP and weapons are intentionally NOT carried (full heal each biome,
	// fixed loadout) — only upgrades persist.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (URunSubsystem* Run = GI->GetSubsystem<URunSubsystem>())
		{
			Run->BindUpgradeManager(UpgradeManager, UpgradeRegistry);
		}
	}

	// Initialize HP based on StartingHPPercent (1.0 = full HP)
	CurrentHP = MaxHP * FMath::Clamp(StartingHPPercent, 0.0f, 1.0f);

	// Remember where the mesh sits before anything can ragdoll it: standing back up has to restore
	// this exactly, and by then the physics simulation has thrown the original attachment away.
	if (const USkeletalMeshComponent* BodyMesh = GetMesh())
	{
		MeshRelativeTransformOnSpawn = BodyMesh->GetRelativeTransform();
	}

	// Hand this character's owner the HUD class to build. Only the authority can read it — the
	// GameMode does not exist anywhere else — so it is replicated from here and the owning client
	// builds the widget in OnRep_HUDClass. The host is its own owner, so it builds straight away.
	if (HasAuthority())
	{
		if (const AShooterGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AShooterGameMode>() : nullptr)
		{
			HUDClass = GameMode->GetShooterUIClass();
			CreateLocalHUD();
		}
	}

	// Store base FOV and location values for ADS interpolation
	if (UCameraComponent* Camera = GetFirstPersonCameraComponent())
	{
		BaseCameraFOV = Camera->FieldOfView;
		BaseFirstPersonFOV = Camera->FirstPersonFieldOfView;
		BaseCameraLocation = Camera->GetRelativeLocation();

		UE_LOG(LogTemp, Warning, TEXT("ShooterCharacter: BaseCameraLocation=%s, BaseFOV=%.1f"),
			*BaseCameraLocation.ToString(), BaseCameraFOV);
	}

	// Initialize recoil component
	if (RecoilComponent)
	{
		APlayerController* PC = Cast<APlayerController>(GetController());
		RecoilComponent->Initialize(PC, GetCharacterMovement(), GetApexMovement());
	}

	// Bind melee hit event to forward to hit marker system
	if (MeleeAttackComponent)
	{
		MeleeAttackComponent->OnMeleeHit.AddDynamic(this, &AShooterCharacter::OnMeleeHit);
	}

	// Configure EMF components if they exist (created in Blueprint)
	if (UEMFVelocityModifier* EMFMod = FindComponentByClass<UEMFVelocityModifier>())
	{
		EMFMod->SetOwnerType(EEMSourceOwnerType::Player);
		// Player doesn't react to NPC EM forces
		EMFMod->NPCForceMultiplier = 0.0f;
	}
	if (UEMF_FieldComponent* FieldComp = FindComponentByClass<UEMF_FieldComponent>())
	{
		FieldComp->SetOwnerType(EEMSourceOwnerType::Player);
	}

	// Bind movement SFX delegates
	BindMovementSFXDelegates();

	// Anything the Blueprint hung under the first-person mesh renders for everyone until it is
	// told otherwise, so claim the whole subtree before the first frame is drawn. Weapons get
	// the same treatment again on every attach, since their meshes arrive later.
	ApplyFirstPersonVisibilityToFPSubtree(GetFirstPersonMesh());

	// Same for the body: anything the Blueprint hung under the third-person mesh is visible to its
	// own owner until it is told otherwise, which puts it in the middle of that player's view.
	ApplyThirdPersonVisibilityToTPSubtree(GetMesh());

	// Initialize first person mesh visibility (hidden if no weapon)
	UpdateFirstPersonMeshVisibility();

	// update health/armor listeners
	BroadcastHealthChanged();

	// Tutorial debug mode: reveal all HUD elements and skip all tutorials
	if (bTutorialDebugMode)
	{
		// Delay slightly so HUD widget has time to initialize
		FTimerHandle DebugTimerHandle;
		GetWorldTimerManager().SetTimer(DebugTimerHandle, [this]()
		{
			if (UGameInstance* GI = GetGameInstance())
			{
				if (UTutorialSubsystem* TutorialSub = GI->GetSubsystem<UTutorialSubsystem>())
				{
					TArray<FName> AllTutorialIDs;
					AllTutorialIDs.Add(FirstDamageTutorialID);
					AllTutorialIDs.Add(HealthPickupObjectiveTutorialID);
					AllTutorialIDs.Add(FirstChargeTutorialID);
					AllTutorialIDs.Add(FirstDepletionTutorialID);
					AllTutorialIDs.Add(MeleeChargesTutorialID);
					TutorialSub->RunTutorialDebugReveal(AllTutorialIDs);
				}
			}
		}, 0.5f, false);
	}

}

void AShooterCharacter::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	// Unbind movement SFX delegates
	UnbindMovementSFXDelegates();

	// Stop any looping sounds
	StopSlideLoopSound();
	StopWallRunLoopSound();

	Super::EndPlay(EndPlayReason);

	// clear the respawn timer
	GetWorld()->GetTimerManager().ClearTimer(RespawnTimer);
}

void AShooterCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// base class handles move, aim and jump inputs
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Firing
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Started, this, &AShooterCharacter::DoStartFiring);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Completed, this, &AShooterCharacter::DoStopFiring);

		// Reload the equipped weapon
		if (ReloadAction)
		{
			EnhancedInputComponent->BindAction(ReloadAction, ETriggerEvent::Started, this, &AShooterCharacter::DoReload);
		}

		// Switch weapon — plain forward cycle on press. Hold-to-throw moved to the yanked
		// weapon's own per-weapon switch key (see the WeaponSwitchActions loop below).
		EnhancedInputComponent->BindAction(SwitchWeaponAction, ETriggerEvent::Started, this, &AShooterCharacter::DoSwitchWeapon);

		// Reverse cycle (e.g. mouse wheel down) — simple single-shot per press, no tap/hold throw logic.
		if (SwitchWeaponBackAction)
		{
			EnhancedInputComponent->BindAction(SwitchWeaponBackAction, ETriggerEvent::Started, this, &AShooterCharacter::DoSwitchWeaponBackward);
		}

		// ADS (hold to aim)
		if (ADSAction)
		{
			EnhancedInputComponent->BindAction(ADSAction, ETriggerEvent::Started, this, &AShooterCharacter::DoStartADS);
			EnhancedInputComponent->BindAction(ADSAction, ETriggerEvent::Completed, this, &AShooterCharacter::DoStopADS);
		}

		// Melee attack
		if (MeleeAction)
		{
			// Tap = swing on Triggered (matches existing behaviour).
			EnhancedInputComponent->BindAction(MeleeAction, ETriggerEvent::Triggered, this, &AShooterCharacter::DoMeleeAttack);
			// Hold = broadcast Started/Completed so upgrades like ChargedPunch can do their own hold timing.
			EnhancedInputComponent->BindAction(MeleeAction, ETriggerEvent::Started, this, &AShooterCharacter::DoMeleePressed);
			EnhancedInputComponent->BindAction(MeleeAction, ETriggerEvent::Completed, this, &AShooterCharacter::DoMeleeReleased);
		}

		// Shield toggle (tap = raise/lower). Throw is bound to the channel/grab key (DoChannelPressed override).
		if (ShieldToggleAction)
		{
			EnhancedInputComponent->BindAction(ShieldToggleAction, ETriggerEvent::Started, this, &AShooterCharacter::OnShieldTogglePressed);
		}

		// Ability activation. Started = press (TryActivate), Completed = release (OnButtonReleased) —
		// the press/release pair feeds the ability's own Tap-vs-Hold ActivationMode (do NOT use Triggered).
		if (AbilityAction)
		{
			EnhancedInputComponent->BindAction(AbilityAction, ETriggerEvent::Started, this, &AShooterCharacter::DoAbilityPressed);
			EnhancedInputComponent->BindAction(AbilityAction, ETriggerEvent::Completed, this, &AShooterCharacter::DoAbilityReleased);
		}

		// Weapon-switch keys. Each weapon declares its own SwitchAction; we bind the listed actions and
		// the handler resolves which OWNED weapon the pressed action selects (so several weapon classes
		// can share one key — only one is owned at a time).
		// The YANKED weapon's key is special: tap (<YankSwapHoldThreshold) = equip it as usual,
		// hold (≥YankSwapHoldThreshold) = ThrowYankedWeaponIfAny (fired by SwapHoldTimer).
		// Any other weapon's key equips instantly on press.
		for (UInputAction* SwitchActionEntry : WeaponSwitchActions)
		{
			if (SwitchActionEntry)
			{
				EnhancedInputComponent->BindActionValueLambda(SwitchActionEntry, ETriggerEvent::Started,
					[this, SwitchActionEntry](const FInputActionValue&)
					{
						// Resolve which owned weapon this key selects.
						AShooterWeapon* Target = nullptr;
						for (AShooterWeapon* W : OwnedWeapons)
						{
							if (W && W->GetSwitchAction() == SwitchActionEntry)
							{
								Target = W;
								break;
							}
						}

						// Yanked weapon's key: arm the hold-to-throw timer; the equip (tap)
						// happens on release if the threshold hasn't fired yet.
						if (Target && Target->bWasYanked)
						{
							SwapKeyPressTime = GetWorld()->GetTimeSeconds();
							bSwapKeyHeldPending = true;
							GetWorld()->GetTimerManager().SetTimer(
								SwapHoldTimer, this, &AShooterCharacter::OnSwapHoldThresholdFired,
								YankSwapHoldThreshold, false);
							return;
						}

						// Normal weapon: equip immediately on press.
						DoWeaponSwitchByAction(SwitchActionEntry);
					});

				EnhancedInputComponent->BindActionValueLambda(SwitchActionEntry, ETriggerEvent::Completed,
					[this, SwitchActionEntry](const FInputActionValue&)
					{
						// Only meaningful for the yanked weapon's armed press above.
						if (!bSwapKeyHeldPending)
						{
							return;
						}

						FTimerManager& TimerMgr = GetWorld()->GetTimerManager();
						if (TimerMgr.IsTimerActive(SwapHoldTimer))
						{
							// Tap: released before the hold threshold — plain equip.
							TimerMgr.ClearTimer(SwapHoldTimer);
							DoWeaponSwitchByAction(SwitchActionEntry);
						}
						// else: hold already fired (throw executed) — nothing to do.
						bSwapKeyHeldPending = false;
					});
			}
		}
	}
}

void AShooterCharacter::DoAim(float Yaw, float Pitch)
{
	// Call parent implementation
	Super::DoAim(Yaw, Pitch);

	// Track mouse delta for recoil sway
	LastMouseDelta = FVector2D(Yaw, Pitch);

	// Feed mouse input to recoil component for sway
	if (RecoilComponent)
	{
		RecoilComponent->AddMouseInput(Yaw, Pitch);
	}
}

void AShooterCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Everyone, not owner-only: which class a teammate is decides how they look and what they can do
	// with an item, and both of those are things the other players need to see.
	DOREPLIFETIME(AShooterCharacter, ClassDefinition);

	DOREPLIFETIME(AShooterCharacter, CurrentHP);
	DOREPLIFETIME(AShooterCharacter, HeldByCharacter);
	DOREPLIFETIME(AShooterCharacter, CurrentArmor);
	DOREPLIFETIME(AShooterCharacter, CurrentWeapon);
	DOREPLIFETIME(AShooterCharacter, OwnedWeapons);

	// Only the owner builds a HUD, so only the owner needs to know which one.
	DOREPLIFETIME_CONDITION(AShooterCharacter, HUDClass, COND_OwnerOnly);

	// Everyone needs both: a downed player is a ragdoll on every screen, and a rescuer has to be
	// able to see that the body in front of them is one that can be picked up.
	DOREPLIFETIME(AShooterCharacter, bIsDowned);
	DOREPLIFETIME(AShooterCharacter, bTerminalDeath);
}

void AShooterCharacter::OnRep_HUDClass()
{
	CreateLocalHUD();
}

// ==================== Downed and revive ====================

void AShooterCharacter::EnterDownedState()
{
	if (!HasAuthority() || bIsDowned)
	{
		return;
	}

	bIsDowned = true;

	// Nothing this player was in the middle of survives going down.
	if (AbilityComponent && AbilityComponent->IsCasting())
	{
		AbilityComponent->CancelCast();
	}
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->DeactivateWeapon();
	}
	StopSlideLoopSound();
	StopWallRunLoopSound();

	UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] %s is down, waiting for a pick-up"), *GetName());

	ApplyDownedPresentation(true);
}

void AShooterCharacter::ReviveFromDowned()
{
	if (!HasAuthority() || !bIsDowned)
	{
		return;
	}

	bIsDowned = false;
	CurrentHP = FMath::Max(1.0f, MaxHP * FMath::Clamp(RevivePercent, 0.05f, 1.0f));

	UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] %s is back up with %.0f HP"), *GetName(), CurrentHP);

	ApplyDownedPresentation(false);
	BroadcastHealthChanged();

	// Whatever was in hand went quiet on the way down; give it back.
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->ActivateWeapon();
	}
}

void AShooterCharacter::OnRep_IsDowned()
{
	ApplyDownedPresentation(bIsDowned);
}

void AShooterCharacter::OnRep_TerminalDeath()
{
	if (bTerminalDeath && !bHasPlayedLocalDeath)
	{
		// Safe on a client: the only authority-side line in Die() is the team score, and
		// GetAuthGameMode() already returns null here. Everything else is local presentation.
		Die();
	}
}

void AShooterCharacter::ApplyDownedPresentation(bool bDowned)
{
	if (bRagdollActive == bDowned)
	{
		return;
	}
	bRagdollActive = bDowned;

	USkeletalMeshComponent* BodyMesh = GetMesh();
	UCharacterMovementComponent* Movement = GetCharacterMovement();

	if (bDowned)
	{
		if (Movement)
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}
		if (BodyMesh)
		{
			BodyMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			BodyMesh->SetCollisionProfileName(TEXT("Ragdoll"));
			BodyMesh->SetAllBodiesBelowSimulatePhysics(TEXT("pelvis"), true, true);
			BodyMesh->SetSimulatePhysics(true);
		}
		// The capsule stops shoving people around while its owner is on the floor.
		if (UCapsuleComponent* Capsule = GetCapsuleComponent())
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		if (IsLocallyControlled())
		{
			DisableInput(nullptr);
		}
		return;
	}

	// Standing back up. The mesh has to go back exactly where it was: simulating physics detaches
	// it from the capsule, and putting it back by eye leaves the body floating or sunk.
	if (BodyMesh)
	{
		BodyMesh->SetSimulatePhysics(false);
		BodyMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		BodyMesh->SetCollisionProfileName(TEXT("CharacterMesh"));
		BodyMesh->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		BodyMesh->SetRelativeTransform(MeshRelativeTransformOnSpawn);
	}
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	if (Movement)
	{
		Movement->SetMovementMode(MOVE_Walking);
	}
	if (IsLocallyControlled())
	{
		EnableInput(nullptr);
	}
}

void AShooterCharacter::Server_ReviveTeammate_Implementation(AShooterCharacter* Target)
{
	if (!Target || Target == this || !Target->IsDowned())
	{
		return;
	}

	// A player on the floor cannot pick anyone else up.
	if (bIsDowned || IsDead())
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s tried to revive %s while down itself - rejected"),
			*GetName(), *Target->GetName());
		return;
	}

	// Same margin as everything else reported by a client: both of them moved during the round trip.
	static constexpr float ReviveMarginCm = 200.0f;
	const float Distance = FVector::Dist(GetActorLocation(), Target->GetActorLocation());
	if (Distance > Target->ReviveRange + ReviveMarginCm)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s tried to revive %s from %.0f cm, reach is %.0f - rejected"),
			*GetName(), *Target->GetName(), Distance, Target->ReviveRange);
		return;
	}

	Target->ReviveFromDowned();
}

void AShooterCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// The server possesses the pawn after BeginPlay has already run, so this is where the host
	// finally has a controller to build its HUD against.
	CreateLocalHUD();

	// And where the owning client can be told what the over-prop charge widgets look like. Read from
	// the server's own subsystem, which the GameMode blueprint filled in at BeginPlay. On the host
	// this call runs locally and sets what is already set, which costs nothing.
	if (const UEMFChargeWidgetSubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>() : nullptr)
	{
		Client_ConfigureChargeWidgets(Sub->WidgetClass, Sub->ReticleWidgetClass);
	}
}

void AShooterCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();

	// Same on a client, for whichever of the controller and the HUD class arrives second.
	CreateLocalHUD();
}

void AShooterCharacter::CreateLocalHUD()
{
	// One HUD, on the machine whose player is looking through this character. A remote copy of a
	// teammate must not build one, and neither must a dedicated server.
	if (LocalHUD || !HUDClass || !IsLocallyControlled())
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	// The controller has to already own this pawn, not merely be attached to it. The HUD's Construct
	// reads Get Owning Player Pawn once and caches what it finds — on a client the pawn pointer and
	// the controller pointer arrive by separate replication paths in no fixed order, so building the
	// widget on the earlier of the two handed the blueprint a null pawn, the cast failed, and the
	// HUD sat there doing nothing for the rest of the match. Wait for the pair to be complete;
	// AShooterPlayerController::BindToPossessedCharacter calls back in once it is.
	if (PC->GetPawn() != this)
	{
		return;
	}

	LocalHUD = CreateWidget<UShooterUI>(PC, HUDClass);
	if (LocalHUD)
	{
		LocalHUD->AddToViewport(0);
		UE_LOG(LogTemp, Log, TEXT("[COOP_DEBUG] %s built its own HUD"), *GetName());
	}
}

void AShooterCharacter::Client_UpdateScore_Implementation(uint8 ScoringTeam, int32 Score)
{
	if (LocalHUD)
	{
		LocalHUD->BP_UpdateScore(ScoringTeam, Score);
	}
}

void AShooterCharacter::Client_ShowLoadingCover_Implementation(TSubclassOf<UUserWidget> CoverClass)
{
	if (LocalLoadingCover || !CoverClass || !IsLocallyControlled())
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	LocalLoadingCover = CreateWidget<UUserWidget>(PC, CoverClass);
	if (LocalLoadingCover)
	{
		// Very high Z-order so it sits above the HUD and everything else.
		LocalLoadingCover->AddToViewport(1000);
	}
}

void AShooterCharacter::Client_DismissLoadingCover_Implementation()
{
	if (LocalLoadingCover)
	{
		LocalLoadingCover->RemoveFromParent();
		LocalLoadingCover = nullptr;
	}
}

void AShooterCharacter::Client_ConfigureChargeWidgets_Implementation(TSubclassOf<UEMFChargeWidget> InWidgetClass,
	TSubclassOf<UCaptureReticleWidget> InReticleClass)
{
	UEMFChargeWidgetSubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>() : nullptr;
	if (!Sub)
	{
		return;
	}

	if (InWidgetClass)
	{
		Sub->WidgetClass = InWidgetClass;
	}
	if (InReticleClass)
	{
		Sub->ReticleWidgetClass = InReticleClass;
	}

	// Anything registered while the class was missing is sitting in the pending queue; the
	// subsystem's own tick drains it as soon as WidgetClass is set.
	UE_LOG(LogTemp, Log, TEXT("[COOP_DEBUG] %s received charge widget classes: bar=%s reticle=%s"),
		*GetName(), *GetNameSafe(InWidgetClass), *GetNameSafe(InReticleClass));
}

void AShooterCharacter::OnRep_OwnedWeapons()
{
	// Inventory arrived: the arms are allowed to exist again.
	UpdateFirstPersonMeshVisibility();
}

void AShooterCharacter::OnRep_CurrentWeapon()
{
	// [COOP_DEBUG] Does the switch actually arrive on the observer, and with a resolved actor?
	// A replicated pointer can land before the weapon actor itself is relevant here, in which case
	// this fires with null and nothing gets attached.
	UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] OnRep_CurrentWeapon: Char=%s weapon=%s hidden=%d local=%d"),
		*GetName(),
		CurrentWeapon ? *CurrentWeapon->GetName() : TEXT("NULL"),
		CurrentWeapon ? (CurrentWeapon->IsHidden() ? 1 : 0) : -1,
		IsLocallyControlled() ? 1 : 0);

	// Observers only ever learn "which weapon" — the attachment itself is local work, and the
	// weapon actor arrives unattached because its movement is not replicated.
	if (CurrentWeapon)
	{
		AttachWeaponMeshes(CurrentWeapon);

		// Attaching the mesh is only half of holding a gun: the pose comes from the weapon's
		// animation blueprint, and nothing sets it here otherwise. That is why a teammate's
		// character carried the right weapon while running with empty-handed arm swings, and why a
		// client whose equip runs on the server saw the same on its own first-person mesh.
		// OnWeaponActivated is pure presentation (HUD, anim classes, melee mesh, recoil), so it is
		// safe on any machine; ActivateWeapon is not, it also drives tutorials and visibility.
		OnWeaponActivated(CurrentWeapon);

		// CurrentWeapon and OwnedWeapons can arrive in either order, so re-check from both.
		UpdateFirstPersonMeshVisibility();
	}

	// Same event the server-side equip path fires, so the local HUD reacts identically.
	OnActiveWeaponChanged.Broadcast(CurrentWeapon);
}

void AShooterCharacter::OnRep_CurrentHP()
{
	// The authority already ran the full TakeDamage path. Everyone else only learns the result,
	// so this is where a client's HUD and death visuals catch up.
	BroadcastHealthChanged();

	// Zero HP no longer means "play the death" on its own: it is also what going down looks like,
	// and the two are told apart by flags of their own. Driving the presentation off HP here would
	// race them — replicated fields arrive in no guaranteed order, so a downed player whose HP
	// landed first would start dying before the flag saying otherwise turned up. See
	// OnRep_IsDowned and OnRep_TerminalDeath.
	if (CurrentHP > 0.0f)
	{
		// Respawn or heal: allow the next death to play again.
		bHasPlayedLocalDeath = false;
	}
}

void AShooterCharacter::OnRep_CurrentArmor()
{
	BroadcastHealthChanged();
}

namespace
{
	/** Is this actor dead right now? Mirrors IsActorDeadAfterDamage in ShooterWeapon.cpp. */
	bool IsTargetDead(const AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return true;
		}
		if (const AShooterNPC* NPC = Cast<AShooterNPC>(Actor))
		{
			return NPC->IsDead();
		}
		if (const AShooterCharacter* Character = Cast<AShooterCharacter>(Actor))
		{
			return Character->IsDead();
		}
		return false;
	}
}

void AShooterCharacter::DealDamage(AActor* HitActor, float Damage, TSubclassOf<UDamageType> DamageTypeClass,
	AShooterWeapon* Weapon)
{
	if (!HitActor || Damage <= 0.0f)
	{
		return;
	}

	if (HasAuthority())
	{
		FPointDamageEvent DamageEvent;
		DamageEvent.DamageTypeClass = DamageTypeClass;
		if (!DamageEvent.DamageTypeClass)
		{
			DamageEvent.DamageTypeClass = UDamageType::StaticClass();
		}
		HitActor->TakeDamage(Damage, DamageEvent, GetController(), this);
		return;
	}

	// Client: the hit only counts once the server has applied it.
	Server_ReportDamage(HitActor, Damage, DamageTypeClass, Weapon);
}

void AShooterCharacter::Server_FireProjectile_Implementation(AShooterWeapon* Weapon,
	const FTransform& ProjectileTransform, float ChargeMultiplier)
{
	// Same trust model as a reported hit: the client decided where its shot came from, and the server
	// checks that the answer is possible rather than re-deriving it.
	if (!Weapon || !OwnedWeapons.Contains(Weapon))
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s asked to fire a projectile from a weapon it does not own (%s) - rejected"),
			*GetName(), *GetNameSafe(Weapon));
		return;
	}

	// A muzzle sits on the character holding it. The margin is loose on purpose: the character has
	// moved on this machine since the client fired, and the muzzle is an arm's length out in front.
	static constexpr float MuzzleMarginCm = 500.0f;
	const float MuzzleDistance = FVector::Dist(GetActorLocation(), ProjectileTransform.GetLocation());
	if (MuzzleDistance > MuzzleMarginCm)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s reported a muzzle %.0f cm away from itself - rejected"),
			*GetName(), MuzzleDistance);
		return;
	}

	Weapon->SpawnProjectileAtTransform(ProjectileTransform, ChargeMultiplier, /*bCosmeticOnly*/ false);
}

void AShooterCharacter::Server_ReportDamage_Implementation(AActor* HitActor, float Damage,
	TSubclassOf<UDamageType> DamageTypeClass, AShooterWeapon* Weapon)
{
	if (!HitActor || Damage <= 0.0f)
	{
		return;
	}

	// ==================== Validation ====================
	//
	// The client still does the tracing, because that is what makes a shot feel instant, but the
	// server no longer takes the result on faith. These checks are deliberately loose: every one of
	// them either clamps or rejects something that cannot happen in normal play, so a laggy but
	// honest hit is never lost. Rejections are logged under [NET_DEBUG].
	//
	// Not checked here, on purpose:
	//  - Ammo. CurrentBullets is not replicated, so the server's copy never decrements for a client's
	//    shot and comparing against it would reject everything.
	//  - Line of sight. Without rewinding the target to where it stood when the client fired, a
	//    target that stepped behind cover during the round trip looks like a wallhack. That check
	//    belongs with lag compensation, not here.
	//  - Rate of fire. A single trigger pull reports once per *hit*, so pellets and pierced targets
	//    arrive as several calls in one frame; limiting per call would drop legitimate ones.

	// A shooter can only be hurt by their own weapon, and only ever hurt someone else with it.
	if (HitActor == this)
	{
		return;
	}

	if (!Weapon || !OwnedWeapons.Contains(Weapon))
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s reported damage with a weapon it does not own (%s) - rejected"),
			*GetName(), *GetNameSafe(Weapon));
		return;
	}

	// Nothing can be shot from further away than the weapon reaches. The margin covers the distance
	// both parties travelled during the round trip.
	static constexpr float RangeMarginCm = 500.0f;
	const float DistanceToTarget = FVector::Dist(GetActorLocation(), HitActor->GetActorLocation());
	if (DistanceToTarget > Weapon->GetMaxHitscanRange() + RangeMarginCm)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s reported a hit at %.0f cm, weapon reaches %.0f - rejected"),
			*GetName(), DistanceToTarget, Weapon->GetMaxHitscanRange());
		return;
	}

	const float DamageCeiling = Weapon->GetMaxReportedSingleHitDamage();
	if (Damage > DamageCeiling)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s reported %.0f damage, ceiling for %s is %.0f - clamped"),
			*GetName(), Damage, *Weapon->GetName(), DamageCeiling);
		Damage = DamageCeiling;
	}

	FPointDamageEvent DamageEvent;
	DamageEvent.DamageTypeClass = DamageTypeClass;
	if (!DamageEvent.DamageTypeClass)
	{
		DamageEvent.DamageTypeClass = UDamageType::StaticClass();
	}

	// The class passive's own damage, first and unconditionally. It is the half that is meant to
	// reach health through a shield that is still standing, so it must not sit behind the gate the
	// weapon's own damage sits behind. Same call the host's own shots take, so both routes into the
	// server apply it exactly once. @see AShooterWeapon::ApplyPassivePierceDamage
	const float PierceDamage = Weapon->ApplyPassivePierceDamage(HitActor);

	// The gate is re-checked HERE rather than trusted from the client. The client now reports a hit
	// even when its own copy said the shield was up, precisely so the pierce above can happen; that
	// report grants the weapon's damage nothing on its own.
	float ActualDamage = PierceDamage;
	if (!Weapon->IsShieldGateBlocking(HitActor))
	{
		ActualDamage += HitActor->TakeDamage(Damage, DamageEvent, GetController(), this);
	}

	// Only the server can know what the hit really did. Hand the answer back to the shooter, whose
	// upgrades are otherwise told nothing and never fire on a kill.
	Client_ConfirmDamageDealt(Weapon, HitActor, ActualDamage, IsTargetDead(HitActor));
}

void AShooterCharacter::Server_ReportMeleeDamage_Implementation(AActor* HitActor, float Damage,
	TSubclassOf<UDamageType> DamageTypeClass)
{
	if (!HitActor || Damage <= 0.0f)
	{
		return;
	}

	// Same trust model as a reported shot, and the same deliberate looseness: the client did the
	// tracing, because that is what makes a punch feel instant, and the server checks that the answer
	// is possible rather than re-deriving it. Everything here either clamps or rejects something that
	// cannot happen in normal play.
	if (HitActor == this)
	{
		return;
	}

	const UMeleeAttackComponent* Melee = MeleeAttackComponent;
	if (!Melee)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s reported a melee hit but has no melee component - rejected"),
			*GetName());
		return;
	}

	// The margin covers the ground both parties gave up during the round trip, exactly as it does for
	// a shot. A melee reach is short, so this is proportionally generous on purpose: rejecting an
	// honest hit costs a kill, and the ceiling below is what actually bounds the damage.
	static constexpr float ReachMarginCm = 500.0f;
	const float DistanceToTarget = FVector::Dist(GetActorLocation(), HitActor->GetActorLocation());
	const float Reach = Melee->GetMaxReportedReach();
	if (DistanceToTarget > Reach + ReachMarginCm)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s reported a melee hit at %.0f cm, melee reaches %.0f - rejected"),
			*GetName(), DistanceToTarget, Reach);
		return;
	}

	const float DamageCeiling = Melee->GetMaxReportedSingleHitDamage();
	if (Damage > DamageCeiling)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s reported %.0f melee damage, ceiling is %.0f - clamped"),
			*GetName(), Damage, DamageCeiling);
		Damage = DamageCeiling;
	}

	FPointDamageEvent DamageEvent;
	DamageEvent.DamageTypeClass = DamageTypeClass;
	if (!DamageEvent.DamageTypeClass)
	{
		DamageEvent.DamageTypeClass = UDamageType::StaticClass();
	}

	HitActor->TakeDamage(Damage, DamageEvent, GetController(), this);
}

void AShooterCharacter::Server_ReportMeleeKnockback_Implementation(AActor* Target, FVector Direction,
	float Distance, float Duration)
{
	if (!Target || Duration <= 0.0f)
	{
		return;
	}

	const UMeleeAttackComponent* Melee = MeleeAttackComponent;
	if (!Melee)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s reported a melee shove but has no melee component - rejected"),
			*GetName());
		return;
	}

	// Same shape of check as a reported hit: near enough to have been hit at all, and no further than
	// the settings on THIS machine say a shove can throw somebody.
	static constexpr float ReachMarginCm = 500.0f;
	const float DistanceToTarget = FVector::Dist(GetActorLocation(), Target->GetActorLocation());
	if (DistanceToTarget > Melee->GetMaxReportedReach() + ReachMarginCm)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s reported a melee shove at %.0f cm - rejected"),
			*GetName(), DistanceToTarget);
		return;
	}

	const float DistanceCeiling = Melee->GetMaxReportedKnockbackDistance();
	if (Distance > DistanceCeiling)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s reported a %.0f cm melee shove, ceiling is %.0f - clamped"),
			*GetName(), Distance, DistanceCeiling);
		Distance = DistanceCeiling;
	}

	// The attacker is whoever sent this, so its location is right here and does not need sending.
	UMeleeAttackComponent::ApplyKnockbackOnAuthority(Target, Direction.GetSafeNormal(), Distance, Duration,
		GetActorLocation());
}

void AShooterCharacter::Client_ApplyKnockback_Implementation(FVector LaunchVelocity)
{
	// The server has already launched its own copy of this character; this is the same launch on the
	// machine that predicts this character's movement, so the two agree and nothing gets corrected.
	// bXYOverride / bZOverride match the authority's call in ApplyKnockbackOnAuthority.
	LaunchCharacter(LaunchVelocity, true, true);

	UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s got shoved on its own client at %.0f u/s (t=%.3f)"),
		*GetName(), LaunchVelocity.Size(), GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f);
}

void AShooterCharacter::Client_ConfirmDamageDealt_Implementation(AShooterWeapon* Weapon, AActor* HitActor,
	float ActualDamage, bool bKilled)
{
	if (UpgradeManager && Weapon && HitActor)
	{
		UpgradeManager->NotifyWeaponDealtDamage(Weapon, HitActor, ActualDamage, bKilled);
	}
}

void AShooterCharacter::Server_ReportWeaponFired_Implementation(AShooterWeapon* Weapon)
{
	// Ownership, not "is it the current weapon": a shot fired a moment before a switch would
	// otherwise be dropped because the server had already moved on, which is what made the muzzle
	// flash visible in one direction and not the other. Ownership still stops a stale or foreign
	// reference from making someone else's gun flash.
	if (Weapon && OwnedWeapons.Contains(Weapon))
	{
		Weapon->Multicast_PlayFireEffects();

		// The server does not run Fire() for a remote pawn's weapon — only these effects — so this
		// is the ONLY place the server learns that a client pulled the trigger. A passive that
		// spends something per shot would otherwise never spend anything on the server and would
		// hand every client shot its maximum, forever. Arrives before the damage report, which is
		// the order the passive is written for. @see AShooterWeapon::Fire
		if (UAbilityComponent* Abilities = FindComponentByClass<UAbilityComponent>())
		{
			Abilities->NotifyOwnerFiredWeapon();
		}
	}
}

void AShooterCharacter::Server_ReportWeaponReloaded_Implementation(AShooterWeapon* Weapon)
{
	// Ownership rather than "is it equipped", for the same reason as the shot above: a reload
	// started a moment before a weapon switch still belongs to this player.
	if (Weapon && OwnedWeapons.Contains(Weapon))
	{
		Weapon->Multicast_PlayReloadEffects();
	}
}

void AShooterCharacter::Server_ReportBeamEffect_Implementation(AShooterWeapon* Weapon, FVector Start, FVector End,
	float EnergyMultiplier, float OverrideBoltSpeed, float OverrideBoltSpeedVariance,
	float OverrideBoltLength, float OverrideRandomSeed)
{
	if (Weapon && OwnedWeapons.Contains(Weapon))
	{
		Weapon->Multicast_PlayBeamEffect(Start, End, EnergyMultiplier,
			OverrideBoltSpeed, OverrideBoltSpeedVariance, OverrideBoltLength, OverrideRandomSeed);
	}
}

void AShooterCharacter::Server_ReportImpactEffect_Implementation(AShooterWeapon* Weapon,
	FVector_NetQuantize100 Location, FVector_NetQuantizeNormal Normal, uint8 SurfaceByte)
{
	// Ownership rather than "is it equipped", same as the shot and the reload above: an impact from
	// a bullet fired a moment before a weapon switch still belongs to this player.
	if (Weapon && OwnedWeapons.Contains(Weapon))
	{
		Weapon->Multicast_PlayImpactEffect(Location, Normal, SurfaceByte);
	}
}

void AShooterCharacter::Server_CaptureProp_Implementation(AEMFPhysicsProp* Prop, float ReportedCaptureRange)
{
	if (!Prop || !Prop->bCanBeCaptured)
	{
		return;
	}

	// Already holding it — the reverse-channeling re-attach path re-confirms the same capture, so
	// this has to be a harmless no-op rather than a rejection.
	if (Prop->GetHoldingCharacter() == this)
	{
		return;
	}

	if (Prop->GetHoldingCharacter() != nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s tried to capture %s, already held by %s - rejected"),
			*GetName(), *Prop->GetName(), *GetNameSafe(Prop->GetHoldingCharacter()));
		return;
	}

	// Same distance margin as a reported hit: the character has moved since the client captured,
	// and capture range already has its own generous falloff.
	// The reported range, held to what this client's own search radius could ever have found.
	float CaptureRange = FMath::Max(0.0f, ReportedCaptureRange);
	if (const UChargeAnimationComponent* Charge = GetChargeAnimationComponent())
	{
		if (CaptureRange > Charge->CaptureSearchRadius)
		{
			UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s reported a capture range of %.0f, its search radius is %.0f - clamped"),
				*GetName(), CaptureRange, Charge->CaptureSearchRadius);
			CaptureRange = Charge->CaptureSearchRadius;
		}
	}

	static constexpr float CaptureMarginCm = 500.0f;
	const float DistanceToProp = FVector::Dist(GetActorLocation(), Prop->GetActorLocation());
	if (DistanceToProp > CaptureRange + CaptureMarginCm)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s tried to capture %s at %.0f cm, range is %.0f - rejected"),
			*GetName(), *Prop->GetName(), DistanceToProp, CaptureRange);
		return;
	}

	Prop->BeginRemoteHold(this, CaptureRange);
}

void AShooterCharacter::Server_ReleaseProp_Implementation(AEMFPhysicsProp* Prop)
{
	if (!Prop || Prop->GetHoldingCharacter() != this)
	{
		return;
	}

	Prop->EndRemoteHold();
}

void AShooterCharacter::Server_RequestWeaponPickup_Implementation(ADroppedRangedWeapon* Drop, float ReportedCaptureRange)
{
	if (!Drop)
	{
		return;
	}

	// The reported reach, held to what this client's own search radius could ever have found.
	float ClaimedRange = FMath::Max(0.0f, ReportedCaptureRange);
	if (const UChargeAnimationComponent* Charge = GetChargeAnimationComponent())
	{
		ClaimedRange = FMath::Min(ClaimedRange, Charge->CaptureSearchRadius);
	}

	// Reach test with the same round-trip margin as everything else reported from a client.
	static constexpr float PickupMarginCm = 500.0f;
	const float DistanceToDrop = FVector::Dist(GetActorLocation(), Drop->GetActorLocation());
	const float PickupRange = ClaimedRange + PickupMarginCm;
	if (DistanceToDrop > PickupRange)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s asked to pick up %s at %.0f cm, reach is %.0f - rejected"),
			*GetName(), *Drop->GetName(), DistanceToDrop, PickupRange);
		return;
	}

	if (!Drop->TryStartPullForClient(this))
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s asked to pick up %s, already taken or not capturable - rejected"),
			*GetName(), *Drop->GetName());
	}
}

void AShooterCharacter::Server_ReportIonization_Implementation(AActor* Target, AShooterWeapon* Weapon)
{
	if (!Target || !Weapon || !OwnedWeapons.Contains(Weapon))
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s reported ionization with a weapon it does not own (%s) - rejected"),
			*GetName(), *GetNameSafe(Weapon));
		return;
	}

	// Same reach test as a reported hit, same margin for the round trip.
	static constexpr float RangeMarginCm = 500.0f;
	const float DistanceToTarget = FVector::Dist(GetActorLocation(), Target->GetActorLocation());
	if (DistanceToTarget > Weapon->GetMaxHitscanRange() + RangeMarginCm)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s reported ionizing a target at %.0f cm, weapon reaches %.0f - rejected"),
			*GetName(), DistanceToTarget, Weapon->GetMaxHitscanRange());
		return;
	}

	// Null component: the shield rule was already applied on the shooter's machine (see the header).
	Weapon->ApplyHitscanIonization(Target, nullptr);
}

FVector AShooterCharacter::GetAllyHoldPoint() const
{
	// The same point the channeling plate sits at: eye position plus the plate's local offset turned
	// by where this character is aiming. Both machines can compute it from replicated state alone.
	static constexpr float HoldForwardOffset = 200.0f;
	return GetPawnViewLocation() + GetBaseAimRotation().Vector() * HoldForwardOffset;
}

void AShooterCharacter::Server_CaptureAlly_Implementation(AShooterCharacter* Ally)
{
	if (!Ally || Ally == this || Ally->IsDead())
	{
		return;
	}

	if (Ally->HeldByCharacter != nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] %s tried to pick up %s, already held by %s - rejected"),
			*GetName(), *Ally->GetName(), *GetNameSafe(Ally->HeldByCharacter));
		return;
	}

	static constexpr float GrabRangeCm = 1200.0f;
	static constexpr float GrabMarginCm = 300.0f;
	const float Distance = FVector::Dist(GetActorLocation(), Ally->GetActorLocation());
	if (Distance > GrabRangeCm + GrabMarginCm)
	{
		UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] %s tried to pick up %s at %.0f cm - rejected"),
			*GetName(), *Ally->GetName(), Distance);
		return;
	}

	Ally->HeldByCharacter = this;

	UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] Server_CaptureAlly %s picked up %s at %.0f cm"),
		*GetName(), *Ally->GetName(), Distance);
}

void AShooterCharacter::Server_ReleaseAlly_Implementation(AShooterCharacter* Ally)
{
	if (!Ally || Ally->HeldByCharacter != this)
	{
		return;
	}

	Ally->HeldByCharacter = nullptr;

	UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] Server_ReleaseAlly %s put down %s"), *GetName(), *Ally->GetName());
}

void AShooterCharacter::Server_LaunchAlly_Implementation(AShooterCharacter* Ally, FVector LaunchVelocity)
{
	if (!Ally || Ally == this || Ally->IsDead())
	{
		return;
	}

	// Only the carrier may throw, and only what they are actually carrying. Without this the throw is
	// a free "shove any player in view" for anyone who asks.
	if (Ally->HeldByCharacter != this)
	{
		UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] %s tried to throw %s it is not holding - rejected"),
			*GetName(), *Ally->GetName());
		return;
	}

	// Cleared BEFORE the launch: the hold pins velocity every frame, so a launch issued while still
	// held would be overwritten on the very next simulated move.
	Ally->HeldByCharacter = nullptr;

	// Same shape of check the prop throw makes: the client decided, the server agrees or refuses. The
	// margin is generous on purpose -- both players have moved since the grab, and this is a teammate,
	// so the cost of being strict is a throw that silently does nothing.
	static constexpr float ThrowRangeCm = 1200.0f;
	static constexpr float ThrowMarginCm = 500.0f;
	const float Distance = FVector::Dist(GetActorLocation(), Ally->GetActorLocation());
	if (Distance > ThrowRangeCm + ThrowMarginCm)
	{
		UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] %s tried to throw %s at %.0f cm - rejected"),
			*GetName(), *Ally->GetName(), Distance);
		return;
	}

	// Speed is not taken from the client. Direction is theirs -- it came from their camera and only
	// they know where they were looking -- but how hard a throw is belongs to the game.
	const UEMFVelocityModifier* AllyMod = Ally->FindComponentByClass<UEMFVelocityModifier>();
	const float Speed = AllyMod ? AllyMod->AllyLaunchSpeed : 2000.0f;
	const FVector Velocity = LaunchVelocity.GetSafeNormal() * Speed;

	Ally->LaunchCharacter(Velocity, true, true);

	UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] Server_LaunchAlly %s -> %s vel=%s"),
		*GetName(), *Ally->GetName(), *Velocity.ToCompactString());
}

void AShooterCharacter::Server_LaunchProp_Implementation(AEMFPhysicsProp* Prop)
{
	if (!Prop || Prop->GetHoldingCharacter() != this)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s tried to throw %s it is not holding - rejected"),
			*GetName(), *GetNameSafe(Prop));
		return;
	}

	Prop->BeginRemoteLaunch(this);
}

// ==================== Lunge reach ====================

float AShooterCharacter::ApplyLungePassiveToRange(const AActor* Target, float BaseRange) const
{
	if (BaseRange <= 0.0f)
	{
		return 0.0f;
	}

	// Asked through the ability component rather than by knowing about any particular class: the
	// Melee's passive answers with more reach into a broken shield, every other passive and the
	// absence of one hands the base range straight back, and nothing here learns which is which.
	if (const UAbilityComponent* Abilities = FindComponentByClass<UAbilityComponent>())
	{
		if (const UAbilityHandler* Passive = Abilities->GetPassiveHandler())
		{
			const float Granted = FMath::Max(0.0f, Passive->ModifyLungeRange(Target, BaseRange));
			if (IsLungeDebugEnabled())
			{
				UE_LOG(LogTemp, Warning, TEXT("[LUNGE_DEBUG] passive %s: base=%.0f -> %.0f for %s"),
					*GetNameSafe(Passive->GetDefinition()), BaseRange, Granted,
					Target ? *GetNameSafe(Target) : TEXT("<ceiling>"));
			}
			return Granted;
		}
	}

	// The single most common reason the reach never grows: DA_Class_*.PassiveAbility is empty, so
	// there is no handler to ask and the base range is all there is. Said out loud because from
	// inside the game it looks identical to a passive that is installed and doing nothing.
	if (IsLungeDebugEnabled())
	{
		UE_LOG(LogTemp, Warning, TEXT("[LUNGE_DEBUG] NO PASSIVE HANDLER on %s: reach stays at base %.0f"),
			*GetName(), BaseRange);
	}

	return BaseRange;
}

float AShooterCharacter::GetActiveLungeRangeFor(const AActor* Target) const
{
	// One answer now, whatever is in hand. AShooterWeapon_Melee used to carry a second lunge with
	// its own reach and this had to pick between them; the weapon borrows the component's lunge
	// instead, so there is nothing left to choose.
	if (const UMeleeAttackComponent* Melee = GetMeleeAttackComponent())
	{
		return Melee->GetLungeRangeFor(Target);
	}

	return 0.0f;
}

float AShooterCharacter::GetActiveMaxLungeRange() const
{
	// A null target is the agreed way to ask for a ceiling rather than for one enemy's answer. See
	// UAbilityHandler::ModifyLungeRange.
	return GetActiveLungeRangeFor(nullptr);
}

bool AShooterCharacter::WouldLungeAt(const AActor* Target, const FVector& ViewLocation, const FVector& ViewForward) const
{
	if (!Target)
	{
		return false;
	}

	const float Reach = GetActiveLungeRangeFor(Target);
	if (Reach <= 0.0f)
	{
		return false;
	}

	const FVector ToTarget = Target->GetActorLocation() - ViewLocation;
	const float Dist = ToTarget.Size();
	if (Dist < 1.0f || Dist > Reach)
	{
		return false;
	}

	const FVector DirToTarget = ToTarget / Dist;

	// One cone, because there is one lunge: the component's, whether the swing came from a fist or
	// from a blade borrowing it.
	if (const UMeleeAttackComponent* Melee = GetMeleeAttackComponent())
	{
		const float ConeCos = FMath::Cos(FMath::DegreesToRadians(Melee->Settings.LungeConeHalfAngle));
		return FVector::DotProduct(ViewForward, DirToTarget) >= ConeCos;
	}

	return false;
}

void AShooterCharacter::Server_ConsumePropForHeal_Implementation(AEMFPhysicsProp* Prop)
{
	if (!Prop || Prop->GetHoldingCharacter() != this)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s tried to eat %s it is not holding - rejected"),
			*GetName(), *GetNameSafe(Prop));
		return;
	}

	// The verb is checked HERE and not on the machine that pressed the button. A client can send
	// this RPC whenever it likes; whether this player's class turns props into medicine is the
	// server's answer, and taking the client's word for it would let any class heal off any prop.
	if (GetItemVerb() != EClassItemVerb::Heal)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s tried to eat %s but its class verb is %d - rejected"),
			*GetName(), *GetNameSafe(Prop), (int32)GetItemVerb());
		return;
	}

	Prop->ConsumeForHeal(this);
}

void AShooterCharacter::Server_UpdateHeldPropTransform_Implementation(AEMFPhysicsProp* Prop, FVector Location,
	FRotator Rotation, FVector LinearVelocity)
{
	if (!Prop || Prop->GetHoldingCharacter() != this)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s reported a held-prop transform for %s it is not holding - rejected"),
			*GetName(), *GetNameSafe(Prop));
		return;
	}

	static constexpr float HoldMarginCm = 500.0f;
	const float HoldRange = Prop->GetHeldCaptureRange();
	const float DistanceToReport = FVector::Dist(GetActorLocation(), Location);
	if (DistanceToReport > HoldRange + HoldMarginCm)
	{
		UE_LOG(LogTemp, Warning, TEXT("[NET_DEBUG] %s reported a held prop %.0f cm away, range is %.0f - rejected"),
			*GetName(), DistanceToReport, HoldRange);
		return;
	}

	// Throttled: this arrives every tick of every hold.
	static double LastHeldLogTime = 0.0;
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Now - LastHeldLogTime >= 0.5)
	{
		LastHeldLogTime = Now;
		UE_LOG(LogTemp, Warning, TEXT("[HOLD_DEBUG] SERVER got held transform for %s at %s vel=%.0f"),
			*Prop->GetName(), *Location.ToCompactString(), LinearVelocity.Size());
	}

	Prop->ApplyHeldTransform(Location, Rotation, LinearVelocity);
}

float AShooterCharacter::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// Only the server decides how much health anyone has. Without this a client would kill its
	// own local copy of a teammate while the real one stands there at full HP, which is exactly
	// what the first coop session showed.
	if (!HasAuthority())
	{
		return 0.0f;
	}

	// ignore if already dead
	if (CurrentHP <= 0.0f)
	{
		return 0.0f;
	}

	// Armor absorption (DOOM Eternal-style): armor absorbs damage before health
	float RemainingDamage = Damage;
	if (CurrentArmor > 0.0f)
	{
		const float ArmorAbsorbed = FMath::Min(CurrentArmor, RemainingDamage);
		CurrentArmor -= ArmorAbsorbed;
		RemainingDamage -= ArmorAbsorbed;
	}

	// Reduce HP by remaining damage after armor
	CurrentHP -= RemainingDamage;

	// Reset regeneration delay timer
	TimeSinceLastDamage = 0.0f;

	// Get damage type for feedback
	TSubclassOf<UDamageType> DamageTypeClass = DamageEvent.DamageTypeClass;

	// Calculate damage direction angle relative to player forward
	// Only show damage direction for actual damage (positive value), not healing
	FVector DamageDirection = FVector::ZeroVector;
	if (DamageCauser && Damage > 0.0f)
	{
		// Get direction from damage source to player
		DamageDirection = (DamageCauser->GetActorLocation() - GetActorLocation()).GetSafeNormal();

		// Get player's forward vector (ignore pitch)
		FVector PlayerForward = GetActorForwardVector();
		PlayerForward.Z = 0.0f;
		PlayerForward.Normalize();

		FVector DamageDir2D = DamageDirection;
		DamageDir2D.Z = 0.0f;
		DamageDir2D.Normalize();

		// Calculate angle using atan2 for proper signed angle
		// Positive = right side, Negative = left side
		float DotProduct = FVector::DotProduct(PlayerForward, DamageDir2D);
		float CrossProduct = FVector::CrossProduct(PlayerForward, DamageDir2D).Z;
		float AngleDegrees = FMath::RadiansToDegrees(FMath::Atan2(CrossProduct, DotProduct));

		// Broadcast damage direction
		OnDamageDirection.Broadcast(AngleDegrees, Damage);
	}

	// Play damage feedback (camera shake, impact sound)
	if (Damage > 0.0f)
	{
		PlayDamageFeedback(Damage, DamageTypeClass);
	}

	// Anything the class answers damage with (the Tank returns part of it) gets told here, on the
	// authority, with health already moved -- so a passive can see whether this hit killed him.
	if (AbilityComponent && Damage > 0.0f)
	{
		// The event's own account of where it landed: a point hit gives the real impact and bone, a
		// radial or generic one gives the actor. Asked for here because it is gone by the time
		// anything downstream wants it, and a reaction drawn from the wound needs both.
		FHitResult HitInfo;
		FVector ImpulseDir = FVector::ZeroVector;

		// FRadialDamageEvent::GetBestHitInfo reads ComponentHits[0] behind nothing but an ensure, so
		// a radial event whose ComponentHits nobody filled takes the whole game down the first time
		// an explosion touches a player. Every radial event raised by hand in this project is built
		// that way - the projectile, the flying drone and the kamikaze all set Origin and Params and
		// stop there - because filling ComponentHits means running the component sweep that
		// UGameplayStatics::ApplyRadialDamageWithFalloff does internally, and none of them go through
		// it.
		//
		// Guarded here rather than only at those three sites because this is the single consumer:
		// one check covers the sources that exist and the ones somebody adds later, which is the
		// difference between fixing a crash and fixing this crash.
		bool bHasUsableHitInfo = true;
		if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
		{
			const FRadialDamageEvent& RadialEvent = static_cast<const FRadialDamageEvent&>(DamageEvent);
			bHasUsableHitInfo = RadialEvent.ComponentHits.Num() > 0;

			if (!bHasUsableHitInfo)
			{
				// Same answer GetBestHitInfo would have given, minus the crash: for a radial event
				// the "impact" is the blast origin, not a point on the body. Downstream reactions
				// read the direction from it, and origin-to-victim is the direction the blast came
				// from, which is what they actually want.
				ImpulseDir = (GetActorLocation() - RadialEvent.Origin).GetSafeNormal();

				HitInfo.bBlockingHit = true;
				HitInfo.HitObjectHandle = FActorInstanceHandle(this);
				HitInfo.Component = Cast<UPrimitiveComponent>(GetRootComponent());
				HitInfo.ImpactPoint = RadialEvent.Origin;
				HitInfo.Location = RadialEvent.Origin;
				HitInfo.ImpactNormal = -ImpulseDir;
				HitInfo.Normal = -ImpulseDir;
			}
		}

		if (bHasUsableHitInfo)
		{
			DamageEvent.GetBestHitInfo(this, DamageCauser, HitInfo, ImpulseDir);
		}

		if (UAbilityComponent::IsBeamDebugEnabled())
		{
			// The first link of the chain: what the event itself claimed, before any ability read it.
			// The type matters more than the numbers -- a radial event's "best hit info" is its ORIGIN,
			// which sits on whatever exploded rather than on the body it damaged.
			const TCHAR* EventKind =
				DamageEvent.IsOfType(FPointDamageEvent::ClassID)  ? TEXT("Point")  :
				DamageEvent.IsOfType(FRadialDamageEvent::ClassID) ? TEXT("Radial") : TEXT("Generic");

			UE_LOG(LogTemp, Warning,
				TEXT("[BEAM_DEBUG] TAKEDAMAGE kind=%s dmg=%.1f causer=%s impact=%s bone='%s'")
				TEXT(" impactDistToMe=%.0f myLocation=%s"),
				EventKind, Damage, *GetNameSafe(DamageCauser),
				*FVector(HitInfo.ImpactPoint).ToCompactString(),
				*HitInfo.BoneName.ToString(),
				FVector::Dist(FVector(HitInfo.ImpactPoint), GetActorLocation()),
				*GetActorLocation().ToCompactString());
		}

		AbilityComponent->NotifyOwnerDamaged(Damage, DamageCauser, EventInstigator, HitInfo);
	}

	// Apply knockback for melee damage
	if (bEnableMeleeKnockback && Damage > 0.0f && DamageTypeClass)
	{
		if (DamageTypeClass->IsChildOf(UDamageType_Melee::StaticClass()))
		{
			// Knockback direction is away from damage source
			FVector KnockbackDir = -DamageDirection;
			KnockbackDir.Z = 0.0f;
			if (!KnockbackDir.IsNearlyZero())
			{
				KnockbackDir.Normalize();
				ApplyMeleeKnockback(KnockbackDir, MeleeKnockbackDistance, MeleeKnockbackDuration);
			}
		}
	}

	// Apply damage slowdown for ranged hits
	if (bEnableDamageSlowdown && Damage > 0.0f && DamageTypeClass && DamageSlowdownArray.Num() > 0)
	{
		if (DamageTypeClass->IsChildOf(UDamageType_Ranged::StaticClass()))
		{
			DamageSlowdownHitCount++;

			// Restart the reset timer
			GetWorldTimerManager().SetTimer(
				DamageSlowdownResetTimerHandle,
				this,
				&AShooterCharacter::OnDamageSlowdownTimerExpired,
				DamageSlowdownWindow,
				false
			);

			ApplyDamageSlowdown();
		}
	}

	// Have we depleted HP?
	if (CurrentHP <= 0.0f)
	{
		// Going down is not dying. The team loses a pair of hands and gains something to do about
		// it; the run only actually ends once nobody is left to do it, which is the question
		// ShouldRunEndOnThisDeath already answers. Note the order: this player is at zero HP by now,
		// so it counts itself as down and the last one standing falling really is the last.
		if (!bIsDowned && !ShouldRunEndOnThisDeath())
		{
			EnterDownedState();
		}
		else
		{
			Die();
		}
	}

	// update health/armor listeners
	BroadcastHealthChanged();

	// Trigger first-damage tutorial arrow (Health Bar)
	if (!FirstDamageTutorialID.IsNone() && Damage > 0.0f)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UTutorialSubsystem* TutorialSub = GI->GetSubsystem<UTutorialSubsystem>())
			{
				APlayerController* PC = Cast<APlayerController>(GetController());
				TutorialSub->ShowHUDArrow(FirstDamageTutorialID, FirstDamageArrowData, PC);
			}
		}
	}

	return Damage;
}

void AShooterCharacter::DoStartFiring()
{
	// Don't fire if melee attacking
	if (MeleeAttackComponent && MeleeAttackComponent->IsAttacking())
	{
		return;
	}

	// Don't fire if charge animating (but allow during channeling/reverse channeling)
	if (ChargeAnimationComponent && ChargeAnimationComponent->IsBlockingFiring())
	{
		return;
	}

	// Don't fire while an ability is casting — the fire montage shares the FP mesh slot and
	// would interrupt the ability's montage (e.g. Charge Burst).
	if (AbilityComponent && AbilityComponent->IsCasting())
	{
		return;
	}

	// Hands are busy swapping. The shot is remembered rather than dropped, the same bargain the
	// sprint-out gate below makes: let go of the trigger and it is forgotten, keep holding it and
	// the weapon fires the instant the draw finishes.
	if (IsWeaponSwitchInProgress())
	{
		bFireHeldThroughSwitch = true;
		return;
	}

	// Firing vetoes sprinting. Done here, on the input, and not on the shot itself: the shot is
	// resolved with the server in the loop and would arrive at a different time on every machine,
	// while the input is the same thing the movement prediction is built from. The veto does not
	// clear the player's sprint intent, so holding the key through a firefight resumes sprinting
	// by itself once the suppression lapses.
	if (UApexMovementComponent* Apex = GetApexMovement())
	{
		Apex->SuppressSprint();
	}

	// fire the current weapon
	if (CurrentWeapon)
	{
		CurrentWeapon->StartFiring();
	}
}

void AShooterCharacter::DoStopFiring()
{
	// Letting go is an answer too: a shot queued during a swap is forgotten here rather than going
	// off by itself once the draw ends.
	bFireHeldThroughSwitch = false;

	// stop firing the current weapon
	if (CurrentWeapon)
	{
		CurrentWeapon->StopFiring();
	}

	// Notify recoil component that firing ended
	if (RecoilComponent)
	{
		RecoilComponent->OnFiringEnded();
	}
}

void AShooterCharacter::AccumulateFirstPersonSpinePose(float DeltaTime, FVector& Translation, FRotator& Rotation)
{
	Super::AccumulateFirstPersonSpinePose(DeltaTime, Translation, Rotation);

	if (!MovementSettings)
	{
		return;
	}

	// Reload. Keyed to the animation rather than to the reload state, for the same reason the left
	// hand IK is: a weapon with no reload montage assigned must not spend the reload leaning into a
	// pose with nothing playing.
	const bool bReloadAnim = IsPlayingReloadAnimation();
	ReloadSpineAlpha = FMath::FInterpTo(ReloadSpineAlpha, bReloadAnim ? 1.0f : 0.0f,
		DeltaTime, MovementSettings->SpinePoseInterpSpeed);

	if (CurrentWeapon && ReloadSpineAlpha > KINDA_SMALL_NUMBER)
	{
		Translation += CurrentWeapon->ReloadSpinePose.Translation * ReloadSpineAlpha;
		Rotation += CurrentWeapon->ReloadSpinePose.Rotation * ReloadSpineAlpha;
	}
}

void AShooterCharacter::UpdateLeftHandPose(float DeltaTime)
{
	const bool bWallrunning = GetApexMovement() && GetApexMovement()->IsWallRunning();

	const UAbilityComponent* AbilComp = FindComponentByClass<UAbilityComponent>();
	const bool bCasting = AbilComp && AbilComp->IsCasting();

	LeftHandWallrunAlpha = FMath::FInterpTo(LeftHandWallrunAlpha, bWallrunning ? 1.0f : 0.0f,
		DeltaTime, LeftHandPoseInterpSpeed);
	LeftHandCastAlpha = FMath::FInterpTo(LeftHandCastAlpha, bCasting ? 1.0f : 0.0f,
		DeltaTime, LeftHandPoseInterpSpeed);

	LeftHandPoseOffset =
		LeftHandWallrunOffset * LeftHandWallrunAlpha +
		LeftHandAbilityCastOffset * LeftHandCastAlpha;

	static const FName LeftHandPoseOffsetName(TEXT("LeftHandPoseOffset"));

	if (const USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
	{
		PushAnimVector(FPMesh->GetAnimInstance(), LeftHandPoseOffsetName, LeftHandPoseOffset);
	}
}

bool AShooterCharacter::IsPlayingReloadAnimation() const
{
	if (!CurrentWeapon)
	{
		return false;
	}

	UAnimMontage* Montage = CurrentWeapon->GetReloadMontage();
	if (!Montage)
	{
		return false;
	}

	const USkeletalMeshComponent* FPMesh = GetFirstPersonMesh();
	UAnimInstance* AnimInstance = FPMesh ? FPMesh->GetAnimInstance() : nullptr;

	return AnimInstance && AnimInstance->Montage_IsPlaying(Montage);
}

void AShooterCharacter::DoReload()
{
	// Not while the weapon is being put away or brought out: the reload montage would fight the
	// swap montage over the same arms, and a reload started on a gun that is leaving the hand
	// finishes behind the player's back.
	if (IsWeaponSwitchInProgress())
	{
		return;
	}

	// The weapon owns the decision: it knows whether it has a magazine, whether that magazine is
	// already full and whether it is the kind that gets thrown away instead of reloaded.
	if (CurrentWeapon)
	{
		CurrentWeapon->StartReload();
	}
}

void AShooterCharacter::DoSwitchWeapon()
{
	// Forward cycle (next weapon).
	CycleWeapon(1);
}

void AShooterCharacter::DoSwitchWeaponBackward()
{
	// Reverse cycle (previous weapon) — bound to SwitchWeaponBackAction (e.g. mouse wheel down).
	CycleWeapon(-1);
}

void AShooterCharacter::CycleWeapon(int32 Direction)
{
	// Don't switch if melee attacking
	if (MeleeAttackComponent && MeleeAttackComponent->IsAttacking())
	{
		return;
	}

	// Don't switch if charge animating
	if (ChargeAnimationComponent && ChargeAnimationComponent->IsAnimating())
	{
		return;
	}

	// Don't switch while an ability is casting — swapping weapons re-inits the FP AnimInstance
	// (SetAnimInstanceClass), which orphans the ability montage's end delegate and can leave the
	// cast stuck (bIsCasting never cleared).
	if (AbilityComponent && AbilityComponent->IsCasting())
	{
		return;
	}

	// Don't interrupt a weapon being put away; a draw may be interrupted. @see CanStartWeaponSwitch
	if (!CanStartWeaponSwitch())
	{
		return;
	}

	// Ensure we have at least two weapons to switch between
	if (OwnedWeapons.Num() > 1)
	{
		// Find the index of the current weapon in the owned list
		int32 WeaponIndex = OwnedWeapons.Find(CurrentWeapon);
		if (WeaponIndex == INDEX_NONE)
		{
			WeaponIndex = 0;
		}

		// Step by Direction (+1 = next, -1 = previous), wrapping around both ends.
		// Adding Count before the modulo keeps the result non-negative when Direction == -1.
		const int32 Count = OwnedWeapons.Num();
		WeaponIndex = (WeaponIndex + Direction + Count) % Count;

		// Start animated switch to the new weapon
		StartWeaponSwitch(OwnedWeapons[WeaponIndex]);
	}
}

void AShooterCharacter::DoWeaponSwitchByAction(UInputAction* Action)
{
	if (!Action)
	{
		return;
	}

	// Don't switch if melee attacking
	if (MeleeAttackComponent && MeleeAttackComponent->IsAttacking())
	{
		return;
	}

	// Don't switch if charge animating
	if (ChargeAnimationComponent && ChargeAnimationComponent->IsAnimating())
	{
		return;
	}

	// Don't switch while an ability is casting — see DoSwitchWeapon for rationale.
	if (AbilityComponent && AbilityComponent->IsCasting())
	{
		return;
	}

	// Don't interrupt a weapon being put away; a draw may be interrupted. @see CanStartWeaponSwitch
	if (!CanStartWeaponSwitch())
	{
		return;
	}

	// Equip the owned weapon whose per-weapon SwitchAction matches the pressed key (if not already held).
	// Several weapon classes can share one action, but only one is ever owned, so the match is unambiguous.
	for (AShooterWeapon* TargetWeapon : OwnedWeapons)
	{
		if (TargetWeapon && TargetWeapon->GetSwitchAction() == Action && TargetWeapon != CurrentWeapon)
		{
			StartWeaponSwitch(TargetWeapon);
			return;
		}
	}
}

/** Slack added before the failsafe timer fires behind a swap-point notify. Enough that the notify
 *  still wins the race, short enough that a genuinely interrupted swap recovers unnoticed. */
static constexpr float WeaponSwitchFailsafeMargin = 0.1f;

namespace
{
	/** Where in Montage the animator placed the swap point, in montage seconds. False when there is
	 *  none, which is the case the timing below has to cover by itself. */
	bool FindWeaponSwapPointTime(const UAnimMontage* Montage, float& OutTime)
	{
		if (!Montage)
		{
			return false;
		}

		for (const FAnimNotifyEvent& Event : Montage->Notifies)
		{
			if (Event.Notify && Event.Notify->IsA<UAnimNotify_WeaponSwitchSwapPoint>())
			{
				OutTime = Event.GetTriggerTime();
				return true;
			}
		}

		return false;
	}
}

float AShooterCharacter::GetHolsterSwapDelay(const AShooterWeapon* Weapon) const
{
	if (!Weapon)
	{
		return 0.0f;
	}

	const UAnimMontage* Montage = Weapon->GetHolsterMontage();
	if (!Montage)
	{
		return 0.0f;
	}

	const float Rate = FMath::Max(Weapon->GetHolsterPlayRate(), KINDA_SMALL_NUMBER);

	float NotifyTime = 0.0f;
	if (FindWeaponSwapPointTime(Montage, NotifyTime))
	{
		// The notify does the work at exactly the authored frame; this is only the net under it.
		return NotifyTime / Rate + WeaponSwitchFailsafeMargin;
	}

	// Nobody placed a swap point, so the timing is ours to pick, and "when the montage ends" is the
	// wrong answer: a montage does not end, it fades out, and what it fades back to is the idle pose
	// holding the OLD weapon. That fade is the gap where the old gun reappears on screen. Swap as
	// the fade begins instead, so the draw montage takes over from the holster pose and the two
	// animations cross straight into each other.
	const float BlendOut = Montage->GetDefaultBlendOutTime();
	return FMath::Max(0.0f, (Montage->GetPlayLength() - BlendOut) / Rate);
}

void AShooterCharacter::StartWeaponSwitch(AShooterWeapon* NewWeapon)
{
	if (!NewWeapon || NewWeapon == CurrentWeapon)
	{
		return;
	}

	// Don't let a manual switch cut the yank-throw montage short: its AnimNotifies carry the
	// discard gameplay (dropped-weapon spawn + inventory removal), and swapping the anim
	// instance mid-montage would leave the yanked weapon in limbo. The montage's own Lower
	// notify performs the switch when it finishes.
	if (PendingYankThrowWeapon.IsValid() &&
		IsYankThrowMontageActiveOnFPMesh(ChargeAnimationComponent, GetFirstPersonMesh()))
	{
		return;
	}

	// A swap that is putting a weapon away owns the sequence until its swap point: the meshes have
	// not changed hands yet, and interrupting there would strand the old weapon in the hand. A DRAW
	// may be interrupted, and that is the case worth allowing: the player has already changed their
	// mind, and finishing an animation for a gun they no longer want reads as ignored input.
	if (!CanStartWeaponSwitch())
	{
		return;
	}

	// Interrupting a draw. The weapon it already put in our hands stays there; the code below is
	// what puts THAT one away, so the sequence stays honest: every weapon leaves the hand through
	// its own holster animation.
	CancelWeaponSwitch();

	// Stop firing current weapon
	if (CurrentWeapon)
	{
		CurrentWeapon->StopFiring();
	}

	PendingWeapon = NewWeapon;

	// Coop, unchanged in substance from the instant swap this replaces: which weapon a character
	// holds is the server's decision, because CurrentWeapon and the weapon actor's hidden flag both
	// replicate downward and overwrite a purely local idea (that is what produced "a rifle held in
	// a pistol stance" in the first coop session). What the animation adds is only WHEN the local
	// equip and the request to the authority happen: at the swap point instead of on the keypress.
	// Both still happen in the same breath, so the prediction and the confirmation cannot disagree.
	const float HolsterLength = CurrentWeapon ? CurrentWeapon->GetHolsterLength() : 0.0f;
	if (HolsterLength <= 0.0f)
	{
		// No holster animation on this weapon: the instant swap, exactly as before, plus whatever
		// draw the incoming weapon has. This is the fallback that keeps unanimated guns working.
		PlayWeaponSwitchSound();
		BeginWeaponDraw();
		return;
	}

	PlayWeaponSwitchMontage(CurrentWeapon->GetHolsterMontage(), CurrentWeapon->GetHolsterMontageTP(),
		CurrentWeapon->GetHolsterPlayRate());

	WeaponSwitchPhase = EWeaponSwitchPhase::Holstering;

	// The swap normally happens on the montage's notify. This timer is the failsafe behind it (a
	// montage cut short by death or another animation never sends its notify), and when no swap
	// point was placed at all it is the swap itself. @see GetHolsterSwapDelay
	GetWorldTimerManager().SetTimer(WeaponSwitchTimer, this, &AShooterCharacter::OnWeaponSwitchSwapNotify,
		FMath::Max(GetHolsterSwapDelay(CurrentWeapon), KINDA_SMALL_NUMBER), false);

	// Play weapon switch sound
	PlayWeaponSwitchSound();
}

void AShooterCharacter::EquipWeaponImmediate(AShooterWeapon* NewWeapon)
{
	if (!NewWeapon || NewWeapon == CurrentWeapon)
	{
		return;
	}

	// [COOP_DEBUG] Who ran the equip, on which side, and what it hid/showed.
	UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] EquipWeaponImmediate: Char=%s authority=%d local=%d | old=%s new=%s"),
		*GetName(), HasAuthority() ? 1 : 0, IsLocallyControlled() ? 1 : 0,
		CurrentWeapon ? *CurrentWeapon->GetName() : TEXT("NULL"),
		*NewWeapon->GetName());

	// Same body the lowered phase of the animated switch used to run.
	AShooterWeapon* OldWeapon = CurrentWeapon;

	if (CurrentWeapon)
	{
		CurrentWeapon->DeactivateWeapon();
	}

	CurrentWeapon = NewWeapon;
	CurrentWeapon->ActivateWeapon();
	PendingWeapon = nullptr;

	// Observers get this through OnRep_CurrentWeapon; the local machine needs it here.
	AttachWeaponMeshes(CurrentWeapon);
	OnActiveWeaponChanged.Broadcast(CurrentWeapon);

	if (UpgradeManager && OldWeapon != CurrentWeapon)
	{
		UpgradeManager->NotifyWeaponChanged(OldWeapon, CurrentWeapon);
	}
}

void AShooterCharacter::Server_RequestEquipWeapon_Implementation(AShooterWeapon* Weapon)
{
	// Only ever equip something this character actually owns, so a stale or foreign reference
	// cannot put someone else's gun in these hands.
	if (Weapon && OwnedWeapons.Contains(Weapon))
	{
		EquipWeaponImmediate(Weapon);
	}
}

float AShooterCharacter::PlayWeaponSwitchMontage(UAnimMontage* FirstPersonMontage, UAnimMontage* ThirdPersonMontage, float PlayRate)
{
	float Length = 0.0f;

	// The player's own arms, on this machine only: nobody else has a copy of them.
	if (FirstPersonMontage)
	{
		if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
		{
			if (UAnimInstance* AnimInstance = FPMesh->GetAnimInstance())
			{
				Length = AnimInstance->Montage_Play(FirstPersonMontage, PlayRate);
			}
		}
	}

	// The body everyone else is looking at, on every machine, at the same rate. A teammate watching
	// must see the weapon leave the hand at the moment it actually leaves it.
	if (ThirdPersonMontage)
	{
		PlayThirdPersonMontageEverywhere(ThirdPersonMontage, PlayRate);
	}

	return Length;
}

void AShooterCharacter::OnWeaponSwitchSwapNotify()
{
	// Only the putting-away half has a swap point. This guard is what makes the notify and its
	// failsafe timer safe to both fire: whichever arrives second finds the phase already moved on
	// and does nothing, so the weapons never change hands twice.
	if (WeaponSwitchPhase != EWeaponSwitchPhase::Holstering)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(WeaponSwitchTimer);

	if (!PendingWeapon)
	{
		// Yank: the hands are empty and what fills them is still flying over. Put the old weapon
		// away for real and wait; FinishWeaponSwitch names the arrival when it lands.
		if (CurrentWeapon)
		{
			CurrentWeapon->DeactivateWeapon();
		}

		WeaponSwitchPhase = EWeaponSwitchPhase::WaitingForWeapon;
		return;
	}

	BeginWeaponDraw();
}

void AShooterCharacter::BeginWeaponDraw()
{
	AShooterWeapon* NewWeapon = PendingWeapon;
	if (!NewWeapon)
	{
		// Nothing to draw: a yank whose weapon never arrived. End the swap rather than leaving the
		// player locked out of firing forever.
		FinishWeaponDraw();
		return;
	}

	// Apply locally right away so the weapon changes in your own hands with no round trip, then
	// let the authority make it true for everyone. The server always accepts a weapon this
	// character owns, so this prediction cannot disagree with the confirmation.
	EquipWeaponImmediate(NewWeapon);

	if (!HasAuthority())
	{
		Server_RequestEquipWeapon(NewWeapon);
	}

	PendingWeapon = nullptr;

	const float DrawLength = CurrentWeapon ? CurrentWeapon->GetDrawLength() : 0.0f;
	if (DrawLength <= 0.0f)
	{
		// Unanimated weapon: it is simply in the hand and ready, which is the old behaviour.
		FinishWeaponDraw();
		return;
	}

	PlayWeaponSwitchMontage(CurrentWeapon->GetDrawMontage(), CurrentWeapon->GetDrawMontageTP(),
		CurrentWeapon->GetDrawPlayRate());

	WeaponSwitchPhase = EWeaponSwitchPhase::Drawing;

	// The arms are held back for the first frames of the draw, exactly as they are coming out of a
	// grapple. It is the same defect and it was visible here first: the montage above has been
	// ASKED for, not evaluated, so this frame still holds the pose the last animation left behind
	// while the new weapon is already in the hand. Only when there is a montage to wait for -- an
	// unanimated weapon has no pose to arrive late. @see FirstPersonRevealFramesLeft
	FirstPersonRevealFramesLeft = 2;
	UpdateFirstPersonMeshVisibility();

	// A timer rather than a notify: nothing gameplay-critical lands inside the draw, only its end,
	// and an interrupted draw is cancelled explicitly by whatever interrupted it.
	GetWorldTimerManager().SetTimer(WeaponSwitchTimer, this, &AShooterCharacter::FinishWeaponDraw,
		DrawLength, false);
}

void AShooterCharacter::FinishWeaponDraw()
{
	GetWorldTimerManager().ClearTimer(WeaponSwitchTimer);

	WeaponSwitchPhase = EWeaponSwitchPhase::None;
	PendingWeapon = nullptr;

	// The shot the player asked for while their hands were busy. Routed back through DoStartFiring
	// so it re-checks everything else that can veto a shot; the phase is already None, so it cannot
	// queue itself a second time.
	if (bFireHeldThroughSwitch)
	{
		bFireHeldThroughSwitch = false;
		DoStartFiring();
	}
}

void AShooterCharacter::StowWeaponForGrapple(float SpeedMultiplier)
{
	if (bWeaponStowedForGrapple)
	{
		return;
	}
	bWeaponStowedForGrapple = true;

	// A swap already running loses. The player asked for a grapple, both hands are needed now, and
	// the alternative -- letting a holster-into-draw finish while the line is out -- would put a
	// weapon back into hands that are on a rope. The weapon left holding is whatever the swap had
	// got as far as, and that is the one that comes back afterwards.
	CancelWeaponSwitch();
	PendingWeapon = nullptr;

	if (!CurrentWeapon)
	{
		// Nothing in hand to put away. Still take the phase, so that the gates which read it behave
		// the same whether or not the character happened to be holding something.
		WeaponSwitchPhase = EWeaponSwitchPhase::StowedForGrapple;
		return;
	}

	CurrentWeapon->StopFiring();

	const float Mult = FMath::Max(SpeedMultiplier, KINDA_SMALL_NUMBER);
	const float Length = CurrentWeapon->GetHolsterLength() / Mult;

	if (Length <= 0.0f)
	{
		// Unanimated weapon: it simply vanishes, which is what an unanimated swap does too.
		FinishGrappleStow();
		return;
	}

	// The play rate is SCALED rather than replaced, so a weapon with a deliberately heavy holster
	// stays heavier than a light one; the multiplier is a "do it quicker", not a fixed duration.
	PlayWeaponSwitchMontage(CurrentWeapon->GetHolsterMontage(), CurrentWeapon->GetHolsterMontageTP(),
		CurrentWeapon->GetHolsterPlayRate() * Mult);

	WeaponSwitchPhase = EWeaponSwitchPhase::StowingForGrapple;

	// A plain timer, not the swap-point notify: that notify exists to change two weapons over at an
	// exact frame, and there is no second weapon here. Nothing gameplay-critical lands inside this
	// animation, only its end.
	GetWorldTimerManager().SetTimer(WeaponSwitchTimer, this, &AShooterCharacter::FinishGrappleStow,
		Length, false);
}

void AShooterCharacter::FinishGrappleStow()
{
	GetWorldTimerManager().ClearTimer(WeaponSwitchTimer);

	// DeactivateWeapon rather than a bare SetActorHiddenInGame: it also stops firing, cancels a
	// reload that would otherwise finish behind the player's back, and tells the character so the
	// recoil and melee state come back to rest. On the authority the hidden flag replicates, which
	// is how a teammate sees the gun leave the hand.
	if (CurrentWeapon)
	{
		CurrentWeapon->DeactivateWeapon();
	}

	// The phase goes in BEFORE the visibility call, because the phase is what that call reads to
	// decide the arms are away.
	WeaponSwitchPhase = EWeaponSwitchPhase::StowedForGrapple;
	UpdateFirstPersonMeshVisibility();
}

void AShooterCharacter::UnstowWeaponAfterGrapple(float SpeedMultiplier)
{
	if (!bWeaponStowedForGrapple)
	{
		return;
	}
	bWeaponStowedForGrapple = false;

	// Covers the short grapple: the line let go while the holster animation was still playing, so
	// the weapon was never actually hidden. Activating a weapon that is already visible is harmless.
	GetWorldTimerManager().ClearTimer(WeaponSwitchTimer);

	// The phase has to leave StowedForGrapple before the arms can come back: that is the flag the
	// visibility rule reads. Doing it here rather than in each branch below also covers the
	// weaponless case, where nothing else would ever put them back.
	//
	// But they do not come back on THIS frame, and the branch below that plays the draw sets the
	// hold for exactly that reason. Set here as well so the weaponless and unanimated cases -- which
	// return before ever reaching it -- still get their frames rather than popping.
	WeaponSwitchPhase = EWeaponSwitchPhase::None;
	FirstPersonRevealFramesLeft = 2;
	UpdateFirstPersonMeshVisibility();

	if (!CurrentWeapon)
	{
		return;
	}

	CurrentWeapon->ActivateWeapon();

	const float Mult = FMath::Max(SpeedMultiplier, KINDA_SMALL_NUMBER);
	const float Length = CurrentWeapon->GetDrawLength() / Mult;

	if (Length <= 0.0f)
	{
		FinishWeaponDraw();
		return;
	}

	PlayWeaponSwitchMontage(CurrentWeapon->GetDrawMontage(), CurrentWeapon->GetDrawMontageTP(),
		CurrentWeapon->GetDrawPlayRate() * Mult);

	// Ends through FinishWeaponDraw, which is the same ending a swap has: the phase clears and a
	// trigger held through the grapple finally fires. Reused rather than copied precisely so that
	// the deferred shot keeps working here too.
	WeaponSwitchPhase = EWeaponSwitchPhase::Drawing;

	GetWorldTimerManager().SetTimer(WeaponSwitchTimer, this, &AShooterCharacter::FinishWeaponDraw,
		Length, false);
}

void AShooterCharacter::CancelWeaponSwitch()
{
	GetWorldTimerManager().ClearTimer(WeaponSwitchTimer);

	WeaponSwitchPhase = EWeaponSwitchPhase::None;
	PendingWeapon = nullptr;

	// bFireHeldThroughSwitch deliberately survives: a swap interrupted by another swap is still one
	// continuous "I am holding the trigger" from the player's side.
}

void AShooterCharacter::DoMeleeAttack()
{
	UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] === DoMeleeAttack CALLED ==="));
	UE_LOG(LogTemp, Warning, TEXT("[MELEE_INPUT_DEBUG] DoMeleeAttack (Triggered) fired @ %.3fs"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f);

	// Block regular swings while a ChargedPunch is in ANY active phase — charging,
	// flying toward endpoint, or waiting for the post-lunge air montage to finish.
	// IA_Melee's Triggered binding pulses repeatedly during the hold AND after, so
	// the simple bIsCharging check missed the lunge / post-anim windows: a Triggered
	// pulse during the flight made the player perform a ground swing on the NPC
	// they were piercing through, applying ordinary melee damage instead of the
	// charged-punch effect.
	if (UUpgrade_ChargedPunch* ChargedPunch = FindComponentByClass<UUpgrade_ChargedPunch>())
	{
		if (ChargedPunch->IsBusy())
		{
			UE_LOG(LogTemp, Warning, TEXT("[MELEE_INPUT_DEBUG] DoMeleeAttack SUPPRESSED — ChargedPunch busy (charging=%d, lunging=%d)"),
				ChargedPunch->IsCharging() ? 1 : 0, ChargedPunch->IsLunging() ? 1 : 0);
			return;
		}
	}

	// Don't melee if charge animating
	if (ChargeAnimationComponent && ChargeAnimationComponent->IsAnimating())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] BLOCKED: ChargeAnimation playing"));
		return;
	}

	// Don't melee if weapon switch in progress
	if (IsWeaponSwitchInProgress())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] BLOCKED: WeaponSwitch"));
		return;
	}

	// Check for boss finisher mode
	if (bIsOnBossFinisher && !bBossFinisherActive)
	{
		StartBossFinisher();
		return;
	}

	// Don't allow normal melee during boss finisher
	if (bBossFinisherActive)
	{
		return;
	}

	// Shield equipped → suppress melee entirely (mirrors prop-capture behavior: hands are busy holding it).
	if (EquippedShield)
	{
		return;
	}

	if (MeleeAttackComponent)
	{
		// Stop firing if we're shooting
		if (CurrentWeapon)
		{
			CurrentWeapon->StopFiring();
		}

		bool bResult = MeleeAttackComponent->StartAttack();
		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] StartAttack returned: %d"), bResult);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[DROPKICK_DEBUG] BLOCKED: No MeleeAttackComponent!"));
	}
}

void AShooterCharacter::DoMeleePressed()
{
	UE_LOG(LogTemp, Warning, TEXT("[MELEE_INPUT_DEBUG] DoMeleePressed (Started) fired @ %.3fs"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f);
	// Broadcast for hold-based upgrades. The regular swing still fires from
	// DoMeleeAttack on Triggered — this hook only signals "button went down".
	OnMeleeChargeHoldStarted.Broadcast();
}

void AShooterCharacter::DoMeleeReleased()
{
	UE_LOG(LogTemp, Warning, TEXT("[MELEE_INPUT_DEBUG] DoMeleeReleased (Completed) fired @ %.3fs"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f);
	// Broadcast for hold-based upgrades to finalize. Subscribers decide whether
	// the elapsed hold time crossed their threshold and act accordingly.
	OnMeleeChargeHoldReleased.Broadcast();
}

void AShooterCharacter::DoAbilityPressed()
{
	// Diagnostic: confirms the C++ IA_Ability binding is the trigger. If the ability fires WITHOUT this
	// line appearing in the log, something else (a Blueprint node or another IMC mapping) called it.
	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_INPUT_DEBUG] DoAbilityPressed (C++ AbilityAction binding) @ %.3fs"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f);

	// Dump EVERY key currently mapped to AbilityAction across ALL active mapping contexts — this is what
	// the engine actually sees (not just one IMC asset). If a mouse-wheel / swap key shows up here, then
	// IA_Ability is mapped to it in one of the active IMCs, and the bug is in the input data, not this code.
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				FString KeyList;
				for (const FKey& MappedKey : Sub->QueryKeysMappedToAction(AbilityAction))
				{
					KeyList += MappedKey.ToString() + TEXT(" ");
				}
				UE_LOG(LogTemp, Warning, TEXT("[ABILITY_INPUT_DEBUG]   AbilityAction='%s' is mapped to keys: [ %s]"),
					*GetNameSafe(AbilityAction), *KeyList);
			}
		}
	}

	// Not while the weapon is being put away or brought out. An ability montage shares the FP arms
	// with the swap, and the swap is the one the player asked for first.
	if (IsWeaponSwitchInProgress())
	{
		return;
	}

	// Only an aimed ability defers its shot to the release. Everything else keeps firing on the press:
	// moving the activation for all abilities would hand a Hold-mode charge its own release in the
	// same frame it started, which is not a change anybody asked for.
	if (AbilityComponent && Cast<UAbilityDefinition_ShieldBypass>(AbilityComponent->GetActiveAbility()))
	{
		// The player holds to see who the bolt will pick, and lets go to send it.
		BeginAbilityAiming();
		return;
	}

	if (AbilityComponent)
	{
		AbilityComponent->TryActivate();
	}
}

void AShooterCharacter::DoAbilityReleased()
{
	UE_LOG(LogTemp, Warning, TEXT("[ABILITY_INPUT_DEBUG] DoAbilityReleased (C++ AbilityAction binding) @ %.3fs"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f);

	// Hand the authority the enemy that was actually highlighted, THEN fire. Order matters: the
	// handler reads the aim target, so it has to be there before the activation arrives.
	if (bAbilityAiming)
	{
		Server_SetAbilityAimTarget(AbilityAimTarget.Get());
		if (AbilityComponent)
		{
			AbilityComponent->TryActivate();
		}
		EndAbilityAiming();
	}

	if (AbilityComponent)
	{
		AbilityComponent->OnButtonReleased();
	}
}

void AShooterCharacter::Server_SetAbilityAimTarget_Implementation(AShooterNPC* Target)
{
	// Believed, not re-derived, for the same reason the melee lunge's target is: the two machines
	// would otherwise pick different enemies whenever two stand close together, and the brackets the
	// player was looking at would have been a lie. Validated where it is used, not here.
	AbilityAimTarget = Target;
}

void AShooterCharacter::BeginAbilityAiming()
{
	if (bAbilityAiming || !IsLocallyControlled())
	{
		return;
	}
	bAbilityAiming = true;

	// Borrow the capture brackets rather than draw a second set. Two reticles on one screen read as
	// noise, and the capture one is already the right shape.
	if (UEMFChargeWidgetSubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>() : nullptr)
	{
		Sub->SetReticleSuppressed(true);
	}
}

void AShooterCharacter::EndAbilityAiming()
{
	if (!bAbilityAiming)
	{
		return;
	}
	bAbilityAiming = false;
	AbilityAimTarget = nullptr;

	if (UEMFChargeWidgetSubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>() : nullptr)
	{
		if (UCaptureReticleWidget* Reticle = Sub->GetReticleForExternalUse(Cast<APlayerController>(GetController())))
		{
			Reticle->ClearTarget();
		}
		Sub->SetReticleSuppressed(false);
	}
}

void AShooterCharacter::UpdateAbilityAiming()
{
	if (!bAbilityAiming || !IsLocallyControlled())
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(GetController());
	UEMFChargeWidgetSubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>() : nullptr;
	if (!PC || !Sub)
	{
		return;
	}

	UCaptureReticleWidget* Reticle = Sub->GetReticleForExternalUse(PC);
	if (!Reticle)
	{
		return;
	}

	// Same formula the handler will run, so the brackets and the bolt cannot disagree.
	float Range = 4000.0f;
	if (AbilityComponent)
	{
		if (const UAbilityDefinition_ShieldBypass* Def = Cast<UAbilityDefinition_ShieldBypass>(AbilityComponent->GetActiveAbility()))
		{
			Range = Def->TargetSearchRange;
		}
	}

	AShooterNPC* Best = UAbilityHandler_ShieldBypass::ScoreBestTarget(this, Range);
	AbilityAimTarget = Best;

	if (!Best)
	{
		Reticle->ClearTarget();
		return;
	}

	FVector2D Screen;
	if (!PC->ProjectWorldLocationToScreen(Best->GetActorLocation(), Screen))
	{
		Reticle->ClearTarget();
		return;
	}

	// Bracket size from the target's own bounds, so a boss gets bigger brackets than a grunt without
	// anybody authoring a number per enemy.
	FVector BoundsOrigin, BoundsExtent;
	Best->GetActorBounds(true, BoundsOrigin, BoundsExtent);
	const float Distance = FMath::Max(1.0f, FVector::Dist(GetActorLocation(), Best->GetActorLocation()));
	const float PixelRadius = FMath::Clamp((BoundsExtent.Size() / Distance) * 600.0f, 24.0f, 400.0f);

	Reticle->UpdateForTarget(Screen, PixelRadius, 0);
}

void AShooterCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateAbilityAiming();


	// Boss finisher has priority over everything
	if (bBossFinisherActive)
	{
		UpdateBossFinisher(DeltaTime);
		return; // Skip normal updates during finisher
	}

	// Update knockback interpolation if active
	if (bIsInKnockback)
	{
		UpdateKnockbackInterpolation(DeltaTime);
	}

	// Update chromatic aberration effect if active
	if (bChromaticAberrationActive)
	{
		UpdateChromaticAberration(DeltaTime);
	}

	// The grapple line is drawn on every machine, including the ones only watching this character:
	// what it reads is replicated to simulated proxies for exactly that. Purely cosmetic, so it sits
	// in the tick rather than in the movement simulation the swing itself lives in.
	UpdateGrappleVisual(DeltaTime);

	// The arms held back for a frame or two after a grapple's draw begins, so they are not shown in
	// the pose they were stowed in. @see FirstPersonRevealFramesLeft. Reaching zero is what puts
	// them back, and the visibility call is made once, on that frame, rather than every frame.
	if (FirstPersonRevealFramesLeft > 0)
	{
		--FirstPersonRevealFramesLeft;
		if (FirstPersonRevealFramesLeft == 0)
		{
			UpdateFirstPersonMeshVisibility();
		}
	}

	// Keep the sprint veto alive while the trigger or the aim button is held. Refreshing it every
	// frame is what turns SprintSuppressionTime into "this long after the last shot" instead of
	// "this long after the first one": a full auto burst or a held aim keeps pushing the deadline,
	// and sprinting comes back on its own once neither is happening.
	if (UApexMovementComponent* Apex = GetApexMovement())
	{
		if (bWantsToAim || (CurrentWeapon && CurrentWeapon->IsFiring()))
		{
			Apex->SuppressSprint();
		}
	}

	UpdateADS(DeltaTime);
	UpdateRegeneration(DeltaTime);
	UpdateLeftHandIK(DeltaTime);
	UpdateLeftHandPose(DeltaTime);
	UpdateLowHealthWarning(DeltaTime);
	UpdatePostProcessEffects(DeltaTime);

	// Update recoil component state
	if (RecoilComponent)
	{
		RecoilComponent->SetAiming(bWantsToAim);

		// Check if crouching via ApexMovement or CharacterMovement
		bool bIsCrouching = false;
		if (UApexMovementComponent* Apex = GetApexMovement())
		{
			bIsCrouching = Apex->IsCrouching() || Apex->IsSliding();
		}
		else
		{
			bIsCrouching = GetCharacterMovement()->IsCrouching();
		}
		RecoilComponent->SetCrouching(bIsCrouching);
	}

	// ==================== UI Updates ====================

	// Update Heat UI from current weapon
	if (CurrentWeapon && CurrentWeapon->IsHeatSystemEnabled())
	{
		float HeatPercent = CurrentWeapon->GetCurrentHeat();
		float DamageMult = CurrentWeapon->GetHeatDamageMultiplier();
		OnHeatUpdated.Broadcast(HeatPercent, DamageMult);
	}
	else
	{
		// No heat system - broadcast 0 heat
		OnHeatUpdated.Broadcast(0.0f, 1.0f);
	}

	// Update Speed UI
	float CurrentSpeed = GetVelocity().Size();
	float SpeedPercent = FMath::Clamp(CurrentSpeed / MaxSpeedForUI, 0.0f, 1.0f);
	OnSpeedUpdated.Broadcast(SpeedPercent, CurrentSpeed, MaxSpeedForUI);

	// Update Charge/Polarity UI - get charge from EMFVelocityModifier (not PolarityCharacter::CurrentCharge!)
	float ChargeValue = 0.0f;
	float StableCharge = 0.0f;
	float UnstableCharge = 0.0f;
	float MaxStableCharge = 0.0f;
	float MaxUnstableCharge = 0.0f;

	if (UEMFVelocityModifier* EMFMod = FindComponentByClass<UEMFVelocityModifier>())
	{
		ChargeValue = EMFMod->GetCharge();
		StableCharge = EMFMod->GetBaseCharge();
		UnstableCharge = EMFMod->GetBonusCharge();
		MaxStableCharge = EMFMod->MaxBaseCharge;
		MaxUnstableCharge = EMFMod->MaxBonusCharge;
	}

	// Determine current polarity (0=Neutral, 1=Positive, 2=Negative)
	uint8 CurrentPolarity = 0; // Neutral
	if (ChargeValue > KINDA_SMALL_NUMBER)
	{
		CurrentPolarity = 1; // Positive
	}
	else if (ChargeValue < -KINDA_SMALL_NUMBER)
	{
		CurrentPolarity = 2; // Negative
	}

	// Broadcast charge update every tick
	OnChargeUpdated.Broadcast(ChargeValue, CurrentPolarity);

	// Broadcast extended charge info with stable/unstable breakdown
	float TotalCharge = StableCharge + UnstableCharge;
	OnChargeExtended.Broadcast(TotalCharge, StableCharge, UnstableCharge, MaxStableCharge, MaxUnstableCharge, CurrentPolarity);

	// Check if polarity changed
	if (CurrentPolarity != PreviousPolarity)
	{
		OnPolarityChanged.Broadcast(CurrentPolarity, ChargeValue);
		UpdateChargeOverlay(CurrentPolarity);

		// Trigger first-depletion tutorial arrow — when polarity returns to Neutral after being charged
		if (CurrentPolarity == 0 && PreviousPolarity != 0 && !FirstDepletionTutorialID.IsNone())
		{
			if (UGameInstance* GI = GetGameInstance())
			{
				if (UTutorialSubsystem* TutorialSub = GI->GetSubsystem<UTutorialSubsystem>())
				{
					APlayerController* PC = Cast<APlayerController>(GetController());

					// Force-route to the dedicated FirstDepleted BP branch regardless of editor defaults
					FTutorialHUDArrowData ArrowDataCopy = FirstDepletionArrowData;
					ArrowDataCopy.TargetElement = EHUDElement::FirstDepleted;
					TutorialSub->ShowHUDArrow(FirstDepletionTutorialID, ArrowDataCopy, PC);
				}
			}
		}

		PreviousPolarity = CurrentPolarity;
	}

	/*bool bIsWallRunning = false;
	if (UApexMovementComponent* Apex = GetApexMovement())
	{
		bIsWallRunning = Apex->IsWallRunning();
	}


	if (bIsWallRunning)
	{
		SetLeftHandIKAlpha(0.0f);  // ���� ��������
	}
	else
	{
		SetLeftHandIKAlpha(1.0f);  // ���� �� ������

	}*/
	
}

void AShooterCharacter::DoStartADS()
{
	// Don't ADS if melee attacking
	if (MeleeAttackComponent && MeleeAttackComponent->IsAttacking())
	{
		return;
	}

	// Don't ADS while shield is raised (shield blocks the sight line)
	if (EquippedShield && EquippedShield->IsRaised())
	{
		return;
	}

	// Don't ADS if charge animating
	if (ChargeAnimationComponent && ChargeAnimationComponent->IsAnimating())
	{
		return;
	}

	// Don't ADS while the yank-throw montage is playing (throw interrupts ADS; don't let it
	// re-enter mid-animation). Guarded by Montage_IsPlaying rather than PendingYankThrowWeapon
	// alone so an interrupted montage can never leave ADS permanently blocked.
	if (PendingYankThrowWeapon.IsValid() &&
		IsYankThrowMontageActiveOnFPMesh(ChargeAnimationComponent, GetFirstPersonMesh()))
	{
		return;
	}

	// Let upgrades consume weapon-specific secondary actions before the weapon itself
	// (e.g. melee charge on ADS while a sword is equipped).
	if (CurrentWeapon && UpgradeManager && UpgradeManager->HandleWeaponSecondaryAction(CurrentWeapon))
	{
		return;
	}

	// Let weapon handle secondary action as ability (e.g. laser's Second Harmonic)
	if (CurrentWeapon && CurrentWeapon->OnSecondaryAction())
	{
		return;
	}

	if (MovementSettings && MovementSettings->bEnableADS)
	{
		bWantsToAim = true;

		// The movement component needs its own copy: MovementSettings::ADSSpeed caps ground speed
		// inside the movement simulation, and the bit has to ride the saved move so the server caps
		// a remote client's pawn on the same frames. Reading bWantsToAim from there would give the
		// server nothing, since this function only ever runs on the aiming player's machine.
		if (UApexMovementComponent* Apex = GetApexMovement())
		{
			Apex->SetAiming(true);
		}

		if (CurrentWeapon)
		{
			CurrentWeapon->PlayADSInSound();
		}

		// Tell recoil component we're aiming (split between camera/viewmodel kick)
		if (RecoilComponent)
		{
			RecoilComponent->SetAiming(true);
		}
	}
}

void AShooterCharacter::DoStopADS()
{
	// Notify weapon that secondary action button was released
	// Must come before bWantsToAim check: when OnSecondaryAction() returned true,
	// bWantsToAim was never set, but the weapon still needs the release callback
	if (CurrentWeapon)
	{
		if (UpgradeManager)
		{
			UpgradeManager->HandleWeaponSecondaryActionReleased(CurrentWeapon);
		}
		CurrentWeapon->OnSecondaryActionReleased();
	}

	if (bWantsToAim && CurrentWeapon)
	{
		CurrentWeapon->PlayADSOutSound();
	}

	bWantsToAim = false;

	// Unconditional, unlike the start: this also runs when bEnableADS was turned off or a weapon
	// swallowed the press, and a stuck aim bit would leave the player capped to ADSSpeed forever.
	if (UApexMovementComponent* Apex = GetApexMovement())
	{
		Apex->SetAiming(false);
	}

	// Tell recoil component we stopped aiming
	if (RecoilComponent)
	{
		RecoilComponent->SetAiming(false);
	}
}

void AShooterCharacter::UpdateADS(float DeltaTime)
{
	// Shield camera offset — interpolated independently of ADS being enabled, applied at the very
	// end so it composes with whatever path (ADS-on or ADS-off) sets the camera transform.
	{
		const FVector ShieldOffsetTarget = (EquippedShield && EquippedShield->IsRaised())
			? EquippedShield->GetCameraOffsetWhenRaised()
			: FVector::ZeroVector;
		CurrentShieldCameraOffset = FMath::VInterpTo(
			CurrentShieldCameraOffset, ShieldOffsetTarget, DeltaTime, ShieldCameraInterpSpeed);
	}

	if (!MovementSettings || !MovementSettings->bEnableADS)
	{
		// ADS disabled but we still need the shield offset to land on the camera. The camera's
		// relative location is otherwise untouched here, so set it = base + shield offset each tick.
		if (UCameraComponent* Cam = GetFirstPersonCameraComponent())
		{
			AppliedCrouchCameraOffset = GetCrouchCameraOffset();
			Cam->SetRelativeLocation(BaseCameraLocation + CurrentShieldCameraOffset + AppliedCrouchCameraOffset);
		}
		return;
	}

	// Determine target alpha
	float TargetAlpha = bWantsToAim ? 1.0f : 0.0f;

	// Interpolate alpha (used by other systems like recoil WeaponFraction)
	CurrentADSAlpha = FMath::FInterpTo(
		CurrentADSAlpha,
		TargetAlpha,
		DeltaTime,
		MovementSettings->ADSInterpSpeed
	);

	// Hand the alpha to the FP anim graph. Nothing in animation knew about aiming before this:
	// the whole difference between hip and aimed was a component transform, so the arms kept
	// playing the hip idle either way.
	//
	// The graph does NOT use this to point the weapon — AccumulateADSSightAlignment does that, and
	// more precisely than a pose can. What the graph owes us is a CALM hold: the aimed target pose
	// is the inverse of where the sight sits relative to the mesh, so every degree the idle swings
	// the gun is a degree the alignment counter-swings the whole mesh, arms included.
	//
	// Reflection, like the other pushes here, so this is a silent no-op until an ABP declares a
	// float named ADSAlpha. See Docs/ADS_Editor_Handoff_WavePistol_2026-08-20.md.
	if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
	{
		static const FName ADSAlphaName(TEXT("ADSAlpha"));
		PushAnimFloat(FPMesh->GetAnimInstance(), ADSAlphaName, CurrentADSAlpha);
	}

	// ==================== Camera Placement ====================
	// ADS no longer moves the camera. The FP mesh is parented to the camera, so the weapon's
	// ADSCamera anchor is a descendant of it — chasing that anchor with the camera would be a
	// runaway feedback loop. Instead AccumulateFirstPersonPose slides the MESH so the anchor
	// lands on the eye. The camera keeps only its own offsets: shake and the raised shield.
	UCameraComponent* Camera = GetFirstPersonCameraComponent();
	if (!Camera)
	{
		return;
	}

	FVector ShakeOffset = FVector::ZeroVector;
	if (UCameraShakeComponent* ShakeComp = GetCameraShake())
	{
		ShakeOffset = ShakeComp->GetCameraOffset();
	}

	// The crouch counter-offset joins the camera's own offsets here, and is remembered so the pose
	// pipeline can tell it apart from them: shake and shield are deliberately only half-followed by
	// the hands, while a crouch has to be followed whole (the hands are part of the view, and a
	// half-followed crouch would slide them out of the frame and back).
	AppliedCrouchCameraOffset = GetCrouchCameraOffset();

	Camera->SetRelativeLocation(BaseCameraLocation + ShakeOffset + CurrentShieldCameraOffset + AppliedCrouchCameraOffset);

	// Hipfire FOV comes from the player setting, read fresh every frame. This is the ONLY path the
	// setting takes to the renderer, on purpose.
	//
	// It used to also be pushed with APlayerCameraManager::SetFOV from ApplyGameplaySettings, under
	// the belief that LockedFOV goes nowhere. It does go somewhere: ULocalPlayer::GetViewPoint
	// overwrites the view's FOV with GetFOVAngle() (LocalPlayer.cpp:715), so a non-zero LockedFOV
	// pins the rendered FOV for the whole session and aim zoom simply stops existing. See the note
	// in UShooterGameSettings::ApplyGameplaySettings.
	//
	// Everything downstream (shake BaseFOV, then the mirrored first person FOV) follows from here.
	float HipfireFOV = BaseCameraFOV;
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UShooterSettingsSubsystem* SettingsSub = GI->GetSubsystem<UShooterSettingsSubsystem>())
		{
			HipfireFOV = SettingsSub->GetFieldOfView();
		}
	}

	// FOV target. ONE knob owns aim zoom: AShooterWeapon::ADSZoom, "how many times closer", set per
	// weapon in its Blueprint. It is a multiplier and not an angle because an angle would have to be
	// an angle relative to something, and the only something available is the player's own FOV
	// setting, which the player moves. That is precisely what used to break aiming: the target FOV
	// was authored absolutely, so a weapon tuned to 40 degrees gave 2.75x zoom to a player on 90,
	// 1.2x to a player on 70, and NO zoom whatsoever to a player on 40. A multiplier means the same
	// thing at every setting.
	//
	// The ADSCamera component's own FieldOfView is NOT read here and means nothing: that component
	// exists to mark where the sight sits, not how much it magnifies.
	const float ADSFOV = CurrentWeapon
		? AShooterWeapon::ApplyZoomToFOV(HipfireFOV, CurrentWeapon->GetADSZoom())
		: HipfireFOV;

	// Blend hip to aim. Interpolating the ANGLE rather than the tangent is deliberate: a linear
	// sweep in degrees is the shape players read as a smooth pull to the sights, and the endpoint
	// is what has to be exact, not the path.
	//
	// CameraShakeComponent overwrites Camera->FieldOfView each frame using its own BaseFOV + effect
	// offsets, so we route our blended value through SetBaseFOV(). The speed effects (slide,
	// wallrun, air dash) are additive degrees on top of it, faded out as the sights come up so a
	// slide cannot push the scope back open.
	const float InterpFOV = FMath::Lerp(HipfireFOV, ADSFOV, CurrentADSAlpha);
	if (UCameraShakeComponent* ShakeComp = GetCameraShake())
	{
		ShakeComp->SetBaseFOV(InterpFOV);
		ShakeComp->SetFOVEffectScale(1.0f - CurrentADSAlpha);
	}
	else
	{
		Camera->SetFieldOfView(InterpFOV);
	}

	// First person FOV is kept EQUAL to the world FOV. Written here for the case where there is no
	// shake component; when there is one it writes FieldOfView after us, and mirrors the first
	// person FOV itself. The per-weapon ADSCamera->FirstPersonFieldOfView is deliberately ignored:
	// letting it drift from the world FOV is what put the weapon out of the hands.
	Camera->FirstPersonFieldOfView = InterpFOV;
}

void AShooterCharacter::UpdateRegeneration(float DeltaTime)
{
	// Check if regeneration is enabled
	if (!bEnableRegeneration)
	{
		return;
	}

	// Don't regenerate if dead
	if (CurrentHP <= 0.0f)
	{
		return;
	}

	// Don't regenerate if already at max HP
	if (CurrentHP >= MaxHP)
	{
		return;
	}

	// Update damage delay timer
	TimeSinceLastDamage += DeltaTime;

	// Check if we're still in the post-damage delay period
	if (TimeSinceLastDamage < RegenDelayAfterDamage)
	{
		return;
	}

	// Calculate current speed ratio (0-1)
	const float CurrentSpeed = GetVelocity().Size();
	const float SpeedRatio = FMath::Clamp(CurrentSpeed / MaxSpeedForRegen, 0.0f, 1.0f);

	// Calculate regen multiplier from speed
	float RegenMultiplier;
	if (SpeedToRegenCurve)
	{
		// Use curve for custom falloff
		RegenMultiplier = FMath::Clamp(SpeedToRegenCurve->GetFloatValue(SpeedRatio), 0.0f, 1.0f);
	}
	else
	{
		// Linear interpolation
		RegenMultiplier = SpeedRatio;
	}

	// Calculate final regen rate
	const float CurrentRegenRate = FMath::Lerp(BaseRegenRate, MaxRegenRate, RegenMultiplier);

	// Apply regeneration
	const float OldHP = CurrentHP;
	CurrentHP = FMath::Min(CurrentHP + CurrentRegenRate * DeltaTime, MaxHP);

	// Update health listeners if HP changed
	if (CurrentHP != OldHP)
	{
		BroadcastHealthChanged();
	}
}

void AShooterCharacter::BroadcastHealthChanged()
{
	const float SafeMaxHP = FMath::Max(1.0f, MaxHP);
	const float ClampedCurrentHP = FMath::Clamp(CurrentHP, 0.0f, SafeMaxHP);
	const float LifePercent = ClampedCurrentHP / SafeMaxHP;
	const float ArmorPercent = MaxArmor > 0.0f ? FMath::Clamp(CurrentArmor / MaxArmor, 0.0f, 1.0f) : 0.0f;

	OnHealthChanged.Broadcast(ClampedCurrentHP, SafeMaxHP, LifePercent, ArmorPercent);

	// Legacy route for existing listeners that only need normalized health/armor values.
	OnDamaged.Broadcast(LifePercent, ArmorPercent);
}

void AShooterCharacter::UpdateChargeOverlay(uint8 NewPolarity)
{
	// Don't update if feature is disabled
	if (!bUseChargeOverlay)
	{
		return;
	}

	// Select appropriate material based on polarity
	UMaterialInterface* TargetMaterial = nullptr;

	switch (NewPolarity)
	{
	case 0: // Neutral
		TargetMaterial = NeutralChargeOverlayMaterial;
		break;
	case 1: // Positive
		TargetMaterial = PositiveChargeOverlayMaterial;
		break;
	case 2: // Negative
		TargetMaterial = NegativeChargeOverlayMaterial;
		break;
	default:
		TargetMaterial = NeutralChargeOverlayMaterial;
		break;
	}

	// Apply overlay material to third person mesh
	if (USkeletalMeshComponent* TPMesh = GetMesh())
	{
		TPMesh->SetOverlayMaterial(TargetMaterial);
	}

	// Apply overlay material to first person mesh
	if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
	{
		FPMesh->SetOverlayMaterial(TargetMaterial);
	}
}

void AShooterCharacter::AccumulateFirstPersonPose(float DeltaTime, FVector& Location, FRotator& Rotation)
{
	Super::AccumulateFirstPersonPose(DeltaTime, Location, Rotation);

	USkeletalMeshComponent* FPMesh = GetFirstPersonMesh();
	if (!FPMesh)
	{
		return;
	}

	const bool bIsWallrunning = GetApexMovement() && GetApexMovement()->IsWallRunning();

	// === Per-weapon base pose ===
	// Faded out by crouch/slide (smoothly, via CrouchSlideProgress) and by wallrun (hard switch —
	// acceptable, since the wallrun tilt itself ramps in through ApexMovement's interpolation).
	if (CurrentWeapon)
	{
		// Crouch and wallrun used to REPLACE this pose with mesh offsets of their own, which is what
		// the fade is for. Once those states bend the spine instead they stop touching the mesh, so
		// there is nothing for the neutral pose to make way for and it stays put.
		const float WeaponBaseFactor = bDriveStatePosesFromSpine
			? 1.0f
			: (1.0f - CrouchSlideProgress) * (bIsWallrunning ? 0.0f : 1.0f);
		Location += CurrentWeapon->FirstPersonMeshOffset * WeaponBaseFactor;
		Rotation += CurrentWeapon->FirstPersonMeshTilt * WeaponBaseFactor;
	}

	// === External lowers (melee attack) ===
	// The weapon switch used to add its own offset here, sliding the whole FP mesh down and back
	// up. It is gone: a swap is animated now, by the two weapons involved.
	Location.Z += ExternalMeshZOffset;

	// === Camera follow compensation ===
	// The camera component itself is displaced by shake and by the raised shield (see UpdateADS).
	// Parented to it, the mesh inherits that 1:1; hand back the fraction the designer does not
	// want. GetRelativeLocation is capsule-space, so rotate it into camera space first.
	if (CameraLocationFollowAlpha < 1.0f)
	{
		if (const UCameraComponent* Camera = GetFirstPersonCameraComponent())
		{
			// Minus the crouch offset: that one is not a camera effect the mesh should be shielded
			// from, it IS the view moving, and the hands ride the view.
			const FVector CameraOffsetFromBase = Camera->GetRelativeLocation() - BaseCameraLocation - AppliedCrouchCameraOffset;
			const FVector OffsetInCameraSpace = Camera->GetRelativeRotation().UnrotateVector(CameraOffsetFromBase);
			Location -= OffsetInCameraSpace * (1.0f - CameraLocationFollowAlpha);
		}
	}

	AccumulateADSSightAlignment(Location, Rotation, FPMesh);

	// === Recoil kick and sway ===
	// Deliberately AFTER the alignment. At full ADS the alignment assigns rather than adds, so
	// every layer above is erased; these two are the ones that have to survive aiming, and each
	// already carries its own ADS weighting from inside the recoil component (ADSWeaponFraction
	// for the kick, ADSSwayMultiplier for the sway). Asked for separately, not through the summed
	// getter, so the two can be weighted apart here later without touching the component.
	//
	// Why this does not un-aim the sights: after alignment the sight sits on the camera's forward
	// axis. GetWeaponOffset is the kick-BACK, which runs along that same axis, and Roll turns
	// around it, so neither moves where the sight projects on screen. Pitch and yaw would, and
	// those are exactly what ADSWeaponFraction takes out of the weapon and gives to the camera.
	if (RecoilComponent)
	{
		Location += RecoilComponent->GetWeaponOffset();
		Rotation += RecoilComponent->GetWeaponKickRotation();
		Rotation += RecoilComponent->GetWeaponSwayRotation();
	}
}

void AShooterCharacter::AccumulateADSSightAlignment(FVector& Location, FRotator& Rotation, const USkeletalMeshComponent* FPMesh) const
{
	if (CurrentADSAlpha <= KINDA_SMALL_NUMBER || !CurrentWeapon || !FPMesh)
	{
		return;
	}

	const USceneComponent* Anchor = CurrentWeapon->GetADSCamera();
	if (!Anchor)
	{
		return;
	}

	// Where the sight sits RELATIVE TO THE MESH. This depends only on the animated pose, the
	// socket and the weapon attachment, never on where the mesh itself is standing, so reading it
	// from last frame's world transform introduces no feedback loop. (Chasing the anchor with the
	// CAMERA would be a loop, since the anchor is a descendant of the camera. That is the old
	// design and it is not coming back.)
	FTransform SightRelMesh = Anchor->GetComponentTransform().GetRelativeTransform(FPMesh->GetComponentTransform());
	SightRelMesh.RemoveScaling();

	// Location and Rotation are the mesh's transform in CAMERA space (the camera is the mesh's
	// parent), so "aimed" means the sight lands on the camera origin pointing down camera forward:
	//
	//     SightRelMesh * MeshRelCamera == Identity      =>      MeshRelCamera == SightRelMesh^-1
	//
	// Same fact as the grip socket rule already in CLAUDE.md: a socket is the inverse of the pose
	// you want the thing to end up in.
	const FTransform Current(Rotation, Location);

	// Eye relief. The alignment below puts the sight socket ON the camera origin, which is right
	// for an abstract "aim point" and wrong for every real sight mesh, where that socket marks the
	// optic or the front post: with no offset the weapon ends up a hand's width inside the player's
	// head. Pushing the mesh forward along camera X after the inverse is the same correction the
	// Low Poly Shooter Pack applies per scope as OffsetAiming, and it is applied HERE rather than
	// folded into SightRelMesh so that it stays in camera axes -- after alignment the camera's
	// forward IS the sight line, so X is eye relief no matter how the socket happens to be turned.
	const FVector AimOffset = CurrentWeapon->GetSightAimOffset();

	if (CurrentWeapon->ShouldAlignSightRotation())
	{
		const FRotator SocketCorrection = CurrentWeapon->GetSightRotationOffset();
		if (!SocketCorrection.IsNearlyZero())
		{
			SightRelMesh.ConcatenateRotation(SocketCorrection.Quaternion());
			SightRelMesh.NormalizeRotation();
		}

		FTransform Target = SightRelMesh.Inverse();
		Target.AddToTranslation(AimOffset);

		// Slerp, not a rotator sum. Adding FRotators at alpha 0.5 does not give the halfway
		// rotation, and the error shows up as the weapon swinging into the sights along a bent
		// arc instead of a straight one.
		FQuat Blended = FQuat::Slerp(Current.GetRotation(), Target.GetRotation(), CurrentADSAlpha);
		Blended.Normalize();

		Location = FMath::Lerp(Current.GetTranslation(), Target.GetTranslation(), CurrentADSAlpha);
		Rotation = Blended.Rotator();
	}
	else
	{
		// Position only. This is what ADS did before alignment existed, kept per weapon for the
		// ones whose sight socket ROTATION has not been verified yet: nothing read that rotation
		// until now, so a socket placed by eye for position alone will aim the barrel wrong.
		const FVector SightInCameraSpace = Current.TransformPosition(SightRelMesh.GetTranslation());
		Location -= (SightInCameraSpace - AimOffset) * CurrentADSAlpha;
	}
}

void AShooterCharacter::OnMeleeHit(AActor* HitActor, const FVector& HitLocation, bool bHeadshot, float Damage)
{
	UE_LOG(LogTemp, Warning, TEXT("[MeleeHit] %s hit %s - Damage=%.1f, Headshot=%d"),
		*GetName(),
		HitActor ? *HitActor->GetName() : TEXT("NULL"),
		Damage, bHeadshot);

	bool bKilled = false;
	bool bIsDummyTarget = HitActor && HitActor->Implements<UShooterDummyTarget>();

	// Forward melee hits to the hit marker system
	if (HitMarkerComponent)
	{
		// Try to get remaining health from hit actor
		APawn* HitPawn = Cast<APawn>(HitActor);
		if (HitPawn)
		{
			// For ShooterCharacter targets, check their HP
			AShooterCharacter* HitCharacter = Cast<AShooterCharacter>(HitPawn);
			if (HitCharacter && HitCharacter->CurrentHP <= 0.0f)
			{
				bKilled = true;
			}
		}

		// Check for dummy death via interface
		if (bIsDummyTarget)
		{
			bKilled = IShooterDummyTarget::Execute_IsDummyDead(HitActor);
		}

		// Calculate hit direction
		FVector HitDirection = (HitLocation - GetActorLocation()).GetSafeNormal();

		// Register hit with hit marker component using actual damage dealt
		HitMarkerComponent->RegisterHit(
			HitLocation,
			HitDirection,
			Damage,
			bHeadshot,
			bKilled
		);
	}

	// === Stream style hook (melee path) ===
	if (bKilled)
	{
		if (UStyleComponent* Style = FindComponentByClass<UStyleComponent>())
		{
			EStyleCategory Category = EStyleCategory::MeleeKill;
			if (ActiveAirDashTrailComponent != nullptr)
			{
				Category = EStyleCategory::AirDashKill;
			}
			else if (bHeadshot)
			{
				Category = EStyleCategory::Headshot;
			}

			FStyleAction Action;
			Action.Category = Category;
			Action.WorldLocation = HitActor ? HitActor->GetActorLocation() : HitLocation;
			Action.InstanceMultiplier = 1.0f;
			Style->RegisterAction(Action);
		}
	}

	// Handle charge based on target type
	if (UEMFVelocityModifier* EMFMod = FindComponentByClass<UEMFVelocityModifier>())
	{
		// Check if target implements IShooterDummyTarget for stable charge
		if (bIsDummyTarget)
		{
			bool bGrantsStable = IShooterDummyTarget::Execute_GrantsStableCharge(HitActor);

			if (bGrantsStable)
			{
				float StableAmount = IShooterDummyTarget::Execute_GetStableChargeAmount(HitActor);
				if (StableAmount > 0.0f)
				{
					UE_LOG(LogTemp, Warning, TEXT("[MeleeCharge] Dummy stable charge: +%.2f to %s"),
						StableAmount, *GetName());
					EMFMod->AddPermanentCharge(StableAmount);

					// Trigger first-charge tutorial arrow (Charge Bar)
					if (!FirstChargeTutorialID.IsNone())
					{
						if (UGameInstance* GI = GetGameInstance())
						{
							if (UTutorialSubsystem* TutorialSub = GI->GetSubsystem<UTutorialSubsystem>())
							{
								APlayerController* PC = Cast<APlayerController>(GetController());
								TutorialSub->ShowHUDArrow(FirstChargeTutorialID, FirstChargeArrowData, PC);
							}
						}
					}
				}

				// Add kill bonus if we killed the dummy
				if (bKilled)
				{
					float KillBonus = IShooterDummyTarget::Execute_GetKillChargeBonus(HitActor);
					if (KillBonus > 0.0f)
					{
						UE_LOG(LogTemp, Warning, TEXT("[MeleeCharge] Dummy kill bonus: +%.2f to %s"),
							KillBonus, *GetName());
						EMFMod->AddPermanentCharge(KillBonus);
					}
				}
				return; // Don't add bonus charge for dummy targets
			}
		}

		// Get charge from the enemy's ChargeChangeOnMeleeHit (negated: enemy loses → player gains)
		float ChargeAmount = EMFMod->ChargePerMeleeHit; // fallback
		if (AShooterNPC* HitNPC = Cast<AShooterNPC>(HitActor))
		{
			ChargeAmount = -HitNPC->GetChargeChangeOnMeleeHit();
		}

		// Apply drop kick charge multiplier for successful drop kicks
		if (MeleeAttackComponent && MeleeAttackComponent->IsDropKick())
		{
			ChargeAmount *= MeleeAttackComponent->Settings.DropKickChargeMultiplier;
		}

		float OldCharge = EMFMod->GetCharge();
		EMFMod->AddBonusCharge(ChargeAmount);
		float NewCharge = EMFMod->GetCharge();

		UE_LOG(LogTemp, Warning, TEXT("[MeleeCharge] Hit %s - Charge: %.2f -> %.2f (added %.2f bonus%s)"),
			HitActor ? *HitActor->GetName() : TEXT("NULL"),
			OldCharge, NewCharge, ChargeAmount,
			(MeleeAttackComponent && MeleeAttackComponent->IsDropKick()) ? TEXT(", DropKick x2") : TEXT(""));
	}
}

// ==================== SFX Functions ====================

void AShooterCharacter::PlayFootstepSound()
{
	if (!FootstepSound)
	{
		return;
	}

	const float RandomPitch = FMath::RandRange(FootstepPitchMin, FootstepPitchMax);

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		FootstepSound,
		GetActorLocation(),
		FRotator::ZeroRotator,
		FootstepVolume,
		RandomPitch
	);
}

void AShooterCharacter::PlayCrouchFootstepSound()
{
	if (!CrouchFootstepSound)
	{
		return;
	}

	const float RandomPitch = FMath::RandRange(CrouchFootstepPitchMin, CrouchFootstepPitchMax);

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		CrouchFootstepSound,
		GetActorLocation(),
		FRotator::ZeroRotator,
		CrouchFootstepVolume,
		RandomPitch
	);
}

void AShooterCharacter::PlaySlideStartSound()
{
	if (!SlideStartSound)
	{
		return;
	}

	const float RandomPitch = FMath::RandRange(SlideSoundPitchMin, SlideSoundPitchMax);

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		SlideStartSound,
		GetActorLocation(),
		FRotator::ZeroRotator,
		SlideSoundVolume,
		RandomPitch
	);
}

void AShooterCharacter::PlaySlideEndSound()
{
	if (!SlideEndSound)
	{
		return;
	}

	const float RandomPitch = FMath::RandRange(SlideSoundPitchMin, SlideSoundPitchMax);

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		SlideEndSound,
		GetActorLocation(),
		FRotator::ZeroRotator,
		SlideSoundVolume,
		RandomPitch
	);
}

void AShooterCharacter::StartSlideLoopSound()
{
	if (!SlideLoopSound)
	{
		return;
	}

	// Stop existing loop if any
	StopSlideLoopSound();

	// Create and play looping sound attached to character
	SlideLoopAudioComponent = UGameplayStatics::SpawnSoundAttached(
		SlideLoopSound,
		GetRootComponent(),
		NAME_None,
		FVector::ZeroVector,
		EAttachLocation::KeepRelativeOffset,
		false,
		SlideSoundVolume,
		FMath::RandRange(SlideSoundPitchMin, SlideSoundPitchMax),
		0.0f,
		nullptr,
		nullptr,
		true
	);
}

void AShooterCharacter::StopSlideLoopSound()
{
	if (SlideLoopAudioComponent && SlideLoopAudioComponent->IsPlaying())
	{
		SlideLoopAudioComponent->Stop();
		SlideLoopAudioComponent = nullptr;
	}
}

void AShooterCharacter::PlayWallRunStartSound()
{
	if (!WallRunStartSound)
	{
		return;
	}

	const float RandomPitch = FMath::RandRange(WallRunSoundPitchMin, WallRunSoundPitchMax);

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		WallRunStartSound,
		GetActorLocation(),
		FRotator::ZeroRotator,
		WallRunSoundVolume,
		RandomPitch
	);
}

void AShooterCharacter::PlayWallRunEndSound()
{
	if (!WallRunEndSound)
	{
		return;
	}

	const float RandomPitch = FMath::RandRange(WallRunSoundPitchMin, WallRunSoundPitchMax);

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		WallRunEndSound,
		GetActorLocation(),
		FRotator::ZeroRotator,
		WallRunSoundVolume,
		RandomPitch
	);
}

void AShooterCharacter::StartWallRunLoopSound()
{
	if (!WallRunLoopSound)
	{
		return;
	}

	// Stop existing loop if any
	StopWallRunLoopSound();

	// Create and play looping sound attached to character
	WallRunLoopAudioComponent = UGameplayStatics::SpawnSoundAttached(
		WallRunLoopSound,
		GetRootComponent(),
		NAME_None,
		FVector::ZeroVector,
		EAttachLocation::KeepRelativeOffset,
		false,
		WallRunSoundVolume,
		FMath::RandRange(WallRunSoundPitchMin, WallRunSoundPitchMax),
		0.0f,
		nullptr,
		nullptr,
		true
	);
}

void AShooterCharacter::StopWallRunLoopSound()
{
	if (WallRunLoopAudioComponent && WallRunLoopAudioComponent->IsPlaying())
	{
		WallRunLoopAudioComponent->Stop();
		WallRunLoopAudioComponent = nullptr;
	}
}

void AShooterCharacter::PlayJumpSound(bool bIsDoubleJump)
{
	USoundBase* SoundToPlay = bIsDoubleJump ? DoubleJumpSound : JumpSound;

	if (!SoundToPlay)
	{
		return;
	}

	const float RandomPitch = FMath::RandRange(JumpSoundPitchMin, JumpSoundPitchMax);

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		SoundToPlay,
		GetActorLocation(),
		FRotator::ZeroRotator,
		JumpSoundVolume,
		RandomPitch
	);
}

void AShooterCharacter::PlayLandSound(float FallSpeed)
{
	if (!LandSound)
	{
		return;
	}

	// Only play if fall speed exceeds minimum threshold
	if (FallSpeed < LandSoundMinFallSpeed)
	{
		return;
	}

	const float RandomPitch = FMath::RandRange(LandSoundPitchMin, LandSoundPitchMax);

	// Scale volume based on fall speed (louder for harder landings)
	const float SpeedRatio = FMath::Clamp(FallSpeed / 1000.0f, 0.5f, 1.5f);
	const float AdjustedVolume = LandSoundVolume * SpeedRatio;

	UGameplayStatics::SpawnSoundAtLocation(
		this,
		LandSound,
		GetActorLocation(),
		FRotator::ZeroRotator,
		AdjustedVolume,
		RandomPitch
	);
}

// ==================== SFX Delegate Handlers ====================

void AShooterCharacter::OnSlideStarted_SFX()
{
	PlaySlideStartSound();
	StartSlideLoopSound();
}

void AShooterCharacter::OnSlideEnded_SFX()
{
	StopSlideLoopSound();
	PlaySlideEndSound();
}

void AShooterCharacter::OnWallRunStarted_SFX(EWallSide Side)
{
	PlayWallRunStartSound();
	StartWallRunLoopSound();
}

void AShooterCharacter::OnWallRunEnded_SFX()
{
	StopWallRunLoopSound();
	PlayWallRunEndSound();
}

void AShooterCharacter::OnLanded_SFX(const FHitResult& Hit)
{
	if (UApexMovementComponent* Apex = GetApexMovement())
	{
		PlayLandSound(Apex->LastFallVelocity);
	}
}

void AShooterCharacter::BeginRunLaunch(const FVector& LaunchVelocity)
{
	bRunLaunchInProgress = true;

	if (UApexMovementComponent* Apex = GetApexMovement())
	{
		Apex->SetRunLaunchActive(true);
		SavedAirControl = Apex->AirControl;
		Apex->AirControl = 0.f;   // pure-physics arc: no mid-air steering
	}

	// Override both XY and Z so the toss is exactly the authored velocity (deterministic landing).
	LaunchCharacter(LaunchVelocity, true, true);

	UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] BeginRunLaunch vel=%s"), *LaunchVelocity.ToString());
}

void AShooterCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	if (!bRunLaunchInProgress)
	{
		return;
	}
	bRunLaunchInProgress = false;

	if (UApexMovementComponent* Apex = GetApexMovement())
	{
		Apex->SetRunLaunchActive(false);
		Apex->AirControl = SavedAirControl;
	}

	// Hand the player their starting weapon, drawn with the smooth raise.
	EquipStartingWeaponAnimated();

	UE_LOG(LogTemp, Log, TEXT("[RUN_DEBUG] Run-launch landed -> grant starting weapon"));
}

void AShooterCharacter::OnRep_ClassDefinition()
{
	ApplyClassDefinition();
}

EClassItemVerb AShooterCharacter::GetItemVerb() const
{
	return ClassDefinition ? ClassDefinition->ItemVerb : EClassItemVerb::None;
}

void AShooterCharacter::ApplyClassDefinition()
{
	if (!ClassDefinition)
	{
		// Classless is a supported state, not an error: every map and test that predates classes
		// still spawns BP_ShooterCharacter directly.
		return;
	}

	// The starting weapon is only supplied, not granted. Handing it over this way means the run-start
	// path, the animated draw and everything built around them keep working untouched.
	if (ClassDefinition->StartingWeaponClass)
	{
		StartingWeaponClass = ClassDefinition->StartingWeaponClass;
	}

	// Abilities are inventory, and inventory belongs to the server — UAbilityComponent::AddAbility
	// refuses a client outright. The client learns its loadout when the slots replicate down, which
	// is the same road a picked-up ability travels.
	if (HasAuthority() && AbilityComponent)
	{
		// The passive goes to its own channel, not into a slot. Through AddAbility it became an
		// ordinary ability: selectable, activatable, and eating one of three inventory slots the
		// player never spent on it.
		if (ClassDefinition->PassiveAbility)
		{
			AbilityComponent->GrantPassive(ClassDefinition->PassiveAbility);
		}
		if (ClassDefinition->ActiveAbility)
		{
			AbilityComponent->AddAbility(ClassDefinition->ActiveAbility);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[COOP_DEBUG] %s applied class '%s' role=%d verb=%d"),
		*GetName(), *GetNameSafe(ClassDefinition), (int32)GetLocalRole(), (int32)ClassDefinition->ItemVerb);
}

void AShooterCharacter::EquipStartingWeaponAnimated()
{
	if (!StartingWeaponClass)
	{
		return;
	}

	// AddWeaponClassAnimated equips instantly when the player is unarmed: there is nothing to put
	// away first. The half that is still worth playing is the draw, so the run does not open with a
	// gun that was simply always there.
	AShooterWeapon* Equipped = AddWeaponClassAnimated(StartingWeaponClass);
	if (Equipped && CurrentWeapon == Equipped && !IsWeaponSwitchInProgress())
	{
		const float DrawLength = Equipped->GetDrawLength();
		if (DrawLength > 0.0f)
		{
			PlayWeaponSwitchMontage(Equipped->GetDrawMontage(), Equipped->GetDrawMontageTP(),
				Equipped->GetDrawPlayRate());

			WeaponSwitchPhase = EWeaponSwitchPhase::Drawing;
			GetWorldTimerManager().SetTimer(WeaponSwitchTimer, this, &AShooterCharacter::FinishWeaponDraw,
				DrawLength, false);
		}

		PlayWeaponSwitchSound();
	}
}

// ==================== Grapple line ====================

void AShooterCharacter::SetGrappleLine(bool bOn, FVector Anchor, UAbilityDefinition_Grapple* Def, int32 Level)
{
	// This machine first, then the one that predicts this character's movement. Both, always: a
	// server that only set its own copy would be corrected away by the client's next move, and a
	// client left out of it would be corrected INTO a swing it never predicted. @see
	// Client_ApplyKnockback, which is the same shape for the same reason.
	ApplyGrappleLineLocally(bOn, Anchor, Def, Level);

	if (!IsLocallyControlled())
	{
		Client_SetGrappleLine(bOn, Anchor, Def, Level);
	}
}

void AShooterCharacter::Client_SetGrappleLine_Implementation(bool bOn, FVector Anchor,
	UAbilityDefinition_Grapple* Def, int32 Level)
{
	ApplyGrappleLineLocally(bOn, Anchor, Def, Level);
}

void AShooterCharacter::ApplyGrappleLineLocally(bool bOn, FVector Anchor, UAbilityDefinition_Grapple* Def,
	int32 Level)
{
	UApexMovementComponent* Apex = GetApexMovement();
	if (!Apex)
	{
		return;
	}

	// The tuning goes in before the intent: StartGrapple runs on the next simulated move and the
	// numbers have to be the ones this line was thrown with, not the ones the last one used.
	if (bOn && Def)
	{
		const FGrappleLevelStats Stats = Def->GetStatsAtLevel(Level);

		// Every field, copied by hand, and it is worth saying why nobody should "simplify" this into
		// a memcpy or a shared struct: the authored side is a UPROPERTY struct a designer edits and
		// the movement side is a plain mirror that lives inside the simulation. They are allowed to
		// drift apart, and the compiler catches it here when they do. This list has already lost a
		// field once -- GroundLaunchSpeed was authored on the asset and never copied, so the asset's
		// value did nothing at all and the component's default silently stood in for it.
		FGrappleMotionParams Params;
		Params.PullAcceleration             = Stats.PullAcceleration;
		Params.SpeedRampMin                 = Stats.SpeedRampMin;
		Params.SpeedRampMax                 = Stats.SpeedRampMax;
		Params.SpeedRampTime                = Stats.SpeedRampTime;
		Params.PullDelay                    = Stats.PullDelay;
		Params.LateralDeceleration          = Stats.LateralDeceleration;
		Params.bDontFightGravity            = Stats.bDontFightGravity;
		Params.Lift                         = Stats.Lift;
		Params.MaxSpeed                     = Stats.MaxSpeed;
		Params.LetGravityHelpCosAngle       = Stats.LetGravityHelpCosAngle;
		Params.GravityPushUnderContribution = Stats.GravityPushUnderContribution;
		Params.InitialSlowFracHorizontal    = Stats.InitialSlowFracHorizontal;
		Params.InitialSlowFracVertical      = Stats.InitialSlowFracVertical;
		Params.InitialImpulse               = Stats.InitialImpulse;
		Params.InitialImpulseOffGround      = Stats.InitialImpulseOffGround;
		Params.InitialSpeedMin              = Stats.InitialSpeedMin;
		Params.SwingAirAcceleration         = Stats.SwingAirAcceleration;
		Params.SwingWishSpeed               = Stats.SwingWishSpeed;
		Params.GroundFrictionScale          = Stats.GroundFrictionScale;
		Params.GroundBrakingScale           = Stats.GroundBrakingScale;
		Params.DetachLowSpeed               = Stats.DetachLowSpeed;
		Params.DetachLowSpeedTime           = Stats.DetachLowSpeedTime;
		Params.ArrivalRadius                = Stats.ArrivalRadius;
		Params.MaxDuration                  = Stats.MaxDuration;
		Apex->SetGrappleTuning(Params);

		if (Stats.MaxDuration > 0.0f)
		{
			// A line that outlives the ability is a player stuck in the air, so the visual is given
			// the same ceiling the swing has.
			GrappleVisualMaxEndTime = GetWorld() ? GetWorld()->GetTimeSeconds() + Stats.MaxDuration + 0.5f : -1.0f;
		}
	}

	Apex->SetGrappleIntent(bOn, Anchor);

	// Both hands go on the line, so the weapon goes away, and comes back when the line lets go.
	//
	// Here rather than in Multicast_PlayGrappleThrow, and that is the coop-shaped decision in this
	// change: this function runs on the authority and on the machine that predicts this character,
	// which is exactly the pair every other weapon action in this class uses. A watching client is
	// covered without doing anything itself -- the third-person montage goes out over
	// PlayThirdPersonMontageEverywhere, and the weapon actor's hidden flag replicates down from the
	// server. Running it on every machine instead would have each proxy play the body animation
	// twice, once locally and once from the multicast.
	if (Def && Def->bStowWeapon)
	{
		if (bOn)
		{
			StowWeaponForGrapple(Def->WeaponStowSpeedMultiplier);
		}
		else
		{
			UnstowWeaponAfterGrapple(Def->WeaponStowSpeedMultiplier);
		}
	}

	if (bOn && Def && Def->AttachSound && GetWorld())
	{
		UGameplayStatics::PlaySoundAtLocation(this, Def->AttachSound, Anchor);
	}
	else if (!bOn && Def && Def->DetachSound && GetWorld())
	{
		UGameplayStatics::PlaySoundAtLocation(this, Def->DetachSound, GetActorLocation());
	}
}

void AShooterCharacter::Multicast_PlayGrappleThrow_Implementation(FVector Anchor, float TravelTime,
	UAbilityDefinition_Grapple* Def)
{
	if (!GetWorld())
	{
		return;
	}

	GrappleVisualDefinition = Def;
	EnsureGrappleCable(Def);

	GrappleVisualAnchor    = Anchor;
	GrappleVisualEnd       = GetGrappleHandLocation();
	GrappleThrowStartTime  = GetWorld()->GetTimeSeconds();
	GrappleThrowTravelTime = FMath::Max(0.0f, TravelTime);
	bGrappleVisualActive   = true;

	// Put the rope on the straight line between the two ends BEFORE anybody looks at it.
	//
	// UCableComponent is a Verlet simulation and it seeds its particles once, in OnRegister, along
	// whatever EndLocation happened to hold at that moment -- which for a freshly built component is
	// the default one metre along its own +X. Left alone, the rope therefore starts as a short stub
	// pointing wherever the capsule faces and then SWINGS across to the anchor over the next second,
	// which is exactly the "goes backward and right, then forward" arc this used to draw. Re-seeding
	// on the throw is the only way to place those particles: there is no public API for it, and
	// OnRegister is what does the seeding.
	ReseedGrappleCable(GrappleVisualEnd);

	if (Def && Def->AttachVFX && TravelTime <= 0.0f)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), Def->AttachVFX, Anchor);
	}
}

FVector AShooterCharacter::GetGrappleHandLocation() const
{
	const UAbilityDefinition_Grapple* Def = GrappleVisualDefinition.Get();
	const bool bFromChest = !Def || Def->LineOrigin == EGrappleLineOrigin::Chest;

	// NOT the first-person mesh, however tempting. That mesh is drawn through the first-person path,
	// which applies its own field-of-view correction and scale, so where it APPEARS and where its
	// bones actually are in the world are two different places -- by metres. A cable is ordinary
	// world geometry and would be drawn between the real transforms, which is how the line ended up
	// pointing at the sky. The near end therefore comes from the camera for the player holding it.
	if (IsLocallyControlled())
	{
		const FVector Offset = bFromChest ? GrappleFirstPersonChestOffset : GrappleFirstPersonMuzzleOffset;

		if (const UCameraComponent* Cam = GetFirstPersonCameraComponent())
		{
			// Position from the component, ROTATION FROM THE CONTROLLER, and they are two different
			// sources on purpose.
			//
			// The camera has bUsePawnControlRotation set (PolarityCharacter.cpp), and that flag is
			// not applied to the component's transform when the player looks around: it is applied
			// inside UCameraComponent::GetCameraView, which the camera manager calls at the very end
			// of the world tick, after every actor has ticked. So anything reading the component's
			// ROTATION during a tick is reading last frame's aim -- the line lagged the crosshair by
			// a frame, which on a swing is most of a swing. GetViewRotation is the control rotation
			// itself, updated in the player controller's tick before any of this runs, so it is the
			// aim this frame will actually be drawn with.
			//
			// The LOCATION is the opposite way round: the component is a child of the capsule and
			// the movement component has already moved it this frame (it ticks before its owner),
			// so the component is current and the controller knows nothing about it.
			const FTransform CamTransform(GetViewRotation(), Cam->GetComponentLocation());
			return CamTransform.TransformPosition(Offset);
		}
		return GetActorLocation();
	}

	// Everybody else is looking at this character's body, where the bones really are where they look.
	const FName Socket = bFromChest
		? (Def ? Def->ChestSocket : FName("spine_03"))
		: (Def ? Def->HandSocket  : FName("hand_l"));

	if (const USkeletalMeshComponent* BodyMesh = GetMesh())
	{
		if (BodyMesh->DoesSocketExist(Socket))
		{
			return BodyMesh->GetSocketLocation(Socket);
		}
	}

	return GetActorLocation();
}

void AShooterCharacter::EnsureGrappleCable(UAbilityDefinition_Grapple* Def)
{
	const int32 WantedSegments = Def ? FMath::Max(1, Def->CableSegments) : 32;

	// NumSegments MUST be right before the component registers, and changing it afterwards is a
	// crash, not a cosmetic mistake. UCableComponent::OnRegister allocates its particle array as
	// NumSegments + 1 and never revisits it, so a component registered on the default 10 and then
	// told it has 32 segments indexes 32 into an array of 11 the moment it simulates:
	//   Assertion failed: (Index >= 0) & (Index < ArrayNum) ... 32 into an array of size 11
	// So: set it first on creation, and re-register when it changes.
	if (GrappleCable && GrappleCable->NumSegments != WantedSegments)
	{
		GrappleCable->UnregisterComponent();
		GrappleCable->NumSegments = WantedSegments;
		GrappleCable->RegisterComponent();
	}


	if (!GrappleCable)
	{
		GrappleCable = NewObject<UCableComponent>(this, UCableComponent::StaticClass(), TEXT("GrappleCable"));

		// Attached to the capsule and then placed in world space by hand every frame. One cable, seen
		// by everybody including its owner: it is world geometry, so there is nothing about it that
		// differs between the screen of the player holding it and the screen of a teammate.
		GrappleCable->SetupAttachment(GetRootComponent());

		// ...and then told to ignore that parent completely, which is the fix for the line being
		// drawn somewhere other than where it is aimed.
		//
		// UCableComponent pins its far end at EndLocation expressed in the COMPONENT's own space
		// (UCableComponent::GetEndPositions), and its near end at the component's location. Both are
		// written here once per frame from the character's tick -- and then the capsule keeps moving
		// for the rest of the frame: the movement component integrates the move, and the capsule
		// YAWS with the mouse because bUseControllerRotationYaw is on. A child component inherits
		// all of it, so by the time the cable simulates, its idea of "the anchor" has been rotated
		// around the player by however far they turned this frame and shifted by however far they
		// flew. On a swing that is constant mouse movement at high speed, which is exactly the
		// picture: a rope that points somewhere near the anchor and swings about, while a debug line
		// drawn in absolute world coordinates from the same two numbers sits exactly right.
		//
		// Absolute transform makes the component's world transform the one that was written and
		// nothing else, so the two ends stay where they were put. The rotation is left at identity
		// for good measure, so EndLocation is a plain world-space offset.
		GrappleCable->SetUsingAbsoluteLocation(true);
		GrappleCable->SetUsingAbsoluteRotation(true);
		GrappleCable->SetUsingAbsoluteScale(true);
		GrappleCable->SetWorldRotation(FRotator::ZeroRotator);
		GrappleCable->SetWorldScale3D(FVector::OneVector);

		// And the far end is told, explicitly, that EndLocation is measured against THIS component.
		// Without this line it is measured against the CAPSULE, which is not a thing anybody would
		// guess and is why the rope pointed somewhere unrelated to where it was aimed.
		//
		// GetEndPositions resolves the end through AttachEndTo, an FComponentReference. An empty one
		// does not mean "no attachment": FBaseComponentReference::ExtractComponent falls through to
		//   Result = SearchActor->GetRootComponent();
		// so the end silently resolves to the character's capsule, and EndLocation gets multiplied
		// by the capsule's transform -- rotated by the player's yaw and offset by the player's
		// position. Every version of this code so far wrote EndLocation in some other space (world,
		// or the cable's own) and the engine read it in capsule space, so the far end was thrown off
		// by the whole of the player's rotation. Pointing the reference at the cable makes the
		// obvious reading the true one, and since the cable holds an absolute identity-rotated
		// transform, EndLocation is then a plain world-space offset from the near end.
		GrappleCable->SetAttachEndToComponent(GrappleCable, NAME_None);

		// Everything OnRegister reads goes in BEFORE the register call. @see the note above.
		GrappleCable->NumSegments = WantedSegments;

		// The far end is PINNED. With bAttachEnd false the engine treats EndLocation as a starting
		// position for a rope that then dangles freely, which is a rope hanging off the player rather
		// than a line stretched to a wall.
		GrappleCable->bAttachEnd = true;
		GrappleCable->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		GrappleCable->RegisterComponent();
		GrappleCable->SetHiddenInGame(true);

		// The rope simulates AFTER the character has told it where its ends are. Said out loud,
		// because the engine does not arrange it: UActorComponent::SetupActorComponentTickFunction
		// just registers the tick function, and a component's tick has no prerequisite on its own
		// owner's tick. Which of the two runs first inside TG_PrePhysics is therefore whatever the
		// tick manager happens to do, and if it is the cable, the rope is pinned to the ends of the
		// PREVIOUS frame -- half a metre behind at swinging speed, and the sort of bug that reads as
		// "sometimes it lags" and survives several sessions.
		GrappleCable->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);
	}

	// The two engine defaults that make a grapple line look like a washing line, both set here.
	//
	// SolverIterations is 1 out of the box. UCableComponent is a Verlet chain whose length constraint
	// is relaxed ONE LINK PER ITERATION, so with a single pass over 32 segments the rope can never be
	// pulled straight no matter what CableLength says -- which is exactly why changing the slack made
	// no visible difference whatsoever. 16 is the property's own maximum.
	//
	// CableGravityScale is 1 out of the box, so the rope has its own gravity, hangs in a catenary
	// between its two ends and drops through the floor. A grapple line is under tension; it has no
	// business sagging at all. Zero here, and the sag becomes something the asset asks for rather
	// than something it has to fight.
	GrappleCable->SolverIterations = 16;
	GrappleCable->bEnableStiffness = false;
	GrappleCable->CableForce = FVector::ZeroVector;

	// CableSlack now drives the rope's OWN GRAVITY rather than its length, and that is the only way
	// the setting has ever been able to do anything: length could not straighten the rope while the
	// solver ran one iteration, and gravity is what was bending it. 0 is a dead straight taut line,
	// 0.5 hangs. Worth renaming to CableSagGravity next time the header is being touched anyway.
	GrappleCable->CableGravityScale = Def ? FMath::Clamp(Def->CableSlack, 0.0f, 1.0f) : 0.0f;

	// A player on a line moves tens of centimetres per frame. Left alone the particles lag behind and
	// the rope trails like a ribbon; resetting them on a big jump snaps it back to straight.
	GrappleCable->bResetAfterTeleport = true;

	// ...and "a big jump" has to mean a swing, not a teleport, which is the second half of the line
	// being drawn somewhere it is not aimed.
	//
	// UCableComponent is a Verlet chain of free particles between two pinned ends. Move those ends
	// quickly and the middle simply lags: the rope bows out behind the player and the far end is the
	// only part of it that touches the anchor, so it reads as a rope pointing somewhere else even
	// when both endpoints are exactly right. The one thing that puts every particle back on the
	// straight line is DoTeleportCorrections, and it only runs when an end moves further in a frame
	// than TeleportDistanceThreshold -- which ships at 500, roughly ten times further than anybody
	// moves in a frame, so it never ran at all.
	//
	// Tying the threshold to the authored slack makes it a single knob: no slack asks for a rope
	// re-laid straight on almost any movement, and slack buys the distance a rope may lag before it
	// is snapped back. At the default 0.02 that is about 20cm per frame, so the fast half of a swing
	// draws dead straight and a player hanging nearly still gets their sag.
	GrappleCable->TeleportDistanceThreshold = Def
		? FMath::Lerp(1.0f, 500.0f, FMath::Clamp(Def->CableSlack, 0.0f, 0.5f) / 0.5f)
		: 1.0f;

	// Width and material are read per frame by the proxy, so they are safe to change at any time.
	if (Def)
	{
		GrappleCable->CableWidth = Def->CableWidth;
		if (Def->CableMaterial)
		{
			GrappleCable->SetMaterial(0, Def->CableMaterial);
		}
	}
}

void AShooterCharacter::ReseedGrappleCable(const FVector& WorldEnd)
{
	if (!GrappleCable)
	{
		return;
	}

	// Position and both ends first, then re-register: OnRegister lays the particles evenly along the
	// line from the component to EndLocation, so it has to be told the truth before it runs.
	const FVector Start = GetGrappleHandLocation();
	GrappleCable->SetWorldLocation(Start);
	GrappleCable->EndLocation = WorldEnd - Start;

	// Taut from the very first frame: OnRegister lays the particles out along this line, and a length
	// longer than the span would have them sag before anybody has even seen the rope.
	GrappleCable->CableLength = FVector::Dist(Start, WorldEnd);

	GrappleCable->UnregisterComponent();
	GrappleCable->RegisterComponent();
}

void AShooterCharacter::UpdateGrappleVisual(float DeltaTime)
{
	if (!bGrappleVisualActive || !GetWorld() || !GrappleCable)
	{
		return;
	}

	const UApexMovementComponent* Apex = GetApexMovement();
	const bool bAttached = Apex && Apex->IsGrappling();
	const float Now = GetWorld()->GetTimeSeconds();
	const float SinceThrow = Now - GrappleThrowStartTime;
	const FVector Start = GetGrappleHandLocation();

	// While the hook is in flight the far end travels; once it has bitten, the far end is wherever
	// the movement component says the line is anchored, which on a machine that is only watching is
	// the replicated copy.
	const bool bInFlight = SinceThrow < GrappleThrowTravelTime;
	if (bInFlight)
	{
		// The hook is a thrown object, so it closes on the anchor at its own speed and nothing else
		// decides where it is. This used to interpolate by TIME between the CURRENT hand position and
		// the anchor, which is a different thing entirely: the near end moves while the hook is in
		// the air, so the "hook" was dragged around by the player's own motion and never travelled at
		// the speed the asset asks for.
		//
		// Measured backwards from the anchor, which is the only fixed point in the picture. The
		// remaining gap can only shrink, so the drawn length can only grow while the hook is airborne
		// and stops the instant it arrives -- by construction, not by hoping the timer agrees.
		const float SpanNow = FVector::Dist(Start, GrappleVisualAnchor);
		const float HookSpeed = (GrappleThrowTravelTime > KINDA_SMALL_NUMBER)
			? SpanNow / GrappleThrowTravelTime
			: 0.0f;
		const float RemainingToAnchor = FMath::Clamp(SpanNow - HookSpeed * SinceThrow, 0.0f, SpanNow);

		GrappleVisualEnd = GrappleVisualAnchor + (Start - GrappleVisualAnchor).GetSafeNormal() * RemainingToAnchor;
	}
	else
	{
		GrappleVisualEnd = bAttached ? Apex->GetGrappleAnchor() : GrappleVisualAnchor;

		// The throw has landed and nothing is hanging off it. A short grace rather than an immediate
		// cut, because the attach is decided on the server and reaches a watching machine a moment
		// after the flight ends; without it every line would blink out and back in.
		static constexpr float AttachGraceSeconds = 0.4f;
		const bool bGraceExpired = SinceThrow > GrappleThrowTravelTime + AttachGraceSeconds;
		const bool bOverstayed = GrappleVisualMaxEndTime > 0.0f && Now > GrappleVisualMaxEndTime;

		if ((!bAttached && bGraceExpired) || bOverstayed)
		{
			bGrappleVisualActive = false;
			GrappleVisualMaxEndTime = -1.0f;
			GrappleCable->SetHiddenInGame(true);

			// Failsafe for the weapon, not for the rope. The stow is driven by the ability telling
			// this character the line dropped, and that message is the one thing here that can fail
			// to arrive: an ability cancelled by death, a handler torn down mid-flight, a client
			// that never hears the release. The rope has already given up by this point, so if the
			// gun is still away it is away for no reason, and a player left permanently unarmed is a
			// far worse bug than a rope drawn for an extra frame.
			if (bWeaponStowedForGrapple)
			{
				const UAbilityDefinition_Grapple* Def = GrappleVisualDefinition.Get();
				UnstowWeaponAfterGrapple(Def ? Def->WeaponStowSpeedMultiplier : 1.0f);
			}
			return;
		}
	}

	// Both ends written in world space every frame. The near end is MOVED rather than left to an
	// attachment: the point it should leave from is the camera for the owner and a bone for everyone
	// else, and those are not the same component.
	//
	// The component holds an absolute transform with no rotation (@see EnsureGrappleCable), so
	// EndLocation is simply the world-space gap between the two ends and nothing the capsule does
	// for the rest of the frame can turn it into something else.
	GrappleCable->SetWorldLocation(Start);
	GrappleCable->EndLocation = GrappleVisualEnd - Start;

	// Exactly the gap between the two ends, every frame. Not a fraction of it, not a multiple: the
	// line is as long as the distance it has to cover, so it shortens as the player is reeled in and
	// can never be longer than the span it is drawn across.
	//
	// The sag is not this number's job and never was -- it is the rope's own gravity, set in
	// EnsureGrappleCable. Trying to fight a hanging rope by trimming its rest length is what the
	// earlier version did, and it could not work: at one solver iteration the length constraint
	// reaches one link per pass and never propagates along a 32-segment chain at all.
	const float Span = FVector::Dist(Start, GrappleVisualEnd);
	GrappleCable->CableLength = Span;
	GrappleCable->SetHiddenInGame(false);

	if (CVarGrappleCableDebug.GetValueOnAnyThread() > 0)
	{
		// The length and the gap are printed as two separate numbers on purpose: they are set from
		// the same expression, so if they ever disagree the assignment is not the thing running.
		UE_LOG(LogTemp, Warning,
			TEXT("[GRAPPLE_DEBUG] %s gap=%.0f cableLen=%.0f attached=%d t=%.2f/%.2f"),
			bInFlight ? TEXT("FLY ") : TEXT("HELD"),
			Span, GrappleCable->CableLength, bAttached ? 1 : 0,
			SinceThrow, GrappleThrowTravelTime);

		DrawDebugLine(GetWorld(), Start, GrappleVisualEnd, FColor::Green, false, 0.0f, 0, 1.0f);
		DrawDebugSphere(GetWorld(), GrappleVisualEnd, 10.0f, 8, FColor::Red, false, 0.0f, 0, 1.0f);

		// Where the rope ACTUALLY is, straight out of its own particles, drawn in blue on top of the
		// green line we asked for. The two used to disagree and nothing on screen said so: the debug
		// line was drawn from the same two numbers the cable was handed, so it could only ever agree
		// with the intent and never with the result. Anything the cable does to those numbers on the
		// way -- a space nobody expected, a frame of lag, a simulation that has not caught up --
		// shows up here as blue leaving green.
		TArray<FVector> Particles;
		GrappleCable->GetCableParticleLocations(Particles);
		for (int32 i = 0; i + 1 < Particles.Num(); ++i)
		{
			DrawDebugLine(GetWorld(), Particles[i], Particles[i + 1], FColor::Blue, false, 0.0f, 0, 1.0f);
		}
	}
}

void AShooterCharacter::BindMovementSFXDelegates()
{
	if (UApexMovementComponent* Apex = GetApexMovement())
	{
		Apex->OnSlideStarted.AddDynamic(this, &AShooterCharacter::OnSlideStarted_SFX);
		Apex->OnSlideEnded.AddDynamic(this, &AShooterCharacter::OnSlideEnded_SFX);
		Apex->OnWallrunStarted.AddDynamic(this, &AShooterCharacter::OnWallRunStarted_SFX);
		Apex->OnWallrunEnded.AddDynamic(this, &AShooterCharacter::OnWallRunEnded_SFX);
		Apex->OnLanded_Movement.AddDynamic(this, &AShooterCharacter::OnLanded_SFX);

		// New movement event delegates
		Apex->OnJumpPerformed.AddDynamic(this, &AShooterCharacter::OnJumpPerformed_Handler);
		Apex->OnMantleStarted.AddDynamic(this, &AShooterCharacter::OnMantleStarted_Handler);
		Apex->OnAirDashStarted.AddDynamic(this, &AShooterCharacter::OnAirDashStarted_Handler);
		Apex->OnAirDashEnded.AddDynamic(this, &AShooterCharacter::OnAirDashEnded_Handler);
	}
}

void AShooterCharacter::UnbindMovementSFXDelegates()
{
	if (UApexMovementComponent* Apex = GetApexMovement())
	{
		Apex->OnSlideStarted.RemoveDynamic(this, &AShooterCharacter::OnSlideStarted_SFX);
		Apex->OnSlideEnded.RemoveDynamic(this, &AShooterCharacter::OnSlideEnded_SFX);
		Apex->OnWallrunStarted.RemoveDynamic(this, &AShooterCharacter::OnWallRunStarted_SFX);
		Apex->OnWallrunEnded.RemoveDynamic(this, &AShooterCharacter::OnWallRunEnded_SFX);
		Apex->OnLanded_Movement.RemoveDynamic(this, &AShooterCharacter::OnLanded_SFX);

		// New movement event delegates
		Apex->OnJumpPerformed.RemoveDynamic(this, &AShooterCharacter::OnJumpPerformed_Handler);
		Apex->OnMantleStarted.RemoveDynamic(this, &AShooterCharacter::OnMantleStarted_Handler);
		Apex->OnAirDashStarted.RemoveDynamic(this, &AShooterCharacter::OnAirDashStarted_Handler);
		Apex->OnAirDashEnded.RemoveDynamic(this, &AShooterCharacter::OnAirDashEnded_Handler);
	}
}

void AShooterCharacter::AttachWeaponMeshes(AShooterWeapon* Weapon)
{
	// SnapToTarget for location+rotation (so socket aligns the mesh), but KeepRelative for SCALE
	// so the weapon's blueprint-set Scale is preserved instead of being reset to (1,1,1) by the
	// socket's scale (the single-arg ctor would have applied SnapToTarget to scale too).
	const FAttachmentTransformRules AttachmentRule(
		EAttachmentRule::SnapToTarget,    // Location
		EAttachmentRule::SnapToTarget,    // Rotation
		EAttachmentRule::KeepRelative,    // Scale
		false);

	// attach the weapon actor
	Weapon->AttachToActor(this, AttachmentRule);

	// attach the weapon meshes
	Weapon->GetFirstPersonMesh()->AttachToComponent(GetFirstPersonMesh(), AttachmentRule, FirstPersonWeaponSocket);
	Weapon->GetThirdPersonMesh()->AttachToComponent(GetMesh(), AttachmentRule, ThirdPersonWeaponSocket);

	// If the weapon mesh has an OptionalGrip socket, shift the mesh's relative transform so
	// that this socket lands exactly at the origin of the parent attach socket (the hand).
	//
	// Derivation. Let P be the hand-socket world transform, M the mesh's relative transform
	// (what we're setting), and S the OptionalGrip transform in component space. The world
	// position of OptionalGrip is:
	//     O = P * M * S
	// We want O.Location == P.Location and O.Rotation == P.Rotation, i.e. (M * S) acts as
	// identity on rotation and position. Expanding the FTransform composition rule
	//     (M * S).Location = M.Rotation.Rotate(S.Location * M.Scale) + M.Location
	//     (M * S).Rotation = M.Rotation * S.Rotation
	// gives:
	//     M.Rotation = S.Rotation.Inverse()
	//     M.Location = -M.Rotation.Rotate(S.Location * M.Scale)
	//
	// The previous implementation set
	//     M.Location = -S.Location
	//     M.Rotation = S.Rotation.Inverse()
	// which is only correct when (a) S.Rotation is identity AND (b) M.Scale is (1,1,1).
	// With any non-identity socket rotation OR a BP-set mesh scale ≠ 1 (very common for
	// FP weapon meshes — they're usually scaled down to look right on camera), the grip
	// lands off by `S.Location * (1 - M.Scale)` plus a rotation-induced error, so the whole
	// mesh hangs with a visible offset from the hand and any BP-aligned child (sight,
	// suppressor) appears shifted relative to where it sits in the BP preview.
	//
	// Children attached to sockets on the weapon mesh are intentionally NOT compensated:
	// they ride the parent's relative shift as Unreal's attachment system expects, so their
	// socket-relative offsets stay intact and bone animation drives them correctly. (The
	// earlier `SetWorldTransform` restore overwrote those offsets, which is what made
	// sights/suppressors/lasers drift off their sockets the moment the weapon animated.)
	// The maths and the logging live on AShooterWeapon so that NPCs, which attach their weapons
	// through their own override, hold them exactly the way the player does.
	AShooterWeapon::AlignMeshToGripSocket(Weapon->GetFirstPersonMesh(), FName("OptionalGrip"));

	USkeletalMeshComponent* ThirdPersonWeaponMesh = Weapon->GetThirdPersonMesh();
	AShooterWeapon::AlignMeshToGripSocket(ThirdPersonWeaponMesh,
		AShooterWeapon::PickThirdPersonSocket(ThirdPersonWeaponMesh, AShooterWeapon::OptionalGripSocketName));

	// Blueprint-added attachments under the weapon's first-person mesh (sights, lasers, anything
	// bolted on in the BP) do not inherit its owner-only rendering and would otherwise show up on
	// the third-person body for every other player.
	ApplyFirstPersonVisibilityToFPSubtree(Weapon->GetFirstPersonMesh());
	ApplyFirstPersonVisibilityToFPSubtree(GetFirstPersonMesh());

	// And the mirror of it: attachments under the weapon's third-person mesh do not inherit its
	// owner-hidden rendering, so without this the shooter sees their own third-person sight or
	// laser floating in front of the camera.
	ApplyThirdPersonVisibilityToTPSubtree(Weapon->GetThirdPersonMesh());
	ApplyThirdPersonVisibilityToTPSubtree(GetMesh());
}

void AShooterCharacter::PlayFiringMontage(UAnimMontage* Montage)
{
	if (!Montage)
	{
		return;
	}

	// Play on third-person mesh (visible to other players)
	PlayThirdPersonMontageLocal(Montage, 1.0f);

	// Play on first-person mesh (visible to local player)
	if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
	{
		if (UAnimInstance* AnimInstance = FPMesh->GetAnimInstance())
		{
			AnimInstance->Montage_Play(Montage);
		}
	}
}

void AShooterCharacter::PlayReloadMontage(UAnimMontage* Montage)
{
	if (!Montage)
	{
		return;
	}

	// The player's own arms, on this machine only: nobody else has a copy of them.
	if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
	{
		if (UAnimInstance* AnimInstance = FPMesh->GetAnimInstance())
		{
			AnimInstance->Montage_Play(Montage);
		}
	}

	// The body everyone else is looking at, on every machine.
	PlayThirdPersonMontageEverywhere(Montage, 1.0f);
}

void AShooterCharacter::PlayThirdPersonMontageEverywhere(UAnimMontage* Montage, float PlayRate)
{
	if (!Montage)
	{
		return;
	}

	if (HasAuthority())
	{
		// The multicast runs on the server too, so this covers the listen server's own body.
		Multicast_PlayThirdPersonMontage(Montage, PlayRate);
		return;
	}

	// A remote client plays it immediately rather than waiting for its own message to come back off
	// the server, then asks the server to show it to everybody else.
	PlayThirdPersonMontageLocal(Montage, PlayRate);

	if (IsLocallyControlled())
	{
		Server_PlayThirdPersonMontage(Montage, PlayRate);
	}
}

void AShooterCharacter::Server_PlayThirdPersonMontage_Implementation(UAnimMontage* Montage, float PlayRate)
{
	Multicast_PlayThirdPersonMontage(Montage, PlayRate);
}

void AShooterCharacter::Multicast_PlayThirdPersonMontage_Implementation(UAnimMontage* Montage, float PlayRate)
{
	// The owning client already played this the moment it asked; playing it again here would
	// restart the animation a round trip in.
	if (IsLocallyControlled() && !HasAuthority())
	{
		return;
	}

	PlayThirdPersonMontageLocal(Montage, PlayRate);
}

void AShooterCharacter::PlayThirdPersonMontageLocal(UAnimMontage* Montage, float PlayRate)
{
	if (!Montage)
	{
		return;
	}

	if (USkeletalMeshComponent* TPMesh = GetMesh())
	{
		if (UAnimInstance* AnimInstance = TPMesh->GetAnimInstance())
		{
			AnimInstance->Montage_Play(Montage, PlayRate);
		}
	}
}

void AShooterCharacter::AddWeaponRecoil(float Recoil)
{
	if (CurrentWeapon && CurrentWeapon->UsesAdvancedRecoil() && RecoilComponent)
	{
		RecoilComponent->OnWeaponFired();
	}
	else
	{
		AddControllerPitchInput(Recoil);
	}
}

void AShooterCharacter::UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize)
{
	OnBulletCountUpdated.Broadcast(MagazineSize, CurrentAmmo);
}

void AShooterCharacter::GetAimRay(float Range, FVector& OutStart, FVector& OutEnd) const
{
	// The ONE definition of where this character is pointing, and everything that needs to know has
	// to come through here.
	//
	// The start is the first-person CAMERA, not GetPawnViewLocation(). Those are different places in
	// this project and not by a little: the pawn view location is the capsule plus BaseEyeHeight,
	// while the camera has been moved by crouch, by the camera-follow lag, by shake and by ADS. A
	// trace from the wrong one leaves along a line that does not pass through the crosshair, and near
	// any edge it hits something else entirely -- which is how the grapple ended up throwing its hook
	// at scenery the player was not looking at.
	//
	// The direction is the controller's, not the camera's forward: the camera component does not
	// follow rotation while the ADS camera is active.
	const UCameraComponent* Cam = GetFirstPersonCameraComponent();
	OutStart = Cam ? Cam->GetComponentLocation() : GetPawnViewLocation();

	FVector AimDirection;
	if (const AController* PC = GetController())
	{
		AimDirection = PC->GetControlRotation().Vector();
	}
	else if (Cam)
	{
		AimDirection = Cam->GetForwardVector();
	}
	else
	{
		AimDirection = GetActorForwardVector();
	}

	OutEnd = OutStart + AimDirection * Range;
}

FVector AShooterCharacter::GetWeaponTargetLocation()
{
	FVector Start, End;
	GetAimRay(MaxAimDistance, Start, End);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	FHitResult OutHit;
	GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, QueryParams);

	return OutHit.bBlockingHit ? OutHit.ImpactPoint : OutHit.TraceEnd;
}

static AShooterWeapon* FindOwnedWeaponWithSameSwitchAction(
	const TArray<AShooterWeapon*>& OwnedWeapons,
	const TSubclassOf<AShooterWeapon>& IncomingWeaponClass)
{
	if (!IncomingWeaponClass)
	{
		return nullptr;
	}

	const AShooterWeapon* IncomingDefault = IncomingWeaponClass->GetDefaultObject<AShooterWeapon>();
	UInputAction* IncomingAction = IncomingDefault ? IncomingDefault->GetSwitchAction() : nullptr;
	if (!IncomingAction)
	{
		return nullptr;
	}

	for (AShooterWeapon* OwnedWeapon : OwnedWeapons)
	{
		if (!OwnedWeapon || OwnedWeapon->IsA(IncomingWeaponClass))
		{
			continue;
		}

		if (OwnedWeapon->GetSwitchAction() == IncomingAction)
		{
			return OwnedWeapon;
		}
	}

	return nullptr;
}

static bool DropOwnedRangedWeaponForPickupReplacement(
	AShooterCharacter* Self,
	TArray<AShooterWeapon*>& OwnedWeapons,
	TObjectPtr<AShooterWeapon>& CurrentWeapon,
	UUpgradeManagerComponent* UpgradeManager,
	AShooterWeapon* WeaponToDrop,
	const FVector& LocalSpawnOffset,
	const FVector& LocalLinearImpulse,
	const FVector& AngularImpulse)
{
	if (!Self || !WeaponToDrop)
	{
		return false;
	}

	const FTransform RefTransform = Self->GetActorTransform();

	if (WeaponToDrop->SourceYankDropClass)
	{
		static const FName OptionalGripSocket(TEXT("OptionalGrip"));
		FVector RefLocation;
		FRotator RefRotation;
		if (USkeletalMeshComponent* WeaponFPMesh = WeaponToDrop->GetFirstPersonMesh())
		{
			RefRotation = WeaponFPMesh->GetComponentRotation();
			RefLocation = WeaponFPMesh->DoesSocketExist(OptionalGripSocket)
				? WeaponFPMesh->GetSocketLocation(OptionalGripSocket)
				: WeaponFPMesh->GetComponentLocation();
		}
		else if (USkeletalMeshComponent* WeaponTPMesh = WeaponToDrop->GetThirdPersonMesh())
		{
			RefRotation = WeaponTPMesh->GetComponentRotation();
			RefLocation = WeaponTPMesh->DoesSocketExist(OptionalGripSocket)
				? WeaponTPMesh->GetSocketLocation(OptionalGripSocket)
				: WeaponTPMesh->GetComponentLocation();
		}
		else
		{
			RefLocation = RefTransform.GetLocation();
			RefRotation = RefTransform.Rotator();
		}

		const FVector SpawnLoc = RefLocation + RefRotation.RotateVector(LocalSpawnOffset);
		const FRotator SpawnRot = RefRotation;

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ADroppedRangedWeapon* Discarded = Self->GetWorld()->SpawnActor<ADroppedRangedWeapon>(
			WeaponToDrop->SourceYankDropClass, SpawnLoc, SpawnRot, Params);

		if (Discarded)
		{
			Discarded->bCanBeCaptured = false;
			Discarded->SetCharge(0.0f);
			if (UEMFChargeWidgetSubsystem* WidgetSub = Self->GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>())
			{
				WidgetSub->UnregisterDroppedRangedWeapon(Discarded);
			}

			if (UStaticMeshComponent* DiscardedMesh = Discarded->WeaponMesh)
			{
				const FVector WorldLinearImpulse = RefTransform.TransformVector(LocalLinearImpulse);
				DiscardedMesh->AddImpulse(WorldLinearImpulse, NAME_None, /*bVelChange=*/ true);
				DiscardedMesh->AddAngularImpulseInDegrees(AngularImpulse, NAME_None, /*bVelChange=*/ true);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] Slot replacement: could not spawn dropped copy for %s"),
				*GetNameSafe(WeaponToDrop));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] Slot replacement: %s has no SourceYankDropClass; removing without dropped copy"),
			*GetNameSafe(WeaponToDrop));
	}

	const bool bWasCurrent = (CurrentWeapon == WeaponToDrop);
	OwnedWeapons.Remove(WeaponToDrop);
	Self->OnWeaponInventoryChanged.Broadcast();

	if (bWasCurrent)
	{
		AShooterWeapon* OldCurrent = CurrentWeapon;
		CurrentWeapon->DeactivateWeapon();
		CurrentWeapon = nullptr;
		Self->OnActiveWeaponChanged.Broadcast(nullptr);

		if (UpgradeManager)
		{
			UpgradeManager->NotifyWeaponChanged(OldCurrent, nullptr);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] Slot replacement: dropped old %s because incoming weapon uses the same SwitchAction"),
		*GetNameSafe(WeaponToDrop));

	WeaponToDrop->Destroy();
	return true;
}

void AShooterCharacter::AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass)
{
	// Only the server creates weapons; everyone else receives them.
	//
	// This used to run on every machine, which was invisible while nothing replicated: each side
	// simply kept its own private set. Once weapon actors started replicating, every client ended
	// up holding BOTH sets, nine actors instead of four for two players. That is what put two guns
	// in the same socket, and it is why a client's equip request did nothing: the client handed the
	// server a locally spawned actor, which has no network identity, so the reference arrived null.
	if (!HasAuthority())
	{
		return;
	}

	AShooterWeapon* OwnedWeapon = FindWeaponOfType(WeaponClass);

	UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] AddWeaponClass: Class=%s, OwnedWeapon=%s (%s branch)"),
		*GetNameSafe(WeaponClass),
		*GetNameSafe(OwnedWeapon),
		OwnedWeapon ? TEXT("TOP-UP") : TEXT("SPAWN NEW"));

	if (!OwnedWeapon)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot;

		AShooterWeapon* AddedWeapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponClass, GetActorTransform(), SpawnParams);

		if (AddedWeapon)
		{
			// Check if this is the first weapon (for visibility update)
			const bool bWasUnarmed = OwnedWeapons.Num() == 0;

			UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] Spawned new %s: MagazineSize=%d, bHasLimitedAmmo=%d"),
				*GetNameSafe(AddedWeapon), AddedWeapon->GetMagazineSize(), AddedWeapon->bHasLimitedAmmo ? 1 : 0);

			OwnedWeapons.Add(AddedWeapon);
			OnWeaponInventoryChanged.Broadcast();

			if (CurrentWeapon)
			{
				CurrentWeapon->DeactivateWeapon();
			}

			AShooterWeapon* OldWeapon = CurrentWeapon;
			CurrentWeapon = AddedWeapon;
			CurrentWeapon->ActivateWeapon();

			// Notify upgrade system about new weapon
			if (UpgradeManager)
			{
				UpgradeManager->NotifyWeaponChanged(OldWeapon, CurrentWeapon);
			}

			// Update mesh visibility when picking up first weapon
			if (bWasUnarmed)
			{
				UpdateFirstPersonMeshVisibility();
			}
		}
	}
	else
	{
		// Already own this weapon class — picking up a duplicate tops up its magazine to full
		// instead of being wasted. SetBulletCount clamps to [0, MagazineSize].
		const int32 MaxAmmo = OwnedWeapon->GetMagazineSize();
		const int32 BeforeAmmo = OwnedWeapon->GetBulletCount();

		UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] TOP-UP candidate %s: Bullets=%d/%d, bHasLimitedAmmo=%d, IsCurrent=%d"),
			*GetNameSafe(OwnedWeapon), BeforeAmmo, MaxAmmo,
			OwnedWeapon->bHasLimitedAmmo ? 1 : 0,
			(OwnedWeapon == CurrentWeapon) ? 1 : 0);

		if (BeforeAmmo < MaxAmmo)
		{
			OwnedWeapon->SetBulletCount(MaxAmmo);

			UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] TOP-UP applied: %d -> %d"),
				BeforeAmmo, OwnedWeapon->GetBulletCount());

			// Refresh the ammo HUD only if this is the weapon currently in hand
			if (OwnedWeapon == CurrentWeapon)
			{
				UpdateWeaponHUD(OwnedWeapon->GetBulletCount(), MaxAmmo);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[PICKUP_DEBUG] TOP-UP SKIPPED — magazine already full (%d/%d). "
				"Standard pickups auto-refill, so they are rarely below max."), BeforeAmmo, MaxAmmo);
		}
	}
}

AShooterWeapon* AShooterCharacter::AddWeaponClassAnimated(const TSubclassOf<AShooterWeapon>& WeaponClass)
{
	// Already own this class — return existing instance, no swap.
	if (AShooterWeapon* OwnedWeapon = FindWeaponOfType(WeaponClass))
	{
		return OwnedWeapon;
	}

	// Server creates the weapon; a client gets it replicated. See AddWeaponClass for why.
	if (!HasAuthority())
	{
		return nullptr;
	}

	if (AShooterWeapon* ConflictingWeapon = FindOwnedWeaponWithSameSwitchAction(OwnedWeapons, WeaponClass))
	{
		if (!DropOwnedRangedWeaponForPickupReplacement(this, OwnedWeapons, CurrentWeapon, UpgradeManager,
			ConflictingWeapon, YankDropSpawnOffset, YankDropLinearImpulse, YankDropAngularImpulse))
		{
			return nullptr;
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot;

	AShooterWeapon* AddedWeapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponClass, GetActorTransform(), SpawnParams);
	if (!AddedWeapon)
	{
		return nullptr;
	}

	const bool bWasUnarmed = (OwnedWeapons.Num() == 0);
	OwnedWeapons.Add(AddedWeapon);
	OnWeaponInventoryChanged.Broadcast();

	if (PendingYankThrowWeapon.IsValid() &&
		IsYankThrowMontageActiveOnFPMesh(ChargeAnimationComponent, GetFirstPersonMesh()))
	{
		// A yank-throw montage is playing (this weapon arrived from yanking while another
		// yanked weapon was being thrown). Equipping now would swap the anim instance and
		// kill the montage mid-throw — instead leave the weapon holstered in inventory;
		// OnYankThrowLowerNotify prefers the freshest yanked weapon as the replacement, so
		// it rises through the montage's own lower→swap flow.
	}
	else if (WeaponSwitchPhase == EWeaponSwitchPhase::Holstering ||
			 WeaponSwitchPhase == EWeaponSwitchPhase::WaitingForWeapon)
	{
		// Yank path: BeginWeaponLower() already started putting the old weapon away, and the hands
		// are either still doing that or already empty and waiting. Name the arrival.
		FinishWeaponSwitch(AddedWeapon);
	}
	else if (bWasUnarmed || !CurrentWeapon)
	{
		// Nothing to swap from — instant equip path mirrors the tail of AddWeaponClass.
		AShooterWeapon* OldWeapon = CurrentWeapon;
		CurrentWeapon = AddedWeapon;
		CurrentWeapon->ActivateWeapon();

		if (UpgradeManager)
		{
			UpgradeManager->NotifyWeaponChanged(OldWeapon, CurrentWeapon);
		}

		if (bWasUnarmed)
		{
			UpdateFirstPersonMeshVisibility();
		}
	}
	else
	{
		// Player is currently holding a weapon — use the standard Q-switch pipeline.
		// StartWeaponSwitch plays the holster → swaps at its notify (deactivate old, activate new,
		// notify upgrades) → draws. New weapon must already be in OwnedWeapons (it is, line above).
		StartWeaponSwitch(AddedWeapon);
	}

	return AddedWeapon;
}

void AShooterCharacter::BeginWeaponLower(bool bPlayHolsterMontage)
{
	// Don't interrupt an already-running switch
	if (IsWeaponSwitchInProgress())
	{
		return;
	}

	// Nothing to put away — caller (e.g. yank pickup on unarmed player) will fall through to
	// AddWeaponClassAnimated which handles instant equip.
	if (!CurrentWeapon)
	{
		return;
	}

	// Stop firing current weapon
	CurrentWeapon->StopFiring();

	// No PendingWeapon yet — FinishWeaponSwitch will set it
	PendingWeapon = nullptr;

	const float HolsterLength = bPlayHolsterMontage ? CurrentWeapon->GetHolsterLength() : 0.0f;
	if (HolsterLength <= 0.0f)
	{
		// Either this weapon has no holster animation, or the hands were already emptied by another
		// animation (the yank-throw montage) and a holster on top of it would be two animations
		// fighting over the same arms. Straight to waiting, hands empty.
		CurrentWeapon->DeactivateWeapon();
		WeaponSwitchPhase = EWeaponSwitchPhase::WaitingForWeapon;
		PlayWeaponSwitchSound();
		return;
	}

	PlayWeaponSwitchMontage(CurrentWeapon->GetHolsterMontage(), CurrentWeapon->GetHolsterMontageTP(),
		CurrentWeapon->GetHolsterPlayRate());

	WeaponSwitchPhase = EWeaponSwitchPhase::Holstering;

	// Same timing as an ordinary swap. It matters more here: this path ends in empty hands, and a
	// notify lost to an interruption would leave the player unable to hold anything again.
	GetWorldTimerManager().SetTimer(WeaponSwitchTimer, this, &AShooterCharacter::OnWeaponSwitchSwapNotify,
		FMath::Max(GetHolsterSwapDelay(CurrentWeapon), KINDA_SMALL_NUMBER), false);

	PlayWeaponSwitchSound();
}

void AShooterCharacter::FinishWeaponSwitch(AShooterWeapon* NewWeapon)
{
	if (WeaponSwitchPhase == EWeaponSwitchPhase::Holstering)
	{
		// The old weapon is still being put away. Remember what arrived; the swap point draws it.
		PendingWeapon = NewWeapon;
		return;
	}

	if (WeaponSwitchPhase != EWeaponSwitchPhase::WaitingForWeapon)
	{
		return;
	}

	PendingWeapon = NewWeapon;
	BeginWeaponDraw();
}

// Shared helper: finds the first yanked weapon in OwnedWeapons, spawns a non-capturable
// ADroppedRangedWeapon at LocalSpawnOffset (in player actor space) with given impulses, and
// removes the weapon from inventory. If bEnableStunOnImpact, the spawned actor will stun
// AShooterNPCs it collides with above its velocity threshold.
static void DiscardYankedWeaponShared(
	AShooterCharacter* Self,
	TArray<AShooterWeapon*>& OwnedWeapons,
	TObjectPtr<AShooterWeapon>& CurrentWeapon,
	UUpgradeManagerComponent* UpgradeManager,
	const FVector& LocalSpawnOffset,
	const FVector& LocalLinearImpulse,
	const FVector& AngularImpulse,
	bool bEnableStunOnImpact)
{
	AShooterWeapon* YankedWeapon = nullptr;
	UE_LOG(LogTemp, Warning, TEXT("[YANK_THROW] DiscardYankedWeaponShared — scanning %d owned weapons (stunOnImpact=%d)"),
		OwnedWeapons.Num(), bEnableStunOnImpact ? 1 : 0);
	for (AShooterWeapon* W : OwnedWeapons)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YANK_THROW]   weapon %s: bWasYanked=%d, SourceYankDropClass=%s"),
			W ? *W->GetName() : TEXT("null"),
			W ? (W->bWasYanked ? 1 : 0) : -1,
			(W && W->SourceYankDropClass) ? *W->SourceYankDropClass->GetName() : TEXT("null"));
		if (W && W->bWasYanked)
		{
			YankedWeapon = W;
			break;
		}
	}

	if (!YankedWeapon)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YANK_THROW] DiscardYankedWeaponShared — no yanked weapon found, returning"));
		return;
	}

	if (YankedWeapon->SourceYankDropClass)
	{
		// RefTransform = source frame for IMPULSE direction.
		// Throw uses camera transform (so vertical aim is respected); passive drop uses actor
		// transform (drops behind back, vertical aim irrelevant). bEnableStunOnImpact == true
		// is our proxy for "this is an active throw".
		FTransform RefTransform;
		if (bEnableStunOnImpact)
		{
			if (UCameraComponent* Cam = Self->GetFirstPersonCameraComponent())
			{
				RefTransform = Cam->GetComponentTransform();
			}
			else
			{
				RefTransform = Self->GetActorTransform();
			}
		}
		else
		{
			RefTransform = Self->GetActorTransform();
		}

		// Location: OptionalGrip socket world position if present (= visible grip).
		// Rotation: component world rotation (= rendered orientation of skeletal mesh vertices).
		static const FName OptionalGripSocket(TEXT("OptionalGrip"));
		FVector RefLocation;
		FRotator RefRotation;
		if (USkeletalMeshComponent* WeaponFPMesh = YankedWeapon->GetFirstPersonMesh())
		{
			RefRotation = WeaponFPMesh->GetComponentRotation();
			RefLocation = WeaponFPMesh->DoesSocketExist(OptionalGripSocket)
				? WeaponFPMesh->GetSocketLocation(OptionalGripSocket)
				: WeaponFPMesh->GetComponentLocation();
		}
		else if (USkeletalMeshComponent* WeaponTPMesh = YankedWeapon->GetThirdPersonMesh())
		{
			RefRotation = WeaponTPMesh->GetComponentRotation();
			RefLocation = WeaponTPMesh->DoesSocketExist(OptionalGripSocket)
				? WeaponTPMesh->GetSocketLocation(OptionalGripSocket)
				: WeaponTPMesh->GetComponentLocation();
		}
		else
		{
			RefLocation = RefTransform.GetLocation();
			RefRotation = RefTransform.Rotator();
		}

		const FVector SpawnLoc = RefLocation + RefRotation.RotateVector(LocalSpawnOffset);
		const FRotator SpawnRot = RefRotation;

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ADroppedRangedWeapon* Discarded = Self->GetWorld()->SpawnActor<ADroppedRangedWeapon>(
			YankedWeapon->SourceYankDropClass, SpawnLoc, SpawnRot, Params);

		if (Discarded)
		{
			// Pure decoration — block re-capture and EMF interaction.
			Discarded->bCanBeCaptured = false;
			Discarded->SetCharge(0.0f);

			// SetCharge re-registers with the widget subsystem; explicitly unregister so no
			// charge UI floats over the discarded weapon.
			if (UEMFChargeWidgetSubsystem* WidgetSub = Self->GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>())
			{
				WidgetSub->UnregisterDroppedRangedWeapon(Discarded);
			}

			// Enable stun-on-impact for thrown variant — drop variant leaves it false (default).
			Discarded->bCanStunOnImpact = bEnableStunOnImpact;

			if (UStaticMeshComponent* DiscardedMesh = Discarded->WeaponMesh)
			{
				// For thrown variant: enable Pawn collision so weapon hits NPCs. Constructor
				// sets Pawn=Ignore so passively-dropped weapons don't push characters around;
				// for an active throw we WANT contact (and OnComponentHit fires only on Block).
				if (bEnableStunOnImpact)
				{
					DiscardedMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
				}

				// Apply impulses (RefTransform-space → world). Same transform used for spawn so
				// linear impulse direction matches "forward" of the source frame (camera or actor).
				const FVector WorldLinearImpulse = RefTransform.TransformVector(LocalLinearImpulse);
				DiscardedMesh->AddImpulse(WorldLinearImpulse, NAME_None, /*bVelChange=*/ true);
				DiscardedMesh->AddAngularImpulseInDegrees(AngularImpulse, NAME_None, /*bVelChange=*/ true);
			}
		}
	}

	// Remove from inventory; if it was the equipped weapon, switch to a non-yanked replacement
	// instantly (no animation — for Drop, caller is about to BeginWeaponLower; for Throw, we
	// just leave the player on the next non-yanked weapon).
	const bool bWasCurrent = (CurrentWeapon == YankedWeapon);
	OwnedWeapons.Remove(YankedWeapon);
	// DiscardYankedWeaponShared is a static helper (no 'this'); broadcast on the passed-in character.
	Self->OnWeaponInventoryChanged.Broadcast();

	if (bWasCurrent)
	{
		AShooterWeapon* OldCurrent = CurrentWeapon;
		CurrentWeapon->DeactivateWeapon();

		// Bandolier: prefer a reserve copy of the same class over the generic non-yanked fallback.
		AShooterWeapon* Replacement = Self->PromoteReserveCopyOfClass(YankedWeapon->GetClass());
		if (!Replacement)
		{
			for (AShooterWeapon* W : OwnedWeapons)
			{
				if (W && !W->bWasYanked)
				{
					Replacement = W;
					break;
				}
			}
		}

		CurrentWeapon = Replacement;
		if (CurrentWeapon)
		{
			CurrentWeapon->ActivateWeapon();
		}

		if (UpgradeManager)
		{
			UpgradeManager->NotifyWeaponChanged(OldCurrent, CurrentWeapon);
		}
	}

	YankedWeapon->Destroy();
}

void AShooterCharacter::ThrowYankedWeaponIfAny()
{
	// Find the yanked weapon (or bail out if none).
	AShooterWeapon* YankedWeapon = nullptr;
	for (AShooterWeapon* W : OwnedWeapons)
	{
		if (W && W->bWasYanked) { YankedWeapon = W; break; }
	}

	if (!YankedWeapon)
	{
		return;
	}

	// Throw always interrupts ADS: the montage needs the hipfire camera/FOV, and the ADS
	// camera anchor would otherwise jump between weapons mid-throw. Re-entering ADS during
	// the montage is blocked in DoStartADS.
	DoStopADS();

	UChargeAnimationComponent* ChargeComp = FindComponentByClass<UChargeAnimationComponent>();

	// Animation flow: store the yanked ref, play the dedicated yank-throw montage, and let
	// the AnimNotifies drive the rest (Discard notify hides mesh + spawns dropped; Lower
	// notify swaps weapons).
	if (ChargeComp && ChargeComp->YankThrowMontage)
	{
		// The montage plays on the FP arms' CURRENT anim instance, and the discard notify
		// spawns the dropped weapon at the yanked weapon's FP mesh transform. Both are only
		// correct while the yanked weapon is the equipped one: its FP AnimBP carries the
		// camera-pitch rig (spine_05 Transform Bone) and its meshes are attached + visible.
		// If the player is holding another weapon, instant-switch to the yanked one first —
		// visually "grab it, then throw it" (same swap pattern as DiscardYankedWeaponShared).
		if (CurrentWeapon != YankedWeapon)
		{
			AShooterWeapon* OldCurrent = CurrentWeapon;
			if (CurrentWeapon)
			{
				CurrentWeapon->DeactivateWeapon();
			}
			CurrentWeapon = YankedWeapon;
			CurrentWeapon->ActivateWeapon();

			if (UpgradeManager)
			{
				UpgradeManager->NotifyWeaponChanged(OldCurrent, CurrentWeapon);
			}
		}

		PendingYankThrowWeapon = YankedWeapon;
		ChargeComp->PlayYankThrowMontage();
		return;
	}

	// Fallback: no yank-throw montage configured — do an instant discard the old way so
	// other game logic (ammo-empty, channeling-yank) still works without an animation asset
	// set up.
	DiscardYankedWeaponShared(this, OwnedWeapons, CurrentWeapon, UpgradeManager,
		YankThrowSpawnOffset, YankThrowLinearImpulse, YankThrowAngularImpulse,
		/*bEnableStunOnImpact=*/ true);
}

void AShooterCharacter::ThrowYankedWeaponIfEmpty()
{
	for (AShooterWeapon* W : OwnedWeapons)
	{
		if (W && W->bWasYanked && W->GetBulletCount() <= 0)
		{
			ThrowYankedWeaponIfAny();
			return;
		}
	}
}

void AShooterCharacter::OnYankThrowDiscardNotify()
{
	// Phase 1 of the animated throw: weapon visually leaves the hand. Spawn the dropped
	// version at the held FP weapon mesh's exact world transform (so the dropped pickup
	// appears continuous with what the player saw a frame ago) and hide the held weapon's
	// FP/TP meshes so the rest of the throw animation plays with empty hands.
	AShooterWeapon* Yanked = PendingYankThrowWeapon.Get();
	if (!Yanked)
	{
		return;
	}

	if (!Yanked->SourceYankDropClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YANK_THROW] OnYankThrowDiscardNotify: yanked weapon has no SourceYankDropClass — can't spawn dropped version"));
		return;
	}

	// Spawn position: FP weapon mesh's VISUAL grip — uses OptionalGrip socket world LOCATION
	// when present (matches HandGrip_R thanks to the alignment in AttachWeaponMeshes), so the
	// dropped mesh's pivot lands exactly where the held mesh's grip is.
	//
	// Spawn rotation: COMPONENT world rotation — that's the rotation the skeletal mesh's
	// vertices are rendered with. The static-mesh asset for the dropped version is authored
	// in the same orientation as the skeletal-mesh asset, so component rotation is the
	// correct match (socket rotation differs because of the inverse-rotation alignment math).
	static const FName OptionalGripSocket(TEXT("OptionalGrip"));
	FVector RefLocation;
	FRotator RefRotation;
	if (USkeletalMeshComponent* WeaponFPMesh = Yanked->GetFirstPersonMesh())
	{
		RefRotation = WeaponFPMesh->GetComponentRotation();
		RefLocation = WeaponFPMesh->DoesSocketExist(OptionalGripSocket)
			? WeaponFPMesh->GetSocketLocation(OptionalGripSocket)
			: WeaponFPMesh->GetComponentLocation();
	}
	else if (USkeletalMeshComponent* WeaponTPMesh = Yanked->GetThirdPersonMesh())
	{
		RefRotation = WeaponTPMesh->GetComponentRotation();
		RefLocation = WeaponTPMesh->DoesSocketExist(OptionalGripSocket)
			? WeaponTPMesh->GetSocketLocation(OptionalGripSocket)
			: WeaponTPMesh->GetComponentLocation();
	}
	else
	{
		RefLocation = GetActorLocation();
		RefRotation = GetActorRotation();
	}

	// Apply YankThrowSpawnOffset as fine-tune in world rotation frame.
	const FVector SpawnLoc = RefLocation + RefRotation.RotateVector(YankThrowSpawnOffset);
	const FRotator SpawnRot = RefRotation;

	// Impulse direction: still based on camera (for vertical aim feel).
	FTransform ImpulseRef;
	if (UCameraComponent* Cam = GetFirstPersonCameraComponent())
	{
		ImpulseRef = Cam->GetComponentTransform();
	}
	else
	{
		ImpulseRef = GetActorTransform();
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADroppedRangedWeapon* Discarded = GetWorld()->SpawnActor<ADroppedRangedWeapon>(
		Yanked->SourceYankDropClass, SpawnLoc, SpawnRot, Params);

	if (Discarded)
	{
		Discarded->bCanBeCaptured = false;
		Discarded->SetCharge(0.0f);
		if (UEMFChargeWidgetSubsystem* WidgetSub = GetWorld()->GetSubsystem<UEMFChargeWidgetSubsystem>())
		{
			WidgetSub->UnregisterDroppedRangedWeapon(Discarded);
		}
		Discarded->bCanStunOnImpact = true;

		if (UStaticMeshComponent* DiscardedMesh = Discarded->WeaponMesh)
		{
			DiscardedMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
			const FVector WorldLinearImpulse = ImpulseRef.TransformVector(YankThrowLinearImpulse);
			DiscardedMesh->AddImpulse(WorldLinearImpulse, NAME_None, /*bVelChange=*/ true);
			DiscardedMesh->AddAngularImpulseInDegrees(YankThrowAngularImpulse, NAME_None, /*bVelChange=*/ true);
		}
	}

	// Hide the held weapon's meshes so the rest of the throw animation plays with empty hands.
	if (USkeletalMeshComponent* FPMesh = Yanked->GetFirstPersonMesh())
	{
		FPMesh->SetVisibility(false, /*bPropagateToChildren=*/ true);
	}
	if (USkeletalMeshComponent* TPMesh = Yanked->GetThirdPersonMesh())
	{
		TPMesh->SetVisibility(false, /*bPropagateToChildren=*/ true);
	}
}

void AShooterCharacter::OnYankThrowLowerNotify()
{
	// Phase 2 of the animated throw: empty hands lower, then a non-yanked replacement weapon
	// is raised. Hooks into the existing BeginWeaponLower + FinishWeaponSwitch state machine.
	AShooterWeapon* Yanked = PendingYankThrowWeapon.Get();
	if (!Yanked)
	{
		return;
	}

	// Remove the yanked weapon from inventory now (so PromoteReserveCopyOfClass / the non-yanked
	// fallback below don't accidentally pick it up). CurrentWeapon may still point to Yanked —
	// that's intentional, BeginWeaponLower needs a non-null current to lower. Actor destruction
	// is scheduled below.
	OwnedWeapons.Remove(Yanked);
	OnWeaponInventoryChanged.Broadcast();

	// Replacement priority:
	// 1) The freshest OTHER yanked weapon — when this throw was triggered by yanking a new
	//    weapon, the incoming yank is already holstered in OwnedWeapons (AddWeaponClassAnimated
	//    defers its equip while the montage plays) and is what should rise.
	// 2) Bandolier: reserve copy of the thrown class (promoted into OwnedWeapons so
	//    FinishWeaponSwitch's ActivateWeapon attaches + un-hides it correctly).
	// 3) Any non-yanked weapon (starter pistol, melee).
	AShooterWeapon* Replacement = nullptr;
	for (AShooterWeapon* W : OwnedWeapons)
	{
		if (W && W != Yanked && W->bWasYanked)
		{
			Replacement = W;
			break;
		}
	}

	if (!Replacement)
	{
		Replacement = PromoteReserveCopyOfClass(Yanked->GetClass());
	}

	if (!Replacement)
	{
		for (AShooterWeapon* W : OwnedWeapons)
		{
			if (W && W != Yanked && !W->bWasYanked)
			{
				Replacement = W;
				break;
			}
		}
	}

	if (Replacement)
	{
		// No holster montage here: the throw montage owns these arms and has already emptied them.
		BeginWeaponLower(/*bPlayHolsterMontage=*/ false);
		FinishWeaponSwitch(Replacement); // equips the replacement and plays its draw
	}
	else
	{
		// No replacement — just deactivate yanked, leave hands empty.
		if (CurrentWeapon == Yanked)
		{
			Yanked->DeactivateWeapon();
			CurrentWeapon = nullptr;
			OnActiveWeaponChanged.Broadcast(nullptr);   // now unarmed
		}
	}

	// Schedule destruction of the orphaned yanked actor after the switch animation has had
	// time to complete. 1s is a heuristic safe upper bound for typical lower+raise durations.
	GetWorldTimerManager().SetTimer(YankActorDestroyTimer, this,
		&AShooterCharacter::DestroyOrphanedYankActor, 1.0f, /*bLoop=*/ false);
}

void AShooterCharacter::DestroyOrphanedYankActor()
{
	if (AShooterWeapon* Yanked = PendingYankThrowWeapon.Get())
	{
		Yanked->Destroy();
	}
	PendingYankThrowWeapon.Reset();
}

// ==================== Swap Weapon Hold Detection ====================
// NOTE: OnSwitchWeaponPressed/OnSwitchWeaponReleased are NO LONGER BOUND — the cycle key
// (SwitchWeaponAction) now does a plain DoSwitchWeapon on press, and hold-to-throw lives on
// the yanked weapon's own switch key (lambdas in SetupPlayerInputComponent reusing
// SwapHoldTimer/bSwapKeyHeldPending/OnSwapHoldThresholdFired). Definitions kept because the
// header still declares them; remove both on the next header-touching pass.

void AShooterCharacter::OnSwitchWeaponPressed()
{
	const float Now = GetWorld()->GetTimeSeconds();
	UE_LOG(LogTemp, Warning, TEXT("[YANK_THROW] OnSwitchWeaponPressed @ T=%.3f — starting hold timer (threshold=%.2fs)"),
		Now, YankSwapHoldThreshold);
	SwapKeyPressTime = Now;
	bSwapKeyHeldPending = true;
	GetWorld()->GetTimerManager().SetTimer(
		SwapHoldTimer, this, &AShooterCharacter::OnSwapHoldThresholdFired,
		YankSwapHoldThreshold, false);
}

void AShooterCharacter::OnSwitchWeaponReleased()
{
	const float Now = GetWorld()->GetTimeSeconds();
	const float HeldFor = Now - SwapKeyPressTime;
	UE_LOG(LogTemp, Warning, TEXT("[YANK_THROW] OnSwitchWeaponReleased @ T=%.3f — held for %.3fs (threshold=%.2fs)"),
		Now, HeldFor, YankSwapHoldThreshold);

	// If the timer is still active, this was a tap — preserve the original instant-swap behavior.
	// If the timer already fired, hold path already executed (throw); nothing to do.
	if (bSwapKeyHeldPending)
	{
		FTimerManager& TimerMgr = GetWorld()->GetTimerManager();
		if (TimerMgr.IsTimerActive(SwapHoldTimer))
		{
			TimerMgr.ClearTimer(SwapHoldTimer);
			bSwapKeyHeldPending = false;
			UE_LOG(LogTemp, Warning, TEXT("[YANK_THROW]   → TAP path (DoSwitchWeapon)"));
			DoSwitchWeapon();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[YANK_THROW]   → pending=true but timer inactive"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[YANK_THROW]   → pending=false (hold already fired, no-op)"));
	}
}

void AShooterCharacter::OnSwapHoldThresholdFired()
{
	UE_LOG(LogTemp, Warning, TEXT("[YANK_THROW] OnSwapHoldThresholdFired — calling ThrowYankedWeaponIfAny"));
	bSwapKeyHeldPending = false;
	ThrowYankedWeaponIfAny();
}

// ==================== Riot Shield ====================

void AShooterCharacter::EquipShield(ARiotShield* Shield)
{
	if (!Shield)
	{
		return;
	}
	if (EquippedShield)
	{
		// Already carry one — refuse (swap behavior is a future feature).
		return;
	}

	EquippedShield = Shield;
	Shield->OnDestroyed.AddUniqueDynamic(this, &AShooterCharacter::OnEquippedShieldDestroyed);
	Shield->EquipToCharacter(this);

	// Disable wallrun while a shield is held (kills any active wallrun, blocks new ones).
	if (UApexMovementComponent* Apex = GetApexMovement())
	{
		Apex->SetWallRunExternallyDisabled(true);
	}
}

void AShooterCharacter::OnEquippedShieldDestroyed(AActor* DestroyedActor)
{
	if (DestroyedActor == EquippedShield)
	{
		EquippedShield = nullptr;

		// Re-enable wallrun now that the shield is gone.
		if (UApexMovementComponent* Apex = GetApexMovement())
		{
			Apex->SetWallRunExternallyDisabled(false);
		}
	}
}

void AShooterCharacter::OnShieldTogglePressed()
{
	if (EquippedShield)
	{
		EquippedShield->Toggle();
	}
}

// ==================== Channel button override (grab) ====================

void AShooterCharacter::DoChannelPressed()
{
	// Shield held → repurpose the grab key as "throw shield" and suppress the channel/capture path.
	if (EquippedShield)
	{
		EquippedShield->ThrowAway();
		return;
	}
	Super::DoChannelPressed();
}

void AShooterCharacter::DoChannelReleased()
{
	// Shield held → no channel was started, ignore the release.
	if (EquippedShield)
	{
		return;
	}
	Super::DoChannelReleased();
}

void AShooterCharacter::OnWeaponActivated(AShooterWeapon* Weapon)
{
	OnBulletCountUpdated.Broadcast(Weapon->GetMagazineSize(), Weapon->GetBulletCount());

	TSubclassOf<UAnimInstance> FPAnimClass = Weapon->GetFirstPersonAnimInstanceClass();
	TSubclassOf<UAnimInstance> TPAnimClass = Weapon->GetThirdPersonAnimInstanceClass();

	// Every weapon, melee included, drives the same first-person arms: the sword's own AnimBP
	// comes in through FirstPersonAnimInstanceClass like any other weapon's.
	if (FPAnimClass)
	{
		GetFirstPersonMesh()->SetAnimInstanceClass(FPAnimClass);
	}

	if (TPAnimClass)
	{
		GetMesh()->SetAnimInstanceClass(TPAnimClass);
	}

	if (RecoilComponent && Weapon->UsesAdvancedRecoil())
	{
		RecoilComponent->SetRecoilSettings(Weapon->GetRecoilSettings());
		RecoilComponent->ResetRecoil();
	}

	// Block MeleeAttackComponent while melee weapon is equipped
	if (MeleeAttackComponent)
	{
		MeleeAttackComponent->SetExternallyDisabled(Weapon->IsMeleeWeapon());
	}

	// Notify UI about melee weapon equip state
	if (Weapon->IsMeleeWeapon())
	{
		if (AShooterWeapon_Melee* MeleeWpn = Cast<AShooterWeapon_Melee>(Weapon))
		{
			OnMeleeWeaponEquipped.Broadcast(true, MeleeWpn->RemainingHits, MeleeWpn->MaxHitCount);
		}
	}
	else
	{
		OnMeleeWeaponEquipped.Broadcast(false, 0, 0);
	}

	// Single armed-transition choke point: every weapon that becomes active funnels through here
	// (pickup, switch, yank-equip, save-load), so the HUD crosshair stays in sync from one place.
	OnActiveWeaponChanged.Broadcast(Weapon);
}

void AShooterCharacter::OnWeaponDeactivated(AShooterWeapon* Weapon)
{
	if (RecoilComponent)
	{
		RecoilComponent->ResetRecoil();
	}

	// Re-enable MeleeAttackComponent when melee weapon is unequipped
	if (MeleeAttackComponent && Weapon->IsMeleeWeapon())
	{
		MeleeAttackComponent->SetExternallyDisabled(false);
	}

	// Notify UI that melee weapon is no longer equipped
	if (Weapon->IsMeleeWeapon())
	{
		OnMeleeWeaponEquipped.Broadcast(false, 0, 0);
	}
}

void AShooterCharacter::RemoveMeleeWeapon(AShooterWeapon* WeaponToRemove)
{
	if (!WeaponToRemove)
	{
		return;
	}

	// Deactivate if currently equipped (restores FP mesh, re-enables MeleeAttackComponent)
	if (CurrentWeapon == WeaponToRemove)
	{
		OnWeaponDeactivated(WeaponToRemove);
		CurrentWeapon = nullptr;
	}

	// Remove from inventory
	OwnedWeapons.Remove(WeaponToRemove);
	OnWeaponInventoryChanged.Broadcast();

	// Switch to first available weapon or clear
	if (OwnedWeapons.Num() > 0)
	{
		CurrentWeapon = OwnedWeapons[0];
		CurrentWeapon->ActivateWeapon();   // -> OnWeaponActivated broadcasts the new active weapon
	}
	else
	{
		UpdateFirstPersonMeshVisibility();
		OnActiveWeaponChanged.Broadcast(nullptr);   // now unarmed
	}

	// Destroy the weapon actor
	WeaponToRemove->Destroy();
}

void AShooterCharacter::UpdateFirstPersonMeshVisibility()
{
	USkeletalMeshComponent* FPMesh = GetFirstPersonMesh();
	if (!FPMesh)
	{
		return;
	}

	const bool bHasWeapon = OwnedWeapons.Num() > 0;

	// Hands go away with the gun for the length of a grapple. Hiding the weapon alone leaves a pair
	// of empty gloves floating in front of the camera, which is what it looked like: the weapon
	// actor is hidden by DeactivateWeapon, but the arms are a component of the CHARACTER and know
	// nothing about it.
	//
	// Asked here rather than set directly at the two stow sites, because this function is what every
	// other system calls when it changes something that affects the arms -- picking a weapon up,
	// losing the last one, the inventory arriving on a client. Any of those firing mid-grapple would
	// have put the hands straight back on screen.
	// Two reasons the arms can be away, and both belong in this one expression.
	//
	// The phase covers a grapple holding the line. The frame counter covers the first frames of ANY
	// draw: the montage has been asked for but the animation graph has not run yet, so the arms
	// would be shown for a frame or two in whatever pose they were left in and then snap into the
	// draw. Keeping the counter HERE rather than deciding it at the call sites is the same rule as
	// the phase -- any other system calling this function during those frames would otherwise reveal
	// the arms early and put the flash straight back.
	const bool bStowedForGrapple = (WeaponSwitchPhase == EWeaponSwitchPhase::StowedForGrapple);
	const bool bWaitingForDrawPose = (FirstPersonRevealFramesLeft > 0);

	// PROPAGATED to children, and that is not incidental. The weapon's own first-person mesh is
	// attached to the arms (@see AttachWeaponMeshes), so hiding the arms without propagating leaves
	// the gun hanging in mid-air by itself -- which would trade a two-frame wrong pose for a
	// two-frame floating rifle. Both halves of the hold have to move together.
	FPMesh->SetVisibility(bHasWeapon && !bStowedForGrapple && !bWaitingForDrawPose, true);
}

void AShooterCharacter::OnSemiWeaponRefire()
{
	// unused
}

void AShooterCharacter::OnWeaponHit(const FVector& HitLocation, const FVector& HitDirection, float Damage, bool bHeadshot, bool bKilled, AActor* HitActor)
{
	// Callers that predate the context still work: they arrive with the shield fields at their
	// defaults, which reads as "an ordinary hit", exactly what this call meant before.
	// Overridden rather than inherited so the interface's default does not bounce straight back.
	FHitFeedbackContext Context;
	Context.HitLocation = HitLocation;
	Context.HitDirection = HitDirection;
	Context.Damage = Damage;
	Context.bHeadshot = bHeadshot;
	Context.bKilled = bKilled;
	Context.HitActor = HitActor;

	OnWeaponHitFeedback(Context);
}

void AShooterCharacter::OnWeaponHitFeedback(const FHitFeedbackContext& Context)
{
	const FVector& HitLocation = Context.HitLocation;
	const float Damage = Context.Damage;
	const bool bHeadshot = Context.bHeadshot;
	const bool bKilled = Context.bKilled;
	AActor* HitActor = Context.HitActor;

	if (HitMarkerComponent)
	{
		// The 2D half. The component decides for itself whether this machine is the one that gets
		// to hear it; the world half of the same hit already went out from the weapon.
		HitMarkerComponent->RegisterHitFeedback(Context);
	}

	// === Stream style hook ===
	if (bKilled)
	{
		if (UStyleComponent* Style = FindComponentByClass<UStyleComponent>())
		{
			// Priority: rarer / higher-skill actions first.
			EStyleCategory Category = EStyleCategory::Kill;
			if (ActiveAirDashTrailComponent != nullptr)
			{
				Category = EStyleCategory::AirDashKill;
			}
			else if (bHeadshot)
			{
				Category = EStyleCategory::Headshot;
			}
			else if (CurrentWeapon && CurrentWeapon->bWasYanked)
			{
				Category = EStyleCategory::YankKill;
			}

			FStyleAction Action;
			Action.Category = Category;
			Action.WorldLocation = HitActor ? HitActor->GetActorLocation() : HitLocation;
			Action.InstanceMultiplier = 1.0f;
			Style->RegisterAction(Action);
		}
	}

	// Charge transfer for melee weapon hits (same logic as OnMeleeHit)
	//
	// Stands down for a blade that states its own ionization: that weapon pays its wielder from its
	// own MeleeChargeToAttackerPerHit, in the same place it charges the target. What this block does
	// instead is read the amount off the VICTIM (-GetChargeChangeOnMeleeHit), which meant the same
	// swing paid the player differently depending on what it hit, with the reason authored on the
	// enemy rather than on the sword.
	if (HitActor && CurrentWeapon && CurrentWeapon->IsMeleeWeapon()
		&& !AShooterWeapon_Melee::AttackerOverridesLegacyMeleeCharge(this))
	{
		if (UEMFVelocityModifier* EMFMod = FindComponentByClass<UEMFVelocityModifier>())
		{
			bool bIsDummyTarget = HitActor->Implements<UShooterDummyTarget>();

			if (bIsDummyTarget)
			{
				bool bGrantsStable = IShooterDummyTarget::Execute_GrantsStableCharge(HitActor);
				if (bGrantsStable)
				{
					float StableAmount = IShooterDummyTarget::Execute_GetStableChargeAmount(HitActor);
					if (StableAmount > 0.0f)
					{
						EMFMod->AddPermanentCharge(StableAmount);
					}
					if (bKilled)
					{
						float KillBonus = IShooterDummyTarget::Execute_GetKillChargeBonus(HitActor);
						if (KillBonus > 0.0f)
						{
							EMFMod->AddPermanentCharge(KillBonus);
						}
					}
					return;
				}
			}

			float ChargeAmount = EMFMod->ChargePerMeleeHit;
			if (AShooterNPC* HitNPC = Cast<AShooterNPC>(HitActor))
			{
				ChargeAmount = -HitNPC->GetChargeChangeOnMeleeHit();
			}

			float OldCharge = EMFMod->GetCharge();
			EMFMod->AddBonusCharge(ChargeAmount);
			float NewCharge = EMFMod->GetCharge();

			UE_LOG(LogTemp, Warning, TEXT("[WeaponMeleeCharge] Hit %s - Charge: %.2f -> %.2f (added %.2f bonus)"),
				HitActor ? *HitActor->GetName() : TEXT("NULL"),
				OldCharge, NewCharge, ChargeAmount);
		}
	}
}

AShooterWeapon* AShooterCharacter::FindWeaponOfType(TSubclassOf<AShooterWeapon> WeaponClass) const
{
	for (AShooterWeapon* Weapon : OwnedWeapons)
	{
		if (Weapon->IsA(WeaponClass))
		{
			return Weapon;
		}
	}

	return nullptr;
}

UInputAction* AShooterCharacter::GetSwitchInputActionForWeapon(const AShooterWeapon* Weapon) const
{
	if (!Weapon)
	{
		return nullptr;
	}

	// The hotkey now lives on the weapon itself.
	if (UInputAction* WeaponAction = Weapon->GetSwitchAction())
	{
		return WeaponAction;
	}

	// No per-weapon hotkey — the only route to this weapon is the forward-cycle key.
	return SwitchWeaponAction;
}

int32 AShooterCharacter::CountYankedCopiesOfClass(TSubclassOf<AShooterWeapon> WeaponClass) const
{
	if (!WeaponClass) return 0;

	int32 Count = 0;
	for (const AShooterWeapon* W : OwnedWeapons)
	{
		if (W && W->bHasLimitedAmmo && W->IsA(WeaponClass)) { ++Count; }
	}
	for (const AShooterWeapon* W : ReserveWeapons)
	{
		if (W && W->bHasLimitedAmmo && W->IsA(WeaponClass)) { ++Count; }
	}
	return Count;
}

AShooterWeapon* AShooterCharacter::AddYankedReserveCopy(TSubclassOf<AShooterWeapon> WeaponClass,
	TSubclassOf<ADroppedRangedWeapon> SourceDropClass,
	int32 BulletCount)
{
	if (!WeaponClass) return nullptr;

	// Server creates the weapon; a client gets it replicated. See AddWeaponClass for why.
	if (!HasAuthority())
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot;

	AShooterWeapon* Reserve = GetWorld()->SpawnActor<AShooterWeapon>(WeaponClass, GetActorTransform(), SpawnParams);
	if (!Reserve)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BANDOLIER] AddYankedReserveCopy — SpawnActor failed for %s"),
			*WeaponClass->GetName());
		return nullptr;
	}

	// Hide before any visibility paint hits the screen. ActivateWeapon later un-hides.
	Reserve->SetActorHiddenInGame(true);

	Reserve->bWasYanked = true;
	Reserve->bHasLimitedAmmo = true;
	Reserve->SourceYankDropClass = SourceDropClass;
	Reserve->SetBulletCount(BulletCount);

	ReserveWeapons.Add(Reserve);

	UE_LOG(LogTemp, Warning, TEXT("[BANDOLIER] Stashed reserve %s with %d bullets (reserve count for class now %d)"),
		*Reserve->GetName(), BulletCount, CountYankedCopiesOfClass(WeaponClass));

	return Reserve;
}

void AShooterCharacter::SpillBulletsIntoYankedCopiesOfClass(TSubclassOf<AShooterWeapon> WeaponClass, int32 BulletsToSpill)
{
	if (!WeaponClass || BulletsToSpill <= 0) return;

	// Build the fill order: CurrentWeapon (if it matches), then other yanked OwnedWeapons of class,
	// then ReserveWeapons. Each copy gets topped up to its MagazineSize.
	TArray<AShooterWeapon*> FillOrder;
	if (CurrentWeapon && CurrentWeapon->bHasLimitedAmmo && CurrentWeapon->IsA(WeaponClass))
	{
		FillOrder.Add(CurrentWeapon);
	}
	for (AShooterWeapon* W : OwnedWeapons)
	{
		if (W && W != CurrentWeapon && W->bHasLimitedAmmo && W->IsA(WeaponClass))
		{
			FillOrder.Add(W);
		}
	}
	for (AShooterWeapon* W : ReserveWeapons)
	{
		if (W && W->bHasLimitedAmmo && W->IsA(WeaponClass))
		{
			FillOrder.Add(W);
		}
	}

	int32 Remaining = BulletsToSpill;
	for (AShooterWeapon* W : FillOrder)
	{
		if (Remaining <= 0) break;

		const int32 Headroom = W->GetMagazineSize() - W->GetBulletCount();
		if (Headroom <= 0) continue;

		const int32 Add = FMath::Min(Headroom, Remaining);
		W->SetBulletCount(W->GetBulletCount() + Add);
		Remaining -= Add;

		UE_LOG(LogTemp, Log, TEXT("[BANDOLIER] Spilled %d into %s (now %d/%d), %d left"),
			Add, *W->GetName(), W->GetBulletCount(), W->GetMagazineSize(), Remaining);
	}

	UE_LOG(LogTemp, Warning, TEXT("[BANDOLIER] Spill for %s: %d total bullets, %d wasted (all mags full)"),
		*WeaponClass->GetName(), BulletsToSpill, Remaining);
}

AShooterWeapon* AShooterCharacter::PromoteReserveCopyOfClass(TSubclassOf<AShooterWeapon> WeaponClass)
{
	if (!WeaponClass) return nullptr;

	AShooterWeapon* Promoted = nullptr;
	for (AShooterWeapon* W : ReserveWeapons)
	{
		if (W && W->IsA(WeaponClass)) { Promoted = W; break; }
	}

	if (!Promoted) return nullptr;

	ReserveWeapons.Remove(Promoted);
	OwnedWeapons.Add(Promoted);
	OnWeaponInventoryChanged.Broadcast();

	// ActivateWeapon (called by the equip path right after this) will SetActorHiddenInGame(false)
	// and attach the meshes — no need to do either here.

	UE_LOG(LogTemp, Warning, TEXT("[BANDOLIER] Promoted reserve %s into OwnedWeapons (%d reserves left)"),
		*Promoted->GetName(), ReserveWeapons.Num());

	return Promoted;
}

// ==================== Damage Feedback ====================

void AShooterCharacter::PlayDamageFeedback(float Damage, TSubclassOf<UDamageType> DamageTypeClass)
{
	// Play camera shake scaled by damage
	if (DamageCameraShake)
	{
		float ShakeScale = 1.0f;
		if (DamageToCameraShakeCurve)
		{
			ShakeScale = DamageToCameraShakeCurve->GetFloatValue(Damage) * MaxCameraShakeScale;
		}
		else
		{
			// Default: linear scale up to MaxCameraShakeScale at 100 damage
			ShakeScale = FMath::Clamp(Damage / 100.0f, 0.1f, 1.0f) * MaxCameraShakeScale;
		}

		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			PC->ClientStartCameraShake(DamageCameraShake, ShakeScale);
		}
	}

	// Play impact sound based on damage type
	USoundBase* ImpactSound = GetImpactSoundForDamageType(DamageTypeClass);
	if (ImpactSound)
	{
		UGameplayStatics::PlaySound2D(this, ImpactSound, DamageImpactSoundVolume);
	}

	// Start chromatic aberration effect
	StartChromaticAberrationEffect(Damage);
}

USoundBase* AShooterCharacter::GetImpactSoundForDamageType(TSubclassOf<UDamageType> DamageTypeClass) const
{
	if (!DamageTypeClass)
	{
		return DefaultImpactSound;
	}

	// Check for specific damage types
	if (DamageTypeClass->IsChildOf(UDamageType_Melee::StaticClass()))
	{
		return MeleeImpactSound ? MeleeImpactSound : DefaultImpactSound;
	}
	if (DamageTypeClass->IsChildOf(UDamageType_Ranged::StaticClass()))
	{
		return RangedImpactSound ? RangedImpactSound : DefaultImpactSound;
	}
	if (DamageTypeClass->IsChildOf(UDamageType_EMFWeapon::StaticClass()) ||
		DamageTypeClass->IsChildOf(UDamageType_EMFProximity::StaticClass()))
	{
		return EMFImpactSound ? EMFImpactSound : DefaultImpactSound;
	}

	// Check if it's a radial damage (explosion)
	// UE doesn't have a built-in explosion type, so we use default for now
	// You can add UDamageType_Explosion if needed

	return DefaultImpactSound;
}

// ==================== Melee Knockback ====================

void AShooterCharacter::ApplyMeleeKnockback(const FVector& KnockbackDirection, float Distance, float Duration)
{
	if (Distance < 1.0f || Duration < 0.01f)
	{
		return;
	}

	bIsInKnockback = true;
	KnockbackStartPosition = GetActorLocation();
	KnockbackTargetPosition = KnockbackStartPosition + KnockbackDirection * Distance;
	KnockbackTotalDuration = Duration;
	KnockbackElapsedTime = 0.0f;
}

void AShooterCharacter::UpdateKnockbackInterpolation(float DeltaTime)
{
	if (!bIsInKnockback)
	{
		return;
	}

	KnockbackElapsedTime += DeltaTime;
	float Alpha = FMath::Clamp(KnockbackElapsedTime / KnockbackTotalDuration, 0.0f, 1.0f);

	// Use smooth step for more natural feel
	Alpha = FMath::SmoothStep(0.0f, 1.0f, Alpha);

	FVector NewPosition = FMath::Lerp(KnockbackStartPosition, KnockbackTargetPosition, Alpha);

	// Simple collision check - sweep to new position
	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	float CapsuleRadius = GetCapsuleComponent()->GetScaledCapsuleRadius();
	float CapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	bool bHit = GetWorld()->SweepSingleByChannel(
		Hit,
		GetActorLocation(),
		NewPosition,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight),
		QueryParams
	);

	if (bHit)
	{
		// Stop at wall with small offset
		NewPosition = Hit.Location + Hit.ImpactNormal * 2.0f;
		bIsInKnockback = false; // End knockback on wall hit
	}

	SetActorLocation(NewPosition, false);

	// End knockback when duration complete
	if (KnockbackElapsedTime >= KnockbackTotalDuration)
	{
		bIsInKnockback = false;
	}
}

void AShooterCharacter::CancelKnockback()
{
	if (bIsInKnockback && bKnockbackCancellableByPlayer)
	{
		bIsInKnockback = false;
	}
}

// ==================== Damage Slowdown ====================

void AShooterCharacter::ApplyDamageSlowdown()
{
	if (DamageSlowdownArray.Num() == 0)
	{
		return;
	}

	// Hit count is 1-based (first hit = 1), array is 0-based
	const int32 ArrayIndex = FMath::Min(DamageSlowdownHitCount - 1, DamageSlowdownArray.Num() - 1);
	const float SlowdownValue = DamageSlowdownArray[ArrayIndex];
	const float SpeedReduction = FMath::Clamp(SlowdownValue * DamageSlowdownMultiplier, 0.0f, 1.0f);

	// Instantly cut current velocity
	if (UApexMovementComponent* Apex = GetApexMovement())
	{
		Apex->Velocity *= (1.0f - SpeedReduction);
	}
}

void AShooterCharacter::OnDamageSlowdownTimerExpired()
{
	DamageSlowdownHitCount = 0;
}

// ==================== Chromatic Aberration ====================

void AShooterCharacter::StartChromaticAberrationEffect(float Damage)
{
	// Calculate base intensity from damage (linear, clamped to 0-1)
	ChromaticAberrationBaseIntensity = FMath::Clamp(Damage / MaxDamageForFullChromaticAberration, 0.0f, 1.0f);
	ChromaticAberrationElapsedTime = 0.0f;
	bChromaticAberrationActive = true;
}

void AShooterCharacter::UpdateChromaticAberration(float DeltaTime)
{
	if (!bChromaticAberrationActive)
	{
		return;
	}

	ChromaticAberrationElapsedTime += DeltaTime;

	// Check if effect has finished
	if (ChromaticAberrationElapsedTime >= ChromaticAberrationDuration)
	{
		bChromaticAberrationActive = false;
		// Broadcast final zero intensity
		OnDamageChromaticAberration.Broadcast(0.0f);
		return;
	}

	// Calculate intensity using half sine wave (0 → 1 → 0)
	// sin(t * PI / Duration) where t goes from 0 to Duration
	float Alpha = ChromaticAberrationElapsedTime / ChromaticAberrationDuration;
	float SineMultiplier = FMath::Sin(Alpha * PI);
	float FinalIntensity = ChromaticAberrationBaseIntensity * SineMultiplier;

	// Broadcast current intensity
	OnDamageChromaticAberration.Broadcast(FinalIntensity);
}

// ==================== Health Restoration ====================

void AShooterCharacter::RestoreHealth(float Amount)
{
	if (IsDead() || Amount <= 0.0f)
	{
		return;
	}

	const float OldHP = CurrentHP;
	CurrentHP = FMath::Clamp(CurrentHP + Amount, 0.0f, MaxHP);

	if (CurrentHP != OldHP)
	{
		BroadcastHealthChanged();
	}
}

void AShooterCharacter::ModifyMaxHP(float DeltaMaxHP, bool bHealAddedMaxHP)
{
	if (FMath::IsNearlyZero(DeltaMaxHP))
	{
		return;
	}

	const float OldMaxHP = MaxHP;
	MaxHP = FMath::Max(1.0f, MaxHP + DeltaMaxHP);
	const float ActualDelta = MaxHP - OldMaxHP;

	if (bHealAddedMaxHP && ActualDelta > 0.0f && !IsDead())
	{
		CurrentHP += ActualDelta;
	}

	CurrentHP = FMath::Clamp(CurrentHP, 0.0f, MaxHP);

	BroadcastHealthChanged();
}

// ==================== Health Pickup Objective ====================

void AShooterCharacter::NotifyHealthPickupCollected()
{
	if (!bHealthPickupObjectiveActive)
	{
		return;
	}

	HealthPickupsCollected++;

	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return;
	}

	UTutorialSubsystem* TutorialSub = GI->GetSubsystem<UTutorialSubsystem>();
	if (!TutorialSub)
	{
		return;
	}

	UShooterBulletCounterUI* HUD = TutorialSub->GetHUDWidget();

	if (HealthPickupsCollected >= RequiredHealthPickups)
	{
		// Objective complete
		bHealthPickupObjectiveActive = false;
		TutorialSub->MarkCompleted(HealthPickupObjectiveTutorialID);

		if (HUD)
		{
			HUD->BP_HideHealthPickupObjective();
		}
	}
	else
	{
		// Update progress
		if (HUD)
		{
			HUD->BP_UpdateHealthPickupObjective(HealthPickupsCollected, RequiredHealthPickups);
		}
	}
}

// ==================== Armor Restoration ====================

void AShooterCharacter::RestoreArmor(float Amount)
{
	if (IsDead() || Amount <= 0.0f)
	{
		return;
	}

	CurrentArmor = FMath::Clamp(CurrentArmor + Amount, 0.0f, MaxArmor);

	// Update health/armor listeners
	BroadcastHealthChanged();
}

// ==================== Death ====================

void AShooterCharacter::Die()
{
	// Tell everyone else this one is terminal, so their copy plays the death instead of guessing
	// from HP that looks identical to being downed.
	if (HasAuthority())
	{
		bTerminalDeath = true;
	}
	bHasPlayedLocalDeath = true;

	// Cancel any in-progress ability cast. The character is REUSED on respawn (not destroyed),
	// so AbilityComponent::EndPlay never runs and bIsCasting would carry a stuck cast into the
	// next life — locking firing / ability activation / weapon swap permanently.
	if (AbilityComponent && AbilityComponent->IsCasting())
	{
		AbilityComponent->CancelCast();
	}

	// Same reason as the cast above: the character is reused, so a swap caught mid-animation would
	// carry its phase into the next life and lock firing there. The held trigger goes with it.
	CancelWeaponSwitch();
	bFireHeldThroughSwitch = false;

	// And the grapple's stow, for exactly the same reason and with a nastier failure. The flag is
	// what makes stowing idempotent, so a death on the line would carry a stale "already stowed"
	// into the next life: the next grapple would decline to stow (it thinks it already has) and then
	// unstow at the end, drawing a weapon that never left -- or, worse, the CancelCast above never
	// runs and the player respawns permanently unarmed. CancelCast usually unwinds this properly;
	// this line is here for the times it does not.
	bWeaponStowedForGrapple = false;

	// And the two-frame hold on the arms, for the same reused-character reason: a death landing
	// inside that window would carry a non-zero count into the next life, where nothing counts it
	// down until the next grapple and the arms stay hidden meanwhile.
	FirstPersonRevealFramesLeft = 0;

	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->DeactivateWeapon();
	}

	// Hide the crosshair on death (treat as unarmed until respawn re-equips a weapon).
	OnActiveWeaponChanged.Broadcast(nullptr);

	if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->IncrementTeamScore(TeamByte);
	}

	GetCharacterMovement()->StopMovementImmediately();
	DisableInput(nullptr);
	OnBulletCountUpdated.Broadcast(0, 0);

	// Stop any looping sounds
	StopSlideLoopSound();
	StopWallRunLoopSound();

	BP_OnDeath();

	// A death during an active roguelite run is terminal, but in coop it is terminal for the *team*,
	// not for whoever fell first. See ShouldRunEndOnThisDeath: the run only ends once nobody is left
	// standing, and only the server may decide it, because only the server can see the whole team.
	//
	// The presentation below still plays wherever this runs, so a fallen player watches their own
	// death; what they no longer do is take everybody else back to the menu with them.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (URunSubsystem* Run = GI->GetSubsystem<URunSubsystem>(); Run && Run->IsRunActive())
		{
			const bool bRunIsOver = ShouldRunEndOnThisDeath();
			if (bRunIsOver)
			{
				Run->EndRun(ERunEndReason::PlayerDeath);
			}

			if (PlayerDeathSequenceComponent && PlayerDeathSequenceComponent->StartDeathSequence())
			{
				// The launched camera now sees the world mesh, so remove all first-person presentation.
				if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
				{
					FPMesh->SetVisibility(false, true);
				}
				if (CurrentWeapon)
				{
					CurrentWeapon->SetActorHiddenInGame(true);
				}

				const float SequenceDuration = PlayerDeathSequenceComponent->GetTotalDuration();
				if (bRunIsOver)
				{
					UE_LOG(LogTemp, Log, TEXT("[RUN_FLOW] Player death sequence started -> menu after %.2fs"),
						SequenceDuration);
					GetWorldTimerManager().SetTimer(
						RespawnTimer, this, &AShooterCharacter::ReturnToMainMenuAfterRunDeath,
						FMath::Max(0.01f, SequenceDuration), false);
				}
				return;
			}

			// Safe fallback when the component is disabled or could not start.
			if (APlayerController* PC = Cast<APlayerController>(GetController()))
			{
				if (PC->PlayerCameraManager)
				{
					PC->PlayerCameraManager->StartCameraFade(
						0.0f, 1.0f, DeathFadeOutDuration, DeathFadeColor, false, true);
				}
			}
			if (bRunIsOver)
			{
				UE_LOG(LogTemp, Log, TEXT("[RUN_FLOW] Death sequence unavailable -> fallback menu fade %.2fs"),
					DeathFadeOutDuration);
				GetWorldTimerManager().SetTimer(
					RespawnTimer, this, &AShooterCharacter::ReturnToMainMenuAfterRunDeath,
					FMath::Max(0.01f, DeathFadeOutDuration), false);
			}
			return;
		}
	}

	// Non-run deaths retain the existing checkpoint/respawn behavior.
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StartCameraFade(0.0f, 1.0f, DeathFadeOutDuration, DeathFadeColor, false, true);
		}
	}

	GetWorld()->GetTimerManager().SetTimer(RespawnTimer, this, &AShooterCharacter::OnRespawn, RespawnTime, false);
}

bool AShooterCharacter::ShouldRunEndOnThisDeath() const
{
	// Only the server may answer this. CoopPlayers::GetAll walks the player controller list, and on
	// a client the engine only keeps the local one, so a client asking "is anyone else alive" would
	// always hear "no" and take the whole team back to the menu on its own death.
	if (!HasAuthority())
	{
		return false;
	}

	TArray<APawn*> Players;
	CoopPlayers::GetAll(GetWorld(), Players);

	for (const APawn* Player : Players)
	{
		if (Player == this)
		{
			continue;
		}

		const AShooterCharacter* Teammate = Cast<AShooterCharacter>(Player);
		if (Teammate && !Teammate->IsDead())
		{
			UE_LOG(LogTemp, Log, TEXT("[RUN_FLOW] %s died, but %s is still up: the run continues"),
				*GetName(), *Teammate->GetName());
			return false;
		}
	}

	// Nobody left standing. This is the death that ends it, and the server travel below takes
	// everyone to the menu together.
	return true;
}

void AShooterCharacter::ReturnToMainMenuAfterRunDeath()
{
	if (MainMenuLevel.IsNull())
	{
		UE_LOG(LogTemp, Error, TEXT("[RUN_FLOW] Cannot return to menu: assign MainMenuLevel on the player Blueprint"));
		return;
	}

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetShowMouseCursor(true);
		PC->SetInputMode(FInputModeUIOnly());
	}

	UE_LOG(LogTemp, Log, TEXT("[RUN_FLOW] Opening main menu map %s"), *MainMenuLevel.ToSoftObjectPath().ToString());
	UGameplayStatics::OpenLevelBySoftObjectPtr(this, MainMenuLevel);
}

void AShooterCharacter::OnRespawn()
{
	// Try to respawn at checkpoint first
	if (UCheckpointSubsystem* CheckpointSubsystem = GetWorld()->GetSubsystem<UCheckpointSubsystem>())
	{
		if (CheckpointSubsystem->HasActiveCheckpoint())
		{
			if (CheckpointSubsystem->RespawnAtCheckpoint(this))
			{
				return; // Successfully respawned at checkpoint
			}
		}
	}

	// No checkpoint or respawn failed - destroy and let GameMode handle it
	Destroy();
}

bool AShooterCharacter::SaveToCheckpoint(FCheckpointData& OutData)
{
	// Health
	OutData.Health = CurrentHP;

	// Armor
	OutData.Armor = CurrentArmor;

	// EMF - save base charge (0 for neutral, not bonus charge)
	// Per requirements: reset bonus charge, keep base
	OutData.BaseEMFCharge = 0.0f; // Player spawns neutral

	// Weapon state
	int32 CurrentWeaponIdx = OwnedWeapons.IndexOfByKey(CurrentWeapon);
	OutData.CurrentWeaponIndex = (CurrentWeaponIdx != INDEX_NONE) ? CurrentWeaponIdx : 0;

	// Save ammo for all weapons
	OutData.WeaponAmmo.Empty();
	for (int32 i = 0; i < OwnedWeapons.Num(); ++i)
	{
		if (AShooterWeapon* Weapon = OwnedWeapons[i])
		{
			OutData.WeaponAmmo.Add(i, Weapon->GetBulletCount());
		}
	}

	// Save acquired upgrades
	if (UpgradeManager)
	{
		OutData.AcquiredUpgrades = UpgradeManager->GetUpgradeTagsForSave();
	}

	return true;
}

bool AShooterCharacter::RestoreFromCheckpoint(const FCheckpointData& Data)
{
	if (!Data.bIsValid)
	{
		return false;
	}

	// Reset character state first
	ResetCharacterState();

	// Teleport to spawn point and set view rotation
	SetActorTransform(Data.SpawnTransform);

	// Set controller rotation to match checkpoint direction
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		FRotator SpawnRotation = Data.SpawnTransform.GetRotation().Rotator();
		PC->SetControlRotation(SpawnRotation);
	}

	// Restore health to maximum on respawn
	CurrentHP = MaxHP;

	// Restore armor
	CurrentArmor = Data.Armor;

	// Update listeners with both HP and armor
	BroadcastHealthChanged();

	// Restore EMF charge (reset to base/neutral)
	CurrentCharge = Data.BaseEMFCharge;
	// Calculate polarity byte: 0=neutral, 1=positive, 2=negative
	uint8 RestoredPolarity = 0;
	if (CurrentCharge > 0.01f)
	{
		RestoredPolarity = 1; // Positive
	}
	else if (CurrentCharge < -0.01f)
	{
		RestoredPolarity = 2; // Negative
	}
	OnChargeUpdated.Broadcast(CurrentCharge, RestoredPolarity);

	// Restore weapon
	if (OwnedWeapons.IsValidIndex(Data.CurrentWeaponIndex))
	{
		// Deactivate current weapon if different
		if (CurrentWeapon && CurrentWeapon != OwnedWeapons[Data.CurrentWeaponIndex])
		{
			CurrentWeapon->DeactivateWeapon();
		}

		AShooterWeapon* OldWeapon = CurrentWeapon;
		CurrentWeapon = OwnedWeapons[Data.CurrentWeaponIndex];
		if (CurrentWeapon)
		{
			CurrentWeapon->ActivateWeapon();
		}

		// Notify upgrade system about restored weapon
		if (UpgradeManager && OldWeapon != CurrentWeapon)
		{
			UpgradeManager->NotifyWeaponChanged(OldWeapon, CurrentWeapon);
		}
	}

	// Restore ammo
	for (const auto& AmmoPair : Data.WeaponAmmo)
	{
		if (OwnedWeapons.IsValidIndex(AmmoPair.Key))
		{
			if (AShooterWeapon* Weapon = OwnedWeapons[AmmoPair.Key])
			{
				Weapon->SetBulletCount(AmmoPair.Value);
			}
		}
	}

	// Re-enable input and reset camera
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		EnableInput(PC);

		// Reset view target back to this character (in case death camera was active)
		PC->SetViewTarget(this);

		// Fade in from black
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StartCameraFade(1.0f, 0.0f, RespawnFadeInDuration, DeathFadeColor, false, false);
		}
	}

	// Update UI
	if (CurrentWeapon)
	{
		OnBulletCountUpdated.Broadcast(CurrentWeapon->GetMagazineSize(), CurrentWeapon->GetBulletCount());
	}

	// Restore upgrades
	if (UpgradeManager && UpgradeRegistry)
	{
		UpgradeManager->RestoreUpgradesFromTags(Data.AcquiredUpgrades, UpgradeRegistry);
	}

	// Blueprint event (use this to reset any death-related visual effects)
	BP_OnRespawnAtCheckpoint();

	return true;
}

void AShooterCharacter::ResetCharacterState()
{
	// Stop all movement
	if (UCharacterMovementComponent* MovementComp = GetCharacterMovement())
	{
		MovementComp->StopMovementImmediately();
		MovementComp->Velocity = FVector::ZeroVector;

		// Reset movement mode to walking (in case we died mid-air or in weird state)
		MovementComp->SetMovementMode(MOVE_Walking);
	}

	// Reset apex movement state
	if (UApexMovementComponent* Apex = GetApexMovement())
	{
		Apex->ResetMovementState();
	}

	// Clear respawn timer
	GetWorld()->GetTimerManager().ClearTimer(RespawnTimer);

	// Reset regen delay (allow immediate regeneration)
	TimeSinceLastDamage = RegenDelayAfterDamage;

	// Stop looping sounds
	StopSlideLoopSound();
	StopWallRunLoopSound();

	// Reset mesh visibility and transforms (in case death animation modified them)
	if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
	{
		FPMesh->SetVisibility(true);
		FPMesh->SetRelativeLocation(FirstPersonMeshBaseLocation);
		FPMesh->SetRelativeRotation(FirstPersonMeshBaseRotation);
	}

	// Reset third person mesh if visible
	if (GetMesh())
	{
		GetMesh()->SetVisibility(true);
		GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	// Reactivate weapon if needed
	if (CurrentWeapon)
	{
		CurrentWeapon->ActivateWeapon();
	}
}

void AShooterCharacter::SetFPMontageAlpha(float Target, float BlendTime)
{
	// Deprecated no-op — see the header. The FP mesh is parented to the camera, so two-hand
	// montages follow the view on their own; there is no Control Rig left to blend out.
}

// True while Montage is playing on Mesh, whichever mesh that is.
static bool IsMontagePlayingOnMesh(const USkeletalMeshComponent* Mesh, UAnimMontage* Montage)
{
	if (!Mesh || !Montage)
	{
		return false;
	}

	const UAnimInstance* AnimInstance = Mesh->GetAnimInstance();
	return AnimInstance && AnimInstance->Montage_IsPlaying(Montage);
}

void AShooterCharacter::UpdateLeftHandIK(float DeltaTime)
{
	// Determine target alpha based on state
	bool bIsWallRunning = false;
	if (UApexMovementComponent* Apex = GetApexMovement())
	{
		bIsWallRunning = Apex->IsWallRunning();
	}

	// Riot shield equipped: force alpha=0 — left hand is busy holding the shield, so the AnimBP
	// should drive it via animation rather than IK-pinning it to the weapon's left-hand socket.
	// (Same convention as wallrun and charging-channel: alpha=0 = "hand free of weapon".)
	if (HasShield())
	{
		TargetLeftHandIKAlpha = 0.0f;
	}
	// Wallrun forces alpha=0 (hand free of weapon while running on wall).
	// Otherwise default to 1.0, UNLESS ChargeAnimationComponent has authority over alpha
	// (during channeling its SetLeftHandIKAlpha(0) sets alpha=0 to free left arm for
	// catch/hold/throw montages — don't clobber it every Tick).
	else if (bIsWallRunning)
	{
		TargetLeftHandIKAlpha = 0.0f;
	}
	// Reload ANIMATION (not the reload state): the montage animates both hands, one of them on the
	// magazine, and IK pinning the left hand to the grip socket fights it the whole way. Keyed to
	// the montage actually playing, so a weapon with no reload animation never unpins anything and
	// the hand is released for exactly as long as something is moving it.
	// The body is checked as well as the arms, because on ANOTHER player's machine only the body
	// has the montage: the first person mesh is this player's own and nobody else animates it.
	// Without it a teammate reloaded with their off hand still welded to the grip.
	else if (IsPlayingReloadAnimation()
		|| (CurrentWeapon && IsMontagePlayingOnMesh(GetMesh(), CurrentWeapon->GetReloadMontage())))
	{
		TargetLeftHandIKAlpha = 0.0f;
	}
	// Yank-throw montage: force alpha=0 for its whole duration — frees the left hand from
	// the weapon-grip IK so the montage can animate both hands. Auto-clears when the montage
	// ends, restoring the default alpha=1.
	else if (PendingYankThrowWeapon.IsValid() &&
		IsYankThrowMontageActiveOnFPMesh(ChargeAnimationComponent, GetFirstPersonMesh()))
	{
		TargetLeftHandIKAlpha = 0.0f;
	}
	else
	{
		const UChargeAnimationComponent* ChargeComp = FindComponentByClass<UChargeAnimationComponent>();
		const UAbilityComponent* AbilComp = FindComponentByClass<UAbilityComponent>();
		const bool bChargingOwnsAlpha = ChargeComp && ChargeComp->IsChanneling();
		const bool bAbilityOwnsAlpha = AbilComp && AbilComp->IsCasting();
		if (!bChargingOwnsAlpha && !bAbilityOwnsAlpha)
		{
			TargetLeftHandIKAlpha = 1.0f;
		}
		// else: leave whatever ChargeAnimationComponent or AbilityComponent has set
	}

	// Interpolate alpha
	CurrentLeftHandIKAlpha = FMath::FInterpTo(
		CurrentLeftHandIKAlpha,
		TargetLeftHandIKAlpha,
		DeltaTime,
		LeftHandIKAlphaInterpSpeed
	);

	// Get socket transform from weapon mesh (if available)
	FTransform FinalTransform = FTransform::Identity;

	if (CurrentWeapon)
	{
		if (USkeletalMeshComponent* WeaponMesh = CurrentWeapon->GetFirstPersonMesh())
		{
			if (WeaponMesh->DoesSocketExist(LeftHandGripSocket))
			{
				FTransform SocketTransform = WeaponMesh->GetSocketTransform(LeftHandGripSocket, ERelativeTransformSpace::RTS_World);
				FinalTransform = LeftHandIKOffset * SocketTransform;
			}
		}
	}

	// Always pass the interpolated alpha value
	SetAnimInstanceLeftHandIK(FinalTransform, CurrentLeftHandIKAlpha);

	// Same treatment for the body everyone else sees, so a teammate's off hand sits on the weapon
	// instead of floating beside it.
	//
	// Alpha is forced to zero when there is nothing to hold on to, rather than passing the
	// interpolated value with an identity transform, which would drag the hand to the world origin.
	if (USkeletalMeshComponent* TPMesh = GetMesh())
	{
		FTransform TPTransform = FTransform::Identity;
		float TPAlpha = 0.0f;

		if (CurrentWeapon)
		{
			if (USkeletalMeshComponent* WeaponTPMesh = CurrentWeapon->GetThirdPersonMesh())
			{
				// A weapon may place the off hand differently on the body than on camera.
				const FName TPGripSocket =
					AShooterWeapon::PickThirdPersonSocket(WeaponTPMesh, LeftHandGripSocket);
				if (WeaponTPMesh->DoesSocketExist(TPGripSocket))
				{
					TPTransform = LeftHandIKOffset *
						WeaponTPMesh->GetSocketTransform(TPGripSocket, ERelativeTransformSpace::RTS_World);
					TPAlpha = CurrentLeftHandIKAlpha;
				}
			}
		}

		AShooterWeapon::PushLeftHandIK(TPMesh->GetAnimInstance(), TPTransform, TPAlpha);
	}
}

void AShooterCharacter::SetAnimInstanceLeftHandIK(const FTransform& Transform, float Alpha)
{
	USkeletalMeshComponent* FPMesh = GetFirstPersonMesh();
	if (!FPMesh)
	{
		//UE_LOG(LogTemp, Warning, TEXT("LeftHandIK: No FPMesh!"));
		return;
	}

	UAnimInstance* AnimInstance = FPMesh->GetAnimInstance();
	if (!AnimInstance)
	{
		//UE_LOG(LogTemp, Warning, TEXT("LeftHandIK: No AnimInstance!"));
		return;
	}

	// The write itself is shared with the third person path and with NPCs, so it lives on
	// AShooterWeapon.
	AShooterWeapon::PushLeftHandIK(AnimInstance, Transform, Alpha);
}

// ==================== New Movement SFX/VFX Handlers ====================

void AShooterCharacter::OnJumpPerformed_Handler(bool bIsDoubleJump)
{
	CancelKnockback(); // Player action cancels knockback

	// Play jump sound
	PlayJumpSound(bIsDoubleJump);

	// Spawn double jump VFX if this is a double jump
	if (bIsDoubleJump)
	{
		SpawnDoubleJumpVFX();
	}
}

void AShooterCharacter::OnMantleStarted_Handler()
{
	PlayMantleSound();
}

void AShooterCharacter::OnAirDashStarted_Handler()
{
	CancelKnockback(); // Player action cancels knockback
	PlayAirDashSound();
	StartAirDashTrailVFX();
}

void AShooterCharacter::OnAirDashEnded_Handler()
{
	StopAirDashTrailVFX();
}

void AShooterCharacter::PlayAirDashSound()
{
	if (AirDashSound)
	{
		const float Pitch = FMath::RandRange(AirDashSoundPitchMin, AirDashSoundPitchMax);
		UGameplayStatics::PlaySoundAtLocation(
			this,
			AirDashSound,
			GetActorLocation(),
			AirDashSoundVolume,
			Pitch
		);
	}
}

void AShooterCharacter::PlayMantleSound()
{
	if (MantleSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			MantleSound,
			GetActorLocation(),
			MantleSoundVolume
		);
	}
}

void AShooterCharacter::PlayWeaponSwitchSound()
{
	if (WeaponSwitchSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			WeaponSwitchSound,
			GetActorLocation(),
			WeaponSwitchSoundVolume
		);
	}
}

void AShooterCharacter::UpdateLowHealthWarning(float DeltaTime)
{
	const float HealthPercent = CurrentHP / MaxHP;
	const bool bShouldBeInLowHealth = HealthPercent < LowHealthThreshold && HealthPercent > 0.0f;

	if (bShouldBeInLowHealth)
	{
		if (!bIsLowHealth)
		{
			// Just entered low health state - play warning immediately
			bIsLowHealth = true;
			LowHealthWarningTimer = 0.0f;

			// Start health pickup objective on first low-health event
			if (!bHealthPickupObjectiveActive && !HealthPickupObjectiveTutorialID.IsNone())
			{
				if (UGameInstance* GI = GetGameInstance())
				{
					if (UTutorialSubsystem* TutorialSub = GI->GetSubsystem<UTutorialSubsystem>())
					{
						if (!TutorialSub->IsCompleted(HealthPickupObjectiveTutorialID))
						{
							bHealthPickupObjectiveActive = true;
							HealthPickupsCollected = 0;

							if (UShooterBulletCounterUI* HUD = TutorialSub->GetHUDWidget())
							{
								HUD->BP_ShowHealthPickupObjective(RequiredHealthPickups);
							}
						}
					}
				}
			}

			// Play the warning ONCE on entering low health. Deliberately NOT looped — the
			// bIsLowHealth latch (cleared only in the else-branch below, when HP recovers
			// above the threshold) re-arms this one-shot, so it won't nag while the player
			// stays low. LowHealthWarningInterval / LowHealthWarningTimer are now unused but
			// kept to avoid a header change / Live-Coding break.
			if (LowHealthWarningSound)
			{
				UGameplayStatics::PlaySound2D(this, LowHealthWarningSound, LowHealthWarningVolume);
			}
		}
	}
	else
	{
		// Recovered above the threshold — re-arm the one-shot warning for the next episode.
		bIsLowHealth = false;
		LowHealthWarningTimer = 0.0f;
	}
}

void AShooterCharacter::UpdatePostProcessEffects(float DeltaTime)
{
	// Calculate target intensities
	const float HealthPercent = CurrentHP / MaxHP;
	const float TargetLowHealthIntensity = (HealthPercent < LowHealthThreshold && HealthPercent > 0.0f)
		? FMath::GetMappedRangeValueClamped(FVector2D(0.0f, LowHealthThreshold), FVector2D(1.0f, 0.0f), HealthPercent)
		: 0.0f;

	const float CurrentSpeed = GetVelocity().Size();
	const float TargetHighSpeedIntensity = (CurrentSpeed > HighSpeedThreshold)
		? FMath::GetMappedRangeValueClamped(FVector2D(HighSpeedThreshold, HighSpeedMaxThreshold), FVector2D(0.0f, 1.0f), CurrentSpeed)
		: 0.0f;

	// Interpolate current values
	CurrentLowHealthPPIntensity = FMath::FInterpTo(CurrentLowHealthPPIntensity, TargetLowHealthIntensity, DeltaTime, PPInterpSpeed);
	CurrentHighSpeedPPIntensity = FMath::FInterpTo(CurrentHighSpeedPPIntensity, TargetHighSpeedIntensity, DeltaTime, PPInterpSpeed);

	// Apply to materials
	if (LowHealthPPMaterial)
	{
		LowHealthPPMaterial->SetScalarParameterValue(PPIntensityParameterName, CurrentLowHealthPPIntensity);
	}

	if (HighSpeedPPMaterial)
	{
		HighSpeedPPMaterial->SetScalarParameterValue(PPIntensityParameterName, CurrentHighSpeedPPIntensity);
	}
}

void AShooterCharacter::NotifyTurretTargeting(AActor* Turret, float Progress, bool bIsActive)
{
	if (!Turret)
	{
		return;
	}

	// Drop any entries for turrets that disappeared without calling disengage
	ActiveAimingTurrets.RemoveAll([](const FTurretAimInfo& Info)
	{
		return !IsValid(Info.Turret);
	});

	const int32 Index = ActiveAimingTurrets.IndexOfByPredicate([Turret](const FTurretAimInfo& Info)
	{
		return Info.Turret == Turret;
	});

	if (bIsActive)
	{
		if (Index != INDEX_NONE)
		{
			ActiveAimingTurrets[Index].Progress = Progress;
		}
		else
		{
			FTurretAimInfo Info;
			Info.Turret = Turret;
			Info.Progress = Progress;
			ActiveAimingTurrets.Add(Info);
		}
	}
	else if (Index != INDEX_NONE)
	{
		ActiveAimingTurrets.RemoveAt(Index);
	}

	OnTargetedByTurret.Broadcast(ActiveAimingTurrets);
}

void AShooterCharacter::SpawnDoubleJumpVFX()
{
	if (DoubleJumpFX)
	{
		const FVector SpawnLocation = GetActorLocation() - FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());

		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(),
			DoubleJumpFX,
			SpawnLocation,
			GetActorRotation(),
			FVector(DoubleJumpFXScale),
			true,
			true,
			ENCPoolMethod::AutoRelease
		);
	}
}

void AShooterCharacter::StartAirDashTrailVFX()
{
	if (AirDashTrailFX && !ActiveAirDashTrailComponent)
	{
		ActiveAirDashTrailComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(
			AirDashTrailFX,
			GetRootComponent(),
			NAME_None,
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			EAttachLocation::KeepRelativeOffset,
			true
		);
	}
}

void AShooterCharacter::StopAirDashTrailVFX()
{
	if (ActiveAirDashTrailComponent)
	{
		ActiveAirDashTrailComponent->Deactivate();
		ActiveAirDashTrailComponent = nullptr;
	}
}

// ==================== Boss Finisher Implementation ====================

void AShooterCharacter::StartBossFinisher()
{
	if (bBossFinisherActive)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("BossFinisher: Starting finisher sequence"));

	bBossFinisherActive = true;
	BossFinisherPhase = EBossFinisherPhase::CurveMovement;
	BossFinisherElapsedTime = 0.0f;
	BossFinisherStartPosition = GetActorLocation();

	// Setup Bezier curve
	SetupBezierCurve();

	// Stop any current weapon firing
	if (CurrentWeapon)
	{
		CurrentWeapon->StopFiring();
	}

	// Lower weapon immediately (will skip lowering phase when attack starts later)
	if (MeleeAttackComponent)
	{
		MeleeAttackComponent->LowerWeapon();
	}

	// Disable gravity and movement input
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->GravityScale = 0.0f;
		Movement->Velocity = FVector::ZeroVector;
		Movement->SetMovementMode(MOVE_Flying);
	}

	// Disable player input (movement)
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		// We don't disable input completely - we still want camera control during most phases
		// But movement is handled by the finisher system
	}

	// Broadcast start event
	OnBossFinisherStarted.Broadcast();
}

void AShooterCharacter::StopBossFinisher()
{
	if (!bBossFinisherActive)
	{
		return;
	}

	EndBossFinisher();
}

void AShooterCharacter::SetupBezierCurve()
{
	// P0 = Start position (player current location)
	BezierP0 = BossFinisherStartPosition;

	// P3 = Target position
	BezierP3 = BossFinisherSettings.TargetPoint;

	// Calculate approach point (where the "straight line" phase begins)
	// ApproachOffset is relative to target - we want player to come FROM this direction
	FVector ApproachPoint = BezierP3 + BossFinisherSettings.ApproachOffset;

	// P1 = Control point near start - creates initial curve away from direct path
	// Place it roughly 1/3 of the way, but offset to create the curve shape
	FVector StartToApproach = ApproachPoint - BezierP0;
	FVector StartToTarget = BezierP3 - BezierP0;

	// P1 creates the "swing out" at the beginning
	// Cross product gives us perpendicular direction for the curve
	FVector CurveDirection = FVector::CrossProduct(StartToTarget.GetSafeNormal(), FVector::UpVector);
	if (CurveDirection.IsNearlyZero())
	{
		CurveDirection = FVector::RightVector;
	}
	CurveDirection.Normalize();

	// Add some height and lateral offset for dramatic curve
	BezierP1 = BezierP0 + StartToTarget * 0.33f + CurveDirection * StartToTarget.Size() * 0.3f + FVector(0, 0, 200.0f);

	// P2 = Control point near approach point - creates the "diving in" feel
	// This should be near the approach point but pulled toward P3
	BezierP2 = ApproachPoint + (BezierP3 - ApproachPoint) * 0.3f;

	UE_LOG(LogTemp, Warning, TEXT("BossFinisher: Bezier curve setup - P0: %s, P1: %s, P2: %s, P3: %s"),
		*BezierP0.ToString(), *BezierP1.ToString(), *BezierP2.ToString(), *BezierP3.ToString());
}

FVector AShooterCharacter::EvaluateBezierCurve(float T) const
{
	// Cubic Bezier: B(t) = (1-t)^3*P0 + 3*(1-t)^2*t*P1 + 3*(1-t)*t^2*P2 + t^3*P3
	float OneMinusT = 1.0f - T;
	float OneMinusT2 = OneMinusT * OneMinusT;
	float OneMinusT3 = OneMinusT2 * OneMinusT;
	float T2 = T * T;
	float T3 = T2 * T;

	return OneMinusT3 * BezierP0 +
		   3.0f * OneMinusT2 * T * BezierP1 +
		   3.0f * OneMinusT * T2 * BezierP2 +
		   T3 * BezierP3;
}

void AShooterCharacter::UpdateBossFinisher(float DeltaTime)
{
	BossFinisherElapsedTime += DeltaTime;

	const float TotalTime = BossFinisherSettings.TotalTravelTime;
	const float StraightenTime = BossFinisherSettings.StraightenTime;
	const float AnimStartTime = BossFinisherSettings.AnimationStartTime;
	const float HangTime = BossFinisherSettings.HangTime;

	// Calculate time remaining until reaching target
	float TimeRemaining = TotalTime - BossFinisherElapsedTime;

	// Always focus camera on target point during finisher
	// Add 150 unit offset along approach direction to prevent 180 flip when passing through target
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		// Calculate camera focus point with offset
		// Offset is 150 units from TargetPoint in the direction from ApproachOffset toward TargetPoint
		FVector ApproachPoint = BossFinisherSettings.TargetPoint + BossFinisherSettings.ApproachOffset;
		FVector ApproachDirection = (BossFinisherSettings.TargetPoint - ApproachPoint).GetSafeNormal();
		FVector CameraFocusPoint = BossFinisherSettings.TargetPoint + ApproachDirection * 150.0f;

		FVector ToTarget = CameraFocusPoint - GetActorLocation();
		FRotator TargetRotation = ToTarget.Rotation();
		FRotator CurrentRotation = PC->GetControlRotation();

		// Smooth interpolation to target
		FRotator NewRotation = FMath::RInterpTo(CurrentRotation, TargetRotation, DeltaTime, 10.0f);
		PC->SetControlRotation(NewRotation);
	}

	switch (BossFinisherPhase)
	{
	case EBossFinisherPhase::CurveMovement:
		{
			// Check if we should transition to linear movement
			if (TimeRemaining <= StraightenTime)
			{
				BossFinisherPhase = EBossFinisherPhase::LinearMovement;
				LinearStartPosition = GetActorLocation();
				LinearStartTime = BossFinisherElapsedTime;
				UE_LOG(LogTemp, Warning, TEXT("BossFinisher: Transitioning to LinearMovement"));
				break;
			}

			// Calculate T parameter for Bezier (0 to 1 during curve phase)
			// Curve phase runs from 0 to (TotalTime - StraightenTime)
			float CurvePhaseTime = TotalTime - StraightenTime;
			float LinearT = FMath::Clamp(BossFinisherElapsedTime / CurvePhaseTime, 0.0f, 1.0f);

			// Apply EaseIn (quadratic) - slow start, accelerating toward end
			// T^2 gives nice acceleration curve
			float T = LinearT * LinearT;

			FVector NewPosition = EvaluateBezierCurve(T);
			SetActorLocation(NewPosition);

			// Rotate character to face movement direction
			FVector Velocity = EvaluateBezierCurve(FMath::Min(T + 0.01f, 1.0f)) - NewPosition;
			if (!Velocity.IsNearlyZero())
			{
				SetActorRotation(FRotator(0, Velocity.Rotation().Yaw, 0));
			}
		}
		break;

	case EBossFinisherPhase::LinearMovement:
		{
			// Check if we should start animation
			if (TimeRemaining <= AnimStartTime && BossFinisherPhase != EBossFinisherPhase::Animation)
			{
				BossFinisherPhase = EBossFinisherPhase::Animation;
				StartBossFinisherAnimation();
				UE_LOG(LogTemp, Warning, TEXT("BossFinisher: Starting animation phase"));
				// Fall through to animation phase
			}
			else if (TimeRemaining <= 0)
			{
				// Reached target - start hanging
				BossFinisherPhase = EBossFinisherPhase::Hanging;
				BossFinisherElapsedTime = 0.0f; // Reset for hang timer
				SetActorLocation(BossFinisherSettings.TargetPoint);
				UE_LOG(LogTemp, Warning, TEXT("BossFinisher: Reached target, starting hang phase"));
				break;
			}
			else
			{
				// Linear interpolation to target with EaseIn for acceleration effect
				float LinearPhaseTime = StraightenTime;
				float LinearElapsed = BossFinisherElapsedTime - LinearStartTime;
				float LinearAlpha = FMath::Clamp(LinearElapsed / LinearPhaseTime, 0.0f, 1.0f);

				// EaseIn (quadratic) - continues the acceleration from curve phase
				float Alpha = LinearAlpha * LinearAlpha;

				FVector NewPosition = FMath::Lerp(LinearStartPosition, BossFinisherSettings.TargetPoint, Alpha);
				SetActorLocation(NewPosition);

				// Face target
				FVector ToTarget = BossFinisherSettings.TargetPoint - NewPosition;
				if (!ToTarget.IsNearlyZero())
				{
					SetActorRotation(FRotator(0, ToTarget.Rotation().Yaw, 0));
				}
			}
		}
		break;

	case EBossFinisherPhase::Animation:
		{
			// Continue moving to target while animating
			if (TimeRemaining <= 0)
			{
				// Reached target - start hanging
				BossFinisherPhase = EBossFinisherPhase::Hanging;
				BossFinisherElapsedTime = 0.0f; // Reset for hang timer
				SetActorLocation(BossFinisherSettings.TargetPoint);
				UE_LOG(LogTemp, Warning, TEXT("BossFinisher: Reached target during animation, starting hang phase"));
			}
			else
			{
				// Continue linear movement with EaseIn acceleration
				float LinearPhaseTime = StraightenTime;
				float LinearElapsed = BossFinisherElapsedTime - LinearStartTime;
				float LinearAlpha = FMath::Clamp(LinearElapsed / LinearPhaseTime, 0.0f, 1.0f);

				// EaseIn (quadratic) - accelerates toward target
				float Alpha = LinearAlpha * LinearAlpha;

				FVector NewPosition = FMath::Lerp(LinearStartPosition, BossFinisherSettings.TargetPoint, Alpha);
				SetActorLocation(NewPosition);
			}
		}
		break;

	case EBossFinisherPhase::Hanging:
		{
			// Stay at target point
			SetActorLocation(BossFinisherSettings.TargetPoint);

			if (BossFinisherElapsedTime >= HangTime)
			{
				BossFinisherPhase = EBossFinisherPhase::Falling;

				// Re-enable gravity
				if (UCharacterMovementComponent* Movement = GetCharacterMovement())
				{
					Movement->GravityScale = MovementSettings ? MovementSettings->DefaultGravityScale : 1.5f;
					Movement->SetMovementMode(MOVE_Falling);
				}

				UE_LOG(LogTemp, Warning, TEXT("BossFinisher: Hang complete, starting fall"));
			}
		}
		break;

	case EBossFinisherPhase::Falling:
		{
			// Check if landed
			if (UCharacterMovementComponent* Movement = GetCharacterMovement())
			{
				if (Movement->IsMovingOnGround())
				{
					EndBossFinisher();
					UE_LOG(LogTemp, Warning, TEXT("BossFinisher: Landed, finisher complete"));
				}
			}
		}
		break;

	default:
		break;
	}
}

void AShooterCharacter::StartBossFinisherAnimation()
{
	// Use MeleeAttackComponent's air attack animation
	if (MeleeAttackComponent)
	{
		// Temporarily set movement mode to Falling so MeleeAttackComponent
		// uses AirborneAttack animation instead of Ground
		if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		{
			Movement->SetMovementMode(MOVE_Falling);
		}

		// Trigger the air attack animation through melee component
		// This will apply all the mesh offsets, hidden bones, etc. from AirborneAttack settings
		MeleeAttackComponent->StartAttack();

		// Return to Flying for controlled movement
		if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		{
			Movement->SetMovementMode(MOVE_Flying);
		}
	}
}

void AShooterCharacter::EndBossFinisher()
{
	UE_LOG(LogTemp, Warning, TEXT("BossFinisher: Ending finisher sequence"));

	bBossFinisherActive = false;
	BossFinisherPhase = EBossFinisherPhase::None;
	bIsOnBossFinisher = false; // Reset flag so it needs to be set again for next finisher

	// Restore normal movement
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->GravityScale = MovementSettings ? MovementSettings->DefaultGravityScale : 1.5f;
		if (!Movement->IsMovingOnGround())
		{
			Movement->SetMovementMode(MOVE_Falling);
		}
		else
		{
			Movement->SetMovementMode(MOVE_Walking);
		}
	}

	// Broadcast end event
	OnBossFinisherEnded.Broadcast();
}

// ==================== Cinematic Finisher (Level Sequence) ====================

void AShooterCharacter::BeginFinisherCinematic()
{
	UE_LOG(LogTemp, Warning, TEXT("[FINISHER] BeginFinisherCinematic — hiding player, locking input"));

	// Stop firing and hide the first-person body + weapon; the sequence shows a double.
	if (CurrentWeapon)
	{
		CurrentWeapon->StopFiring();
		CurrentWeapon->SetActorHiddenInGame(true);
	}
	if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
	{
		FPMesh->SetVisibility(false, /*bPropagateToChildren=*/ true);
	}

	// Lock input — the cine camera (Camera Cuts) owns the view during the finisher.
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->StopMovementImmediately();
	}
	DisableInput(nullptr);

	// Kill ALL camera animation — the cine camera (Camera Cuts) owns the view during the finisher,
	// so any procedural bob / shake / focus would only fight it and cause the jitter on entry.
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StopAllCameraShakes(true);
		}
	}
	if (UCameraShakeComponent* Shake = GetCameraShake())
	{
		Shake->SetComponentTickEnabled(false);
	}
}

void AShooterCharacter::EndFinisherCinematic(FVector ExitLocation)
{
	UE_LOG(LogTemp, Warning, TEXT("[FINISHER] EndFinisherCinematic — teleport + reveal + fade in"));

	// Restore the camera animation disabled in BeginFinisherCinematic.
	if (UCameraShakeComponent* Shake = GetCameraShake())
	{
		Shake->SetComponentTickEnabled(true);
	}

	// Under the sequence's black fade: move to the exit point and reveal the player body/weapon.
	SetActorLocation(ExitLocation, /*bSweep=*/ false, nullptr, ETeleportType::TeleportPhysics);

	if (USkeletalMeshComponent* FPMesh = GetFirstPersonMesh())
	{
		FPMesh->SetVisibility(true, /*bPropagateToChildren=*/ true);
	}
	if (CurrentWeapon)
	{
		CurrentWeapon->SetActorHiddenInGame(false);
	}

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		EnableInput(PC);

		// Camera Cuts releases the cine camera on finish; make sure the view is back on the player.
		PC->SetViewTarget(this);

		// Fade in from the black left by the sequence's fade track.
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StartCameraFade(1.0f, 0.0f, RespawnFadeInDuration, DeathFadeColor, false, false);
		}
	}
}
