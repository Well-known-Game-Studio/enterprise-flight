// Slonk - the crew figure. See CrewFigure.h.
#include "CrewFigure.h"
#include "Engine/SkeletalMesh.h"
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

void UCrewFigureComponent::PoseSeated()
{
	if (!GetSkinnedAsset()) { return; }
	// Bends are about the figure's lateral axis (the asset faces +Y, so that is X). Children keep their local
	// transforms when a parent turns, so the shins and forearms are put back to their rest headings afterwards.
	auto Rest = [&](const TCHAR* B) { return GetBoneRotationByName(FName(B), EBoneSpaces::ComponentSpace).Quaternion(); };
	auto Set = [&](const TCHAR* B, const FQuat& Q) { SetBoneRotationByName(FName(B), Q.Rotator(), EBoneSpaces::ComponentSpace); };
	auto Turn = [&](float Deg) { return FQuat(FVector::XAxisVector, FMath::DegreesToRadians(BendSign * Deg)); };
	// The arm bones swing the other way for the same turn (their rest heading is the mirror of the legs').
	auto Arm = [&](float Deg) { return FQuat(FVector::XAxisVector, FMath::DegreesToRadians(-BendSign * Deg)); };
	const FQuat CalfL = Rest(TEXT("calf_l")), CalfR = Rest(TEXT("calf_r")), FootL = Rest(TEXT("foot_l")), FootR = Rest(TEXT("foot_r"));
	const FQuat ArmL = Rest(TEXT("lowerarm_l")), ArmR = Rest(TEXT("lowerarm_r")), HeadQ = Rest(TEXT("head"));
	Set(TEXT("spine_03"), Turn(-8.f) * Rest(TEXT("spine_03")));
	Set(TEXT("thigh_l"), Turn(88.f) * Rest(TEXT("thigh_l")));
	Set(TEXT("thigh_r"), Turn(88.f) * Rest(TEXT("thigh_r")));
	Set(TEXT("calf_l"), Turn(-2.f) * CalfL);
	Set(TEXT("calf_r"), Turn(-2.f) * CalfR);
	Set(TEXT("foot_l"), FootL);
	Set(TEXT("foot_r"), FootR);
	Set(TEXT("upperarm_l"), Arm(38.f) * Rest(TEXT("upperarm_l")));
	Set(TEXT("upperarm_r"), Arm(38.f) * Rest(TEXT("upperarm_r")));
	Set(TEXT("lowerarm_l"), Arm(68.f) * ArmL);
	Set(TEXT("lowerarm_r"), Arm(68.f) * ArmR);
	Set(TEXT("head"), Turn(6.f) * HeadQ);
}

FVector UCrewFigureComponent::HeadCentre()
{
	return GetBoneLocationByName(TEXT("head"), EBoneSpaces::ComponentSpace) + FVector(0.f, 0.f, 10.f);
}
