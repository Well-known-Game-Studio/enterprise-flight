#include "BridgeInteriorComponent.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "SlonkMaterials.h"
#include "UDynamicMesh.h"
#include "UObject/ConstructorHelpers.h"

UBridgeInteriorComponent::UBridgeInteriorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (CubeFinder.Succeeded()) { CubeMesh = CubeFinder.Object; }
	if (CylFinder.Succeeded()) { CylinderMesh = CylFinder.Object; }
	if (SphereFinder.Succeeded()) { SphereMesh = SphereFinder.Object; }

	LitMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_BridgeLit.M_BridgeLit")));
	DisplayMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_BridgeDisplay.M_BridgeDisplay")));
	ButtonsMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_BridgeButtons.M_BridgeButtons")));
	ViewerMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_BridgeViewer.M_BridgeViewer")));
}

void UBridgeInteriorComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!IsBuilt())
	{
		Build();
	}
}

// ---- Materials --------------------------------------------------------------------------------

UMaterialInstanceDynamic* UBridgeInteriorComponent::MakeLit(const TCHAR* Name, FLinearColor Base, float Roughness, float Metallic, FLinearColor Emissive, float EmissiveStrength)
{
	UMaterialInterface* Parent = SlonkMaterials::LoadOrFallback(LitMaterial, TEXT("BridgeLit"), true);
	if (!Parent)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, this, *FString::Printf(TEXT("Bridge_%s"), Name));
	Mid->SetVectorParameterValue(TEXT("BaseColor"), Base);
	Mid->SetScalarParameterValue(TEXT("Roughness"), Roughness);
	Mid->SetScalarParameterValue(TEXT("Metallic"), Metallic);
	Mid->SetVectorParameterValue(TEXT("EmissiveColor"), Emissive);
	Mid->SetScalarParameterValue(TEXT("EmissiveStrength"), EmissiveStrength);
	Materials.Add(Mid);
	return Mid;
}

UMaterialInstanceDynamic* UBridgeInteriorComponent::MakeScreen(const TSoftObjectPtr<UMaterialInterface>& ParentSoft, const TCHAR* Name, float Seed, float Intensity, float CellScale)
{
	UMaterialInterface* Parent = SlonkMaterials::LoadOrFallback(ParentSoft, Name);
	if (!Parent)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, this, *FString::Printf(TEXT("Bridge_%s_%d"), Name, PieceCount));
	Mid->SetScalarParameterValue(TEXT("Seed"), Seed);
	Mid->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	Mid->SetScalarParameterValue(TEXT("CellScale"), CellScale);
	Materials.Add(Mid);
	return Mid;
}

// ---- Primitives -------------------------------------------------------------------------------

void UBridgeInteriorComponent::PrepPrimitive(UPrimitiveComponent* C, bool bShadow)
{
	C->SetupAttachment(this);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetMobility(EComponentMobility::Movable);
	C->CastShadow = bShadow;
	C->bCastDynamicShadow = bShadow;
	C->bAffectDistanceFieldLighting = false;
	C->SetReceivesDecals(false);
	C->SetVisibility(IsVisible());
}

UStaticMeshComponent* UBridgeInteriorComponent::Piece(UStaticMesh* Mesh, const FVector& Center, const FVector& Size, const FRotator& Rot, UMaterialInterface* Mat, bool bShadow)
{
	if (!Mesh)
	{
		return nullptr;
	}
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(GetOwner(), *FString::Printf(TEXT("Bridge_%d"), PieceCount++));
	PrepPrimitive(C, bShadow);
	C->SetStaticMesh(Mesh);
	// Engine basic shapes are 100 cm across.
	C->SetRelativeLocationAndRotation(Center, Rot);
	C->SetRelativeScale3D(Size / 100.0);
	if (Mat)
	{
		C->SetMaterial(0, Mat);
	}
	C->RegisterComponent();
	Pieces.Add(C);
	return C;
}

UDynamicMeshComponent* UBridgeInteriorComponent::Revolve(const TArray<FVector2D>& Profile, float StartDeg, float EndDeg, UMaterialInterface* Mat, bool bShadow)
{
	UDynamicMeshComponent* C = NewObject<UDynamicMeshComponent>(GetOwner(), *FString::Printf(TEXT("BridgeRing_%d"), PieceCount++));
	PrepPrimitive(C, bShadow);
	UDynamicMesh* Mesh = C->GetDynamicMesh();
	if (Mesh)
	{
		Mesh->Reset();
		FGeometryScriptPrimitiveOptions Options;
		FGeometryScriptRevolveOptions Rev;
		Rev.RevolveDegrees = EndDeg - StartDeg;
		Rev.DegreeOffset = StartDeg;
		Rev.bHardNormals = true;
		Rev.HardNormalAngle = 35.f;
		const int32 Steps = FMath::Max(4, FMath::RoundToInt(FMath::Abs(Rev.RevolveDegrees) / 3.f));
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendRevolvePolygon(Mesh, Options, FTransform::Identity, Profile, Rev, 0.f, Steps);
		FGeometryScriptCalculateNormalsOptions NormalOptions;
		UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(Mesh, NormalOptions);
		C->NotifyMeshUpdated();
	}
	C->RegisterComponent();
	if (Mat)
	{
		C->SetMaterial(0, Mat);
	}
	Pieces.Add(C);
	return C;
}

void UBridgeInteriorComponent::Light(const FVector& Pos, float Intensity, float Radius)
{
	UPointLightComponent* L = NewObject<UPointLightComponent>(GetOwner(), *FString::Printf(TEXT("BridgeLight_%d"), PieceCount++));
	L->SetupAttachment(this);
	L->SetMobility(EComponentMobility::Movable);
	L->SetIntensityUnits(ELightUnits::Candelas);
	L->SetIntensity(Intensity);
	L->SetLightColor(LightColor);
	L->SetAttenuationRadius(Radius);
	L->SetSourceRadius(20.f);
	L->SetSoftSourceRadius(40.f);
	L->SetCastShadows(false);
	L->bUseInverseSquaredFalloff = true;
	L->SetRelativeLocation(Pos);
	L->SetVisibility(IsVisible());
	L->RegisterComponent();
	Pieces.Add(L);
}

UStaticMeshComponent* UBridgeInteriorComponent::WallBox(float AngleDeg, float Radial, float Z, float Thickness, float Width, float Height, UMaterialInterface* Mat, float PitchDeg, bool bShadow)
{
	const FRotator Rot(PitchDeg, AngleDeg, 0.f);
	const FVector Dir = FRotator(0.f, AngleDeg, 0.f).Vector();
	return Box(Dir * Radial + FVector(0, 0, Z), FVector(Thickness, Width, Height), Rot, Mat, bShadow);
}

// ---- Furniture --------------------------------------------------------------------------------

void UBridgeInteriorComponent::Crew(const FVector& SeatPos, float YawDeg, UMaterialInterface* UniformMat)
{
	const FRotator R(0.f, YawDeg, 0.f);
	const FVector F = R.Vector();
	Cyl(SeatPos + FVector(0, 0, 36), 19.f, 60.f, UniformMat, R);
	Box(SeatPos + FVector(0, 0, 60), FVector(26, 48, 14), R, UniformMat);
	Cyl(SeatPos + FVector(0, 0, 76), 7.f, 12.f, Skin, R);
	Ball(SeatPos + FVector(0, 0, 94), 12.f, Skin);
	Ball(SeatPos + FVector(0, 0, 96.5f) - F * 2.5f, 12.f, Hair);
	Box(SeatPos + F * 22.f + FVector(0, 0, 6) + R.RotateVector(FVector(0, -11, 0)), FVector(44, 16, 14), R, ChairBlack);
	Box(SeatPos + F * 22.f + FVector(0, 0, 6) + R.RotateVector(FVector(0, 11, 0)), FVector(44, 16, 14), R, ChairBlack);
	Box(SeatPos + F * 16.f + FVector(0, 0, 48) + R.RotateVector(FVector(0, -26, 0)), FVector(34, 9, 9), R, UniformMat);
	Box(SeatPos + F * 16.f + FVector(0, 0, 48) + R.RotateVector(FVector(0, 26, 0)), FVector(34, 9, 9), R, UniformMat);
}

void UBridgeInteriorComponent::SwivelChair(const FVector& FloorPos, float YawDeg, bool bCrew, UMaterialInterface* UniformMat)
{
	const FRotator R(0.f, YawDeg, 0.f);
	const FVector F = R.Vector();
	// Four-spoke white base, white pedestal, black moulded seat with a tall wrap-around back.
	for (int32 i = 0; i < 4; ++i)
	{
		const float A = YawDeg + 45.f + i * 90.f;
		Box(FloorPos + FRotator(0.f, A, 0.f).Vector() * 16.f + FVector(0, 0, 2.5f), FVector(34, 6, 5), FRotator(0.f, A, 0.f), LightGrey);
	}
	Cyl(FloorPos + FVector(0, 0, 24), 5.f, 44.f, LightGrey, R);
	Box(FloorPos + FVector(0, 0, 49), FVector(50, 50, 9), R, ChairBlack);
	Box(FloorPos - F * 24.f + FVector(0, 0, 82), FVector(7, 50, 62), R, ChairBlack);
	Box(FloorPos - F * 14.f + FVector(0, 0, 70) + R.RotateVector(FVector(0, -25, 0)), FVector(26, 6, 38), R, ChairBlack);
	Box(FloorPos - F * 14.f + FVector(0, 0, 70) + R.RotateVector(FVector(0, 25, 0)), FVector(26, 6, 38), R, ChairBlack);
	if (bCrew && bShowCrew)
	{
		Crew(FloorPos + FVector(0, 0, 54.f), YawDeg, UniformMat ? UniformMat : ChairBlack);
	}
}

// ---- Build ------------------------------------------------------------------------------------

void UBridgeInteriorComponent::Build()
{
	for (USceneComponent* P : Pieces)
	{
		if (P)
		{
			P->DestroyComponent();
		}
	}
	Pieces.Reset();
	Materials.Reset();
	PieceCount = 0;

	WallBlue = MakeLit(TEXT("WallBlue"), FLinearColor(0.26f, 0.32f, 0.42f), 0.75f);
	CeilingGrey = MakeLit(TEXT("CeilingGrey"), FLinearColor(0.16f, 0.18f, 0.22f), 0.85f);
	Deck = MakeLit(TEXT("Deck"), FLinearColor(0.27f, 0.28f, 0.30f), 0.9f);
	WellFloor = MakeLit(TEXT("WellFloor"), FLinearColor(0.20f, 0.21f, 0.23f), 0.9f);
	ConsoleBlack = MakeLit(TEXT("ConsoleBlack"), FLinearColor(0.03f, 0.03f, 0.035f), 0.5f);
	Red = MakeLit(TEXT("Red"), FLinearColor(0.72f, 0.03f, 0.03f), 0.45f);
	LightGrey = MakeLit(TEXT("LightGrey"), FLinearColor(0.62f, 0.62f, 0.64f), 0.5f);
	ChairBlack = MakeLit(TEXT("ChairBlack"), FLinearColor(0.045f, 0.045f, 0.05f), 0.65f);
	Skin = MakeLit(TEXT("Skin"), FLinearColor(0.55f, 0.38f, 0.28f), 0.6f);
	Hair = MakeLit(TEXT("Hair"), FLinearColor(0.08f, 0.05f, 0.03f), 0.7f);
	LightPanel = MakeLit(TEXT("LightPanel"), FLinearColor(0.9f, 0.9f, 0.9f), 0.9f, 0.f, FLinearColor(1.0f, 0.96f, 0.9f), LightPanelEmissive);
	UniformGold = MakeLit(TEXT("UniformGold"), FLinearColor(0.50f, 0.36f, 0.06f), 0.85f);
	UniformBlue = MakeLit(TEXT("UniformBlue"), FLinearColor(0.08f, 0.22f, 0.55f), 0.85f);
	UniformRed = MakeLit(TEXT("UniformRed"), FLinearColor(0.50f, 0.05f, 0.07f), 0.85f);

	const float R = WallRadius;
	const float RD = DeckRadius;
	const float HD = DeckHeight;
	const float H = CeilingHeight;
	const float VS = ViewscreenHalfAngle;
	const float TL0 = TurboliftAngle - TurboliftHalfAngle;
	const float TL1 = TurboliftAngle + TurboliftHalfAngle;

	// ---- Floors: central well, raised outer deck ring with its red edging, two steps down at the front.
	Cyl(FVector(0, 0, -3), RD + 10.f, 6.f, WellFloor);
	Revolve({ FVector2D(RD, 0), FVector2D(R + 15.f, 0), FVector2D(R + 15.f, HD), FVector2D(RD, HD) }, 0.f, 360.f, Deck);
	Revolve({ FVector2D(RD - 4.f, HD - 6.f), FVector2D(RD + 6.f, HD - 6.f), FVector2D(RD + 6.f, HD + 1.2f), FVector2D(RD - 4.f, HD + 1.2f) }, 0.f, 360.f, Red, false);
	for (int32 s = -1; s <= 1; s += 2)
	{
		const float A = s * (VS + 6.f);
		WallBox(A, RD - 24.f, HD * 0.25f, 46.f, 110.f, HD * 0.5f, Deck);
		WallBox(A, RD - 24.f, HD * 0.5f - 2.f, 46.f, 110.f, 4.f, Red, 0.f, false);
	}

	// ---- Handrail on the deck edge, in four arcs with gaps fore, aft and abeam.
	{
		const float RR = RD + 17.f;
		const float RailZ = HD + 92.f;
		const float Arcs[4][2] = { { 26.f, 86.f }, { 104.f, 170.f }, { 190.f, 256.f }, { 274.f, 334.f } };
		for (const auto& Arc : Arcs)
		{
			Revolve({ FVector2D(RR - 3.5f, RailZ - 4.f), FVector2D(RR + 3.5f, RailZ - 4.f), FVector2D(RR + 3.5f, RailZ + 4.f), FVector2D(RR - 3.5f, RailZ + 4.f) }, Arc[0], Arc[1], Red, false);
			const int32 Posts = FMath::Max(2, FMath::RoundToInt((Arc[1] - Arc[0]) / 15.f) + 1);
			for (int32 i = 0; i < Posts; ++i)
			{
				const float A = FMath::Lerp(Arc[0], Arc[1], (float)i / (Posts - 1));
				Cyl(FRotator(0.f, A, 0.f).Vector() * RR + FVector(0, 0, HD + 46.f), 2.4f, 92.f, ConsoleBlack, FRotator::ZeroRotator, false);
			}
		}
	}

	// ---- Perimeter console ring: black body with a sloped top, broken at the viewscreen and the turbolift.
	{
		const TArray<FVector2D> Body = { FVector2D(R - 58.f, HD), FVector2D(R + 12.f, HD), FVector2D(R + 12.f, HD + 82.f), FVector2D(R - 58.f, HD + 58.f) };
		Revolve(Body, VS, TL0, ConsoleBlack);
		Revolve(Body, TL1, 360.f - VS, ConsoleBlack);
	}
	// Monitor band and upper wall (the upper wall also runs over the turbolift).
	const TArray<FVector2D> MonitorBand = { FVector2D(R + 2.f, HD + 82.f), FVector2D(R + 12.f, HD + 82.f), FVector2D(R + 12.f, HD + 160.f), FVector2D(R + 2.f, HD + 160.f) };
	Revolve(MonitorBand, VS, TL0, ConsoleBlack);
	Revolve(MonitorBand, TL1, 360.f - VS, ConsoleBlack);
	Revolve({ FVector2D(R + 4.f, HD + 160.f), FVector2D(R + 16.f, HD + 160.f), FVector2D(R + 16.f, H + 2.f), FVector2D(R + 4.f, H + 2.f) }, VS, 360.f - VS, WallBlue);
	// Light-grey trim bands at the console lip and above the monitors.
	const TArray<FVector2D> Lip = { FVector2D(R - 60.f, HD + 56.f), FVector2D(R - 56.f, HD + 56.f), FVector2D(R - 56.f, HD + 61.f), FVector2D(R - 60.f, HD + 61.f) };
	Revolve(Lip, VS, TL0, LightGrey, false);
	Revolve(Lip, TL1, 360.f - VS, LightGrey, false);
	Revolve({ FVector2D(R - 1.f, HD + 158.f), FVector2D(R + 5.f, HD + 158.f), FVector2D(R + 5.f, HD + 164.f), FVector2D(R - 1.f, HD + 164.f) }, VS, 360.f - VS, LightGrey, false);

	// Ceiling: dark outer disc, light-blue inner ring, glowing light ring.
	Cyl(FVector(0, 0, H + 6.f), R + 20.f, 12.f, ConsoleBlack);
	Revolve({ FVector2D(0.f, H - 1.f), FVector2D(R * 0.62f, H - 1.f), FVector2D(R * 0.62f, H + 1.f), FVector2D(0.f, H + 1.f) }, 0.f, 360.f, CeilingGrey);
	Revolve({ FVector2D(R * 0.62f, H - 4.f), FVector2D(R * 0.74f, H - 4.f), FVector2D(R * 0.74f, H), FVector2D(R * 0.62f, H) }, 0.f, 360.f, LightPanel, false);
	Revolve({ FVector2D(R * 0.74f, H - 2.f), FVector2D(R + 4.f, H - 2.f), FVector2D(R + 4.f, H), FVector2D(R * 0.74f, H) }, 0.f, 360.f, CeilingGrey);

	// ---- Wall bays: button panels on the console slope, monitors above, framed viewers on the upper wall.
	const int32 N = FMath::Max(8, WallBays);
	const float Step = 360.f / N;
	const float Chord = 2.f * R * FMath::Sin(FMath::DegreesToRadians(Step * 0.5f));
	const float SlopeDeg = FMath::RadiansToDegrees(FMath::Atan2(24.f, 70.f));
	float Seed = 1.f;
	for (int32 k = 0; k < N; ++k)
	{
		const float A = FMath::Fmod(k * Step + Step * 0.5f, 360.f);
		const bool bScreen = (A < VS) || (A > 360.f - VS);
		const bool bLift = (A > TL0 && A < TL1);
		if (bScreen)
		{
			continue;
		}
		if (!bLift)
		{
			// Sloped lamp panel sitting just proud of the console top (slope runs from R-58 at +58 to R+12 at +82).
			const FVector Mid = FRotator(0.f, A, 0.f).Vector() * (R - 23.f) + FVector(0, 0, HD + 70.f);
			const FVector Up = FRotator(0.f, A, 0.f).RotateVector(FVector(-24.f, 0.f, 70.f).GetSafeNormal());
			Box(Mid + Up * 1.2f, FVector(66.f, Chord - 26.f, 2.f), FRotator(-SlopeDeg, A, 0.f), MakeScreen(ButtonsMaterial, TEXT("Buttons"), Seed, DisplayIntensity, 1.f), false);
			WallBox(A, R + 1.f, HD + 121.f, 3.f, Chord - 22.f, 64.f, MakeScreen(DisplayMaterial, TEXT("Monitor"), Seed + 0.5f, DisplayIntensity * 0.8f, 1.f), 0.f, false);
		}
		// Overhead viewer with a pale frame.
		WallBox(A, R + 3.f, HD + 215.f, 5.f, FMath::Min(Chord - 40.f, 150.f), 92.f, LightGrey, 0.f, false);
		WallBox(A, R + 0.f, HD + 215.f, 3.f, FMath::Min(Chord - 56.f, 134.f), 78.f, MakeScreen(ViewerMaterial, TEXT("Viewer"), Seed + 3.f, DisplayIntensity * 0.7f, 1.f), 0.f, false);
		Seed += 7.f;
	}

	// ---- Viewscreen: console-height sill, lintel above, pale pillars either side (the opening itself is empty).
	Revolve({ FVector2D(R + 2.f, HD), FVector2D(R + 12.f, HD), FVector2D(R + 12.f, ViewscreenBottom), FVector2D(R + 2.f, ViewscreenBottom) }, -VS, VS, ConsoleBlack);
	Revolve({ FVector2D(R - 2.f, ViewscreenBottom - 6.f), FVector2D(R + 6.f, ViewscreenBottom - 6.f), FVector2D(R + 6.f, ViewscreenBottom), FVector2D(R - 2.f, ViewscreenBottom) }, -VS, VS, LightGrey, false);
	Revolve({ FVector2D(R + 2.f, ViewscreenTop), FVector2D(R + 16.f, ViewscreenTop), FVector2D(R + 16.f, H + 2.f), FVector2D(R + 2.f, H + 2.f) }, -VS, VS, WallBlue);
	Revolve({ FVector2D(R - 2.f, ViewscreenTop), FVector2D(R + 6.f, ViewscreenTop), FVector2D(R + 6.f, ViewscreenTop + 6.f), FVector2D(R - 2.f, ViewscreenTop + 6.f) }, -VS, VS, LightGrey, false);
	for (int32 s = -1; s <= 1; s += 2)
	{
		WallBox(s * (VS + 1.5f), R + 4.f, (HD + H) * 0.5f, 26.f, 26.f, H - HD, LightGrey);
	}

	// ---- Turbolift alcove: recessed pale doors with a red surround.
	{
		const float A = TurboliftAngle;
		WallBox(A, R + 8.f, HD + 100.f, 6.f, 180.f, 200.f, Red);
		WallBox(A, R + 4.f, HD + 100.f, 4.f, 150.f, 190.f, LightGrey);
		WallBox(A, R + 1.5f, HD + 100.f, 2.f, 3.f, 190.f, ConsoleBlack, 0.f, false);
		// Console-height wall stubs either side of the doors.
		for (int32 s = -1; s <= 1; s += 2)
		{
			WallBox(A + s * TurboliftHalfAngle, R - 20.f, HD + 40.f, 70.f, 10.f, 80.f, ConsoleBlack);
		}
	}

	// ---- Helm (port) and navigation (starboard): one black wedge console in the well, sloped top, astrogator dome.
	{
		const float X0 = 185.f;
		Box(FVector(X0 + 42.f, 0, 42.f), FVector(84, 250, 84), FRotator::ZeroRotator, ConsoleBlack);
		Box(FVector(X0 + 42.f, 0, 92.f), FVector(90, 256, 16), FRotator(-14.f, 0.f, 0.f), ConsoleBlack);
		Box(FVector(X0 + 40.f, -66.f, 101.f), FVector(60, 96, 2), FRotator(-14.f, 0.f, 0.f), MakeScreen(ButtonsMaterial, TEXT("Helm"), 101.f, DisplayIntensity, 0.8f), false);
		Box(FVector(X0 + 40.f, 66.f, 101.f), FVector(60, 96, 2), FRotator(-14.f, 0.f, 0.f), MakeScreen(ButtonsMaterial, TEXT("Nav"), 102.f, DisplayIntensity, 0.8f), false);
		Ball(FVector(X0 + 48.f, 0, 100.f), 15.f, LightGrey);
		Box(FVector(X0 + 36.f, 0, 100.f), FVector(10, 36, 8), FRotator::ZeroRotator, ChairBlack);
		Box(FVector(X0 + 2.f, 0, 60.f), FVector(4, 250, 3), FRotator::ZeroRotator, LightGrey, false);
		SwivelChair(FVector(118.f, -64.f, 0.f), 0.f, true, UniformGold);
		SwivelChair(FVector(118.f, 64.f, 0.f), 0.f, true, UniformGold);
	}

	// ---- Captain's chair on a round platform at the centre, camera at the headrest.
	Cyl(FVector(0, 0, 15.f), 120.f, 30.f, Deck);
	Revolve({ FVector2D(118.f, 24.f), FVector2D(124.f, 24.f), FVector2D(124.f, 31.f), FVector2D(118.f, 31.f) }, 0.f, 360.f, LightGrey, false);
	Box(FVector(-10.f, 0, 46.f), FVector(64, 62, 32), FRotator::ZeroRotator, LightGrey);
	Box(FVector(-6.f, 0, 68.f), FVector(64, 68, 12), FRotator::ZeroRotator, ChairBlack);
	Box(FVector(-36.f, 0, 112.f), FVector(12, 68, 80), FRotator(-6.f, 0.f, 0.f), ChairBlack);
	for (int32 s = -1; s <= 1; s += 2)
	{
		Box(FVector(-8.f, s * 40.f, 92.f), FVector(64, 14, 12), FRotator::ZeroRotator, LightGrey);
		Box(FVector(10.f, s * 40.f, 98.5f), FVector(22, 12, 1.5f), FRotator::ZeroRotator, MakeScreen(ButtonsMaterial, TEXT("Arm"), 200.f + s, DisplayIntensity * 0.8f, 0.5f), false);
	}

	// ---- Perimeter stations: swivel chairs facing their consoles.
	struct FSeat { float Angle; bool bCrew; UMaterialInterface* Uniform; };
	const FSeat Seats[] = {
		{ 52.f, false, nullptr }, { 82.f, true, UniformBlue }, { 112.f, true, UniformBlue },
		{ 180.f, false, nullptr }, { 212.f, true, UniformRed }, { 248.f, true, UniformRed },
		{ 280.f, false, nullptr }, { 308.f, true, UniformGold } };
	for (const FSeat& S : Seats)
	{
		if (S.Angle > TL0 - 8.f && S.Angle < TL1 + 8.f)
		{
			continue;
		}
		SwivelChair(FRotator(0.f, S.Angle, 0.f).Vector() * (R - 128.f) + FVector(0, 0, HD), S.Angle, S.bCrew, S.Uniform);
	}

	// ---- Lighting: even overhead fill from a ring of points plus the glowing ceiling ring.
	for (int32 i = 0; i < 6; ++i)
	{
		const float A = i * 60.f + 30.f;
		Light(FRotator(0.f, A, 0.f).Vector() * (R * 0.55f) + FVector(0, 0, H - 120.f), LightIntensity, 1300.f);
	}
	Light(FVector(0, 0, H - 130.f), LightIntensity * 0.5f, 1200.f);
}
