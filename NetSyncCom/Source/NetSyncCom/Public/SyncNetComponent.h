// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SyncNetComponent.generated.h"


USTRUCT(BlueprintType)
struct FNetStatev1
{
	GENERATED_BODY()
	UPROPERTY()
		float timestamp;
	UPROPERTY(NotReplicated)
		float localtimestamp;
	UPROPERTY()
		FVector position;
	UPROPERTY()
		FRotator rotation;
	UPROPERTY()
		FVector velocity;
	UPROPERTY()
		FVector angularVelocity;

	FNetStatev1()
	{
		timestamp = 0.0f;
		localtimestamp = 0.0f;
		position = FVector::ZeroVector;
		rotation = FRotator::ZeroRotator;
		velocity = FVector::ZeroVector;
		angularVelocity = FVector::ZeroVector;
	}
};

UENUM(BlueprintType)
enum class NetworkRolesv1 : uint8
{
	None, Owner, Server, Client, ClientSpawned
};
UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class NETSYNCCOM_API USyncNetComponent : public UActorComponent
{
	GENERATED_BODY()
	APawn* owner;
	//UStaticMeshComponent* Mesh;
	FVector prelocation;
	FVector velicity;
	float TickDeltaTime = 0;
	bool ShouldSyncWithServer = true;
	//Networking
	float GetLocalWorldTime()
	{
		return GetWorld()->GetTimeSeconds();
	}
	static float GetPercentBetweenValues(float Value, float Begin, float End)
	{
		return (Value - Begin) / (End - Begin);
	}
public:	
	// Sets default values for this component's properties
	USyncNetComponent();
	UPROPERTY(EditAnywhere, Category = "Vehicle - Network")
		bool ReplicateMovement;
	UPROPERTY(EditAnywhere, Category = "Vehicle - Network")
		float NetSendRate;
	UPROPERTY(EditAnywhere, Category = "Vehicle - Network")
		float NetTimeBehind;
	UPROPERTY(EditAnywhere, Category = "Vehicle - Network")
		float NetLerpStart;
	UPROPERTY(EditAnywhere, Category = "Vehicle - Network")
		float NetPositionTolerance;
	UPROPERTY(EditAnywhere, Category = "Vehicle - Network")
		float NetSmoothing;
	UPROPERTY(ReplicatedUsing = OnRep_RestState)
		FNetStatev1 RestState;
	UFUNCTION()
		void OnRep_RestState()
	{
		IsResting = (RestState.position != FVector::ZeroVector);
	}
	bool IsResting = false;
	TArray<FNetStatev1> StateQueue;
	FNetStatev1 LerpStartState;
	bool CreateNewStartState = true;
	float LastActiveTimestamp = 0;

	FTimerHandle NetSendTimer;
	UFUNCTION()
		void NetStateSend();

	/**Used to temporarily disable movement replication, does not change ReplicateMovement */
	UFUNCTION(BlueprintCallable, Category = "VehicleSystemPlugin")
		void SetShouldSyncWithServer(bool ShouldSync);

	void SetReplicationTimer(bool Enabled);
	FNetStatev1 CreateNetStateForNow();
	void AddStateToQueue(FNetStatev1 StateToAdd);
	void ClearQueue();
	void CalculateTimestamps();
	void SyncPhysics();
	void LerpToNetState(FNetStatev1 NextState, float CurrentServerTime);
	void ApplyExactNetState(FNetStatev1 State);

	void SetVehicleLocation(FVector NewPosition, FRotator NewRotation);
	bool isServer()
	{
		UWorld* World = GetWorld();
		return World ? (World->GetNetMode() != NM_Client) : false;
	}
	NetworkRolesv1 GetNetworkRole()
	{
		if (owner->IsLocallyControlled())
		{
			//I'm controlling this
			return NetworkRolesv1::Owner;
		}
		else if (isServer())
		{
			if (owner->IsPlayerControlled())
			{
				//I'm the server, and a client is controlling this
				return NetworkRolesv1::Server;
			}
			else
			{
				//I'm the server, and I'm controlling this because it's unpossessed
				return NetworkRolesv1::Owner;
			}
		}
		else if (owner->GetLocalRole() == ROLE_Authority)
		{
			//I'm not the server, I'm not controlling this, and I have authority.
			return NetworkRolesv1::ClientSpawned;
		}
		else
		{
			//I'm a client and I'm not controlling this
			return NetworkRolesv1::Client;
		}
		return NetworkRolesv1::None;
	}
	UFUNCTION(Server, unreliable, WithValidation)
		void Server_ReceiveNetState(FNetStatev1 State);
	virtual bool Server_ReceiveNetState_Validate(FNetStatev1 State);
	virtual void Server_ReceiveNetState_Implementation(FNetStatev1 State);
	UFUNCTION(NetMulticast, unreliable, WithValidation)
		void Client_ReceiveNetState(FNetStatev1 State);
	virtual bool Client_ReceiveNetState_Validate(FNetStatev1 State);
	virtual void Client_ReceiveNetState_Implementation(FNetStatev1 State);
	UFUNCTION(Server, reliable, WithValidation)
		void Server_ReceiveRestState(FNetStatev1 State);
	virtual bool Server_ReceiveRestState_Validate(FNetStatev1 State);
	virtual void Server_ReceiveRestState_Implementation(FNetStatev1 State);
protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const override;

		
};
