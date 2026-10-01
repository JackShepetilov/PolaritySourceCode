// TacticalDeviceHandlers.h
// What each side rail device does while it runs. One handler per device on the gun in hand, built
// by its definition (UTacticalDeviceDefinition::CreateHandler), owned by UTacticalDeviceComponent.
//
// A handler runs on EVERY machine: gameplay parts check IsAuthority(), visual parts run wherever
// there is a screen. Docs/TacticalAttachment_Plan_2026-10-01.md.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "TacticalDeviceHandlers.generated.h"

class AActor;
class AShooterNPC;
class UAudioComponent;
class UNiagaraComponent;
class USpotLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UTacticalDeviceComponent;
class UTacticalDeviceDefinition;
class UTacticalDevice_Shield;
class UTacticalDevice_Light;
class UTacticalDevice_Freeze;
struct FDamageEvent;

UCLASS(Abstract)
class POLARITY_API UTacticalDeviceHandler : public UObject
{
	GENERATED_BODY()

public:

	void Init(UTacticalDeviceComponent* InOwner, const UTacticalDeviceDefinition* InDefinition);

	/** The device switched on here. Base: sound and the looping effect. */
	virtual void OnActivated();

	/** The device switched off here (or the handler is going away while on). Must undo everything
	 *  OnActivated and TickActive did, on every path. */
	virtual void OnDeactivated();

	/** Every frame while on. Base: keeps the looping effect at the device. */
	virtual void TickActive(float DeltaTime);

	/** Charge per second while on. Base: the definition's ActiveDuration. */
	virtual float GetDrainPerSecond() const;

	/** Server, while on: take what this device stops of an incoming hit, return what gets through. */
	virtual float AbsorbDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) { return Damage; }

	/** A one-off moment the server announced (UTacticalDeviceComponent::BroadcastDeviceEvent). */
	virtual void PlayEvent(uint8 EventId, const FVector& Location) {}

	/** The handler is being dropped (device changed, owner gone). */
	void Shutdown();

	bool IsOn() const { return bOn; }

protected:

	TWeakObjectPtr<UTacticalDeviceComponent> Owner;

	UPROPERTY()
	TObjectPtr<UTacticalDeviceDefinition> Definition;

	bool bOn = false;

	UPROPERTY()
	TObjectPtr<UAudioComponent> LoopAudio;

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> ActiveFXComponent;

	bool IsAuthority() const;
	bool HasScreen() const;
	AActor* GetOwnerActor() const;

	/** A mesh component for a prototype shape, on the owner, not attached (placed every frame).
	 *  Null when the mesh or the material is missing: a shape with the default opaque material
	 *  would wall off the view, so nothing is drawn instead. */
	UStaticMeshComponent* MakeShape(UStaticMesh* Mesh, UMaterialInterface* Material, FName Name) const;

	/** Destroy a component made by MakeShape or for a light. */
	static void DestroyPart(UActorComponent* Part);

	/** Is Point inside the cone from Origin along Direction? */
	static bool IsInCone(const FVector& Origin, const FVector& Direction, float CosHalfAngle, float Range, const FVector& Point);

	/** Nothing solid between From and To, ignoring the owner and Target. */
	bool HasClearLine(const FVector& From, const FVector& To, const AActor* Target) const;
};

/** Gun shield. The charge is the pool. */
UCLASS()
class POLARITY_API UTacticalDeviceHandler_Shield : public UTacticalDeviceHandler
{
	GENERATED_BODY()

public:

	virtual void OnActivated() override;
	virtual void OnDeactivated() override;
	virtual void TickActive(float DeltaTime) override;
	virtual float AbsorbDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void PlayEvent(uint8 EventId, const FVector& Location) override;

	enum : uint8
	{
		Event_ShieldHit = 0,
		Event_ShieldBreak = 1,
	};

private:

	const UTacticalDevice_Shield* GetShield() const;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> ShieldShape;

	void PlaceShape();
};

/** Flashlight and laser. */
UCLASS()
class POLARITY_API UTacticalDeviceHandler_Light : public UTacticalDeviceHandler
{
	GENERATED_BODY()

public:

	virtual void OnActivated() override;
	virtual void OnDeactivated() override;
	virtual void TickActive(float DeltaTime) override;

private:

	const UTacticalDevice_Light* GetLight() const;

	/** Seconds the light has been held on each enemy, and when it last was. Server. */
	struct FLitRecord
	{
		float LitSeconds = 0.0f;
		float LastLitTime = 0.0f;
		bool bAffectedFXPlayed = false;
	};
	TMap<TWeakObjectPtr<AShooterNPC>, FLitRecord> LitRecords;

	float ScanTimer = 0.0f;

	UPROPERTY()
	TObjectPtr<USpotLightComponent> Spot;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BeamShape;

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> EndPointFXComponent;

	/** Server: who is in the light right now, through the face for people. */
	void ScanTargets(float ScanDeltaTime);

	/** The point on Enemy the light has to reach: the first face bone the mesh has, else its body. */
	bool FindLightPoint(const AShooterNPC* Enemy, const FVector& Origin, const FVector& Direction, float CosHalf, float Range, FVector& OutPoint) const;

	float DurationForLitTime(float LitSeconds) const;

	void UpdateVisuals();
};

/** Freezer cone. */
UCLASS()
class POLARITY_API UTacticalDeviceHandler_Freeze : public UTacticalDeviceHandler
{
	GENERATED_BODY()

public:

	virtual void OnActivated() override;
	virtual void OnDeactivated() override;
	virtual void TickActive(float DeltaTime) override;

private:

	const UTacticalDevice_Freeze* GetFreeze() const;

	float ScanTimer = 0.0f;

	/** Enemies slowed by this cone. Server. */
	TSet<TWeakObjectPtr<AShooterNPC>> Frozen;

	/** Projectiles this cone has slowed, on this machine. */
	TSet<TWeakObjectPtr<AActor>> SlowedProjectiles;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> ConeShape;

	void ScanEnemies();
	void UpdateProjectiles();
	void UpdateBoltZone();
	void ReleaseAll();
	void PlaceShape();
};
