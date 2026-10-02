#include "Cache/ActorCacheSubsystem.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TargetPoint.h"
#include "GameMode/TargetTypeComponent.h"


void UTargetTypeComponent::ActivateTarget()
{
	UStaticMesh* Mesh = PropMesh.LoadSynchronous();
	if (!Mesh) return;
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	
	if (AStaticMeshActor* Actor = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), GetOwner()->GetActorLocation(), GetOwner()->GetActorRotation(), SpawnParams))
	{
		UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetStaticMesh(Mesh);
		Comp->SetSimulatePhysics(true);
	}
}

void UTargetTypeComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UActorCacheSubsystem* CacheSubsystem = GetWorld()->GetSubsystem<UActorCacheSubsystem>())
	{
		if (ATargetPoint* TargetPoint = Cast<ATargetPoint>(GetOwner()))
		{
			CacheSubsystem->AddActorToCache(TargetPoint);
		}
	}
}
