// Slonk - the ship's science sensors: a timed scan of one object that writes a record into the science log.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ScienceLog.h"
#include "ScienceScannerComponent.generated.h"

class APlanetBody;

UENUM(BlueprintType)
enum class EScanState : uint8
{
	Idle,
	/** Progress is running. */
	Scanning,
	/** The target is in range but off the sensor cone: progress holds until the ship turns back. */
	Held
};

/**
 * Point the ship at something within sensor range and start a scan; after a kind-dependent number of seconds the
 * readings go into the UScienceLogSubsystem and earn points. Letting the target drift out of the sensor cone pauses
 * the scan; leaving range, going to warp or losing the target cancels it. A landed craft surveys the ground under
 * it instead (a separate record from the orbital scan of the same world).
 */
UCLASS(ClassGroup = (Slonk), meta = (BlueprintSpawnableComponent))
class SLONK_API UScienceScannerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UScienceScannerComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Start scanning Target (its nav label is used for the record). False, with a reason in OutReason, if it cannot be scanned from here. */
	bool BeginScan(AActor* Target, const FString& Label, FString& OutReason);
	/** Survey the ground under a landed craft. */
	bool BeginSurfaceSurvey(APlanetBody* Body, FString& OutReason);
	void Cancel();
	bool IsBusy() const { return State != EScanState::Idle; }

	UFUNCTION(BlueprintPure, Category = "Science") EScanState GetState() const { return State; }
	UFUNCTION(BlueprintPure, Category = "Science") float GetProgress() const { return Progress; }
	UFUNCTION(BlueprintPure, Category = "Science") FString GetTargetLabel() const { return TargetLabel; }
	UFUNCTION(BlueprintPure, Category = "Science") AActor* GetTarget() const { return Target.Get(); }
	/** Short status for the HUD: "align to target", "hold position"... */
	UFUNCTION(BlueprintPure, Category = "Science") FString GetStatusText() const { return Status; }

	/** Classify any actor the nav list can hold; Surface never comes from here. False if it is not scannable. */
	static bool ClassifyTarget(const AActor* Actor, EScanKind& OutKind);
	/** The log key for an actor: "<system>/<label>", the system being the nearest star system anchor. */
	static FString KeyFor(const AActor* Actor, const FString& Label);
	/** Name of the star system an object (or the ship) is in; "Deep space" without one. */
	static FString SystemNameAt(const UWorld* World, const FVector& Location);
	/** Keys of everything that counts toward a system's survey: its star(s) and planets. */
	static void SurveyKeys(const UWorld* World, const FVector& Location, FString& OutSystem, TArray<FString>& OutKeys);
	/** Range test used both to start and to keep a scan: the distance left to the surface and the range allowed. */
	bool InRange(const AActor* Actor, EScanKind Kind, double& OutDistCm, double& OutRangeCm) const;

	/** Half-angle of the sensor cone about the ship's nose (deg); outside it the scan holds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Science") float ConeDeg = 75.f;
	/** Worlds: within this many radii of the surface, never closer than WorldMinRangeCm allows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Science") float WorldRangeRadii = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Science") double WorldMinRangeCm = 2.0e8;
	/** Stars: within this many AU of the photosphere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Science") float StarRangeAu = 1.0f;
	/** Stations, ships, hostiles: within this of their centre (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Science") double VesselRangeCm = 1.5e7;
	/** Belts: within this many half-widths of the ring's centre line. Nebulae: within this many radii of the centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Science") float BeltRangeHalfWidths = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Science") float NebulaRangeRadii = 1.5f;
	/** Scan durations in seconds by kind (index = EScanKind). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Science") TArray<float> SecondsByKind;
	/** Multiplier on every duration (the shuttle's short-range sensors are quicker up close). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Science") float SpeedFactor = 1.f;

private:
	void Complete();
	void Finish(const FString& Why);
	void BuildRecord(FScanRecord& R) const;
	void FactsForWorld(const APlanetBody* P, FScanRecord& R) const;
	void FactsForSurface(const APlanetBody* P, FScanRecord& R) const;
	void FactsForStar(const AActor* S, FScanRecord& R) const;
	void FactsForVessel(const AActor* V, FScanRecord& R) const;
	void FactsForBelt(const AActor* B, FScanRecord& R) const;
	void FactsForNebula(const AActor* N, FScanRecord& R) const;
	/** Deterministic 0..1 from the record key and a salt. */
	static float Roll(const FString& Key, int32 Salt);
	float Duration() const;

	EScanState State = EScanState::Idle;
	TWeakObjectPtr<AActor> Target;
	EScanKind Kind = EScanKind::Planet;
	FString TargetLabel;
	FString Status;
	float Progress = 0.f;
	/** Where the ship sat when a surface survey began (the sample point). */
	FVector SurfacePoint = FVector::ZeroVector;
};
