// Slonk - the transporter's shimmer (see TransporterEffect.h).
#include "TransporterEffect.h"
#include "SlonkMaterials.h"
#include "RunningLightsComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ATransporterEffect::ATransporterEffect()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Sparkles = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Sparkles"));
	Sparkles->SetupAttachment(Root);
	Sparkles->SetMobility(EComponentMobility::Movable);
	Sparkles->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sparkles->SetGenerateOverlapEvents(false);
	Sparkles->CastShadow = false;
	Sparkles->bAffectDistanceFieldLighting = false;
	Sparkles->SetReceivesDecals(false);
	if (SphereFinder.Succeeded()) { Sparkles->SetStaticMesh(SphereFinder.Object); }

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Root);
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetIntensity(0.f);
	Light->SetAttenuationRadius(700.f);
	Light->SetLightColor(Color);
	Light->CastShadows = false;
	Light->SetRelativeLocation(FVector(0, 0, 110.f));

	// The running lights' additive bulb material: it carries the instanced-mesh usage flag (the lit panel material
	// does not, and an ISM falls back to the default grey without it) and glows without lighting.
	SparkleMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::RunningLightPath));
	Sparkles->NumCustomDataFloats = 6; // phase, period, mode, r, g, b, as the running lights lay them out
}

ATransporterEffect* ATransporterEffect::Spawn(UWorld* World, const FTransform& Feet, bool bMaterialise, float Seconds, USceneComponent* AttachTo, TFunction<void(float)> OnEnvelope)
{
	if (!World) { return nullptr; }
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATransporterEffect* E = World->SpawnActorDeferred<ATransporterEffect>(ATransporterEffect::StaticClass(), FTransform(Feet.GetRotation(), Feet.GetLocation()), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!E) { return nullptr; }
	E->bMaterialise = bMaterialise;
	E->Duration = FMath::Max(0.2f, Seconds);
	E->OnEnvelope = MoveTemp(OnEnvelope);
	E->FinishSpawning(FTransform(Feet.GetRotation(), Feet.GetLocation()));
	if (AttachTo) { E->AttachToComponent(AttachTo, FAttachmentTransformRules::KeepWorldTransform); }
	return E;
}

void ATransporterEffect::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(SparkleMaterial, TEXT("RunningLight"), false))
	{
		MID = UMaterialInstanceDynamic::Create(Mat, this);
		MID->SetScalarParameterValue(TEXT("Intensity"), 0.f);
		Sparkles->SetMaterial(0, MID);
	}
	Light->SetLightColor(Color);
	FRandomStream R((int32)GetUniqueID());
	Seeds.Reset(SparkleCount);
	for (int32 i = 0; i < SparkleCount; ++i)
	{
		FSparkle S;
		S.Angle = R.FRand() * 2.f * PI;
		S.Radius = RadiusCm * FMath::Sqrt(FMath::Lerp(0.04f, 1.f, R.FRand()));
		S.Height = R.FRand() * HeightCm;
		const bool bStreak = R.FRand() < StreakFraction;
		S.Rise = bStreak ? FMath::Lerp(130.f, 240.f, R.FRand()) : FMath::Lerp(35.f, 95.f, R.FRand());
		S.Phase = R.FRand() * 2.f * PI;
		S.Size = bStreak ? FMath::Lerp(0.7f, 1.3f, R.FRand()) : FMath::Lerp(2.2f, 4.2f, R.FRand());
		S.Length = bStreak ? FMath::Lerp(12.f, 30.f, R.FRand()) : S.Size;
		Seeds.Add(S);
		const int32 Index = Sparkles->AddInstance(FTransform(FQuat::Identity, FVector(S.Radius * FMath::Cos(S.Angle), S.Radius * FMath::Sin(S.Angle), S.Height), FVector(0.001f)), false);
		// Steady bulbs from the shimmer's blue to near white, each its own shade.
		const FLinearColor C = FMath::Lerp(Color, FLinearColor(0.9f, 0.96f, 1.f), R.FRand() * 0.7f);
		Sparkles->SetCustomDataValue(Index, 0, S.Phase, false);
		Sparkles->SetCustomDataValue(Index, 1, 1.f, false);
		Sparkles->SetCustomDataValue(Index, 2, (float)(uint8)ERunningLightMode::Steady, false);
		Sparkles->SetCustomDataValue(Index, 3, C.R, false);
		Sparkles->SetCustomDataValue(Index, 4, C.G, false);
		Sparkles->SetCustomDataValue(Index, 5, C.B, false);
	}
	Layout();
}

float ATransporterEffect::Envelope() const
{
	const float T = FMath::Clamp(Age / Duration, 0.f, 1.f);
	// Dematerialising: the shimmer gathers quickly and holds until the cut. Materialising: it stands bright for a
	// moment, then thins as the body takes over.
	return bMaterialise ? 1.f - FMath::SmoothStep(0.3f, 1.f, T) : FMath::SmoothStep(0.f, 0.35f, T);
}

void ATransporterEffect::Layout()
{
	const float E = Envelope();
	TArray<FTransform> Transforms;
	Transforms.Reserve(Seeds.Num());
	const float Dir = bMaterialise ? -1.f : 1.f;
	// The viewer's eye in the column's frame: whoever is beaming sees the shimmer from inside it.
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const FVector Eye = PC && PC->PlayerCameraManager ? GetActorTransform().InverseTransformPosition(PC->PlayerCameraManager->GetCameraLocation()) : FVector(1.0e6);
	for (const FSparkle& S : Seeds)
	{
		float H = FMath::Fmod(S.Height + Dir * S.Rise * Age, HeightCm);
		if (H < 0.f) { H += HeightCm; }
		const float A = S.Angle + Age * 1.7f;
		// Thinner at the top and bottom of the column, as the body is.
		const float Taper = 0.55f + 0.45f * FMath::Sin(PI * H / HeightCm);
		const float Twinkle = 0.5f + 0.5f * FMath::Sin(S.Phase + Age * 15.f);
		const FVector Pos(S.Radius * Taper * FMath::Cos(A), S.Radius * Taper * FMath::Sin(A), H);
		const float Near = FMath::Clamp((float)FVector::Dist(Pos, Eye) / EyeClearanceCm, 0.f, 1.f);
		const float K = (0.45f + 0.55f * Twinkle) * E * Near * Near / 100.f;
		const float Across = FMath::Max(S.Size * K, 0.0005f);
		Transforms.Add(FTransform(FQuat::Identity, Pos, FVector(Across, Across, FMath::Max(S.Length * K, 0.0005f))));
	}
	Sparkles->BatchUpdateInstancesTransforms(0, Transforms, false, true, true);
	if (MID) { MID->SetScalarParameterValue(TEXT("Intensity"), Emissive * E); }
	Light->SetIntensity(LightCandela * E);
	if (OnEnvelope) { OnEnvelope(E); }
}

void ATransporterEffect::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	Age += DeltaTime;
	if (Age >= Duration)
	{
		if (OnEnvelope) { OnEnvelope(0.f); }
		Destroy();
		return;
	}
	Layout();
}
