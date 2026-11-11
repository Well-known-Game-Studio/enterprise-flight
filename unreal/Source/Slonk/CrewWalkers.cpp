#include "CrewWalkers.h"
#include "CrewFigure.h"
#include "CrewRoster.h"
#include "MeshPrims.h"
#include "PersonPawn.h"
#include "ShipInterior.h"
#include "ShipSystemsComponent.h"
#include "SlonkMaterials.h"
#include "StarshipPawn.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/BlendSpace.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "Materials/MaterialInstanceDynamic.h"

// ---- the figure ---------------------------------------------------------------------------------------------

ACrewFigure::ACrewFigure()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Figure = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Figure"));
	Figure->SetupAttachment(Root);
	// The mannequin faces +Y; the figure's root faces +X, at the feet.
	Figure->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	Figure->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Figure->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Figure->CastShadow = true;
	Figure->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	Hair = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Hair"));
	Hair->SetupAttachment(Root);
	Hair->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Hair->CastShadow = false;
}

void ACrewFigure::Setup(USkeletalMesh* Mesh, UBlendSpace* Blend, UMaterialInterface* Crew, UTexture* Base, UTexture* Normal, const FLinearColor& Jacket, const FLinearColor& Skin, const FLinearColor& HairColor)
{
	if (Mesh) { Figure->SetSkeletalMesh(Mesh); }
	if (Crew && Mesh)
	{
		SlonkCrew::Dress(Figure, this, Crew, Mids, Base, Normal, nullptr);
		SlonkCrew::SetDress(Mids, FCrewDress::Uniform(Jacket, Skin));
	}
	if (Blend) { Figure->PlayAnimation(Blend, true); }
	// A dark cap over the crown stands for hair, as on the person.
	{
		using namespace SlonkMesh;
		UDynamicMesh* M = NewObject<UDynamicMesh>(this);
		CurrentMaterialID() = 0;
		Ellipsoid(M, FVector(-1.f, 0.f, 3.f), FVector(12.5f, 12.f, 8.5f), 14);
		FGeometryScriptCalculateNormalsOptions NO;
		UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(M, NO);
		Hair->GetDynamicMesh()->SetMesh(MoveTemp(M->GetMeshRef()));
		Hair->NotifyMeshUpdated();
		if (UMaterialInterface* Hull = SlonkMaterials::LoadOrFallback(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::HullPath)), TEXT("Hull"), true))
		{
			HairMID = UMaterialInstanceDynamic::Create(Hull, this);
			HairMID->SetVectorParameterValue(TEXT("HullColor"), HairColor);
			HairMID->SetScalarParameterValue(TEXT("PanelScale"), 3.f);
			HairMID->SetScalarParameterValue(TEXT("Roughness"), 0.9f);
			HairMID->SetScalarParameterValue(TEXT("Metallic"), 0.f);
			Hair->SetMaterial(0, HairMID);
		}
	}
	Speed = -1.f;
}

void ACrewFigure::Pose(const FTransform& Feet, float SpeedCm)
{
	SetActorTransform(Feet);
	if (!FMath::IsNearlyEqual(Speed, SpeedCm))
	{
		Speed = SpeedCm;
		if (UAnimSingleNodeInstance* Node = Figure->GetSingleNodeInstance()) { Node->SetBlendSpacePosition(FVector(0.f, SpeedCm, 0.f)); }
	}
	if (Hair && Figure->DoesSocketExist(TEXT("head")))
	{
		Hair->SetWorldLocationAndRotation(Figure->GetSocketLocation(TEXT("head")) + Feet.GetUnitAxis(EAxis::Z) * 9.0, Feet.GetRotation());
	}
}

// ---- the walkers --------------------------------------------------------------------------------------------

UCrewWalkersComponent::UCrewWalkersComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	const FString A = TEXT("/Game/Characters/UEFN_Mannequin/Animations/");
	FigureMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(SlonkCrew::UEFNPath));
	FigureBaseMap = TSoftObjectPtr<UTexture>(FSoftObjectPath(SlonkCrew::UEFNBasePath));
	FigureNormalMap = TSoftObjectPtr<UTexture>(FSoftObjectPath(SlonkCrew::UEFNNormalPath));
	LocomotionBlend = TSoftObjectPtr<UBlendSpace>(FSoftObjectPath(A + TEXT("BS_Crew_Locomotion2D.BS_Crew_Locomotion2D")));
	CrewMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkCrew::CrewMaterialPath));
}

AStarshipPawn* UCrewWalkersComponent::Ship() const { return Cast<AStarshipPawn>(GetOwner()); }
UShipInteriorComponent* UCrewWalkersComponent::Interior() const { AStarshipPawn* S = Ship(); return S ? S->GetInterior() : nullptr; }
UCrewRosterSubsystem* UCrewWalkersComponent::Roster() const
{
	AStarshipPawn* S = Ship();
	if (!S || !S->HasCrewModel() || !S->GetGameInstance()) { return nullptr; }
	return S->GetGameInstance()->GetSubsystem<UCrewRosterSubsystem>();
}

void UCrewWalkersComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	for (ACrewFigure* F : Pool) { if (F) { F->Destroy(); } }
	Pool.Reset();
	Super::EndPlay(Reason);
}

void UCrewWalkersComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UShipInteriorComponent* I = Interior();
	if (!I || !I->IsBuilt() || !Roster()) { return; }
	if (!Router) { Router = MakeUnique<FPlanRouter>(I->GetPlan()); }
	SyncTimer -= DeltaTime;
	if (SyncTimer <= 0.f) { SyncTimer = 0.5f; Sync(!bSettled); bSettled = true; }
	for (FCrewWalker& W : Walkers) { Advance(W, DeltaTime); }
	UpdateFigures();
}

int32 UCrewWalkersComponent::NumMoving() const { int32 N = 0; for (const FCrewWalker& W : Walkers) { if (W.bMoving) { ++N; } } return N; }
int32 UCrewWalkersComponent::NumInLift() const { int32 N = 0; for (const FCrewWalker& W : Walkers) { if (W.bInLift) { ++N; } } return N; }
int32 UCrewWalkersComponent::FiguresShown() const { int32 N = 0; for (const FCrewWalker& W : Walkers) { if (W.Figure.IsValid()) { ++N; } } return N; }

void UCrewWalkersComponent::Settle()
{
	for (FCrewWalker& W : Walkers) { W.Where.Empty(); W.bMoving = false; W.bInLift = false; }
	bSettled = false; SyncTimer = 0.f;
}

// ---- where the roster's compartments are on the plan ----

int32 UCrewWalkersComponent::PlaceFor(const FString& Where, int32 Seed) const
{
	UShipInteriorComponent* I = Interior();
	if (!I || !I->IsBuilt() || Where.IsEmpty()) { return INDEX_NONE; }
	// The roster's compartments (CrewRoster.cpp PostingFor, Place) against the sets and the plan's rooms by
	// name. A compartment with many rooms spreads its people over them by a hash of who they are, so everyone
	// has their own cabin and their own lab. The nacelles, the shore and an away team are nowhere aboard.
	struct FMap { const TCHAR* Where; EShipRoom Set; TArray<const TCHAR*> Names; };
	static const TArray<FMap> Table = {
		{ TEXT("Main bridge"), EShipRoom::Bridge, {} },
		{ TEXT("Sickbay"), EShipRoom::Sickbay, {} },
		{ TEXT("Main engineering"), EShipRoom::Engineering, {} },
		{ TEXT("Shuttle bay"), EShipRoom::ShuttleBay, {} },
		{ TEXT("Cargo bays"), EShipRoom::CargoBay, { TEXT("Cargo"), TEXT("General cargo") } },
		{ TEXT("Torpedo bay"), EShipRoom::TorpedoBay, {} },
		{ TEXT("Rec deck"), EShipRoom::RecDeck, { TEXT("Upper rec deck") } },
		{ TEXT("Officers' mess"), EShipRoom::Mess, { TEXT("Officers' dining mess") } },
		{ TEXT("Crew quarters"), EShipRoom::CrewQuarters, { TEXT("Enlisted quarters") } },
		{ TEXT("Officers' quarters"), EShipRoom::Count, { TEXT("Officers' quarters"), TEXT("Junior officers' quarters"), TEXT("Senior officers' quarters") } },
		{ TEXT("Computer core"), EShipRoom::Count, { TEXT("Computer core"), TEXT("Main computer memory bank") } },
		{ TEXT("Phaser control"), EShipRoom::Count, { TEXT("Phaser control") } },
		{ TEXT("Security and armoury"), EShipRoom::Count, { TEXT("Security control"), TEXT("Armoury"), TEXT("Brig cell"), TEXT("Security lounge") } },
		{ TEXT("Impulse engineering"), EShipRoom::Count, { TEXT("Impulse engineering"), TEXT("Engineering equipment") } },
		{ TEXT("Reactor"), EShipRoom::Count, { TEXT("Reactor"), TEXT("Fusion reactor"), TEXT("Intermix shaft") } },
		{ TEXT("Environmental control"), EShipRoom::Count, { TEXT("Environmental control") } },
		{ TEXT("Deflector"), EShipRoom::Count, { TEXT("Deflector"), TEXT("Engineering systems") } },
		{ TEXT("Sensor bay"), EShipRoom::Count, { TEXT("Lower sensor array"), TEXT("Sensor"), TEXT("Science sensor") } },
		{ TEXT("Shield generators"), EShipRoom::Count, { TEXT("Shield generator"), TEXT("Engineering equipment") } },
	};
	const FMap* M = nullptr;
	for (const FMap& T : Table) { if (Where == T.Where) { M = &T; break; } }
	TArray<int32> Candidates;
	if (M && M->Set != EShipRoom::Count) { const int32 S = I->IndexOf(M->Set); if (S != INDEX_NONE) { Candidates.Add(S); } }
	const TArray<const TCHAR*> Own = { *Where };
	const TArray<const TCHAR*>& Names = M ? M->Names : Own;
	for (int32 p = 0; p < I->NumPlaces(); ++p)
	{
		const FShipRoom* R = I->Place(p);
		if (!R || R->IsSignature() || R->PlanRoom == INDEX_NONE) { continue; }
		for (const TCHAR* N : Names) { if (R->Name.StartsWith(N)) { Candidates.Add(p); break; } }
	}
	if (Candidates.Num() == 0) { return INDEX_NONE; }
	const uint32 H = (uint32)Seed * 2654435761u;
	return Candidates[(int32)(H % (uint32)Candidates.Num())];
}

int32 UCrewWalkersComponent::StopOfPlace(int32 Place) const
{
	UShipInteriorComponent* I = Interior();
	const FShipRoom* R = I ? I->Place(Place) : nullptr;
	if (!R) { return INDEX_NONE; }
	if (R->Stop != INDEX_NONE) { return R->Stop; }
	// The three rooms off deck G's corridor share its door.
	if (R->IsSignature()) { const FShipRoom* C = I->Room(UShipInteriorComponent::LiftStop(R->Id)); return C ? C->Stop : INDEX_NONE; }
	return INDEX_NONE;
}

int32 UCrewWalkersComponent::DeckOfPlace(int32 Place) const
{
	UShipInteriorComponent* I = Interior();
	const FShipRoom* R = I ? I->Place(Place) : nullptr;
	if (!R) { return INDEX_NONE; }
	const FShipPlan& P = I->GetPlan();
	if (R->PlanRoom != INDEX_NONE && P.Rooms.IsValidIndex(R->PlanRoom)) { return P.Rooms[R->PlanRoom].Deck; }
	const int32 St = StopOfPlace(Place);
	return P.Stops.IsValidIndex(St) ? P.Stops[St].Deck : INDEX_NONE;
}

FVector UCrewWalkersComponent::SpotIn(int32 Place, int32 Seed, double& OutYaw) const
{
	UShipInteriorComponent* I = Interior();
	const FShipRoom* R = I ? I->Place(Place) : nullptr;
	if (!R) { OutYaw = 0.0; return FVector::ZeroVector; }
	// The stream's first draws are near enough linear in its seed that neighbours would stand in a row: scramble.
	uint32 H = (uint32)Seed * 2654435761u ^ ((uint32)Place * 40503u); H ^= H >> 13; H *= 0x5bd1e995u; H ^= H >> 15;
	FRandomStream Rs((int32)H);
	if (R->IsSignature())
	{
		// Along the line from the lift door to the room's thing, a little to either side; the sets' floors are
		// clear there. A set whose two spots coincide takes a few paces in from the door instead.
		const FVector L = R->Lift.GetLocation(), U = R->Use.GetLocation();
		const FVector Fwd = R->Lift.GetRotation().GetForwardVector().GetSafeNormal2D();
		const FVector Side(-Fwd.Y, Fwd.X, 0.0);
		FVector P;
		if (FVector::Dist2D(L, U) < 100.0) { P = L + Fwd * (150.0 + 400.0 * Rs.FRand()) + Side * Rs.FRandRange(-90.0, 90.0); }
		else { P = L + (U - L) * (0.3 + 0.6 * Rs.FRand()) + Side * Rs.FRandRange(-90.0, 90.0); }
		P.Z = L.Z;
		OutYaw = R->Use.GetRotation().Rotator().Yaw;
		return P;
	}
	// A generated room: anywhere in the middle of its box, facing into the room (its +X runs from the door in).
	const FVector Local(Rs.FRandRange(-0.55, 0.55) * R->Half.X, Rs.FRandRange(-0.6, 0.6) * R->Half.Y, -R->Half.Z);
	const FQuat Q(FVector::UpVector, R->Yaw);
	OutYaw = FMath::RadiansToDegrees(R->Yaw);
	return R->Center + Q.RotateVector(Local);
}

void UCrewWalkersComponent::SetExit(int32 Place, const FVector& From, TArray<FVector>& Out) const
{
	// Out of a set to the lift door it rides from: its own, or deck G's corridor's for the rooms off that
	// corridor (round its corner from the transporter room's leg).
	UShipInteriorComponent* I = Interior();
	const FShipRoom* R = I ? I->Place(Place) : nullptr;
	if (!R) { return; }
	Out.Add(From);
	const EShipRoom Via = UShipInteriorComponent::LiftStop(R->Id);
	if (Via != R->Id)
	{
		Out.Add(R->Lift.GetLocation());   // the room's own door onto the corridor
		if (const FShipRoom* C = I->Room(Via))
		{
			if (R->Id == EShipRoom::Transporter && C->Legs.Num()) { Out.Add(FVector(C->Center.X, (C->Legs[0].Min.Y + C->Legs[0].Max.Y) * 0.5, C->Lift.GetLocation().Z)); }
			Out.Add(C->Lift.GetLocation());
		}
	}
	else
	{
		if (R->Id == EShipRoom::Corridor && R->Legs.Num() && From.X > R->Center.X + 200.0) { Out.Add(FVector(R->Center.X, (R->Legs[0].Min.Y + R->Legs[0].Max.Y) * 0.5, R->Lift.GetLocation().Z)); }
		Out.Add(R->Lift.GetLocation());
	}
}

void UCrewWalkersComponent::SetEntry(int32 Place, const FVector& To, TArray<FVector>& Out) const
{
	TArray<FVector> Back;
	SetExit(Place, To, Back);
	for (int32 i = Back.Num() - 1; i >= 0; --i) { Out.Add(Back[i]); }
}

bool UCrewWalkersComponent::PlanRoute(FCrewWalker& W, int32 ToPlace)
{
	UShipInteriorComponent* I = Interior();
	if (!I || !Router) { return false; }
	const FShipPlan& P = I->GetPlan();
	const FShipRoom* A = I->Place(W.Place);
	const FShipRoom* B = I->Place(ToPlace);
	W.bMoving = false; W.bInLift = false; W.Route.Legs.Reset(); W.Leg = 0; W.Point = 0; W.LegTime = 0.0;
	if (!B) { W.Place = INDEX_NONE; W.Target = INDEX_NONE; W.bOffstage = true; return false; }
	double SpotYaw = 0.0;
	const FVector Spot = SpotIn(ToPlace, W.Member, SpotYaw);
	W.bOffstage = false;
	auto Arrive = [&]() { W.Place = ToPlace; W.Target = ToPlace; W.Pos = Spot; W.Spot = Spot; W.Yaw = SpotYaw; W.SpotYaw = SpotYaw; };
	if (!A || W.Place == ToPlace) { Arrive(); return true; }   // from nowhere, or a new spot in the same room
	// The ends: a set rides from its own door; a plan room is left and entered by its doorway.
	const int32 FromStop = A->IsSignature() ? StopOfPlace(W.Place) : INDEX_NONE;
	const int32 ToStop = B->IsSignature() ? StopOfPlace(ToPlace) : INDEX_NONE;
	const int32 FromDeck = DeckOfPlace(W.Place), ToDeck = DeckOfPlace(ToPlace);
	if (FromDeck == INDEX_NONE || ToDeck == INDEX_NONE || (A->IsSignature() && FromStop == INDEX_NONE) || (B->IsSignature() && ToStop == INDEX_NONE)) { Arrive(); return false; }
	TArray<FVector> Pre, Post;
	FVector From, To;
	if (A->IsSignature()) { SetExit(W.Place, W.Pos, Pre); From = Pre.Last(); }
	else { From = P.Rooms.IsValidIndex(A->PlanRoom) ? P.Rooms[A->PlanRoom].Door : W.Pos; Pre = { W.Pos, From }; }
	if (B->IsSignature()) { SetEntry(ToPlace, Spot, Post); To = Post[0]; }
	else { To = P.Rooms.IsValidIndex(B->PlanRoom) ? P.Rooms[B->PlanRoom].Door : Spot; Post = { To, Spot }; }
	FPlanRoute R;
	if (!Router->Route(FromDeck, From, FromStop, ToDeck, To, ToStop, R) || R.Legs.Num() == 0) { Arrive(); return false; }
	// The way through the rooms at either end joins the first and last legs on foot (or stands as legs of its
	// own when the route begins or ends in a lift: a set's door).
	if (R.Legs[0].bLift) { FPlanRouteLeg L; L.Points = Pre; R.Legs.Insert(L, 0); }
	else { Pre.Pop(EAllowShrinking::No); R.Legs[0].Points.Insert(Pre, 0); }
	if (R.Legs.Last().bLift) { FPlanRouteLeg L; L.Points = Post; R.Legs.Add(L); }
	else { Post.RemoveAt(0); R.Legs.Last().Points.Append(Post); }
	W.Route = MoveTemp(R); W.Target = ToPlace; W.Spot = Spot; W.SpotYaw = SpotYaw; W.Leg = 0; W.Point = 1; W.bMoving = true;
	return true;
}

void UCrewWalkersComponent::Advance(FCrewWalker& W, float Dt)
{
	if (!W.bMoving || !W.Route.Legs.IsValidIndex(W.Leg)) { W.bMoving = false; return; }
	auto Arrived = [&]() { W.bMoving = false; W.bInLift = false; W.Place = W.Target; W.Pos = W.Spot; W.Yaw = W.SpotYaw; W.Route.Legs.Reset(); };
	auto NextLeg = [&]()
	{
		++W.Leg; W.Point = 1; W.LegTime = 0.0;
		if (!W.Route.Legs.IsValidIndex(W.Leg)) { Arrived(); return; }
		const FPlanRouteLeg& N = W.Route.Legs[W.Leg];
		if (!N.bLift && N.Points.Num()) { W.Pos = N.Points[0]; }
	};
	const FPlanRouteLeg& L = W.Route.Legs[W.Leg];
	if (L.bLift)
	{
		W.bInLift = true;
		W.LegTime += Dt;
		if (W.LegTime >= L.Seconds) { W.bInLift = false; NextLeg(); }
		return;
	}
	double Step = WalkSpeedCm * Dt;
	while (Step > 0.0 && W.Point < L.Points.Num())
	{
		const FVector T = L.Points[W.Point];
		const FVector D = T - W.Pos;
		const double Dist = D.Size();
		if (Dist <= Step) { W.Pos = T; Step -= Dist; ++W.Point; }
		else { W.Pos += D / Dist * Step; if (Dist > 1.0) { W.Yaw = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)); } Step = 0.0; }
	}
	if (W.Point >= L.Points.Num()) { NextLeg(); }
}

void UCrewWalkersComponent::Sync(bool bAll)
{
	UCrewRosterSubsystem* R = Roster();
	if (!R) { return; }
	const TArray<FCrewMember>& Roster = R->GetRoster();
	TBitArray<> Seen(false, Walkers.Num());
	for (const FCrewMember& M : Roster)
	{
		int32* Idx = ByMember.Find(M.Id);
		if (!Idx)
		{
			FCrewWalker W; W.Member = M.Id;
			// A face of their own: the skin by a hash of who they are, the hair likewise.
			FRandomStream Rs(M.Id * 48271 + 7);
			const float T = Rs.FRand();
			W.Skin = FMath::Lerp(FLinearColor(0.86f, 0.68f, 0.56f), FLinearColor(0.32f, 0.2f, 0.14f), T * T);
			W.Hair = Rs.FRand() < 0.2f ? FLinearColor(0.55f, 0.5f, 0.42f) : FLinearColor(0.09f, 0.06f, 0.045f);
			Idx = &ByMember.Add(M.Id, Walkers.Add(W));
			Seen.Add(false);
		}
		Seen[*Idx] = true;
		FCrewWalker& W = Walkers[*Idx];
		if (W.Where != M.Where) { W.Where = M.Where; W.bReplan = true; }
	}
	// Those gone from the roster (dead, transferred) leave the ship.
	for (int32 i = 0; i < Walkers.Num(); ++i) { if (!Seen[i] && !Walkers[i].bOffstage) { Release(Walkers[i]); Walkers[i].bOffstage = true; Walkers[i].bMoving = false; Walkers[i].Place = INDEX_NONE; } }
	// New places: four dozen routes a sync (a watch change moves a third of the ship in seconds), all when settling.
	int32 Budget = bAll ? INT32_MAX : 48;
	for (FCrewWalker& W : Walkers)
	{
		if (!W.bReplan || Budget <= 0) { continue; }
		W.bReplan = false; --Budget;
		const int32 P = PlaceFor(W.Where, W.Member);
		if (P == INDEX_NONE) { Release(W); W.bOffstage = true; W.bMoving = false; W.bInLift = false; W.Place = INDEX_NONE; W.Target = INDEX_NONE; continue; }
		if ((W.bMoving && P == W.Target) || (!W.bMoving && P == W.Place)) { continue; }
		if (bAll || W.Place == INDEX_NONE) { W.Place = INDEX_NONE; PlanRoute(W, P); }   // settle: straight there
		else { if (W.bMoving) { W.Place = W.bInLift ? W.Target : W.Place; } PlanRoute(W, P); }
	}
	if (bAll)
	{
		int32 Off = 0; TMap<FString, int32> Nowhere;
		for (const FCrewWalker& W : Walkers) { if (W.bOffstage) { ++Off; ++Nowhere.FindOrAdd(W.Where); } }
		FString Where; for (const TPair<FString, int32>& N : Nowhere) { Where += FString::Printf(TEXT(" %s %d;"), *N.Key, N.Value); }
		UE_LOG(LogTemp, Log, TEXT("Crew walkers: %d settled, %d off the plan:%s"), Walkers.Num() - Off, Off, *Where);
	}
}

bool UCrewWalkersComponent::Rendered(const FCrewWalker& W) const
{
	if (W.bOffstage || W.bInLift) { return false; }
	// The bridge has its seated officers already: nobody is drawn standing in it.
	UShipInteriorComponent* I = Interior();
	if (!W.bMoving && I && W.Place == I->IndexOf(EShipRoom::Bridge)) { return false; }
	return true;
}

ACrewFigure* UCrewWalkersComponent::Acquire(FCrewWalker& W)
{
	if (W.Figure.IsValid()) { return W.Figure.Get(); }
	ACrewFigure* F = nullptr;
	for (ACrewFigure* C : Pool) { if (C && C->Member == INDEX_NONE) { F = C; break; } }
	if (!F)
	{
		if (Pool.Num() >= MaxFigures || !GetWorld()) { return nullptr; }
		FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		F = GetWorld()->SpawnActor<ACrewFigure>(ACrewFigure::StaticClass(), FTransform::Identity, SP);
		if (!F) { return nullptr; }
		Pool.Add(F);
	}
	F->Setup(FigureMesh.LoadSynchronous(), LocomotionBlend.LoadSynchronous(), CrewMaterial.LoadSynchronous(), FigureBaseMap.LoadSynchronous(), FigureNormalMap.LoadSynchronous(), Jacket, W.Skin, W.Hair);
	F->Member = W.Member;
	F->SetActorHiddenInGame(false);
	W.Figure = F;
	return F;
}

void UCrewWalkersComponent::Release(FCrewWalker& W)
{
	if (ACrewFigure* F = W.Figure.Get()) { F->Member = INDEX_NONE; F->SetActorHiddenInGame(true); }
	W.Figure = nullptr;
}

void UCrewWalkersComponent::UpdateFigures()
{
	AStarshipPawn* S = Ship();
	APersonPawn* P = S ? S->GetActivePerson() : nullptr;
	const bool bAboard = bFigures && P && P->IsAboard();
	if (!bAboard)
	{
		for (FCrewWalker& W : Walkers) { if (W.Figure.IsValid()) { Release(W); } }
		return;
	}
	const FVector Feet = P->FeetInCraft();
	// The walkers that can be seen get the figures, nearest first: those on the move (in the passages), and those
	// standing in the player's own place or a room off it (seen through its doorway). Someone standing in a
	// cabin three walls away would spend a figure on nothing, and a watch change has dozens passing by.
	UShipInteriorComponent* I = Interior();
	const int32 Here = P->GetPlace();
	const FShipRoom* HereRoom = I ? I->Place(Here) : nullptr;
	TArray<TPair<double, int32>> Near;
	for (int32 i = 0; i < Walkers.Num(); ++i)
	{
		const FCrewWalker& W = Walkers[i];
		if (!Rendered(W) || FMath::Abs(W.Pos.Z - Feet.Z) > 150.0) { continue; }
		const bool bSeen = W.bMoving || W.Place == Here || (HereRoom && HereRoom->Neighbours.Contains(W.Place));
		if (!bSeen) { continue; }
		const double D2 = FVector::DistSquared2D(W.Pos, Feet);
		if (D2 < FMath::Square((double)ShowRadiusCm)) { Near.Add(TPair<double, int32>(W.bMoving ? D2 : D2 + 1e9, i)); }
	}
	Near.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
	TBitArray<> Keep(false, Walkers.Num());
	for (int32 k = 0; k < Near.Num() && k < MaxFigures; ++k) { Keep[Near[k].Value] = true; }
	for (int32 i = 0; i < Walkers.Num(); ++i) { if (!Keep[i] && Walkers[i].Figure.IsValid()) { Release(Walkers[i]); } }
	const FTransform Craft = S->CraftFrame();
	for (int32 i = 0; i < Walkers.Num(); ++i)
	{
		if (!Keep[i]) { continue; }
		FCrewWalker& W = Walkers[i];
		if (ACrewFigure* F = Acquire(W))
		{
			F->Pose(FTransform(FRotator(0.f, (float)W.Yaw, 0.f), W.Pos) * Craft, W.bMoving ? WalkSpeedCm : 0.f);
		}
	}
}

FString UCrewWalkersComponent::Describe(const FVector& Feet, int32 MaxLines) const
{
	UCrewRosterSubsystem* R = Roster();
	UShipInteriorComponent* I = Interior();
	TArray<TPair<double, int32>> Near;
	for (int32 i = 0; i < Walkers.Num(); ++i) { if (!Walkers[i].bOffstage) { Near.Add(TPair<double, int32>(FVector::Dist(Walkers[i].Pos, Feet), i)); } }
	Near.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
	FString Out;
	for (int32 k = 0; k < Near.Num() && k < MaxLines; ++k)
	{
		const FCrewWalker& W = Walkers[Near[k].Value];
		const FCrewMember* M = R ? R->Find(W.Member) : nullptr;
		Out += FString::Printf(TEXT("\n   %3d %-24s %5.0f m %s %s%s -> %s (%s)"), W.Member, M ? *M->FullName() : TEXT("?"), Near[k].Key / 100.0, W.Figure.IsValid() ? TEXT("*") : TEXT(" "),
			W.bInLift ? TEXT("in a lift ") : W.bMoving ? TEXT("walking ") : TEXT(""), I ? *I->NameOf(W.Place) : TEXT("?"), W.bMoving && I ? *I->NameOf(W.Target) : TEXT("-"), *W.Where);
	}
	return Out;
}

FString UCrewWalkersComponent::DescribeRoute(int32 Member) const
{
	const int32* Idx = ByMember.Find(Member);
	if (!Idx) { return TEXT("no such walker"); }
	const FCrewWalker& W = Walkers[*Idx];
	UShipInteriorComponent* I = Interior();
	FString Out = FString::Printf(TEXT("%s: at (%.0f, %.0f, %.0f) in %s, for %s; %s"), *W.Where, W.Pos.X, W.Pos.Y, W.Pos.Z, I ? *I->NameOf(W.Place) : TEXT("?"), I ? *I->NameOf(W.Target) : TEXT("?"), W.bOffstage ? TEXT("off the plan") : W.bInLift ? TEXT("in a lift") : W.bMoving ? TEXT("walking") : TEXT("standing"));
	for (int32 l = 0; l < W.Route.Legs.Num(); ++l)
	{
		const FPlanRouteLeg& L = W.Route.Legs[l];
		if (L.bLift) { Out += FString::Printf(TEXT("\n   leg %d: lift stop %d -> %d, %.1f s"), l, L.FromStop, L.ToStop, L.Seconds); }
		else
		{
			double D = 0.0; for (int32 i = 1; i < L.Points.Num(); ++i) { D += FVector::Dist2D(L.Points[i - 1], L.Points[i]); }
			Out += FString::Printf(TEXT("\n   leg %d: %d points, %.0f m on foot%s"), l, L.Points.Num(), D / 100.0, l == W.Leg ? *FString::Printf(TEXT(" (at point %d)"), W.Point) : TEXT(""));
		}
	}
	return Out;
}
