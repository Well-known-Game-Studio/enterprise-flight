#include "PlanetFaunaComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FaunaMeshes.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PersonPawn.h"
#include "PlanetBody.h"
#include "PlanetInstancesComponent.h"
#include "PlanetTerrainComponent.h"
#include "ScatterSpot.h"
#include "SlonkMaterials.h"
#include "StarshipMovementComponent.h"
#include "StarshipPawn.h"

static_assert(SlonkFauna::Custom::Num == SlonkScatter::NumCustomFloats, "every planet instance has the scatter's count of custom floats, so one pass writes them all");

// The animals on the worlds with life. 0 takes every one away, and the seas' plankton and shoals with them (APlanetBody),
// so a world looks as it did before it had any (for captures to compare with).
static TAutoConsoleVariable<int32> CVarFaunaEnable(TEXT("slonk.Fauna.Enable"), 1,
	TEXT("The animals on the worlds with life: the flocks, and the seas' plankton and shoals (1, the default), or none, every world as it was before them (0)"));
static TAutoConsoleVariable<float> CVarFaunaDensity(TEXT("slonk.Fauna.Density"), 1.f,
	TEXT("Scales the share of the habitat's cells holding a group of animals (1 as designed, 0 none; the caps on groups and members still hold)"));

bool SlonkFauna::IsEnabled() { return CVarFaunaEnable.GetValueOnGameThread() != 0; }
float SlonkFauna::DensitySetting() { return FMath::Max(CVarFaunaDensity.GetValueOnGameThread(), 0.f); }

UPlanetFaunaComponent::UPlanetFaunaComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetMobility(EComponentMobility::Movable);
}

void UPlanetFaunaComponent::Configure(UPlanetTerrainComponent* InTerrain, const FPlanetTerrainParams& P, const SlonkFauna::FFaunaProfile& InProfile, float InLife, const FScatterPalette& InPalette)
{
	Terrain = InTerrain;
	Params = P;
	Profile = InProfile;
	Life = FMath::Clamp(InLife, 0.f, 1.f);
	Palette = InPalette;
	bConfigured = true;
	RefreshWorld();
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
	return W;
}

void UPlanetFaunaComponent::RefreshWorld()
{
	const SlonkFauna::FFaunaWorld W = MakeFaunaWorld();
	if (Field.HasWorld() && W == Field.GetWorld()) { return; }
	// Another world, density, kinds or profile: every group goes, and the cells are read again.
	ReleaseAllSets();
	Field.SetWorld(W);
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
	TEXT("1: the animals in the engine's basic material; 2: that, at twenty times their size (every group drawn anew as it changes)"));

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
	UStaticMesh* Mesh = SlonkFaunaMeshes::GetMesh(Profile.Plans[(int32)G->Kind], G->Kind);
	if (!Mesh) { return; }
	EnsureMaterial();
	const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(G->Kind);
	UPlanetInstancesComponent* C = AcquireSet();
	if (C->GetStaticMesh() != Mesh) { C->SetStaticMesh(Mesh); }
	C->SetCullPadding(D.CullPaddingUnits);
	if (MID && C->GetMaterial(0) != MID) { C->SetMaterial(0, MID); }
	C->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	// Where it flies now, facing its flight at once if it is still shaken (it had no frame before).
	const double Now = ClockSeconds();
	const SlonkFauna::FFaunaPose Pose = SlonkFauna::FlightAt(*G, D, Panics.FindRef(G->Id), Params.RadiusCm, Now, SlonkFauna::FFaunaPose(), SlonkFauna::PanicSec);
	C->SetRelativeLocationAndRotation(Pose.PosCm, Pose.Rot);
	TArray<FTransform> Xf;
	TArray<float> Custom;
	SlonkFauna::MembersOf(*G, Xf, Custom);
	if (DebugDrawn >= 2) { for (FTransform& T : Xf) { T.SetScale3D(T.GetScale3D() * 20.0); } }
	C->SetPieces(Xf, Custom);
	if (C->CastShadow) { C->SetCastShadow(false); }
	C->SetVisibility(true);
	Sets.Add(G->Id, { G, C, false, Pose, Now });
}

void UPlanetFaunaComponent::ReleaseSet(uint64 Id)
{
	FGroupSet S;
	if (!Sets.RemoveAndCopyValue(Id, S) || !S.Set) { return; }
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

void UPlanetFaunaComponent::Suspend()
{
	if (Sets.Num() == 0 && !Field.GetActive().Num()) { return; }
	ReleaseAllSets();
	// The cells read are kept: back in view, the same groups come back at once.
	Field.Clear(false);
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
	RefreshWorld();
	// slonk.Fauna.Debug changed: every group goes, and comes back drawn as it now asks.
	const int32 Debug = CVarFaunaDebug.GetValueOnGameThread();
	if (Debug != DebugDrawn)
	{
		DebugDrawn = Debug;
		Suspend();
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
	if (OverSea > Ceiling + FMath::Max(Params.ReliefCm, 0.0) || OverSea - FMath::Max(Terrain->SurfaceHeightAtWorld(ViewLocationWorld), 0.0) > Ceiling)
	{
		Suspend();
		return;
	}
	if (!bScanning && Field.WantsScan(CamDir))
	{
		// The cells round the camera's ground point, read on a task from what the last scan read.
		ScanGeneration = Generation;
		bScanning = true;
		ScanTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [Wc = SlonkFauna::FFaunaWorld(W), CamDir, Radius = Field.ScanRadiusCm(), Cache = Field.GetCache()]()
		{
			return FScanRef(SlonkFauna::FFaunaField::Scan(Wc, CamDir, Radius, Cache));
		});
	}
	TArray<SlonkFauna::FFaunaField::FGroupRef> Spawned;
	TArray<uint64> LetGo;
	Field.Select(CamLocal, Spawned, LetGo);
	for (const uint64 Id : LetGo) { ReleaseSet(Id); }
	for (const SlonkFauna::FFaunaField::FGroupRef& G : Spawned) { SpawnSet(G); }

	// The frights that have run their course are forgotten, the group about or not.
	const double Now = ClockSeconds();
	for (auto It = Panics.CreateIterator(); It; ++It)
	{
		if (!It.Value().IsLive(Now)) { It.RemoveCurrent(); }
	}
	TArray<SlonkFauna::FFaunaThreat> Threats;
	GatherThreats(PC, Threats);
	const FVector Sun = SunDirectionWorld.GetSafeNormal();
	for (TPair<uint64, FGroupSet>& Pair : Sets)
	{
		FGroupSet& S = Pair.Value;
		const SlonkFauna::FFaunaGroup& G = *S.Group;
		const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(G.Kind);
		// Its frame as it flies, on its path or shaken off it, turning from the frame it had.
		SlonkFauna::FPanic Panic = Panics.FindRef(Pair.Key);
		const double Dt = FMath::Max(Now - S.PoseAt, 0.0);
		SlonkFauna::FFaunaPose Pose = SlonkFauna::FlightAt(G, D, Panic, Params.RadiusCm, Now, S.Pose, Dt);
		for (const SlonkFauna::FFaunaThreat& T : Threats)
		{
			if (SlonkFauna::StartleGroup(Panic, D, Pose.PosCm, T, Now))
			{
				Panics.Add(Pair.Key, Panic);
				Pose = SlonkFauna::FlightAt(G, D, Panic, Params.RadiusCm, Now, S.Pose, Dt);
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
		SlonkFauna::FrightData(Panic, Now, Pose.Rot, TimeOffset, Gd);
		// Slot by slot: 0 to 3, 4 to 7, and 8 and 9.
		static_assert(SlonkFauna::GroupData::Num == 10, "a group's custom primitive data is sent as slots 0 to 3, 4 to 7, and 8 and 9");
		S.Set->SetCustomPrimitiveDataVector4(0, FVector4(Gd[0], Gd[1], Gd[2], Gd[3]));
		S.Set->SetCustomPrimitiveDataVector4(4, FVector4(Gd[4], Gd[5], Gd[6], Gd[7]));
		S.Set->SetCustomPrimitiveDataVector2(8, FVector2D(Gd[8], Gd[9]));
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
		if (MID)
		{
			float Fov = 90.f;
			if (PC && PC->PlayerCameraManager) { Fov = PC->PlayerCameraManager->GetFOVAngle(); }
			FVector2D Viewport(1920.0, 1080.0);
			if (GEngine && GEngine->GameViewport) { GEngine->GameViewport->GetViewportSize(Viewport); }
			const float PxPerRad = 0.5f * (float)FMath::Max(Viewport.X, 1.0) / FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(Fov, 5.f, 170.f)) * 0.5f);
			MID->SetVectorParameterValue(TEXT("ViewPos"), FLinearColor((float)ViewLocationWorld.X, (float)ViewLocationWorld.Y, (float)ViewLocationWorld.Z, 0.f));
			if (!FMath::IsNearlyEqual(PxPerRad, LastPxPerRad, 0.5f)) { LastPxPerRad = PxPerRad; MID->SetScalarParameterValue(TEXT("PxPerRad"), PxPerRad); }
		}
		// Every group's members are written with its frame of this frame, whether or not the rocks and plants are.
		Terrain->QueuePlanetInstanceWrites();
	}
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
		SlonkFauna::FPanic Panic = Panics.FindRef(Pair.Key);
		const FVector Pos = SlonkFauna::PathAt(G, Params.RadiusCm, Now).PosCm + SlonkFauna::PanicOffset(Panic, D, Now);
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
		// scatter kept if still open, its wingbeat going on from where it stands.
		SlonkFauna::StartleNow(Panic, D, Pos, T, Now);
		Panics.Add(Pair.Key, Panic);
		++N;
		++Startles;
	}
	return N;
}

uint64 UPlanetFaunaComponent::NearestDrawnGroup(const FVector& WorldPos) const
{
	uint64 Best = 0; double BestD = TNumericLimits<double>::Max();
	for (const TPair<uint64, FGroupSet>& Pair : Sets)
	{
		if (!Pair.Value.Set) { continue; }
		const double D = FVector::DistSquared(Pair.Value.Set->GetComponentLocation(), WorldPos);
		if (D < BestD) { BestD = D; Best = Pair.Key; }
	}
	return Best;
}

bool UPlanetFaunaComponent::GetDrawnGroupPose(uint64 Id, FVector& OutWorldPos, FQuat& OutWorldRot) const
{
	const FGroupSet* S = Sets.Find(Id);
	if (!S || !S->Set) { return false; }
	OutWorldPos = S->Set->GetComponentLocation();
	OutWorldRot = S->Set->GetComponentQuat();
	return true;
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
	return FString::Printf(TEXT("%s, life %.2f, density %.2f, strangeness %.2f: %s; %d groups in the last scan of %d cells (%d read anew, %.1f ms), %d shaken, %d startles, clock %+.0f s, %s"),
		SlonkFauna::IsEnabled() ? TEXT("on") : TEXT("off"), W.Life, W.Density, Profile.Strangeness, Kinds.IsEmpty() ? TEXT("none live here") : *Kinds,
		Field.GetCandidates().Num(), LastScanCells, LastScanEvaluated, LastScanMs, Shaken, Startles, TimeOffset,
		MID ? (bFaunaMaterial ? TEXT("M_Fauna") : (bPlainAsked ? TEXT("the basic material (slonk.Fauna.Debug)") : TEXT("the basic material (M_Fauna missing)"))) : TEXT("no material yet"));
}

void UPlanetFaunaComponent::DescribeCells(TArray<FString>& OutLines) const
{
	OutLines.Reset();
	const FVector CamDir = LastCamLocal.GetSafeNormal();
	const double Now = ClockSeconds();
	for (const SlonkFauna::FFaunaField::FGroupRef& G : Field.GetCandidates())
	{
		const double Dist = FVector::Dist(CamDir, G->Centre) * Params.RadiusCm;
		const SlonkFauna::FFaunaPose Pose = SlonkFauna::PathAt(*G, Params.RadiusCm, Now);
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
