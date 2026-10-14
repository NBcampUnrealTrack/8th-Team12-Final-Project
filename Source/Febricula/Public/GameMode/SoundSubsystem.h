// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SoundSubsystem.generated.h"

/**
 * 
 */

class ATargetPoint;

USTRUCT(BlueprintType)
struct FSoundTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	TSoftObjectPtr<USoundBase> Sound;

	UPROPERTY(EditAnywhere)
	bool bLoop = false;
};

UCLASS()
class FEBRICULA_API USoundSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
protected:
	float MasterBGMVolume = 1.0f;
	float MasterSEVolume  = 1.0f;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UAudioComponent> BGMChannel;
	
	UPROPERTY(BlueprintReadOnly)
	TMap<FName, TObjectPtr<UAudioComponent>> SEChannels;
	UPROPERTY(BlueprintReadOnly)
	TMap<FName, FTimerHandle> SETimerHandles;
	
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UDataTable> SoundTable;
	
	UPROPERTY(BlueprintReadOnly)
	TMap<FName, FVector> SoundLocations;
	
public:
	void Initialize(FSubsystemCollectionBase& Collection) override;
	
	void PlayBGM(USoundBase* NewBGM, float FadeTime = .5f);
	void StopBGM(float FadeTime = .5f);
	void PlaySE(FName ChannelName, USoundBase* NewSE, float FadeTime = .5f);
	void PlaySE3d(FName ChannelName, USoundBase* NewSE, FVector Location, float FadeTime = .5f);
	void StopSE(FName ChannelName, float FadeTime = .5f);
	void SetBGMVolume(float Volume);
	void SetSEVolume(FName ChannelTag, float Volume);
	/** 모든 SE 채널 볼륨을 한 번에 변경하고, 이후 재생될 SE에도 적용 */
	void SetMasterSEVolume(float Volume);

	USoundBase* FoundSoundBase(FName SoundName);
	
	void RegisterSoundLoc(FName Tag, FVector Location);
	void UnregisterSoundLoc(FName Tag);
};
