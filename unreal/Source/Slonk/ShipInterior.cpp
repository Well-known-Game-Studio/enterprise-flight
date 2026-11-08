// Slonk - the walkable rooms of the ship (see ShipInterior.h).
#include "ShipInterior.h"
#include "StarshipPawn.h"
#include "SlonkMaterials.h"
#include "Components/StaticMeshComponent.h"
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


UShipInteriorComponent::UShipInteriorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
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
	default: return TEXT("");
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

bool UShipInteriorComponent::InRoom(const FShipRoom& R, const FVector& P, double Tol)
{
	if (R.Half.IsNearlyZero()) { return false; }
	const FVector D = (P - R.Center).GetAbs();
	if (D.X <= R.Half.X + Tol && D.Y <= R.Half.Y + Tol && D.Z <= R.Half.Z + Tol) { return true; }
	for (const FBox& B : R.Legs) { if (B.ExpandBy(Tol).IsInside(P)) { return true; } }
	return false;
}

EShipRoom UShipInteriorComponent::RoomAt(const FVector& P, EShipRoom Current) const
{
	if (const FShipRoom* C = Room(Current)) { if (InRoom(*C, P, 30.0)) { return Current; } }   // a wall's thickness and a step: through a doorway the room beyond takes over
	// The room the point is deepest in (the smallest tolerance that holds it), so a corridor's doorway counts as the
	// room beyond once the walker is through it rather than at the first touch of the box.
	for (double Tol : { 0.0, 50.0, 100.0 })
	{
		for (const FShipRoom& R : Rooms) { if (InRoom(R, P, Tol)) { return R.Id; } }
	}
	return EShipRoom::Count;
}

void UShipInteriorComponent::SetRoomSpots(EShipRoom Which, const FTransform& Lift, const FTransform& Use, const FString& UseLabel)
{
	for (FShipRoom& R : Rooms) { if (R.Id == Which) { R.Lift = Lift; R.Use = Use; R.UseLabel = UseLabel; } }
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
	UMaterialInterface* Touch = MakeScreen(TEXT("MeshTouch"), 7.f, 1.2f);
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
		else if (Name == TEXT("TouchPanel")) { M = Touch; }
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
	const bool bIn = ShownRoom == EShipRoom::ShuttleBay;
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

UStaticMeshComponent* UShipInteriorComponent::Piece(UStaticMesh* Mesh, const FVector& Center, const FVector& Size, const FRotator& Rot, UMaterialInterface* Mat, bool bSolid)
{
	if (!Mesh || !GetOwner()) { return nullptr; }
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(GetOwner(), *FString::Printf(TEXT("Interior_%d"), PieceCount++));
	C->SetupAttachment(this);
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

void UShipInteriorComponent::Light(const FVector& Pos, float Intensity, float Radius, FLinearColor Color)
{
	if (!GetOwner()) { return; }
	UPointLightComponent* L = NewObject<UPointLightComponent>(GetOwner(), *FString::Printf(TEXT("InteriorLight_%d"), PieceCount++));
	L->SetupAttachment(this);
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

	PanelGrey = MakeLit(TEXT("PanelGrey"), FLinearColor(0.62f, 0.63f, 0.66f), 0.75f);
	PanelDark = MakeLit(TEXT("PanelDark"), FLinearColor(0.16f, 0.17f, 0.19f), 0.6f);
	Carpet = MakeLit(TEXT("Carpet"), FLinearColor(0.22f, 0.16f, 0.17f), 0.95f);
	DeckPlate = MakeLit(TEXT("DeckPlate"), FLinearColor(0.35f, 0.36f, 0.38f), 0.55f, 0.3f);
	Burgundy = MakeLit(TEXT("Burgundy"), FLinearColor(0.32f, 0.07f, 0.09f), 0.6f);
	StripWhite = MakeLit(TEXT("StripWhite"), FLinearColor(0.9f, 0.92f, 0.95f), 0.8f, 0.f, FLinearColor(0.95f, 0.97f, 1.0f), 6.f);
	ConsoleBlack = MakeLit(TEXT("ConsoleBlack"), FLinearColor(0.05f, 0.05f, 0.06f), 0.4f, 0.2f);
	Teal = MakeLit(TEXT("Teal"), FLinearColor(0.02f, 0.08f, 0.1f), 0.5f, 0.f, FLinearColor(0.2f, 0.9f, 0.95f), 2.5f);
	CoreBlue = MakeLit(TEXT("CoreBlue"), FLinearColor(0.1f, 0.2f, 0.35f), 0.3f, 0.f, FLinearColor(0.3f, 0.65f, 1.0f), 5.f);
	BedWhite = MakeLit(TEXT("BedWhite"), FLinearColor(0.85f, 0.86f, 0.88f), 0.7f);
	HullGrey = MakeLit(TEXT("HullGrey"), FLinearColor(0.72f, 0.73f, 0.75f), 0.5f, 0.2f);
	PadGlow = MakeLit(TEXT("PadGlow"), FLinearColor(0.5f, 0.6f, 0.7f), 0.4f, 0.f, FLinearColor(0.75f, 0.85f, 1.0f), 3.f);

	const double X = Ship->GetShipLengthCm() / 30500.0;
	const FVector Bay = Ship->ShuttleBayOffset;
	auto Add = [&](EShipRoom Id, const FVector& Center, const FVector& Half)
	{
		FShipRoom R; R.Id = Id; R.Name = RoomName(Id); R.Deck = DeckOf(Id); R.Center = Center; R.Half = Half;
		return Rooms.Add(R);
	};
	Add(EShipRoom::Bridge, FVector(8000.0, 0.0, 3230.0) * X, FVector::ZeroVector); // the set's own box; spots set by the pawn
	const int32 Eng = Add(EShipRoom::Engineering, FVector(-900.0, 0.0, -1900.0) * X, FVector(800.0, 500.0, 225.0));
	const int32 Sick = Add(EShipRoom::Sickbay, FVector(6600.0, 0.0, 1900.0) * X, FVector(600.0, 450.0, 150.0));
	// The straight bay's box begins at the clamshell door's axis (9.2 m forward of the door point) and runs 26 m
	// forward, 5.4 m tall: the hull's side skin at the stern wall (1.55 m forward of the axis, where the Blender
	// set's shell ends) is only 5.8 m over the deck at the bay's width. Its deck is 2 cm above the hull's own fan at
	// the fantail; the throat behind it, over the fan inside the door's base ring, keeps the door's 7 m.
	// (The door point itself sits 2.5 m over the deck, the shuttle's taxi height: her 3 m hull then clears the roof truss.)
	const double BayDeck = -1946.0;
	const int32 BayRoom = Add(EShipRoom::ShuttleBay, FVector(Bay.X + 2220.0 * X, 0.0, BayDeck + 270.0), FVector(1300.0, 800.0, 270.0));
	const int32 Tr = Add(EShipRoom::Transporter, FVector(7200.0, -1500.0, 1900.0) * X, FVector(400.0, 350.0, 150.0));
	// The airlock's staging area sits two metres under the saucer hatch (the hull trace at a quarter length forward
	// lands on the bridge module's roof at z ~3374): deck B, with the lift aft and the hatch forward.
	const int32 Lock = Add(EShipRoom::Airlock, FVector(7625.0, 0.0, 3023.0) * X, FVector(300.0, 250.0, 150.0));
	// Deck G's corridor: an L whose athwartships leg runs past sickbay's aft door (its box) and whose forward leg
	// runs along the port side past the transporter room's door (a second box); the rec deck opens off the
	// athwartships leg to port. Their walls are back to back, so the boxes touch and the rooms are neighbours.
	const double GFloor = 1900.0 * X - 150.0;
	const int32 Corr = Add(EShipRoom::Corridor, FVector(5880.0 * X, -470.0, GFloor + 130.0), FVector(120.0, 680.0, 130.0));
	Rooms[Corr].Legs.Add(FBox(FVector(6000.0 * X, -1150.0, GFloor), FVector(7700.0 * X, -910.0, GFloor + 260.0)));
	const int32 Rec = Add(EShipRoom::RecDeck, FVector(4830.0 * X, -200.0, GFloor + 300.0), FVector(900.0, 700.0, 300.0));
	const int32 MessR = Add(EShipRoom::Mess, FVector(9300.0, 1700.0, 2600.0) * X + FVector(0, 0, 140.0), FVector(550.0, 350.0, 140.0));
	const int32 Cab = Add(EShipRoom::Quarters, FVector(9900.0, -1200.0, 2300.0) * X + FVector(0, 0, 140.0), FVector(450.0, 350.0, 140.0));
	const int32 Crew = Add(EShipRoom::CrewQuarters, FVector(11010.0, 0.0, 2000.0) * X + FVector(0, 0, 130.0), FVector(340.0, 800.0, 130.0));
	const FVector Tube = Ship->TorpedoTubeCentre;
	const int32 Torp = Add(EShipRoom::TorpedoBay, FVector(Tube.X - 800.0 * X, 0.0, Tube.Z), FVector(800.0, 350.0, 225.0));
	const int32 Cargo = Add(EShipRoom::CargoBay, FVector(-4200.0, 0.0, -2600.0) * X, FVector(1000.0, 600.0, 350.0));

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
	// Neighbours: rooms whose boxes come within a wall's thickness of each other.
	auto Boxes = [](const FShipRoom& R)
	{
		TArray<FBox> B; if (!R.Half.IsNearlyZero()) { B.Add(FBox(R.Center - R.Half, R.Center + R.Half)); } B.Append(R.Legs); return B;
	};
	for (FShipRoom& A : Rooms)
	{
		for (const FShipRoom& B : Rooms)
		{
			if (A.Id == B.Id || A.Id == EShipRoom::Bridge || B.Id == EShipRoom::Bridge) { continue; }
			bool bTouch = false;
			for (const FBox& BA : Boxes(A)) { for (const FBox& BB : Boxes(B)) { if (BA.ExpandBy(40.0).Intersect(BB)) { bTouch = true; } } }
			if (bTouch) { A.Neighbours.Add(B.Id); }
		}
	}
	ShownRoom = EShipRoom::Count;
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
	R.UseLabel = FString();
	if (RoomMesh(CorridorMesh, Origin))
	{
		for (double Y : { 100.0, -300.0, -700.0, -1050.0 }) { Light(Origin + FVector(0, Y, 240.0), 18.f, 500.f, FLinearColor(0.95f, 0.97f, 1.f)); }
		for (double Xo : { 400.0, 900.0, 1400.0, 1780.0 }) { Light(Origin + FVector(Xo, -1030.0, 240.0), 18.f, 500.f, FLinearColor(0.95f, 0.97f, 1.f)); }
		return;
	}
	// Primitives: the two legs' floors, ceilings and walls, the lift door at the forward end, the doorway frames.
	const FBox LegB = R.Legs.Num() ? R.Legs[0] : FBox(C - H, C + H);
	const double T = 20.0;
	const double TrX = 7336.0 * (C.X / 5880.0);   // the transporter room's door along the forward leg (TRANS_DOOR_X)
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
	Box(FVector(C.X, LegB.Min.Y - T / 2, C.Z), FVector(2 * H.X + 2 * T, T, 2 * H.Z), PanelGrey);   // the outer corner
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

void UShipInteriorComponent::ShowRoom(EShipRoom Which)
{
	ShownRoom = Which;
	const FShipRoom* Here = Room(Which);
	for (FShipRoom& R : Rooms)
	{
		const bool bIn = R.Id == Which || (Here && Here->Neighbours.Contains(R.Id));
		const bool bOn = bIn || (R.Id == EShipRoom::ShuttleBay && bBayFromOutside);
		for (const TWeakObjectPtr<USceneComponent>& P : R.Pieces) { if (USceneComponent* S = P.Get()) { S->SetVisibility(bOn); } }
		for (const TWeakObjectPtr<UPrimitiveComponent>& P : R.Solids) { if (UPrimitiveComponent* S = P.Get()) { S->SetCollisionEnabled(bIn ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision); } }
	}
	SetBayShuttleShown(bBayShuttleShown);
}

void UShipInteriorComponent::SetBayShownFromOutside(bool bShown)
{
	if (bBayFromOutside == bShown) { return; }
	bBayFromOutside = bShown;
	ShowRoom(ShownRoom);
}

bool UShipInteriorComponent::GetRoomBox(EShipRoom Room, FVector& OutCenter, FVector& OutHalf) const
{
	for (const FShipRoom& R : Rooms)
	{
		if (R.Id == Room && !R.Half.IsNearlyZero()) { OutCenter = R.Center; OutHalf = R.Half; return true; }
	}
	return false;
}
