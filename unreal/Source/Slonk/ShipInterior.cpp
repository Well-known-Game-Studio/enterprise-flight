// Slonk - the walkable places of the ship (see ShipInterior.h).
#include "ShipInterior.h"
#include "StarshipPawn.h"
#include "PersonPawn.h"
#include "SlonkMaterials.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	FTransform InteriorSpot(const FVector& Feet, const FVector& Facing)
	{
		return FTransform(FRotationMatrix::MakeFromXZ(Facing.GetSafeNormal(), FVector::UpVector).ToQuat(), Feet);
	}

}

/** A frame along a bay (or any run): origin on the floor, X along the run, Y to its right, Z up. Boxes are placed
 *  by (along, across, up) offsets and sized in the frame; a canted panel's roll comes from its own up. */
struct FRunFrameRef
{
	FVector O, D, S;
	FQuat Q;
	FRunFrameRef(const FVector& Origin, const FVector& Dir) : O(Origin), D(Dir.GetSafeNormal2D()), S(FVector(-D.Y, D.X, 0.0)), Q(FRotationMatrix::MakeFromXZ(D, FVector::UpVector).ToQuat()) {}
	FVector At(double A, double U, double Z) const { return O + D * A + S * U + FVector(0, 0, Z); }
	/** The rotation of a box whose local up leans across the run: (0, Across, Up) in the frame. */
	FQuat Lean(double Across, double Up) const { return Q * FRotationMatrix::MakeFromXZ(FVector::ForwardVector, FVector(0.0, Across, Up).GetSafeNormal()).ToQuat(); }
};


UShipInteriorComponent::UShipInteriorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CubeFinder.Succeeded()) { CubeMesh = CubeFinder.Object; }
	if (CylFinder.Succeeded()) { CylinderMesh = CylFinder.Object; }
	LitMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_BridgeLit.M_BridgeLit")));
	ButtonsMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_BridgeButtons.M_BridgeButtons")));
	OkudagramMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_Okudagram.M_Okudagram")));
	GlassMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_Glass.M_Glass")));
	EngineeringMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Slonk/Rooms/SM_RoomEngineering.SM_RoomEngineering")));
	SickbayMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Slonk/Rooms/SM_RoomSickbay.SM_RoomSickbay")));
	ShuttleBayMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Slonk/Rooms/SM_RoomShuttleBay.SM_RoomShuttleBay")));
	BayShuttleMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Slonk/Rooms/SM_RoomBayShuttle.SM_RoomBayShuttle")));
	TransporterMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Slonk/Rooms/SM_RoomTransporter.SM_RoomTransporter")));
	AirlockMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Slonk/Rooms/SM_RoomAirlock.SM_RoomAirlock")));
	CorridorMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Slonk/Rooms/SM_RoomCorridorG.SM_RoomCorridorG")));
	RecDeckMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Slonk/Rooms/SM_RoomRecDeck.SM_RoomRecDeck")));
	MessMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Slonk/Rooms/SM_RoomMess.SM_RoomMess")));
	QuartersMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Slonk/Rooms/SM_RoomCabin.SM_RoomCabin")));
	CrewQuartersMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Slonk/Rooms/SM_RoomCrewQuarters.SM_RoomCrewQuarters")));
	TorpedoBayMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Slonk/Rooms/SM_RoomTorpedoBay.SM_RoomTorpedoBay")));
	CargoBayMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Slonk/Rooms/SM_RoomCargoBay.SM_RoomCargoBay")));
}

void UShipInteriorComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!IsBuilt()) { Build(); }
}

void UShipInteriorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickRide(DeltaTime);
}

FString UShipInteriorComponent::RoomName(EShipRoom Which)
{
	switch (Which)
	{
	case EShipRoom::Bridge: return TEXT("Main bridge");
	case EShipRoom::Engineering: return TEXT("Main engineering");
	case EShipRoom::Sickbay: return TEXT("Sickbay");
	case EShipRoom::ShuttleBay: return TEXT("Shuttle bay");
	case EShipRoom::Transporter: return TEXT("Transporter room");
	case EShipRoom::Airlock: return TEXT("Airlock");
	case EShipRoom::Corridor: return TEXT("Deck G corridor");
	case EShipRoom::RecDeck: return TEXT("Rec deck");
	case EShipRoom::Mess: return TEXT("Officers' mess");
	case EShipRoom::Quarters: return TEXT("Officers' quarters");
	case EShipRoom::CrewQuarters: return TEXT("Crew quarters");
	case EShipRoom::TorpedoBay: return TEXT("Torpedo bay");
	case EShipRoom::CargoBay: return TEXT("Cargo bays");
	default: return SlonkPlan::KindName(Which);
	}
}

FString UShipInteriorComponent::DeckOf(EShipRoom Which)
{
	switch (Which)
	{
	case EShipRoom::Bridge: return TEXT("A");
	case EShipRoom::Airlock: return TEXT("B");
	case EShipRoom::Mess: return TEXT("C");
	case EShipRoom::Quarters: return TEXT("D");
	case EShipRoom::CrewQuarters: return TEXT("F");
	case EShipRoom::Sickbay: return TEXT("G");
	case EShipRoom::Transporter: return TEXT("G");
	case EShipRoom::Corridor: return TEXT("G");
	case EShipRoom::RecDeck: return TEXT("G");
	case EShipRoom::TorpedoBay: return TEXT("M");
	case EShipRoom::Engineering: return TEXT("O");
	case EShipRoom::ShuttleBay: return TEXT("Q");
	case EShipRoom::CargoBay: return TEXT("S");
	default: return TEXT("");
	}
}

EShipRoom UShipInteriorComponent::LiftStop(EShipRoom Which)
{
	switch (Which)
	{
	case EShipRoom::Sickbay: case EShipRoom::Transporter: case EShipRoom::RecDeck: return EShipRoom::Corridor;
	default: return Which;
	}
}

const FShipRoom* UShipInteriorComponent::Room(EShipRoom Which) const
{
	for (const FShipRoom& R : Rooms) { if (R.Id == Which) { return &R; } }
	return nullptr;
}

int32 UShipInteriorComponent::IndexOf(EShipRoom Which) const
{
	for (const FShipRoom& R : Rooms) { if (R.Id == Which) { return R.Index; } }
	return INDEX_NONE;
}

bool UShipInteriorComponent::InRoom(const FShipRoom& R, const FVector& P, double Tol) const
{
	if (R.Stretch != INDEX_NONE && R.PlanRoom == INDEX_NONE && R.Id != EShipRoom::Turbolift && Plan.Stretches.IsValidIndex(R.Stretch))
	{
		// A passage: its bays as oriented rectangles, the section's waist and a hand below the floor.
		using namespace SlonkPlan;
		const FPlanStretch& S = Plan.Stretches[R.Stretch];
		for (const FPlanBay& B : S.Bays)
		{
			const FVector D = B.Dir(); const FVector L = P - B.P0;
			const double Along = L.X * D.X + L.Y * D.Y, Across = FMath::Abs(-L.X * D.Y + L.Y * D.X);
			if (Along >= -Tol && Along <= B.Length() + Tol && Across <= CorrW + 12.0 + Tol && L.Z >= -40.0 - Tol && L.Z <= CorrH + Tol) { return true; }
		}
		return false;
	}
	if (!R.Half.IsNearlyZero())
	{
		FVector L = P - R.Center;
		if (R.Yaw != 0.0) { L = L.RotateAngleAxisRad(-R.Yaw, FVector::UpVector); }
		const FVector D = L.GetAbs();
		if (D.X <= R.Half.X + Tol && D.Y <= R.Half.Y + Tol && D.Z <= R.Half.Z + Tol) { return true; }
	}
	for (const FBox& B : R.Legs) { if (B.ExpandBy(Tol).IsInside(P)) { return true; } }
	return false;
}

int32 UShipInteriorComponent::RoomAt(const FVector& P, int32 Current) const
{
	if (const FShipRoom* C = Place(Current)) { if (InRoom(*C, P, 30.0)) { return Current; } }   // a wall's thickness and a step: through a doorway the room beyond takes over
	// The place the point is deepest in (the smallest tolerance that holds it), so a corridor's doorway counts as
	// the room beyond once the walker is through it rather than at the first touch of the box.
	for (double Tol : { 0.0, 50.0, 100.0 })
	{
		for (const FShipRoom& R : Rooms) { if (R.Id != EShipRoom::Bridge && InRoom(R, P, Tol)) { return R.Index; } }
	}
	return INDEX_NONE;
}

void UShipInteriorComponent::SetRoomSpots(EShipRoom Which, const FTransform& Lift, const FTransform& Use, const FString& UseLabel)
{
	for (FShipRoom& R : Rooms)
	{
		if (R.Id != Which) { continue; }
		R.Lift = Lift; R.Use = Use; R.UseLabel = UseLabel;
		// The set's lift door is the room's stop: the car stands behind it.
		if (Plan.Stops.IsValidIndex(R.Stop))
		{
			FPlanStop& S = Plan.Stops[R.Stop];
			const FVector Fwd = Lift.GetRotation().GetForwardVector();
			S.Car = Lift.GetLocation() - Fwd * 230.0; S.Facing = Fwd;
		}
	}
}

// ---- Materials and pieces ---------------------------------------------------------------------

UMaterialInstanceDynamic* UShipInteriorComponent::MakeLit(const TCHAR* Name, FLinearColor Base, float Roughness, float Metallic, FLinearColor Emissive, float EmissiveStrength)
{
	UMaterialInterface* Parent = SlonkMaterials::LoadOrFallback(LitMaterial, TEXT("BridgeLit"), true);
	if (!Parent) { return nullptr; }
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, this, *FString::Printf(TEXT("Interior_%s"), Name));
	Mid->SetVectorParameterValue(TEXT("BaseColor"), Base);
	Mid->SetScalarParameterValue(TEXT("Roughness"), Roughness);
	Mid->SetScalarParameterValue(TEXT("Metallic"), Metallic);
	Mid->SetVectorParameterValue(TEXT("EmissiveColor"), Emissive);
	Mid->SetScalarParameterValue(TEXT("EmissiveStrength"), EmissiveStrength);
	Materials.Add(Mid);
	return Mid;
}

UMaterialInstanceDynamic* UShipInteriorComponent::MakeScreen(const TCHAR* Name, float Seed, float Intensity)
{
	UMaterialInterface* Parent = SlonkMaterials::LoadOrFallback(ButtonsMaterial, TEXT("BridgeButtons"));
	if (!Parent) { return nullptr; }
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, this, *FString::Printf(TEXT("Interior_%s_%d"), Name, PieceCount));
	Mid->SetScalarParameterValue(TEXT("Seed"), Seed);
	Mid->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	Mid->SetScalarParameterValue(TEXT("CellScale"), 0.7f);
	Materials.Add(Mid);
	return Mid;
}

UMaterialInstanceDynamic* UShipInteriorComponent::MakeOkuda(const TCHAR* TextureName, float Intensity)
{
	UMaterialInterface* Parent = OkudagramMaterial.LoadSynchronous();
	UTexture* Tex = LoadObject<UTexture>(nullptr, *FString::Printf(TEXT("/Game/Slonk/Bridge/T_Okuda_%s.T_Okuda_%s"), TextureName, TextureName));
	if (!Parent || !Tex) { return nullptr; }
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Parent, this, *FString::Printf(TEXT("Interior_Okuda_%s_%d"), TextureName, PieceCount++));
	M->SetTextureParameterValue(TEXT("Screen"), Tex);
	M->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	M->SetScalarParameterValue(TEXT("UVMode"), 0.f);
	Materials.Add(M);
	return M;
}

UStaticMeshComponent* UShipInteriorComponent::RoomMesh(const TSoftObjectPtr<UStaticMesh>& Asset, const FVector& FloorCenter)
{
	if (!GetOwner()) { return nullptr; }
	UStaticMesh* Mesh = Asset.LoadSynchronous();
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("Interior: %s missing, building the primitive room instead."), *Asset.ToString());
		return nullptr;
	}
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(GetOwner(), *FString::Printf(TEXT("Interior_%s"), *Mesh->GetName()));
	C->SetupAttachment(this);
	C->SetStaticMesh(Mesh);
	C->SetMobility(EComponentMobility::Movable);
	C->SetRelativeLocation(FloorCenter);
	C->SetGenerateOverlapEvents(false);
	C->bAffectDistanceFieldLighting = false;
	C->SetReceivesDecals(false);
	// The mesh's own triangles (imported as complex-as-simple) are the walls, floors and furniture the walker meets.
	C->SetCollisionObjectType(ECC_WorldDynamic);
	C->SetCollisionResponseToAllChannels(ECR_Ignore);
	C->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetVisibility(false);
	C->RegisterComponent();
	BindSlots(C);
	AllPieces.Add(C);
	if (Building)
	{
		Building->Pieces.Add(C);
		Building->Solids.Add(C);
	}
	return C;
}

void UShipInteriorComponent::BindSlots(UStaticMeshComponent* C)
{
	// Slot names are the Blender material names: screens take the Okudagram named after them, the lit slots get
	// emissive instances, the glass its translucent material; everything else keeps the imported colour.
	UMaterialInterface* Glass = GlassMaterial.LoadSynchronous();
	UMaterialInterface* TouchMesh = MakeScreen(TEXT("MeshTouch"), 7.f, 1.2f);
	const TArray<FName> Slots = C->GetMaterialSlotNames();
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FString Name = Slots[i].ToString();
		UMaterialInterface* M = nullptr;
		if (Name.StartsWith(TEXT("Screen_"))) { M = MakeOkuda(*Name.RightChop(7), ScreenIntensity); }
		else if (Name == TEXT("LightStrip")) { M = StripWhite; }
		else if (Name == TEXT("LightSoft")) { M = MakeLit(TEXT("MeshSoft"), FLinearColor(0.9f, 0.92f, 0.95f), 0.8f, 0.f, FLinearColor(0.95f, 0.97f, 1.0f), 2.4f); }
		else if (Name == TEXT("CoreGlow")) { M = CoreBlue; }
		else if (Name == TEXT("PadGlow")) { M = PadGlow; }
		else if (Name == TEXT("LatticeLit")) { M = MakeLit(TEXT("MeshLattice"), FLinearColor(0.9f, 0.8f, 0.6f), 0.6f, 0.f, FLinearColor(1.0f, 0.85f, 0.6f), 2.2f); }
		else if (Name == TEXT("StripBlue")) { M = MakeLit(TEXT("MeshStripBlue"), FLinearColor(0.2f, 0.4f, 0.9f), 0.6f, 0.f, FLinearColor(0.2f, 0.5f, 1.0f), 5.f); }
		else if (Name == TEXT("VentGlow")) { M = MakeLit(TEXT("MeshVent"), FLinearColor(0.5f, 0.2f, 0.05f), 0.5f, 0.f, FLinearColor(1.0f, 0.45f, 0.15f), 4.f); }
		else if (Name == TEXT("WarnRed")) { M = MakeLit(TEXT("MeshWarn"), FLinearColor(0.5f, 0.05f, 0.03f), 0.5f, 0.f, FLinearColor(1.0f, 0.1f, 0.05f), 4.f); }
		else if (Name == TEXT("TouchPanel")) { M = TouchMesh; }
		else if (Name == TEXT("GlassPanel")) { M = Glass; }
		if (M) { C->SetMaterial(i, M); }
	}
}

void UShipInteriorComponent::SetPadGlow(float Pulse)
{
	if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(PadGlow)) { M->SetScalarParameterValue(TEXT("EmissiveStrength"), 3.f + 10.f * FMath::Clamp(Pulse, 0.f, 1.f)); }
}

void UShipInteriorComponent::BuildAirlock(FShipRoom& R)
{
	const FVector C = R.Center, H = R.Half;
	const double Floor = C.Z - H.Z;
	const FVector Door(C.X - H.X, C.Y, Floor);
	R.Lift = InteriorSpot(Door + FVector(90.0, 0, 0), FVector(1, 0, 0));
	R.Use = InteriorSpot(FVector(C.X + H.X - 110.0, C.Y, Floor), FVector(1, 0, 0));
	R.UseLabel = TEXT("open the airlock");
	if (RoomMesh(AirlockMesh, FVector(C.X, C.Y, Floor)))
	{
		for (double Dx : { -120.0, 120.0 }) { Light(FVector(C.X + Dx, C.Y, C.Z + H.Z - 20.0), 40.f, 700.f, FLinearColor(0.97f, 0.98f, 1.f)); }
		return;
	}
	Shell(R, Door, FVector(1, 0, 0), false);
	// The inner hatch: a round door in the forward bulkhead with its lit ring and a lamp over it.
	const FVector Hatch(C.X + H.X - 10.0, C.Y, Floor + 125.0);
	Piece(CylinderMesh, Hatch, FVector(200.0, 200.0, 16.0), FRotator(90.f, 0.f, 0.f), PanelDark, false);
	Piece(CylinderMesh, Hatch + FVector(-6.0, 0, 0), FVector(176.0, 176.0, 6.0), FRotator(90.f, 0.f, 0.f), StripWhite, false);
	Piece(CylinderMesh, Hatch + FVector(-9.0, 0, 0), FVector(160.0, 160.0, 6.0), FRotator(90.f, 0.f, 0.f), HullGrey, false);
	Box(Hatch + FVector(-14.0, 0, 0), FVector(4.0, 30.0, 8.0), PanelDark, 0.f, false);
	Box(Hatch + FVector(-14.0, 0, 0), FVector(4.0, 8.0, 30.0), PanelDark, 0.f, false);
	Box(FVector(C.X + H.X - 8.0, C.Y + 140.0, Floor + 130.0), FVector(6.0, 36.0, 30.0), ConsoleBlack, 0.f, false);
	Box(FVector(C.X + H.X - 11.5, C.Y + 140.0, Floor + 130.0), FVector(2.0, 30.0, 24.0), Teal, 0.f, false);
	// Suit lockers along the port wall, a bench and the arms cabinet to starboard.
	for (int32 i = 0; i < 4; ++i)
	{
		const double Lx = C.X - 180.0 + i * 90.0;
		Box(FVector(Lx, C.Y - H.Y + 30.0, Floor + 112.0), FVector(80.0, 60.0, 224.0), PanelGrey);
		Box(FVector(Lx, C.Y - H.Y + 61.0, Floor + 118.0), FVector(70.0, 2.0, 190.0), PanelDark, 0.f, false);
		Box(FVector(Lx, C.Y - H.Y + 62.0, Floor + 140.0), FVector(40.0, 2.0, 70.0), HullGrey, 0.f, false);
	}
	Box(FVector(C.X - 80.0, C.Y + H.Y - 25.0, Floor + 22.0), FVector(220.0, 46.0, 44.0), PanelDark);
	Box(FVector(C.X + 160.0, C.Y + H.Y - 22.0, Floor + 120.0), FVector(120.0, 44.0, 170.0), PanelDark);
	Box(FVector(C.X + 160.0, C.Y + H.Y - 45.0, Floor + 150.0), FVector(40.0, 2.0, 30.0), Teal, 0.f, false);
	for (double Dx : { -120.0, 120.0 }) { Light(FVector(C.X + Dx, C.Y, C.Z + H.Z - 20.0), 40.f, 700.f, FLinearColor(0.97f, 0.98f, 1.f)); }
}

void UShipInteriorComponent::SetBayShuttleShown(bool bShown)
{
	bBayShuttleShown = bShown;
	const bool bIn = ShownKind() == EShipRoom::ShuttleBay;
	const bool bOn = bShown && (bIn || bBayFromOutside);
	auto Apply = [bOn, bIn, bShown](UPrimitiveComponent* S)
	{
		S->SetVisibility(bOn);
		// Pieces that never block keep NoCollision either way; solid ones block only for someone in the bay.
		if (S->GetCollisionResponseToChannel(ECC_GameTraceChannel1) == ECR_Block) { S->SetCollisionEnabled(bShown && bIn ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision); }
	};
	if (UStaticMeshComponent* S = BayShuttle.Get()) { Apply(S); }
	for (const TWeakObjectPtr<UPrimitiveComponent>& P : BayShuttlePieces) { if (UPrimitiveComponent* S = P.Get()) { Apply(S); } }
}

void UShipInteriorComponent::PrimitiveShuttle(const FVector& S)
{
	// Hull on its skids, nacelles, canopy, the port door: the pieces are remembered so the launch can hide them.
	const int32 First = AllPieces.Num();
	for (double Side : { -1.0, 1.0 }) { Box(S + FVector(0, Side * 140.0, 20.0), FVector(520.0, 30.0, 40.0), PanelDark); }
	Box(S + FVector(0, 0, 40.0 + 95.0), FVector(900.0, 300.0, 190.0), HullGrey);
	Box(S + FVector(370.0, 0, 40.0 + 150.0), FVector(170.0, 240.0, 70.0), PanelDark, 0.f, false);
	for (double Side : { -1.0, 1.0 })
	{
		Box(S + FVector(-60.0, Side * 200.0, 60.0), FVector(640.0, 72.0, 72.0), HullGrey);
		Box(S + FVector(-60.0, Side * 200.0, 60.0 + 20.0), FVector(300.0, 74.0, 14.0), MakeLit(TEXT("NacelleGlow"), FLinearColor(0.2f, 0.3f, 0.6f), 0.5f, 0.f, FLinearColor(0.3f, 0.5f, 1.f), 1.5f), 0.f, false);
	}
	Box(S + FVector(-20.0, -151.0, 40.0 + 100.0), FVector(90.0, 2.0, 150.0), PanelDark, 0.f, false); // the port door
	for (int32 i = First; i < AllPieces.Num(); ++i)
	{
		if (UPrimitiveComponent* P = Cast<UPrimitiveComponent>(AllPieces[i])) { BayShuttlePieces.Add(P); }
	}
}

UStaticMeshComponent* UShipInteriorComponent::Piece(UStaticMesh* Mesh, const FVector& Center, const FVector& Size, const FRotator& Rot, UMaterialInterface* Mat, bool bSolid, USceneComponent* Parent)
{
	if (!Mesh || !GetOwner()) { return nullptr; }
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(GetOwner(), *FString::Printf(TEXT("Interior_%d"), PieceCount++));
	C->SetupAttachment(Parent ? Parent : this);
	C->SetStaticMesh(Mesh);
	if (Mat) { C->SetMaterial(0, Mat); }
	C->SetMobility(EComponentMobility::Movable);
	C->SetRelativeLocationAndRotation(Center, Rot);
	C->SetRelativeScale3D(Size / 100.0);
	C->SetGenerateOverlapEvents(false);
	C->bAffectDistanceFieldLighting = false;
	C->SetReceivesDecals(false);
	// Solid only to the Boots channel, and only while the room is shown (ShowRoom turns the query on).
	C->SetCollisionObjectType(ECC_WorldDynamic);
	C->SetCollisionResponseToAllChannels(ECR_Ignore);
	C->SetCollisionResponseToChannel(ECC_GameTraceChannel1, bSolid ? ECR_Block : ECR_Ignore);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetVisibility(false);
	C->RegisterComponent();
	AllPieces.Add(C);
	if (Building)
	{
		Building->Pieces.Add(C);
		if (bSolid) { Building->Solids.Add(C); }
	}
	return C;
}

UPointLightComponent* UShipInteriorComponent::Light(const FVector& Pos, float Intensity, float Radius, FLinearColor Color, USceneComponent* Parent)
{
	if (!GetOwner()) { return nullptr; }
	UPointLightComponent* L = NewObject<UPointLightComponent>(GetOwner(), *FString::Printf(TEXT("InteriorLight_%d"), PieceCount++));
	L->SetupAttachment(Parent ? Parent : this);
	L->SetMobility(EComponentMobility::Movable);
	L->SetRelativeLocation(Pos);
	L->SetIntensityUnits(ELightUnits::Candelas);
	L->SetIntensity(Intensity);
	L->SetAttenuationRadius(Radius);
	L->SetLightColor(Color);
	L->SetCastShadows(false);
	L->SetVisibility(false);
	L->RegisterComponent();
	AllPieces.Add(L);
	if (Building) { Building->Pieces.Add(L); }
	return L;
}

void UShipInteriorComponent::WallRun(bool bAlongX, double Pos, double A0, double A1, double Z0, double Z1, double Thick, UMaterialInterface* Mat, const TArray<double>& Cuts)
{
	// The wall as the pieces round its doorways (150 wide, 240 high), a lintel over each.
	TArray<double> Sorted = Cuts; Sorted.Sort();
	TArray<FVector4> Pieces; double A = A0;
	for (double Cut : Sorted)
	{
		if (Cut - 75.0 > A) { Pieces.Add(FVector4(A, Cut - 75.0, Z0, Z1)); }
		Pieces.Add(FVector4(Cut - 75.0, Cut + 75.0, Z0 + 240.0, Z1));
		A = Cut + 75.0;
	}
	if (A < A1) { Pieces.Add(FVector4(A, A1, Z0, Z1)); }
	for (const FVector4& Pc : Pieces)
	{
		if (Pc.Y - Pc.X < 1.0 || Pc.W - Pc.Z < 1.0) { continue; }
		const double Mid = (Pc.X + Pc.Y) / 2, Len = Pc.Y - Pc.X, Zc = (Pc.Z + Pc.W) / 2, Ht = Pc.W - Pc.Z;
		if (bAlongX) { Box(FVector(Mid, Pos, Zc), FVector(Len, Thick, Ht), Mat); }
		else { Box(FVector(Pos, Mid, Zc), FVector(Thick, Len, Ht), Mat); }
	}
}

void UShipInteriorComponent::Shell(const FShipRoom& R, const FVector& LiftDoor, const FVector& LiftNormal, bool bOpenAft, const TArray<FVector>& Doorways)
{
	const FVector& C = R.Center; const FVector& H = R.Half;
	const double T = 20.0;
	Box(FVector(C.X, C.Y, C.Z - H.Z - T / 2), FVector(2 * H.X + 2 * T, 2 * H.Y + 2 * T, T), R.Id == EShipRoom::ShuttleBay ? DeckPlate : Carpet);
	Box(FVector(C.X, C.Y, C.Z + H.Z + T / 2), FVector(2 * H.X + 2 * T, 2 * H.Y + 2 * T, T), PanelGrey);
	// The doorways on each wall: a point within 30 cm of a wall's plane cuts that wall there.
	TArray<double> Fwd, Aft, Port, Stbd;
	for (const FVector& D : Doorways)
	{
		if (FMath::Abs(D.X - (C.X + H.X)) < 30.0) { Fwd.Add(D.Y); }
		else if (FMath::Abs(D.X - (C.X - H.X)) < 30.0) { Aft.Add(D.Y); }
		else if (FMath::Abs(D.Y - (C.Y - H.Y)) < 30.0) { Port.Add(D.X); }
		else if (FMath::Abs(D.Y - (C.Y + H.Y)) < 30.0) { Stbd.Add(D.X); }
	}
	const double Z0 = C.Z - H.Z, Z1 = C.Z + H.Z;
	WallRun(false, C.X + H.X + T / 2, C.Y - H.Y - T, C.Y + H.Y + T, Z0, Z1, T, PanelGrey, Fwd);
	if (!bOpenAft) { WallRun(false, C.X - H.X - T / 2, C.Y - H.Y - T, C.Y + H.Y + T, Z0, Z1, T, PanelGrey, Aft); }
	WallRun(true, C.Y - H.Y - T / 2, C.X - H.X, C.X + H.X, Z0, Z1, T, PanelGrey, Port);
	WallRun(true, C.Y + H.Y + T / 2, C.X - H.X, C.X + H.X, Z0, Z1, T, PanelGrey, Stbd);
	for (const FVector& D : Doorways)
	{
		const FVector N = FMath::Abs(D.X - (C.X + H.X)) < 30.0 ? FVector(-1, 0, 0) : FMath::Abs(D.X - (C.X - H.X)) < 30.0 ? FVector(1, 0, 0) : FMath::Abs(D.Y - (C.Y - H.Y)) < 30.0 ? FVector(0, 1, 0) : FVector(0, -1, 0);
		DoorFrame(D, N);
	}
	// A dark skirting and a lit cove where the walls meet the ceiling.
	for (double Side : { -1.0, 1.0 })
	{
		Box(FVector(C.X, C.Y + Side * (H.Y - 3.0), C.Z - H.Z + 10.0), FVector(2 * H.X - 2.0, 6.0, 20.0), PanelDark, 0.f, false);
		Box(FVector(C.X, C.Y + Side * (H.Y - 4.0), C.Z + H.Z - 6.0), FVector(2 * H.X - 8.0, 8.0, 5.0), StripWhite, 0.f, false);
	}
	// The lift door, set into its wall, with a lighter frame round it (unless that door is a doorway onto a corridor).
	if (Doorways.Contains(LiftDoor)) { return; }
	const FVector N = LiftNormal.GetSafeNormal();
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(N.Y, N.X));
	Box(LiftDoor + N * 4.0 + FVector(0, 0, 118.0), FVector(6.0, 150.0, 236.0), PanelDark, Yaw, false);
	Box(LiftDoor + N * 8.0 + FVector(0, 0, 112.0), FVector(8.0, 128.0, 224.0), Burgundy, Yaw, false);
	Box(LiftDoor + N * 9.0 + FVector(0, 0, 112.0), FVector(2.0, 3.0, 220.0), PanelGrey, Yaw, false); // the seam
	Box(LiftDoor + N * 9.0 + FVector(0, 0, 232.0), FVector(2.0, 90.0, 10.0), StripWhite, Yaw, false);  // the lamp over it
}

void UShipInteriorComponent::Console(const FVector& FloorPos, float YawDeg, float Width, float Seed)
{
	const FQuat Q(FVector::UpVector, FMath::DegreesToRadians(YawDeg));
	const FVector Fwd = Q.GetForwardVector();
	Box(FloorPos + FVector(0, 0, 46.0), FVector(60.0, Width, 92.0), ConsoleBlack, YawDeg);
	Box(FloorPos + FVector(0, 0, 93.0) - Fwd * 2.0, FVector(50.0, Width - 8.0, 2.0), Teal, YawDeg, false);
	// The raked bank behind the desk.
	UMaterialInterface* Screen = MakeScreen(TEXT("Console"), Seed, 2.2f);
	Box(FloorPos - Fwd * 32.0 + FVector(0, 0, 118.0), FVector(8.0, Width - 4.0, 56.0), Screen ? Screen : Teal, YawDeg, false);
	Box(FloorPos - Fwd * 36.0 + FVector(0, 0, 118.0), FVector(6.0, Width, 64.0), PanelDark, YawDeg, false);
}

// ---- Rooms -------------------------------------------------------------------------------------

void UShipInteriorComponent::Build()
{
	AStarshipPawn* Ship = Cast<AStarshipPawn>(GetOwner());
	if (!Ship || !GetWorld() || !GetWorld()->IsGameWorld()) { return; }
	for (USceneComponent* P : AllPieces) { if (P) { P->DestroyComponent(); } }
	AllPieces.Reset(); Materials.Reset(); Rooms.Reset(); PieceCount = 0; BayShuttlePieces.Reset(); BayShuttle = nullptr;
	Zones.Reset(); StretchZone0.Reset(); LampPool.Reset(); StopDoors.Reset(); CarPieces.Reset(); CarSolids.Reset(); CarRoot = nullptr;
	RidePhase = ERide::Idle; CarStop = RideTo = INDEX_NONE; LiftPlaceIndex = INDEX_NONE; LastLiveFeet = FVector(1e9, 1e9, 1e9);

	PanelGrey = MakeLit(TEXT("PanelGrey"), FLinearColor(0.62f, 0.63f, 0.66f), 0.75f);
	PanelLight = MakeLit(TEXT("PanelLight"), FLinearColor(0.70f, 0.71f, 0.74f), 0.6f);
	PanelDark = MakeLit(TEXT("PanelDark"), FLinearColor(0.16f, 0.17f, 0.19f), 0.6f);
	Carpet = MakeLit(TEXT("Carpet"), FLinearColor(0.22f, 0.16f, 0.17f), 0.95f);
	Grating = MakeLit(TEXT("Grating"), FLinearColor(0.20f, 0.21f, 0.23f), 0.6f, 0.5f);
	DeckPlate = MakeLit(TEXT("DeckPlate"), FLinearColor(0.35f, 0.36f, 0.38f), 0.55f, 0.3f);
	Burgundy = MakeLit(TEXT("Burgundy"), FLinearColor(0.32f, 0.07f, 0.09f), 0.6f);
	StripWhite = MakeLit(TEXT("StripWhite"), FLinearColor(0.9f, 0.92f, 0.95f), 0.8f, 0.f, FLinearColor(0.95f, 0.97f, 1.0f), 6.f);
	SoftWhite = MakeLit(TEXT("SoftWhite"), FLinearColor(0.9f, 0.92f, 0.95f), 0.8f, 0.f, FLinearColor(0.95f, 0.97f, 1.0f), 2.2f);
	ConsoleBlack = MakeLit(TEXT("ConsoleBlack"), FLinearColor(0.05f, 0.05f, 0.06f), 0.4f, 0.2f);
	Teal = MakeLit(TEXT("Teal"), FLinearColor(0.02f, 0.08f, 0.1f), 0.5f, 0.f, FLinearColor(0.2f, 0.9f, 0.95f), 2.5f);
	CoreBlue = MakeLit(TEXT("CoreBlue"), FLinearColor(0.1f, 0.2f, 0.35f), 0.3f, 0.f, FLinearColor(0.3f, 0.65f, 1.0f), 5.f);
	BedWhite = MakeLit(TEXT("BedWhite"), FLinearColor(0.85f, 0.86f, 0.88f), 0.7f);
	HullGrey = MakeLit(TEXT("HullGrey"), FLinearColor(0.72f, 0.73f, 0.75f), 0.5f, 0.2f);
	PadGlow = MakeLit(TEXT("PadGlow"), FLinearColor(0.5f, 0.6f, 0.7f), 0.4f, 0.f, FLinearColor(0.75f, 0.85f, 1.0f), 3.f);
	Metal = MakeLit(TEXT("Metal"), FLinearColor(0.22f, 0.22f, 0.24f), 0.35f, 0.6f);
	StripBlue = MakeLit(TEXT("StripBlue"), FLinearColor(0.2f, 0.4f, 0.9f), 0.6f, 0.f, FLinearColor(0.2f, 0.5f, 1.0f), 5.f);
	Wood = MakeLit(TEXT("Wood"), FLinearColor(0.36f, 0.22f, 0.12f), 0.5f);
	Crate = MakeLit(TEXT("Crate"), FLinearColor(0.55f, 0.50f, 0.40f), 0.7f, 0.1f);
	Touch = MakeScreen(TEXT("PlanTouch"), 11.f, 1.2f);
	if (!Touch) { Touch = Teal; }

	const double X = Ship->GetShipLengthCm() / 30500.0;
	const FVector Bay = Ship->ShuttleBayOffset;
	// The decks' floors (ShipPlan): the signature rooms stand on them.
	TArray<FDeckSpec> Decks; SlonkPlan::DeckTable(Decks, X);
	auto FloorOf = [&](TCHAR L, EDeckSection S = EDeckSection::Saucer) { for (const FDeckSpec& D : Decks) { if (D.Letter == L && D.Section == S) { return D.Floor; } } return 0.0; };
	auto Add = [&](EShipRoom Id, const FVector& Center, const FVector& Half)
	{
		FShipRoom R; R.Id = Id; R.Name = RoomName(Id); R.Deck = DeckOf(Id); R.Center = Center; R.Half = Half; R.Index = Rooms.Num();
		return Rooms.Add(R);
	};
	Add(EShipRoom::Bridge, FVector(7790.0 * X, 0.0, FloorOf('A') + 160.0), FVector::ZeroVector); // the set's own box; spots set by the pawn
	const int32 Eng = Add(EShipRoom::Engineering, FVector(-900.0 * X, 0.0, FloorOf('O', EDeckSection::Secondary) + 225.0), FVector(800.0, 500.0, 225.0));
	const int32 Sick = Add(EShipRoom::Sickbay, FVector(6600.0 * X, 0.0, FloorOf('G') + 150.0), FVector(600.0, 450.0, 150.0));
	// The straight bay's box begins at the clamshell door's axis (9.2 m forward of the door point) and runs 26 m
	// forward, 5.4 m tall: the hull's side skin at the stern wall (1.55 m forward of the axis, where the Blender
	// set's shell ends) is only 5.8 m over the deck at the bay's width. Its deck is 2 cm above the hull's own fan at
	// the fantail (deck Q); the throat behind it, over the fan inside the door's base ring, keeps the door's 7 m.
	// (The door point itself sits 2.5 m over the deck, the shuttle's taxi height: her 3 m hull then clears the roof truss.)
	const double BayDeck = -1946.0;
	const int32 BayRoom = Add(EShipRoom::ShuttleBay, FVector(Bay.X + 2220.0 * X, 0.0, BayDeck + 270.0), FVector(1300.0, 800.0, 270.0));
	const int32 Tr = Add(EShipRoom::Transporter, FVector(7200.0 * X, -1500.0 * X, FloorOf('G') + 150.0), FVector(400.0, 350.0, 150.0));
	// The airlock's staging area is on deck B under the bridge module's roof (the hull trace at a quarter length
	// forward lands on it at z ~3374), to port of the module's corridor; its hatch leads out through the hull.
	const int32 Lock = Add(EShipRoom::Airlock, FVector(7625.0 * X, -500.0 * X, FloorOf('B') + 150.0), FVector(300.0, 250.0, 150.0));
	// Deck G's corridor: an L whose athwartships leg runs past sickbay's aft door (its box) and whose forward leg
	// runs along the port side past the transporter room's door (a second box); the rec deck opens off the
	// athwartships leg to port. Their walls are back to back, so the boxes touch and the rooms are neighbours.
	// The adjoining edges are taken from the rooms' own boxes (whose centres scale with the ship's length while
	// their sizes do not), so the layout stays connected at any length; the sets themselves are drawn for the
	// 1701-A's 305 m, and only at that length do their doorways coincide with the boxes' seams.
	const double GFloor = Rooms[Sick].Center.Z - Rooms[Sick].Half.Z;
	const double SickAft = Rooms[Sick].Center.X - Rooms[Sick].Half.X;            // leg A's starboard wall = sickbay's aft wall
	const double TrFwd = Rooms[Tr].Center.Y + Rooms[Tr].Half.Y;                 // leg B's port wall = the transporter room's starboard wall
	const double TrAft = Rooms[Tr].Center.X + Rooms[Tr].Half.X + 100.0;         // leg B runs a metre past the transporter room's aft wall
	const int32 Corr = Add(EShipRoom::Corridor, FVector(SickAft - 120.0, -470.0, GFloor + 130.0), FVector(120.0, 680.0, 130.0));
	Rooms[Corr].Legs.Add(FBox(FVector(SickAft, TrFwd, GFloor), FVector(TrAft, TrFwd + 240.0, GFloor + 260.0)));
	const int32 Rec = Add(EShipRoom::RecDeck, FVector(SickAft - 240.0 - 30.0 - 900.0, -200.0, GFloor + 300.0), FVector(900.0, 700.0, 300.0));
	// The officers' mess sits within deck C's lobby ring, the captain's cabin outside deck D's with its window on
	// the hull, the crew quarters set in deck F's core between the long spokes.
	const int32 MessR = Add(EShipRoom::Mess, FVector(8050.0 * X, 0.0, FloorOf('C') + 140.0), FVector(550.0, 350.0, 140.0));
	const int32 Cab = Add(EShipRoom::Quarters, FVector(9800.0 * X, -500.0 * X, FloorOf('D') + 140.0), FVector(450.0, 350.0, 140.0));
	const int32 Crew = Add(EShipRoom::CrewQuarters, FVector(11010.0 * X, 0.0, FloorOf('F') + 130.0), FVector(340.0, 800.0, 130.0));
	const FVector Tube = Ship->TorpedoTubeCentre;
	const int32 Torp = Add(EShipRoom::TorpedoBay, FVector(Tube.X - 800.0 * X, 0.0, Tube.Z), FVector(800.0, 350.0, 225.0));
	const int32 Cargo = Add(EShipRoom::CargoBay, FVector(-2500.0 * X, 0.0, FloorOf('S', EDeckSection::Secondary) + 350.0), FVector(1000.0, 600.0, 350.0));

	Building = &Rooms[Eng]; BuildEngineering(Rooms[Eng]);
	Building = &Rooms[Sick]; BuildSickbay(Rooms[Sick]);
	Building = &Rooms[BayRoom]; BuildShuttleBay(Rooms[BayRoom]);
	Building = &Rooms[Tr]; BuildTransporter(Rooms[Tr]);
	Building = &Rooms[Lock]; BuildAirlock(Rooms[Lock]);
	Building = &Rooms[Corr]; BuildCorridor(Rooms[Corr]);
	Building = &Rooms[Rec]; BuildRecDeck(Rooms[Rec]);
	Building = &Rooms[MessR]; BuildMess(Rooms[MessR]);
	Building = &Rooms[Cab]; BuildQuarters(Rooms[Cab]);
	Building = &Rooms[Crew]; BuildCrewQuarters(Rooms[Crew]);
	Building = &Rooms[Torp]; BuildTorpedoBay(Rooms[Torp]);
	Building = &Rooms[Cargo]; BuildCargoBay(Rooms[Cargo]);
	Building = nullptr;

	// The plan round the signature rooms, its stretches and rooms as places of their own, the car's place last.
	BuildPlan(X);

	// Neighbours: a signature room and whatever place comes within a wall's thickness of its boxes; a stretch and
	// the passages it joins; a generated room and its passage.
	auto Boxes = [](const FShipRoom& R)
	{
		TArray<FBox> B; if (!R.Half.IsNearlyZero() && R.Yaw == 0.0) { B.Add(FBox(R.Center - R.Half, R.Center + R.Half)); } B.Append(R.Legs); return B;
	};
	for (FShipRoom& A : Rooms)
	{
		if (!A.IsSignature() || A.Id == EShipRoom::Bridge) { continue; }
		for (FShipRoom& B : Rooms)
		{
			if (A.Index == B.Index || B.Id == EShipRoom::Bridge || B.Id == EShipRoom::Turbolift || (!B.IsSignature() && B.Id != EShipRoom::Passage)) { continue; }
			bool bTouch = false;
			for (const FBox& BA : Boxes(A)) { for (const FBox& BB : Boxes(B)) { if (BA.ExpandBy(40.0).Intersect(BB)) { bTouch = true; } } }
			if (bTouch) { A.Neighbours.AddUnique(B.Index); B.Neighbours.AddUnique(A.Index); }
		}
	}
	ShownPlace = INDEX_NONE;
}

void UShipInteriorComponent::BuildEngineering(FShipRoom& R)
{
	const FVector C = R.Center, H = R.Half;
	const double Floor = C.Z - H.Z;
	const FVector Door(C.X + H.X, C.Y + 300.0, Floor);
	R.Lift = InteriorSpot(Door + FVector(-90.0, 0, 0), FVector(-1, 0, 0));
	R.Use = InteriorSpot(FVector(C.X + 220.0, C.Y, Floor), FVector(-1, 0, 0));
	R.UseLabel = TEXT("open the engineering board");
	if (RoomMesh(EngineeringMesh, FVector(C.X, C.Y, Floor)))
	{
		// The vault's panels and the core light the room; the well glows from below.
		for (double Dx : { -500.0, -200.0, 100.0, 400.0, 700.0 }) { for (double Dy : { -170.0, 170.0 }) { Light(FVector(C.X + Dx, C.Y + Dy, C.Z + H.Z - 45.0), 70.f, 1100.f, FLinearColor(0.95f, 0.97f, 1.f)); } }
		Light(FVector(C.X - 300.0, C.Y, Floor + 200.0), 90.f, 900.f, FLinearColor(0.4f, 0.7f, 1.f));
		Light(FVector(C.X - 300.0, C.Y, Floor - 150.0), 40.f, 500.f, FLinearColor(0.4f, 0.7f, 1.f));
		return;
	}
	Shell(R, Door, FVector(-1, 0, 0), false);
	// The warp core: a lit column from deck to ceiling, ringed, on a dark base.
	const FVector Core(C.X - 250.0, C.Y, 0.0);
	Cyl(FVector(Core.X, Core.Y, C.Z), 90.f, (float)(2 * H.Z), CoreBlue);
	Cyl(FVector(Core.X, Core.Y, Floor + 20.0), 135.f, 40.f, PanelDark);
	for (double F : { 0.33, 0.66 }) { Cyl(FVector(Core.X, Core.Y, Floor + 2 * H.Z * F), 108.f, 26.f, PanelDark, false); }
	// The master console faces the core; the chief's board opens from it.
	Box(FVector(C.X + 120.0, C.Y, Floor + 45.0), FVector(200.0, 110.0, 90.0), ConsoleBlack);
	Box(FVector(C.X + 120.0, C.Y, Floor + 91.0), FVector(184.0, 94.0, 2.0), Teal, 0.f, false);
	// Stations along the side walls.
	Console(FVector(C.X - 100.0, C.Y - H.Y + 34.0, Floor), 90.f, 260.f, 3.f);
	Console(FVector(C.X + 300.0, C.Y - H.Y + 34.0, Floor), 90.f, 200.f, 4.f);
	Console(FVector(C.X - 100.0, C.Y + H.Y - 34.0, Floor), -90.f, 260.f, 5.f);
	Console(FVector(C.X + 300.0, C.Y + H.Y - 34.0, Floor), -90.f, 200.f, 6.f);
	for (double Dx : { -450.0, 0.0, 450.0 }) { Light(FVector(C.X + Dx, C.Y, C.Z + H.Z - 30.0), 120.f, 1400.f, FLinearColor(0.95f, 0.97f, 1.f)); }
	Light(FVector(Core.X, Core.Y, C.Z), 60.f, 900.f, FLinearColor(0.4f, 0.7f, 1.f));
}

void UShipInteriorComponent::BuildSickbay(FShipRoom& R)
{
	const FVector C = R.Center, H = R.Half;
	const double Floor = C.Z - H.Z;
	const FVector Door(C.X - H.X, C.Y - 126.0, Floor);   // the doorway onto deck G's corridor, at a bay's centre (build_rooms.py SICK_DOOR_Y)
	R.Lift = InteriorSpot(Door + FVector(90.0, 0, 0), FVector(1, 0, 0));
	R.Use = InteriorSpot(FVector(C.X + 320.0 - 85.0, C.Y + 200.0, Floor), FVector(1, 0, 0));
	R.UseLabel = TEXT("open the sickbay board");
	if (RoomMesh(SickbayMesh, FVector(C.X, C.Y, Floor)))
	{
		for (double Dx : { -250.0, 250.0 }) { for (double Dy : { -160.0, 160.0 }) { Light(FVector(C.X + Dx, C.Y + Dy, C.Z + H.Z - 20.0), 45.f, 900.f, FLinearColor(0.97f, 0.98f, 1.f)); } }
		return;
	}
	Shell(R, Door, FVector(1, 0, 0), false, { Door });   // the doorway onto deck G's corridor
	// Biobeds along the port wall, heads to the wall, each with its monitor.
	for (int32 i = 0; i < 4; ++i)
	{
		const double Bx = C.X - 390.0 + i * 240.0;
		Box(FVector(Bx, C.Y - H.Y + 115.0, Floor + 32.0), FVector(90.0, 200.0, 64.0), BedWhite);
		Box(FVector(Bx, C.Y - H.Y + 18.0, Floor + 70.0), FVector(90.0, 36.0, 140.0), PanelDark);
		UMaterialInterface* Mon = MakeScreen(TEXT("Biobed"), 10.f + i, 1.8f);
		Box(FVector(Bx, C.Y - H.Y + 37.0, Floor + 110.0), FVector(70.0, 2.0, 40.0), Mon ? Mon : Teal, 0.f, false);
	}
	// The doctor's desk, facing the beds; the sickbay board opens from it.
	Console(FVector(C.X + 320.0, C.Y + 200.0, Floor), 180.f, 220.f, 8.f);
	// A supplies cabinet on the starboard wall.
	Box(FVector(C.X - 200.0, C.Y + H.Y - 30.0, Floor + 110.0), FVector(300.0, 60.0, 220.0), PanelGrey);
	Box(FVector(C.X - 200.0, C.Y + H.Y - 61.0, Floor + 110.0), FVector(290.0, 2.0, 200.0), PanelDark, 0.f, false);
	for (double Dx : { -300.0, 300.0 }) { Light(FVector(C.X + Dx, C.Y, C.Z + H.Z - 25.0), 90.f, 1200.f, FLinearColor(0.97f, 0.98f, 1.f)); }
}

void UShipInteriorComponent::BuildShuttleBay(FShipRoom& R)
{
	const FVector C = R.Center, H = R.Half;
	const double Floor = C.Z - H.Z;
	const FVector Door(C.X + H.X, C.Y + 500.0, Floor);
	R.Lift = InteriorSpot(Door + FVector(-90.0, 0, 0), FVector(-1, 0, 0));
	const FVector S(C.X + 150.0, C.Y, Floor);
	R.Use = InteriorSpot(S + FVector(100.0, -150.0 - 170.0, 0.0), FVector(0, 1, 0));   // before her port door, forward of the pods
	R.UseLabel = TEXT("board the shuttle");
	if (RoomMesh(ShuttleBayMesh, FVector(C.X, C.Y, Floor)))
	{
		// The shuttle mesh is authored in the bay's frame (already on the landing box), so it shares the bay's origin.
		BayShuttle = RoomMesh(BayShuttleMesh, FVector(C.X, C.Y, Floor));
		if (!BayShuttle.IsValid()) { PrimitiveShuttle(S); }
		// The atmosphere field across the opening: nothing to see, but the deck ends there for anyone on foot.
		if (UStaticMeshComponent* Field = Box(FVector(C.X - H.X + 70.0, C.Y, C.Z), FVector(10.0, 2 * H.Y, 2 * H.Z), PanelDark)) { Field->SetHiddenInGame(true); }
		for (double Dx : { -800.0, -400.0, 0.0, 400.0, 800.0 }) { for (double Dy : { -350.0, 350.0 }) { Light(FVector(C.X + Dx, C.Y + Dy, C.Z + H.Z - 140.0), 170.f, 1800.f, FLinearColor(0.95f, 0.97f, 1.f)); } }
		// The throat under the stern cap, up to the clamshell door.
		Light(FVector(C.X - H.X - 420.0, C.Y, C.Z + H.Z - 40.0), 220.f, 2200.f, FLinearColor(0.95f, 0.97f, 1.f));
		return;
	}
	Shell(R, Door, FVector(-1, 0, 0), true);
	// The bay door frame, open to space astern.
	for (double Side : { -1.0, 1.0 }) { Box(FVector(C.X - H.X + 30.0, C.Y + Side * (H.Y - 30.0), C.Z), FVector(60.0, 60.0, 2 * H.Z), PanelDark); }
	Box(FVector(C.X - H.X + 30.0, C.Y, C.Z + H.Z - 40.0), FVector(60.0, 2 * H.Y, 80.0), PanelDark);
	Box(FVector(C.X - H.X + 61.0, C.Y, C.Z + H.Z - 40.0), FVector(2.0, 2 * H.Y - 140.0, 10.0), StripWhite, 0.f, false);
	// The atmosphere field across the opening: nothing to see, but the deck ends there for anyone on foot.
	if (UStaticMeshComponent* Field = Box(FVector(C.X - H.X + 70.0, C.Y, C.Z), FVector(10.0, 2 * H.Y, 2 * H.Z), PanelDark)) { Field->SetHiddenInGame(true); }
	// Guide lines down the deck to the door.
	for (double Dy : { -220.0, 220.0 }) { Box(FVector(C.X - 200.0, C.Y + Dy, Floor + 1.0), FVector(2 * H.X - 500.0, 12.0, 2.0), MakeLit(TEXT("BayLine"), FLinearColor(0.8f, 0.7f, 0.1f), 0.8f, 0.f, FLinearColor(1.f, 0.85f, 0.2f), 1.2f), 0.f, false); }
	// The parked shuttle: the real one is spawned when it leaves.
	PrimitiveShuttle(S);
	for (double Dx : { -800.0, 0.0, 800.0 }) { for (double Dy : { -400.0, 400.0 }) { Light(FVector(C.X + Dx, C.Y + Dy, C.Z + H.Z - 40.0), 160.f, 1800.f, FLinearColor(0.95f, 0.97f, 1.f)); } }
}

void UShipInteriorComponent::BuildTransporter(FShipRoom& R)
{
	const FVector C = R.Center, H = R.Half;
	const double Floor = C.Z - H.Z;
	const FVector Door(C.X + 136.0, C.Y + H.Y, Floor);   // the doorway onto the corridor's forward leg (TRANS_DOOR_X)
	R.Lift = InteriorSpot(Door + FVector(0, -90.0, 0), FVector(0, -1, 0));
	const FVector Pad(C.X - 150.0, C.Y, Floor);
	R.UseLabel = TEXT("energise");
	if (RoomMesh(TransporterMesh, FVector(C.X, C.Y, Floor)))
	{
		R.Use = InteriorSpot(Pad + FVector(0, 0, 24.5), FVector(1, 0, 0));
		Light(FVector(Pad.X, Pad.Y, C.Z + H.Z - 30.0), 70.f, 900.f, FLinearColor(0.8f, 0.88f, 1.f));
		Light(FVector(Pad.X - 150.0, Pad.Y, Floor + 150.0), 40.f, 600.f, FLinearColor(1.f, 0.9f, 0.75f));
		Light(FVector(C.X + 230.0, C.Y, C.Z + H.Z - 25.0), 50.f, 800.f, FLinearColor(0.97f, 0.98f, 1.f));
		return;
	}
	R.Use = InteriorSpot(Pad + FVector(0, 0, 18.0), FVector(1, 0, 0));
	Shell(R, Door, FVector(0, -1, 0), false, { Door });   // the doorway onto deck G's corridor
	// The pad: a low round platform with six lit discs, a glowing wall behind it.
	Cyl(Pad + FVector(0, 0, 9.0), 170.f, 18.f, PanelGrey);
	for (int32 i = 0; i < 6; ++i)
	{
		const float A = FMath::DegreesToRadians(60.f * i);
		Cyl(Pad + FVector(FMath::Cos(A) * 110.0, FMath::Sin(A) * 110.0, 19.0), 28.f, 3.f, PadGlow, false);
	}
	Cyl(Pad + FVector(0, 0, 19.0), 30.f, 3.f, PadGlow, false);
	Box(Pad + FVector(-172.0, 0, 130.0), FVector(8.0, 320.0, 240.0), PanelDark);
	Box(Pad + FVector(-167.0, 0, 130.0), FVector(2.0, 300.0, 220.0), MakeLit(TEXT("PadWall"), FLinearColor(0.3f, 0.4f, 0.55f), 0.5f, 0.f, FLinearColor(0.55f, 0.7f, 1.f), 1.6f), 0.f, false);
	// The operator's console faces the pad.
	Console(FVector(C.X + 250.0, C.Y, Floor), 180.f, 220.f, 12.f);
	Light(FVector(Pad.X, Pad.Y, C.Z + H.Z - 25.0), 80.f, 900.f, FLinearColor(0.75f, 0.85f, 1.f));
	Light(FVector(C.X + 200.0, C.Y, C.Z + H.Z - 25.0), 70.f, 900.f, FLinearColor(0.97f, 0.98f, 1.f));
}

// ---- The crew's places (the interiors round): modelled sets with primitive stand-ins -----------------------------

void UShipInteriorComponent::DoorFrame(const FVector& Door, const FVector& Normal)
{
	const FVector N = Normal.GetSafeNormal();
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(N.Y, N.X));
	const FVector Side = FVector::CrossProduct(FVector::UpVector, N);
	for (double S : { -1.0, 1.0 })
	{
		Box(Door + Side * S * 73.0 + FVector(0, 0, 118.0), FVector(24.0, 16.0, 236.0), PanelDark, Yaw);
		Box(Door + Side * S * 91.0 + N * 10.0 + FVector(0, 0, 120.0), FVector(3.0, 6.0, 200.0), StripWhite, Yaw, false);
	}
	Box(Door + FVector(0, 0, 240.0), FVector(24.0, 162.0, 20.0), PanelDark, Yaw);
}

void UShipInteriorComponent::BuildCorridor(FShipRoom& R)
{
	// The athwartships leg is the room's box; the forward leg its second box. Origin for the set: the floor at
	// the athwartships leg's centre line abreast of the corridor's forward (+y) end, pawn (5880, 0).
	const FVector C = R.Center, H = R.Half;
	const double Floor = C.Z - H.Z;
	const FVector Origin(C.X, 0.0, Floor);
	R.Lift = InteriorSpot(Origin + FVector(0, 210.0 - 90.0, 0), FVector(0, -1, 0));
	R.Use = R.Lift;
	R.UseLabel = TEXT("turbolift");   // the corridor's only thing to use is its lift, so the hint names it
	if (RoomMesh(CorridorMesh, Origin))
	{
		for (double Y : { 100.0, -300.0, -700.0, -1050.0 }) { Light(Origin + FVector(0, Y, 240.0), 18.f, 500.f, FLinearColor(0.95f, 0.97f, 1.f)); }
		for (double Xo : { 400.0, 900.0, 1400.0, 1780.0 }) { Light(Origin + FVector(Xo, -1030.0, 240.0), 18.f, 500.f, FLinearColor(0.95f, 0.97f, 1.f)); }
		return;
	}
	// Primitives: the two legs' floors, ceilings and walls, the lift door at the forward end, the doorway frames.
	const FBox LegB = R.Legs.Num() ? R.Legs[0] : FBox(C - H, C + H);
	const double T = 20.0;
	const FShipRoom* TrRoom = Room(EShipRoom::Transporter);
	const double TrX = TrRoom ? TrRoom->Center.X + 136.0 : LegB.Max.X - 364.0;   // the transporter room's doorway along the forward leg (its BuildTransporter Door)
	auto Leg = [&](const FVector& Lo, const FVector& Hi, bool bNoPosX, bool bNoNegX, bool bNoPosY, bool bNoNegY, const TArray<double>& PosXCuts, const TArray<double>& NegXCuts, const TArray<double>& NegYCuts)
	{
		const FVector Mid = (Lo + Hi) / 2, Ext = (Hi - Lo) / 2;
		Box(FVector(Mid.X, Mid.Y, Lo.Z - T / 2), FVector(2 * Ext.X + 2 * T, 2 * Ext.Y + 2 * T, T), Carpet);
		Box(FVector(Mid.X, Mid.Y, Hi.Z + T / 2), FVector(2 * Ext.X + 2 * T, 2 * Ext.Y + 2 * T, T), PanelGrey);
		if (!bNoPosX) { WallRun(false, Hi.X + T / 2, Lo.Y - T, Hi.Y + T, Lo.Z, Hi.Z, T, PanelGrey, PosXCuts); }
		if (!bNoNegX) { WallRun(false, Lo.X - T / 2, Lo.Y - T, Hi.Y + T, Lo.Z, Hi.Z, T, PanelGrey, NegXCuts); }
		if (!bNoPosY) { Box(FVector(Mid.X, Hi.Y + T / 2, Mid.Z), FVector(2 * Ext.X, T, 2 * Ext.Z), PanelGrey); }
		if (!bNoNegY) { WallRun(true, Lo.Y - T / 2, Lo.X, Hi.X, Lo.Z, Hi.Z, T, PanelGrey, NegYCuts); }
		Box(FVector(Mid.X, Mid.Y, Hi.Z - 3.0), FVector(Ext.X > Ext.Y ? 2 * Ext.X - 40.0 : 40.0, Ext.X > Ext.Y ? 40.0 : 2 * Ext.Y - 40.0, 4.0), StripWhite, 0.f, false);
	};
	// The athwartships leg: sickbay's doorway to starboard, the rec deck's to port, open at its aft (-y) end into
	// the forward leg; the forward leg: the transporter room's doorway to port, open at its aft (-x) end.
	Leg(C - H, C + H, false, false, false, true, { -126.0, (LegB.Min.Y + LegB.Max.Y) / 2 }, { -574.0 }, {});
	Leg(LegB.Min, LegB.Max, false, true, false, false, {}, {}, { TrX });
	// No wall across the athwartships leg's port end: like the authored set's, it stands open for the plan's
	// passage that carries the corridor on from there (ShipPlan.cpp).
	Box(FVector(C.X + H.X + T / 2, LegB.Min.Y - T / 2, C.Z), FVector(T, T, 2 * H.Z), PanelGrey);   // the post at the outer corner
	// The lift door in the forward end wall; the doorways' frames (the openings are in the rooms beyond, whose
	// walls stand back to back with these: the frames mark them).
	const FVector LiftDoor(C.X, C.Y + H.Y, Floor);
	Box(LiftDoor + FVector(0, -8.0, 112.0), FVector(128.0, 8.0, 224.0), Burgundy, 0.f, false);
	Box(LiftDoor + FVector(0, -9.0, 112.0), FVector(3.0, 2.0, 220.0), PanelGrey, 0.f, false);
	DoorFrame(FVector(C.X + H.X, -126.0, Floor), FVector(-1, 0, 0));
	DoorFrame(FVector(C.X - H.X, -574.0, Floor), FVector(1, 0, 0));
	DoorFrame(FVector(TrX, LegB.Min.Y, Floor), FVector(0, 1, 0));
	Console(FVector(LegB.Max.X - 40.0, (LegB.Min.Y + LegB.Max.Y) / 2, Floor), 0.f, 160.f, 21.f);
	for (double Y : { 100.0, -300.0, -700.0, -1050.0 }) { Light(Origin + FVector(0, Y, 240.0), 18.f, 500.f, FLinearColor(0.95f, 0.97f, 1.f)); }
	for (double Xo : { 400.0, 900.0, 1400.0 }) { Light(Origin + FVector(Xo, -1030.0, 240.0), 18.f, 500.f, FLinearColor(0.95f, 0.97f, 1.f)); }
}

void UShipInteriorComponent::BuildRecDeck(FShipRoom& R)
{
	const FVector C = R.Center, H = R.Half;
	const double Floor = C.Z - H.Z;
	const FVector Door(C.X + H.X, C.Y - 374.0, Floor);   // the doorway from the corridor (REC_DOOR_Y)
	R.Lift = InteriorSpot(Door + FVector(-90.0, 0, 0), FVector(-1, 0, 0));
	R.Use = InteriorSpot(FVector(C.X - H.X + 250.0, C.Y, Floor), FVector(-1, 0, 0));
	R.UseLabel = TEXT("the screen: open the star chart");
	if (RoomMesh(RecDeckMesh, FVector(C.X, C.Y, Floor)))
	{
		for (double Dx : { -600.0, -200.0, 200.0, 600.0 }) { Light(FVector(C.X + Dx, C.Y, C.Z + H.Z - 40.0), 120.f, 1600.f, FLinearColor(0.97f, 0.98f, 1.f)); }
		Light(FVector(C.X - H.X + 150.0, C.Y, Floor + 300.0), 60.f, 900.f, FLinearColor(0.6f, 0.75f, 1.f));
		return;
	}
	Shell(R, Door, FVector(-1, 0, 0), false, { Door });
	// The screen aft, tables on the floor, the gallery's fascia along the sides.
	Box(FVector(C.X - H.X + 10.0, C.Y, Floor + 300.0), FVector(8.0, 900.0, 400.0), [&]{ UMaterialInterface* M = MakeScreen(TEXT("RecScreen"), 31.f, 1.8f); return M ? M : Teal; }(), 0.f, false);
	for (const FVector2D& P : { FVector2D(-450, -300), FVector2D(-450, 60), FVector2D(-50, -350), FVector2D(-50, 150), FVector2D(350, -250), FVector2D(350, 100) })
	{
		Cyl(FVector(C.X + P.X, C.Y + P.Y, Floor + 37.0), 55.f, 74.f, PanelGrey);
	}
	for (double S : { -1.0, 1.0 })
	{
		Box(FVector(C.X, C.Y + S * (H.Y - 110.0), Floor + 300.0), FVector(2 * H.X, 220.0, 20.0), PanelGrey);
		Box(FVector(C.X, C.Y + S * (H.Y - 222.0), Floor + 270.0), FVector(2 * H.X, 4.0, 60.0), PanelDark, 0.f, false);
	}
	for (double Dx : { -600.0, -200.0, 200.0, 600.0 }) { Light(FVector(C.X + Dx, C.Y, C.Z + H.Z - 40.0), 120.f, 1600.f, FLinearColor(0.97f, 0.98f, 1.f)); }
}

void UShipInteriorComponent::BuildMess(FShipRoom& R)
{
	const FVector C = R.Center, H = R.Half;
	const double Floor = C.Z - H.Z;
	const FVector Door(C.X - H.X, C.Y, Floor);
	R.Lift = InteriorSpot(Door + FVector(90.0, 0, 0), FVector(1, 0, 0));
	R.Use = InteriorSpot(FVector(C.X + 490.0, C.Y, Floor), FVector(-1, 0, 0));
	R.UseLabel = TEXT("join the table: the crew");
	if (RoomMesh(MessMesh, FVector(C.X, C.Y, Floor)))
	{
		for (double Dx : { -300.0, 30.0, 360.0 }) { for (double Dy : { -140.0, 140.0 }) { Light(FVector(C.X + Dx, C.Y + Dy, C.Z + H.Z - 40.0), 22.f, 600.f, FLinearColor(1.f, 0.92f, 0.8f)); } }
		return;
	}
	Shell(R, Door, FVector(1, 0, 0), false);
	Box(FVector(C.X + 30.0, C.Y, Floor + 72.0), FVector(660.0, 130.0, 8.0), ConsoleBlack);
	Box(FVector(C.X + 30.0, C.Y, Floor + 36.0), FVector(560.0, 60.0, 72.0), PanelDark);
	for (int32 i = 0; i < 5; ++i) { for (double S : { -1.0, 1.0 }) { Box(FVector(C.X + 30.0 - 240.0 + 120.0 * i, C.Y + S * 115.0, Floor + 45.0), FVector(50.0, 50.0, 90.0), Burgundy); } }
	for (double Dx : { -300.0, 30.0, 360.0 }) { for (double Dy : { -140.0, 140.0 }) { Light(FVector(C.X + Dx, C.Y + Dy, C.Z + H.Z - 40.0), 22.f, 600.f, FLinearColor(1.f, 0.92f, 0.8f)); } }
}

void UShipInteriorComponent::BuildQuarters(FShipRoom& R)
{
	const FVector C = R.Center, H = R.Half;
	const double Floor = C.Z - H.Z;
	const FVector Door(C.X - H.X, C.Y + 150.0, Floor);
	R.Lift = InteriorSpot(Door + FVector(90.0, 0, 0), FVector(1, 0, 0));
	R.Use = InteriorSpot(FVector(C.X + H.X - 220.0, C.Y + 140.0, Floor + 25.0), FVector(1, 0, 0));
	R.UseLabel = TEXT("the desk: Starfleet's orders");
	if (RoomMesh(QuartersMesh, FVector(C.X, C.Y, Floor)))
	{
		Light(FVector(C.X + 200.0, C.Y + 120.0, C.Z + H.Z - 40.0), 20.f, 700.f, FLinearColor(1.f, 0.95f, 0.9f));
		Light(FVector(C.X - 150.0, C.Y + 120.0, C.Z + H.Z - 40.0), 20.f, 700.f, FLinearColor(1.f, 0.95f, 0.9f));
		Light(FVector(C.X, C.Y - 200.0, Floor + 150.0), 12.f, 600.f, FLinearColor(0.8f, 0.88f, 1.f));
		return;
	}
	Shell(R, Door, FVector(1, 0, 0), false);
	Box(FVector(C.X + 100.0, C.Y + 140.0, Floor + 12.5), FVector(700.0, 420.0, 25.0), Carpet);
	Box(FVector(C.X + H.X - 70.0, C.Y + 140.0, Floor + 25.0 + 37.0), FVector(90.0, 180.0, 74.0), PanelGrey);
	Box(FVector(C.X + H.X - 55.0, C.Y + 140.0, Floor + 25.0 + 105.0), FVector(10.0, 60.0, 45.0), [&]{ UMaterialInterface* M = MakeScreen(TEXT("Viewer"), 17.f, 1.5f); return M ? M : Teal; }(), 0.f, false);
	Box(FVector(C.X - 100.0, C.Y + H.Y - 100.0, Floor + 25.0 + 30.0), FVector(220.0, 180.0, 60.0), BedWhite);
	Box(FVector(C.X, C.Y - H.Y - 4.0, Floor + 160.0), FVector(2 * H.X, 2.0, 180.0), MakeLit(TEXT("Window"), FLinearColor(0.02f, 0.03f, 0.05f), 0.1f, 0.f, FLinearColor(0.1f, 0.15f, 0.3f), 0.5f), 0.f, false);
	Light(FVector(C.X + 200.0, C.Y + 120.0, C.Z + H.Z - 40.0), 20.f, 700.f, FLinearColor(1.f, 0.95f, 0.9f));
}

void UShipInteriorComponent::BuildCrewQuarters(FShipRoom& R)
{
	// The corridor runs athwartships (x -130..130 of the set's origin at pawn x 10800), the open cabin forward of
	// its port end; the room's box covers both.
	const FVector C = R.Center, H = R.Half;
	const double Floor = C.Z - H.Z;
	const FVector Origin(C.X - 210.0, 0.0, Floor);
	R.Lift = InteriorSpot(Origin + FVector(0, 800.0 - 90.0, 0), FVector(0, -1, 0));
	R.Use = InteriorSpot(Origin + FVector(300.0, -400.0 - 125.0, 0), FVector(0, 1, 0));   // before the desk against the cabin's forward wall
	R.UseLabel = TEXT("the viewer: the library computer");
	if (RoomMesh(CrewQuartersMesh, Origin))
	{
		for (double Y : { 600.0, 200.0, -200.0, -600.0 }) { Light(Origin + FVector(0, Y, 240.0), 16.f, 500.f, FLinearColor(0.95f, 0.97f, 1.f)); }
		Light(Origin + FVector(340.0, -620.0, 220.0), 18.f, 500.f, FLinearColor(0.97f, 0.98f, 1.f));
		return;
	}
	Shell(R, FVector(Origin.X, Origin.Y + 800.0, Floor), FVector(0, -1, 0), false);
	Box(Origin + FVector(120.0 + 420.0 - 50.0, -600.0, 60.0), FVector(100.0, 200.0, 20.0), BedWhite);
	Box(Origin + FVector(120.0 + 420.0 - 50.0, -600.0, 160.0), FVector(100.0, 200.0, 20.0), BedWhite);
	Box(Origin + FVector(300.0, -400.0 - 35.0, 37.0), FVector(140.0, 60.0, 74.0), PanelGrey);
	for (double Y : { 600.0, 200.0, -200.0, -600.0 }) { Light(Origin + FVector(0, Y, 240.0), 16.f, 500.f, FLinearColor(0.95f, 0.97f, 1.f)); }
}

void UShipInteriorComponent::BuildTorpedoBay(FShipRoom& R)
{
	const FVector C = R.Center, H = R.Half;
	const double Floor = C.Z - H.Z;
	const FVector Door(C.X - H.X, C.Y + 200.0, Floor);
	R.Lift = InteriorSpot(Door + FVector(90.0, 0, 0), FVector(1, 0, 0));
	R.Use = InteriorSpot(FVector(C.X - 620.0 - 85.0, C.Y, Floor), FVector(1, 0, 0));
	R.UseLabel = TEXT("open the tactical board");
	if (RoomMesh(TorpedoBayMesh, FVector(C.X, C.Y, Floor)))
	{
		for (double Dx : { -500.0, -150.0, 200.0, 550.0 }) { Light(FVector(C.X + Dx, C.Y, C.Z + H.Z - 40.0), 50.f, 1000.f, FLinearColor(1.f, 0.85f, 0.8f)); }
		return;
	}
	Shell(R, Door, FVector(1, 0, 0), false);
	Box(FVector(C.X + 50.0, C.Y, Floor + 20.0), FVector(1300.0, 140.0, 40.0), PanelGrey);
	Box(FVector(C.X + 100.0, C.Y, Floor + 65.0), FVector(210.0, 76.0, 50.0), ConsoleBlack);
	for (double Y : { -100.0, 100.0 }) { Piece(CylinderMesh, FVector(C.X + H.X - 10.0, C.Y + Y, Floor + 110.0), FVector(140.0, 140.0, 16.0), FRotator(90.f, 0.f, 0.f), PanelDark, false); }
	for (double S : { -1.0, 1.0 }) { for (double Z : { 30.0, 130.0 }) { Box(FVector(C.X - 100.0, C.Y + S * (H.Y - 80.0), Floor + Z + 25.0), FVector(840.0, 76.0, 50.0), ConsoleBlack); } }
	Console(FVector(C.X - 620.0, C.Y, Floor), 0.f, 240.f, 23.f);
	for (double Dx : { -500.0, -150.0, 200.0, 550.0 }) { Light(FVector(C.X + Dx, C.Y, C.Z + H.Z - 40.0), 50.f, 1000.f, FLinearColor(1.f, 0.85f, 0.8f)); }
}

void UShipInteriorComponent::BuildCargoBay(FShipRoom& R)
{
	const FVector C = R.Center, H = R.Half;
	const double Floor = C.Z - H.Z;
	const FVector Door(C.X + H.X, C.Y + 300.0, Floor);
	R.Lift = InteriorSpot(Door + FVector(-90.0, 0, 0), FVector(-1, 0, 0));
	R.Use = InteriorSpot(FVector(C.X + H.X - 160.0 - 85.0, C.Y + 60.0, Floor), FVector(1, 0, 0));
	R.UseLabel = TEXT("the manifest: the master systems display");
	if (RoomMesh(CargoBayMesh, FVector(C.X, C.Y, Floor)))
	{
		for (double Dx : { -600.0, -200.0, 200.0, 600.0 }) { for (double Dy : { -240.0, 240.0 }) { Light(FVector(C.X + Dx, C.Y + Dy, C.Z + H.Z - 100.0), 100.f, 1500.f, FLinearColor(0.97f, 0.98f, 1.f)); } }
		Light(FVector(C.X - H.X + 220.0, C.Y, Floor + 300.0), 50.f, 900.f, FLinearColor(0.8f, 0.88f, 1.f));
		return;
	}
	Shell(R, Door, FVector(-1, 0, 0), false);
	for (double S : { -1.0, 1.0 }) { for (int32 i = 0; i < 8; ++i) { for (int32 Tier = 0; Tier < 3; ++Tier) { Box(FVector(C.X - 800.0 + i * 230.0, C.Y + S * (H.Y - 120.0), Floor + 105.0 + Tier * 220.0), FVector(200.0, 200.0, 200.0), (i + Tier) % 2 ? HullGrey : PanelGrey); } } }
	Box(FVector(C.X - H.X + 220.0, C.Y, Floor + 10.0), FVector(360.0, 460.0, 20.0), PanelGrey);
	Console(FVector(C.X + H.X - 160.0, C.Y + 60.0, Floor), 0.f, 160.f, 27.f);
	for (double Dx : { -600.0, -200.0, 200.0, 600.0 }) { for (double Dy : { -240.0, 240.0 }) { Light(FVector(C.X + Dx, C.Y + Dy, C.Z + H.Z - 100.0), 100.f, 1500.f, FLinearColor(0.97f, 0.98f, 1.f)); } }
}

void UShipInteriorComponent::ShowPlace(int32 Which, const FVector* Feet)
{
	ShownPlace = Which;
	const FShipRoom* Here = Place(Which);
	for (FShipRoom& R : Rooms)
	{
		if (!R.IsSignature()) { continue; }
		const bool bIn = R.Index == Which || (Here && Here->Neighbours.Contains(R.Index));
		const bool bOn = bIn || (R.Id == EShipRoom::ShuttleBay && bBayFromOutside);
		for (const TWeakObjectPtr<USceneComponent>& P : R.Pieces) { if (USceneComponent* S = P.Get()) { S->SetVisibility(bOn); } }
		for (const TWeakObjectPtr<UPrimitiveComponent>& P : R.Solids) { if (UPrimitiveComponent* S = P.Get()) { S->SetCollisionEnabled(bIn ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision); } }
	}
	SetBayShuttleShown(bBayShuttleShown);
	LastLiveFeet = FVector(1e9, 1e9, 1e9);
	if (Here) { UpdateLive(Feet ? *Feet : Here->Center); }
	else
	{
		for (int32 i = 0; i < Zones.Num(); ++i) { SetZoneShown(i, false, false); }
		for (UPointLightComponent* L : LampPool) { if (L) { L->SetVisibility(false); } }
	}
}

void UShipInteriorComponent::UpdateLive(const FVector& Feet)
{
	if (FVector::DistSquared(Feet, LastLiveFeet) < FMath::Square(250.0)) { return; }
	LastLiveFeet = Feet;
	const FShipRoom* Here = Place(ShownPlace);
	// The zones in sight: within the live radius on this deck (a storey either way); solid only close by. The
	// zone under the walker's feet always, whatever the distance test makes of a box they stand in the middle of.
	int32 Must = INDEX_NONE;
	if (Here && Here->Stretch != INDEX_NONE && Plan.Stretches.IsValidIndex(Here->Stretch))
	{
		int32 Bay = Plan.Rooms.IsValidIndex(Here->PlanRoom) ? Plan.Rooms[Here->PlanRoom].Bay : INDEX_NONE;
		if (Bay == INDEX_NONE)
		{
			double Best = 1e30;
			const TArray<FPlanBay>& Bays = Plan.Stretches[Here->Stretch].Bays;
			for (int32 b = 0; b < Bays.Num(); ++b) { const double D2 = FVector::DistSquared2D(Bays[b].Mid(), Feet); if (D2 < Best) { Best = D2; Bay = b; } }
		}
		Must = ZoneOf(Here->Stretch, Bay);
	}
	for (int32 i = 0; i < Zones.Num(); ++i)
	{
		FZone& Z = Zones[i];
		const double D2 = Z.Bounds.ComputeSquaredDistanceToPoint(Feet);
		const bool bLevel = FMath::Abs(Z.Bounds.Min.Z - Feet.Z) < 150.0;   // this deck's zones only
		const bool bShow = i == Must || (bLevel && D2 < FMath::Square((double)LiveRadiusCm));
		const bool bSolid = i == Must || (bLevel && D2 < FMath::Square((double)SolidRadiusCm));
		SetZoneShown(i, bShow, bSolid);
	}
	PlaceLamps(Feet);
}

void UShipInteriorComponent::SetBayShownFromOutside(bool bShown)
{
	if (bBayFromOutside == bShown) { return; }
	bBayFromOutside = bShown;
	ShowPlace(ShownPlace);
}

bool UShipInteriorComponent::GetRoomBox(EShipRoom Room, FVector& OutCenter, FVector& OutHalf) const
{
	for (const FShipRoom& R : Rooms)
	{
		if (R.Id == Room && !R.Half.IsNearlyZero()) { OutCenter = R.Center; OutHalf = R.Half; return true; }
	}
	return false;
}

// ---- The plan: zones of instanced boxes --------------------------------------------------------------------

void UShipInteriorComponent::BuildPlan(double X)
{
	using namespace SlonkPlan;
	TArray<FPlanReserved> Reserved;
	for (const FShipRoom& R : Rooms)
	{
		if (R.Half.IsNearlyZero()) { continue; }
		FPlanReserved P; P.Name = R.Name; P.Box = FBox(R.Center - R.Half, R.Center + R.Half); Reserved.Add(P);
		for (const FBox& L : R.Legs) { FPlanReserved Q; Q.Name = R.Name; Q.Box = L; Reserved.Add(Q); }
	}
	SlonkPlan::Build(Plan, X, Reserved);
	// A place for every stretch (its bays as boxes) and every room (an oriented box).
	TArray<int32> StretchPlace; StretchPlace.Init(INDEX_NONE, Plan.Stretches.Num());
	for (int32 s = 0; s < Plan.Stretches.Num(); ++s)
	{
		const FPlanStretch& St = Plan.Stretches[s];
		FShipRoom R; R.Id = EShipRoom::Passage; R.Index = Rooms.Num(); R.Stretch = s;
		R.Deck = Plan.Decks[St.Deck].Label(); R.Name = FString::Printf(TEXT("Deck %s, %s"), *R.Deck, *St.Name);
		for (const FPlanBay& B : St.Bays)
		{
			// The bay's box, a hand below the floor so feet standing on it count (FBox::IsInside is strict).
			FBox Bx(ForceInit);
			for (const FVector& P : { B.P0, B.P1 }) { for (double Sd : { -1.0, 1.0 }) { const FVector Q = P + B.SideDir(0) * (Sd * (CorrW + 12.0)); Bx += Q - FVector(0, 0, 40.0); Bx += Q + FVector(0, 0, CorrH); } }
			R.Legs.Add(Bx);
		}
		if (St.Bays.Num()) { R.Lift = InteriorSpot(St.Bays[0].Mid(), St.Bays[0].Dir()); R.Use = R.Lift; }
		StretchPlace[s] = Rooms.Add(R);
	}
	for (int32 s = 0; s < Plan.Stretches.Num(); ++s)
	{
		for (int32 L : Plan.Stretches[s].Links) { if (StretchPlace.IsValidIndex(L)) { Rooms[StretchPlace[s]].Neighbours.AddUnique(StretchPlace[L]); } }
	}
	for (int32 r = 0; r < Plan.Rooms.Num(); ++r)
	{
		const FPlanRoom& Pr = Plan.Rooms[r];
		FShipRoom R; R.Id = Pr.Kind; R.Index = Rooms.Num(); R.Stretch = Pr.Stretch; R.PlanRoom = r;
		R.Deck = Plan.Decks[Pr.Deck].Label(); R.Name = Pr.Name;
		R.Center = Pr.Center; R.Half = Pr.Half; R.Yaw = Pr.Yaw;
		// Stand just inside the doorway to use the room (nothing to use yet: the spot is where the lift op lands).
		R.Lift = InteriorSpot(Pr.Door + Pr.DoorIn * 90.0, Pr.DoorIn);
		R.Use = R.Lift;
		const int32 Idx = Rooms.Add(R);
		if (StretchPlace.IsValidIndex(Pr.Stretch)) { Rooms[Idx].Neighbours.Add(StretchPlace[Pr.Stretch]); Rooms[StretchPlace[Pr.Stretch]].Neighbours.Add(Idx); }
	}
	for (FPlanStop& S : Plan.Stops) { if (StretchPlace.IsValidIndex(S.Stretch)) { S.Stretch = S.Stretch; } }
	// The signature rooms' stops: their sets' shut lift doors (the three rooms off deck G's corridor share its).
	for (FShipRoom& R : Rooms)
	{
		if (!R.IsSignature() || LiftStop(R.Id) != R.Id) { continue; }
		FPlanStop S; S.Name = R.Name; S.Room = R.Id; S.bWalkIn = false; S.Shaft = TEXT("Set");
		const EDeckSection Sec = (R.Id == EShipRoom::Engineering || R.Id == EShipRoom::ShuttleBay || R.Id == EShipRoom::CargoBay) ? EDeckSection::Secondary : R.Id == EShipRoom::TorpedoBay ? EDeckSection::Dorsal : EDeckSection::Saucer;
		S.Deck = Plan.DeckIndex(R.Deck.Len() ? R.Deck[0] : 'A', Sec);
		const FVector Fwd = R.Lift.GetRotation().GetForwardVector();
		S.Car = R.Half.IsNearlyZero() ? FVector(R.Center.X, R.Center.Y, Plan.Decks.IsValidIndex(S.Deck) ? Plan.Decks[S.Deck].Floor : R.Center.Z) : R.Lift.GetLocation() - Fwd * 230.0;
		S.Facing = Fwd.IsNearlyZero() ? FVector(1, 0, 0) : Fwd;
		R.Stop = Plan.Stops.Add(S);
	}
	// The car.
	{
		FShipRoom Car; Car.Id = EShipRoom::Turbolift; Car.Index = Rooms.Num(); Car.Name = TEXT("Turbolift"); Car.Half = FVector(130.0, 130.0, CorrH / 2);
		LiftPlaceIndex = Rooms.Add(Car);
	}
	// Zones: a stretch in runs of ZoneBays bays (a ring of 168 is fourteen zones, a spoke one), each taking in
	// the rooms whose doorways are in its bays; the bounds are what the live radii are measured to.
	Zones.Reset(); StretchZone0.Init(INDEX_NONE, Plan.Stretches.Num());
	for (int32 s = 0; s < Plan.Stretches.Num(); ++s)
	{
		const FPlanStretch& St = Plan.Stretches[s];
		const double Floor = Plan.Decks[St.Deck].Floor;
		StretchZone0[s] = Zones.Num();
		for (int32 b0 = 0; b0 < St.Bays.Num(); b0 += ZoneBays)
		{
			FZone Z; Z.Deck = St.Deck; Z.Stretch = s; Z.Bay0 = b0; Z.Bay1 = FMath::Min(b0 + ZoneBays, St.Bays.Num());
			FBox B(ForceInit);
			for (int32 b = Z.Bay0; b < Z.Bay1; ++b) { B += St.Bays[b].P0; B += St.Bays[b].P1; }
			Z.Bounds = B.ExpandBy(FVector(CorrW, CorrW, 0.0)) + FVector(B.Min.X, B.Min.Y, Floor + CorrH);
			Z.Bounds.Min.Z = Floor;
			Zones.Add(Z);
		}
	}
	for (const FPlanRoom& Pr : Plan.Rooms) { const int32 Zi = ZoneOf(Pr.Stretch, Pr.Bay); if (Zones.IsValidIndex(Zi)) { Zones[Zi].Bounds += Pr.Bounds; } }
	BuildCar();
	UE_LOG(LogTemp, Log, TEXT("Interior: the plan has %d decks, %d stretches, %d rooms, %d stops (%d places)"), Plan.Decks.Num(), Plan.Stretches.Num(), Plan.Rooms.Num(), Plan.Stops.Num(), Rooms.Num());
}

void UShipInteriorComponent::Inst(FZone& Z, const FVector& Center, const FVector& Size, const FQuat& Rot, UMaterialInterface* Mat, bool bSolid, bool bCylinder)
{
	if (!GetOwner()) { return; }
	UStaticMesh* Mesh = bCylinder ? CylinderMesh.Get() : CubeMesh.Get();
	if (!Mesh) { return; }
	auto Make = [&](UMaterialInterface* M, bool bVisible, bool bBlocks)
	{
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(GetOwner(), *FString::Printf(TEXT("Plan_%d"), PieceCount++));
		C->SetupAttachment(this);
		C->SetStaticMesh(Mesh);
		if (M) { C->SetMaterial(0, M); }
		C->SetMobility(EComponentMobility::Movable);
		C->SetGenerateOverlapEvents(false);
		C->bAffectDistanceFieldLighting = false;
		C->SetReceivesDecals(false);
		C->SetCollisionObjectType(ECC_WorldDynamic);
		C->SetCollisionResponseToAllChannels(ECR_Ignore);
		C->SetCollisionResponseToChannel(ECC_GameTraceChannel1, bBlocks ? ECR_Block : ECR_Ignore);
		C->SetCollisionEnabled(bBlocks && Z.bSolidOn ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		C->SetVisibility(bVisible && Z.bShown);
		C->SetCastShadow(bVisible);
		C->RegisterComponent();
		AllPieces.Add(C);
		return C;
	};
	const FTransform T(Rot, Center, Size / 100.0);
	TMap<UMaterialInterface*, UInstancedStaticMeshComponent*>& Map = bCylinder ? Z.VisualCyl : Z.Visual;
	UInstancedStaticMeshComponent*& V = Map.FindOrAdd(Mat);
	if (!V) { V = Make(Mat, true, false); }
	V->AddInstance(T, false);
	if (bSolid)
	{
		if (!Z.Solid) { Z.Solid = Make(nullptr, false, true); }
		if (bCylinder)
		{
			// The solid set is cubes: a cylinder blocks as its bounding box.
			Z.Solid->AddInstance(T, false);
		}
		else { Z.Solid->AddInstance(T, false); }
	}
}

void UShipInteriorComponent::Cant(FZone& Z, const FRunFrameRef& Fr, double Along, double Length, double Sign, double ULo, double ZLo, double UHi, double ZHi, UMaterialInterface* Mat, double Thick, bool bSolid)
{
	const double Ln = FMath::Sqrt(FMath::Square(UHi - ULo) + FMath::Square(ZHi - ZLo));
	Inst(Z, Fr.At(Along, Sign * (ULo + UHi) / 2, (ZLo + ZHi) / 2), FVector(Length, Thick, Ln), Fr.Lean(Sign * (UHi - ULo), ZHi - ZLo), Mat, bSolid);
}

void UShipInteriorComponent::MakeStopDoor(int32 Stop, int32 Zone, const FVector& Center, const FVector& Along, const FQuat& Rot)
{
	using namespace SlonkPlan;
	if (!GetOwner() || Stop == INDEX_NONE) { return; }
	FStopDoor D; D.Along = Along.GetSafeNormal2D(); D.Zone = Zone;
	for (int32 k = 0; k < 2; ++k)
	{
		D.Shut[k] = Center + D.Along * ((k == 0 ? -1.0 : 1.0) * DoorW / 4);
		UStaticMeshComponent* Leaf = Piece(CubeMesh, D.Shut[k], FVector(DoorW / 2, 6.0, DoorH), Rot.Rotator(), Burgundy, true);
		if (!Leaf) { return; }
		Leaf->SetVisibility(false);
		D.Leaf[k] = Leaf;
	}
	StopDoors.Add(Stop, D);
}

void UShipInteriorComponent::DrawBay(FZone& Z, const FPlanStretch& S, int32 i, double Floor)
{
	using namespace SlonkPlan;
	const double W = CorrW, F = CorrF, C = CorrC, Z1 = CorrZ1, Z2 = CorrZ2, H = CorrH, T = 12.0;
	const FPlanBay& B = S.Bays[i];
	const double L = B.Length();
	if (L < 1.0) { return; }
	const FVector M = B.Mid();
	const FRunFrameRef Fr(FVector(M.X, M.Y, Floor), B.Dir());
	Inst(Z, Fr.At(0, 0, -6.0), FVector(L, 2 * (W + T), 12.0), Fr.Q, Grating, true);
	Inst(Z, Fr.At(0, 0, H + 6.0), FVector(L, 2 * C, 12.0), Fr.Q, PanelDark, false);
	// The frame at the bay's start (and at the last bay's end where nothing closes it): ribs following the
	// section both sides, the beam across, a threshold over the floor seam; turned to the bisector of a turn, and
	// widened to cover the wedge a turn opens at the outer wall.
	auto Frame = [&](const FVector& P, const FVector& Dir, double Turn)
	{
		const FRunFrameRef Fb(FVector(P.X, P.Y, Floor), Dir);
		const double Rw = 16.0 + 2.0 * (W + T) * FMath::Abs(FMath::Tan(Turn / 2));
		Inst(Z, Fb.At(0, 0, H - 6.0), FVector(Rw, 2 * C - 10.0, 12.0), Fb.Q, PanelGrey, false);
		Inst(Z, Fb.At(0, 0, 0.5), FVector(Rw, 2 * (W + T), 1.0), Fb.Q, Metal, false);
		// The ribs are solid: at a turn they are what closes the wedge between the bays' panels.
		for (double Sg : { -1.0, 1.0 })
		{
			Cant(Z, Fb, 0, Rw, Sg, F - 6.0, 0.0, W - 6.0, Z1, PanelGrey, 12.0, true);
			Inst(Z, Fb.At(0, Sg * (W - 3.0), (Z1 + Z2) / 2), FVector(Rw, 12.0, Z2 - Z1), Fb.Q, PanelGrey, true);
			Cant(Z, Fb, 0, Rw, Sg, W - 6.0, Z2, C - 6.0, H, PanelGrey, 12.0, false);
		}
	};
	if (i > 0 || !S.bEnd0)
	{
		const FVector Prev = i > 0 ? S.Bays[i - 1].Dir() : B.Dir();
		Frame(B.P0, (Prev + B.Dir()).GetSafeNormal2D(), B.TurnIn);
	}
	if (i == S.Bays.Num() - 1 && !S.bEnd1) { Frame(B.P1, B.Dir(), 0.0); }
	// The light in the bay's ceiling.
	Inst(Z, Fr.At(0, 0, H - 1.5), FVector(60.0, 25.0, 3.0), Fr.Q, StripWhite, false);
	Z.Lamps.Add(Fr.At(0, 0, H - 12.0));
	// The sides.
	const double La = L - 16.0;
	for (int32 s = 0; s < 2; ++s)
	{
		const double Sg = s == 0 ? 1.0 : -1.0;
		const EPlanWall Kind = B.Side[s];
		if (Kind == EPlanWall::Panel)
		{
			// The lit lower panel in its grey surround, the straight band and its rail, the pale upper panel.
			Cant(Z, Fr, 0, La, Sg, F, 0.0, W, Z1, PanelGrey, 4.0, true);
			Cant(Z, Fr, 0, La - 24.0, Sg, F - 3.5, 12.0, W - 3.5, Z1 - 12.0, SoftWhite, 2.0, false);
			Inst(Z, Fr.At(0, Sg * (W + 2.0), (Z1 + Z2) / 2), FVector(La, 4.0, Z2 - Z1), Fr.Q, PanelGrey, true);
			Inst(Z, Fr.At(0, Sg * (W - 1.0), Z1 + 8.0), FVector(La, 3.0, 5.0), Fr.Q, PanelDark, false);
			Cant(Z, Fr, 0, La, Sg, W, Z2, C, H, PanelLight, 4.0, false);
			continue;
		}
		// A flat side: the floor and ceiling filled out to the waist, then whatever stands there.
		Inst(Z, Fr.At(0, Sg * (F + W) / 2, -0.5), FVector(L, W - F, 1.0), Fr.Q, Grating, false);
		Inst(Z, Fr.At(0, Sg * (C + W) / 2, H - 2.0), FVector(L, W - C + 2.0, 4.0), Fr.Q, PanelDark, false);
		if (Kind == EPlanWall::Open)
		{
			// Posts at the junction's two corners, the full height: the other passage's canted panels end against them.
			for (double A : { -1.0, 1.0 }) { Inst(Z, Fr.At(A * (L / 2 - 8.0), Sg * (W + 4.0), H / 2), FVector(16.0, 20.0, H), Fr.Q, PanelGrey, true); }
			continue;
		}
		if (Kind == EPlanWall::DoorOpen || Kind == EPlanWall::Lift)
		{
			const double Side = (L - DoorW) / 2;
			Inst(Z, Fr.At(-(L / 2 - Side / 2), Sg * (W + T / 2), H / 2), FVector(Side, T, H), Fr.Q, PanelLight, true);
			Inst(Z, Fr.At((L / 2 - Side / 2), Sg * (W + T / 2), H / 2), FVector(Side, T, H), Fr.Q, PanelLight, true);
			Inst(Z, Fr.At(0, Sg * (W + T / 2), (DoorH + H) / 2), FVector(DoorW, T, H - DoorH), Fr.Q, PanelLight, true);
		}
		else { Inst(Z, Fr.At(0, Sg * (W + T / 2), H / 2), FVector(L, T, H), Fr.Q, PanelLight, true); }
		if (Kind == EPlanWall::Flat) { continue; }
		// The doorway's frame: jambs and head proud of the wall, lamps each side, the sill.
		for (double A : { -1.0, 1.0 })
		{
			Inst(Z, Fr.At(A * (DoorW / 2 + 8.0), Sg * W, DoorH / 2 + 5.0), FVector(16.0, 24.0, DoorH + 10.0), Fr.Q, PanelDark, true);
			Inst(Z, Fr.At(A * (DoorW / 2 + 26.0), Sg * (W - 14.0), DoorH / 2 + 10.0), FVector(6.0, 3.0, DoorH - 30.0), Fr.Q, StripWhite, false);
		}
		Inst(Z, Fr.At(0, Sg * W, DoorH + 10.0), FVector(DoorW + 32.0, 24.0, 20.0), Fr.Q, PanelDark, false);
		Inst(Z, Fr.At(0, Sg * W, 0.5), FVector(DoorW, 24.0, 1.0), Fr.Q, Metal, false);
		if (Kind == EPlanWall::DoorShut)
		{
			Inst(Z, Fr.At(0, Sg * (W + 3.0), DoorH / 2), FVector(DoorW, 6.0, DoorH), Fr.Q, Burgundy, true);
			Inst(Z, Fr.At(0, Sg * (W - 0.5), DoorH / 2), FVector(2.0, 1.0, DoorH), Fr.Q, PanelDark, false);
		}
		else if (Kind == EPlanWall::Lift)
		{
			Inst(Z, Fr.At(0, Sg * (W - 14.0), DoorH + 24.0), FVector(60.0, 2.0, 8.0), Fr.Q, StripWhite, false);
			MakeStopDoor(B.Stop[s], (int32)(&Z - Zones.GetData()), Fr.At(0, Sg * (W + 3.0), DoorH / 2), Fr.D, Fr.Q);
		}
	}
}

void UShipInteriorComponent::DrawEndWall(FZone& Z, const FPlanBay& B, bool bAtStart, double Floor, int32 Stop)
{
	using namespace SlonkPlan;
	const double W = CorrW, H = CorrH, T = 12.0;
	const FVector E = bAtStart ? B.P0 : B.P1;
	// A frame along the wall: X across the corridor, Y back along the run (so +Y at the start points outward).
	const FRunFrameRef Fw(FVector(E.X, E.Y, Floor), B.SideDir(0));
	const double Uo = bAtStart ? 1.0 : -1.0;
	if (Stop == INDEX_NONE) { Inst(Z, Fw.At(0, Uo * T / 2, H / 2), FVector(2 * (W + T), T, H), Fw.Q, PanelLight, true); return; }
	// The lift's door cut through the wall: the pieces each side and the lintel over.
	const double Side = (2 * (W + T) - DoorW) / 2;
	for (double A : { -1.0, 1.0 }) { Inst(Z, Fw.At(A * (W + T - Side / 2), Uo * T / 2, H / 2), FVector(Side, T, H), Fw.Q, PanelLight, true); }
	Inst(Z, Fw.At(0, Uo * T / 2, (DoorH + H) / 2), FVector(DoorW, T, H - DoorH), Fw.Q, PanelLight, true);
	// A sill from the wall out to the car's floor, so the step through finds deck under it.
	Inst(Z, Fw.At(0, Uo * 24.0, -6.0), FVector(DoorW + 40.0, 72.0, 12.0), Fw.Q, Grating, true);
	for (double A : { -1.0, 1.0 })
	{
		Inst(Z, Fw.At(A * (DoorW / 2 + 8.0), 0, DoorH / 2 + 5.0), FVector(16.0, 24.0, DoorH + 10.0), Fw.Q, PanelDark, true);
		Inst(Z, Fw.At(A * (DoorW / 2 + 26.0), -Uo * 14.0, DoorH / 2 + 10.0), FVector(6.0, 3.0, DoorH - 30.0), Fw.Q, StripWhite, false);
	}
	Inst(Z, Fw.At(0, 0, DoorH + 10.0), FVector(DoorW + 32.0, 24.0, 20.0), Fw.Q, PanelDark, false);
	Inst(Z, Fw.At(0, 0, 0.5), FVector(DoorW, 24.0, 1.0), Fw.Q, Metal, false);
	Inst(Z, Fw.At(0, -Uo * 14.0, DoorH + 24.0), FVector(60.0, 2.0, 8.0), Fw.Q, StripWhite, false);
	MakeStopDoor(Stop, (int32)(&Z - Zones.GetData()), Fw.At(0, -Uo * 3.0, DoorH / 2), Fw.D, Fw.Q);
}

void UShipInteriorComponent::DrawRoom(FZone& Z, const FPlanRoom& R, double Floor)
{
	using namespace SlonkPlan;
	const double H = CorrH;
	auto Wall = [&](const FVector& A, const FVector& B)
	{
		const double Len = FVector::Dist2D(A, B);
		if (Len < 1.0) { return; }
		const FVector Mid = (A + B) * 0.5;
		const FRunFrameRef Fr(FVector(Mid.X, Mid.Y, Floor), B - A);
		Inst(Z, Fr.At(0, 0, H / 2), FVector(Len + 12.0, 12.0, H), Fr.Q, PanelLight, true);
		Inst(Z, Fr.At(0, 0, 10.0), FVector(Len, 14.0, 20.0), Fr.Q, PanelDark, false);
	};
	Wall(R.Corners[0], R.Corners[3]);
	Wall(R.Corners[1], R.Corners[2]);
	Wall(R.Corners[3], R.Corners[2]);
	// Floor and ceiling: the room's oriented box, wide enough for a wedge's back; a hair's height apart from the
	// neighbours' so coplanar faces never fight.
	const FQuat Q(FVector::UpVector, R.Yaw);
	double Wide = 0.0;
	for (const FVector& Cn : R.Corners) { Wide = FMath::Max(Wide, FMath::Abs(Q.UnrotateVector(Cn - R.Center).Y)); }
	const double Nudge = 0.3 * (double)((R.Stretch + (int32)(R.Center.X * 0.01)) % 3);
	const bool bSoft = R.Kind == EShipRoom::Cabin || R.Kind == EShipRoom::Office || R.Kind == EShipRoom::Lounge;
	const FVector FloorC(R.Center.X, R.Center.Y, Floor - 3.0 - Nudge);
	Inst(Z, FloorC, FVector(R.Half.X * 2 + 12.0, Wide * 2 + 12.0, 4.0), Q, bSoft ? Carpet : DeckPlate, true);
	Inst(Z, FVector(R.Center.X, R.Center.Y, Floor + H + 4.0 + Nudge), FVector(R.Half.X * 2 + 12.0, Wide * 2 + 12.0, 4.0), Q, PanelGrey, false);
	// Lights.
	const int32 N = R.Half.Y > 300.0 ? 2 : 1;
	for (int32 k = 0; k < N; ++k)
	{
		const double Off = N == 1 ? 0.0 : (k == 0 ? -R.Half.Y / 2 : R.Half.Y / 2);
		const FVector P = R.Center + Q.RotateVector(FVector(0, Off, 0));
		Inst(Z, FVector(P.X, P.Y, Floor + H - 1.5), FVector(120.0, 60.0, 3.0), Q, SoftWhite, false);
		Z.Lamps.Add(FVector(P.X, P.Y, Floor + H - 20.0));
	}
	Furnish(Z, R, Floor);
}

void UShipInteriorComponent::Furnish(FZone& Z, const FPlanRoom& R, double Floor)
{
	using namespace SlonkPlan;
	// The room's frame: origin at the middle of the front wall on the floor, X into the room, Y to the right.
	const FQuat Q(FVector::UpVector, R.Yaw);
	const FVector In = Q.GetForwardVector();
	const FVector Front = R.Center - In * R.Half.X;
	const FRunFrameRef Fr(FVector(Front.X, Front.Y, Floor), In);
	const double D = R.Half.X * 2, Wd = R.Half.Y * 2, H = CorrH;
	const double Du = FVector::DotProduct(R.Door - Front, Fr.S);    // the door's offset along the front wall
	const double Away = Du >= 0.0 ? -1.0 : 1.0;                        // the side away from the door
	auto Put = [&](double A, double U, double Zc, double Sa, double Su, double Sz, UMaterialInterface* Mat, bool bSolid = true, bool bCyl = false, double YawDeg = 0.0)
	{
		Inst(Z, Fr.At(A, U, Zc), FVector(Sa, Su, Sz), YawDeg != 0.0 ? Fr.Q * FQuat(FVector::UpVector, FMath::DegreesToRadians(YawDeg)) : Fr.Q, Mat, bSolid, bCyl);
	};
	auto Chair = [&](double A, double U, double YawDeg)
	{
		const FQuat Qc = Fr.Q * FQuat(FVector::UpVector, FMath::DegreesToRadians(YawDeg));
		const FVector Fwd = Qc.GetForwardVector();
		const FVector Seat = Fr.At(A, U, 22.0);
		Inst(Z, Seat, FVector(46.0, 46.0, 44.0), Qc, PanelGrey, true);
		Inst(Z, Seat - Fwd * 20.0 + FVector(0, 0, 42.0), FVector(8.0, 46.0, 50.0), Qc, PanelGrey, false);
	};
	auto Lounger = [&](double A, double U, double YawDeg)
	{
		const FQuat Qc = Fr.Q * FQuat(FVector::UpVector, FMath::DegreesToRadians(YawDeg));
		const FVector Fwd = Qc.GetForwardVector();
		const FVector Seat = Fr.At(A, U, 22.0);
		Inst(Z, Seat, FVector(80.0, 80.0, 44.0), Qc, BedWhite, true);
		Inst(Z, Seat - Fwd * 33.0 + FVector(0, 0, 50.0), FVector(14.0, 80.0, 70.0), Qc * FQuat(FVector::RightVector, FMath::DegreesToRadians(-14.0)), BedWhite, false);
	};
	auto Bunk = [&](double A, double U, double YawDeg)
	{
		Put(A, U, 25.0, 200.0, 92.0, 50.0, PanelGrey, true, false, YawDeg);
		Put(A, U, 56.0, 196.0, 88.0, 12.0, BedWhite, false, false, YawDeg);
		Put(A + (YawDeg == 0.0 ? 75.0 : 0.0), U + (YawDeg == 0.0 ? 0.0 : 75.0), 66.0, 40.0, 60.0, 8.0, BedWhite, false, false, YawDeg);
	};
	auto Cabinet = [&](double A, double U, double Sa, double Su, UMaterialInterface* Front)
	{
		Put(A, U, 110.0, Sa, Su, 220.0, PanelGrey);
		const double Face = Su > Sa ? (U > 0 ? -1.0 : 1.0) : 0.0;   // the lit front faces into the room
		if (Face != 0.0) { Put(A, U + Face * (Su / 2 + 0.6), 130.0, Sa - 20.0, 1.0, 60.0, Front, false); }
		else { Put(A - Sa / 2 - 0.6, U, 130.0, 1.0, Su - 20.0, 60.0, Front, false); }
	};
	switch (R.Kind)
	{
	case EShipRoom::Cabin:
		// The bunk along the back wall, the desk and chair against the far side wall, a locker by the door's
		// opposite jamb, the viewer over the desk, a lamp strip over the bunk.
		Bunk(D - 50.0, 0.0, 90.0);
		Put(D - 6.0, 0.0, 200.0, 2.0, 120.0, 6.0, SoftWhite, false);
		Put(D / 2, -Away * (Wd / 2 - 36.0), 37.0, 120.0, 70.0, 74.0, PanelDark);
		Chair(D / 2, -Away * (Wd / 2 - 110.0), -Away * 90.0);
		Put(D / 2, -Away * (Wd / 2 - 3.0), 150.0, 70.0, 2.0, 45.0, Teal, false);
		Put(40.0, Away * (Wd / 2 - 32.0), 100.0, 60.0, 60.0, 200.0, PanelGrey);
		break;
	case EShipRoom::Lab:
		for (double Sg : { -1.0, 1.0 })
		{
			Put(D / 2 + 20.0, Sg * (Wd / 2 - 38.0), 45.0, D - 160.0, 72.0, 90.0, PanelDark);
			Put(D / 2 + 20.0, Sg * (Wd / 2 - 38.0), 91.0, D - 170.0, 62.0, 2.0, Teal, false);
			Put(D / 2 + 20.0, Sg * (Wd / 2 - 3.0), 150.0, D - 180.0, 2.0, 50.0, Teal, false);
		}
		Cabinet(D - 32.0, 0.0, 60.0, Wd - 60.0, Touch);
		break;
	case EShipRoom::Store:
		Cabinet(D - 36.0, 0.0, 70.0, Wd - 30.0, PanelDark);
		for (int32 k = 0; k < 3; ++k)
		{
			const double U = -Wd / 2 + 80.0 + k * ((Wd - 160.0) / 2);
			Put(D - 36.0, U, 166.0, 50.0, 50.0, 50.0, Crate, false);
			Put(D - 110.0, U, 30.0, 60.0, 60.0, 60.0, Crate);
		}
		Put(D - 110.0, 0.0, 90.0, 60.0, 60.0, 60.0, Crate, false);
		break;
	case EShipRoom::Workshop:
		Put(D - 48.0, 0.0, 45.0, 92.0, Wd - 60.0, 90.0, PanelDark);
		Put(D - 3.0, 0.0, 160.0, 2.0, Wd - 100.0, 90.0, PanelGrey, false);
		for (int32 k = 0; k < 5; ++k) { Put(D - 6.0, -Wd / 2 + 80.0 + k * ((Wd - 160.0) / 4), 160.0, 4.0, 10.0, 40.0 + 10.0 * (k % 3), Metal, false); }
		Put(D / 2 - 20.0, -Away * (Wd / 2 - 70.0), 60.0, 100.0, 100.0, 120.0, HullGrey);
		Put(D / 2 - 20.0, -Away * (Wd / 2 - 70.0), 121.0, 60.0, 60.0, 2.0, Touch, false);
		break;
	case EShipRoom::Office:
		Put(D * 0.6, 0.0, 37.0, 80.0, 180.0, 74.0, PanelDark);
		Put(D * 0.6, 0.0, 75.0, 76.0, 176.0, 2.0, Wood, false);
		Chair(D * 0.6 + 80.0, 0.0, 180.0);
		Chair(D * 0.6 - 90.0, -60.0, 0.0); Chair(D * 0.6 - 90.0, 60.0, 0.0);
		Put(D - 3.0, 0.0, 150.0, 2.0, 120.0, 70.0, Teal, false);
		break;
	case EShipRoom::Lounge:
		Put(D - 3.0, 0.0, 145.0, 2.0, FMath::Min(Wd - 120.0, 600.0), 90.0, Teal, false);
		for (double U = -Wd / 2 + 90.0; U < Wd / 2 - 60.0; U += 110.0) { Lounger(D - 150.0, U, 0.0); if (D > 400.0) { Lounger(D - 290.0, U + 50.0, 0.0); } }
		Put(D / 2 - 60.0, 0.0, 25.0, 90.0, 90.0, 50.0, Wood, true, true);
		break;
	case EShipRoom::TransporterRoom:
		Put(D - 170.0, 0.0, 10.0, 300.0, 300.0, 20.0, PanelGrey, true, true);
		Put(D - 170.0, 0.0, 20.0, 240.0, 240.0, 2.0, PanelDark, false, true);
		for (int32 k = 0; k < 6; ++k)
		{
			const double A = FMath::DegreesToRadians(k * 60.0);
			Put(D - 170.0 + 95.0 * FMath::Cos(A), 95.0 * FMath::Sin(A), 22.0, 70.0, 70.0, 2.0, PadGlow, false, true);
		}
		Put(120.0, -Away * (Wd / 2 - 80.0), 45.0, 60.0, 140.0, 90.0, ConsoleBlack);
		Put(120.0, -Away * (Wd / 2 - 80.0), 91.0, 50.0, 130.0, 2.0, Teal, false);
		Put(D - 170.0, 0.0, H - 20.0, 320.0, 320.0, 6.0, SoftWhite, false, true);
		break;
	case EShipRoom::Brig:
		Bunk(D - 50.0, 0.0, 90.0);
		for (double A : { -1.0, 1.0 }) { Put(14.0, A * (DoorW / 2 + 2.0), DoorH / 2, 4.0, 4.0, DoorH, StripBlue, false); }
		Put(14.0, 0.0, DoorH + 2.0, 4.0, DoorW + 8.0, 4.0, StripBlue, false);
		Put(14.0, 0.0, DoorH / 2, 1.0, DoorW, DoorH, CoreBlue, true);
		break;
	case EShipRoom::Armoury:
		for (double Sg : { -1.0, 1.0 })
		{
			Put(D / 2 + 20.0, Sg * (Wd / 2 - 24.0), 100.0, D - 160.0, 40.0, 200.0, PanelDark);
			for (int32 k = 0; k < 6; ++k) { Put(D / 2 + 20.0 - (D - 200.0) / 2 + k * ((D - 200.0) / 5), Sg * (Wd / 2 - 46.0), 110.0, 8.0, 10.0, 110.0, Metal, false); }
		}
		Put(D - 30.0, 0.0, 60.0, 60.0, Wd - 100.0, 120.0, PanelGrey);
		Put(D - 61.0, 0.0, 90.0, 1.0, Wd - 140.0, 40.0, Touch, false);
		break;
	case EShipRoom::Hold:
		for (int32 k = 0; k < 2; ++k)
		{
			for (double U = -Wd / 2 + 70.0; U < Wd / 2 - 50.0; U += 125.0)
			{
				Put(D - 70.0 - k * 125.0, U, 55.0, 110.0, 110.0, 110.0, Crate);
				if ((int32)(U + k * 37.0) % 3 == 0) { Put(D - 70.0 - k * 125.0, U, 165.0, 110.0, 110.0, 110.0, Crate, false); }
			}
		}
		Put(D - 120.0, 0.0, H - 20.0, 2 * D / 3, 40.0, 10.0, PanelDark, false);
		break;
	case EShipRoom::Hatch:
		Inst(Z, Fr.At(D - 8.0, 0.0, 125.0), FVector(200.0, 200.0, 16.0), Fr.Q * FQuat(FVector::RightVector, FMath::DegreesToRadians(90.0)), PanelDark, false, true);
		Inst(Z, Fr.At(D - 14.0, 0.0, 125.0), FVector(176.0, 176.0, 6.0), Fr.Q * FQuat(FVector::RightVector, FMath::DegreesToRadians(90.0)), StripWhite, false, true);
		Inst(Z, Fr.At(D - 17.0, 0.0, 125.0), FVector(160.0, 160.0, 6.0), Fr.Q * FQuat(FVector::RightVector, FMath::DegreesToRadians(90.0)), HullGrey, false, true);
		for (int32 k = 0; k < 3; ++k) { Put(80.0 + k * 90.0, -Away * (Wd / 2 - 30.0), 112.0, 80.0, 60.0, 224.0, PanelGrey); }
		Put(D - 40.0, Away * (Wd / 2 - 40.0), 130.0, 6.0, 36.0, 30.0, ConsoleBlack, false);
		Put(D - 43.5, Away * (Wd / 2 - 40.0), 130.0, 2.0, 30.0, 24.0, Teal, false);
		break;
	case EShipRoom::Equipment:
	default:
		for (double Sg : { -1.0, 1.0 })
		{
			Put(D / 2 + 20.0, Sg * (Wd / 2 - 31.0), 110.0, D - 140.0, 62.0, 220.0, PanelDark);
			Put(D / 2 + 20.0, Sg * (Wd / 2 - 62.5), 150.0, D - 180.0, 1.0, 40.0, Touch, false);
			Put(D / 2 + 20.0, Sg * (Wd / 2 - 62.5), 60.0, D - 180.0, 1.0, 10.0, StripBlue, false);
		}
		Put(D - 26.0, 0.0, 125.0, 52.0, 52.0, 250.0, Metal);
		Put(D - 52.5, 0.0, 125.0, 1.0, 20.0, 220.0, CoreBlue, false);
		break;
	}
}

int32 UShipInteriorComponent::ZoneOf(int32 Stretch, int32 Bay) const
{
	if (!StretchZone0.IsValidIndex(Stretch) || Bay < 0) { return INDEX_NONE; }
	const int32 Zi = StretchZone0[Stretch] + Bay / ZoneBays;
	return Zones.IsValidIndex(Zi) && Zones[Zi].Stretch == Stretch ? Zi : INDEX_NONE;
}

void UShipInteriorComponent::BuildZone(int32 Zi)
{
	if (!Zones.IsValidIndex(Zi) || Zones[Zi].bBuilt) { return; }
	FZone& Z = Zones[Zi];
	Z.bBuilt = true;
	const int32 s = Z.Stretch;
	const FPlanStretch& S = Plan.Stretches[s];
	const double Floor = Plan.Decks[S.Deck].Floor;
	for (int32 i = Z.Bay0; i < Z.Bay1; ++i) { DrawBay(Z, S, i, Floor); }
	// End walls, with a lift door where a stop stands past the end.
	auto EndStop = [&](const FVector& E)
	{
		for (int32 i = 0; i < Plan.Stops.Num(); ++i)
		{
			const FPlanStop& St = Plan.Stops[i];
			if (St.Stretch != s) { continue; }
			bool bInBay = false;
			for (const FPlanBay& B : S.Bays) { if (B.Stop[0] == i || B.Stop[1] == i) { bInBay = true; } }
			if (!bInBay && FVector::Dist2D(St.Car, E) < 400.0) { return i; }
		}
		return (int32)INDEX_NONE;
	};
	if (S.Bays.Num())
	{
		if (S.bEnd0 && Z.Bay0 == 0) { DrawEndWall(Z, S.Bays[0], true, Floor, EndStop(S.Bays[0].P0)); }
		if (S.bEnd1 && Z.Bay1 == S.Bays.Num()) { DrawEndWall(Z, S.Bays.Last(), false, Floor, EndStop(S.Bays.Last().P1)); }
	}
	for (const FPlanRoom& R : Plan.Rooms) { if (R.Stretch == s && R.Bay >= Z.Bay0 && R.Bay < Z.Bay1) { DrawRoom(Z, R, Floor); } }
}

void UShipInteriorComponent::SetZoneShown(int32 s, bool bShow, bool bSolid)
{
	if (!Zones.IsValidIndex(s)) { return; }
	if (bShow && !Zones[s].bBuilt) { BuildZone(s); }
	FZone& Z = Zones[s];
	if (!Z.bBuilt) { return; }
	const bool bWasShown = Z.bShown, bWasSolid = Z.bSolidOn;
	Z.bShown = bShow; Z.bSolidOn = bSolid && bShow;
	if (bWasShown != Z.bShown)
	{
		for (auto& It : Z.Visual) { if (It.Value) { It.Value->SetVisibility(Z.bShown); } }
		for (auto& It : Z.VisualCyl) { if (It.Value) { It.Value->SetVisibility(Z.bShown); } }
	}
	if (bWasSolid != Z.bSolidOn && Z.Solid) { Z.Solid->SetCollisionEnabled(Z.bSolidOn ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision); }
	if (bWasShown != Z.bShown || bWasSolid != Z.bSolidOn)
	{
		for (auto& It : StopDoors)
		{
			if (It.Value.Zone != s) { continue; }
			const bool bShut = It.Value.Open < 0.98f;
			for (UStaticMeshComponent* Leaf : It.Value.Leaf)
			{
				if (!Leaf) { continue; }
				Leaf->SetVisibility(Z.bShown && bShut);
				Leaf->SetCollisionEnabled(Z.bSolidOn && bShut ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
			}
		}
	}
}

void UShipInteriorComponent::PlaceLamps(const FVector& Feet)
{
	// The pool's lights go to the nearest lamps of the zones in view; the rest are put out.
	TArray<TPair<double, FVector>> Near;
	for (const FZone& Z : Zones)
	{
		if (!Z.bShown) { continue; }
		for (const FVector& L : Z.Lamps)
		{
			const double D2 = FVector::DistSquared(L, Feet);
			if (D2 < FMath::Square(1800.0)) { Near.Emplace(D2, L); }
		}
	}
	Near.Sort([](const TPair<double, FVector>& A, const TPair<double, FVector>& B) { return A.Key < B.Key; });
	for (int32 i = 0; i < LampPoolSize; ++i)
	{
		if (i >= LampPool.Num())
		{
			UPointLightComponent* L = Light(FVector::ZeroVector, 18.f, 750.f, FLinearColor(0.95f, 0.96f, 1.f));
			if (!L) { break; }
			LampPool.Add(L);
		}
		UPointLightComponent* L = LampPool[i];
		if (!L) { continue; }
		if (i < Near.Num()) { L->SetRelativeLocation(Near[i].Value); L->SetVisibility(true); }
		else { L->SetVisibility(false); }
	}
}

// ---- The turbolift ----------------------------------------------------------------------------------------------

void UShipInteriorComponent::BuildCar()
{
	using namespace SlonkPlan;
	if (!GetOwner()) { return; }
	CarRoot = NewObject<USceneComponent>(GetOwner(), TEXT("LiftCar"));
	CarRoot->SetupAttachment(this);
	CarRoot->SetMobility(EComponentMobility::Movable);
	CarRoot->RegisterComponent();
	AllPieces.Add(CarRoot);
	const double H = CorrH, R = 127.0;
	const int32 First = AllPieces.Num();
	auto Solidify = [&](UStaticMeshComponent* C, bool bSolid) { if (C) { CarPieces.Add(C); if (bSolid) { CarSolids.Add(C); } } };
	// Twelve wall panels round the car with the door's gap forward (+X), posts at the gap, the floor, the ceiling
	// with its lit ring, the panel of deck buttons by the door.
	for (int32 k = 0; k < 12; ++k)
	{
		const double A = 15.0 + 30.0 * k;
		if (A < 31.0 || A > 329.0) { continue; }
		const double Ar = FMath::DegreesToRadians(A);
		Solidify(Piece(CubeMesh, FVector(R * FMath::Cos(Ar), R * FMath::Sin(Ar), H / 2), FVector(6.0, 2 * 130.0 * FMath::Sin(FMath::DegreesToRadians(15.0)) + 2.0, H), FRotator(0.f, (float)A, 0.f), PanelLight, true, CarRoot), true);
		if (A > 100.0 && A < 260.0) { Solidify(Piece(CubeMesh, FVector((R - 4.0) * FMath::Cos(Ar), (R - 4.0) * FMath::Sin(Ar), 150.0), FVector(2.0, 50.0, 60.0), FRotator(0.f, (float)A, 0.f), SoftWhite, false, CarRoot), false); }
	}
	for (double Sg : { -1.0, 1.0 })
	{
		const double Ar = FMath::DegreesToRadians(30.0 * Sg);
		Solidify(Piece(CubeMesh, FVector(R * FMath::Cos(Ar), R * FMath::Sin(Ar), H / 2), FVector(14.0, 14.0, H), FRotator(0.f, (float)(30.0 * Sg), 0.f), PanelDark, true, CarRoot), true);
	}
	Solidify(Piece(CylinderMesh, FVector(0, 0, -4.0), FVector(2 * R + 46.0, 2 * R + 46.0, 8.0), FRotator::ZeroRotator, Carpet, true, CarRoot), true);
	Solidify(Piece(CylinderMesh, FVector(0, 0, H + 4.0), FVector(2 * R + 14.0, 2 * R + 14.0, 8.0), FRotator::ZeroRotator, PanelGrey, false, CarRoot), false);
	Solidify(Piece(CylinderMesh, FVector(0, 0, H - 1.5), FVector(110.0, 110.0, 3.0), FRotator::ZeroRotator, StripWhite, false, CarRoot), false);
	Solidify(Piece(CubeMesh, FVector((R - 5.0) * FMath::Cos(FMath::DegreesToRadians(60.0)), (R - 5.0) * FMath::Sin(FMath::DegreesToRadians(60.0)), 130.0), FVector(3.0, 40.0, 30.0), FRotator(0.f, 60.f, 0.f), Touch, false, CarRoot), false);
	if (UPointLightComponent* L = Light(FVector(0, 0, H - 30.0), 10.f, 500.f, FLinearColor(0.95f, 0.96f, 1.f), CarRoot)) { CarPieces.Add(L); }
	SetCarShown(false);
}

void UShipInteriorComponent::SetCarShown(bool bShown)
{
	bCarShown = bShown;
	for (const TWeakObjectPtr<USceneComponent>& P : CarPieces) { if (USceneComponent* C = P.Get()) { C->SetVisibility(bShown); } }
	for (const TWeakObjectPtr<UPrimitiveComponent>& P : CarSolids) { if (UPrimitiveComponent* C = P.Get()) { C->SetCollisionEnabled(bShown ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision); } }
}

void UShipInteriorComponent::MoveCarTo(int32 Stop)
{
	if (!Plan.Stops.IsValidIndex(Stop) || !CarRoot) { return; }
	const FPlanStop& S = Plan.Stops[Stop];
	CarRoot->SetRelativeLocationAndRotation(S.Car, FRotationMatrix::MakeFromXZ(S.Facing.GetSafeNormal2D(), FVector::UpVector).ToQuat());
	CarStop = Stop;
	if (FShipRoom* Car = Rooms.IsValidIndex(LiftPlaceIndex) ? &Rooms[LiftPlaceIndex] : nullptr)
	{
		Car->Center = S.Car + FVector(0, 0, SlonkPlan::CorrH / 2);
		Car->Yaw = FMath::Atan2(S.Facing.Y, S.Facing.X);   // the box turned with the doors, not the deck's axes
		Car->Deck = Plan.Decks.IsValidIndex(S.Deck) ? Plan.Decks[S.Deck].Label() : FString();
		Car->Stop = Stop;
		Car->Lift = InteriorSpot(S.Car, S.Facing);
		Car->Use = Car->Lift;
		Car->UseLabel = TEXT("choose a deck");
	}
}

void UShipInteriorComponent::SetStopDoor(int32 Stop, float Open)
{
	FStopDoor* D = StopDoors.Find(Stop);
	if (!D) { return; }
	D->Open = FMath::Clamp(Open, 0.f, 1.f);
	const bool bShut = D->Open < 0.98f;
	const FZone* Z = Zones.IsValidIndex(D->Zone) ? &Zones[D->Zone] : nullptr;
	for (int32 k = 0; k < 2; ++k)
	{
		UStaticMeshComponent* Leaf = D->Leaf[k];
		if (!Leaf) { continue; }
		Leaf->SetRelativeLocation(D->Shut[k] + D->Along * ((k == 0 ? -1.0 : 1.0) * D->Open * (SlonkPlan::DoorW / 2 + 2.0)));
		Leaf->SetVisibility(bShut && Z && Z->bShown);
		Leaf->SetCollisionEnabled(bShut && Z && Z->bSolidOn ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
}

int32 UShipInteriorComponent::StopFor(int32 PlaceIndex, const FVector& Feet) const
{
	const FShipRoom* R = Place(PlaceIndex);
	if (!R) { return INDEX_NONE; }
	if (R->Stop != INDEX_NONE) { return R->Stop; }
	int32 Best = INDEX_NONE; double BestD = 1e18;
	for (int32 i = 0; i < Plan.Stops.Num(); ++i)
	{
		const FPlanStop& S = Plan.Stops[i];
		if (R->Stretch == INDEX_NONE || S.Stretch != R->Stretch) { continue; }
		const double D = FVector::DistSquared2D(S.Car, Feet);
		if (D < BestD) { BestD = D; Best = i; }
	}
	if (Best != INDEX_NONE) { return Best; }
	// Nothing in this passage: the nearest stop on the deck.
	for (int32 i = 0; i < Plan.Stops.Num(); ++i)
	{
		const FPlanStop& S = Plan.Stops[i];
		if (!Plan.Decks.IsValidIndex(S.Deck) || Plan.Decks[S.Deck].Label() != R->Deck) { continue; }
		const double D = FVector::DistSquared2D(S.Car, Feet);
		if (D < BestD) { BestD = D; Best = i; }
	}
	return Best;
}

int32 UShipInteriorComponent::StopAtDoor(int32 PlaceIndex, const FVector& Feet, double Reach) const
{
	const FShipRoom* R = Place(PlaceIndex);
	if (!R) { return INDEX_NONE; }
	if (R->Stop != INDEX_NONE) { return FVector::Dist(Feet, R->Lift.GetLocation()) <= Reach ? R->Stop : INDEX_NONE; }
	for (int32 i = 0; i < Plan.Stops.Num(); ++i)
	{
		const FPlanStop& S = Plan.Stops[i];
		if (S.Stretch != R->Stretch || R->Stretch == INDEX_NONE) { continue; }
		if (FVector::Dist2D(Feet, S.Car + S.Facing * 140.0) <= Reach) { return i; }
	}
	return INDEX_NONE;
}

FTransform UShipInteriorComponent::StopDoor(int32 Stop) const
{
	if (!Plan.Stops.IsValidIndex(Stop)) { return FTransform::Identity; }
	const FPlanStop& S = Plan.Stops[Stop];
	return InteriorSpot(S.Car + S.Facing * 210.0, -S.Facing);
}

bool UShipInteriorComponent::OpenLift(int32 Stop)
{
	if (!Plan.Stops.IsValidIndex(Stop) || !Plan.Stops[Stop].bWalkIn || IsRiding()) { return false; }
	if (CarStop != Stop || !bCarShown)
	{
		if (CarStop != INDEX_NONE && CarStop != Stop) { SetStopDoor(CarStop, 0.f); }   // the doors it stood open at shut behind it
		MoveCarTo(Stop); SetCarShown(true);
		RidePhase = ERide::Idle;   // whatever it stood at before, this stop's doors have still to open
	}
	if (RidePhase == ERide::Open) { IdleT = 0.0; return true; }
	RidePhase = ERide::Opening; RideT = 0.0;
	SetComponentTickEnabled(true);
	return true;
}

void UShipInteriorComponent::BoardAndRide(APersonPawn* Who, int32 FromStop, int32 ToStop)
{
	AStarshipPawn* Ship = Cast<AStarshipPawn>(GetOwner());
	if (!Ship || !Who || !Plan.Stops.IsValidIndex(FromStop)) { return; }
	MoveCarTo(FromStop);
	SetCarShown(true);
	SetStopDoor(FromStop, 0.f);
	const FPlanStop& S = Plan.Stops[FromStop];
	Who->PlaceAboard(Ship, LiftPlaceIndex, InteriorSpot(S.Car, S.Facing) * Ship->CraftFrame());
	// The car is the place shown now: the room left behind (the bridge set's floor among them) lets go of the feet.
	Ship->SetInteriorPlace(LiftPlaceIndex);
	Ride(Who, ToStop);
}

bool UShipInteriorComponent::Ride(APersonPawn* Who, int32 ToStop)
{
	if (!Plan.Stops.IsValidIndex(ToStop) || CarStop == INDEX_NONE || ToStop == CarStop || IsRiding()) { return false; }
	Rider = Who;
	RideTo = ToStop;
	RideDuration = FShipPlan::TravelSeconds(Plan.Stops[CarStop], Plan.Stops[ToStop]);
	RidePhase = ERide::Closing; RideT = 0.0; PassedDeck = INDEX_NONE;
	SetComponentTickEnabled(true);
	UE_LOG(LogTemp, Log, TEXT("Turbolift: %s -> %s, %.1f s"), *Plan.Stops[CarStop].Name, *Plan.Stops[ToStop].Name, RideDuration);
	return true;
}

void UShipInteriorComponent::TickRide(float DeltaTime)
{
	const double DoorSeconds = 0.7;
	AStarshipPawn* Ship = Cast<AStarshipPawn>(GetOwner());
	APersonPawn* Who = Rider.Get();
	auto Aboard = [this](const APersonPawn* P) { return P && P->IsAboard() && P->GetPlace() == LiftPlaceIndex; };
	switch (RidePhase)
	{
	case ERide::Idle:
		SetComponentTickEnabled(false);
		return;
	case ERide::Open:
	{
		// Standing open: once nobody is in the car for a moment the doors shut and the car goes.
		// Standing open: the doors wait while anyone is in the car or near its door, and shut a few seconds after.
		const APersonPawn* P = Ship ? Ship->GetActivePerson() : nullptr;
		const FVector CarAt = Plan.Stops.IsValidIndex(CarStop) ? Plan.Stops[CarStop].Car : FVector::ZeroVector;
		const bool bNear = P && P->IsAboard() && (P->GetPlace() == LiftPlaceIndex || FVector::Dist2D(P->FeetInCraft(), CarAt) < 400.0);
		IdleT = bNear ? 0.0 : IdleT + DeltaTime;
		if (IdleT > 6.0) { RidePhase = ERide::Closing; RideT = 0.0; RideTo = INDEX_NONE; }
		return;
	}
	case ERide::Closing:
		RideT += DeltaTime;
		SetStopDoor(CarStop, 1.f - (float)(RideT / DoorSeconds));
		if (RideTo != INDEX_NONE && Who)
		{
			// The doors' sensor: a rider who steps back into the doorway, or out of the car, while they shut has
			// them open again and the ride is off; the car waits open as it did before.
			const FPlanStop& At = Plan.Stops[CarStop];
			const bool bInDoorway = Aboard(Who) && FVector::DotProduct(Who->FeetInCraft() - At.Car, At.Facing.GetSafeNormal2D()) > 100.0;
			if (!Aboard(Who) || bInDoorway)
			{
				UE_LOG(LogTemp, Log, TEXT("Turbolift: the rider %s; the doors open again"), bInDoorway ? TEXT("stands in the doorway") : TEXT("left the car"));
				RideTo = INDEX_NONE; Rider = nullptr;
				RidePhase = ERide::Opening; RideT = FMath::Clamp(DoorSeconds - RideT, 0.0, DoorSeconds);
				return;
			}
		}
		if (RideT < DoorSeconds) { return; }
		SetStopDoor(CarStop, 0.f);
		if (RideTo == INDEX_NONE) { SetCarShown(false); RidePhase = ERide::Idle; return; }
		RidePhase = ERide::Travelling; RideT = 0.0;
		return;
	case ERide::Travelling:
	{
		RideT += DeltaTime;
		const FPlanStop& From = Plan.Stops[CarStop]; const FPlanStop& To = Plan.Stops[RideTo];
		// The decks going by, called off as the car passes their floors.
		const double Zc = FMath::Lerp(From.Car.Z, To.Car.Z, FMath::Clamp(RideT / RideDuration, 0.0, 1.0));
		const double ZPrev = FMath::Lerp(From.Car.Z, To.Car.Z, FMath::Clamp((RideT - DeltaTime) / RideDuration, 0.0, 1.0));
		for (int32 d = 0; d < Plan.Decks.Num(); ++d)
		{
			if (d == To.Deck || d == From.Deck) { continue; }
			const double F = Plan.Decks[d].Floor;
			if ((ZPrev - F) * (Zc - F) <= 0.0 && ZPrev != Zc && d != PassedDeck)
			{
				PassedDeck = d;
				if (OnLiftMessage) { OnLiftMessage(FString::Printf(TEXT("Deck %s"), *Plan.Decks[d].Label())); }
			}
		}
		if (RideT < RideDuration) { return; }
		// Arrival: the car moves to the stop, and whoever still stands in it with it; the doors open where they
		// are the plan's. A rider who is no longer in the car (left it, or is away) is not touched and the shown
		// interior stays theirs.
		const bool bRider = Aboard(Who) && Ship;
		FVector Off = FVector::ZeroVector; FQuat Heading = FQuat::Identity;
		if (bRider)
		{
			Off = Who->FeetInCraft() - From.Car;
			Off.Z = 0.0;   // on the car's floor, whatever they stood on when they boarded
			Heading = Ship->CraftFrame().InverseTransformRotation(Who->GetActorQuat());
		}
		const FQuat Turn = FQuat::FindBetweenNormals(From.Facing.GetSafeNormal2D(), To.Facing.GetSafeNormal2D());
		MoveCarTo(RideTo);
		if (bRider)
		{
			const FVector Feet = To.Car + Turn.RotateVector(Off);
			Who->PlaceAboard(Ship, LiftPlaceIndex, FTransform(Turn * Heading, Feet) * Ship->CraftFrame());
			Ship->SetInteriorPlace(LiftPlaceIndex);
		}
		RideTo = INDEX_NONE;
		if (To.bWalkIn) { RidePhase = ERide::Opening; RideT = 0.0; }
		else { RidePhase = ERide::Idle; SetCarShown(false); }
		if (OnArrived) { OnArrived(CarStop, bRider ? Who : nullptr); }
		return;
	}
	case ERide::Opening:
		RideT += DeltaTime;
		SetStopDoor(CarStop, (float)(RideT / DoorSeconds));
		if (RideT < DoorSeconds) { return; }
		SetStopDoor(CarStop, 1.f);
		RidePhase = ERide::Open; IdleT = 0.0;
		return;
	}
}
