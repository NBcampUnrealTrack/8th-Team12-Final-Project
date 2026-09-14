// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UPrimitiveService.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogUPrimitiveService, Log, All);

// ============================================
// Helper Functions
// ============================================

UWorld* UPrimitiveService::GetEditorWorld()
{
	if (!GEditor)
	{
		UE_LOG(LogUPrimitiveService, Warning, TEXT("GetEditorWorld: GEditor is null"));
		return nullptr;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		UE_LOG(LogUPrimitiveService, Warning, TEXT("GetEditorWorld: No active editor world"));
	}
	return World;
}

UStaticMesh* UPrimitiveService::ResolveBasicShape(const FString& Type)
{
	if (Type.IsEmpty())
	{
		UE_LOG(LogUPrimitiveService, Warning, TEXT("ResolveBasicShape: Empty primitive type"));
		return nullptr;
	}

	// Normalize the requested type to the canonical basic shape name.
	FString Normalized;
	if (Type.Equals(TEXT("Cube"), ESearchCase::IgnoreCase))
	{
		Normalized = TEXT("Cube");
	}
	else if (Type.Equals(TEXT("Sphere"), ESearchCase::IgnoreCase))
	{
		Normalized = TEXT("Sphere");
	}
	else if (Type.Equals(TEXT("Cylinder"), ESearchCase::IgnoreCase))
	{
		Normalized = TEXT("Cylinder");
	}
	else if (Type.Equals(TEXT("Cone"), ESearchCase::IgnoreCase))
	{
		Normalized = TEXT("Cone");
	}
	else if (Type.Equals(TEXT("Plane"), ESearchCase::IgnoreCase))
	{
		Normalized = TEXT("Plane");
	}
	else
	{
		UE_LOG(LogUPrimitiveService, Warning, TEXT("ResolveBasicShape: Unsupported primitive type '%s'"), *Type);
		return nullptr;
	}

	// Engine basic shapes live at /Engine/BasicShapes/<X>.<X>
	const FString AssetPath = FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), *Normalized, *Normalized);

	UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Mesh)
	{
		UE_LOG(LogUPrimitiveService, Warning, TEXT("ResolveBasicShape: Failed to load basic shape mesh: %s"), *AssetPath);
	}
	return Mesh;
}

AActor* UPrimitiveService::FindActorInEditorWorld(const FString& ActorPathOrLabel)
{
	if (ActorPathOrLabel.IsEmpty())
	{
		return nullptr;
	}

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return nullptr;
	}

	// Iterate all actors in the editor world, matching by label, name, or path.
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		if (Actor->GetActorLabel().Equals(ActorPathOrLabel, ESearchCase::IgnoreCase) ||
			Actor->GetName().Equals(ActorPathOrLabel, ESearchCase::IgnoreCase) ||
			Actor->GetPathName().Equals(ActorPathOrLabel, ESearchCase::IgnoreCase))
		{
			return Actor;
		}
	}

	UE_LOG(LogUPrimitiveService, Warning, TEXT("FindActorInEditorWorld: Actor not found: %s"), *ActorPathOrLabel);
	return nullptr;
}

// ============================================
// Spawn Actions
// ============================================

FString UPrimitiveService::SpawnPrimitiveActor(
	const FString& PrimitiveType,
	FVector Location,
	FRotator Rotation,
	FVector Scale,
	const FString& ActorLabel)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return FString();
	}

	UStaticMesh* Mesh = ResolveBasicShape(PrimitiveType);
	if (!Mesh)
	{
		UE_LOG(LogUPrimitiveService, Warning, TEXT("SpawnPrimitiveActor: Could not resolve mesh for type '%s'"), *PrimitiveType);
		return FString();
	}

	// Spawn the static mesh actor at the requested transform. Spawns are
	// undoable by editor convention (Ctrl+Z removes the actor), so the
	// transaction covers the spawn itself.
	FScopedTransaction Transaction(NSLOCTEXT("PrimitiveService", "SpawnPrimitiveActor", "Spawn Primitive Actor"));
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	FTransform SpawnTransform(Rotation, Location, Scale);

	AStaticMeshActor* MeshActor = World->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(),
		SpawnTransform,
		SpawnParams);

	if (!MeshActor)
	{
		UE_LOG(LogUPrimitiveService, Warning, TEXT("SpawnPrimitiveActor: Failed to spawn AStaticMeshActor"));
		return FString();
	}

	// Assign the basic shape mesh to the actor's static mesh component.
	UStaticMeshComponent* MeshComponent = MeshActor->GetStaticMeshComponent();
	if (MeshComponent)
	{
		MeshComponent->SetMobility(EComponentMobility::Movable);
		MeshComponent->SetStaticMesh(Mesh);
	}
	else
	{
		UE_LOG(LogUPrimitiveService, Warning, TEXT("SpawnPrimitiveActor: Spawned actor has no static mesh component"));
	}

	// Apply the editor label if provided.
	if (!ActorLabel.IsEmpty())
	{
		MeshActor->SetActorLabel(ActorLabel);
	}

	// Ensure the transform is applied (scale especially).
	MeshActor->SetActorTransform(SpawnTransform);
	MeshActor->MarkPackageDirty();

	UE_LOG(LogUPrimitiveService, Display, TEXT("SpawnPrimitiveActor: Spawned %s as '%s' (%s)"),
		*PrimitiveType, *MeshActor->GetActorLabel(), *MeshActor->GetPathName());

	return MeshActor->GetPathName();
}

// ============================================
// Component Actions
// ============================================

FString UPrimitiveService::AddPrimitiveComponentToActor(
	const FString& ActorPathOrLabel,
	const FString& PrimitiveType,
	const FString& ComponentName)
{
	if (ComponentName.IsEmpty())
	{
		UE_LOG(LogUPrimitiveService, Warning, TEXT("AddPrimitiveComponentToActor: ComponentName is required"));
		return FString();
	}

	AActor* Actor = FindActorInEditorWorld(ActorPathOrLabel);
	if (!Actor)
	{
		return FString();
	}

	UStaticMesh* Mesh = ResolveBasicShape(PrimitiveType);
	if (!Mesh)
	{
		UE_LOG(LogUPrimitiveService, Warning, TEXT("AddPrimitiveComponentToActor: Could not resolve mesh for type '%s'"), *PrimitiveType);
		return FString();
	}

	// Create the new static mesh component owned by the actor.
	FScopedTransaction Transaction(NSLOCTEXT("PrimitiveService", "AddPrimitiveComponent", "Add Primitive Component"));
	Actor->Modify();
	UStaticMeshComponent* NewComponent = NewObject<UStaticMeshComponent>(Actor, FName(*ComponentName));
	if (!NewComponent)
	{
		UE_LOG(LogUPrimitiveService, Warning, TEXT("AddPrimitiveComponentToActor: Failed to create UStaticMeshComponent"));
		return FString();
	}

	NewComponent->SetMobility(EComponentMobility::Movable);
	NewComponent->SetStaticMesh(Mesh);

	// Attach to the actor's root if available, otherwise make this the root.
	USceneComponent* RootComp = Actor->GetRootComponent();
	if (RootComp)
	{
		NewComponent->SetupAttachment(RootComp);
		NewComponent->AttachToComponent(RootComp, FAttachmentTransformRules::KeepRelativeTransform);
	}
	else
	{
		Actor->SetRootComponent(NewComponent);
	}

	// Register the component with the world so it becomes live.
	NewComponent->RegisterComponent();
	Actor->AddInstanceComponent(NewComponent);

	Actor->MarkPackageDirty();

	UE_LOG(LogUPrimitiveService, Display, TEXT("AddPrimitiveComponentToActor: Added %s component '%s' to actor '%s'"),
		*PrimitiveType, *ComponentName, *Actor->GetActorLabel());

	return NewComponent->GetName();
}

// ============================================
// Discovery Actions
// ============================================

TArray<FString> UPrimitiveService::ListPrimitiveTypes()
{
	TArray<FString> Types;
	Types.Add(TEXT("Cube"));
	Types.Add(TEXT("Sphere"));
	Types.Add(TEXT("Cylinder"));
	Types.Add(TEXT("Cone"));
	Types.Add(TEXT("Plane"));
	return Types;
}
