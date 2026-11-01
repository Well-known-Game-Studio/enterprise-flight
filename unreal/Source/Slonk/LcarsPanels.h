// Slonk - the LCARS panels: main menu, controls, science log, engineering, tactical, medical.

#pragma once

#include "CoreMinimal.h"
#include "LcarsPanel.h"
#include "LcarsPanels.generated.h"

class UScrollBox;
class UProgressBar;
enum class EShipSystem : uint8;

/** Escape: ship status and the way to every other panel. */
UCLASS()
class SLONK_API UMainMenuPanel : public ULcarsPanel
{
	GENERATED_BODY()

protected:
	virtual void BuildContent() override;
	virtual FString PanelTitle() const override { return TEXT("Main Computer"); }
	virtual FVector2D PanelSize() const override { return FVector2D(1100.0, 640.0); }
};

/** H: every key and what it does, in two columns. */
UCLASS()
class SLONK_API UControlsPanel : public ULcarsPanel
{
	GENERATED_BODY()

protected:
	virtual void BuildContent() override;
	virtual FString PanelTitle() const override { return TEXT("Helm and Systems Controls"); }
	virtual FLinearColor FrameColor() const override { return Lcars::Periwinkle; }
};

/** Settings: the typeface and the HUD overlays, from the main menu. */
UCLASS()
class SLONK_API USettingsPanel : public ULcarsPanel
{
	GENERATED_BODY()

protected:
	virtual void BuildContent() override;
	virtual FString PanelTitle() const override { return TEXT("Settings"); }
	virtual FLinearColor FrameColor() const override { return Lcars::Tan; }
	virtual FVector2D PanelSize() const override { return FVector2D(1000.0, 560.0); }

private:
	/** A row of choices; the current one is lit. OnPick gets the index and should return the new current index. */
	void AddChoiceRow(const FString& Label, const TArray<FString>& Choices, int32 Current, TFunction<int32(int32)> OnPick);
	struct FChoiceRow { TArray<UButton*> Buttons; int32 Current = 0; };
	TArray<FChoiceRow> Rows;
	void Light(FChoiceRow& Row);
};

/** /: the science catalogue. */
UCLASS()
class SLONK_API UScienceLogPanel : public ULcarsPanel
{
	GENERATED_BODY()

protected:
	virtual void BuildContent() override;
	virtual FString PanelTitle() const override { return TEXT("Science Log"); }
	virtual FLinearColor FrameColor() const override { return Lcars::Gold; }

private:
	void FillRecords();
	UPROPERTY() TObjectPtr<UScrollBox> List;
	bool bDiscoveriesOnly = false;
	UPROPERTY() TObjectPtr<UButton> FilterButton;
};

/** , : the chief engineer's board. Every system's integrity and power share, the orders to divert power and send the
 *  damage-control team, and the bridge log of what the senior staff reported. */
UCLASS()
class SLONK_API UEngineeringPanel : public ULcarsPanel
{
	GENERATED_BODY()

protected:
	virtual void BuildContent() override;
	virtual void Refresh() override;
	virtual FString PanelTitle() const override { return TEXT("Engineering"); }
	virtual FLinearColor FrameColor() const override { return Lcars::Tan; }

private:
	struct FSystemRow { UProgressBar* Bar = nullptr; UButton* PowerButton = nullptr; UButton* RepairButton = nullptr; float LastIntegrity = -1.f; bool bLastPower = false; bool bLastRepair = false; };
	TArray<FSystemRow> Rows;
	UPROPERTY() TObjectPtr<UScrollBox> LogBox;
	int32 LastLogCount = -1;
	UPROPERTY() TObjectPtr<UButton> AutoRepairButton;
	UPROPERTY() TObjectPtr<UButton> ResupplyButton;
	bool bLastCanResupply = false;
	void FillLog();
};

/** The turbolift: where to, from wherever the crew member stands. Opened with F at a lift door. */
UCLASS()
class SLONK_API UTurboliftPanel : public ULcarsPanel
{
	GENERATED_BODY()

protected:
	virtual void BuildContent() override;
	virtual FString PanelTitle() const override { return TEXT("Turbolift"); }
	virtual FLinearColor FrameColor() const override { return Lcars::Tan; }
	virtual FVector2D PanelSize() const override { return FVector2D(760.0, 440.0); }
};

/** ; : the chief medical officer's board. The crew by condition, sickbay and its supplies, life support and the air,
 *  the senior staff and who is standing in for whom, triage, and the medical and operations reports. */
UCLASS()
class SLONK_API UMedicalPanel : public ULcarsPanel
{
	GENERATED_BODY()

protected:
	virtual void BuildContent() override;
	virtual void Refresh() override;
	virtual FString PanelTitle() const override { return TEXT("Sickbay and Life Support"); }
	virtual FLinearColor FrameColor() const override { return Lcars::Sky; }

private:
	UPROPERTY() TObjectPtr<UProgressBar> FitBar;
	UPROPERTY() TObjectPtr<UProgressBar> AirBar;
	UPROPERTY() TObjectPtr<UProgressBar> SuppliesBar;
	UPROPERTY() TObjectPtr<UButton> TriageButton;
	UPROPERTY() TObjectPtr<UButton> ResupplyButton;
	UPROPERTY() TObjectPtr<UScrollBox> LogBox;
	UPROPERTY() TObjectPtr<UVerticalBox> StaffBox;
	int32 LastLogRevision = -1;
	int32 LastTriage = -1;
	FString LastStaff;
	bool bLastCanResupply = false;
	void FillStaff();
};

/** . : the tactical officer's board. Alert state, shields and weapons, the locked target and the contacts in range. */
UCLASS()
class SLONK_API UTacticalPanel : public ULcarsPanel
{
	GENERATED_BODY()

protected:
	virtual void BuildContent() override;
	virtual void Refresh() override;
	virtual FString PanelTitle() const override { return TEXT("Tactical"); }
	virtual FLinearColor FrameColor() const override { return Lcars::Red; }

private:
	UPROPERTY() TObjectPtr<UScrollBox> Contacts;
	UPROPERTY() TArray<TObjectPtr<UButton>> AlertButtons;
	UPROPERTY() TArray<TObjectPtr<UButton>> ReinforceButtons;
	float ContactsTimer = 0.f;
	int32 LastAlert = -1;
	int32 LastReinforced = -2;
	void FillContacts();
};
