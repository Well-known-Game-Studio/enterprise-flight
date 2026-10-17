#include "StarshipPlayerController.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "StarshipHUD.h"
#include "StarshipMovementComponent.h"
#include "StarshipPawn.h"
#include "StarshipWeaponsComponent.h"
#include "WarpDriveComponent.h"

AStarshipPlayerController::AStarshipPlayerController()
{
	bShowMouseCursor = false;
	PrimaryActorTick.bCanEverTick = true;
}

AStarshipPawn* AStarshipPlayerController::GetStarship() const
{
	return Cast<AStarshipPawn>(GetPawn());
}

namespace
{
	UInputAction* MakeAction(UObject* Outer, const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, Name);
		Action->ValueType = Type;
		return Action;
	}

	FEnhancedActionKeyMapping& Map(UInputMappingContext* Ctx, UInputAction* Action, FKey Key)
	{
		return Ctx->MapKey(Action, Key);
	}

	void MapNegated(UInputMappingContext* Ctx, UInputAction* Action, FKey Key)
	{
		FEnhancedActionKeyMapping& M = Ctx->MapKey(Action, Key);
		UInputModifierNegate* Neg = NewObject<UInputModifierNegate>(Ctx);
		M.Modifiers.Add(Neg);
	}

	void MapAsY(UInputMappingContext* Ctx, UInputAction* Action, FKey Key, bool bNegate)
	{
		FEnhancedActionKeyMapping& M = Ctx->MapKey(Action, Key);
		UInputModifierSwizzleAxis* Swz = NewObject<UInputModifierSwizzleAxis>(Ctx);
		Swz->Order = EInputAxisSwizzle::YXZ;
		M.Modifiers.Add(Swz);
		if (bNegate)
		{
			M.Modifiers.Add(NewObject<UInputModifierNegate>(Ctx));
		}
	}

	void MapWithDeadZone(UInputMappingContext* Ctx, UInputAction* Action, FKey Key, float DeadZone)
	{
		FEnhancedActionKeyMapping& M = Ctx->MapKey(Action, Key);
		UInputModifierDeadZone* DZ = NewObject<UInputModifierDeadZone>(Ctx);
		DZ->LowerThreshold = DeadZone;
		DZ->Type = EDeadZoneType::Radial;
		M.Modifiers.Add(DZ);
	}
}

void AStarshipPlayerController::CreateDefaultInputAssets()
{
	auto Ensure = [this](TObjectPtr<UInputAction>& Slot, const TCHAR* Name, EInputActionValueType Type)
	{
		if (!Slot)
		{
			Slot = MakeAction(this, Name, Type);
		}
	};

	Ensure(IA_SteerMouse, TEXT("IA_SteerMouse"), EInputActionValueType::Axis2D);
	Ensure(IA_SteerStick, TEXT("IA_SteerStick"), EInputActionValueType::Axis2D);
	Ensure(IA_LookStick, TEXT("IA_LookStick"), EInputActionValueType::Axis2D);
	Ensure(IA_Roll, TEXT("IA_Roll"), EInputActionValueType::Axis1D);
	Ensure(IA_YawKeys, TEXT("IA_YawKeys"), EInputActionValueType::Axis1D);
	Ensure(IA_Throttle, TEXT("IA_Throttle"), EInputActionValueType::Axis1D);
	Ensure(IA_ThrottleWheel, TEXT("IA_ThrottleWheel"), EInputActionValueType::Axis1D);
	Ensure(IA_Impulse1, TEXT("IA_Impulse1"), EInputActionValueType::Boolean);
	Ensure(IA_Impulse2, TEXT("IA_Impulse2"), EInputActionValueType::Boolean);
	Ensure(IA_Impulse3, TEXT("IA_Impulse3"), EInputActionValueType::Boolean);
	Ensure(IA_Impulse4, TEXT("IA_Impulse4"), EInputActionValueType::Boolean);
	Ensure(IA_AllStop, TEXT("IA_AllStop"), EInputActionValueType::Boolean);
	Ensure(IA_Reverse, TEXT("IA_Reverse"), EInputActionValueType::Boolean);
	Ensure(IA_WarpToggle, TEXT("IA_WarpToggle"), EInputActionValueType::Boolean);
	Ensure(IA_WarpUp, TEXT("IA_WarpUp"), EInputActionValueType::Boolean);
	Ensure(IA_WarpDown, TEXT("IA_WarpDown"), EInputActionValueType::Boolean);
	Ensure(IA_FirePhasers, TEXT("IA_FirePhasers"), EInputActionValueType::Boolean);
	Ensure(IA_FireTorpedo, TEXT("IA_FireTorpedo"), EInputActionValueType::Boolean);
	Ensure(IA_CycleTarget, TEXT("IA_CycleTarget"), EInputActionValueType::Boolean);
	Ensure(IA_ClearTarget, TEXT("IA_ClearTarget"), EInputActionValueType::Boolean);
	Ensure(IA_CycleCamera, TEXT("IA_CycleCamera"), EInputActionValueType::Boolean);
	Ensure(IA_FreeLook, TEXT("IA_FreeLook"), EInputActionValueType::Boolean);
	Ensure(IA_ResetCamera, TEXT("IA_ResetCamera"), EInputActionValueType::Boolean);
	Ensure(IA_ToggleHUD, TEXT("IA_ToggleHUD"), EInputActionValueType::Boolean);
	Ensure(IA_ToggleHelp, TEXT("IA_ToggleHelp"), EInputActionValueType::Boolean);
	Ensure(IA_ToggleNavMarkers, TEXT("IA_ToggleNavMarkers"), EInputActionValueType::Boolean);
	Ensure(IA_ToggleMinimap, TEXT("IA_ToggleMinimap"), EInputActionValueType::Boolean);
	Ensure(IA_CycleNavTarget, TEXT("IA_CycleNavTarget"), EInputActionValueType::Boolean);
	Ensure(IA_ClearNavTarget, TEXT("IA_ClearNavTarget"), EInputActionValueType::Boolean);
	Ensure(IA_JumpToNavTarget, TEXT("IA_JumpToNavTarget"), EInputActionValueType::Boolean);
	Ensure(IA_ToggleWarpVisuals, TEXT("IA_ToggleWarpVisuals"), EInputActionValueType::Boolean);
	Ensure(IA_Vertical, TEXT("IA_Vertical"), EInputActionValueType::Axis1D);
	Ensure(IA_OrbitHold, TEXT("IA_OrbitHold"), EInputActionValueType::Boolean);
	Ensure(IA_TractorBeam, TEXT("IA_TractorBeam"), EInputActionValueType::Boolean);
	Ensure(IA_ThrusterFore, TEXT("IA_ThrusterFore"), EInputActionValueType::Axis1D);
	Ensure(IA_ThrusterSide, TEXT("IA_ThrusterSide"), EInputActionValueType::Axis1D);

	if (MappingContext)
	{
		return;
	}

	UInputMappingContext* Ctx = NewObject<UInputMappingContext>(this, TEXT("IMC_Starship"));

	// Steering: mouse (relative, virtual stick) and gamepad left stick.
	Map(Ctx, IA_SteerMouse, EKeys::MouseX);
	MapAsY(Ctx, IA_SteerMouse, EKeys::MouseY, false);
	MapWithDeadZone(Ctx, IA_SteerStick, EKeys::Gamepad_Left2D, 0.2f);
	MapWithDeadZone(Ctx, IA_LookStick, EKeys::Gamepad_Right2D, 0.2f);

	// Roll: A/D, bumpers.
	MapNegated(Ctx, IA_Roll, EKeys::A);
	Map(Ctx, IA_Roll, EKeys::D);
	MapNegated(Ctx, IA_Roll, EKeys::Gamepad_LeftShoulder);
	Map(Ctx, IA_Roll, EKeys::Gamepad_RightShoulder);

	// Yaw on keys: Q/E.
	MapNegated(Ctx, IA_YawKeys, EKeys::Q);
	Map(Ctx, IA_YawKeys, EKeys::E);

	// Throttle: W/S hold, D-pad up/down, wheel.
	Map(Ctx, IA_Throttle, EKeys::W);
	MapNegated(Ctx, IA_Throttle, EKeys::S);
	Map(Ctx, IA_Throttle, EKeys::Gamepad_DPad_Up);
	MapNegated(Ctx, IA_Throttle, EKeys::Gamepad_DPad_Down);
	Map(Ctx, IA_ThrottleWheel, EKeys::MouseWheelAxis);

	// Impulse presets.
	Map(Ctx, IA_Impulse1, EKeys::One);
	Map(Ctx, IA_Impulse2, EKeys::Two);
	Map(Ctx, IA_Impulse3, EKeys::Three);
	Map(Ctx, IA_Impulse4, EKeys::Four);
	Map(Ctx, IA_AllStop, EKeys::X);
	Map(Ctx, IA_AllStop, EKeys::Zero);
	Map(Ctx, IA_AllStop, EKeys::Gamepad_FaceButton_Right);
	Map(Ctx, IA_Reverse, EKeys::Z);

	// Warp.
	Map(Ctx, IA_WarpToggle, EKeys::J);
	Map(Ctx, IA_WarpToggle, EKeys::Gamepad_FaceButton_Top);
	Map(Ctx, IA_WarpUp, EKeys::RightBracket);
	Map(Ctx, IA_WarpUp, EKeys::Add);
	Map(Ctx, IA_WarpUp, EKeys::Gamepad_DPad_Right);
	Map(Ctx, IA_WarpDown, EKeys::LeftBracket);
	Map(Ctx, IA_WarpDown, EKeys::Subtract);
	Map(Ctx, IA_WarpDown, EKeys::Gamepad_DPad_Left);

	// Weapons.
	Map(Ctx, IA_FirePhasers, EKeys::LeftMouseButton);
	Map(Ctx, IA_FirePhasers, EKeys::F);
	Map(Ctx, IA_FirePhasers, EKeys::Gamepad_RightTrigger);
	Map(Ctx, IA_FireTorpedo, EKeys::SpaceBar);
	Map(Ctx, IA_FireTorpedo, EKeys::G);
	Map(Ctx, IA_FireTorpedo, EKeys::Gamepad_FaceButton_Bottom);
	Map(Ctx, IA_CycleTarget, EKeys::T);
	Map(Ctx, IA_CycleTarget, EKeys::Gamepad_FaceButton_Left);
	Map(Ctx, IA_ClearTarget, EKeys::Y);

	// Cameras.
	Map(Ctx, IA_CycleCamera, EKeys::V);
	Map(Ctx, IA_CycleCamera, EKeys::Gamepad_RightThumbstick);
	Map(Ctx, IA_FreeLook, EKeys::RightMouseButton);
	Map(Ctx, IA_FreeLook, EKeys::Gamepad_LeftTrigger);
	Map(Ctx, IA_ResetCamera, EKeys::R);
	Map(Ctx, IA_ToggleHUD, EKeys::Tab);
	Map(Ctx, IA_ToggleHelp, EKeys::H);
	Map(Ctx, IA_ToggleHelp, EKeys::Gamepad_Special_Left);

	// Navigation.
	Map(Ctx, IA_ToggleNavMarkers, EKeys::N);
	Map(Ctx, IA_ToggleMinimap, EKeys::M);
	Map(Ctx, IA_ToggleMinimap, EKeys::Gamepad_Special_Right);
	Map(Ctx, IA_CycleNavTarget, EKeys::L);
	Map(Ctx, IA_CycleNavTarget, EKeys::Gamepad_LeftThumbstick);
	Map(Ctx, IA_ClearNavTarget, EKeys::K);
	Map(Ctx, IA_JumpToNavTarget, EKeys::Home);
	Map(Ctx, IA_ToggleWarpVisuals, EKeys::U);

	// Vertical thrusters: Page Up / Page Down (hold). Orbit hold: O.
	Map(Ctx, IA_Vertical, EKeys::PageUp);
	MapNegated(Ctx, IA_Vertical, EKeys::PageDown);
	Map(Ctx, IA_OrbitHold, EKeys::O);
	Map(Ctx, IA_TractorBeam, EKeys::B);
	// Reaction control thrusters: arrow keys translate the hull (hold), fore/aft and port/starboard.
	Map(Ctx, IA_ThrusterFore, EKeys::Up);
	MapNegated(Ctx, IA_ThrusterFore, EKeys::Down);
	Map(Ctx, IA_ThrusterSide, EKeys::Right);
	MapNegated(Ctx, IA_ThrusterSide, EKeys::Left);

	MappingContext = Ctx;
}

void AStarshipPlayerController::BeginPlay()
{
	Super::BeginPlay();

	FInputModeGameOnly Mode;
	SetInputMode(Mode);
	bShowMouseCursor = false;

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			if (MappingContext)
			{
				Subsystem->AddMappingContext(MappingContext, 0);
			}
		}
	}
}

void AStarshipPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	CreateDefaultInputAssets();

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC)
	{
		UE_LOG(LogTemp, Error, TEXT("StarshipPlayerController: InputComponent is not an EnhancedInputComponent. Check DefaultInput.ini."));
		return;
	}

	EIC->BindAction(IA_SteerMouse, ETriggerEvent::Triggered, this, &AStarshipPlayerController::OnSteerMouse);
	EIC->BindAction(IA_SteerStick, ETriggerEvent::Triggered, this, &AStarshipPlayerController::OnSteerStick);
	EIC->BindAction(IA_SteerStick, ETriggerEvent::Completed, this, &AStarshipPlayerController::OnSteerStickReleased);
	EIC->BindAction(IA_LookStick, ETriggerEvent::Triggered, this, &AStarshipPlayerController::OnLookStick);
	EIC->BindAction(IA_LookStick, ETriggerEvent::Completed, this, &AStarshipPlayerController::OnLookStick);
	EIC->BindAction(IA_Roll, ETriggerEvent::Triggered, this, &AStarshipPlayerController::OnRoll);
	EIC->BindAction(IA_Roll, ETriggerEvent::Completed, this, &AStarshipPlayerController::OnRollReleased);
	EIC->BindAction(IA_YawKeys, ETriggerEvent::Triggered, this, &AStarshipPlayerController::OnYawKeys);
	EIC->BindAction(IA_YawKeys, ETriggerEvent::Completed, this, &AStarshipPlayerController::OnYawKeysReleased);
	EIC->BindAction(IA_Throttle, ETriggerEvent::Triggered, this, &AStarshipPlayerController::OnThrottle);
	EIC->BindAction(IA_ThrottleWheel, ETriggerEvent::Triggered, this, &AStarshipPlayerController::OnThrottleWheel);
	EIC->BindAction(IA_Impulse1, ETriggerEvent::Started, this, &AStarshipPlayerController::OnImpulse1);
	EIC->BindAction(IA_Impulse2, ETriggerEvent::Started, this, &AStarshipPlayerController::OnImpulse2);
	EIC->BindAction(IA_Impulse3, ETriggerEvent::Started, this, &AStarshipPlayerController::OnImpulse3);
	EIC->BindAction(IA_Impulse4, ETriggerEvent::Started, this, &AStarshipPlayerController::OnImpulse4);
	EIC->BindAction(IA_AllStop, ETriggerEvent::Started, this, &AStarshipPlayerController::OnAllStop);
	EIC->BindAction(IA_Reverse, ETriggerEvent::Started, this, &AStarshipPlayerController::OnReverse);
	EIC->BindAction(IA_WarpToggle, ETriggerEvent::Started, this, &AStarshipPlayerController::OnWarpToggle);
	EIC->BindAction(IA_WarpUp, ETriggerEvent::Started, this, &AStarshipPlayerController::OnWarpUp);
	EIC->BindAction(IA_WarpDown, ETriggerEvent::Started, this, &AStarshipPlayerController::OnWarpDown);
	EIC->BindAction(IA_FirePhasers, ETriggerEvent::Started, this, &AStarshipPlayerController::OnFirePhasersStarted);
	EIC->BindAction(IA_FirePhasers, ETriggerEvent::Completed, this, &AStarshipPlayerController::OnFirePhasersCompleted);
	EIC->BindAction(IA_FireTorpedo, ETriggerEvent::Started, this, &AStarshipPlayerController::OnFireTorpedo);
	EIC->BindAction(IA_CycleTarget, ETriggerEvent::Started, this, &AStarshipPlayerController::OnCycleTarget);
	EIC->BindAction(IA_ClearTarget, ETriggerEvent::Started, this, &AStarshipPlayerController::OnClearTarget);
	EIC->BindAction(IA_CycleCamera, ETriggerEvent::Started, this, &AStarshipPlayerController::OnCycleCamera);
	EIC->BindAction(IA_FreeLook, ETriggerEvent::Started, this, &AStarshipPlayerController::OnFreeLookStarted);
	EIC->BindAction(IA_FreeLook, ETriggerEvent::Completed, this, &AStarshipPlayerController::OnFreeLookCompleted);
	EIC->BindAction(IA_ResetCamera, ETriggerEvent::Started, this, &AStarshipPlayerController::OnResetCamera);
	EIC->BindAction(IA_ToggleHUD, ETriggerEvent::Started, this, &AStarshipPlayerController::OnToggleHUD);
	EIC->BindAction(IA_ToggleHelp, ETriggerEvent::Started, this, &AStarshipPlayerController::OnToggleHelp);
	EIC->BindAction(IA_ToggleNavMarkers, ETriggerEvent::Started, this, &AStarshipPlayerController::OnToggleNavMarkers);
	EIC->BindAction(IA_ToggleMinimap, ETriggerEvent::Started, this, &AStarshipPlayerController::OnToggleMinimap);
	EIC->BindAction(IA_CycleNavTarget, ETriggerEvent::Started, this, &AStarshipPlayerController::OnCycleNavTarget);
	EIC->BindAction(IA_ClearNavTarget, ETriggerEvent::Started, this, &AStarshipPlayerController::OnClearNavTarget);
	EIC->BindAction(IA_JumpToNavTarget, ETriggerEvent::Started, this, &AStarshipPlayerController::OnJumpToNavTarget);
	EIC->BindAction(IA_Vertical, ETriggerEvent::Triggered, this, &AStarshipPlayerController::OnVertical);
	EIC->BindAction(IA_Vertical, ETriggerEvent::Completed, this, &AStarshipPlayerController::OnVerticalReleased);
	EIC->BindAction(IA_ThrusterFore, ETriggerEvent::Triggered, this, &AStarshipPlayerController::OnThrusterFore);
	EIC->BindAction(IA_ThrusterFore, ETriggerEvent::Completed, this, &AStarshipPlayerController::OnThrusterForeReleased);
	EIC->BindAction(IA_ThrusterSide, ETriggerEvent::Triggered, this, &AStarshipPlayerController::OnThrusterSide);
	EIC->BindAction(IA_ThrusterSide, ETriggerEvent::Completed, this, &AStarshipPlayerController::OnThrusterSideReleased);
	EIC->BindAction(IA_OrbitHold, ETriggerEvent::Started, this, &AStarshipPlayerController::OnOrbitHold);
	EIC->BindAction(IA_TractorBeam, ETriggerEvent::Started, this, &AStarshipPlayerController::OnTractorBeam);
	EIC->BindAction(IA_ToggleWarpVisuals, ETriggerEvent::Started, this, &AStarshipPlayerController::OnToggleWarpVisuals);
}

void AStarshipPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (AStarshipPawn* Ship = Cast<AStarshipPawn>(InPawn))
	{
		if (Ship->Movement)
		{
			// Steering is written in PlayerTick; make sure the ship integrates after that.
			Ship->Movement->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);
		}
	}
}

void AStarshipPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	AStarshipPawn* Ship = GetStarship();
	if (!Ship || !Ship->Movement)
	{
		MouseDeltaThisFrame = FVector2D::ZeroVector;
		return;
	}

	float Pitch = 0.f;
	float Yaw = 0.f;

	if (bFreeLookHeld)
	{
		// Mouse drives the camera instead of the ship; stick keeps steering.
		Ship->AddLookInput(MouseDeltaThisFrame.X * MouseLookSensitivity, MouseDeltaThisFrame.Y * MouseLookSensitivity);
		MouseStick = FVector2D::ZeroVector;
	}
	else
	{
		MouseStick += MouseDeltaThisFrame * MouseSensitivity;
		MouseStick *= FMath::Exp(-MouseReturnRate * DeltaTime);
		MouseStick.X = FMath::Clamp(MouseStick.X, -1.f, 1.f);
		MouseStick.Y = FMath::Clamp(MouseStick.Y, -1.f, 1.f);
		Yaw = MouseStick.X;
		Pitch = bInvertMousePitch ? -MouseStick.Y : MouseStick.Y;
	}
	MouseDeltaThisFrame = FVector2D::ZeroVector;

	if (StickInput.SizeSquared() > 0.0025f)
	{
		Yaw = StickInput.X;
		Pitch = bInvertStickPitch ? -StickInput.Y : StickInput.Y;
	}

	// Right stick always looks.
	if (LookStickInput.SizeSquared() > 0.0025f)
	{
		Ship->AddLookInput(LookStickInput.X * StickLookRate * DeltaTime, LookStickInput.Y * StickLookRate * DeltaTime);
		Ship->SetFreeLook(true);
	}
	else
	{
		Ship->SetFreeLook(bFreeLookHeld);
	}

	if (!Ship->IsDebugSteering())
	{
		Ship->Movement->SetSteeringInput(Pitch, FMath::Clamp(Yaw + YawKeyInput, -1.f, 1.f), RollInput);
	}
	if (!Ship->IsDebugVertical())
	{
		Ship->Movement->SetVerticalInput(VerticalInput);
	}
	if (!Ship->IsDebugThrusting())
	{
		Ship->Movement->SetThrusterInput(FVector(ThrusterFore, ThrusterSide, 0.f));
	}
}

// ---- Handlers ----

void AStarshipPlayerController::OnSteerMouse(const FInputActionValue& Value)
{
	MouseDeltaThisFrame += Value.Get<FVector2D>();
}

void AStarshipPlayerController::OnSteerStick(const FInputActionValue& Value)
{
	StickInput = Value.Get<FVector2D>();
}

void AStarshipPlayerController::OnSteerStickReleased(const FInputActionValue& Value)
{
	StickInput = FVector2D::ZeroVector;
}

void AStarshipPlayerController::OnLookStick(const FInputActionValue& Value)
{
	LookStickInput = Value.Get<FVector2D>();
}

void AStarshipPlayerController::OnRoll(const FInputActionValue& Value)
{
	RollInput = Value.Get<float>();
}

void AStarshipPlayerController::OnRollReleased(const FInputActionValue& Value)
{
	RollInput = 0.f;
}

void AStarshipPlayerController::OnYawKeys(const FInputActionValue& Value)
{
	YawKeyInput = Value.Get<float>();
}

void AStarshipPlayerController::OnYawKeysReleased(const FInputActionValue& Value)
{
	YawKeyInput = 0.f;
}

void AStarshipPlayerController::OnThrottle(const FInputActionValue& Value)
{
	if (AStarshipPawn* Ship = GetStarship())
	{
		Ship->Movement->AdjustThrottle(Value.Get<float>() * ThrottleRate * GetWorld()->GetDeltaSeconds());
	}
}

void AStarshipPlayerController::OnThrottleWheel(const FInputActionValue& Value)
{
	if (AStarshipPawn* Ship = GetStarship())
	{
		// While free-looking the wheel frames the shot instead of changing speed.
		if (bFreeLookHeld)
		{
			Ship->AdjustChaseZoom(Value.Get<float>());
		}
		else
		{
			Ship->Movement->AdjustThrottle(Value.Get<float>() * ThrottleWheelStep);
		}
	}
}

void AStarshipPlayerController::OnImpulse1() { if (AStarshipPawn* Ship = GetStarship()) Ship->Movement->SetThrottle(0.25); }
void AStarshipPlayerController::OnImpulse2() { if (AStarshipPawn* Ship = GetStarship()) Ship->Movement->SetThrottle(0.5); }
void AStarshipPlayerController::OnImpulse3() { if (AStarshipPawn* Ship = GetStarship()) Ship->Movement->SetThrottle(0.75); }
void AStarshipPlayerController::OnImpulse4() { if (AStarshipPawn* Ship = GetStarship()) Ship->Movement->SetThrottle(1.0); }

void AStarshipPlayerController::OnAllStop()
{
	if (AStarshipPawn* Ship = GetStarship())
	{
		Ship->Movement->SetThrottle(0.0);
		if (Ship->WarpDrive && Ship->WarpDrive->IsWarpEngaged())
		{
			Ship->WarpDrive->DisengageWarp();
		}
	}
}

void AStarshipPlayerController::OnReverse()
{
	if (AStarshipPawn* Ship = GetStarship())
	{
		Ship->Movement->SetThrottle(-Ship->Movement->ReverseThrottleLimit);
	}
}

void AStarshipPlayerController::OnWarpToggle()
{
	if (AStarshipPawn* Ship = GetStarship())
	{
		if (Ship->WarpDrive)
		{
			Ship->WarpDrive->ToggleWarp();
		}
	}
}

void AStarshipPlayerController::OnWarpUp()
{
	if (AStarshipPawn* Ship = GetStarship())
	{
		if (Ship->WarpDrive)
		{
			Ship->WarpDrive->AdjustWarpFactor(WarpFactorStep);
		}
	}
}

void AStarshipPlayerController::OnWarpDown()
{
	if (AStarshipPawn* Ship = GetStarship())
	{
		if (Ship->WarpDrive)
		{
			Ship->WarpDrive->AdjustWarpFactor(-WarpFactorStep);
		}
	}
}

void AStarshipPlayerController::OnFirePhasersStarted()
{
	if (AStarshipPawn* Ship = GetStarship())
	{
		if (Ship->Weapons)
		{
			Ship->Weapons->StartPhasers();
		}
	}
}

void AStarshipPlayerController::OnFirePhasersCompleted()
{
	if (AStarshipPawn* Ship = GetStarship())
	{
		if (Ship->Weapons)
		{
			Ship->Weapons->StopPhasers();
		}
	}
}

void AStarshipPlayerController::OnFireTorpedo()
{
	if (AStarshipPawn* Ship = GetStarship())
	{
		if (Ship->Weapons)
		{
			Ship->Weapons->FireTorpedo();
		}
	}
}

void AStarshipPlayerController::OnCycleTarget()
{
	if (AStarshipPawn* Ship = GetStarship())
	{
		if (Ship->Weapons)
		{
			Ship->Weapons->CycleTarget();
		}
	}
}

void AStarshipPlayerController::OnClearTarget()
{
	if (AStarshipPawn* Ship = GetStarship())
	{
		if (Ship->Weapons)
		{
			Ship->Weapons->ClearTarget();
		}
	}
}

void AStarshipPlayerController::OnCycleCamera()
{
	if (AStarshipPawn* Ship = GetStarship())
	{
		Ship->CycleCameraMode();
	}
}

void AStarshipPlayerController::OnFreeLookStarted()
{
	bFreeLookHeld = true;
}

void AStarshipPlayerController::OnFreeLookCompleted()
{
	bFreeLookHeld = false;
}

void AStarshipPlayerController::OnResetCamera()
{
	if (AStarshipPawn* Ship = GetStarship())
	{
		Ship->ResetCamera();
	}
}

void AStarshipPlayerController::OnToggleHUD()
{
	if (AHUD* H = GetHUD())
	{
		H->ShowHUD();
	}
}

void AStarshipPlayerController::OnToggleHelp()
{
	if (AStarshipHUD* H = Cast<AStarshipHUD>(GetHUD()))
	{
		H->bShowHelp = !H->bShowHelp;
	}
}

void AStarshipPlayerController::OnToggleNavMarkers()
{
	if (AStarshipHUD* H = Cast<AStarshipHUD>(GetHUD())) { H->ToggleNavMarkers(); }
}

void AStarshipPlayerController::OnToggleMinimap()
{
	if (AStarshipHUD* H = Cast<AStarshipHUD>(GetHUD())) { H->ToggleMinimap(); }
}

void AStarshipPlayerController::OnCycleNavTarget()
{
	if (AStarshipHUD* H = Cast<AStarshipHUD>(GetHUD())) { H->CycleNavTarget(); }
}

void AStarshipPlayerController::OnClearNavTarget()
{
	if (AStarshipHUD* H = Cast<AStarshipHUD>(GetHUD())) { H->ClearNavTarget(); }
}

void AStarshipPlayerController::OnJumpToNavTarget()
{
	if (AStarshipPawn* Ship = GetStarship())
	{
		Ship->JumpToNavTarget();
	}
}

void AStarshipPlayerController::OnToggleWarpVisuals()
{
	if (AStarshipPawn* Ship = GetStarship())
	{
		Ship->ToggleWarpVisualMode();
	}
}

void AStarshipPlayerController::OnVertical(const FInputActionValue& Value)
{
	VerticalInput = Value.Get<float>();
}

void AStarshipPlayerController::OnVerticalReleased(const FInputActionValue& Value)
{
	VerticalInput = 0.f;
}

void AStarshipPlayerController::OnThrusterFore(const FInputActionValue& Value) { ThrusterFore = Value.Get<float>(); }
void AStarshipPlayerController::OnThrusterForeReleased(const FInputActionValue& Value) { ThrusterFore = 0.f; }
void AStarshipPlayerController::OnThrusterSide(const FInputActionValue& Value) { ThrusterSide = Value.Get<float>(); }
void AStarshipPlayerController::OnThrusterSideReleased(const FInputActionValue& Value) { ThrusterSide = 0.f; }

void AStarshipPlayerController::OnOrbitHold()
{
	if (AStarshipPawn* Ship = GetStarship())
	{
		Ship->ToggleOrbitHold();
	}
}

void AStarshipPlayerController::OnTractorBeam()
{
	if (AStarshipPawn* Ship = Cast<AStarshipPawn>(GetPawn()))
	{
		Ship->ToggleTractorBeam();
	}
}
