#include "Engine/StaticMeshActor.h"
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
