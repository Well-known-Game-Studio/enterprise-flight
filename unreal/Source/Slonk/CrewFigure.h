// Slonk - a crew member's figure: Epic's UE5 mannequin dressed by the crew material (M_Crew), shared by the person
// on foot and the officers seated at the bridge stations.

#pragma once

#include "CoreMinimal.h"
#include "Components/PoseableMeshComponent.h"
#include "CrewFigure.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkinnedMeshComponent;
class UTexture;

/** The colours M_Crew dresses the mannequin with, by rest-pose height. */
struct FCrewDress
{
	FLinearColor Torso, Legs, Head, Hands, Boots;
	float ClothWeight = 0.7f;
	/** The Wrath of Khan's jacket over black trousers, head and hands bare. */
	static FCrewDress Uniform(const FLinearColor& Jacket, const FLinearColor& Skin);
	/** The suit: one colour from the boots up, dark gloves and boots, the helmet over the head. */
	static FCrewDress Suit(const FLinearColor& Cloth, const FLinearColor& Trim);
};

namespace SlonkCrew
{
	inline const TCHAR* MannyPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
	inline const TCHAR* QuinnPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple");
	inline const TCHAR* CrewMaterialPath = TEXT("/Game/Slonk/Materials/M_Crew.M_Crew");
	/** One crew material instance per slot of Mesh, each fed that slot's own mannequin maps, assigned to the mesh. */
	void Dress(USkinnedMeshComponent* Mesh, UObject* Outer, UMaterialInterface* Crew, TArray<TObjectPtr<UMaterialInstanceDynamic>>& Out, UTexture* Base = nullptr, UTexture* Normal = nullptr, UTexture* MRA = nullptr);
	inline const TCHAR* UEFNPath = TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin");
	inline const TCHAR* UEFNBasePath = TEXT("/Game/Characters/UEFN_Mannequin/Textures/T_UEFN_Mannequin_D.T_UEFN_Mannequin_D");
	inline const TCHAR* UEFNNormalPath = TEXT("/Game/Characters/UEFN_Mannequin/Textures/T_UEFN_Mannequin_N.T_UEFN_Mannequin_N");
	void SetDress(const TArray<TObjectPtr<UMaterialInstanceDynamic>>& Mids, const FCrewDress& D);
}

/** A mannequin posed by hand: the seated officers of the bridge. */
UCLASS()
class SLONK_API UCrewFigureComponent : public UPoseableMeshComponent
{
	GENERATED_BODY()
public:
	UCrewFigureComponent();
	/** Load the mannequin (Quinn or Manny), dress it in the uniform, and sit it facing its own +Y (the asset's front). Call once registered. */
	void Setup(bool bQuinn, const FLinearColor& Jacket, const FLinearColor& Skin);
	/** The seated pose: thighs forward, shins down, forearms out to a console, a little lean back. Angles in degrees
	 *  about the figure's lateral axis, applied to each bone's rest heading in component space, parents first. */
	void PoseSeated();
	void PoseSeated(float Thigh, float Calf, float UpperArm, float Forearm, float Lean);
	/** The seated angles (degrees): thigh and calf from the rest heading, upper arm and forearm likewise, the lean of the spine. */
	UPROPERTY(EditAnywhere, Category = "Crew") float SeatThigh = 88.f;
	UPROPERTY(EditAnywhere, Category = "Crew") float SeatCalf = 0.f;
	UPROPERTY(EditAnywhere, Category = "Crew") float SeatUpperArm = 38.f;
	UPROPERTY(EditAnywhere, Category = "Crew") float SeatForearm = 68.f;
	UPROPERTY(EditAnywhere, Category = "Crew") float SeatLean = -8.f;
	/** The head's centre now, in component space (for a hair cap). */
	FVector HeadCentre();
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Mids;

};
