// Slonk - the walkable rooms of the ship (see ShipInterior.h).
#include "ShipInterior.h"
#include "StarshipPawn.h"
#include "SlonkMaterials.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

UShipInteriorComponent::UShipInteriorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CubeFinder.Succeeded()) { CubeMesh = CubeFinder.Object; }
	if (CylFinder.Succeeded()) { CylinderMesh = CylFinder.Object; }
	LitMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_BridgeLit.M_BridgeLit")));
	ButtonsMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_BridgeButtons.M_BridgeButtons")));
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
	default: return TEXT("");
	}
}

FString UShipInteriorComponent::DeckOf(EShipRoom Which)
{
	switch (Which)
	{
	case EShipRoom::Bridge: return TEXT("A");
	case EShipRoom::Airlock: return TEXT("B");
	case EShipRoom::Sickbay: return TEXT("G");
	case EShipRoom::Transporter: return TEXT("G");
	case EShipRoom::Engineering: return TEXT("O");
	case EShipRoom::ShuttleBay: return TEXT("Q");
	default: return TEXT("");
	}
}

const FShipRoom* UShipInteriorComponent::Room(EShipRoom Which) const
{
	for (const FShipRoom& R : Rooms) { if (R.Id == Which) { return &R; } }
	return nullptr;
}

EShipRoom UShipInteriorComponent::RoomAt(const FVector& P) const
{
	for (const FShipRoom& R : Rooms)
	{
		if (R.Half.IsNearlyZero()) { continue; }
		const FVector D = (P - R.Center).GetAbs();
		if (D.X <= R.Half.X + 100.0 && D.Y <= R.Half.Y + 100.0 && D.Z <= R.Half.Z + 100.0) { return R.Id; }
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

void UShipInteriorComponent::Shell(const FShipRoom& R, const FVector& LiftDoor, const FVector& LiftNormal, bool bOpenAft)
{
	const FVector& C = R.Center; const FVector& H = R.Half;
	const double T = 20.0;
	Box(FVector(C.X, C.Y, C.Z - H.Z - T / 2), FVector(2 * H.X + 2 * T, 2 * H.Y + 2 * T, T), R.Id == EShipRoom::ShuttleBay ? DeckPlate : Carpet);
	Box(FVector(C.X, C.Y, C.Z + H.Z + T / 2), FVector(2 * H.X + 2 * T, 2 * H.Y + 2 * T, T), PanelGrey);
	Box(FVector(C.X + H.X + T / 2, C.Y, C.Z), FVector(T, 2 * H.Y + 2 * T, 2 * H.Z), PanelGrey);
	if (!bOpenAft) { Box(FVector(C.X - H.X - T / 2, C.Y, C.Z), FVector(T, 2 * H.Y + 2 * T, 2 * H.Z), PanelGrey); }
	Box(FVector(C.X, C.Y - H.Y - T / 2, C.Z), FVector(2 * H.X, T, 2 * H.Z), PanelGrey);
	Box(FVector(C.X, C.Y + H.Y + T / 2, C.Z), FVector(2 * H.X, T, 2 * H.Z), PanelGrey);
	// A dark skirting and a lit cove where the walls meet the ceiling.
	for (double Side : { -1.0, 1.0 })
	{
		Box(FVector(C.X, C.Y + Side * (H.Y - 3.0), C.Z - H.Z + 10.0), FVector(2 * H.X - 2.0, 6.0, 20.0), PanelDark, 0.f, false);
		Box(FVector(C.X, C.Y + Side * (H.Y - 4.0), C.Z + H.Z - 6.0), FVector(2 * H.X - 8.0, 8.0, 5.0), StripWhite, 0.f, false);
	}
	// The lift door, set into its wall, with a lighter frame round it.
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
	AllPieces.Reset(); Materials.Reset(); Rooms.Reset(); PieceCount = 0;

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
	const int32 BayRoom = Add(EShipRoom::ShuttleBay, FVector(Bay.X + 1300.0 * X, 0.0, Bay.Z), FVector(1300.0, 800.0, 325.0));
	const int32 Tr = Add(EShipRoom::Transporter, FVector(7200.0, -1500.0, 1900.0) * X, FVector(400.0, 350.0, 150.0));
	Add(EShipRoom::Airlock, FVector::ZeroVector, FVector::ZeroVector);

	Building = &Rooms[Eng]; BuildEngineering(Rooms[Eng]);
	Building = &Rooms[Sick]; BuildSickbay(Rooms[Sick]);
	Building = &Rooms[BayRoom]; BuildShuttleBay(Rooms[BayRoom]);
	Building = &Rooms[Tr]; BuildTransporter(Rooms[Tr]);
	Building = nullptr;
	ShownRoom = EShipRoom::Count;
}

namespace
{
	FTransform InteriorSpot(const FVector& Feet, const FVector& Facing)
	{
		return FTransform(FRotationMatrix::MakeFromXZ(Facing.GetSafeNormal(), FVector::UpVector).ToQuat(), Feet);
	}
}

void UShipInteriorComponent::BuildEngineering(FShipRoom& R)
{
	const FVector C = R.Center, H = R.Half;
	const double Floor = C.Z - H.Z;
	const FVector Door(C.X + H.X, C.Y + 300.0, Floor);
	Shell(R, Door, FVector(-1, 0, 0), false);
	R.Lift = InteriorSpot(Door + FVector(-90.0, 0, 0), FVector(-1, 0, 0));
	// The warp core: a lit column from deck to ceiling, ringed, on a dark base.
	const FVector Core(C.X - 250.0, C.Y, 0.0);
	Cyl(FVector(Core.X, Core.Y, C.Z), 90.f, (float)(2 * H.Z), CoreBlue);
	Cyl(FVector(Core.X, Core.Y, Floor + 20.0), 135.f, 40.f, PanelDark);
	for (double F : { 0.33, 0.66 }) { Cyl(FVector(Core.X, Core.Y, Floor + 2 * H.Z * F), 108.f, 26.f, PanelDark, false); }
	// The master console faces the core; the chief's board opens from it.
	Box(FVector(C.X + 120.0, C.Y, Floor + 45.0), FVector(200.0, 110.0, 90.0), ConsoleBlack);
	Box(FVector(C.X + 120.0, C.Y, Floor + 91.0), FVector(184.0, 94.0, 2.0), Teal, 0.f, false);
	R.Use = InteriorSpot(FVector(C.X + 120.0 + 55.0 + 45.0, C.Y, Floor), FVector(-1, 0, 0));
	R.UseLabel = TEXT("open the engineering board");
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
	const FVector Door(C.X - H.X, C.Y - 250.0, Floor);
	Shell(R, Door, FVector(1, 0, 0), false);
	R.Lift = InteriorSpot(Door + FVector(90.0, 0, 0), FVector(1, 0, 0));
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
	R.Use = InteriorSpot(FVector(C.X + 320.0 - 85.0, C.Y + 200.0, Floor), FVector(1, 0, 0));
	R.UseLabel = TEXT("open the sickbay board");
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
	Shell(R, Door, FVector(-1, 0, 0), true);
	R.Lift = InteriorSpot(Door + FVector(-90.0, 0, 0), FVector(-1, 0, 0));
	// The bay door frame, open to space astern.
	for (double Side : { -1.0, 1.0 }) { Box(FVector(C.X - H.X + 30.0, C.Y + Side * (H.Y - 30.0), C.Z), FVector(60.0, 60.0, 2 * H.Z), PanelDark); }
	Box(FVector(C.X - H.X + 30.0, C.Y, C.Z + H.Z - 40.0), FVector(60.0, 2 * H.Y, 80.0), PanelDark);
	Box(FVector(C.X - H.X + 61.0, C.Y, C.Z + H.Z - 40.0), FVector(2.0, 2 * H.Y - 140.0, 10.0), StripWhite, 0.f, false);
	// Guide lines down the deck to the door.
	for (double Dy : { -220.0, 220.0 }) { Box(FVector(C.X - 200.0, C.Y + Dy, Floor + 1.0), FVector(2 * H.X - 500.0, 12.0, 2.0), MakeLit(TEXT("BayLine"), FLinearColor(0.8f, 0.7f, 0.1f), 0.8f, 0.f, FLinearColor(1.f, 0.85f, 0.2f), 1.2f), 0.f, false); }
	// The parked shuttle: hull on its skids, nacelles, canopy. The real one is spawned when it leaves.
	const FVector S(C.X + 150.0, C.Y, Floor);
	for (double Side : { -1.0, 1.0 }) { Box(S + FVector(0, Side * 140.0, 20.0), FVector(520.0, 30.0, 40.0), PanelDark); }
	Box(S + FVector(0, 0, 40.0 + 95.0), FVector(900.0, 300.0, 190.0), HullGrey);
	Box(S + FVector(370.0, 0, 40.0 + 150.0), FVector(170.0, 240.0, 70.0), PanelDark, 0.f, false);
	for (double Side : { -1.0, 1.0 })
	{
		Box(S + FVector(-60.0, Side * 200.0, 60.0), FVector(640.0, 72.0, 72.0), HullGrey);
		Box(S + FVector(-60.0, Side * 200.0, 60.0 + 20.0), FVector(300.0, 74.0, 14.0), MakeLit(TEXT("NacelleGlow"), FLinearColor(0.2f, 0.3f, 0.6f), 0.5f, 0.f, FLinearColor(0.3f, 0.5f, 1.f), 1.5f), 0.f, false);
	}
	Box(S + FVector(-20.0, -151.0, 40.0 + 100.0), FVector(90.0, 2.0, 150.0), PanelDark, 0.f, false); // the port door
	R.Use = InteriorSpot(S + FVector(-20.0, -150.0 - 170.0, 0.0), FVector(0, 1, 0));
	R.UseLabel = TEXT("board the shuttle");
	for (double Dx : { -800.0, 0.0, 800.0 }) { for (double Dy : { -400.0, 400.0 }) { Light(FVector(C.X + Dx, C.Y + Dy, C.Z + H.Z - 40.0), 160.f, 1800.f, FLinearColor(0.95f, 0.97f, 1.f)); } }
}

void UShipInteriorComponent::BuildTransporter(FShipRoom& R)
{
	const FVector C = R.Center, H = R.Half;
	const double Floor = C.Z - H.Z;
	const FVector Door(C.X + 150.0, C.Y + H.Y, Floor);
	Shell(R, Door, FVector(0, -1, 0), false);
	R.Lift = InteriorSpot(Door + FVector(0, -90.0, 0), FVector(0, -1, 0));
	// The pad: a low round platform with six lit discs, a glowing wall behind it.
	const FVector Pad(C.X - 150.0, C.Y, Floor);
	Cyl(Pad + FVector(0, 0, 9.0), 170.f, 18.f, PanelGrey);
	for (int32 i = 0; i < 6; ++i)
	{
		const float A = FMath::DegreesToRadians(60.f * i);
		Cyl(Pad + FVector(FMath::Cos(A) * 110.0, FMath::Sin(A) * 110.0, 19.0), 28.f, 3.f, PadGlow, false);
	}
	Cyl(Pad + FVector(0, 0, 19.0), 30.f, 3.f, PadGlow, false);
	Box(Pad + FVector(-172.0, 0, 130.0), FVector(8.0, 320.0, 240.0), PanelDark);
	Box(Pad + FVector(-167.0, 0, 130.0), FVector(2.0, 300.0, 220.0), MakeLit(TEXT("PadWall"), FLinearColor(0.3f, 0.4f, 0.55f), 0.5f, 0.f, FLinearColor(0.55f, 0.7f, 1.f), 1.6f), 0.f, false);
	R.Use = InteriorSpot(Pad + FVector(0, 0, 18.0), FVector(1, 0, 0));
	R.UseLabel = TEXT("energise");
	// The operator's console faces the pad.
	Console(FVector(C.X + 250.0, C.Y, Floor), 180.f, 220.f, 12.f);
	Light(FVector(Pad.X, Pad.Y, C.Z + H.Z - 25.0), 80.f, 900.f, FLinearColor(0.75f, 0.85f, 1.f));
	Light(FVector(C.X + 200.0, C.Y, C.Z + H.Z - 25.0), 70.f, 900.f, FLinearColor(0.97f, 0.98f, 1.f));
}

void UShipInteriorComponent::ShowRoom(EShipRoom Which)
{
	ShownRoom = Which;
	for (FShipRoom& R : Rooms)
	{
		const bool bOn = R.Id == Which;
		for (const TWeakObjectPtr<USceneComponent>& P : R.Pieces) { if (USceneComponent* S = P.Get()) { S->SetVisibility(bOn); } }
		for (const TWeakObjectPtr<UPrimitiveComponent>& P : R.Solids) { if (UPrimitiveComponent* S = P.Get()) { S->SetCollisionEnabled(bOn ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision); } }
	}
}
