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
// The herds are sets too, one a herd, moved the same way: each frame its frame is set on the drawn ground on its loop,
// tilted to the plane fitted to the ground under it (read again each frame for a herd near the camera, every sixth
// further off), and the material stands each walker on the ground under its feet from a grid of that ground's heights
// over the plane, turns it the way it walks and swings its legs from how far it has walked (SlonkFauna::StepHerd). A herd
// walks out of the way of the crew on foot, and bolts from a craft passing low or someone running in on it. They are
// drawn in M_Fauna's walker branch (its Walkers parameter, set on a material instance of their own): while M_Fauna has
// none (built before it) there are no herds, and the line the status gives says so. A herd going faster than a walk near
// the camera raises dust off dry ground by day (HerdDust.h: a Niagara system, one pooled component on each such herd's
// set; none without the asset, or with slonk.Fauna.Dust 0).
//
// The motes (pollen and gnats by day, fireflies at night) are the terrain's chunks' to carry, as the plants are
// (SlonkScatter::PlaceMotes): this component says what they are (FMoteSettings) and the terrain places and draws them.
//
// slonk.Fauna.Enable 0 takes every animal away (the seas' plankton and shoals with them, APlanetBody, and the motes),
// leaving each world as it was before it had any; slonk.Fauna.Density scales the share of the habitat's cells holding a
// group, and of the ground's cells holding a mote cloud. Without M_Fauna (the builder not run yet) the animals are drawn
// in the engine's basic material, their wings still, and one warning says so; without its mote branch there are no motes.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "FaunaHabitat.h"
#include "HerdDust.h"
#include "Tasks/Task.h"
#include "PlanetFaunaComponent.generated.h"

class APlayerController;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UNiagaraSystem;
class UPlanetInstancesComponent;
class UPlanetTerrainComponent;

namespace SlonkFauna
{
	/** slonk.Fauna.Enable, slonk.Fauna.Density and slonk.Fauna.Debug as they stand (game thread). */
	SLONK_API bool IsEnabled();
	SLONK_API float DensitySetting();
	SLONK_API int32 DebugSetting();
	/** How many pixels a radian is to a player's camera (its field of view across the viewport): the speck floors' scale. */
	SLONK_API float PixelsPerRadian(const APlayerController* PC);
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
	void Configure(UPlanetTerrainComponent* InTerrain, const FPlanetTerrainParams& P, const SlonkFauna::FFaunaProfile& InProfile, float InLife, const FScatterPalette& InPalette,
		bool bInTowns = false, const FCivilization& InPeople = FCivilization());
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
	/** The motes the terrain's chunks carry as things stand: the fauna on and the motes allowed, the world's life, the
	 *  settings' density, the look's pollen and fireflies. */
	SlonkScatter::FMoteSettings MakeMoteSettings() const;
	bool IsConfigured() const { return bConfigured; }
	int32 GetGroupCount() const { return Sets.Num(); }
	int32 GetMemberCount() const;
	/** Whether the groups are drawn in M_Fauna (else the engine's basic material stands in: M_Fauna not found, or
	 *  slonk.Fauna.Debug asking for it). */
	bool IsUsingFaunaMaterial() const { return bFaunaMaterial; }
	/** Whether the herds can be drawn: M_Fauna has its walker branch, or slonk.Fauna.Debug draws them in the basic
	 *  material. While they cannot, the world keeps none. */
	bool CanDrawHerds() const { return bWalkerMaterial || bHerdDebug; }
	/** Debug (fauna watch): the group drawn nearest a point in the world, and where a drawn group is and how it is turned
	 *  (its frame, its flight along X), in the world; false when it is not drawn. */
	uint64 NearestDrawnGroup(const FVector& WorldPos) const;
	/** A herd's pose is its frame turned the way it walks. */
	bool GetDrawnGroupPose(uint64 Id, FVector& OutWorldPos, FQuat& OutWorldRot) const;
	/** The kind of a drawn group (a flier for one not drawn). */
	EFaunaKind GetDrawnGroupKind(uint64 Id) const;
	/** Where a herd is now, planet-local: its frame on the ground if it is drawn, else its path's place off by any fright it
	 *  is still in. */
	FVector HerdPlaceNow(const SlonkFauna::FFaunaGroup& G) const;
	/** How many herds are raising dust now. */
	int32 GetDustCount() const { return DustPool.NumLive(); }
	/** One line for the debug readout, and one a candidate group of the last scan. */
	FString Describe() const;
	void DescribeCells(TArray<FString>& OutLines) const;
	/** The world as the settings have it now, given to the field (a change lets every group go) and the motes' to the
	 *  terrain: each view update does it, and a debug command that changed a setting does it at once, so the line it reads
	 *  out after is the settings' as they now stand. */
	void RefreshWorld();

private:
	struct FGroupSet
	{
		SlonkFauna::FFaunaField::FGroupRef Group;
		UPlanetInstancesComponent* Set = nullptr;
		bool bShadow = false;
		/** Its frame as last drawn, and when (the paths' clock): a shaken group wheels round from it. */
		SlonkFauna::FFaunaPose Pose;
		double PoseAt = 0.0;
		/** A herd's clocks, ground and place (SlonkFauna::StepHerd), and how many frames it has been about. */
		TSharedPtr<SlonkFauna::FHerdState> Herd;
		uint32 Frames = 0;
		/** How wet the ground under a herd raising dust was when last read, and when (the world's seconds). */
		double DustWet = 0.5, DustWetAt = -1.0e9;
	};
	using FScanRef = TSharedPtr<SlonkFauna::FFaunaField::FScan, ESPMode::ThreadSafe>;

	void EnsureMaterial();
	/** The herds' material instance (M_Fauna, its Walkers on), looked for again every few seconds while M_Fauna has no
	 *  walker branch, so one built during play is taken up. */
	void EnsureHerdMaterial();
	/** A herd's step this frame: its frame on the drawn ground, its fright, the crew, its material's data, its dust. */
	void UpdateHerd(uint64 Id, FGroupSet& S, const TArray<SlonkFauna::FFaunaThreat>& Threats, const FVector& CamLocal, const FVector& Sun, double Now, double Dt);
	/** A herd's dust this frame (how deep in the night it stands): raised, kept up or given back to the pool. */
	void UpdateDust(uint64 Id, FGroupSet& S, const FVector& CamLocal, float Night);
	/** Gives a herd's dust back, its puffs left to fade. */
	void ReleaseDust(uint64 Id);
	/** NS_HerdDust, loaded the first time a herd would raise dust (false, and one line in the log, without it). */
	bool EnsureDustSystem();
	UNiagaraComponent* MakeDustComponent();
	/** The world's own seconds, which the dust's pool rests its components by. */
	double WorldSeconds() const;
	/** The drawn ground's height at a planet-local direction (the field's where none is drawn), cm over the sea-level sphere. */
	double GroundHeightAt(const FVector& Dir) const;
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
	bool bTowns = false;
	FCivilization People;
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
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HerdMID;
	bool bWalkerMaterial = false;
	bool bHerdDebug = false;
	int32 HerdMIDDebug = -1;
	double HerdProbedAt = -1.0e9;
	float LastHerdPxPerRad = -1.f;
	double LastUpdateSec = -1.0;

	/** The herds' dust: its system (none until a herd would first raise it; bDustMissing once it was looked for and not
	 *  found), every component made of it (kept here from the garbage collector) and the pool lending them. */
	UPROPERTY() TObjectPtr<UNiagaraSystem> DustSystem;
	bool bDustMissing = false;
	UPROPERTY() TArray<TObjectPtr<UNiagaraComponent>> DustComponents;
	SlonkFauna::FDustPool DustPool;
};
