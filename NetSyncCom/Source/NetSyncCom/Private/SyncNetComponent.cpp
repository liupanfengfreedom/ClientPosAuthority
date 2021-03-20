// Fill out your copyright notice in the Description page of Project Settings.


#include "SyncNetComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/KismetMathLibrary.h"
// Sets default values for this component's properties
USyncNetComponent::USyncNetComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
	// ...
	ReplicateMovement = true;
	NetSendRate = 0.05f;
	NetTimeBehind = 0.15f;
	NetLerpStart = 0.35f;
	NetPositionTolerance = 0.1f;
	NetSmoothing = 10.0f;
}
void USyncNetComponent::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USyncNetComponent, RestState);
}

// Called when the game starts
void USyncNetComponent::BeginPlay()
{
	Super::BeginPlay();
	owner = Cast<APawn>(GetOwner());
	check(owner)
	//Mesh = (UStaticMeshComponent*)owner->GetComponentByClass(UStaticMeshComponent::StaticClass());
	//check(Mesh)
	SetReplicationTimer(ReplicateMovement);// ...
	
}
void USyncNetComponent::SetShouldSyncWithServer(bool ShouldSync)
{
	ShouldSyncWithServer = ShouldSync;
	SetReplicationTimer(ShouldSync);
}
void USyncNetComponent::SetReplicationTimer(bool Enabled)
{
	if (ReplicateMovement && Enabled)
	{
		owner->GetWorldTimerManager().SetTimer(NetSendTimer, this, &USyncNetComponent::NetStateSend, NetSendRate, true);
	}
	else
	{
		owner->GetWorldTimerManager().ClearTimer(NetSendTimer);
		IsResting = false;
		ClearQueue();
	}
}

void USyncNetComponent::NetStateSend()
{
	if (GetNetworkRole() == NetworkRolesv1::Owner)
	{
		FNetStatev1 NewState = CreateNetStateForNow();

		//Check if resting
		if (NewState.velocity.Size() > 50) //Not resting
		{
			Server_ReceiveNetState(NewState);
			if (IsResting) //Is resting but should not be
			{
				FNetStatev1 BlankRestState;
				Server_ReceiveRestState(BlankRestState);
				if (owner->GetLocalRole() == ROLE_Authority) { OnRep_RestState(); } //RepNotify on Server
			}
		}
		else //Is resting
		{
			//Not resting but should be, or distance is too different
			if (!IsResting || FVector::DistXY(RestState.position, NewState.position) > 50)
			{
				Server_ReceiveRestState(NewState);
				if (owner->GetLocalRole() == ROLE_Authority) { OnRep_RestState(); } //RepNotify on Server
			}
		}

		if (StateQueue.Num() > 0)
		{
			ClearQueue(); //Clear the queue if we are the owner to avoid syncing to old states
		}
	}
}
FNetStatev1 USyncNetComponent::CreateNetStateForNow()
{
	FNetStatev1 newState;
	FTransform primTransform = owner->GetActorTransform();
	newState.position = primTransform.GetLocation();
	newState.rotation = primTransform.GetRotation().Rotator();
	newState.velocity = velicity;// Mesh->GetPhysicsLinearVelocity();
	//newState.angularVelocity = Mesh->GetPhysicsAngularVelocityInDegrees();
	newState.timestamp = GetLocalWorldTime();
	return newState;
}

bool USyncNetComponent::Server_ReceiveNetState_Validate(FNetStatev1 State)
{
	return true;
}
void USyncNetComponent::Server_ReceiveNetState_Implementation(FNetStatev1 State)
{
	Client_ReceiveNetState(State);
}

bool USyncNetComponent::Client_ReceiveNetState_Validate(FNetStatev1 State)
{
	return true;
}
void USyncNetComponent::Client_ReceiveNetState_Implementation(FNetStatev1 State)
{
	if (ShouldSyncWithServer)
	{
		AddStateToQueue(State);
	}
}

bool USyncNetComponent::Server_ReceiveRestState_Validate(FNetStatev1 State)
{
	return true;
}
void USyncNetComponent::Server_ReceiveRestState_Implementation(FNetStatev1 State)
{
	RestState = State; //Clients should still receive even when not actively syncing
	if (owner->GetLocalRole() == ROLE_Authority) { OnRep_RestState(); } //RepNotify on Server
}

void USyncNetComponent::AddStateToQueue(FNetStatev1 StateToAdd)
{
	if (GetNetworkRole() != NetworkRolesv1::Owner)
	{
		//If we have 10 or more states we are flooded and should drop new states
		if (StateQueue.Num() < 10)
		{
			StateToAdd.timestamp += NetTimeBehind; //Change the timestamp to the future so we can lerp

			if (StateToAdd.timestamp < LastActiveTimestamp)
			{
				return; //This state is late and should be discarded
			}

			if (StateQueue.IsValidIndex(0))
			{
				int8 lastindex = StateQueue.Num() - 1;
				for (int8 i = lastindex; i >= 0; --i)
				{
					if (StateQueue.IsValidIndex(i))
					{
						if (StateQueue[i].timestamp < StateToAdd.timestamp)
						{
							StateQueue.Insert(StateToAdd, i + 1);
							CalculateTimestamps();
							StateToAdd.localtimestamp = StateQueue[i + 1].timestamp;
							break;
						}
					}
				}
			}
			else
			{
				StateToAdd.localtimestamp = GetLocalWorldTime() + NetTimeBehind;
				StateQueue.Insert(StateToAdd, 0); //If the queue is empty just add it in the first spot
			}
		}
	}
}

void USyncNetComponent::ClearQueue()
{
	StateQueue.Empty();
	CreateNewStartState = true;
	LastActiveTimestamp = 0;
}

void USyncNetComponent::CalculateTimestamps()
{
	int8 lastindex = StateQueue.Num() - 1;
	for (int8 i = 0; i <= lastindex; i++)
	{
		//The first state is always our point of reference and should not change
		//Especially since it could be actively syncing
		if (i != 0)
		{
			if (StateQueue.IsValidIndex(i))
			{
				//Calculate the time difference in the owners times and apply it to our local times
				float timeDifference = StateQueue[i].timestamp - StateQueue[i - 1].timestamp;
				StateQueue[i].localtimestamp = StateQueue[i - 1].localtimestamp + timeDifference;
			}
		}
	}
}

void USyncNetComponent::SyncPhysics()
{
	if (IsResting)
	{
		SetVehicleLocation(RestState.position, RestState.rotation);
		if (StateQueue.Num() > 0)
		{
			ClearQueue(); //Queue should be empty while resting
		}
		return;
	}

	if (StateQueue.IsValidIndex(0))
	{
		FNetStatev1 NextState = StateQueue[0];
		float ServerTime = GetLocalWorldTime();

		//use physics until we are close enough to this timestamp
		if (ServerTime >= (NextState.localtimestamp - NetLerpStart))
		{
			if (CreateNewStartState)
			{
				LerpStartState = CreateNetStateForNow();
				CreateNewStartState = false;

				//If our start state is nearly equal to our end state, just skip it
				//This keeps the physics from looking weird when moving slowly, and allows physics to settle
				if (FMath::IsNearlyEqual(LerpStartState.position.X, NextState.position.X, NetPositionTolerance) &&
					FMath::IsNearlyEqual(LerpStartState.position.Y, NextState.position.Y, NetPositionTolerance) &&
					FMath::IsNearlyEqual(LerpStartState.position.Z, NextState.position.Z, NetPositionTolerance))
				{
					StateQueue.RemoveAt(0);
					CreateNewStartState = true;
					return;
				}
			}

			LastActiveTimestamp = NextState.timestamp;

			//Lerp To State
			//Our start state may have been created after the lerp start time, so choose whatever is latest
			float lerpBeginTime = LerpStartState.timestamp;
			float lerpPercent = FMath::Clamp(GetPercentBetweenValues(ServerTime, lerpBeginTime, NextState.localtimestamp), 0.0f, 1.0f);
			FVector NewPosition = UKismetMathLibrary::VLerp(LerpStartState.position, NextState.position, lerpPercent);
			FRotator NewRotation = UKismetMathLibrary::RLerp(LerpStartState.rotation, NextState.rotation, lerpPercent, true);
			SetVehicleLocation(NewPosition, NewRotation);

			if (lerpPercent >= 0.99f || lerpBeginTime > NextState.localtimestamp)
			{
				ApplyExactNetState(NextState);
				StateQueue.RemoveAt(0);
				CreateNewStartState = true;
			}
		}
	}
}
void USyncNetComponent::LerpToNetState(FNetStatev1 NextState, float CurrentServerTime)
{
	//Our start state may have been created after the lerp start time, so choose whatever is latest
	float lerpBeginTime = FMath::Max(LerpStartState.timestamp, (NextState.timestamp - NetLerpStart));

	float lerpPercent = FMath::Clamp(GetPercentBetweenValues(CurrentServerTime, lerpBeginTime, NextState.timestamp), 0.0f, 1.0f);

	FVector NewPosition = UKismetMathLibrary::VLerp(LerpStartState.position, NextState.position, lerpPercent);
	FRotator NewRotation = UKismetMathLibrary::RLerp(LerpStartState.rotation, NextState.rotation, lerpPercent, true);
	SetVehicleLocation(NewPosition, NewRotation);
}

void USyncNetComponent::ApplyExactNetState(FNetStatev1 State)
{
	SetVehicleLocation(State.position, State.rotation);
	//Mesh->SetPhysicsLinearVelocity(State.velocity);
	//Mesh->SetPhysicsAngularVelocityInDegrees(State.angularVelocity);
}
void USyncNetComponent::SetVehicleLocation(FVector NewPosition, FRotator NewRotation)
{
	//Move vehicle chassis
	if (FVector::DistXY(owner->GetActorLocation(), NewPosition) < 3000)
	{
		FVector SmoothPos = UKismetMathLibrary::VInterpTo(owner->GetActorLocation(), NewPosition, TickDeltaTime, NetSmoothing);
		FRotator SmoothRot = UKismetMathLibrary::RInterpTo(owner->GetActorRotation(), NewRotation, TickDeltaTime, NetSmoothing);
		owner->SetActorLocationAndRotation(SmoothPos, SmoothRot, false, nullptr, TeleportFlagToEnum(false));
	}
	else
	{
		//Calculate offset of new position
		FTransform VehicleTransform = owner->GetActorTransform();
		FVector OffsetLocation = NewPosition - VehicleTransform.GetLocation();
		FRotator OffsetRotation = NewRotation - VehicleTransform.Rotator();
		OffsetRotation.Normalize();
		FTransform Offset = FTransform(OffsetRotation, OffsetLocation, FVector::OneVector);

		//Teleport Vehicle chassis
		owner->SetActorLocationAndRotation(NewPosition, NewRotation, false, nullptr, TeleportFlagToEnum(false));
		//Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		//Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
}
// Called every frame
void USyncNetComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickDeltaTime = DeltaTime;
	NetworkRolesv1 CurrentRole = GetNetworkRole();
	if (CurrentRole != NetworkRolesv1::Owner)
	{
		if (ReplicateMovement && ShouldSyncWithServer)
		{
			SyncPhysics();
		}
	}
	// ...
	velicity = (owner->GetActorLocation()- prelocation)/ DeltaTime;
	prelocation = owner->GetActorLocation();
}

