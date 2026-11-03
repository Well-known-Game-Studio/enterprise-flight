// Slonk - the transporter's shimmer (see TransporterEffect.h).
#include "TransporterEffect.h"
#include "PersonPawn.h"
#include "SlonkMaterials.h"
#include "RunningLightsComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "UDynamicMesh.h"
#include "UObject/ConstructorHelpers.h"

ATransporterEffect::ATransporterEffect()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork; // after the body has moved this frame
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
	Sparkles->SetUsingAbsoluteLocation(true);
	Sparkles->SetUsingAbsoluteRotation(true);
	Sparkles->SetUsingAbsoluteScale(true);
	if (SphereFinder.Succeeded()) { Sparkles->SetStaticMesh(SphereFinder.Object); }
	// The running lights' additive bulb material: it carries the instanced-mesh usage flag (the lit panel material
	// does not, and an ISM falls back to the default grey without it) and glows without lighting.
	SparkleMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::RunningLightPath));
	Sparkles->NumCustomDataFloats = 6; // phase, period, mode, r, g, b, as the running lights lay them out
	System = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Slonk/FX/NS_Transporter.NS_Transporter")));

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Root);
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetIntensity(0.f);
	Light->SetAttenuationRadius(700.f);
	Light->SetLightColor(Color);
	Light->CastShadows = false;
	Light->SetRelativeLocation(FVector(0, 0, 110.f));
}

ATransporterEffect* ATransporterEffect::Spawn(UWorld* World, APersonPawn* Body, bool bMaterialise, float Seconds, TFunction<void(float)> OnEnvelope)
{
	if (!World || !Body) { return nullptr; }
	ATransporterEffect* E = World->SpawnActorDeferred<ATransporterEffect>(ATransporterEffect::StaticClass(), Body->GetActorTransform(), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!E) { return nullptr; }
	E->Body = Body;
	E->bMaterialise = bMaterialise;
	E->Duration = FMath::Max(0.2f, Seconds);
	E->OnEnvelope = MoveTemp(OnEnvelope);
	E->FinishSpawning(Body->GetActorTransform());
	return E;
}

FTransform ATransporterEffect::Feet() const
{
	const APersonPawn* P = Body.Get();
	if (!P) { return GetActorTransform(); }
	return FTransform(P->GetActorQuat(), P->GetActorLocation() - P->GetActorUpVector() * APersonPawn::HalfHeightCm);
}

void ATransporterEffect::SampleBody(FRandomStream& R)
{
	// Points round the figure's bones, spread by the flesh on each, each with a direction to drift out along.
	APersonPawn* P = Body.Get();
	USkeletalMeshComponent* Figure = P ? P->GetFigure() : nullptr;
	if (!Figure) { return; }
	Bones.Reset();
	struct FBone { FName Name; float Radius; float Length; double Cum; };
	struct FSpec { const TCHAR* Name; float Radius; float Length; };
	static const FSpec Spec[] = {
		{ TEXT("pelvis"), 15.f, 18.f }, { TEXT("spine_01"), 14.f, 14.f }, { TEXT("spine_02"), 14.f, 14.f }, { TEXT("spine_03"), 15.f, 16.f },
		{ TEXT("neck_01"), 7.f, 10.f }, { TEXT("head"), 11.f, 18.f },
		{ TEXT("clavicle_l"), 7.f, 14.f }, { TEXT("clavicle_r"), 7.f, 14.f }, { TEXT("upperarm_l"), 7.f, 28.f }, { TEXT("upperarm_r"), 7.f, 28.f },
		{ TEXT("lowerarm_l"), 6.f, 26.f }, { TEXT("lowerarm_r"), 6.f, 26.f }, { TEXT("hand_l"), 5.f, 14.f }, { TEXT("hand_r"), 5.f, 14.f },
		{ TEXT("thigh_l"), 9.f, 42.f }, { TEXT("thigh_r"), 9.f, 42.f }, { TEXT("calf_l"), 7.f, 40.f }, { TEXT("calf_r"), 7.f, 40.f },
		{ TEXT("foot_l"), 6.f, 16.f }, { TEXT("foot_r"), 6.f, 16.f } };
	TArray<FBone> Found;
	double Total = 0.0;
	for (const FSpec& Sp : Spec)
	{
		if (!Figure->DoesSocketExist(Sp.Name)) { continue; }
		Total += Sp.Radius * Sp.Length;
		Found.Add({ FName(Sp.Name), Sp.Radius, Sp.Length, Total });
	}
	if (Found.Num() == 0 || Total <= 0.0) { return; }
	for (const FBone& B : Found) { Bones.Add(B.Name); }
	for (int32 i = 0; i < BodySparkles; ++i)
	{
		// Pick a bone by its flesh, then a point on a capsule round it: along the bone (its +X), out to its radius.
		const double Pick = R.FRand() * Total;
		int32 Lo = 0, Hi = Found.Num() - 1;
		while (Lo < Hi) { const int32 Mid = (Lo + Hi) / 2; if (Found[Mid].Cum < Pick) { Lo = Mid + 1; } else { Hi = Mid; } }
		const FBone& B = Found[Lo];
		const float A = R.FRand() * 2.f * PI;
		FSparkle S;
		S.Part = Lo;
		S.Local = FVector(R.FRand() * B.Length, FMath::Cos(A) * B.Radius, FMath::Sin(A) * B.Radius);
		S.Drift = R.GetUnitVector();
		S.Angle = 0.f; S.Radius = 0.f; S.Height = 0.f;
		S.Rise = FMath::Lerp(4.f, 14.f, R.FRand());
		S.Phase = R.FRand() * 2.f * PI;
		S.Size = FMath::Lerp(1.8f, 3.4f, R.FRand());
		S.Length = S.Size;
		Seeds.Add(S);
	}
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
	// The Niagara system on the figure, if both exist; its body emitter samples the skeletal mesh it is attached to.
	if (UNiagaraSystem* Sys = System.LoadSynchronous())
	{
		if (APersonPawn* P = Body.Get())
		{
			if (USkeletalMeshComponent* Figure = P->GetFigure())
			{
				Niagara = UNiagaraFunctionLibrary::SpawnSystemAttached(Sys, Figure, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, false, true);
				if (Niagara)
				{
					Niagara->SetVariableFloat(TEXT("Direction"), bMaterialise ? -1.f : 1.f);
					Niagara->SetVariableLinearColor(TEXT("Color"), Color * NiagaraBrightness);
					UE_LOG(LogTemp, Log, TEXT("Transporter: Niagara on %s (attached to %s)"), *P->GetName(), *GetNameSafe(Niagara->GetAttachParent()));
					Niagara->SetVariableFloat(TEXT("Envelope"), 0.f);
				}
			}
		}
	}
	if (!Niagara) { UE_LOG(LogTemp, Log, TEXT("Transporter: %s not usable, sparkles stand in."), *System.ToString()); }
	FRandomStream R((int32)GetUniqueID());
	Seeds.Reset(BodySparkles + Streaks);
	if (Niagara) { Layout(); return; }
	SampleBody(R);
	for (int32 i = 0; i < Streaks; ++i)
	{
		FSparkle S;
		S.Part = -1;
		S.Local = FVector::ZeroVector; S.Drift = FVector::ZeroVector;
		S.Angle = R.FRand() * 2.f * PI;
		S.Radius = RadiusCm * FMath::Sqrt(FMath::Lerp(0.2f, 1.f, R.FRand()));
		S.Height = R.FRand() * HeightCm;
		S.Rise = FMath::Lerp(130.f, 240.f, R.FRand());
		S.Phase = R.FRand() * 2.f * PI;
		S.Size = FMath::Lerp(0.6f, 1.1f, R.FRand());
		S.Length = FMath::Lerp(10.f, 26.f, R.FRand());
		Seeds.Add(S);
	}
	for (const FSparkle& S : Seeds)
	{
		const int32 Index = Sparkles->AddInstance(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(0.001f)), true);
		// Steady bulbs from the shimmer's blue to a paler blue, each its own shade; never white.
		const FLinearColor C = FMath::Lerp(Color, FLinearColor(0.55f, 0.75f, 1.f), R.FRand() * 0.6f);
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

float ATransporterEffect::Dissolve() const
{
	const float T = FMath::Clamp(Age / Duration, 0.f, 1.f);
	// The figure goes part by part once the shimmer has it, and comes back the same way at the far end.
	return bMaterialise ? 1.f - FMath::SmoothStep(0.1f, 0.75f, T) : FMath::SmoothStep(0.3f, 0.95f, T);
}

void ATransporterEffect::Layout()
{
	const float E = Envelope();
	const float D = Dissolve();
	const FTransform Base = Feet();
	SetActorTransform(Base);
	TArray<FTransform> Transforms;
	Transforms.Reserve(Seeds.Num());
	const float Dir = bMaterialise ? -1.f : 1.f;
	// The viewer's eye: whoever is beaming may be seeing the shimmer from inside it.
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const FVector Eye = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector(1.0e9);
	const FVector Up = Base.GetRotation().GetUpVector();
	for (const FSparkle& S : Seeds)
	{
		FVector Pos;
		float Across, Along;
		const float Twinkle = 0.5f + 0.5f * FMath::Sin(S.Phase + Age * 15.f);
		if (S.Part >= 0)
		{
			// On the body, drifting out as the figure lets go of it (and in again as it forms).
			const APersonPawn* P = Body.Get();
			USkeletalMeshComponent* Figure = P ? P->GetFigure() : nullptr;
			if (!Figure || !Bones.IsValidIndex(S.Part)) { Transforms.Add(FTransform(FQuat::Identity, Base.GetLocation(), FVector(0.0005f))); continue; }
			const float Out = 2.f + 22.f * D;
			Pos = Figure->GetSocketTransform(Bones[S.Part]).TransformPosition(S.Local) + S.Drift * Out + Up * (Dir * S.Rise * Age);
			Across = S.Size * (0.5f + 0.5f * Twinkle) * E;
			Along = Across;
		}
		else
		{
			float H = FMath::Fmod(S.Height + Dir * S.Rise * Age, HeightCm);
			if (H < 0.f) { H += HeightCm; }
			const float A = S.Angle + Age * 1.4f;
			const float Taper = 0.6f + 0.4f * FMath::Sin(PI * H / HeightCm);
			Pos = Base.TransformPosition(FVector(S.Radius * Taper * FMath::Cos(A), S.Radius * Taper * FMath::Sin(A), H));
			Across = S.Size * (0.6f + 0.4f * Twinkle) * E;
			Along = S.Length * (0.6f + 0.4f * Twinkle) * E;
		}
		const float Near = FMath::Clamp((float)FVector::Dist(Pos, Eye) / EyeClearanceCm, 0.f, 1.f);
		const float K = Near * Near / 100.f;
		Transforms.Add(FTransform(Base.GetRotation(), Pos, FVector(FMath::Max(Across * K, 0.0005f), FMath::Max(Across * K, 0.0005f), FMath::Max(Along * K, 0.0005f))));
	}
	if (Seeds.Num() > 0) { Sparkles->BatchUpdateInstancesTransforms(0, Transforms, true, true, true); }
	if (Niagara) { Niagara->SetVariableFloat(TEXT("Envelope"), E); }
	if (MID) { MID->SetScalarParameterValue(TEXT("Intensity"), Emissive * E); }
	Light->SetIntensity(LightCandela * E);
	if (APersonPawn* P = Body.Get()) { P->SetTransportState(E, D); }
	if (OnEnvelope) { OnEnvelope(E); }
}

void ATransporterEffect::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	Age += DeltaTime;
	if (Age >= Duration)
	{
		if (OnEnvelope) { OnEnvelope(0.f); }
		// Dematerialised: the body stays gone (the cut and the far shimmer take over). Materialised: the suit is back.
		if (APersonPawn* P = Body.Get()) { P->SetTransportState(0.f, bMaterialise ? 0.f : 1.f); }
		if (Niagara)
		{
			// Stop spawning; the last particles live out their lifetime, then the component goes.
			Niagara->SetVariableFloat(TEXT("Envelope"), 0.f);
			Niagara->SetAutoDestroy(true);
			Niagara->Deactivate();
		}
		Destroy();
		return;
	}
	Layout();
}
