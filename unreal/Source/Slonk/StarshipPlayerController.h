// Slonk - Enhanced Input player controller for the starship. Builds its own input assets if none are assigned.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "StarshipPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class AStarshipPawn;

UCLASS()
class SLONK_API AStarshipPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AStarshipPlayerController();

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void PlayerTick(float DeltaTime) override;

	// ---- Tuning ----

	/** Mouse pixels -> virtual stick deflection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Mouse")
	float MouseSensitivity = 0.06f;

	/** How fast the virtual stick recentres when the mouse stops (1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Mouse")
	float MouseReturnRate = 5.f;

	/** Mouse forward = nose down when true (aircraft style). Default: mouse forward = nose up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Mouse")
	bool bInvertMousePitch = false;

	/** Degrees of camera free-look per mouse pixel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Mouse")
	float MouseLookSensitivity = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Gamepad")
	bool bInvertStickPitch = false;

	/** Degrees per second of camera free-look at full right-stick deflection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Gamepad")
	float StickLookRate = 120.f;

	/** Throttle change per second while holding W/S. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	float ThrottleRate = 0.5f;

	/** Throttle change per mouse-wheel notch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	float ThrottleWheelStep = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	float WarpFactorStep = 0.5f;

	/** Optional authored mapping context. If null, a default keyboard/mouse/gamepad context is generated at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	TObjectPtr<UInputMappingContext> MappingContext;

	// ---- Actions (generated if unset) ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_SteerMouse;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_SteerStick;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_LookStick;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_Roll;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_YawKeys;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_Throttle;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_ThrottleWheel;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_Impulse1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_Impulse2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_Impulse3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_Impulse4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_AllStop;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_Reverse;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_WarpToggle;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_WarpUp;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_WarpDown;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_FirePhasers;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_FireTorpedo;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_CycleTarget;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_ClearTarget;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_CycleCamera;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_FreeLook;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_ResetCamera;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_ToggleHUD;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_ToggleHelp;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_ToggleNavMarkers;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_ToggleMinimap;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_CycleNavTarget;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input|Actions") TObjectPtr<UInputAction> IA_ClearNavTarget;

	UFUNCTION(BlueprintPure, Category = "Starship|Input")
	FVector2D GetVirtualStick() const { return MouseStick; }

protected:
	void CreateDefaultInputAssets();
	AStarshipPawn* GetStarship() const;

	// Handlers
	void OnSteerMouse(const FInputActionValue& Value);
	void OnSteerStick(const FInputActionValue& Value);
	void OnSteerStickReleased(const FInputActionValue& Value);
	void OnLookStick(const FInputActionValue& Value);
	void OnRoll(const FInputActionValue& Value);
	void OnRollReleased(const FInputActionValue& Value);
	void OnYawKeys(const FInputActionValue& Value);
	void OnYawKeysReleased(const FInputActionValue& Value);
	void OnThrottle(const FInputActionValue& Value);
	void OnThrottleWheel(const FInputActionValue& Value);
	void OnImpulse1();
	void OnImpulse2();
	void OnImpulse3();
	void OnImpulse4();
	void OnAllStop();
	void OnReverse();
	void OnWarpToggle();
	void OnWarpUp();
	void OnWarpDown();
	void OnFirePhasersStarted();
	void OnFirePhasersCompleted();
	void OnFireTorpedo();
	void OnCycleTarget();
	void OnClearTarget();
	void OnCycleCamera();
	void OnFreeLookStarted();
	void OnFreeLookCompleted();
	void OnResetCamera();
	void OnToggleHUD();
	void OnToggleHelp();
	void OnToggleNavMarkers();
	void OnToggleMinimap();
	void OnCycleNavTarget();
	void OnClearNavTarget();

private:
	FVector2D MouseDeltaThisFrame = FVector2D::ZeroVector;
	FVector2D MouseStick = FVector2D::ZeroVector;
	FVector2D StickInput = FVector2D::ZeroVector;
	FVector2D LookStickInput = FVector2D::ZeroVector;
	float RollInput = 0.f;
	float YawKeyInput = 0.f;
	bool bFreeLookHeld = false;
};
