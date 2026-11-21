// Slonk - the animals round the camera on a world with life (FaunaHabitat.h says where they are and how they fly),
// drawn as GPU-only planet instances that turn with the world for nothing (UPlanetInstancesComponent). Attached to the
// terrain, so its frame is the planet's. Each group (a flock) is one instanced set, set up once when it comes and moved
// as a whole each frame to its place on its path: its flight is in the set's transform, which the velocity pass sees,
// and its members' motion (their wings, the ripple through a flock, a startle's scatter) is the material's, on the
// material's own clock (M_Fauna, tools/planet_materials/fauna_material.py). Groups come and go by their distance from
// the camera alone, read off a scan of the habitat cells round it made on a task, so a flock looked away from and back
// at is still there. A craft passing low by a flock, or someone walking under it, startles it: it flees, scatters and
// drifts back to its path over a couple of minutes, and is still shaken if left and come back to within that time.
//
// slonk.Fauna.Enable 0 takes every animal away (the seas' plankton and shoals with them, APlanetBody), leaving each world
// as it was before it had any; slonk.Fauna.Density scales the share of the habitat's cells holding a group. Without
// M_Fauna (the builder not run yet) the animals are drawn in the engine's basic material, their wings still, and one
// warning says so.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "FaunaHabitat.h"
#include "Tasks/Task.h"
#include "PlanetFaunaComponent.generated.h"

class APlayerController;
class UMaterialInstanceDynamic;
class UPlanetInstancesComponent;
class UPlanetTerrainComponent;

namespace SlonkFauna
{
	/** slonk.Fauna.Enable and slonk.Fauna.Density as they stand (game thread). */
	SLONK_API bool IsEnabled();
	SLONK_API float DensitySetting();
	/** The material the animals are drawn in. */
	inline const TCHAR* MaterialPath = TEXT("/Game/Slonk/Materials/M_Fauna.M_Fauna");
}

UCLASS(ClassGroup = (Slonk))
class SLONK_API UPlanetFaunaComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPlanetFaunaComponent();

	/** The world's field and terrain, its animals, how much lives there and the living ground's colours. A change lets
	 *  every group go and scans again; the same again changes nothing. */
	void Configure(UPlanetTerrainComponent* InTerrain, const FPlanetTerrainParams& P, const SlonkFauna::FFaunaProfile& InProfile, float InLife, const FScatterPalette& InPalette);
	/** Each frame, after the terrain's view update: the groups round the camera come and go, each group's frame moves along
	 *  its path (and off it, turned the way it flies, while it is shaken: SlonkFauna::FlightAt), and a craft in the air
	 *  or someone on foot near one startles it. SunDirectionWorld is the way to the star. */
	void UpdateView(const APlayerController* PC, const FVector& ViewLocationWorld, const FVector& SunDirectionWorld);
	/** Lets every group go (the world hidden, or not updated this frame): they come back on their paths. */
	void Suspend();

	/** Debug: the kinds allowed (SlonkFauna::KindBit), the path clock's offset from the world's (a look at where the
	 *  flocks will be), another profile (another strangeness), a startle of every group by a craft where it is and as it
	 *  flies (the camera, standing still, with none). */
	void SetKindMask(uint32 Mask) { KindMask = Mask; }
	uint32 GetKindMask() const { return KindMask; }
	void SetTimeOffset(double Seconds);
	double GetTimeOffset() const { return TimeOffset; }
	void SetProfile(const SlonkFauna::FFaunaProfile& InProfile) { Profile = InProfile; }
	const SlonkFauna::FFaunaProfile& GetProfile() const { return Profile; }
	int32 StartleAll(const AActor* Craft);
	/** The paths' clock: the world's seconds and the offset. */
	double ClockSeconds() const;
	/** The world as the animals see it now (the settings' density and the kinds allowed in it). */
	SlonkFauna::FFaunaWorld MakeFaunaWorld() const;
	bool IsConfigured() const { return bConfigured; }
	int32 GetGroupCount() const { return Sets.Num(); }
	int32 GetMemberCount() const;
	/** Whether the groups are drawn in M_Fauna (else the engine's basic material stands in: M_Fauna not found, or
	 *  slonk.Fauna.Debug asking for it). */
	bool IsUsingFaunaMaterial() const { return bFaunaMaterial; }
	/** Debug (fauna watch): the group drawn nearest a point in the world, and where a drawn group is and how it is turned
	 *  (its frame, its flight along X), in the world; false when it is not drawn. */
	uint64 NearestDrawnGroup(const FVector& WorldPos) const;
	bool GetDrawnGroupPose(uint64 Id, FVector& OutWorldPos, FQuat& OutWorldRot) const;
	/** One line for the debug readout, and one a candidate group of the last scan. */
	FString Describe() const;
	void DescribeCells(TArray<FString>& OutLines) const;

private:
	struct FGroupSet
	{
		SlonkFauna::FFaunaField::FGroupRef Group;
		UPlanetInstancesComponent* Set = nullptr;
		bool bShadow = false;
		/** Its frame as last drawn, and when (the paths' clock): a shaken group wheels round from it. */
		SlonkFauna::FFaunaPose Pose;
		double PoseAt = 0.0;
	};
	using FScanRef = TSharedPtr<SlonkFauna::FFaunaField::FScan, ESPMode::ThreadSafe>;

	/** The world as the settings have it now, given to the field: a change lets every group go. */
	void RefreshWorld();
	void EnsureMaterial();
	UPlanetInstancesComponent* AcquireSet();
	void SpawnSet(const SlonkFauna::FFaunaField::FGroupRef& G);
	void ReleaseSet(uint64 Id);
	void ReleaseAllSets();
	void GatherThreats(const APlayerController* PC, TArray<SlonkFauna::FFaunaThreat>& Out) const;

	UPROPERTY() TObjectPtr<UPlanetTerrainComponent> Terrain;
	FPlanetTerrainParams Params;
	SlonkFauna::FFaunaProfile Profile;
	FScatterPalette Palette;
	float Life = 0.f;
	bool bConfigured = false;
	uint32 KindMask = SlonkFauna::AllKinds;
	double TimeOffset = 0.0;

	SlonkFauna::FFaunaField Field;
	/** Bumped as the world changes: a scan launched for an older one is dropped when it lands. */
	uint32 Generation = 0;
	uint32 ScanGeneration = 0;
	UE::Tasks::TTask<FScanRef> ScanTask;
	bool bScanning = false;
	double LastScanMs = 0.0;
	int32 LastScanEvaluated = 0;
	int32 LastScanCells = 0;

	TMap<uint64, FGroupSet> Sets;
	/** Each startled group's fright, by its id: kept while it lasts, whether the group is about or not. */
	TMap<uint64, SlonkFauna::FPanic> Panics;
	int32 Startles = 0;
	FVector LastCamLocal = FVector::ZeroVector;

	UPROPERTY() TArray<TObjectPtr<UPlanetInstancesComponent>> SetPool;
	TArray<UPlanetInstancesComponent*> FreeSets;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MID;
	bool bFaunaMaterial = false;
	/** slonk.Fauna.Debug as the groups drawn have it, and whether MID is the basic material it asked for. */
	int32 DebugDrawn = 0;
	bool bPlainAsked = false;
	float LastPxPerRad = -1.f;
};
