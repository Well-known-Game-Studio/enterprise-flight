#include "BridgeInteriorComponent.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "SlonkMaterials.h"
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

UMaterialInstanceDynamic* UBridgeInteriorComponent::MakeDisplay(const TCHAR* Name, float Seed, float Intensity)
{
	UMaterialInterface* Parent = SlonkMaterials::LoadOrFallback(DisplayMaterial, TEXT("BridgeDisplay"));
	if (!Parent)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, this, *FString::Printf(TEXT("Bridge_%s"), Name));
	Mid->SetScalarParameterValue(TEXT("Seed"), Seed);
	Mid->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	Materials.Add(Mid);
	return Mid;
}

// ---- Primitives -------------------------------------------------------------------------------

UStaticMeshComponent* UBridgeInteriorComponent::Piece(UStaticMesh* Mesh, const FVector& Center, const FVector& Size, const FRotator& Rot, UMaterialInterface* Mat, bool bShadow)
{
	if (!Mesh)
	{
		return nullptr;
	}
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(GetOwner(), *FString::Printf(TEXT("Bridge_%d"), PieceCount++));
	C->SetupAttachment(this);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetMobility(EComponentMobility::Movable);
	C->CastShadow = bShadow;
	C->bCastDynamicShadow = bShadow;
	C->bAffectDistanceFieldLighting = false;
	C->SetReceivesDecals(false);
	// Engine basic shapes are 100 cm across.
	C->SetRelativeLocationAndRotation(Center, Rot);
	C->SetRelativeScale3D(Size / 100.0);
	if (Mat)
	{
		C->SetMaterial(0, Mat);
	}
	C->SetVisibility(IsVisible());
	C->RegisterComponent();
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
	L->SetSourceRadius(15.f);
	L->SetSoftSourceRadius(30.f);
	L->SetCastShadows(false);
	L->bUseInverseSquaredFalloff = true;
	L->SetRelativeLocation(Pos);
	L->SetVisibility(IsVisible());
	L->RegisterComponent();
	Pieces.Add(L);
}

// ---- Furniture --------------------------------------------------------------------------------

void UBridgeInteriorComponent::Crew(const FVector& SeatPos, float YawDeg)
{
	const FRotator R(0.f, YawDeg, 0.f);
	const FVector F = R.Vector();
	// Seated figure: torso cylinder, shoulders, head, legs tucked under the console.
	Cyl(SeatPos + FVector(0, 0, 36), 19.f, 60.f, Uniform, R);
	Box(SeatPos + FVector(0, 0, 60), FVector(26, 48, 14), R, Uniform);
	Cyl(SeatPos + FVector(0, 0, 76), 7.f, 12.f, Skin, R);           // neck
	Ball(SeatPos + FVector(0, 0, 94), 12.f, Skin);
	Ball(SeatPos + FVector(0, 0, 96.5f) - F * 2.5f, 12.f, Hair);
	// Thighs forward to the console, lower legs down.
	Box(SeatPos + F * 22.f + FVector(0, 0, 6) + R.RotateVector(FVector(0, -11, 0)), FVector(44, 16, 14), R, Trim);
	Box(SeatPos + F * 22.f + FVector(0, 0, 6) + R.RotateVector(FVector(0, 11, 0)), FVector(44, 16, 14), R, Trim);
	// Upper arms resting toward the console.
	Box(SeatPos + F * 16.f + FVector(0, 0, 48) + R.RotateVector(FVector(0, -26, 0)), FVector(34, 9, 9), R, Uniform);
	Box(SeatPos + F * 16.f + FVector(0, 0, 48) + R.RotateVector(FVector(0, 26, 0)), FVector(34, 9, 9), R, Uniform);
}

void UBridgeInteriorComponent::Chair(const FVector& FloorPos, float YawDeg, float SeatHeight, bool bCrew, bool bArmrests)
{
	const FRotator R(0.f, YawDeg, 0.f);
	const FVector F = R.Vector();
	Cyl(FloorPos + FVector(0, 0, 2), 24.f, 4.f, Trim, R);
	Cyl(FloorPos + FVector(0, 0, SeatHeight * 0.5f), 7.f, SeatHeight - 4.f, Trim, R);
	Box(FloorPos + FVector(0, 0, SeatHeight + 5.f), FVector(52, 54, 10), R, ChairMat);
	Box(FloorPos - F * 26.f + FVector(0, 0, SeatHeight + 42.f), FVector(10, 54, 68), R, ChairMat);
	if (bArmrests)
	{
		Box(FloorPos - F * 2.f + FVector(0, 0, SeatHeight + 26.f) + R.RotateVector(FVector(0, -31, 0)), FVector(50, 9, 8), R, Trim);
		Box(FloorPos - F * 2.f + FVector(0, 0, SeatHeight + 26.f) + R.RotateVector(FVector(0, 31, 0)), FVector(50, 9, 8), R, Trim);
	}
	if (bCrew && bShowCrew)
	{
		Crew(FloorPos + FVector(0, 0, SeatHeight + 10.f), YawDeg);
	}
}

void UBridgeInteriorComponent::Station(const FVector& ConsolePos, float YawDeg, float Width, bool bWallDisplay, bool bCrew, float Seed)
{
	const FRotator R(0.f, YawDeg, 0.f);
	const FVector F = R.Vector();
	// Console body (front edge toward the operator), slanted display on top, kick plate.
	Box(ConsolePos + F * 35.f + FVector(0, 0, 45), FVector(70, Width, 90), R, DarkPanel);
	Box(ConsolePos + F * 35.f + FVector(0, 0, 10), FVector(74, Width + 4.f, 6), R, Trim);
	UMaterialInstanceDynamic* Disp = MakeDisplay(TEXT("Display"), Seed, DisplayIntensity);
	Box(ConsolePos + F * 32.f + FVector(0, 0, 97), FVector(58, Width - 16.f, 3), R + FRotator(-22.f, 0.f, 0.f), Disp, false);
	// Lip at the back of the display.
	Box(ConsolePos + F * 66.f + FVector(0, 0, 112), FVector(8, Width, 30), R, Trim);
	if (bWallDisplay)
	{
		UMaterialInstanceDynamic* Wall = MakeDisplay(TEXT("WallDisplay"), Seed + 7.f, DisplayIntensity * 0.6f);
		Box(ConsolePos + F * 86.f + FVector(0, 0, 195), FVector(4, Width - 30.f, 95), R, Wall, false);
	}
	Chair(ConsolePos - F * 55.f, YawDeg, 44.f, bCrew, false);
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

	Panel = MakeLit(TEXT("Panel"), FLinearColor(0.46f, 0.47f, 0.50f), 0.55f);
	DarkPanel = MakeLit(TEXT("DarkPanel"), FLinearColor(0.16f, 0.17f, 0.20f), 0.5f);
	Trim = MakeLit(TEXT("Trim"), FLinearColor(0.06f, 0.06f, 0.07f), 0.45f, 0.3f);
	Carpet = MakeLit(TEXT("Carpet"), FLinearColor(0.30f, 0.10f, 0.11f), 1.0f);
	PlatformCarpet = MakeLit(TEXT("PlatformCarpet"), FLinearColor(0.36f, 0.12f, 0.13f), 1.0f);
	ChairMat = MakeLit(TEXT("Chair"), FLinearColor(0.20f, 0.07f, 0.08f), 0.7f);
	Uniform = MakeLit(TEXT("Uniform"), FLinearColor(0.42f, 0.07f, 0.10f), 0.8f);
	Skin = MakeLit(TEXT("Skin"), FLinearColor(0.55f, 0.38f, 0.28f), 0.6f);
	Hair = MakeLit(TEXT("Hair"), FLinearColor(0.08f, 0.05f, 0.03f), 0.7f);
	LightPanel = MakeLit(TEXT("LightPanel"), FLinearColor(0.9f, 0.9f, 0.9f), 0.9f, 0.f, FLinearColor(1.0f, 0.94f, 0.86f), LightPanelEmissive);
	Rail = MakeLit(TEXT("Rail"), FLinearColor(0.75f, 0.72f, 0.65f), 0.35f, 0.9f);

	const FVector C = BridgeCenter;
	const float R = WallRadius;
	const float H = CeilingHeight;
	const int32 N = FMath::Max(8, WallSegments);
	const float Step = 360.f / N;
	const float SegWidth = 2.f * R * FMath::Sin(FMath::DegreesToRadians(Step * 0.5f)) + 2.f;

	// Deck, captain's platform, ceiling.
	Cyl(C + FVector(0, 0, -5), R + 8.f, 10.f, Carpet);
	Cyl(FVector(-30, 0, 17.5f), 200.f, 35.f, PlatformCarpet);
	Cyl(FVector(-30, 0, 36.f), 205.f, 2.f, Trim);
	Cyl(C + FVector(0, 0, H + 5.f), R + 8.f, 10.f, DarkPanel);
	// Recessed ceiling ring with light strips (kept dark so the overhead lights do not bloom on it).
	Cyl(C + FVector(0, 0, H - 2.f), R * 0.7f, 4.f, DarkPanel);
	for (int32 i = 0; i < 12; ++i)
	{
		const float A = i * 30.f + 15.f;
		const FVector Dir = FRotator(0.f, A, 0.f).Vector();
		Box(C + Dir * (R * 0.78f) + FVector(0, 0, H - 2.f), FVector(28, 110, 3), FRotator(0.f, A, 0.f), LightPanel, false);
	}

	// Walls: panels around the room, the forward segments left open as the viewscreen.
	const int32 Half = ViewscreenSegments / 2;
	for (int32 k = 0; k < N; ++k)
	{
		int32 Rel = k;
		if (Rel > N / 2) { Rel -= N; }
		const float A = k * Step;
		const FRotator Rot(0.f, A, 0.f);
		const FVector Dir = Rot.Vector();
		const bool bOpening = FMath::Abs(Rel) <= Half;
		if (bOpening)
		{
			// Sill below and lintel above the screen, with the frame between.
			Box(C + Dir * R + FVector(0, 0, ViewscreenBottom * 0.5f), FVector(16, SegWidth, ViewscreenBottom), Rot, DarkPanel);
			Box(C + Dir * R + FVector(0, 0, (H + ViewscreenTop) * 0.5f), FVector(16, SegWidth, H - ViewscreenTop), Rot, Panel);
			Box(C + Dir * (R - 6.f) + FVector(0, 0, ViewscreenBottom + 3.f), FVector(10, SegWidth, 6), Rot, Trim);
			Box(C + Dir * (R - 6.f) + FVector(0, 0, ViewscreenTop - 3.f), FVector(10, SegWidth, 6), Rot, Trim);
			continue;
		}
		const bool bAlt = (k % 2) == 0;
		Box(C + Dir * R + FVector(0, 0, H * 0.5f), FVector(16, SegWidth, H), Rot, bAlt ? Panel : DarkPanel);
		// Horizontal trim band at the height of the station displays.
		Box(C + Dir * (R - 7.f) + FVector(0, 0, 140.f), FVector(4, SegWidth, 10), Rot, Trim);
	}
	// Vertical frame pillars at the edges of the viewscreen.
	for (int32 s = -1; s <= 1; s += 2)
	{
		const float A = s * (Half + 0.5f) * Step;
		const FVector Dir = FRotator(0.f, A, 0.f).Vector();
		Box(C + Dir * (R - 4.f) + FVector(0, 0, H * 0.5f), FVector(24, 24, H), FRotator(0.f, A, 0.f), Trim);
	}
	// Turbolift doors at the back.
	{
		const FVector Dir = FRotator(0.f, 180.f, 0.f).Vector();
		Box(C + Dir * (R - 9.f) + FVector(0, 0, 112), FVector(4, 130, 224), FRotator(0.f, 180.f, 0.f), Trim);
		Box(C + Dir * (R - 11.f) + FVector(0, 0, 112) + FVector(0, 33, 0), FVector(3, 60, 216), FRotator(0.f, 180.f, 0.f), Panel);
		Box(C + Dir * (R - 11.f) + FVector(0, 0, 112) - FVector(0, 33, 0), FVector(3, 60, 216), FRotator(0.f, 180.f, 0.f), Panel);
	}

	// Helm (left) and navigation (right): one wide console forward of the captain, two chairs facing the screen.
	{
		const FVector HelmFront(230.f, 0.f, 0.f);
		Box(HelmFront + FVector(35, 0, 45), FVector(70, 330, 90), FRotator::ZeroRotator, DarkPanel);
		Box(HelmFront + FVector(35, 0, 10), FVector(74, 334, 6), FRotator::ZeroRotator, Trim);
		Box(HelmFront + FVector(66, 0, 112), FVector(8, 330, 30), FRotator::ZeroRotator, Trim);
		Box(HelmFront + FVector(32, -82, 97), FVector(58, 148, 3), FRotator(-22.f, 0.f, 0.f), MakeDisplay(TEXT("Helm"), 1.f, DisplayIntensity), false);
		Box(HelmFront + FVector(32, 82, 97), FVector(58, 148, 3), FRotator(-22.f, 0.f, 0.f), MakeDisplay(TEXT("Nav"), 2.f, DisplayIntensity), false);
		// Centre instrument between the two.
		Box(HelmFront + FVector(40, 0, 108), FVector(30, 12, 26), FRotator::ZeroRotator, Trim);
		Chair(HelmFront + FVector(-55, -82, 0), 0.f, 44.f, true, false);
		Chair(HelmFront + FVector(-55, 82, 0), 0.f, 44.f, true, false);
	}

	// Captain's chair on the platform, camera at the headrest.
	Chair(FVector(-16.f, 0.f, 35.f), 0.f, 48.f, false, true);
	// Small armrest display on the right arm.
	Box(FVector(-10.f, 36.f, 35.f + 48.f + 31.f), FVector(20, 8, 2), FRotator::ZeroRotator, MakeDisplay(TEXT("Arm"), 3.f, DisplayIntensity * 0.8f), false);

	// Perimeter stations (science, communications, engineering, tactical, ...).
	const int32 StationSegments[] = { 2, -2, 4, -4, 6, -6 };
	float Seed = 10.f;
	for (int32 Rel : StationSegments)
	{
		if (FMath::Abs(Rel) <= Half || FMath::Abs(Rel) >= N / 2)
		{
			continue;
		}
		const float A = Rel * Step;
		const FVector Dir = FRotator(0.f, A, 0.f).Vector();
		Station(C + Dir * (R - 96.f), A, FMath::Min(SegWidth - 30.f, 190.f), true, bShowCrew && FMath::Abs(Rel) != 6, Seed);
		Seed += 5.f;
	}

	// Rail behind the captain's platform.
	for (int32 i = 0; i < 5; ++i)
	{
		const float A = 135.f + i * 22.5f;
		const FVector Dir = FRotator(0.f, A, 0.f).Vector();
		const FVector Base = FVector(-30, 0, 36) + Dir * 225.f;
		Cyl(Base + FVector(0, 0, 50), 2.5f, 100.f, Rail);
		if (i < 4)
		{
			const float A2 = A + 11.25f;
			const FVector Mid = FVector(-30, 0, 36) + FRotator(0.f, A2, 0.f).Vector() * 225.f;
			Box(Mid + FVector(0, 0, 100), FVector(5, 90, 5), FRotator(0.f, A2, 0.f), Rail);
		}
	}

	// Lighting: overhead points, no shadows (the hull mesh is hidden from the bridge anyway).
	Light(FVector(250, 0, H - 95), LightIntensity, 1400.f);
	Light(C + FVector(0, 0, H - 95), LightIntensity * 0.8f, 1400.f);
	Light(C + FVector(-300, 260, H - 80), LightIntensity * 0.5f, 1100.f);
	Light(C + FVector(-300, -260, H - 80), LightIntensity * 0.5f, 1100.f);
}
