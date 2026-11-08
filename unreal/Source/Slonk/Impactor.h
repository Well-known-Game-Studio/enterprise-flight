// Slonk - an asteroid or comet on a collision course with a world: a rock with a velocity in universe space under
// the game clock, which the tractor beam can push off its course and the weapons can break. Design in
// docs/frontier-design.md §3.

#pragma once

#include "CoreMinimal.h"
#include "SpaceTarget.h"
#include "Impactor.generated.h"

class APlanetBody;

UENUM(BlueprintType)
enum class EImpactOutcome : uint8 { Averted = 0, Shattered, Struck };

UCLASS()
class SLONK_API AImpactor : public ASpaceTarget
{
	GENERATED_BODY()
public:
	AImpactor();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual FString GetDisplayName() const override { return DisplayName; }

	/** The rock on a course for the world, meeting it in HoursToImpact of ship's time; null without a world. */
	static AImpactor* Spawn(UWorld* World, APlanetBody* Target, const FString& Name, double HoursToImpact, bool bComet, int32 Seed);
	/** The one named, or null. */
	static AImpactor* FindByName(UWorld* World, const FString& Name);

	/** The closest the track comes to the world's centre, in cm (the present distance once past). */
	double MissDistanceCm() const;
	/** Hours of ship's time to the closest approach (0 once past). */
	double HoursToClosest() const;
	bool WillHit() const;
	bool IsComet() const { return bComet; }
	/** The world's name now (it follows a rename); the one at the spawn when the world is gone. */
	FString GetWorldName() const;
	int32 MissionId = -1;

	/** A push off the course: a change of velocity (cm/s) in this direction; the sideways part is what counts. */
	void Push(const FVector& DeltaV);
	/** Break it: the explosion, the fragments, the mission told. */
	void Shatter();
	/** Dev: the rock on a course that misses by three radii of the world. */
	void DevDeflect();
	/** Dev: the rock at the world now. */
	void DevStrike();

	/** Pure: the closest approach of a track to a point, and the time to it (seconds; 0 when it is behind). */
	static double TrackMissDistance(const FVector& Pos, const FVector& Vel, const FVector& Point);
	static double TrackSecondsToClosest(const FVector& Pos, const FVector& Vel, const FVector& Point);
	/** The beam's push: cm/s^2 of sideways acceleration while the tractor holds the rock. */
	static constexpr double TractorPushCmPerS2 = 300.0;
	static constexpr double SpeedCmPerS = 2.5e6;   // 25 km/s
	/** An averted rock is retired this far past the world (three million kilometres). */
	static constexpr double RetireCm = 3.0e11;

private:
	FVector WorldUniverse() const;
	FVector ToUniverse(const FVector& W) const;
	FVector ToWorld(const FVector& U) const;
	void Strike();
	void Resolve(EImpactOutcome Outcome);
	void BuildComet();
	void UpdateTail();

	UPROPERTY() FString DisplayName;
	UPROPERTY() FString WorldName;
	TWeakObjectPtr<APlanetBody> TargetWorld;
	FVector Universe = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	bool bComet = false;
	bool bResolved = false;
	bool bStruck = false;
	bool bPlaced = false;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Coma;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Tail;
	float TailTimer = 0.f;
};
