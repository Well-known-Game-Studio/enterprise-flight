// Slonk - a leviathan alongside: a living thing nine kilometres long, cased in rock and wreck, that lays itself along
// the ship's beam and closes on her with every hour's work done under it. The body of the mission of that kind
// (Missions.h, EMissionKind::Leviathan): it keeps station by the mission's hours, shows what the mission tells it to,
// tells the mission what the ship's guns do to it, and answers a tractor beam itself. Design in
// docs/missions-design.md §7.

#pragma once

#include "CoreMinimal.h"
#include "SpaceTarget.h"
#include "Leviathan.generated.h"

class AStarshipPawn;
class UMaterialInstanceDynamic;
class UPointLightComponent;

UCLASS()
class SLONK_API ALeviathan : public ASpaceTarget
{
	GENERATED_BODY()
public:
	ALeviathan();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	/** Her guns: it is never destroyed. The first hit, and the hit that turns it, are the mission's to answer. */
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual FString GetDisplayName() const override { return DisplayName; }

	/** Come up from astern onto the ship's starboard beam, for the mission of this id; null without a ship. */
	static ALeviathan* Spawn(UWorld* World, AStarshipPawn* Ship, const FString& Name, int32 MissionId, double HoursLeft);
	/** The one that is the mission's, or null. */
	static ALeviathan* FindByMission(UWorld* World, int32 MissionId);
	/** Pure: how far off the beam it lies with this many hours of work left before it is against the hull, cm. Inside
	 *  the reach of the phasers and the torpedoes from the first hour. */
	static double StationCm(double HoursLeft) { return 1.0e5 + 1.3e5 * FMath::Max(HoursLeft, 0.0); }

	/** A band of light down its length, head to tail. */
	void Ripple(const FLinearColor& Color);
	/** White from end to end, and a lunge along the hull. */
	void Lash();
	/** It goes: no longer to be locked or hurt, off down its road, gone in a minute (one left astern stays where it
	 *  was while the ship draws away, and turns for its road after). Settled, its lights are low. */
	void Depart(bool bSettled);
	bool IsDeparting() const { return bDeparting; }

	int32 MissionId = -1;
	/** The rocks that trail the head, and the damage from her guns that turns it. */
	static constexpr int32 NumSegments = 19;
	static constexpr float DriveOffDamage = 600.f;
	/** The colours of a ripple: a finding or the road's beats; a thing misread or a shot; her beam. */
	static FLinearColor Pale() { return FLinearColor(0.55f, 0.8f, 1.f); }
	static FLinearColor Amber() { return FLinearColor(1.f, 0.55f, 0.15f); }
	static FLinearColor Blue() { return FLinearColor(0.25f, 0.45f, 1.f); }

private:
	void Swim(float DeltaTime);

	UPROPERTY() FString DisplayName;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Segments;
	/** The light in each seam of its case: a thin glowing collar between two rocks; and, at every other seam, a lamp
	 *  that throws that light on the flank it shows the ship. */
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Lights;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> LightMats;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> Lamps;
	/** The worked stone in its case: of the make of the rings and the standing stones. */
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Stone;
	TWeakObjectPtr<AStarshipPawn> Ship;
	/** Where it lies from the ship (fixed in the world from the spawn), which way its head points, how far off, and
	 *  how far ahead of her beam its head is. */
	FVector Bearing = FVector::RightVector, Heading = FVector::ForwardVector;
	double Range = 0.0, Lead = 0.0;
	float Age = 0.f, RippleAge = -1.f, LashAge = -1.f, DepartAge = 0.f, Dim = 1.f, BeamCooldown = 0.f;
	FLinearColor RippleColor = FLinearColor::White;
	float DamageTaken = 0.f;
	/** Going: how far it has got from its station, as the ship sees it; and, once she has gone to warp or is gone, on
	 *  its own in the world. */
	FVector DepartOff = FVector::ZeroVector;
	bool bDeparting = false, bSettled = false, bFiredOn = false, bAdrift = false;
};
