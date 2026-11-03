// Slonk - a crew member's figure: Epic's UE5 mannequin dressed by the crew material (M_Crew), shared by the person
// on foot and the officers seated at the bridge stations.

#pragma once

#include "CoreMinimal.h"
#include "Components/PoseableMeshComponent.h"
#include "CrewFigure.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkinnedMeshComponent;

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
	void Dress(USkinnedMeshComponent* Mesh, UObject* Outer, UMaterialInterface* Crew, TArray<TObjectPtr<UMaterialInstanceDynamic>>& Out);
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
	/** The seated pose: thighs forward, shins down, forearms out to a console, a little lean back. */
	void PoseSeated();
	/** The head's centre now, in component space (for a hair cap). */
	FVector HeadCentre();
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Mids;
	/** The sense of the bends about the figure's lateral axis; flipped if the mannequin folds the wrong way. */
	UPROPERTY(EditAnywhere, Category = "Crew") float BendSign = -1.f;
};
