// Slonk - the crew figure. See CrewFigure.h.
#include "CrewFigure.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

FCrewDress FCrewDress::Uniform(const FLinearColor& Jacket, const FLinearColor& Skin)
{
	FCrewDress D;
	D.Torso = Jacket; D.Legs = FLinearColor(0.03f, 0.03f, 0.035f); D.Head = Skin; D.Hands = Skin; D.Boots = FLinearColor(0.02f, 0.02f, 0.022f);
	D.ClothWeight = 0.7f;
	return D;
}

FCrewDress FCrewDress::Suit(const FLinearColor& Cloth, const FLinearColor& Trim)
{
	FCrewDress D;
	D.Torso = Cloth; D.Legs = Cloth; D.Head = Cloth; D.Hands = Trim; D.Boots = Trim;
	D.ClothWeight = 0.4f;
	return D;
}

void SlonkCrew::Dress(USkinnedMeshComponent* Mesh, UObject* Outer, UMaterialInterface* Crew, TArray<TObjectPtr<UMaterialInstanceDynamic>>& Out)
{
	Out.Reset();
	USkeletalMesh* Asset = Mesh ? Cast<USkeletalMesh>(Mesh->GetSkinnedAsset()) : nullptr;
	if (!Asset || !Crew) { return; }
	const TArray<FSkeletalMaterial>& Slots = Asset->GetMaterials();
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Crew, Outer);
		// The mannequin's own instances name their maps Base Texture / BNormal / MRA; M_Crew's samplers Base / BNormal / MRA.
		static const TPair<FName, FName> TexParams[] = { { TEXT("Base Texture"), TEXT("Base") }, { TEXT("BNormal"), TEXT("BNormal") }, { TEXT("MRA"), TEXT("MRA") } };
		for (const auto& Pair : TexParams)
		{
			UTexture* Tex = nullptr;
			if (Slots[i].MaterialInterface && Slots[i].MaterialInterface->GetTextureParameterValue(FHashedMaterialParameterInfo(Pair.Key), Tex) && Tex)
			{
				Mid->SetTextureParameterValue(Pair.Value, Tex);
			}
		}
		Mesh->SetMaterial(i, Mid);
		Out.Add(Mid);
	}
}

void SlonkCrew::SetDress(const TArray<TObjectPtr<UMaterialInstanceDynamic>>& Mids, const FCrewDress& D)
{
	for (UMaterialInstanceDynamic* Mid : Mids)
	{
		if (!Mid) { continue; }
		Mid->SetVectorParameterValue(TEXT("Torso"), D.Torso);
		Mid->SetVectorParameterValue(TEXT("Legs"), D.Legs);
		Mid->SetVectorParameterValue(TEXT("Head"), D.Head);
		Mid->SetVectorParameterValue(TEXT("Hands"), D.Hands);
		Mid->SetVectorParameterValue(TEXT("Boots"), D.Boots);
		Mid->SetScalarParameterValue(TEXT("ClothWeight"), D.ClothWeight);
	}
}

UCrewFigureComponent::UCrewFigureComponent()
{
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	CastShadow = true;
	bAffectDistanceFieldLighting = false;
}

void UCrewFigureComponent::Setup(bool bQuinn, const FLinearColor& Jacket, const FLinearColor& Skin)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, bQuinn ? SlonkCrew::QuinnPath : SlonkCrew::MannyPath);
	if (!Mesh) { UE_LOG(LogTemp, Warning, TEXT("CrewFigure: mannequin missing")); return; }
	SetSkinnedAssetAndUpdate(Mesh);
	if (UMaterialInterface* Crew = LoadObject<UMaterialInterface>(nullptr, SlonkCrew::CrewMaterialPath))
	{
		SlonkCrew::Dress(this, GetOwner() ? static_cast<UObject*>(GetOwner()) : this, Crew, Mids);
		SlonkCrew::SetDress(Mids, FCrewDress::Uniform(Jacket, Skin));
	}
	PoseSeated();
}

void UCrewFigureComponent::PoseSeated() { PoseSeated(SeatThigh, SeatCalf, SeatUpperArm, SeatForearm, SeatLean); }

void UCrewFigureComponent::PoseSeated(float Thigh, float Calf, float UpperArm, float Forearm, float Lean)
{
	USkeletalMesh* SM = Cast<USkeletalMesh>(GetSkinnedAsset());
	if (!SM) { return; }
	// Every bone's rest heading in component space from the reference skeleton (the poseable mesh's own cache is
	// only refreshed on request), each turned about the figure's lateral axis (the asset faces +Y, so that is X)
	// by its angle; a bone with no angle keeps its rest heading, so a shin stays vertical when its thigh swings.
	const FReferenceSkeleton& Ref = SM->GetRefSkeleton();
	const TArray<FTransform>& RefPose = Ref.GetRefBonePose();
	const int32 N = Ref.GetNum();
	TArray<FQuat> RestComp; RestComp.SetNum(N);
	TMap<FName, float> Bends = {
		{ TEXT("spine_03"), Lean }, { TEXT("thigh_l"), Thigh }, { TEXT("thigh_r"), Thigh }, { TEXT("calf_l"), Calf }, { TEXT("calf_r"), Calf },
		{ TEXT("foot_l"), 0.f }, { TEXT("foot_r"), 0.f }, { TEXT("upperarm_l"), UpperArm }, { TEXT("upperarm_r"), UpperArm },
		{ TEXT("lowerarm_l"), Forearm }, { TEXT("lowerarm_r"), Forearm }, { TEXT("head"), 0.f } };
	for (int32 b = 0; b < N; ++b)
	{
		const int32 P = Ref.GetParentIndex(b);
		RestComp[b] = P == INDEX_NONE ? RefPose[b].GetRotation() : RestComp[P] * RefPose[b].GetRotation();
		if (const float* Deg = Bends.Find(Ref.GetBoneName(b)))
		{
			const FQuat Q = FQuat(FVector::XAxisVector, FMath::DegreesToRadians(*Deg)) * RestComp[b];
			SetBoneRotationByName(Ref.GetBoneName(b), Q.Rotator(), EBoneSpaces::ComponentSpace);
			// The conversion to bone space reads the parents' cached component transforms: refresh them for the next.
			RefreshBoneTransforms();
		}
	}
}

FVector UCrewFigureComponent::HeadCentre()
{
	return GetBoneLocationByName(TEXT("head"), EBoneSpaces::ComponentSpace) + FVector(0.f, 0.f, 10.f);
}
