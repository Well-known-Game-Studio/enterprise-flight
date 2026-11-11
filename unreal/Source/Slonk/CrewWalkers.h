// Slonk - the crew on their shifts: every member of the roster has a place aboard each hour (their posting on
// watch, the mess, their quarters, the rec deck) and walks the plan's passages and rides its lifts between them.
// The walk is simulated for everyone all the time in the ship's frame (cheap: a position along a route); the
// walkers near the player on foot are given a figure from a small pool (docs/interiors-design.md §8).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "ShipPlan.h"
#include "ShipInterior.h"
#include "CrewWalkers.generated.h"

class USkeletalMeshComponent;
class UDynamicMeshComponent;
class UBlendSpace;
class USkeletalMesh;
class UTexture;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class AStarshipPawn;
class UShipInteriorComponent;
class UCrewRosterSubsystem;

/** One of the crew seen about the ship: the mannequin with the locomotion blend, dressed by the crew material,
 *  posed by the walkers component every frame. It has no collision and no tick of its own. */
UCLASS()
class SLONK_API ACrewFigure : public AActor
{
	GENERATED_BODY()
public:
	ACrewFigure();
	void Setup(USkeletalMesh* Mesh, UBlendSpace* Blend, UMaterialInterface* Crew, UTexture* Base, UTexture* Normal, const FLinearColor& Jacket, const FLinearColor& Skin, const FLinearColor& Hair);
	/** Feet on the floor in the world, facing the transform's forward; the speed drives the blend (0 stands). */
	void Pose(const FTransform& Feet, float SpeedCm);
	int32 Member = INDEX_NONE;
private:
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> Figure;
	UPROPERTY() TObjectPtr<UDynamicMeshComponent> Hair;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Mids;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HairMID;
	float Speed = -1.f;
};

/** A member of the crew as a walker: where they are, where they are bound, the route between. Pawn space. */
struct FCrewWalker
{
	int32 Member = INDEX_NONE;
	/** The roster's compartment for the hour, as last read. */
	FString Where;
	/** The interior place they stand in (INDEX_NONE off the plan: the nacelles, ashore, away, dead), and the one
	 *  they are walking to. */
	int32 Place = INDEX_NONE, Target = INDEX_NONE;
	FVector Pos = FVector::ZeroVector;
	double Yaw = 0.0;
	FVector Spot = FVector::ZeroVector;
	double SpotYaw = 0.0;
	FPlanRoute Route;
	int32 Leg = 0, Point = 0;
	double LegTime = 0.0;
	bool bMoving = false, bInLift = false, bReplan = false, bOffstage = true;
	FLinearColor Skin = FLinearColor(0.72f, 0.52f, 0.42f), Hair = FLinearColor(0.09f, 0.06f, 0.045f);
	TWeakObjectPtr<ACrewFigure> Figure;
};

UCLASS(ClassGroup = (Starship), meta = (BlueprintSpawnableComponent))
class SLONK_API UCrewWalkersComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UCrewWalkersComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** The figures given out at once, to the walkers nearest the player on foot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crew") int32 MaxFigures = 24;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crew") float WalkSpeedCm = 200.f;
	/** How far along the deck a walker is given a figure (the interior shows its zones to the same radius). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crew") float ShowRadiusCm = 6000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crew") bool bFigures = true;
	UPROPERTY(EditAnywhere, Category = "Crew|Figure") TSoftObjectPtr<USkeletalMesh> FigureMesh;
	UPROPERTY(EditAnywhere, Category = "Crew|Figure") TSoftObjectPtr<UTexture> FigureBaseMap;
	UPROPERTY(EditAnywhere, Category = "Crew|Figure") TSoftObjectPtr<UTexture> FigureNormalMap;
	UPROPERTY(EditAnywhere, Category = "Crew|Figure") TSoftObjectPtr<UBlendSpace> LocomotionBlend;
	UPROPERTY(EditAnywhere, Category = "Crew|Figure") TSoftObjectPtr<UMaterialInterface> CrewMaterial;
	UPROPERTY(EditAnywhere, Category = "Crew|Figure") FLinearColor Jacket = FLinearColor(0.36f, 0.05f, 0.07f);

	const TArray<FCrewWalker>& GetWalkers() const { return Walkers; }
	int32 NumMoving() const;
	int32 NumInLift() const;
	int32 FiguresShown() const;
	/** The interior place the roster's compartment name maps to for a person (the same each time for them):
	 *  a set, or one of the plan's rooms of that purpose; INDEX_NONE for nowhere on the plan. */
	int32 PlaceFor(const FString& Where, int32 Seed) const;
	/** The walkers nearest a point on foot (pawn space), a line each, for the console. */
	FString Describe(const FVector& Feet, int32 MaxLines) const;
	FString DescribeRoute(int32 Member) const;
	/** Bring everyone to their hour's place at once (a session's start, a jump of the clock). */
	void Settle();

private:
	TArray<FCrewWalker> Walkers;
	TMap<int32, int32> ByMember;
	TUniquePtr<FPlanRouter> Router;
	UPROPERTY() TArray<TObjectPtr<ACrewFigure>> Pool;
	float SyncTimer = 0.f;
	bool bSettled = false;

	AStarshipPawn* Ship() const;
	UShipInteriorComponent* Interior() const;
	UCrewRosterSubsystem* Roster() const;
	void Sync(bool bAll);
	bool PlanRoute(FCrewWalker& W, int32 ToPlace);
	void Advance(FCrewWalker& W, float Dt);
	void UpdateFigures();
	FVector SpotIn(int32 Place, int32 Seed, double& OutYaw) const;
	int32 DeckOfPlace(int32 Place) const;
	int32 StopOfPlace(int32 Place) const;
	/** A set's spot by its lift door (its stop's car where the spots were never set). */
	FVector LiftSpot(int32 Place) const;
	/** Deck G's corridor: the nearest point of its centre lines, and the way along them (round the corner). */
	FVector CorridorPoint(const FShipRoom& C, const FVector& P) const;
	void AlongCorridor(const FShipRoom& C, const FVector& From, const FVector& To, TArray<FVector>& Out) const;
	/** The way out of a set from a point in it to its lift door, and the way in from the door to a point. */
	void SetExit(int32 Place, const FVector& From, TArray<FVector>& Out) const;
	void SetEntry(int32 Place, const FVector& To, TArray<FVector>& Out) const;
	bool Rendered(const FCrewWalker& W) const;
	ACrewFigure* Acquire(FCrewWalker& W);
	void Release(FCrewWalker& W);
};
