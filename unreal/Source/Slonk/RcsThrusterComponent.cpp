#include "RcsThrusterComponent.h"

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
	NumCustomDataFloats = 1;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (ConeFinder.Succeeded())
	{
		SetStaticMesh(ConeFinder.Object);
	}
	ThrusterMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::ThrusterPath));
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
}

void URcsThrusterComponent::SetNozzles(const TArray<FRcsNozzle>& InNozzles)
{
	ClearInstances();
	Nozzles = InNozzles;
	NumCustomDataFloats = 1;
	Target.Init(0.f, Nozzles.Num());
	Current.Init(0.f, Nozzles.Num());
	for (const FRcsNozzle& N : Nozzles)
	{
		const FQuat Rot = FRotationMatrix::MakeFromZ(-N.ExhaustDir).ToQuat();
		const int32 Index = AddInstance(FTransform(Rot, N.Position, FVector(0.001f)), false);
		if (Index >= 0)
		{
			SetCustomDataValue(Index, 0, 0.f, false);
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
	for (int32 i = 0; i < Current.Num(); ++i)
	{
		const float Rate = Target[i] > Current[i] ? AttackRate : DecayRate;
		Current[i] = FMath::Lerp(Current[i], Target[i], 1.f - FMath::Exp(-Rate * DeltaTime));
		if (Current[i] < 0.01f)
		{
			Current[i] = 0.f;
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
	if (Nozzles.Num() == 0 || GetInstanceCount() != Nozzles.Num())
	{
		return;
	}
	TArray<FTransform> Transforms;
	Transforms.Reserve(Nozzles.Num());
	for (int32 i = 0; i < Nozzles.Num(); ++i)
	{
		const FRcsNozzle& N = Nozzles[i];
		const float A = Current[i];
		// A fast flicker on top of the activation: a rocket plume is never steady.
		const float Flicker = 0.8f + 0.2f * FMath::Sin(Clock * 47.f + i * 2.3f) * FMath::Sin(Clock * 31.f + i * 1.1f);
		const float Len = PlumeLength * N.Size * (0.4f + 0.6f * A) * Flicker;
		const float Wid = PlumeWidth * N.Size * (0.5f + 0.5f * A);
		// The engine cone is 100 cm tall with its apex at +Z and its origin midway: apex on the nozzle, base downstream.
		const FQuat Rot = FRotationMatrix::MakeFromZ(-N.ExhaustDir).ToQuat();
		const FVector Centre = N.Position + N.ExhaustDir * (Len * 0.5f);
		const FVector Scale = A > 0.f ? FVector(Wid / 100.f, Wid / 100.f, Len / 100.f) : FVector(0.001f);
		Transforms.Add(FTransform(Rot, Centre, Scale));
		SetCustomDataValue(i, 0, A * Flicker, false);
	}
	BatchUpdateInstancesTransforms(0, Transforms, false, true, false);
}
