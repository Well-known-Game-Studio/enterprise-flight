// Slonk - the animals round the camera on a world with life (FaunaHabitat.h says where they are and how they fly),
// drawn as GPU-only planet instances that turn with the world for nothing (UPlanetInstancesComponent). Attached to the
// terrain, so its frame is the planet's. Each group (a flock) is one instanced set, set up once when it comes and moved
// as a whole each frame to its place on its path: its flight is in the set's transform, which the velocity pass sees,
// and its members' motion (their wings, the ripple through a flock, a startle's scatter) is the material's, on the
// material's own clock (M_Fauna, tools/planet_materials/fauna_material.py). Groups come and go by their distance from
// the camera alone, read off a scan of the habitat cells round it made on a task, so a flock looked away from and back
// at is still there. A craft passing low by a flock, or someone walking under it, startles it: it flees, scatters and
// drifts back to its path over a couple of minutes, and is still shaken if left and come back to within that time. A
// strange world's flocks are of its species too (four-winged fliers that hover and dart, membrane-winged ones that glide
// long), and its fish (eels, spinefins), each alien plan's groups drawn in an instance of M_Fauna of their own that gives
// it the plan's wings or form and its glow organs' row (SlonkFauna::Wings, FishForm, GlowOrgan), lit in the world's glow;
// the Earth's bird and fish in the fliers' and the leapers' own instances, as before.
//
// The herds are sets too, one a herd, moved the same way: each frame its frame is set on the drawn ground on its loop,
// tilted to the plane fitted to the ground under it (read again each frame for a herd near the camera, every sixth
// further off), and the material stands each walker on the ground under its feet from a grid of that ground's heights
// over the plane, turns it the way it walks and swings its legs from how far it has walked (SlonkFauna::StepHerd). A herd
// walks out of the way of the crew on foot, and bolts from a craft passing low or someone running in on it. They are
// drawn in M_Fauna's walker branch (its Walkers parameter, set on a material instance of their own): while M_Fauna has
// none (built before it) there are no herds, and the line the status gives says so. A strange world's herds are of its
// species (SlonkFauna::FFaunaProfile::Species: six-legged grazers, insectoids), each herd's mesh its species' plan's, and
// each alien plan's herds drawn in an instance of their own that gives the walker branch the plan's rig (its neck's pivot,
// its feet's lift, its head's swing, its carapace: SlonkFauna::Walker::FRig); the Earth grazer's in the herds' own, as
// before. A herd going faster than a walk near
// the camera raises dust off dry ground by day (HerdDust.h: a Niagara system, one pooled component on each such herd's
// set; none without the asset, or with slonk.Fauna.Dust 0).
//
// The leapers are sets too, one a group of fish leaping out of a painted shoal, the set's frame moved each frame with the
// shoal as the terrain material swims it (SlonkFauna::PathAt, on the world's own seconds): the material leaps each fish
// out of the sea and back on its schedule, from the swell's surface where it leaves to where it dives back in (each
// read here where and when the leap crosses it, and given the material: SlonkFauna::LeaperData), and keeps it masked
// and under the sea between leaps. They are drawn in M_Fauna's leaper branch (its Leapers parameter, on a material
// instance of their own): while M_Fauna has none there are no leapers, and the status line says so. A craft passing low
// over a group sends it diving: no fish of it leaves the water for a minute. Where a leap leaves the water and dives back
// in near the camera, a splash is thrown (FishSplash.h: a Niagara one-shot from a pool of components; none without the
// asset, or with slonk.Fauna.Splash 0), by day and dimmer through dusk; none once the sun cannot light it (past
// SplashNightMax).
//
// The sky-jellies (on a strange world whose air is thick) are sets too, one a jelly, its frame moved each frame along its
// loop high over the ground (and off it while a startle has it squeezing away and down: SlonkFauna::FlightAt), never
// turned; the material pulses its bell, waves its tentacles and trails them back from the way it drifts through the air
// (given it each frame with how fast that changes, so its trail runs on in closed form), and at night runs light down its
// ribs and tentacles in the world's glow colour. They are drawn in M_Fauna's floater branch (its Floaters parameter, on a
// material instance of their own): while M_Fauna has none there are no jellies, and the status line says so; it says too
// why a world has none.
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
#include "FishSplash.h"
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
	/** A group drawn: its id, its kind and where it is (fauna watch picks among them: PickWatched). */
	struct FDrawnGroupAt
	{
		uint64 Id = 0;
		EFaunaKind Kind = EFaunaKind::Flier;
		FVector Pos = FVector::ZeroVector;
	};
	/** The group a watch follows: Preferred (the group the last fauna goto found) while it is drawn and of the kind asked
	 *  for (EFaunaKind as an int, or -1 for any), else the drawn group of that kind nearest View; 0 for none. */
	SLONK_API uint64 PickWatched(TConstArrayView<FDrawnGroupAt> Drawn, const FVector& View, int32 Kind, uint64 Preferred);
	/** A watch's pick (PickWatched) after a fauna goto, GotoId the group it found (0 for none): once a watch has taken it, it
	 *  is forgotten (as fauna watch off forgets it, StarshipPawn), so only the watch after the goto follows its group first,
	 *  and every later one the nearest of the kind asked for again. A watch that has not taken it (another kind asked for,
	 *  or the group not drawn yet) leaves it for the next. */
	SLONK_API uint64 TakeWatched(TConstArrayView<FDrawnGroupAt> Drawn, const FVector& View, int32 Kind, uint64& GotoId);
	/** Each plan a profile's species draw (their meshes and their material instances): the fliers', the herds', the
	 *  leapers' and the jellies'. */
	SLONK_API TArray<TPair<FFaunaBodyPlan, EFaunaKind>> PlansOf(const FFaunaProfile& Profile);
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
	/** What the world's seas carry as the terrain material paints them (its shoals, its swell, its colours: the leapers
	 *  leap out of those shoals): a change lets every group go, as Configure's does. */
	void SetSea(const SlonkFauna::FFaunaSea& InSea);
	const SlonkFauna::FFaunaSea& GetSea() const { return Sea; }
	/** Each frame, after the terrain's view update: the groups round the camera come and go, each group's frame moves along
	 *  its path (and off it, turned the way it flies, while it is shaken: SlonkFauna::FlightAt), and a craft in the air
	 *  or someone on foot near one startles it. SunDirectionWorld is the way to the star. */
	void UpdateView(const APlayerController* PC, const FVector& ViewLocationWorld, const FVector& SunDirectionWorld);
	/** Lets every group go (the world hidden, or not updated this frame): they come back on their paths. And, unless
	 *  bKeepPlans (the debug setting changed, the world drawn on), its species' meshes and its plans' material instances
	 *  let go too, once, until it draws a group again (HoldOwnPlans): a world not drawn keeps none of them. */
	void Suspend(bool bKeepPlans = false);

	/** Debug: the kinds allowed (SlonkFauna::KindBit), the path clock's offset from the world's (a look at where the
	 *  flocks will be), another profile (another strangeness), a startle of every group by a craft where it is and as it
	 *  flies (the camera, standing still, with none). */
	void SetKindMask(uint32 Mask) { KindMask = Mask; }
	uint32 GetKindMask() const { return KindMask; }
	void SetTimeOffset(double Seconds);
	double GetTimeOffset() const { return TimeOffset; }
	/** Another profile: its species' meshes held (SlonkFaunaMeshes::HoldPlans: those of a profile left behind, or of a
	 *  world suspended, are let go) and the material instances of plans it no longer has let go. */
	void SetProfile(const SlonkFauna::FFaunaProfile& InProfile);
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
	/** Whether the leapers can be drawn: M_Fauna has its leaper branch, or slonk.Fauna.Debug draws them in the basic
	 *  material (each fish still at its home on the sea). While they cannot, the world keeps none. */
	bool CanDrawLeapers() const { return bLeaperMaterial || bLeaperDebug; }
	/** Whether the jellies can be drawn: M_Fauna has its floater branch, or slonk.Fauna.Debug draws them in the basic
	 *  material (each still, its tentacles hanging). While they cannot, the world keeps none. */
	bool CanDrawFloaters() const { return bFloaterMaterial || bFloaterDebug; }
	/** The group drawn by an id (none if it is not drawn). */
	const SlonkFauna::FFaunaGroup* GetDrawnGroup(uint64 Id) const;
	/** Where a jelly is now, planet-local: its path's place off by any fright it is still in. */
	FVector FloaterPlaceNow(const SlonkFauna::FFaunaGroup& G) const;
	/** How many splashes are in the air now, and how many fish are out of the water (or about to be, under it) now. */
	int32 GetSplashCount() const { return SplashPool.NumLive(); }
	int32 GetLeapingCount() const;
	/** Debug (fauna watch): the group drawn nearest a point in the world (of a kind, EFaunaKind as an int, or of any for
	 *  -1), or, given the group the last fauna goto found, the watch's pick (SlonkFauna::TakeWatched, which forgets it once
	 *  taken); and where a drawn group is and how it is turned (its frame, its flight along X), in the world; false when it
	 *  is not drawn. */
	uint64 NearestDrawnGroup(const FVector& WorldPos, int32 Kind = -1, uint64* GotoId = nullptr) const;
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
	/** The world's species, a line each, kind by kind: its plan in words, its share of its kind's cells, its size (fauna
	 *  plans). */
	void DescribePlans(TArray<FString>& OutLines) const;
	/** The world as the settings have it now, given to the field (a change lets every group go) and the motes' to the
	 *  terrain: each view update does it, and a debug command that changed a setting does it at once, so the line it reads
	 *  out after is the settings' as they now stand. */
	void RefreshWorld();
	/** The material instance a group of a plan of a kind is drawn in (the kind's own for an Earth plan, else its plan's,
	 *  made the first time it is asked for, its world's meshes held again if it was suspended), and how many instances of
	 *  plans are kept: one for each alien plan of the profile that has been drawn, none of a profile left behind nor of a
	 *  world suspended. */
	UMaterialInstanceDynamic* MaterialForPlan(EFaunaKind Kind, const FFaunaBodyPlan& Plan);
	int32 NumPlanMaterials() const { return HerdPlanMIDs.Num() + FlierPlanMIDs.Num() + LeaperPlanMIDs.Num(); }

	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void BeginDestroy() override;

private:
	/** A profile taken: its plans' meshes held, its predecessor's plans' material instances let go. */
	void AdoptProfile(const SlonkFauna::FFaunaProfile& InProfile);
	/** Its profile's plans' meshes held again if a suspension let them go (before a group is drawn or its material asked
	 *  for), and whether they are held now. */
	void HoldOwnPlans();
	bool bHoldingPlans = false;
	/** A group of leapers as it is drawn: each fish's custom floats (as its set was given them) and home in the group's
	 *  frame; and the sea's surface each fish's leap to read crosses, as last read (SlonkFauna::LeaperGroupData). */
	struct FLeaperState
	{
		TArray<float> Custom;
		TArray<FVector> Home;
		SlonkFauna::FLeaperSurfaceCache Surface;
	};
	struct FGroupSet
	{
		SlonkFauna::FFaunaField::FGroupRef Group;
		UPlanetInstancesComponent* Set = nullptr;
		bool bShadow = false;
		/** Its frame as last drawn, and when (the paths' clock): a shaken group wheels round from it. */
		SlonkFauna::FFaunaPose Pose;
		double PoseAt = 0.0;
		/** A herd's clocks, ground and place (SlonkFauna::StepHerd), and how many frames it has been about; the water about
		 *  it as its startles and the crew have read it (kept while it is about). */
		TSharedPtr<SlonkFauna::FHerdState> Herd;
		TSharedPtr<SlonkFauna::FHerdWater> Water;
		uint32 Frames = 0;
		/** How wet the ground under a herd raising dust was when last read, and when (the world's seconds); whether it
		 *  goes fast enough to raise any, its speed smoothed (SlonkFauna::StepDustGate). */
		double DustWet = 0.5, DustWetAt = -1.0e9;
		/** How much of the sun's light the clouds kept from it then (APlanetBody::CloudShadeAt), and how much of the sky
		 *  over it they covered, read with the wetness. */
		double DustShade = 0.0;
		float DustCover = 0.f;
		SlonkFauna::FDustGate DustGate;
		/** A group of leapers' fish and the sea's surface their leaps cross. */
		TSharedPtr<FLeaperState> Leaper;
	};
	using FScanRef = TSharedPtr<SlonkFauna::FFaunaField::FScan, ESPMode::ThreadSafe>;

	void EnsureMaterial();
	/** The herds' material instance (M_Fauna, its Walkers on), looked for again every few seconds while M_Fauna has no
	 *  walker branch, so one built during play is taken up. */
	void EnsureHerdMaterial();
	/** The material instance a herd of a plan is drawn in: the herds' own for the Earth grazer (the walker branch's
	 *  defaults are its rig), else one of the plan's own, made from M_Fauna the first time a herd of it comes and kept
	 *  while the herds' is (its walker branch given the plan's rig: Walker::FRig). The herds' own while they are drawn in
	 *  the debug's basic material. */
	UMaterialInstanceDynamic* HerdMaterialFor(const FFaunaBodyPlan& Plan);
	/** The material instance a flock of a plan is drawn in: the fliers' own for the Earth's bird, else one of the plan's
	 *  own made from M_Fauna the first time a flock of it comes (its wings, SlonkFauna::Wings, and its glow organs given),
	 *  kept while the fliers' is; the fliers' own while M_Fauna has no WingForm (built before the alien fliers: their wings
	 *  then beat as a bird's) or they are drawn in the debug's basic material. */
	UMaterialInstanceDynamic* FlierMaterialFor(const FFaunaBodyPlan& Plan);
	/** As the fliers', a group of leapers' (its form, SlonkFauna::FishForm, and its glow organs given): the leapers' own for
	 *  the Earth's fish, or while M_Fauna has no FishForm. */
	UMaterialInstanceDynamic* LeaperMaterialFor(const FFaunaBodyPlan& Plan);
	/** Gives a plan's instance the row of its glow organs (SlonkFaunaMeshes::GlowRowOf: none for a plan without them) and
	 *  the world's glow they light in. */
	void SetGlowParams(UMaterialInstanceDynamic* M, const FFaunaBodyPlan& Plan) const;
	/** A herd's step this frame: its frame on the drawn ground, its fright, the crew, its material's data, its dust. */
	void UpdateHerd(uint64 Id, FGroupSet& S, const TArray<SlonkFauna::FFaunaThreat>& Threats, const FVector& CamLocal, const FVector& Sun, double Now, double Dt);
	/** A herd's dust this frame (how deep in the night it stands, and the frame's step, s): raised, kept up or given back
	 *  to the pool; its puffs born moving with the ground as the planet turns (User.Carry). */
	void UpdateDust(uint64 Id, FGroupSet& S, const FVector& CamLocal, float Night, double Dt);
	/** Gives a herd's dust back, its puffs left to fade. */
	void ReleaseDust(uint64 Id);
	/** NS_HerdDust, loaded the first time a herd would raise dust (false, and one line in the log, without it). */
	bool EnsureDustSystem();
	UNiagaraComponent* MakeDustComponent();
	/** The leapers' material instance (M_Fauna, its Leapers on), looked for as the herds' is. */
	void EnsureLeaperMaterial();
	/** A group of leapers' step this frame (the world's seconds, and the last frame's): its frame with its shoal, its dive,
	 *  the sea's surface its fish's leaps cross, and the splashes of the leaps that crossed it since the last frame. */
	void UpdateLeaper(uint64 Id, FGroupSet& S, const TArray<SlonkFauna::FFaunaThreat>& Threats, const FVector& CamLocal, const FVector& Sun, double WorldNow, double WorldFrom);
	/** The jellies' material instance (M_Fauna, its Floaters on, its GlowColor the world's), looked for as the herds' is. */
	void EnsureFloaterMaterial();
	/** A jelly's step this frame (the paths' clock): its frame on its loop or squeezing away from a fright, the threats to
	 *  it, what its material reads (its fright, its drift through the air), its shadow. */
	void UpdateFloater(uint64 Id, FGroupSet& S, const TArray<SlonkFauna::FFaunaThreat>& Threats, const FVector& CamLocal, const FVector& Sun, double Now);
	/** A splash where a leap crossed the sea's surface (planet-local), of a fish of that length; an entry or not; how deep
	 *  in the night it is there. */
	void ThrowSplash(const FVector& PlaceLocal, double SizeCm, bool bEntry, float Night);
	/** How much of the sun's light the clouds keep from a place in the planet's frame, 0..1 (APlanetBody::CloudShadeAt,
	 *  by the sun as the last UpdateView had it, at the strength the game mode casts its cloud layer's shadow with); 0
	 *  with slonk.Fauna.CloudShade 0, where the game draws no cloud layer to cast a shadow, or on no planet. */
	double CloudShadeAt(const FVector& PlaceLocal, float* OutCover = nullptr) const;
	/** The way to the sun in the world, as the last UpdateView was given it. */
	FVector SunWorld = FVector::ZeroVector;
	/** NS_FishSplash, loaded the first time a splash would be thrown (false, and one line in the log, without it). */
	bool EnsureSplashSystem();
	UNiagaraComponent* MakeSplashComponent();
	/** The ground's own velocity through the world at a world place, as the planet turns (User.Carry): with bOverFrame,
	 *  over this frame's tick of an emitter (for the dust, set again each frame), else as it is (for a one-shot's burst). */
	FVector GroundCarryAt(const FVector& WorldPos, bool bOverFrame) const;
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
	/** The alien fliers' material instances, one to a plan (by its hash), and whether M_Fauna has their wings. */
	UPROPERTY() TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>> FlierPlanMIDs;
	bool bAlienFlierMaterial = false;
	/** The world's glow as last given to the alien plans' instances (their glow organs'). */
	FLinearColor PlanGlowSet = FLinearColor::Transparent;
	/** slonk.Fauna.Debug as the groups drawn have it, and whether MID is the basic material it asked for. */
	int32 DebugDrawn = 0;
	bool bPlainAsked = false;
	float LastPxPerRad = -1.f;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HerdMID;
	/** The alien walkers' material instances, one to a plan (by its hash): a world's few. */
	UPROPERTY() TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>> HerdPlanMIDs;
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

	/** The seas as the terrain material paints them. */
	SlonkFauna::FFaunaSea Sea;
	/** The leapers' material instance, as the herds' is kept. */
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> LeaperMID;
	bool bLeaperMaterial = false;
	/** The alien fish's material instances, one to a plan, and whether M_Fauna has their forms (its FishForm). */
	UPROPERTY() TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>> LeaperPlanMIDs;
	bool bAlienLeaperMaterial = false;
	bool bLeaperDebug = false;
	int32 LeaperMIDDebug = -1;
	double LeaperProbedAt = -1.0e9;
	float LastLeaperPxPerRad = -1.f;
	/** Each diving group of leapers' dive, by its id, on the world's own seconds (the leaps' clock): kept while it lasts,
	 *  whether the group is about or not. */
	TMap<uint64, SlonkFauna::FPanic> LeaperDives;
	/** The world's seconds the leapers were last stepped at (the splashes are of the crossings since). */
	double LastLeaperWorldSec = -1.0;
	/** The splashes: their system (none until a splash would first be thrown; bSplashMissing once it was looked for and not
	 *  found), every component made of it (kept here from the garbage collector) and the pool lending them. */
	UPROPERTY() TObjectPtr<UNiagaraSystem> SplashSystem;
	bool bSplashMissing = false;
	UPROPERTY() TArray<TObjectPtr<UNiagaraComponent>> SplashComponents;
	SlonkFauna::FSplashPool SplashPool;

	/** The jellies' material instance, as the herds' is kept, and the glow colour it was last given (the profile's). */
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FloaterMID;
	bool bFloaterMaterial = false;
	bool bFloaterDebug = false;
	int32 FloaterMIDDebug = -1;
	double FloaterProbedAt = -1.0e9;
	float LastFloaterPxPerRad = -1.f;
	FLinearColor FloaterGlowSet = FLinearColor::Transparent;
};
