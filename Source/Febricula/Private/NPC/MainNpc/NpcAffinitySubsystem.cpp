// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/MainNpc/NpcAffinitySubsystem.h"

float UNpcAffinitySubsystem::GetAffinityValue(FName NpcId) const
{
	if (const float* Found = AffinityByNpcId.Find(NpcId))
	{
		return *Found;
	}
	return 0.f;
}

ENpcRelationshipLevel UNpcAffinitySubsystem::GetRelationshipLevel(FName NpcId) const
{
	const float Value = GetAffinityValue(NpcId);

	if (Value < WaryThreshold)
	{
		return ENpcRelationshipLevel::Wary;
	}
	if (Value < FavorThreshold)
	{
		return ENpcRelationshipLevel::Neutral;
	}
	if (Value < TrustThreshold)
	{
		return ENpcRelationshipLevel::Favor;
	}
	if (Value < IntimateThreshold)
	{
		return ENpcRelationshipLevel::Trust;
	}
	return ENpcRelationshipLevel::Intimate;
}

float UNpcAffinitySubsystem::AddAffinity(FName NpcId, float Delta)
{
	float& Value = AffinityByNpcId.FindOrAdd(NpcId);
	Value += Delta;
	return Value;
}

void UNpcAffinitySubsystem::SetAffinity(FName NpcId, float NewValue)
{
	AffinityByNpcId.Add(NpcId, NewValue);
}

bool UNpcAffinitySubsystem::IsRelationshipAtLeast(FName NpcId, ENpcRelationshipLevel MinimumLevel) const
{
	// enum 정의 순서가 서열이므로 정수 비교로 "이상" 조건을 그대로 검사한다.
	return static_cast<uint8>(GetRelationshipLevel(NpcId)) >= static_cast<uint8>(MinimumLevel);
}
