#include "PlanetFaunaComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FaunaMeshes.h"
#include "FaunaProfile.h"
#include "GameClock.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "NiagaraComponent.h"
#include "NiagaraSettings.h"
#include "NiagaraSystem.h"
#include "PersonPawn.h"
#include "PlanetBody.h"
#include "PlanetInstancesComponent.h"
#include "PlanetTerrainComponent.h"
#include "ScatterSpot.h"
#include "SlonkMaterials.h"
#include "StarshipMovementComponent.h"
#include "StarshipPawn.h"
#include "TerrainScatter.h"

static_assert(SlonkFauna::Custom::Num == SlonkScatter::NumCustomFloats, "every planet instance has the scatter's count of custom floats, so one pass writes them all");

// The animals on the worlds with life. 0 takes every one away, the seas' plankton and shoals (APlanetBody) and the motes
// over the ground with them, so a world looks as it did before it had any (for captures to compare with).
static TAutoConsoleVariable<int32> CVarFaunaEnable(TEXT("slonk.Fauna.Enable"), 1,
	TEXT("The animals on the worlds with life: the flocks, the herds, the leaping fish and their splashes, the sky-jellies of the strange worlds, the seas' plankton and shoals, the pollen, gnats and fireflies over the ground (1, the default), or none, every world as it was before them (0)"));
static TAutoConsoleVariable<float> CVarFaunaDensity(TEXT("slonk.Fauna.Density"), 1.f,
	TEXT("Scales the share of the habitat's cells holding a group of animals, and of the ground's cells holding a cloud of motes (1 as designed, 0 none; the caps on groups and members still hold)"));

bool SlonkFauna::IsEnabled() { return CVarFaunaEnable.GetValueOnGameThread() != 0; }
float SlonkFauna::DensitySetting() { return FMath::Max(CVarFaunaDensity.GetValueOnGameThread(), 0.f); }

UPlanetFaunaComponent::UPlanetFaunaComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetMobility(EComponentMobility::Movable);
}

void UPlanetFaunaComponent::Configure(UPlanetTerrainComponent* InTerrain, const FPlanetTerrainParams& P, const SlonkFauna::FFaunaProfile& InProfile, float InLife, const FScatterPalette& InPalette,
	bool bInTowns, const FCivilization& InPeople)
{
	Terrain = InTerrain;
	Params = P;
	AdoptProfile(InProfile);
	Life = FMath::Clamp(InLife, 0.f, 1.f);
	Palette = InPalette;
	bTowns = bInTowns;
	People = InPeople;
	bConfigured = true;
	RefreshWorld();
}

void UPlanetFaunaComponent::SetProfile(const SlonkFauna::FFaunaProfile& InProfile)
{
	AdoptProfile(InProfile);
}

TArray<TPair<FFaunaBodyPlan, EFaunaKind>> SlonkFauna::PlansOf(const FFaunaProfile& Profile)
{
	TArray<TPair<FFaunaBodyPlan, EFaunaKind>> Out;
	for (const EFaunaKind K : { EFaunaKind::Flier, EFaunaKind::Herd, EFaunaKind::Leaper, EFaunaKind::Floater })
	{
		const FFaunaKindSpecies& Sp = Profile.Species[(int32)K];
		for (int32 i = 0; i < Sp.Num; ++i) { Out.Add(TPair<FFaunaBodyPlan, EFaunaKind>(Sp[i].Plan, K)); }
	}
	return Out;
}

void UPlanetFaunaComponent::AdoptProfile(const SlonkFauna::FFaunaProfile& InProfile)
{
	Profile = InProfile;
	// Its species' meshes held (in place of the profile's before; a world suspended lets them go until it draws again:
	// Suspend, HoldOwnPlans), and the material instances of the plans it no longer has let go, so none of them is kept or
	// written to each frame.
	const TArray<TPair<FFaunaBodyPlan, EFaunaKind>> Plans = SlonkFauna::PlansOf(Profile);
	SlonkFaunaMeshes::HoldPlans(this, Plans);
	bHoldingPlans = true;
	TSet<uint32> Live;
	for (const TPair<FFaunaBodyPlan, EFaunaKind>& P : Plans) { Live.Add(P.Key.Hash()); }
	for (TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>>* Plan : { &HerdPlanMIDs, &FlierPlanMIDs, &LeaperPlanMIDs })
	{
		for (auto It = Plan->CreateIterator(); It; ++It) { if (!Live.Contains(It.Key())) { It.RemoveCurrent(); } }
	}
}

void UPlanetFaunaComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	// Its plans' meshes no longer held by it.
	SlonkFaunaMeshes::HoldPlans(this, {});
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UPlanetFaunaComponent::BeginDestroy()
{
	// Gone without being destroyed as a component (collected): its plans forgotten, their meshes let go at the next hold.
	SlonkFaunaMeshes::HoldPlans(this, {}, false);
	Super::BeginDestroy();
}

void UPlanetFaunaComponent::HoldOwnPlans()
{
	// Drawn again after a suspension: its species' meshes held once more (each made anew as a group of it comes), once.
	if (bHoldingPlans) { return; }
	SlonkFaunaMeshes::HoldPlans(this, SlonkFauna::PlansOf(Profile));
	bHoldingPlans = true;
}

UMaterialInstanceDynamic* UPlanetFaunaComponent::MaterialForPlan(EFaunaKind Kind, const FFaunaBodyPlan& Plan)
{
	HoldOwnPlans();
	if (Kind == EFaunaKind::Herd) { EnsureHerdMaterial(); return HerdMaterialFor(Plan); }
	if (Kind == EFaunaKind::Leaper) { EnsureLeaperMaterial(); return LeaperMaterialFor(Plan); }
	if (Kind == EFaunaKind::Floater) { EnsureFloaterMaterial(); return FloaterMID.Get(); }
	EnsureMaterial();
	return FlierMaterialFor(Plan);
}

void UPlanetFaunaComponent::SetSea(const SlonkFauna::FFaunaSea& InSea)
{
	if (InSea == Sea) { return; }
	Sea = InSea;
	RefreshWorld();
}

SlonkScatter::FMoteSettings UPlanetFaunaComponent::MakeMoteSettings() const
{
	SlonkScatter::FMoteSettings M;
	M.Life = Life;
	M.Density = SlonkFauna::DensitySetting();
	M.bEnabled = bConfigured && SlonkFauna::IsEnabled() && Life > 0.f && M.Density > 0.f && !Params.bAirless && (KindMask & SlonkFauna::KindBit(EFaunaKind::Mote)) != 0u;
	M.Pollen = Profile.Look.Pollen;
	M.Glow = Profile.Look.Firefly;
	return M;
}

SlonkFauna::FFaunaWorld UPlanetFaunaComponent::MakeFaunaWorld() const
{
	SlonkFauna::FFaunaWorld W;
	W.P = Params;
	W.Profile = Profile;
	W.Palette = Palette;
	W.Life = Life;
	W.Density = SlonkFauna::DensitySetting();
	W.KindMask = KindMask;
	W.bTowns = bTowns;
	W.People = People;
	W.Sea = Sea;
	// The herds only while something can draw them (M_Fauna's walker branch, or the debug's basic material); the leapers
	// likewise (its leaper branch).
	if (!CanDrawHerds()) { W.KindMask &= ~SlonkFauna::KindBit(EFaunaKind::Herd); }
	if (!CanDrawLeapers()) { W.KindMask &= ~SlonkFauna::KindBit(EFaunaKind::Leaper); }
	// And the jellies (its floater branch).
	if (!CanDrawFloaters()) { W.KindMask &= ~SlonkFauna::KindBit(EFaunaKind::Floater); }
	return W;
}

void UPlanetFaunaComponent::RefreshWorld()
{
	if (!bConfigured || !Terrain) { return; }
	// Whether the herds, the leapers and the jellies can be drawn, before the world they are in is made.
	EnsureHerdMaterial();
	EnsureLeaperMaterial();
	EnsureFloaterMaterial();
	// The motes on the terrain's chunks, as the settings have them now (nothing is built again unless they moved).
	Terrain->SetMoteOptions(MakeMoteSettings());
	const SlonkFauna::FFaunaWorld W = MakeFaunaWorld();
	if (Field.HasWorld() && W == Field.GetWorld()) { return; }
	// Another world, density, kinds or profile: every group goes, and the cells are read again. Its seas alone (fauna
	// switched off or on): only the leapers go, and only their cells are read again.
	if (Field.SetWorld(W))
	{
		TArray<uint64> Leapers;
		for (const TPair<uint64, FGroupSet>& S : Sets) { if (S.Value.Group->Kind == EFaunaKind::Leaper) { Leapers.Add(S.Key); } }
		for (const uint64 Id : Leapers) { ReleaseSet(Id); }
	}
	else { ReleaseAllSets(); }
	++Generation;
}

double UPlanetFaunaComponent::ClockSeconds() const
{
	const UWorld* World = GetWorld();
	return (World ? World->GetTimeSeconds() : 0.0) + TimeOffset;
}

void UPlanetFaunaComponent::SetTimeOffset(double Seconds)
{
	TimeOffset = Seconds;
	// A fright is on the paths' clock: moved, it would be somewhere else in its flight.
	Panics.Reset();
}

int32 UPlanetFaunaComponent::GetMemberCount() const
{
	int32 N = 0;
	for (const TPair<uint64, FGroupSet>& S : Sets) { N += S.Value.Group->Members; }
	return N;
}

// A look at the animals with their material set aside: 1 draws them in the engine's basic material (no wingbeat, no
// fade), 2 also twenty times their size, so where the groups are and that they draw can be seen apart from M_Fauna.
// A change lets every group go, and they come back drawn as it asks (UpdateView).
static TAutoConsoleVariable<int32> CVarFaunaDebug(TEXT("slonk.Fauna.Debug"), 0,
	TEXT("1: the animals in the engine's basic material (the motes too, as plain cards where they stand, M_Fauna's mote branch or no); 2: that, the flocks at twenty times their size (every group drawn anew as it changes)"));

int32 SlonkFauna::DebugSetting() { return FMath::Max(CVarFaunaDebug.GetValueOnGameThread(), 0); }

float SlonkFauna::PixelsPerRadian(const APlayerController* PC)
{
	float Fov = 90.f;
	if (PC && PC->PlayerCameraManager) { Fov = PC->PlayerCameraManager->GetFOVAngle(); }
	FVector2D Viewport(1920.0, 1080.0);
	if (GEngine && GEngine->GameViewport) { GEngine->GameViewport->GetViewportSize(Viewport); }
	return 0.5f * (float)FMath::Max(Viewport.X, 1.0) / FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(Fov, 5.f, 170.f)) * 0.5f);
}

void UPlanetFaunaComponent::EnsureMaterial()
{
	// Made again when slonk.Fauna.Debug asks for the other material.
	const bool bPlain = DebugDrawn > 0;
	if (MID && bPlain == bPlainAsked) { return; }
	bPlainAsked = bPlain;
	UMaterialInterface* Mat = bPlain ? nullptr : LoadObject<UMaterialInterface>(nullptr, SlonkFauna::MaterialPath, nullptr, LOAD_Quiet | LOAD_NoWarn);
	bFaunaMaterial = Mat != nullptr;
	MID = nullptr;
	LastPxPerRad = -1.f;
	// The alien plans' fliers are drawn in instances of their own, made from it as their flocks come (FlierMaterialFor):
	// only when M_Fauna has their wings (its WingForm parameter); else in the fliers' own, their wings beaten as a bird's.
	FlierPlanMIDs.Reset();
	float Probe = 0.f;
	bAlienFlierMaterial = Mat && Mat->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("WingForm")), Probe);
	if (!Mat)
	{
		static bool bWarned = false;
		if (!bPlain && !bWarned)
		{
			bWarned = true;
			UE_LOG(LogTemp, Warning, TEXT("Fauna: %s not found (tools/planet_materials/fauna_material.py builds it): the animals are drawn in the engine's basic material, their wings still and nothing faded"), SlonkFauna::MaterialPath);
		}
		Mat = LoadObject<UMaterialInterface>(nullptr, SlonkMaterials::FallbackLitPath);
	}
	if (Mat) { MID = UMaterialInstanceDynamic::Create(Mat, this); }
}

void UPlanetFaunaComponent::EnsureHerdMaterial()
{
	// Looked for again when the debug setting changes, and every few seconds while M_Fauna has no walker branch.
	const int32 Debug = CVarFaunaDebug.GetValueOnGameThread();
	const double Now = FPlatformTime::Seconds();
	if (HerdMIDDebug == Debug && (bWalkerMaterial || Debug > 0 || Now - HerdProbedAt < 5.0)) { return; }
	HerdMIDDebug = Debug;
	HerdProbedAt = Now;
	LastHerdPxPerRad = -1.f;
	bHerdDebug = Debug > 0;
	// M_Fauna draws the walkers in its walker branch, which its Walkers parameter switches on: one built before the
	// branch has no such parameter, and would draw them as birds; then there are none. slonk.Fauna.Debug draws them in
	// the engine's basic material instead, standing still on their herd's plane.
	UMaterialInterface* Mat = Debug > 0 ? nullptr : LoadObject<UMaterialInterface>(nullptr, SlonkFauna::MaterialPath, nullptr, LOAD_Quiet | LOAD_NoWarn);
	float Probe = 0.f;
	const bool bWalkers = Mat && Mat->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Walkers")), Probe);
	if (bWalkers == bWalkerMaterial && HerdMID && Debug == 0) { return; }
	bWalkerMaterial = bWalkers;
	HerdMID = nullptr;
	HerdPlanMIDs.Reset();
	if (bWalkers)
	{
		HerdMID = UMaterialInstanceDynamic::Create(Mat, this);
		HerdMID->SetScalarParameterValue(TEXT("Walkers"), 1.f);
	}
	else if (Debug > 0)
	{
		if (UMaterialInterface* Basic = LoadObject<UMaterialInterface>(nullptr, SlonkMaterials::FallbackLitPath)) { HerdMID = UMaterialInstanceDynamic::Create(Basic, this); }
	}
	else
	{
		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogTemp, Warning, TEXT("Fauna: %s has no walker branch yet (tools/planet_materials/fauna_material.py builds it): no herds until it does"), SlonkFauna::MaterialPath);
		}
	}
	// The herds already drawn take the new instance (or their plan's, made from it), so every herd is drawn in one the
	// camera's view is given to (with none, the world keeps no herds and lets them go: RefreshWorld).
	if (HerdMID)
	{
		for (TPair<uint64, FGroupSet>& Pair : Sets)
		{
			if (!Pair.Value.Herd.IsValid() || !Pair.Value.Set) { continue; }
			UMaterialInstanceDynamic* PlanMat = HerdMaterialFor(Pair.Value.Group->Plan);
			if (Pair.Value.Set->GetMaterial(0) != PlanMat) { Pair.Value.Set->SetMaterial(0, PlanMat); }
		}
	}
}

UMaterialInstanceDynamic* UPlanetFaunaComponent::HerdMaterialFor(const FFaunaBodyPlan& Plan)
{
	if (!HerdMID) { return nullptr; }
	const SlonkFauna::Walker::FRig Rig = SlonkFauna::Walker::ShapeOf(Plan).Rig;
	if (!bWalkerMaterial || Rig == SlonkFauna::Walker::FRig() || !HerdMID->Parent) { return HerdMID; }
	const uint32 Key = Plan.Hash();
	if (const TObjectPtr<UMaterialInstanceDynamic>* Found = HerdPlanMIDs.Find(Key)) { return Found->Get(); }
	// Its walker branch given its plan's rig: its neck's pivot, its feet's lift, its head's swing, its carapace. Constant
	// for the plan, so nothing of it moves from frame to frame.
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(HerdMID->Parent, this);
	M->SetScalarParameterValue(TEXT("Walkers"), 1.f);
	M->SetScalarParameterValue(TEXT("WalkerNeckX"), Rig.NeckX);
	M->SetScalarParameterValue(TEXT("WalkerNeckZ"), Rig.NeckZ);
	M->SetScalarParameterValue(TEXT("WalkerLift"), Rig.LiftCm);
	M->SetScalarParameterValue(TEXT("WalkerPitch"), Rig.PitchScale);
	M->SetScalarParameterValue(TEXT("WalkerCarapace"), Rig.Carapace);
	if (LastHerdPxPerRad > 0.f) { M->SetScalarParameterValue(TEXT("PxPerRad"), LastHerdPxPerRad); }
	SetGlowParams(M, Plan);
	HerdPlanMIDs.Add(Key, M);
	return M;
}

void UPlanetFaunaComponent::SetGlowParams(UMaterialInstanceDynamic* M, const FFaunaBodyPlan& Plan) const
{
	// Its glow organs' row along its flanks, and the world's glow they light in (none on a plan without them).
	const SlonkFauna::GlowOrgan::FRow Row = SlonkFaunaMeshes::GlowRowOf(Plan);
	M->SetScalarParameterValue(TEXT("GlowSpots"), (float)Row.Spots);
	M->SetVectorParameterValue(TEXT("GlowRow"), FLinearColor(Row.U0, Row.U1, Row.V, 0.f));
	M->SetVectorParameterValue(TEXT("GlowSize"), FLinearColor(Row.RU, Row.RV, 0.f, 0.f));
	M->SetVectorParameterValue(TEXT("GlowColor"), Profile.Look.Glow);
}

UMaterialInstanceDynamic* UPlanetFaunaComponent::FlierMaterialFor(const FFaunaBodyPlan& Plan)
{
	if (!MID) { return nullptr; }
	if (!bFaunaMaterial || !bAlienFlierMaterial || SlonkFauna::IsEarthPlan(EFaunaKind::Flier, Plan) || !MID->Parent) { return MID; }
	const uint32 Key = Plan.Hash();
	if (const TObjectPtr<UMaterialInstanceDynamic>* Found = FlierPlanMIDs.Find(Key)) { return Found->Get(); }
	// Its wings (SlonkFauna::Wings) and its glow organs, constant for the plan, so nothing of it moves from frame to frame.
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(MID->Parent, this);
	M->SetScalarParameterValue(TEXT("WingForm"), (float)SlonkFauna::Wings::FormOf(Plan));
	if (LastPxPerRad > 0.f) { M->SetScalarParameterValue(TEXT("PxPerRad"), LastPxPerRad); }
	SetGlowParams(M, Plan);
	FlierPlanMIDs.Add(Key, M);
	return M;
}

UMaterialInstanceDynamic* UPlanetFaunaComponent::LeaperMaterialFor(const FFaunaBodyPlan& Plan)
{
	if (!LeaperMID) { return nullptr; }
	if (!bLeaperMaterial || !bAlienLeaperMaterial || SlonkFauna::IsEarthPlan(EFaunaKind::Leaper, Plan) || !LeaperMID->Parent) { return LeaperMID; }
	const uint32 Key = Plan.Hash();
	if (const TObjectPtr<UMaterialInstanceDynamic>* Found = LeaperPlanMIDs.Find(Key)) { return Found->Get(); }
	// Its form (SlonkFauna::FishForm) and its glow organs, constant for the plan.
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(LeaperMID->Parent, this);
	M->SetScalarParameterValue(TEXT("Leapers"), 1.f);
	M->SetScalarParameterValue(TEXT("FishForm"), (float)SlonkFauna::FishForm::FormOf(Plan));
	if (LastLeaperPxPerRad > 0.f) { M->SetScalarParameterValue(TEXT("PxPerRad"), LastLeaperPxPerRad); }
	SetGlowParams(M, Plan);
	LeaperPlanMIDs.Add(Key, M);
	return M;
}

void UPlanetFaunaComponent::EnsureLeaperMaterial()
{
	// As the herds' (EnsureHerdMaterial): looked for again when the debug setting changes, and every few seconds while
	// M_Fauna has no leaper branch (its Leapers parameter); one built before it would draw the fish as birds, so then
	// there are none. slonk.Fauna.Debug draws them in the engine's basic material, each still at its home on the sea.
	const int32 Debug = CVarFaunaDebug.GetValueOnGameThread();
	const double Now = FPlatformTime::Seconds();
	if (LeaperMIDDebug == Debug && (bLeaperMaterial || Debug > 0 || Now - LeaperProbedAt < 5.0)) { return; }
	LeaperMIDDebug = Debug;
	LeaperProbedAt = Now;
	LastLeaperPxPerRad = -1.f;
	bLeaperDebug = Debug > 0;
	UMaterialInterface* Mat = Debug > 0 ? nullptr : LoadObject<UMaterialInterface>(nullptr, SlonkFauna::MaterialPath, nullptr, LOAD_Quiet | LOAD_NoWarn);
	float Probe = 0.f;
	const bool bLeapers = Mat && Mat->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Leapers")), Probe);
	if (bLeapers == bLeaperMaterial && LeaperMID && Debug == 0) { return; }
	bLeaperMaterial = bLeapers;
	LeaperMID = nullptr;
	LeaperPlanMIDs.Reset();
	bAlienLeaperMaterial = bLeapers && Mat->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("FishForm")), Probe);
	if (bLeapers)
	{
		LeaperMID = UMaterialInstanceDynamic::Create(Mat, this);
		LeaperMID->SetScalarParameterValue(TEXT("Leapers"), 1.f);
	}
	else if (Debug > 0)
	{
		if (UMaterialInterface* Basic = LoadObject<UMaterialInterface>(nullptr, SlonkMaterials::FallbackLitPath)) { LeaperMID = UMaterialInstanceDynamic::Create(Basic, this); }
	}
	else
	{
		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogTemp, Warning, TEXT("Fauna: %s has no leaper branch yet (tools/planet_materials/fauna_material.py builds it): no leaping fish until it does"), SlonkFauna::MaterialPath);
		}
	}
	if (LeaperMID)
	{
		for (TPair<uint64, FGroupSet>& Pair : Sets)
		{
			if (!Pair.Value.Leaper.IsValid() || !Pair.Value.Set) { continue; }
			UMaterialInstanceDynamic* PlanMat = LeaperMaterialFor(Pair.Value.Group->Plan);
			if (Pair.Value.Set->GetMaterial(0) != PlanMat) { Pair.Value.Set->SetMaterial(0, PlanMat); }
		}
	}
}

void UPlanetFaunaComponent::EnsureFloaterMaterial()
{
	// As the leapers' (EnsureLeaperMaterial): looked for again when the debug setting changes, and every few seconds while
	// M_Fauna has no floater branch (its Floaters parameter); one built before it would draw the jellies as birds, so then
	// there are none. slonk.Fauna.Debug draws them in the engine's basic material, still. Its glow the world's own colour,
	// given again whenever the profile's changes (fauna strange).
	const int32 Debug = CVarFaunaDebug.GetValueOnGameThread();
	const double Now = FPlatformTime::Seconds();
	if (!(FloaterMIDDebug == Debug && (bFloaterMaterial || Debug > 0 || Now - FloaterProbedAt < 5.0)))
	{
		FloaterMIDDebug = Debug;
		FloaterProbedAt = Now;
		LastFloaterPxPerRad = -1.f;
		bFloaterDebug = Debug > 0;
		UMaterialInterface* Mat = Debug > 0 ? nullptr : LoadObject<UMaterialInterface>(nullptr, SlonkFauna::MaterialPath, nullptr, LOAD_Quiet | LOAD_NoWarn);
		float Probe = 0.f;
		const bool bFloaters = Mat && Mat->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Floaters")), Probe);
		if (!(bFloaters == bFloaterMaterial && FloaterMID && Debug == 0))
		{
			bFloaterMaterial = bFloaters;
			FloaterMID = nullptr;
			FloaterGlowSet = FLinearColor::Transparent;
			if (bFloaters)
			{
				FloaterMID = UMaterialInstanceDynamic::Create(Mat, this);
				FloaterMID->SetScalarParameterValue(TEXT("Floaters"), 1.f);
			}
			else if (Debug > 0)
			{
				if (UMaterialInterface* Basic = LoadObject<UMaterialInterface>(nullptr, SlonkMaterials::FallbackLitPath)) { FloaterMID = UMaterialInstanceDynamic::Create(Basic, this); }
			}
			else
			{
				static bool bWarned = false;
				if (!bWarned)
				{
					bWarned = true;
					UE_LOG(LogTemp, Warning, TEXT("Fauna: %s has no floater branch yet (tools/planet_materials/fauna_material.py builds it): no sky-jellies until it does"), SlonkFauna::MaterialPath);
				}
			}
			if (FloaterMID)
			{
				for (TPair<uint64, FGroupSet>& Pair : Sets)
				{
					if (Pair.Value.Group->Kind == EFaunaKind::Floater && Pair.Value.Set && Pair.Value.Set->GetMaterial(0) != FloaterMID) { Pair.Value.Set->SetMaterial(0, FloaterMID); }
				}
			}
		}
	}
	if (FloaterMID && bFloaterMaterial && FloaterGlowSet != Profile.Look.Glow)
	{
		FloaterGlowSet = Profile.Look.Glow;
		FloaterMID->SetVectorParameterValue(TEXT("GlowColor"), Profile.Look.Glow);
	}
}

double UPlanetFaunaComponent::GroundHeightAt(const FVector& Dir) const
{
	double H = 0.0;
	if (Terrain && Terrain->DrawnHeightAtLocalDir(Dir, H)) { return H; }
	// Where nothing is drawn, the field's, finer than ten metres left out (there is nothing to stand on that would show).
	return FMath::Max(SlonkTerrain::Sample(Params, Dir, 1000.0).HeightCm, 0.0);
}

UPlanetInstancesComponent* UPlanetFaunaComponent::AcquireSet()
{
	if (FreeSets.Num() > 0) { return FreeSets.Pop(EAllowShrinking::No); }
	UPlanetInstancesComponent* C = NewObject<UPlanetInstancesComponent>(GetOwner() ? (UObject*)GetOwner() : (UObject*)this, NAME_None, RF_Transient);
	C->SetNumCustomDataFloats(SlonkFauna::Custom::Num);
	C->SetMobility(EComponentMobility::Movable);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetCanEverAffectNavigation(false);
	C->bAffectDistanceFieldLighting = false;
	C->bReceivesDecals = false;
	// Shadows near the camera alone (UpdateView), as the moving casters invalidate the shadow maps' pages they touch.
	C->CastShadow = false;
	C->bCastDynamicShadow = true;
	// The wings beat at any distance (a flock a kilometre off is its specks' twinkle), never in the ray-traced scene.
	C->bEvaluateWorldPositionOffsetInRayTracing = false;
	C->SetWorldPositionOffsetDisableDistance(0);
	C->SetVisibility(false);
	C->RegisterComponent();
	SetPool.Add(C);
	return C;
}

void UPlanetFaunaComponent::SpawnSet(const SlonkFauna::FFaunaField::FGroupRef& G)
{
	if (!G.IsValid() || Sets.Contains(G->Id)) { return; }
	// Its species' mesh (made once a process for each plan, so a world makes one for each of its species), held by its
	// world again if it was suspended.
	HoldOwnPlans();
	UStaticMesh* Mesh = SlonkFaunaMeshes::GetMesh(G->Plan, G->Kind);
	if (!Mesh) { return; }
	EnsureMaterial();
	const bool bHerd = G->Kind == EFaunaKind::Herd;
	const bool bLeaper = G->Kind == EFaunaKind::Leaper;
	const bool bFloater = G->Kind == EFaunaKind::Floater;
	UMaterialInstanceDynamic* Mat = bHerd ? HerdMaterialFor(G->Plan) : (bLeaper ? LeaperMaterialFor(G->Plan) : (bFloater ? FloaterMID.Get() : FlierMaterialFor(G->Plan)));
	// A herd only in its own material (M_Fauna's walker branch, or the debug's): drawn as birds, they would fly apart. A
	// group of leapers likewise (its leaper branch), and a jelly (its floater branch).
	if ((bHerd || bLeaper || bFloater) && !Mat) { return; }
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(G->Kind);
	UPlanetInstancesComponent* C = AcquireSet();
	if (C->GetStaticMesh() != Mesh) { C->SetStaticMesh(Mesh); }
	C->SetCullPadding(D.CullPaddingUnits);
	if (Mat && C->GetMaterial(0) != Mat) { C->SetMaterial(0, Mat); }
	C->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	const double Now = ClockSeconds();
	TSharedPtr<SlonkFauna::FHerdState> Herd;
	SlonkFauna::FFaunaPose Pose;
	if (bHerd)
	{
		// Its clocks from the time, and stood on the drawn ground at once, so it is never drawn off it.
		Herd = MakeShared<SlonkFauna::FHerdState>();
		SlonkFauna::StartHerd(*Herd, *G, Now);
		const SlonkFauna::FPanic Panic = Panics.FindRef(G->Id);
		const FVector Ahead = SlonkFauna::HerdFrightOffset(*G, D, Panic, Params.RadiusCm, Now + 1.0);
		SlonkFauna::StepHerd(*Herd, *G, Params.RadiusCm, Now, 0.0, SlonkFauna::HerdFrightOffset(*G, D, Panic, Params.RadiusCm, Now), Panic.IsLive(Now), false, true,
			[this](const FVector& Dir) { return GroundHeightAt(Dir); }, TimeOffset, nullptr, &Ahead);
		C->SetRelativeLocationAndRotation(Herd->FramePos, Herd->FrameRot);
	}
	else if (bLeaper)
	{
		// With its shoal where the material paints it now (the world's own seconds).
		Pose = SlonkFauna::PathAt(*G, Params.RadiusCm, WorldSeconds());
		C->SetRelativeLocationAndRotation(Pose.PosCm, Pose.Rot);
	}
	else
	{
		// Where it flies now, facing its flight at once if it is still shaken (it had no frame before).
		Pose = SlonkFauna::FlightAt(*G, D, Panics.FindRef(G->Id), Params.RadiusCm, Now, SlonkFauna::FFaunaPose(), SlonkFauna::PanicSec);
		C->SetRelativeLocationAndRotation(Pose.PosCm, Pose.Rot);
	}
	TArray<FTransform> Xf;
	TArray<float> Custom;
	SlonkFauna::MembersOf(*G, Xf, Custom);
	// (A jelly is big enough to see as it is.)
	if (DebugDrawn >= 2 && !bFloater) { for (FTransform& T : Xf) { T.SetScale3D(T.GetScale3D() * 20.0); } }
	C->SetPieces(Xf, Custom);
	if (C->CastShadow) { C->SetCastShadow(false); }
	C->SetVisibility(true);
	FGroupSet Entry;
	Entry.Group = G;
	Entry.Set = C;
	Entry.Pose = Pose;
	Entry.PoseAt = Now;
	Entry.Herd = Herd;
	if (bHerd) { Entry.Water = MakeShared<SlonkFauna::FHerdWater>(Params); }
	if (bLeaper)
	{
		// Its fish as the set was given them, for their leaps' schedule and their splashes (their homes before the debug's
		// growth, which only shows where they are).
		Entry.Leaper = MakeShared<FLeaperState>();
		Entry.Leaper->Custom = Custom;
		for (const FTransform& T : Xf) { Entry.Leaper->Home.Add(T.GetLocation()); }
		Entry.PoseAt = WorldSeconds();
	}
	Sets.Add(G->Id, MoveTemp(Entry));
}

void UPlanetFaunaComponent::ReleaseSet(uint64 Id)
{
	FGroupSet S;
	if (!Sets.RemoveAndCopyValue(Id, S) || !S.Set) { return; }
	ReleaseDust(Id);
	// Back to the pool: empty, hidden and attached to nothing, so the planet's turn no longer moves it.
	S.Set->ClearPieces();
	S.Set->SetVisibility(false);
	S.Set->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
	FreeSets.Add(S.Set);
}

void UPlanetFaunaComponent::ReleaseAllSets()
{
	TArray<uint64> Ids;
	Sets.GetKeys(Ids);
	for (const uint64 Id : Ids) { ReleaseSet(Id); }
}

void UPlanetFaunaComponent::Suspend(bool bKeepPlans)
{
	if (Sets.Num() > 0 || Field.GetActive().Num() > 0)
	{
		ReleaseAllSets();
		// The cells read are kept: back in view, the same groups come back at once.
		Field.Clear(false);
	}
	// A world no longer drawn (its terrain put away, beyond the horizon, or seen from too high for any of its animals) lets
	// its species' meshes and its plans' material instances go, once: so a strange world left behind, or each one a
	// spawnplanet run leaves about, keeps none of them. Each is made again as a group of it comes, once it draws again
	// (HoldOwnPlans).
	if (bKeepPlans || !bHoldingPlans) { return; }
	bHoldingPlans = false;
	SlonkFaunaMeshes::HoldPlans(this, {});
	HerdPlanMIDs.Reset();
	FlierPlanMIDs.Reset();
	LeaperPlanMIDs.Reset();
	// Its pool's sets, empty and hidden, drew them last: they draw nothing until they are taken again, and so keep nothing.
	for (UPlanetInstancesComponent* C : FreeSets)
	{
		if (!C) { continue; }
		C->SetStaticMesh(nullptr);
		C->EmptyOverrideMaterials();
	}
}

void UPlanetFaunaComponent::GatherThreats(const APlayerController* PC, TArray<SlonkFauna::FFaunaThreat>& Out) const
{
	Out.Reset();
	const FTransform& Frame = GetComponentTransform();
	const AActor* World = GetOwner();
	auto AddCraft = [&](const AActor* A)
	{
		const UStarshipMovementComponent* M = A ? A->FindComponentByClass<UStarshipMovementComponent>() : nullptr;
		if (!M || M->GroundBody.Get() != World || M->AltitudeAGLCm < 0.0) { return; }
		SlonkFauna::FFaunaThreat T;
		T.PosCm = Frame.InverseTransformPositionNoScale(A->GetActorLocation());
		T.VelCmS = Frame.InverseTransformVectorNoScale(M->Velocity);
		T.AltitudeCm = M->AltitudeAGLCm;
		T.bCraft = true;
		// A craft set down scares nothing (Scares): a flock comes back to the ground by a ship parked there.
		T.bLanded = M->bLanded;
		Out.Add(T);
	};
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (const APersonPawn* Person = Cast<APersonPawn>(Pawn))
	{
		// Someone on this world's ground, and the craft they came from if it is in the air over it.
		if (Person->GroundPlanet.Get() == World)
		{
			SlonkFauna::FFaunaThreat T;
			T.PosCm = Frame.InverseTransformPositionNoScale(Person->GetActorLocation());
			T.VelCmS = Frame.InverseTransformVectorNoScale(Person->Velocity);
			T.AltitudeCm = Person->AltitudeCm;
			T.bCraft = false;
			Out.Add(T);
		}
		AddCraft(Person->Craft.Get());
	}
	else { AddCraft(Pawn); }
}

void UPlanetFaunaComponent::UpdateView(const APlayerController* PC, const FVector& ViewLocationWorld, const FVector& SunDirectionWorld)
{
	if (!bConfigured || !Terrain) { return; }
	// The world as the settings have it now was taken by the planet's tick (RefreshWorld) before its chunks were chosen.
	// slonk.Fauna.Debug changed: every group goes, and comes back drawn as it now asks (its species' meshes kept, so none
	// is made again).
	const int32 Debug = CVarFaunaDebug.GetValueOnGameThread();
	if (Debug != DebugDrawn)
	{
		DebugDrawn = Debug;
		Suspend(true);
	}
	const SlonkFauna::FFaunaWorld& W = Field.GetWorld();
	if (!SlonkFauna::IsEnabled() || !W.SpawnsAny())
	{
		Suspend();
		return;
	}
	// A finished scan lands first (one launched for another world is dropped).
	if (bScanning && ScanTask.IsCompleted())
	{
		bScanning = false;
		const FScanRef S = ScanTask.GetResult();
		ScanTask = UE::Tasks::TTask<FScanRef>();
		if (S.IsValid() && ScanGeneration == Generation)
		{
			Field.Accept(*S);
			LastScanMs = S->Ms;
			LastScanEvaluated = S->Evaluated;
			LastScanCells = S->CellIds.Num();
		}
	}
	const FTransform& Frame = GetComponentTransform();
	const FVector CamLocal = Frame.InverseTransformPositionNoScale(ViewLocationWorld);
	LastCamLocal = CamLocal;
	const FVector CamDir = CamLocal.GetSafeNormal();
	// A camera higher over the ground under it (the drawn ground, or the field's where none is drawn; the sea's surface
	// over water) than any kind is kept under sees none: no scan for a world seen from space or from high over it. A
	// kilometre more, for the ground under the last scan's middle, a sixth of a cell off, standing higher. Over the
	// highest ground there could be, the ground is not read.
	double MaxAltitude = 0.0;
	for (int32 K = 0; K < SlonkFauna::NumKinds; ++K)
	{
		if (W.Spawns((EFaunaKind)K)) { MaxAltitude = FMath::Max(MaxAltitude, SlonkFauna::KindDesc((EFaunaKind)K).MaxAltitudeCm * SlonkFauna::SpawnHysteresis); }
	}
	const double Ceiling = MaxAltitude + 1.0e5;
	const double OverSea = CamLocal.Size() - Params.RadiusCm;
	if (OverSea > Ceiling + FMath::Max(Params.ReliefCm, 0.0))
	{
		Suspend();
		return;
	}
	const double CamAltitude = OverSea - FMath::Max(Terrain->SurfaceHeightAtWorld(ViewLocationWorld), 0.0);
	if (CamAltitude > Ceiling)
	{
		Suspend();
		return;
	}
	if (!bScanning && Field.WantsScan(CamDir, CamAltitude))
	{
		// The cells round the camera's ground point, read on a task from what the last scan read: of the kinds that can be
		// kept at the camera's height (high over a strange world, the jellies alone).
		ScanGeneration = Generation;
		bScanning = true;
		const uint32 Kinds = Field.ScanKinds(CamAltitude);
		ScanTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Wc = SlonkFauna::FFaunaWorld(W), CamDir, Kinds, Radius = Field.ScanRadiusCm(Kinds), Cache = Field.GetCache()]()
		{
			return FScanRef(SlonkFauna::FFaunaField::Scan(Wc, CamDir, Radius, Cache, Kinds));
		});
	}
	TArray<SlonkFauna::FFaunaField::FGroupRef> Spawned;
	TArray<uint64> LetGo;
	Field.Select(CamLocal, Spawned, LetGo);
	for (const uint64 Id : LetGo) { ReleaseSet(Id); }
	for (const SlonkFauna::FFaunaField::FGroupRef& G : Spawned) { SpawnSet(G); }

	// The frights that have run their course are forgotten, the group about or not.
	const double Now = ClockSeconds();
	// The frame's step on the paths' clock (none after a pause or a jump of the clock).
	const double Dt = LastUpdateSec >= 0.0 ? FMath::Clamp(Now - LastUpdateSec, 0.0, 0.25) : 0.0;
	LastUpdateSec = Now;
	for (auto It = Panics.CreateIterator(); It; ++It)
	{
		if (!It.Value().IsLive(Now)) { It.RemoveCurrent(); }
	}
	// The leapers keep to the world's own seconds, as the terrain material swims its shoals: their dives over by then are
	// forgotten too, and their splashes are of the crossings since the last frame (none after a pause or a jump).
	const double WorldNow = WorldSeconds();
	const double WorldFrom = LastLeaperWorldSec >= 0.0 && WorldNow > LastLeaperWorldSec && WorldNow - LastLeaperWorldSec < 1.0 ? LastLeaperWorldSec : WorldNow;
	LastLeaperWorldSec = WorldNow;
	for (auto It = LeaperDives.CreateIterator(); It; ++It)
	{
		if (!It.Value().IsLive(WorldNow)) { It.RemoveCurrent(); }
	}
	SplashPool.Tick(WorldNow);
	TArray<SlonkFauna::FFaunaThreat> Threats;
	GatherThreats(PC, Threats);
	const FVector Sun = SunDirectionWorld.GetSafeNormal();
	for (TPair<uint64, FGroupSet>& Pair : Sets)
	{
		FGroupSet& S = Pair.Value;
		// A herd walks on the drawn ground (UpdateHerd); a group of leapers leaps from its shoal (UpdateLeaper); a jelly drifts
		// high over the ground (UpdateFloater).
		if (S.Herd.IsValid()) { UpdateHerd(Pair.Key, S, Threats, CamLocal, Sun, Now, Dt); continue; }
		if (S.Leaper.IsValid()) { UpdateLeaper(Pair.Key, S, Threats, CamLocal, Sun, WorldNow, WorldFrom); continue; }
		if (S.Group->Kind == EFaunaKind::Floater) { UpdateFloater(Pair.Key, S, Threats, CamLocal, Sun, Now); continue; }
		const SlonkFauna::FFaunaGroup& G = *S.Group;
		const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(G.Kind);
		// Its frame as it flies, on its path or shaken off it, turning from the frame it had.
		SlonkFauna::FPanic Panic = Panics.FindRef(Pair.Key);
		const double SinceDrawn = FMath::Max(Now - S.PoseAt, 0.0);
		SlonkFauna::FFaunaPose Pose = SlonkFauna::FlightAt(G, D, Panic, Params.RadiusCm, Now, S.Pose, SinceDrawn);
		for (const SlonkFauna::FFaunaThreat& T : Threats)
		{
			if (SlonkFauna::StartleGroup(Panic, D, Pose.PosCm, T, Now))
			{
				Panics.Add(Pair.Key, Panic);
				Pose = SlonkFauna::FlightAt(G, D, Panic, Params.RadiusCm, Now, S.Pose, SinceDrawn);
				++Startles;
				break;
			}
		}
		S.Pose = Pose;
		S.PoseAt = Now;
		const FVector& Pos = Pose.PosCm;
		// The whole group moves as its frame, facing the way it flies: the velocity pass sees its flight in the transform.
		S.Set->SetRelativeLocationAndRotation(Pos, Pose.Rot);
		// What the material reads of the group: the night where it is; its fright, each moment of it on the material's
		// clock (the world's time, wrapped to its hour): when its members' scatter opened and the way it opened in its own
		// frame, whether it is shaken, when it was last startled and when its wingbeat's urge began to come in; the bank of
		// its turn; its spread.
		const double SunUp = FVector::DotProduct(Frame.TransformVectorNoScale(Pos.GetSafeNormal()), Sun);
		const float Night = (float)(1.0 - SlonkScatter::SmoothStep(-0.12, 0.08, SunUp));
		float Gd[SlonkFauna::GroupData::Num] = {};
		Gd[SlonkFauna::GroupData::Night] = FMath::RoundToFloat(Night * 100.f) / 100.f;
		// The bank to a two-hundredth of a radian, so a steady turn sends nothing new each frame.
		Gd[SlonkFauna::GroupData::Bank] = FMath::RoundToFloat((float)Pose.Bank * 200.f) / 200.f;
		Gd[SlonkFauna::GroupData::SpreadM] = G.SpacingM;
		// A four-winged flock's own number for its darts and its span, which every member of it darts together on.
		Gd[SlonkFauna::GroupData::DartSeed] = SlonkFauna::DartSeedOf(G);
		Gd[SlonkFauna::GroupData::DartSpanM] = Gd[SlonkFauna::GroupData::DartSeed] > 0.f ? G.SpanM : 0.f;
		SlonkFauna::FrightData(Panic, Now, Pose.Rot, TimeOffset, Gd);
		// Slot by slot: 0 to 3, 4 to 7, and 8 to 11.
		static_assert(SlonkFauna::GroupData::Num == 12, "a group's custom primitive data is sent as slots 0 to 3, 4 to 7, and 8 to 11");
		S.Set->SetCustomPrimitiveDataVector4(0, FVector4(Gd[0], Gd[1], Gd[2], Gd[3]));
		S.Set->SetCustomPrimitiveDataVector4(4, FVector4(Gd[4], Gd[5], Gd[6], Gd[7]));
		S.Set->SetCustomPrimitiveDataVector4(8, FVector4(Gd[8], Gd[9], Gd[10], Gd[11]));
		// Shadows within the kind's reach for them, let go a fifth further out.
		if (D.ShadowReachCm > 0.0)
		{
			const double Dist = FVector::Dist(Pos, CamLocal);
			const bool bShadow = S.bShadow ? Dist < D.ShadowReachCm * 1.2 : Dist < D.ShadowReachCm;
			if (bShadow != S.bShadow) { S.bShadow = bShadow; S.Set->SetCastShadow(bShadow); }
		}
	}
	if (Sets.Num() > 0)
	{
		// The material's view of the camera, the same in the shadow passes as in the view: where it stands, for the fade
		// and the speck floor, and how many pixels a radian is to it.
		// The world's glow, which the alien plans' glow organs light in, given again as the look changes (fauna strange).
		const bool bGlow = PlanGlowSet != Profile.Look.Glow;
		PlanGlowSet = Profile.Look.Glow;
		for (TMap<uint32, TObjectPtr<UMaterialInstanceDynamic>>* Plans : { &HerdPlanMIDs, &FlierPlanMIDs, &LeaperPlanMIDs })
		{
			for (const TPair<uint32, TObjectPtr<UMaterialInstanceDynamic>>& P : *Plans) { if (P.Value && bGlow) { P.Value->SetVectorParameterValue(TEXT("GlowColor"), Profile.Look.Glow); } }
		}
		if (MID)
		{
			// The fliers' own instance and each of their plans' alike.
			const float PxPerRad = SlonkFauna::PixelsPerRadian(PC);
			const FLinearColor View((float)ViewLocationWorld.X, (float)ViewLocationWorld.Y, (float)ViewLocationWorld.Z, 0.f);
			const bool bPx = !FMath::IsNearlyEqual(PxPerRad, LastPxPerRad, 0.5f);
			if (bPx) { LastPxPerRad = PxPerRad; }
			MID->SetVectorParameterValue(TEXT("ViewPos"), View);
			if (bPx) { MID->SetScalarParameterValue(TEXT("PxPerRad"), PxPerRad); }
			for (const TPair<uint32, TObjectPtr<UMaterialInstanceDynamic>>& P : FlierPlanMIDs)
			{
				if (!P.Value) { continue; }
				P.Value->SetVectorParameterValue(TEXT("ViewPos"), View);
				if (bPx) { P.Value->SetScalarParameterValue(TEXT("PxPerRad"), PxPerRad); }
			}
		}
		if (HerdMID)
		{
			// The herds' own instance and each of their plans' alike.
			const float PxPerRad = SlonkFauna::PixelsPerRadian(PC);
			const bool bPx = !FMath::IsNearlyEqual(PxPerRad, LastHerdPxPerRad, 0.5f);
			if (bPx) { LastHerdPxPerRad = PxPerRad; }
			const FLinearColor View((float)ViewLocationWorld.X, (float)ViewLocationWorld.Y, (float)ViewLocationWorld.Z, 0.f);
			HerdMID->SetVectorParameterValue(TEXT("ViewPos"), View);
			if (bPx) { HerdMID->SetScalarParameterValue(TEXT("PxPerRad"), PxPerRad); }
			for (const TPair<uint32, TObjectPtr<UMaterialInstanceDynamic>>& P : HerdPlanMIDs)
			{
				if (!P.Value) { continue; }
				P.Value->SetVectorParameterValue(TEXT("ViewPos"), View);
				if (bPx) { P.Value->SetScalarParameterValue(TEXT("PxPerRad"), PxPerRad); }
			}
		}
		if (LeaperMID)
		{
			// The leapers' own instance and each of their plans' alike.
			const float PxPerRad = SlonkFauna::PixelsPerRadian(PC);
			const FLinearColor View((float)ViewLocationWorld.X, (float)ViewLocationWorld.Y, (float)ViewLocationWorld.Z, 0.f);
			const bool bPx = !FMath::IsNearlyEqual(PxPerRad, LastLeaperPxPerRad, 0.5f);
			if (bPx) { LastLeaperPxPerRad = PxPerRad; }
			LeaperMID->SetVectorParameterValue(TEXT("ViewPos"), View);
			if (bPx) { LeaperMID->SetScalarParameterValue(TEXT("PxPerRad"), PxPerRad); }
			for (const TPair<uint32, TObjectPtr<UMaterialInstanceDynamic>>& P : LeaperPlanMIDs)
			{
				if (!P.Value) { continue; }
				P.Value->SetVectorParameterValue(TEXT("ViewPos"), View);
				if (bPx) { P.Value->SetScalarParameterValue(TEXT("PxPerRad"), PxPerRad); }
			}
		}
		if (FloaterMID)
		{
			const float PxPerRad = SlonkFauna::PixelsPerRadian(PC);
			FloaterMID->SetVectorParameterValue(TEXT("ViewPos"), FLinearColor((float)ViewLocationWorld.X, (float)ViewLocationWorld.Y, (float)ViewLocationWorld.Z, 0.f));
			if (!FMath::IsNearlyEqual(PxPerRad, LastFloaterPxPerRad, 0.5f)) { LastFloaterPxPerRad = PxPerRad; FloaterMID->SetScalarParameterValue(TEXT("PxPerRad"), PxPerRad); }
		}
		// Every group's members are written with its frame of this frame, whether or not the rocks and plants are.
		Terrain->QueuePlanetInstanceWrites();
	}
}

void UPlanetFaunaComponent::UpdateHerd(uint64 Id, FGroupSet& S, const TArray<SlonkFauna::FFaunaThreat>& Threats, const FVector& CamLocal, const FVector& Sun, double Now, double Dt)
{
	const SlonkFauna::FFaunaGroup& G = *S.Group;
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(G.Kind);
	SlonkFauna::FHerdState& H = *S.Herd;
	// Its first frame about stands where its spawn stood it, the same moment.
	const double StepDt = S.Frames == 0 ? 0.0 : Dt;
	++S.Frames;
	// Startled where it stands: by a craft passing low over it, by someone run in close to its nearest member. On the
	// move at a walk or a trot it stops and turns to flee where it stands before it goes, a way that keeps it dry
	// (SlonkFauna::StartleHerd).
	SlonkFauna::FPanic Panic = Panics.FindRef(Id);
	TArray<FVector> Crew;
	for (const SlonkFauna::FFaunaThreat& T : Threats)
	{
		if (!T.bCraft) { Crew.Add(T.PosCm); }
		if (SlonkFauna::StartleHerd(Panic, H, G, D, Params.RadiusCm, T, Now, false, S.Water.Get()))
		{
			Panics.Add(Id, Panic);
			++Startles;
		}
	}
	// Out of the way of anyone walking toward it, never into the water.
	const bool bShaken = Panic.IsLive(Now);
	// Its crew offset held while it is frightened: its flight was read dry with the offset it had when startled
	// (SlonkFauna::StartleHerd), and anyone closing on it frightens it again rather than pushing it.
	const bool bMoving = !bShaken && SlonkFauna::KeepFromCrew(H, G, D, Crew, StepDt, S.Water.Get());
	// The ground under it read again each frame for a herd near the camera or fleeing, every sixth frame further off
	// (each herd on frames of its own); its frame set on the ground at its place each frame, turned toward the way its
	// fright and the crew are taking it (where they will have it a second on: a fright's flight, a walk away from the
	// crew or back to its path, which its walkers turn to before it goes).
	const bool bNear = FVector::Dist(H.FramePos, CamLocal) < 4.0e4;
	const bool bRefresh = bNear || bShaken || bMoving || ((S.Frames + (uint32)(Id % 6)) % 6) == 0;
	const FVector Ahead = SlonkFauna::HerdFrightOffset(G, D, Panic, Params.RadiusCm, Now + 1.0) + H.Avoid + (bMoving ? H.AvoidWay : FVector::ZeroVector);
	float Gd[SlonkFauna::HerdData::Num] = {};
	SlonkFauna::StepHerd(H, G, Params.RadiusCm, Now, StepDt, SlonkFauna::HerdFrightOffset(G, D, Panic, Params.RadiusCm, Now) + H.Avoid, bShaken, bMoving, bRefresh,
		[this](const FVector& Dir) { return GroundHeightAt(Dir); }, TimeOffset, Gd, &Ahead);
	// The whole herd moves as its frame: the velocity pass sees its walk in the transform, its walkers' own in the
	// material's clocks.
	S.Set->SetRelativeLocationAndRotation(H.FramePos, H.FrameRot);
	const double SunUp = FVector::DotProduct(GetComponentTransform().TransformVectorNoScale(H.FramePos.GetSafeNormal()), Sun);
	Gd[SlonkFauna::HerdData::Night] = FMath::RoundToFloat((float)(1.0 - SlonkScatter::SmoothStep(-0.12, 0.08, SunUp)) * 100.f) / 100.f;
	SlonkFauna::HerdFrightData(Panic, Now, TimeOffset, Gd);
	// Every slot at once (one update of the primitive's data a frame).
	S.Set->SetCustomPrimitiveDataFloatArray(0, TConstArrayView<float>(Gd, SlonkFauna::HerdData::Num));
	// Shadows within the kind's reach for them, let go a fifth further out.
	if (D.ShadowReachCm > 0.0)
	{
		const double Dist = FVector::Dist(H.FramePos, CamLocal);
		const bool bShadow = S.bShadow ? Dist < D.ShadowReachCm * 1.2 : Dist < D.ShadowReachCm;
		if (bShadow != S.bShadow) { S.bShadow = bShadow; S.Set->SetCastShadow(bShadow); }
	}
	UpdateDust(Id, S, CamLocal, Gd[SlonkFauna::HerdData::Night], StepDt);
}

double UPlanetFaunaComponent::WorldSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

bool UPlanetFaunaComponent::EnsureDustSystem()
{
	if (DustSystem) { return true; }
	if (bDustMissing) { return false; }
	// As the transporter takes its shimmer (ATransporterEffect): the asset if it is there, else none, said once.
	if (FPackageName::DoesPackageExist(SlonkFauna::HerdDustPackage)) { DustSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(SlonkFauna::HerdDustPath)).LoadSynchronous(); }
	if (!DustSystem)
	{
		bDustMissing = true;
		UE_LOG(LogTemp, Log, TEXT("Fauna: %s not usable, the herds raise no dust."), SlonkFauna::HerdDustPath);
	}
	return DustSystem != nullptr;
}

UNiagaraComponent* UPlanetFaunaComponent::MakeDustComponent()
{
	if (!DustSystem) { return nullptr; }
	UNiagaraComponent* C = NewObject<UNiagaraComponent>(GetOwner() ? (UObject*)GetOwner() : (UObject*)this, NAME_None, RF_Transient);
	C->SetAsset(DustSystem);
	C->SetAutoActivate(false);
	C->SetAutoDestroy(false);
	C->SetCastShadow(false);
	C->RegisterComponent();
	DustComponents.Add(C);
	return C;
}

void UPlanetFaunaComponent::ReleaseDust(uint64 Id)
{
	// Its puffs left to fade where they are (they live in the world, not on the herd); the component rests in the pool.
	if (UNiagaraComponent* C = DustPool.Release(Id, WorldSeconds()))
	{
		C->SetVariableFloat(TEXT("Rate"), 0.f);
		C->Deactivate();
		C->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	}
}

void UPlanetFaunaComponent::UpdateDust(uint64 Id, FGroupSet& S, const FVector& CamLocal, float Night, double Dt)
{
	const SlonkFauna::FHerdState& H = *S.Herd;
	const SlonkFauna::FFaunaGroup& G = *S.Group;
	UNiagaraComponent* Have = DustPool.Find(Id);
	// Whether it goes fast enough, its speed smoothed (a single fast frame, a hitch or its ground shifting under it, is no
	// trot) and switched with some slack and a least time on (SlonkFauna::StepDustGate).
	const bool bFast = SlonkFauna::StepDustGate(S.DustGate, H.SpeedMS, Dt, WorldSeconds());
	// The cheap reasons first: the dust off, too far off, too slow, too dark, a dead world, no system.
	const double Dist = FVector::Dist(H.FramePos, CamLocal);
	const bool bCould = SlonkFauna::DustEnabled() && Dist < SlonkFauna::DustReachCm * (Have ? 1.1 : 1.0) && bFast && Night < 1.f
		&& Life > 0.f && EnsureDustSystem();
	SlonkFauna::FHerdDust Dust;
	if (bCould)
	{
		// How wet the ground under it is, read again each second it raises dust.
		const double Now = WorldSeconds();
		if (Now - S.DustWetAt > 1.0) { S.DustWet = SlonkScatter::HabitatAt(Params, H.FramePos.GetSafeNormal()).Wet; S.DustWetAt = Now; }
		SlonkFauna::FHerdDustInput In;
		In.SpeedMS = S.DustGate.SpeedMS;
		In.VelWorldCmS = GetComponentTransform().TransformVectorNoScale(H.VelCmS);
		// The ground's own way through the world as the planet turns, which every puff is born with and comes to rest in: as
		// far over this frame's tick of the dust's emitter as the ground turned in it.
		In.CarryWorldCmS = GroundCarryAt(GetComponentTransform().TransformPositionNoScale(H.FramePos), true);
		In.Wet = S.DustWet;
		In.Life = Life;
		In.Night = Night;
		In.DistCm = FMath::Min(Dist, SlonkFauna::DustReachCm);
		In.Members = G.Members;
		In.SpanM = G.SpanM;
		In.RadiusCm = G.HerdRadiusCm;
		In.Color = SlonkFauna::HerdDustColor(Palette, S.DustWet, Night);
		Dust = SlonkFauna::HerdDustFor(In);
	}
	// Kept while the gate holds it on, at whatever rate its smoothed speed gives (none for a moment does not let it go).
	if (!bCould || (Dust.Rate <= 0.f && !Have))
	{
		if (Have) { ReleaseDust(Id); }
		return;
	}
	UNiagaraComponent* C = Have ? Have : DustPool.Acquire(Id, WorldSeconds(), [this]() { return MakeDustComponent(); });
	if (!C) { return; }
	C->SetVariableFloat(TEXT("Rate"), Dust.Rate);
	C->SetVariableFloat(TEXT("Radius"), Dust.Radius);
	C->SetVariableLinearColor(TEXT("Color"), Dust.Color);
	C->SetVariableVec3(TEXT("Drift"), Dust.Drift);
	C->SetVariableVec3(TEXT("Carry"), Dust.Carry);
	C->SetVariableFloat(TEXT("Size"), Dust.Size);
	if (!Have)
	{
		// Carried with the herd's set at its middle; its puffs stay where they rise.
		C->AttachToComponent(S.Set, FAttachmentTransformRules::KeepRelativeTransform);
		C->SetRelativeLocationAndRotation(FVector::ZeroVector, FQuat::Identity);
		C->Activate(true);
	}
}

FVector UPlanetFaunaComponent::GroundCarryAt(const FVector& WorldPos, bool bOverFrame) const
{
	const APlanetBody* Planet = Cast<APlanetBody>(GetOwner());
	if (!Planet) { return FVector::ZeroVector; }
	const UNiagaraSettings* NS = GetDefault<UNiagaraSettings>();
	const UWorld* World = GetWorld();
	return SlonkFauna::GroundCarryCmS(Planet->GetActorUpVector(), Planet->GetActorLocation(), Planet->SpinDegPerSec, UGameClockSubsystem::ScaleOf(this), Planet->IsSpinHeld(), WorldPos,
		bOverFrame && World ? World->GetDeltaSeconds() : 0.0, NS && NS->bLimitDeltaTime ? NS->MaxDeltaTimePerTick : 0.0);
}

void UPlanetFaunaComponent::UpdateLeaper(uint64 Id, FGroupSet& S, const TArray<SlonkFauna::FFaunaThreat>& Threats, const FVector& CamLocal, const FVector& Sun, double WorldNow, double WorldFrom)
{
	using namespace SlonkFauna;
	const FFaunaGroup& G = *S.Group;
	const FFaunaKindDesc& D = KindDesc(G.Kind);
	FLeaperState& L = *S.Leaper;
	const double R = Params.RadiusCm;
	// The whole group moves as its frame, with its shoal: the velocity pass sees the shoal's swim in the transform, each
	// fish's leap in the material's clock.
	const FFaunaPose Pose = PathAt(G, R, WorldNow);
	S.Set->SetRelativeLocationAndRotation(Pose.PosCm, Pose.Rot);
	S.Pose = Pose;
	S.PoseAt = WorldNow;
	// Sent diving by a craft passing low over it (someone on a shore is nothing to it).
	FPanic Dive = LeaperDives.FindRef(Id);
	for (const FFaunaThreat& T : Threats)
	{
		if (StartleLeapers(Dive, D, Pose.PosCm, T, WorldNow))
		{
			LeaperDives.Add(Id, Dive);
			++Startles;
			break;
		}
	}
	// What the material reads of the group: the night where it is, its dive, and the sea's surface where and when each
	// fish's current or next leap leaves the water and dives back in (read once a leap, at the swell's full strength, and
	// faded each frame as the material fades the swell at the camera's distance).
	float Gd[LeaperData::Num] = {};
	const double SunUp = FVector::DotProduct(GetComponentTransform().TransformVectorNoScale(Pose.PosCm.GetSafeNormal()), Sun);
	const float Night = LeaperNight(SunUp);
	Gd[LeaperData::Night] = Night;
	const double Dist = FVector::Dist(Pose.PosCm, CamLocal);
	const double Fade = SwellFade(Dist);
	LeaperGroupData(G, L.Custom, L.Home, R, Sea.SwellCm, Fade, WorldNow, Dive, L.Surface, Gd);
	const int32 N = FMath::Min(L.Home.Num(), Leaper::MaxPerGroup);
	// Every slot at once (one update of the primitive's data a frame).
	S.Set->SetCustomPrimitiveDataFloatArray(0, TConstArrayView<float>(Gd, LeaperData::Num));
	// The splashes of the leaps that left the water or dove back in since the last frame, near the camera (a crossing too
	// long ago, over a hitch, is let go), of those the dive did not keep in the water; none by night, where the sun that
	// lights them is too far under the horizon to.
	if (!SplashEnabled() || !SplashLitAt(Night) || WorldFrom >= WorldNow || Dist > SplashReachCm + G.ExtentCm()) { return; }
	TArray<FLeapEvent> Events;
	for (int32 i = 0; i < N; ++i) { LeapEventsBetween(L.Custom.GetData() + i * Custom::Num, i, FMath::Max(WorldFrom, WorldNow - SplashLateSec), WorldNow, Events); }
	for (const FLeapEvent& E : Events)
	{
		if (LeapDived(Dive, E.Leap.T0)) { continue; }
		const FVector Place = LeapCrossing(G, R, Sea.SwellCm, Fade, L.Home[E.Member], E.Leap, E.bEntry);
		if (FVector::Dist(Place, CamLocal) > SplashReachCm) { continue; }
		// As big as its fish (a slim eel's smaller, a spinefin's, its fins spread, bigger).
		ThrowSplash(Place, 100.0 * L.Custom[E.Member * Custom::Num + Custom::SizeM] * SplashScaleOf(G.Plan), E.bEntry, Night);
	}
}

bool UPlanetFaunaComponent::EnsureSplashSystem()
{
	if (SplashSystem) { return true; }
	if (bSplashMissing) { return false; }
	// As the herds take their dust: the asset if it is there, else none, said once.
	if (FPackageName::DoesPackageExist(SlonkFauna::FishSplashPackage)) { SplashSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(SlonkFauna::FishSplashPath)).LoadSynchronous(); }
	if (!SplashSystem)
	{
		bSplashMissing = true;
		UE_LOG(LogTemp, Log, TEXT("Fauna: %s not usable, the leaping fish throw no splashes."), SlonkFauna::FishSplashPath);
	}
	return SplashSystem != nullptr;
}

UNiagaraComponent* UPlanetFaunaComponent::MakeSplashComponent()
{
	if (!SplashSystem) { return nullptr; }
	UNiagaraComponent* C = NewObject<UNiagaraComponent>(GetOwner() ? (UObject*)GetOwner() : (UObject*)this, NAME_None, RF_Transient);
	C->SetAsset(SplashSystem);
	C->SetAutoActivate(false);
	C->SetAutoDestroy(false);
	C->SetCastShadow(false);
	// In the planet's frame, so the burst comes where the crossing is however the world turns before its first tick.
	C->SetupAttachment(this);
	C->RegisterComponent();
	SplashComponents.Add(C);
	return C;
}

void UPlanetFaunaComponent::ThrowSplash(const FVector& PlaceLocal, double SizeCm, bool bEntry, float Night)
{
	if (!EnsureSplashSystem()) { return; }
	UNiagaraComponent* C = SplashPool.Acquire(WorldSeconds(), [this]() { return MakeSplashComponent(); });
	if (!C) { return; }
	// At the crossing on the sea's surface, its z the sea's up there; its droplets born moving with the ground as the
	// planet turns (User.Carry).
	if (C->GetAttachParent() != this) { C->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform); }
	C->SetRelativeLocationAndRotation(PlaceLocal, FRotationMatrix::MakeFromZ(PlaceLocal.GetSafeNormal()).ToQuat());
	SlonkFauna::FFishSplashInput In;
	In.SizeCm = SizeCm;
	In.bEntry = bEntry;
	In.Night = Night;
	// Set once for the whole burst, the ground's own velocity as it is (not scaled to this frame's tick, as the dust's,
	// set again every frame, is).
	In.CarryWorldCmS = GroundCarryAt(GetComponentTransform().TransformPositionNoScale(PlaceLocal), false);
	In.Shallow = Sea.Shallow;
	In.Ocean = Sea.Ocean;
	const SlonkFauna::FFishSplash Sp = SlonkFauna::FishSplashFor(In);
	C->SetVariableFloat(TEXT("Size"), Sp.Size);
	C->SetVariableLinearColor(TEXT("Color"), Sp.Color);
	C->SetVariableVec3(TEXT("Carry"), Sp.Carry);
	C->SetVariableFloat(TEXT("Entry"), Sp.Entry);
	C->SetVariableFloat(TEXT("Night"), Sp.Night);
	C->Activate(true);
}

int32 UPlanetFaunaComponent::GetLeapingCount() const
{
	const double Now = WorldSeconds();
	int32 N = 0;
	for (const TPair<uint64, FGroupSet>& Pair : Sets)
	{
		if (!Pair.Value.Leaper.IsValid()) { continue; }
		const SlonkFauna::FPanic Dive = LeaperDives.FindRef(Pair.Key);
		const FLeaperState& L = *Pair.Value.Leaper;
		for (int32 i = 0; i < L.Home.Num(); ++i)
		{
			const SlonkFauna::FLeap Lp = SlonkFauna::LeapAt(L.Custom.GetData() + i * SlonkFauna::Custom::Num, Now);
			N += Lp.DrawnAt(Now) && !SlonkFauna::LeapDived(Dive, Lp.T0) ? 1 : 0;
		}
	}
	return N;
}

FVector UPlanetFaunaComponent::HerdPlaceNow(const SlonkFauna::FFaunaGroup& G) const
{
	if (const FGroupSet* S = Sets.Find(G.Id))
	{
		if (S->Herd.IsValid() && S->Herd->bStarted) { return S->Herd->FramePos; }
	}
	const double Now = ClockSeconds();
	const SlonkFauna::FPanic Panic = Panics.FindRef(G.Id);
	return SlonkFauna::PathAt(G, Params.RadiusCm, Now).PosCm + SlonkFauna::HerdFrightOffset(G, SlonkFauna::KindDesc(G.Kind), Panic, Params.RadiusCm, Now);
}

FVector UPlanetFaunaComponent::FloaterPlaceNow(const SlonkFauna::FFaunaGroup& G) const
{
	const double Now = ClockSeconds();
	return SlonkFauna::PathAt(G, Params.RadiusCm, Now).PosCm + SlonkFauna::PanicOffset(Panics.FindRef(G.Id), SlonkFauna::KindDesc(G.Kind), Now);
}

const SlonkFauna::FFaunaGroup* UPlanetFaunaComponent::GetDrawnGroup(uint64 Id) const
{
	const FGroupSet* S = Sets.Find(Id);
	return S && S->Group.IsValid() ? S->Group.Get() : nullptr;
}

void UPlanetFaunaComponent::UpdateFloater(uint64 Id, FGroupSet& S, const TArray<SlonkFauna::FFaunaThreat>& Threats, const FVector& CamLocal, const FVector& Sun, double Now)
{
	using namespace SlonkFauna;
	const FFaunaGroup& G = *S.Group;
	const FFaunaKindDesc& D = KindDesc(G.Kind);
	// Its frame where its loop and any fright have it, never turned.
	FPanic Panic = Panics.FindRef(Id);
	FFaunaPose Pose = FlightAt(G, D, Panic, Params.RadiusCm, Now, S.Pose, 0.0);
	// Startled by a craft passing close by its middle (someone on foot, far under it, is nothing to it): it squeezes itself
	// away and down, carrying on the drift a fright before left it.
	for (const FFaunaThreat& T : Threats)
	{
		if (StartleGroup(Panic, D, Pose.PosCm, T, Now))
		{
			Panics.Add(Id, Panic);
			Pose = FlightAt(G, D, Panic, Params.RadiusCm, Now, S.Pose, 0.0);
			++Startles;
			break;
		}
	}
	S.Pose = Pose;
	S.PoseAt = Now;
	// The whole jelly moves as its frame: the velocity pass sees its drift in the transform, its pulse and its tentacles'
	// waves and trail in the material's clock.
	S.Set->SetRelativeLocationAndRotation(Pose.PosCm, Pose.Rot);
	// What the material reads of it: the night where it is, its fright, and the way it drifts through the air in its frame
	// and how fast that changes, each as of now on the material's clock (every slot at once).
	float Gd[FloaterData::Num] = {};
	const double SunUp = FVector::DotProduct(GetComponentTransform().TransformVectorNoScale(Pose.PosCm.GetSafeNormal()), Sun);
	Gd[FloaterData::Night] = FMath::RoundToFloat((float)(1.0 - SlonkScatter::SmoothStep(-0.12, 0.08, SunUp)) * 100.f) / 100.f;
	FloaterGroupData(G, D, Panic, Params.RadiusCm, Now, Pose.Rot, TimeOffset, Gd);
	S.Set->SetCustomPrimitiveDataFloatArray(0, TConstArrayView<float>(Gd, FloaterData::Num));
	// Shadows while it, or its shadow, is within the kind's reach for them (a jelly overhead dapples the ground; a high
	// one's shadow falls kilometres off at a low sun), let go a fifth further out. Where its shadow lands hangs on the
	// ground it meets (a valley below its loop's ridge, a slope rising to it, the world's curve), so the camera is measured
	// to the nearest point of the line its shadow is cast along, from the jelly out to twice as far as flat ground at its
	// loop's height would stop it: crew standing in its shadow are on that line wherever the ground stops it.
	const FVector SunLocal = GetComponentTransform().InverseTransformVectorNoScale(Sun).GetSafeNormal();
	const FVector Fall = FloaterShadowFall(Pose.PosCm, Params.RadiusCm, FloaterGroundAt(G, Now), SunLocal);
	double Dist = FMath::Min(FVector::Dist(Pose.PosCm, CamLocal), FVector::Dist(Fall, CamLocal));
	const double Along = FVector::Dist(Fall, Pose.PosCm);
	if (Along > 1.0)
	{
		const double T = FMath::Clamp(FVector::DotProduct(CamLocal - Pose.PosCm, -SunLocal), 0.0, 2.0 * Along);
		Dist = FMath::Min(Dist, FVector::Dist(Pose.PosCm - SunLocal * T, CamLocal));
	}
	const bool bShadow = S.bShadow ? Dist < D.ShadowReachCm * 1.2 : Dist < D.ShadowReachCm;
	if (bShadow != S.bShadow) { S.bShadow = bShadow; S.Set->SetCastShadow(bShadow); }
}

int32 UPlanetFaunaComponent::StartleAll(const AActor* Craft)
{
	const double Now = ClockSeconds();
	const FTransform& Frame = GetComponentTransform();
	// What comes at them: the craft where it is and as it flies, or the camera, standing still, with none.
	FVector From = LastCamLocal, Vel = FVector::ZeroVector;
	if (Craft)
	{
		const UStarshipMovementComponent* M = Craft->FindComponentByClass<UStarshipMovementComponent>();
		From = Frame.InverseTransformPositionNoScale(Craft->GetActorLocation());
		Vel = Frame.InverseTransformVectorNoScale(M ? M->Velocity : Craft->GetVelocity());
	}
	int32 N = 0;
	for (TPair<uint64, FGroupSet>& Pair : Sets)
	{
		const SlonkFauna::FFaunaGroup& G = *Pair.Value.Group;
		const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(G.Kind);
		if (Pair.Value.Leaper.IsValid())
		{
			// A group of leapers dives, whatever comes at it, on the world's own seconds (but for the second or two after a
			// dive's window closes, while the leaps it kept under are not yet past their time back in the water).
			SlonkFauna::FPanic Dive = LeaperDives.FindRef(Pair.Key);
			if (SlonkFauna::StartleLeapers(Dive, D, Pair.Value.Pose.PosCm, SlonkFauna::FFaunaThreat(), WorldSeconds(), true))
			{
				LeaperDives.Add(Pair.Key, Dive);
				++N;
				++Startles;
			}
			continue;
		}
		SlonkFauna::FPanic Panic = Panics.FindRef(Pair.Key);
		// A herd where its frame stands on the ground, a flock where its path and its fright have it.
		const FVector Pos = Pair.Value.Herd.IsValid() ? Pair.Value.Herd->FramePos : SlonkFauna::PathAt(G, Params.RadiusCm, Now).PosCm + SlonkFauna::PanicOffset(Panic, D, Now);
		// As if it came at the group low, from the way it really is along the ground (no further off than half the
		// kind's scare radius) and at its own velocity, whatever startled it last: each flock flees from where it is,
		// away from it, or off its track if it is overhead.
		const FVector Up = Pos.GetSafeNormal();
		FVector Toward = From - Pos;
		Toward -= Up * FVector::DotProduct(Toward, Up);
		SlonkFauna::FFaunaThreat T;
		T.PosCm = Pos + Toward.GetClampedToMaxSize(0.5 * D.ScareRadiusCm);
		T.VelCmS = Vel;
		T.AltitudeCm = 0.0;
		T.bCraft = true;
		if (!SlonkFauna::Scares(D, Pos, T)) { continue; }
		// However lately it was startled, as any startle leaves it: what is left of its last flight carried, its members'
		// scatter kept if still open, its wingbeat going on from where it stands; a herd stopping and turning to flee where
		// it stands, unless it is galloping. A jelly only as a craft passing would (StartleByCommand), so asked again and
		// again it is never pushed off past the ground read round its loop.
		if (Pair.Value.Herd.IsValid()) { SlonkFauna::StartleHerd(Panic, *Pair.Value.Herd, G, D, Params.RadiusCm, T, Now, true, Pair.Value.Water.Get()); }
		else if (!SlonkFauna::StartleByCommand(Panic, D, Pos, T, Now)) { continue; }
		Panics.Add(Pair.Key, Panic);
		++N;
		++Startles;
	}
	return N;
}

uint64 SlonkFauna::PickWatched(TConstArrayView<FDrawnGroupAt> Drawn, const FVector& View, int32 Kind, uint64 Preferred)
{
	// The group a goto found, while it is drawn and of the kind asked for, before any nearer the view.
	if (Preferred != 0)
	{
		for (const FDrawnGroupAt& G : Drawn) { if (G.Id == Preferred && (Kind < 0 || (int32)G.Kind == Kind)) { return Preferred; } }
	}
	uint64 Best = 0; double BestD = TNumericLimits<double>::Max();
	for (const FDrawnGroupAt& G : Drawn)
	{
		if (Kind >= 0 && (int32)G.Kind != Kind) { continue; }
		const double D = FVector::DistSquared(G.Pos, View);
		if (D < BestD) { BestD = D; Best = G.Id; }
	}
	return Best;
}

uint64 SlonkFauna::TakeWatched(TConstArrayView<FDrawnGroupAt> Drawn, const FVector& View, int32 Kind, uint64& GotoId)
{
	const uint64 Picked = PickWatched(Drawn, View, Kind, GotoId);
	if (Picked != 0 && Picked == GotoId) { GotoId = 0; }
	return Picked;
}

uint64 UPlanetFaunaComponent::NearestDrawnGroup(const FVector& WorldPos, int32 Kind, uint64* GotoId) const
{
	TArray<SlonkFauna::FDrawnGroupAt> Drawn;
	Drawn.Reserve(Sets.Num());
	for (const TPair<uint64, FGroupSet>& Pair : Sets)
	{
		if (!Pair.Value.Set || !Pair.Value.Group.IsValid()) { continue; }
		Drawn.Add({ Pair.Key, Pair.Value.Group->Kind, Pair.Value.Set->GetComponentLocation() });
	}
	return GotoId ? SlonkFauna::TakeWatched(Drawn, WorldPos, Kind, *GotoId) : SlonkFauna::PickWatched(Drawn, WorldPos, Kind, 0);
}

bool UPlanetFaunaComponent::GetDrawnGroupPose(uint64 Id, FVector& OutWorldPos, FQuat& OutWorldRot) const
{
	const FGroupSet* S = Sets.Find(Id);
	if (!S || !S->Set) { return false; }
	OutWorldPos = S->Set->GetComponentLocation();
	OutWorldRot = S->Set->GetComponentQuat();
	// A herd's frame never turns: its pose faces the way it walks.
	if (S->Herd.IsValid()) { OutWorldRot = OutWorldRot * FQuat(FVector::ZAxisVector, S->Herd->Heading); }
	return true;
}

EFaunaKind UPlanetFaunaComponent::GetDrawnGroupKind(uint64 Id) const
{
	const FGroupSet* S = Sets.Find(Id);
	return S && S->Group.IsValid() ? S->Group->Kind : EFaunaKind::Flier;
}

FString UPlanetFaunaComponent::Describe() const
{
	const SlonkFauna::FFaunaWorld& W = Field.GetWorld();
	FString Kinds;
	for (int32 K = 0; K < SlonkFauna::NumKinds; ++K)
	{
		if (!W.Spawns((EFaunaKind)K)) { continue; }
		Kinds += FString::Printf(TEXT("%s%d %ss (%d members)"), Kinds.IsEmpty() ? TEXT("") : TEXT(", "), Field.ActiveGroups((EFaunaKind)K), SlonkFauna::KindName((EFaunaKind)K), Field.ActiveMembers((EFaunaKind)K));
	}
	int32 Shaken = 0;
	const double Now = ClockSeconds();
	for (const TPair<uint64, SlonkFauna::FPanic>& P : Panics) { Shaken += P.Value.IsLive(Now) ? 1 : 0; }
	// The herds about: how many are grazing, walking and fleeing, and how many members.
	int32 Grazing = 0, Walking = 0, Fleeing = 0, Walkers = 0;
	for (const TPair<uint64, FGroupSet>& S : Sets)
	{
		if (!S.Value.Herd.IsValid()) { continue; }
		Walkers += S.Value.Group->Members;
		const bool bShaken = Panics.Contains(S.Key) && Panics[S.Key].IsLive(Now);
		(bShaken ? Fleeing : (S.Value.Herd->Travel > 0.5 ? Walking : Grazing)) += 1;
	}
	// Their dust: how many raise it, and the pool's components.
	const FString Dust = !SlonkFauna::DustEnabled() ? FString(TEXT("no dust (slonk.Fauna.Dust 0)"))
		: (bDustMissing ? FString(TEXT("no dust (NS_HerdDust missing)"))
			: FString::Printf(TEXT("%d raising dust (%d components, %d resting)"), DustPool.NumLive(), DustPool.NumMade(), DustPool.NumResting()));
	const FString Herds = !CanDrawHerds() ? FString(TEXT("no herds (M_Fauna has no walker branch yet)"))
		: FString::Printf(TEXT("%d herds drawn (%d walkers: %d grazing, %d walking, %d fleeing; %s) in %s"), Grazing + Walking + Fleeing, Walkers, Grazing, Walking, Fleeing, *Dust,
			bWalkerMaterial ? (HerdPlanMIDs.Num() > 0 ? *FString::Printf(TEXT("M_Fauna's walker branch (%d instances of alien plans)"), HerdPlanMIDs.Num()) : TEXT("M_Fauna's walker branch"))
				: TEXT("the basic material (slonk.Fauna.Debug)"));
	// The groups drawn of each species of a kind of more than one (fauna plans says what each is).
	FString BySpecies;
	for (int32 K = 0; K < SlonkFauna::NumKinds; ++K)
	{
		const SlonkFauna::FFaunaKindSpecies& Sp = Profile.Species[K];
		if (Sp.Num <= 1) { continue; }
		FString Each;
		for (int32 i = 0; i < Sp.Num; ++i)
		{
			int32 Groups = 0, Members = 0;
			for (const TPair<uint64, FGroupSet>& S : Sets)
			{
				if ((int32)S.Value.Group->Kind != K || S.Value.Group->Species != i) { continue; }
				++Groups;
				Members += S.Value.Group->Members;
			}
			Each += FString::Printf(TEXT("%s#%d %d (%d)"), Each.IsEmpty() ? TEXT("") : TEXT(", "), i, Groups, Members);
		}
		BySpecies += FString::Printf(TEXT("%s%s species drawn %s"), BySpecies.IsEmpty() ? TEXT("; ") : TEXT(", "), SlonkFauna::KindName((EFaunaKind)K), *Each);
	}
	// The leapers about: how many groups and fish, how many fish are out of the water (or rising or diving under it) now, how
	// many groups are diving; and the splashes in the air.
	int32 LeaperGroups = 0, Fish = 0, Diving = 0;
	const double WorldNow = WorldSeconds();
	for (const TPair<uint64, FGroupSet>& S : Sets)
	{
		if (!S.Value.Leaper.IsValid()) { continue; }
		++LeaperGroups;
		Fish += S.Value.Group->Members;
		Diving += LeaperDives.Contains(S.Key) && LeaperDives[S.Key].IsLive(WorldNow) ? 1 : 0;
	}
	const FString Splashes = !SlonkFauna::SplashEnabled() ? FString(TEXT("no splashes (slonk.Fauna.Splash 0)"))
		: (bSplashMissing ? FString(TEXT("no splashes (NS_FishSplash missing)"))
			: FString::Printf(TEXT("%d splashes in the air (%d thrown, %d components, %d resting, %d let go)"), SplashPool.NumLive(), SplashPool.NumBursts(), SplashPool.NumMade(),
				SplashPool.NumResting(), SplashPool.NumDropped()));
	const FString Leapers = Sea.ShoalDensity <= 0.f ? FString(TEXT("no leapers (no shoals in these seas)"))
		: (!CanDrawLeapers() ? FString(TEXT("no leapers (M_Fauna has no leaper branch yet)"))
			: FString::Printf(TEXT("%d leaper groups drawn (%d fish, %d leaping now, %d groups diving; %s) in %s"), LeaperGroups, Fish, GetLeapingCount(), Diving, *Splashes,
				bLeaperMaterial ? (LeaperPlanMIDs.Num() > 0 ? *FString::Printf(TEXT("M_Fauna's leaper branch (%d instances of alien plans)"), LeaperPlanMIDs.Num())
					: (bAlienLeaperMaterial ? TEXT("M_Fauna's leaper branch") : TEXT("M_Fauna's leaper branch (no FishForm yet: alien fish drawn as Earth fish)"))) : TEXT("the basic material (slonk.Fauna.Debug)")));
	// The jellies about: how many, how many shaken and casting shadows; and why a world has none (read as if they could be
	// drawn, then whether they can).
	int32 Jellies = 0, JelliesShaken = 0, JelliesShadowed = 0;
	for (const TPair<uint64, FGroupSet>& S : Sets)
	{
		if (S.Value.Group->Kind != EFaunaKind::Floater) { continue; }
		++Jellies;
		JelliesShaken += Panics.Contains(S.Key) && Panics[S.Key].IsLive(Now) ? 1 : 0;
		JelliesShadowed += S.Value.bShadow ? 1 : 0;
	}
	SlonkFauna::FFaunaWorld Wf = W;
	if ((KindMask & SlonkFauna::KindBit(EFaunaKind::Floater)) != 0u) { Wf.KindMask |= SlonkFauna::KindBit(EFaunaKind::Floater); }
	const FString Absent = SlonkFauna::FloaterAbsence(Wf);
	const FString Floaters = !Absent.IsEmpty() ? FString::Printf(TEXT("no jellies (%s)"), *Absent)
		: (!CanDrawFloaters() ? FString(TEXT("no jellies (M_Fauna has no floater branch yet)"))
			: FString::Printf(TEXT("%d jellies drawn (%d shaken, %d casting shadows; %.0f%% of its cells hold one at its life and density) in %s"), Jellies, JelliesShaken, JelliesShadowed,
				100.f * FMath::Min(Profile.FloaterShare * W.Life * W.Density, 1.f), bFloaterMaterial ? TEXT("M_Fauna's floater branch") : TEXT("the basic material (slonk.Fauna.Debug)")));
	return FString::Printf(TEXT("%s, life %.2f, density %.2f, strangeness %.2f: %s%s; %s; %s; %s; %d groups in the last scan of %d cells (%d read anew, %.1f ms), %d shaken, %d startles, clock %+.0f s, %s; %s"),
		SlonkFauna::IsEnabled() ? TEXT("on") : TEXT("off"), W.Life, W.Density, Profile.Strangeness, Kinds.IsEmpty() ? TEXT("none live here") : *Kinds, *BySpecies, *Herds, *Leapers, *Floaters,
		Field.GetCandidates().Num(), LastScanCells, LastScanEvaluated, LastScanMs, Shaken, Startles, TimeOffset,
		MID ? (bFaunaMaterial ? (FlierPlanMIDs.Num() > 0 ? *FString::Printf(TEXT("M_Fauna (%d instances of alien fliers' plans)"), FlierPlanMIDs.Num())
			: (bAlienFlierMaterial ? TEXT("M_Fauna") : TEXT("M_Fauna (no WingForm yet: alien fliers beat their wings as birds)")))
			: (bPlainAsked ? TEXT("the basic material (slonk.Fauna.Debug)") : TEXT("the basic material (M_Fauna missing)"))) : TEXT("no material yet"),
		Terrain ? *Terrain->DescribeMotes() : TEXT("no motes"));
}

void UPlanetFaunaComponent::DescribePlans(TArray<FString>& OutLines) const
{
	OutLines.Reset();
	for (int32 K = 0; K < SlonkFauna::NumKinds; ++K)
	{
		const EFaunaKind Kind = (EFaunaKind)K;
		if (Kind == EFaunaKind::Mote) { continue; }
		const SlonkFauna::FFaunaKindSpecies& Sp = Profile.Species[K];
		for (int32 i = 0; i < Sp.Num; ++i)
		{
			const SlonkFauna::FFaunaSpecies& S = Sp.List[i];
			// Its size: the kind's own range times the species' (a herd's walkers a tenth either way of its span; a leaper's
			// fish of 0.35 to 0.8 m, an eel's of a metre to 1.8 m; a flock's span its habitat's; a jelly's bell 20 to 80 m).
			FString Size;
			if (Kind == EFaunaKind::Herd)
			{
				const SlonkFauna::Walker::FShape Shape = SlonkFauna::Walker::ShapeOf(S.Plan);
				const double Lo = FMath::Clamp(FMath::Clamp(1.6 * S.SizeScale, Shape.MinSizeM, Shape.MaxSizeM / 1.1) * 0.9, Shape.MinSizeM, Shape.MaxSizeM);
				const double Hi = FMath::Clamp(FMath::Clamp(2.3 * S.SizeScale, Shape.MinSizeM, Shape.MaxSizeM / 1.1) * 1.1, Shape.MinSizeM, Shape.MaxSizeM);
				Size = FString::Printf(TEXT("%.1f to %.1f m long"), Lo, Hi);
			}
			else if (Kind == EFaunaKind::Leaper && SlonkFauna::FishForm::FormOf(S.Plan) == SlonkFauna::FishForm::Eel)
			{
				// An eel's as its groups' spans are drawn (LeaperSpanM, LeaperInCell's own), its fish a tenth either way.
				const double Longest = SlonkFauna::LeaperMaxSizeM(S.Plan);
				Size = FString::Printf(TEXT("%.2f to %.2f m long"), FMath::Max((double)SlonkFauna::LeaperSpanM(S.Plan, S.SizeScale, 0.35f) * 0.9, SlonkFauna::Leaper::MinSizeM),
					FMath::Min((double)SlonkFauna::LeaperSpanM(S.Plan, S.SizeScale, 0.8f) * 1.1, Longest));
			}
			else if (Kind == EFaunaKind::Leaper)
			{
				const double Longest = SlonkFauna::LeaperMaxSizeM(S.Plan);
				Size = FString::Printf(TEXT("%.2f to %.2f m long"), FMath::Clamp(0.35 * S.SizeScale, SlonkFauna::Leaper::MinSizeM, Longest / 1.1) * 0.9,
					FMath::Min(FMath::Clamp(0.8 * S.SizeScale, SlonkFauna::Leaper::MinSizeM, Longest / 1.1) * 1.1, Longest));
			}
			else if (Kind == EFaunaKind::Floater) { Size = FString::Printf(TEXT("%.0f to %.0f m across"), SlonkFauna::Floater::MinSpanM * S.SizeScale, SlonkFauna::Floater::MaxSpanM * S.SizeScale); }
			else { Size = FString::Printf(TEXT("%.2f times its habitat's span"), S.SizeScale); }
			const TCHAR* Here = Kind == EFaunaKind::Floater && Profile.FloaterShare <= 0.f ? TEXT(" (none live here)") : TEXT("");
			OutLines.Add(FString::Printf(TEXT("%s #%d: %s; %.0f%% of its cells; %s%s"), SlonkFauna::KindName(Kind), i, *SlonkFauna::DescribePlan(Kind, S.Plan), 100.f * S.Share, *Size, Here));
		}
	}
}

void UPlanetFaunaComponent::DescribeCells(TArray<FString>& OutLines) const
{
	OutLines.Reset();
	const FVector CamDir = LastCamLocal.GetSafeNormal();
	const double Now = ClockSeconds();
	for (const SlonkFauna::FFaunaField::FGroupRef& G : Field.GetCandidates())
	{
		const double Dist = FVector::Dist(CamDir, G->Centre) * Params.RadiusCm;
		if (G->Kind == EFaunaKind::Leaper)
		{
			const double WorldNow = WorldSeconds();
			const bool bDiving = LeaperDives.Contains(G->Id) && LeaperDives[G->Id].IsLive(WorldNow);
			OutLines.Add(FString::Printf(TEXT("%016llx leaper: %d fish of %.2f m leaping every %.0f s about, from a shoal %.0f by %.0f m %.0f m down over %.0f m of water at the least, %.2f km off%s%s"),
				G->Id, G->Members, G->SpanM, G->CycleSec, 2.0 * G->ShoalHalfLengthCm / 100.0, 2.0 * G->ShoalHalfWidthCm / 100.0, G->ShoalDepthCm / 100.0, G->WaterCm / 100.0, Dist / 1e5,
				Sets.Contains(G->Id) ? TEXT(", about") : TEXT(""), bDiving ? TEXT(", diving") : TEXT("")));
			continue;
		}
		const SlonkFauna::FFaunaPose Pose = SlonkFauna::PathAt(*G, Params.RadiusCm, Now);
		if (G->Kind == EFaunaKind::Floater)
		{
			OutLines.Add(FString::Printf(TEXT("%016llx floater: a jelly of %.0f m (%d ribs, pulsing every %.1f s), %.1f km off, loop %.1f km in %.0f s (%.1f m/s), cruising %.0f m over the highest ground near its loop (its tentacles' tips %.0f m over it), now %.0f m over the sea's level%s%s"),
				G->Id, G->SpanM, G->Ribs, G->FlapHz > 0.f ? 1.0 / G->FlapHz : 0.0, Dist / 1e5, G->LengthCm / 1e5, G->PeriodSec, G->SpeedCmS / 100.0, G->CruiseCm / 100.0,
				(G->CruiseCm - G->HangCm) / 100.0, (Pose.PosCm.Size() - Params.RadiusCm) / 100.0, Sets.Contains(G->Id) ? TEXT(", about") : TEXT(""),
				Panics.Contains(G->Id) && Panics[G->Id].IsLive(Now) ? TEXT(", shaken") : TEXT("")));
			continue;
		}
		if (G->Kind == EFaunaKind::Herd)
		{
			const FGroupSet* S = Sets.Find(G->Id);
			const double Speed = SlonkFauna::HerdSpeedCmS(*G, Now) / 100.0;
			const TCHAR* Doing = Panics.Contains(G->Id) && Panics[G->Id].IsLive(Now) ? TEXT("fleeing") : (Speed > 1.8 ? TEXT("trotting") : (Speed > 0.05 ? TEXT("walking") : TEXT("grazing")));
			OutLines.Add(FString::Printf(TEXT("%016llx herd (species #%d): %d walkers of %.1f m within %.0f m, %.1f km off, loop %.1f km a lap an hour, every %.0f s walking %.0f s at %.1f m/s (trotting %.0f s of it at %.1f m/s); now %s%s%s"),
				G->Id, G->Species, G->Members, G->SpanM, G->HerdRadiusCm / 100.0, Dist / 1e5, G->LengthCm / 1e5, G->CycleSec, G->WalkB - G->WalkA, G->WalkCmS / 100.0, G->TrotB - G->TrotA,
				G->TrotCmS / 100.0, Doing, S ? TEXT(", about") : TEXT(""),
				S && S->Herd.IsValid() ? *FString::Printf(TEXT(" (%.1f m/s, %.0f m off its path for the crew)"), S->Herd->SpeedMS, S->Herd->Avoid.Size() / 100.0) : TEXT("")));
			continue;
		}
		OutLines.Add(FString::Printf(TEXT("%016llx %s %s: %d of %.2f m span, %.1f km off, loop %.1f km in %.0f s (%.1f m/s), %.0f m over the ground, now %.0f m up%s%s"),
			G->Id, SlonkFauna::KindName(G->Kind), SlonkFauna::HabitatName(G->Habitat), G->Members, G->SpanM, Dist / 1e5, G->LengthCm / 1e5, G->PeriodSec, G->SpeedCmS / 100.0,
			G->CruiseCm / 100.0, (Pose.PosCm.Size() - Params.RadiusCm) / 100.0, Sets.Contains(G->Id) ? TEXT(", about") : TEXT(""),
			Panics.Contains(G->Id) && Panics[G->Id].IsLive(Now) ? TEXT(", shaken") : TEXT("")));
	}
	// The sets drawn, as the scene has them: where each is against the camera, whether it is registered, shown and has a
	// proxy, its bounds and its first member, for a look at why a flock does not show.
	FVector Cam = FVector::ZeroVector;
	if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr) { if (PC->PlayerCameraManager) { Cam = PC->PlayerCameraManager->GetCameraLocation(); } }
	for (const TPair<uint64, FGroupSet>& Pair : Sets)
	{
		const UPlanetInstancesComponent* C = Pair.Value.Set;
		if (!C) { continue; }
		OutLines.Add(FString::Printf(TEXT("set %016llx: %.0f m from the camera; registered %d, visible %d, proxy %d, %d instances, mesh %s, material %s; bounds %.0f m round %s; scale %s; parent %s; relative %s"),
			Pair.Key, FVector::Dist(C->GetComponentLocation(), Cam) / 100.0, C->IsRegistered() ? 1 : 0, C->IsVisible() ? 1 : 0, C->SceneProxy ? 1 : 0, C->GetInstanceCount(),
			C->GetStaticMesh() ? *C->GetStaticMesh()->GetName() : TEXT("none"), C->GetMaterial(0) ? *C->GetMaterial(0)->GetName() : TEXT("none"),
			C->Bounds.SphereRadius / 100.0, *(C->Bounds.Origin - Cam).ToString(), *C->GetComponentScale().ToString(), C->GetAttachParent() ? *C->GetAttachParent()->GetName() : TEXT("none"),
			*C->GetRelativeLocation().ToString()));
	}
}
