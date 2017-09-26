// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "CarPawn.h"
#include "CarWheelFront.h"
#include "CarWheelRear.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "common/common_utils/Utils.hpp"
#include "Components/InputComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundCue.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "WheeledVehicleMovementComponent4W.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Controller.h"
#include "AirBlueprintLib.h"
#include "PIPCamera.h"
#include "UObject/ConstructorHelpers.h"

// Needed for VR Headset
#if HMD_MODULE_INCLUDED
#include "IHeadMountedDisplay.h"
#endif // HMD_MODULE_INCLUDED

const FName ACarPawn::LookUpBinding("LookUp");
const FName ACarPawn::LookRightBinding("LookRight");
const FName ACarPawn::EngineAudioRPM("RPM");

#define LOCTEXT_NAMESPACE "VehiclePawn"

ACarPawn::ACarPawn()
{
    this->AutoPossessPlayer = EAutoReceiveInput::Player0;
    //this->AutoReceiveInput = EAutoReceiveInput::Player0;

    // Car mesh
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> CarMesh(TEXT("/AirSim/VehicleAdv/Vehicle/Vehicle_SkelMesh.Vehicle_SkelMesh"));
    GetMesh()->SetSkeletalMesh(CarMesh.Object);
    
    static ConstructorHelpers::FClassFinder<UObject> AnimBPClass(TEXT("/AirSim/VehicleAdv/Vehicle/VehicleAnimationBlueprint"));
    GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    GetMesh()->SetAnimInstanceClass(AnimBPClass.Class);

    // Setup friction materials
    static ConstructorHelpers::FObjectFinder<UPhysicalMaterial> SlipperyMat(TEXT("/AirSim/VehicleAdv/PhysicsMaterials/Slippery.Slippery"));
    SlipperyMaterial = SlipperyMat.Object;
        
    static ConstructorHelpers::FObjectFinder<UPhysicalMaterial> NonSlipperyMat(TEXT("/AirSim/VehicleAdv/PhysicsMaterials/NonSlippery.NonSlippery"));
    NonSlipperyMaterial = NonSlipperyMat.Object;

    static ConstructorHelpers::FClassFinder<APIPCamera> pip_camera_class(TEXT("Blueprint'/AirSim/Blueprints/BP_PIPCamera'"));
    pip_camera_class_ = pip_camera_class.Succeeded() ? pip_camera_class.Class : nullptr;

    UWheeledVehicleMovementComponent4W* Vehicle4W = CastChecked<UWheeledVehicleMovementComponent4W>(GetVehicleMovement());

    check(Vehicle4W->WheelSetups.Num() == 4);

    // Wheels/Tyres
    // Setup the wheels
    Vehicle4W->WheelSetups[0].WheelClass = UCarWheelFront::StaticClass();
    Vehicle4W->WheelSetups[0].BoneName = FName("PhysWheel_FL");
    Vehicle4W->WheelSetups[0].AdditionalOffset = FVector(0.f, -8.f, 0.f);

    Vehicle4W->WheelSetups[1].WheelClass = UCarWheelFront::StaticClass();
    Vehicle4W->WheelSetups[1].BoneName = FName("PhysWheel_FR");
    Vehicle4W->WheelSetups[1].AdditionalOffset = FVector(0.f, 8.f, 0.f);

    Vehicle4W->WheelSetups[2].WheelClass = UCarWheelRear::StaticClass();
    Vehicle4W->WheelSetups[2].BoneName = FName("PhysWheel_BL");
    Vehicle4W->WheelSetups[2].AdditionalOffset = FVector(0.f, -8.f, 0.f);

    Vehicle4W->WheelSetups[3].WheelClass = UCarWheelRear::StaticClass();
    Vehicle4W->WheelSetups[3].BoneName = FName("PhysWheel_BR");
    Vehicle4W->WheelSetups[3].AdditionalOffset = FVector(0.f, 8.f, 0.f);

    // Adjust the tire loading
    Vehicle4W->MinNormalizedTireLoad = 0.0f;
    Vehicle4W->MinNormalizedTireLoadFiltered = 0.2f;
    Vehicle4W->MaxNormalizedTireLoad = 2.0f;
    Vehicle4W->MaxNormalizedTireLoadFiltered = 2.0f;

    // Engine 
    // Torque setup
    Vehicle4W->MaxEngineRPM = 5700.0f;
    Vehicle4W->EngineSetup.TorqueCurve.GetRichCurve()->Reset();
    Vehicle4W->EngineSetup.TorqueCurve.GetRichCurve()->AddKey(0.0f, 400.0f);
    Vehicle4W->EngineSetup.TorqueCurve.GetRichCurve()->AddKey(1890.0f, 500.0f);
    Vehicle4W->EngineSetup.TorqueCurve.GetRichCurve()->AddKey(5730.0f, 400.0f);
 
    // Adjust the steering 
    Vehicle4W->SteeringCurve.GetRichCurve()->Reset();
    Vehicle4W->SteeringCurve.GetRichCurve()->AddKey(0.0f, 1.0f);
    Vehicle4W->SteeringCurve.GetRichCurve()->AddKey(40.0f, 0.7f);
    Vehicle4W->SteeringCurve.GetRichCurve()->AddKey(120.0f, 0.6f);
            
    // Transmission	
    // We want 4wd
    Vehicle4W->DifferentialSetup.DifferentialType = EVehicleDifferential4W::LimitedSlip_4W;
    
    // Drive the front wheels a little more than the rear
    Vehicle4W->DifferentialSetup.FrontRearSplit = 0.65;

    // Automatic gearbox
    Vehicle4W->TransmissionSetup.bUseGearAutoBox = true;
    Vehicle4W->TransmissionSetup.GearSwitchTime = 0.15f;
    Vehicle4W->TransmissionSetup.GearAutoBoxLatency = 1.0f;

    // Physics settings
    // Adjust the center of mass - the buggy is quite low
    UPrimitiveComponent* UpdatedPrimitive = Cast<UPrimitiveComponent>(Vehicle4W->UpdatedComponent);
    if (UpdatedPrimitive)
    {
        UpdatedPrimitive->BodyInstance.COMNudge = FVector(8.0f, 0.0f, 0.0f);
    }

    // Set the inertia scale. This controls how the mass of the vehicle is distributed.
    Vehicle4W->InertiaTensorScale = FVector(1.0f, 1.333f, 1.2f);

    // Create In-Car camera component 
    InternalCameraOrigin = FVector(-34.0f, -10.0f, 50.0f);
    InternalCameraBase = CreateDefaultSubobject<USceneComponent>(TEXT("InternalCameraBase"));
    InternalCameraBase->SetRelativeLocation(InternalCameraOrigin);
    InternalCameraBase->SetupAttachment(GetMesh());

    // In car HUD
    // Create text render component for in car speed display
    InCarSpeed = CreateDefaultSubobject<UTextRenderComponent>(TEXT("IncarSpeed"));
    InCarSpeed->SetRelativeScale3D(FVector(0.1f, 0.1f, 0.1f));
    InCarSpeed->SetRelativeLocation(FVector(35.0f, -6.0f, 20.0f));
    InCarSpeed->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
    InCarSpeed->SetupAttachment(GetMesh());
    InCarSpeed->SetVisibility(true);

    // Create text render component for in car gear display
    InCarGear = CreateDefaultSubobject<UTextRenderComponent>(TEXT("IncarGear"));
    InCarGear->SetRelativeScale3D(FVector(0.1f, 0.1f, 0.1f));
    InCarGear->SetRelativeLocation(FVector(35.0f, 5.0f, 20.0f));
    InCarGear->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
    InCarGear->SetupAttachment(GetMesh());
    InCarGear->SetVisibility(true);

    // Setup the audio component and allocate it a sound cue
    static ConstructorHelpers::FObjectFinder<USoundCue> SoundCue(TEXT("/AirSim/VehicleAdv/Sound/Engine_Loop_Cue.Engine_Loop_Cue"));
    EngineSoundComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("EngineSound"));
    EngineSoundComponent->SetSound(SoundCue.Object);
    EngineSoundComponent->SetupAttachment(GetMesh());

    // Colors for the in-car gear display. One for normal one for reverse
    GearDisplayReverseColor = FColor(255, 0, 0, 255);
    GearDisplayColor = FColor(255, 255, 255, 255);

    bIsLowFriction = false;
    bInReverseGear = false;

    wrapper_.reset(new VehiclePawnWrapper());
}

void ACarPawn::NotifyHit(class UPrimitiveComponent* MyComp, class AActor* Other, class UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, 
    FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
    wrapper_->onCollision(MyComp, Other, OtherComp, bSelfMoved, HitLocation,
        HitNormal, NormalImpulse, Hit);
}

void ACarPawn::initializeForBeginPlay()
{

    //put camera little bit above vehicle
    FTransform camera_transform(FVector(0, 0, 0));
    FActorSpawnParameters camera_spawn_params;
    camera_spawn_params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    InternalCamera = this->GetWorld()->SpawnActor<APIPCamera>(pip_camera_class_, camera_transform, camera_spawn_params);
    InternalCamera->AttachToComponent(InternalCameraBase, FAttachmentTransformRules::KeepRelativeTransform);

    setupInputBindings();

    std::vector<APIPCamera*> cameras = { InternalCamera };
    wrapper_->initialize(this, cameras);
}

void ACarPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (InternalCamera)
        InternalCamera->DetachFromActor(FDetachmentTransformRules::KeepRelativeTransform);
    InternalCamera = nullptr;
}


VehiclePawnWrapper* ACarPawn::getVehiclePawnWrapper()
{
    return wrapper_.get();
}

void ACarPawn::setupInputBindings()
{
    UAirBlueprintLib::EnableInput(this);

    UAirBlueprintLib::BindAxisToKey(FInputAxisKeyMapping("MoveForward", EKeys::Up, 1), this,
        this, &ACarPawn::MoveForward);

    &UAirBlueprintLib::BindAxisToKey(FInputAxisKeyMapping("MoveForward", EKeys::Down, -1), this,
        this, &ACarPawn::MoveForward);

    UAirBlueprintLib::BindAxisToKey(FInputAxisKeyMapping("MoveRight", EKeys::Right, 1), this,
        this, &ACarPawn::MoveRight);

    UAirBlueprintLib::BindAxisToKey(FInputAxisKeyMapping("MoveRight", EKeys::Left, -1), this,
        this, &ACarPawn::MoveRight);

    UAirBlueprintLib::BindActionToKey("Handbrake", EKeys::SpaceBar, this, &ACarPawn::OnHandbrakePressed, true);
    UAirBlueprintLib::BindActionToKey("Handbrake", EKeys::SpaceBar, this, &ACarPawn::OnHandbrakeReleased, false);

    //PlayerInputComponent->BindAxis("MoveForward", this, &ACarPawn::MoveForward);
    //PlayerInputComponent->BindAxis("MoveRight", this, &ACarPawn::MoveRight);
    //PlayerInputComponent->BindAxis(LookUpBinding);
    //PlayerInputComponent->BindAxis(LookRightBinding);

    //PlayerInputComponent->BindAction("Handbrake", IE_Pressed, this, &ACarPawn::OnHandbrakePressed);
    //PlayerInputComponent->BindAction("Handbrake", IE_Released, this, &ACarPawn::OnHandbrakeReleased);
    //PlayerInputComponent->BindAction("SwitchCamera", IE_Pressed, this, &ACarPawn::OnToggleCamera);

    //PlayerInputComponent->BindAction("ResetVR", IE_Pressed, this, &ACarPawn::OnResetVR); 
}

void ACarPawn::MoveForward(float Val)
{
    UAirBlueprintLib::LogMessage(TEXT("Throttle: "), FString::SanitizeFloat(Val), LogDebugLevel::Informational);

    GetVehicleMovementComponent()->SetThrottleInput(Val);

}

void ACarPawn::MoveRight(float Val)
{
    UAirBlueprintLib::LogMessage(TEXT("Steering: "), FString::SanitizeFloat(Val), LogDebugLevel::Informational);

    GetVehicleMovementComponent()->SetSteeringInput(Val);
}

void ACarPawn::OnHandbrakePressed()
{
    UAirBlueprintLib::LogMessage(TEXT("Handbreak: "), TEXT("Pressed"), LogDebugLevel::Informational);

    GetVehicleMovementComponent()->SetHandbrakeInput(true);
}

void ACarPawn::OnHandbrakeReleased()
{
    UAirBlueprintLib::LogMessage(TEXT("Handbreak: "), TEXT("Released"), LogDebugLevel::Informational);

    GetVehicleMovementComponent()->SetHandbrakeInput(false);
}

void ACarPawn::Tick(float Delta)
{
    Super::Tick(Delta);

    // Setup the flag to say we are in reverse gear
    bInReverseGear = GetVehicleMovement()->GetCurrentGear() < 0;
    
    // Update phsyics material
    UpdatePhysicsMaterial();

    // Update the strings used in the hud (incar and onscreen)
    UpdateHUDStrings();

    // Set the string in the incar hud
    UpdateInCarHUD();

    // Pass the engine RPM to the sound component
    float RPMToAudioScale = 2500.0f / GetVehicleMovement()->GetEngineMaxRotationSpeed();
    EngineSoundComponent->SetFloatParameter(EngineAudioRPM, GetVehicleMovement()->GetEngineRotationSpeed()*RPMToAudioScale);
}

void ACarPawn::BeginPlay()
{
    Super::BeginPlay();

    // Start an engine sound playing
    EngineSoundComponent->Play();
}

void ACarPawn::UpdateHUDStrings()
{
    float KPH = FMath::Abs(GetVehicleMovement()->GetForwardSpeed()) * 0.036f;
    int32 KPH_int = FMath::FloorToInt(KPH);
    int32 Gear = GetVehicleMovement()->GetCurrentGear();

    // Using FText because this is display text that should be localizable
    SpeedDisplayString = FText::Format(LOCTEXT("SpeedFormat", "{0} km/h"), FText::AsNumber(KPH_int));


    if (bInReverseGear == true)
    {
        GearDisplayString = FText(LOCTEXT("ReverseGear", "R"));
    }
    else
    {
        GearDisplayString = (Gear == 0) ? LOCTEXT("N", "N") : FText::AsNumber(Gear);
    }


    UAirBlueprintLib::LogMessage(TEXT("Speed: "), SpeedDisplayString.ToString(), LogDebugLevel::Informational);
    UAirBlueprintLib::LogMessage(TEXT("Gear: "), GearDisplayString.ToString(), LogDebugLevel::Informational);

}

void ACarPawn::UpdateInCarHUD()
{
    APlayerController* PlayerController = Cast<APlayerController>(GetController());
    if ((PlayerController != nullptr) && (InCarSpeed != nullptr) && (InCarGear != nullptr))
    {
        // Setup the text render component strings
        InCarSpeed->SetText(SpeedDisplayString);
        InCarGear->SetText(GearDisplayString);
        
        if (bInReverseGear == false)
        {
            InCarGear->SetTextRenderColor(GearDisplayColor);
        }
        else
        {
            InCarGear->SetTextRenderColor(GearDisplayReverseColor);
        }
    }
}

void ACarPawn::UpdatePhysicsMaterial()
{
    if (GetActorUpVector().Z < 0)
    {
        if (bIsLowFriction == true)
        {
            GetMesh()->SetPhysMaterialOverride(NonSlipperyMaterial);
            bIsLowFriction = false;
        }
        else
        {
            GetMesh()->SetPhysMaterialOverride(SlipperyMaterial);
            bIsLowFriction = true;
        }
    }
}

#undef LOCTEXT_NAMESPACE