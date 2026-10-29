#include "RcsThrusterComponent.h"

#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SlonkMaterials.h"
#include "UObject/ConstructorHelpers.h"

URcsThrusterComponent::URcsThrusterComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	CastShadow = false;
	bCastDynamicShadow = false;
	SetReceivesDecals(false);
	NumCustomDataFloats = 4;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (ConeFinder.Succeeded())
	{
		SetStaticMesh(ConeFinder.Object);
	}
	ThrusterMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::ThrusterPath));
	FlareMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::ThrusterFlarePath));
}

void URcsThrusterComponent::OnRegister()
{
	Super::OnRegister();
	if (Flares || HasAnyFlags(RF_ClassDefaultObject) || !GetOwner())
	{
		return;
	}
	Flares = NewObject<UInstancedStaticMeshComponent>(GetOwner());
	Flares->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Flares->SetGenerateOverlapEvents(false);
	Flares->CastShadow = false;
	Flares->bCastDynamicShadow = false;
	Flares->SetReceivesDecals(false);
	Flares->NumCustomDataFloats = 3;
	if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		Flares->SetStaticMesh(Sphere);
	}
	Flares->SetupAttachment(this);
	Flares->RegisterComponent();
	for (int32 i = 0; i < MaxLights; ++i)
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(GetOwner());
		L->SetupAttachment(this);
		L->SetMobility(EComponentMobility::Movable);
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetIntensity(0.f);
		L->SetLightColor(PlumeColor);
		L->SetAttenuationRadius(LightRadius);
		L->SetSourceRadius(60.f);
		L->CastShadows = false;
		L->SetCastShadows(false);
		L->bUseInverseSquaredFalloff = true;
		L->SetVisibility(false);
		L->RegisterComponent();
		Lights.Add(L);
		LightNozzle.Add(INDEX_NONE);
	}
	if (Nozzles.Num() > 0)
	{
		SetNozzles(TArray<FRcsNozzle>(Nozzles));
	}
}

void URcsThrusterComponent::UpdateLights()
{
	// Lights stay with their nozzle while it burns; parked lights go to the brightest unlit nozzles.
	for (int32 i = 0; i < Lights.Num(); ++i)
	{
		const int32 N = LightNozzle[i];
		if (N != INDEX_NONE && (!Current.IsValidIndex(N) || Current[N] < 0.02f))
		{
			LightNozzle[i] = INDEX_NONE;
		}
	}
	TArray<int32> Candidates;
	for (int32 n = 0; n < Current.Num(); ++n)
	{
		if (Current[n] > 0.05f && !LightNozzle.Contains(n))
		{
			Candidates.Add(n);
		}
	}
	Candidates.Sort([this](int32 A, int32 B) { return Current[A] > Current[B]; });
	int32 Next = 0;
	for (int32 i = 0; i < Lights.Num(); ++i)
	{
		if (LightNozzle[i] == INDEX_NONE && Next < Candidates.Num())
		{
			LightNozzle[i] = Candidates[Next++];
		}
		UPointLightComponent* L = Lights[i];
		const int32 N = LightNozzle[i];
		if (!L)
		{
			continue;
		}
		if (N == INDEX_NONE)
		{
			if (L->IsVisible())
			{
				L->SetIntensity(0.f);
				L->SetVisibility(false);
			}
			continue;
		}
		const FRcsNozzle& Noz = Nozzles[N];
		L->SetRelativeLocation(Noz.Position + Noz.ExhaustDir * (PlumeLength * Noz.Size * 0.12f));
		L->SetIntensity(LightCandela * Noz.Size * Current[N] * (1.f + 1.5f * Flash[N]));
		if (!L->IsVisible())
		{
			L->SetVisibility(true);
		}
	}
}

void URcsThrusterComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureMaterial();
}

void URcsThrusterComponent::EnsureMaterial()
{
	if (MID)
	{
		return;
	}
	if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(ThrusterMaterial, TEXT("Thruster")))
	{
		MID = UMaterialInstanceDynamic::Create(Mat, this);
		MID->SetVectorParameterValue(TEXT("Color"), PlumeColor);
		MID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		SetMaterial(0, MID);
	}
	if (Flares && !FlareMID)
	{
		if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(FlareMaterial, TEXT("ThrusterFlare")))
		{
			FlareMID = UMaterialInstanceDynamic::Create(Mat, this);
			FlareMID->SetVectorParameterValue(TEXT("Color"), PlumeColor);
			FlareMID->SetScalarParameterValue(TEXT("Intensity"), FlareIntensity);
			Flares->SetMaterial(0, FlareMID);
		}
	}
}

void URcsThrusterComponent::SetNozzles(const TArray<FRcsNozzle>& InNozzles)
{
	ClearInstances();
	if (Flares)
	{
		Flares->ClearInstances();
	}
	Nozzles = InNozzles;
	NumCustomDataFloats = 4;
	Target.Init(0.f, Nozzles.Num());
	Current.Init(0.f, Nozzles.Num());
	Flash.Init(0.f, Nozzles.Num());
	// Instances 0..N-1 are the envelope cones, N..2N-1 the core cones, in nozzle order.
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 i = 0; i < Nozzles.Num(); ++i)
		{
			const FRcsNozzle& N = Nozzles[i];
			const FQuat Rot = FRotationMatrix::MakeFromZ(-N.ExhaustDir).ToQuat();
			const int32 Index = AddInstance(FTransform(Rot, N.Position, FVector(0.001f)), false);
			if (Index >= 0)
			{
				SetCustomDataValue(Index, 0, 0.f, false);
				SetCustomDataValue(Index, 1, 0.f, false);
				SetCustomDataValue(Index, 2, FMath::Frac(0.618034f * (i + 1)), false);
				SetCustomDataValue(Index, 3, (float)Pass, false);
			}
		}
	}
	for (int32 i = 0; i < Nozzles.Num(); ++i)
	{
		const FRcsNozzle& N = Nozzles[i];
		const float Seed = FMath::Frac(0.618034f * (i + 1));
		if (Flares)
		{
			const int32 F = Flares->AddInstance(FTransform(FQuat::Identity, N.Position, FVector(0.001f)), false);
			if (F >= 0)
			{
				Flares->SetCustomDataValue(F, 0, 0.f, false);
				Flares->SetCustomDataValue(F, 1, 0.f, false);
				Flares->SetCustomDataValue(F, 2, Seed, false);
			}
		}
	}
	bDirty = true;
	EnsureMaterial();
}

void URcsThrusterComponent::SetDemand(const FVector& TorquePitchYawRoll, const FVector& ForceFwdRightUp)
{
	// Attitude: rotate each nozzle's position by a small rotation with the demanded (pitch, yaw, roll) mix, the
	// same way the flight model integrates its rates (CurrentQuat * FQuat(FRotator)). A nozzle helps when its
	// thrust pushes its mounting point the way that rotation would carry it; no cross-product conventions to get
	// wrong. The small angle keeps the result linear in the demand.
	const float ScaleDeg = 5.f;
	const bool bTurning = !TorquePitchYawRoll.IsNearlyZero(1e-3);
	const FQuat Q = bTurning ? FQuat(FRotator(TorquePitchYawRoll.X * ScaleDeg, TorquePitchYawRoll.Y * ScaleDeg, TorquePitchYawRoll.Z * ScaleDeg)) : FQuat::Identity;
	const double Rad = FMath::DegreesToRadians(ScaleDeg);
	const FVector Force(FMath::Clamp(ForceFwdRightUp.X, -1.0, 1.0), FMath::Clamp(ForceFwdRightUp.Y, -1.0, 1.0), FMath::Clamp(ForceFwdRightUp.Z, -1.0, 1.0));

	for (int32 i = 0; i < Nozzles.Num(); ++i)
	{
		const FVector Thrust = -Nozzles[i].ExhaustDir;
		const FVector Arm = Nozzles[i].Position; // about the root, which is the hull's centre
		double Help = FVector::DotProduct(Thrust, Force);
		if (bTurning)
		{
			const FVector Swing = Q.RotateVector(Arm) - Arm; // where the rotation carries the nozzle
			const double ArmLen = FMath::Max(Arm.Size(), 1.0);
			Help += FVector::DotProduct(Thrust, Swing) / (ArmLen * Rad);
		}
		Target[i] = Help > Threshold ? (float)FMath::Clamp(Help, 0.0, 1.0) : 0.f;
	}
}

float URcsThrusterComponent::GetTotalActivation() const
{
	float Sum = 0.f;
	for (float A : Current)
	{
		Sum += A;
	}
	return Sum;
}

void URcsThrusterComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Clock += DeltaTime;
	if (AllOnTime > 0.f)
	{
		AllOnTime -= DeltaTime;
		for (float& Tgt : Target)
		{
			Tgt = 1.f;
		}
	}
	bool bAnyLit = false;
	const float FlashKeep = FMath::Exp(-FlashDecay * DeltaTime);
	for (int32 i = 0; i < Current.Num(); ++i)
	{
		// A nozzle lighting from cold overshoots: the ignition flash the materials brighten by, then decays.
		if (Target[i] > 0.05f && Current[i] < 0.05f)
		{
			Flash[i] = 1.f;
		}
		else
		{
			Flash[i] *= FlashKeep;
		}
		const float Rate = Target[i] > Current[i] ? AttackRate : DecayRate;
		Current[i] = FMath::Lerp(Current[i], Target[i], 1.f - FMath::Exp(-Rate * DeltaTime));
		if (Current[i] < 0.01f)
		{
			Current[i] = 0.f;
			Flash[i] = 0.f;
		}
		else
		{
			bAnyLit = true;
		}
	}
	if (bAnyLit || bDirty)
	{
		ApplyInstances();
		bDirty = bAnyLit;
	}
}

void URcsThrusterComponent::ApplyInstances()
{
	const int32 Count = Nozzles.Num();
	if (Count == 0 || GetInstanceCount() != Count * 2)
	{
		return;
	}
	const bool bFlares = Flares && Flares->GetInstanceCount() == Count;
	TArray<FTransform> Transforms;
	TArray<FTransform> FlareTransforms;
	Transforms.SetNum(Count * 2);
	FlareTransforms.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		const FRcsNozzle& N = Nozzles[i];
		const float A = Current[i];
		const float F = Flash[i];
		// A fast flicker on top of the activation (the material adds the filaments): a rocket plume is never steady.
		const float Flicker = 0.86f + 0.14f * FMath::Sin(Clock * 47.f + i * 2.3f) * FMath::Sin(Clock * 31.f + i * 1.1f);
		const float Len = PlumeLength * N.Size * (0.45f + 0.55f * A) * Flicker * (1.f + 0.25f * F);
		const float Wid = PlumeWidth * N.Size * (0.55f + 0.45f * A);
		// The engine cone is 100 cm tall with its apex at +Z and its origin midway: apex on the nozzle, base downstream.
		const FQuat Rot = FRotationMatrix::MakeFromZ(-N.ExhaustDir).ToQuat();
		const FVector Centre = N.Position + N.ExhaustDir * (Len * 0.5f);
		Transforms[i] = FTransform(Rot, Centre, A > 0.f ? FVector(Wid / 100.f, Wid / 100.f, Len / 100.f) : FVector(0.001f));
		SetCustomDataValue(i, 0, A * Flicker, false);
		SetCustomDataValue(i, 1, F, false);
		// The core cone: narrower and shorter, its own flicker phase.
		const float CoreFlicker = 0.9f + 0.1f * FMath::Sin(Clock * 61.f + i * 1.7f);
		const float CLen = Len * CoreLength * CoreFlicker;
		const float CWid = Wid * CoreWidth;
		Transforms[Count + i] = FTransform(Rot, N.Position + N.ExhaustDir * (CLen * 0.5f), A > 0.f ? FVector(CWid / 100.f, CWid / 100.f, CLen / 100.f) : FVector(0.001f));
		SetCustomDataValue(Count + i, 0, A * CoreFlicker, false);
		SetCustomDataValue(Count + i, 1, F, false);
		if (bFlares)
		{
			// The glow sphere (100 cm across) sits in the nozzle mouth, half proud of the plating.
			const float D = PlumeWidth * N.Size * FlareSize * (0.6f + 0.4f * A) * (1.f + 0.5f * F);
			const FVector FlareCentre = N.Position + N.ExhaustDir * (D * 0.3f);
			FlareTransforms.Add(FTransform(FQuat::Identity, FlareCentre, A > 0.f ? FVector(D / 100.f) : FVector(0.001f)));
			Flares->SetCustomDataValue(i, 0, A, false);
			Flares->SetCustomDataValue(i, 1, F, false);
		}
	}
	BatchUpdateInstancesTransforms(0, Transforms, false, true, false);
	if (bFlares)
	{
		Flares->BatchUpdateInstancesTransforms(0, FlareTransforms, false, true, false);
	}
	UpdateLights();
}
