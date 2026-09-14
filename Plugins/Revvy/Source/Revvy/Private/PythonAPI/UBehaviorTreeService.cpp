// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UBehaviorTreeService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyBT, Log, All);

// Editor / Engine
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

// AI
#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BrainComponent.h"

// Blackboard key types
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"

// Asset tools / library
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"

// Undo / transactions
#include "ScopedTransaction.h"

// Shared Revvy helpers
#include "Core/URevvyCommonHelpers.h"

// =================================================================
// Local helpers (private to this translation unit)
// =================================================================

namespace
{
	/**
	 * Resolve an AAIController from an actor query. The query may point at:
	 *  - an AAIController directly
	 *  - a APawn (returns its Controller if it is an AAIController)
	 *  - any actor that owns / possesses / IsA AAIController
	 */
	AAIController* ResolveAIController(const FString& Query)
	{
		AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, Query);
		if (!Actor)
		{
			return nullptr;
		}

		// Direct AIController
		if (AAIController* AsAIC = Cast<AAIController>(Actor))
		{
			return AsAIC;
		}

		// Pawn → Controller
		if (APawn* AsPawn = Cast<APawn>(Actor))
		{
			if (AAIController* PawnAIC = Cast<AAIController>(AsPawn->GetController()))
			{
				return PawnAIC;
			}
		}

		return nullptr;
	}

	/**
	 * Resolve the UBlackboardComponent for an AI query. AAIController exposes
	 * GetBlackboardComponent() directly.
	 */
	UBlackboardComponent* ResolveBlackboardComponent(const FString& Query)
	{
		AAIController* AIC = ResolveAIController(Query);
		if (!AIC)
		{
			return nullptr;
		}
		return AIC->GetBlackboardComponent();
	}

	/** Split "/Game/Foo/Bar" into ("/Game/Foo", "Bar"). Returns false on empty. */
	bool BehaviorTreeSplitPackagePath(const FString& AssetPath, FString& OutPackageDir, FString& OutAssetName)
	{
		if (AssetPath.IsEmpty())
		{
			return false;
		}
		OutPackageDir = FPackageName::GetLongPackagePath(AssetPath);
		OutAssetName  = FPackageName::GetShortName(AssetPath);
		return !OutPackageDir.IsEmpty() && !OutAssetName.IsEmpty();
	}

	/**
	 * Construct the correct UBlackboardKeyType_* CDO-instanced subobject for
	 * the given schema string, owned by the OwnerData asset. Returns nullptr
	 * for unrecognized / unsupported type strings.
	 */
	UBlackboardKeyType* MakeBlackboardKeyTypeFromString(
		UBlackboardData* OwnerData,
		const FString& KeyTypeStr)
	{
		if (!OwnerData)
		{
			return nullptr;
		}

		const FString Lower = KeyTypeStr.ToLower();

		if (Lower == TEXT("object"))
		{
			return NewObject<UBlackboardKeyType_Object>(OwnerData);
		}
		if (Lower == TEXT("bool"))
		{
			return NewObject<UBlackboardKeyType_Bool>(OwnerData);
		}
		if (Lower == TEXT("float"))
		{
			return NewObject<UBlackboardKeyType_Float>(OwnerData);
		}
		if (Lower == TEXT("int") || Lower == TEXT("int32") || Lower == TEXT("integer"))
		{
			return NewObject<UBlackboardKeyType_Int>(OwnerData);
		}
		if (Lower == TEXT("vector"))
		{
			return NewObject<UBlackboardKeyType_Vector>(OwnerData);
		}
		if (Lower == TEXT("string"))
		{
			return NewObject<UBlackboardKeyType_String>(OwnerData);
		}
		if (Lower == TEXT("name"))
		{
			return NewObject<UBlackboardKeyType_Name>(OwnerData);
		}
		if (Lower == TEXT("class"))
		{
			return NewObject<UBlackboardKeyType_Class>(OwnerData);
		}
		if (Lower == TEXT("enum"))
		{
			// Enum keys require an FEnum context (UEnum*) which callers must
			// supply out-of-band; we don't have an include contract for
			// UBlackboardKeyType_Enum in this iteration, so log and refuse.
			UE_LOG(LogRevvyBT, Warning,
				TEXT("MakeBlackboardKeyTypeFromString: Enum key type is not "
					 "supported in this iteration — set EnumType via editor UI."));
			return nullptr;
		}

		UE_LOG(LogRevvyBT, Warning,
			TEXT("MakeBlackboardKeyTypeFromString: unknown key type '%s'."),
			*KeyTypeStr);
		return nullptr;
	}
}

// =================================================================
// Asset Create
// =================================================================

FString UBehaviorTreeService::CreateBehaviorTree(
	const FString& OutputAssetPath,
	const FString& BlackboardDataPath)
{
	FString PackageDir, AssetName;
	if (!BehaviorTreeSplitPackagePath(OutputAssetPath, PackageDir, AssetName))
	{
		UE_LOG(LogRevvyBT, Warning,
			TEXT("CreateBehaviorTree: invalid OutputAssetPath '%s'."),
			*OutputAssetPath);
		return FString();
	}

	if (UEditorAssetLibrary::DoesAssetExist(OutputAssetPath))
	{
		UE_LOG(LogRevvyBT, Warning,
			TEXT("CreateBehaviorTree: asset already exists at '%s'."),
			*OutputAssetPath);
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("BehaviorTreeService",
		"CreateBehaviorTree", "Create Behavior Tree"));

	// Prefer CreatePackage + NewObject fallback — this avoids depending on
	// the editor-only UBehaviorTreeFactory (BehaviorTreeEditor module),
	// which is not currently declared in Revvy.Build.cs.
	const FString FullPackageName = PackageDir / AssetName;
	UPackage* Package = CreatePackage(*FullPackageName);
	if (!Package)
	{
		UE_LOG(LogRevvyBT, Warning,
			TEXT("CreateBehaviorTree: failed to create package '%s'."),
			*FullPackageName);
		return FString();
	}

	UBehaviorTree* NewBT = NewObject<UBehaviorTree>(
		Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (!NewBT)
	{
		UE_LOG(LogRevvyBT, Warning,
			TEXT("CreateBehaviorTree: failed to instantiate UBehaviorTree."));
		return FString();
	}

	// Optional blackboard binding
	if (!BlackboardDataPath.IsEmpty())
	{
		if (UObject* Loaded = UEditorAssetLibrary::LoadAsset(BlackboardDataPath))
		{
			if (UBlackboardData* BB = Cast<UBlackboardData>(Loaded))
			{
				NewBT->BlackboardAsset = BB;
			}
			else
			{
				UE_LOG(LogRevvyBT, Warning,
					TEXT("CreateBehaviorTree: '%s' is not a UBlackboardData — "
						 "leaving BlackboardAsset unset."),
					*BlackboardDataPath);
			}
		}
		else
		{
			UE_LOG(LogRevvyBT, Warning,
				TEXT("CreateBehaviorTree: could not load blackboard at '%s'."),
				*BlackboardDataPath);
		}
	}

	FAssetRegistryModule::AssetCreated(NewBT);
	Package->SetDirtyFlag(true);
	NewBT->MarkPackageDirty();
	NewBT->PostEditChange();

	UEditorAssetLibrary::SaveLoadedAsset(NewBT, /*bOnlyIfDirty*/ false);

	return NewBT->GetPathName();
}

FString UBehaviorTreeService::CreateBlackboardData(const FString& OutputAssetPath)
{
	FString PackageDir, AssetName;
	if (!BehaviorTreeSplitPackagePath(OutputAssetPath, PackageDir, AssetName))
	{
		UE_LOG(LogRevvyBT, Warning,
			TEXT("CreateBlackboardData: invalid OutputAssetPath '%s'."),
			*OutputAssetPath);
		return FString();
	}

	if (UEditorAssetLibrary::DoesAssetExist(OutputAssetPath))
	{
		UE_LOG(LogRevvyBT, Warning,
			TEXT("CreateBlackboardData: asset already exists at '%s'."),
			*OutputAssetPath);
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("BehaviorTreeService",
		"CreateBlackboardData", "Create Blackboard Data"));

	const FString FullPackageName = PackageDir / AssetName;
	UPackage* Package = CreatePackage(*FullPackageName);
	if (!Package)
	{
		UE_LOG(LogRevvyBT, Warning,
			TEXT("CreateBlackboardData: failed to create package '%s'."),
			*FullPackageName);
		return FString();
	}

	UBlackboardData* NewBB = NewObject<UBlackboardData>(
		Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (!NewBB)
	{
		UE_LOG(LogRevvyBT, Warning,
			TEXT("CreateBlackboardData: failed to instantiate UBlackboardData."));
		return FString();
	}

	FAssetRegistryModule::AssetCreated(NewBB);
	Package->SetDirtyFlag(true);
	NewBB->MarkPackageDirty();
	NewBB->PostEditChange();

	UEditorAssetLibrary::SaveLoadedAsset(NewBB, /*bOnlyIfDirty*/ false);

	return NewBB->GetPathName();
}

// =================================================================
// Blackboard Schema
// =================================================================

bool UBehaviorTreeService::AddBlackboardKey(
	const FString& BlackboardDataPath,
	const FString& KeyName,
	const FString& KeyTypeStr)
{
	if (BlackboardDataPath.IsEmpty() || KeyName.IsEmpty() || KeyTypeStr.IsEmpty())
	{
		UE_LOG(LogRevvyBT, Warning,
			TEXT("AddBlackboardKey: BlackboardDataPath / KeyName / KeyTypeStr required."));
		return false;
	}

	UObject* Loaded = UEditorAssetLibrary::LoadAsset(BlackboardDataPath);
	UBlackboardData* BB = Cast<UBlackboardData>(Loaded);
	if (!BB)
	{
		UE_LOG(LogRevvyBT, Warning,
			TEXT("AddBlackboardKey: could not load UBlackboardData at '%s'."),
			*BlackboardDataPath);
		return false;
	}

	// Reject duplicate keys — the editor allows shadowing across parent
	// chains, but for a service API a same-name collision is almost always
	// a mistake.
	for (const FBlackboardEntry& Existing : BB->Keys)
	{
		if (Existing.EntryName == FName(*KeyName))
		{
			UE_LOG(LogRevvyBT, Warning,
				TEXT("AddBlackboardKey: key '%s' already exists on '%s'."),
				*KeyName, *BlackboardDataPath);
			return false;
		}
	}

	FScopedTransaction Tx(NSLOCTEXT("BehaviorTreeService",
		"AddBlackboardKey", "Add Blackboard Key"));
	BB->Modify();

	FBlackboardEntry NewEntry;
	NewEntry.EntryName = FName(*KeyName);
	NewEntry.KeyType = MakeBlackboardKeyTypeFromString(BB, KeyTypeStr);
	if (!NewEntry.KeyType)
	{
		UE_LOG(LogRevvyBT, Warning,
			TEXT("AddBlackboardKey: unsupported KeyTypeStr '%s' — aborting."),
			*KeyTypeStr);
		return false;
	}

	BB->Keys.Add(NewEntry);

	BB->PostEditChange();
	BB->MarkPackageDirty();

	UEditorAssetLibrary::SaveLoadedAsset(BB, /*bOnlyIfDirty*/ false);
	return true;
}

// =================================================================
// Blackboard Runtime Values
// =================================================================

bool UBehaviorTreeService::SetBlackboardValueAsBool(
	const FString& AIControllerActorNameOrPath,
	const FString& KeyName,
	bool Value)
{
	UBlackboardComponent* BBComp = ResolveBlackboardComponent(AIControllerActorNameOrPath);
	if (!BBComp)
	{
		UE_LOG(LogRevvyBT, Warning,
			TEXT("SetBlackboardValueAsBool: no UBlackboardComponent for '%s'."),
			*AIControllerActorNameOrPath);
		return false;
	}

	BBComp->SetValueAsBool(FName(*KeyName), Value);
	return true;
}

bool UBehaviorTreeService::SetBlackboardValueAsFloat(
	const FString& AIControllerActorNameOrPath,
	const FString& KeyName,
	float Value)
{
	UBlackboardComponent* BBComp = ResolveBlackboardComponent(AIControllerActorNameOrPath);
	if (!BBComp)
	{
		UE_LOG(LogRevvyBT, Warning,
			TEXT("SetBlackboardValueAsFloat: no UBlackboardComponent for '%s'."),
			*AIControllerActorNameOrPath);
		return false;
	}

	BBComp->SetValueAsFloat(FName(*KeyName), Value);
	return true;
}

bool UBehaviorTreeService::SetBlackboardValueAsObject(
	const FString& AIControllerActorNameOrPath,
	const FString& KeyName,
	const FString& ObjectPath)
{
	UBlackboardComponent* BBComp = ResolveBlackboardComponent(AIControllerActorNameOrPath);
	if (!BBComp)
	{
		UE_LOG(LogRevvyBT, Warning,
			TEXT("SetBlackboardValueAsObject: no UBlackboardComponent for '%s'."),
			*AIControllerActorNameOrPath);
		return false;
	}

	UObject* LoadedObj = nullptr;
	if (!ObjectPath.IsEmpty())
	{
		LoadedObj = UEditorAssetLibrary::LoadAsset(ObjectPath);
		if (!LoadedObj)
		{
			// Not fatal — engine SetValueAsObject accepts nullptr as "clear".
			UE_LOG(LogRevvyBT, Warning,
				TEXT("SetBlackboardValueAsObject: could not load '%s' — "
					 "clearing key."),
				*ObjectPath);
		}
	}

	BBComp->SetValueAsObject(FName(*KeyName), LoadedObj);
	return true;
}

// =================================================================
// BT Runtime
// =================================================================

bool UBehaviorTreeService::RunBehaviorTree(
	const FString& AIControllerActorNameOrPath,
	const FString& BTAssetPath)
{
	AAIController* AIC = ResolveAIController(AIControllerActorNameOrPath);
	if (!AIC)
	{
		UE_LOG(LogRevvyBT, Warning,
			TEXT("RunBehaviorTree: no AAIController for '%s'."),
			*AIControllerActorNameOrPath);
		return false;
	}

	UObject* Loaded = UEditorAssetLibrary::LoadAsset(BTAssetPath);
	UBehaviorTree* BT = Cast<UBehaviorTree>(Loaded);
	if (!BT)
	{
		UE_LOG(LogRevvyBT, Warning,
			TEXT("RunBehaviorTree: could not load UBehaviorTree at '%s'."),
			*BTAssetPath);
		return false;
	}

	const bool bStarted = AIC->RunBehaviorTree(BT);
	if (!bStarted)
	{
		UE_LOG(LogRevvyBT, Warning,
			TEXT("RunBehaviorTree: AAIController::RunBehaviorTree returned false for '%s'."),
			*AIControllerActorNameOrPath);
		return false;
	}
	return true;
}

bool UBehaviorTreeService::StopBehaviorTree(const FString& AIControllerActorNameOrPath)
{
	AAIController* AIC = ResolveAIController(AIControllerActorNameOrPath);
	if (!AIC)
	{
		UE_LOG(LogRevvyBT, Warning,
			TEXT("StopBehaviorTree: no AAIController for '%s'."),
			*AIControllerActorNameOrPath);
		return false;
	}

	UBrainComponent* Brain = AIC->GetBrainComponent();
	if (!Brain)
	{
		UE_LOG(LogRevvyBT, Warning,
			TEXT("StopBehaviorTree: no UBrainComponent on '%s'."),
			*AIControllerActorNameOrPath);
		return false;
	}

	Brain->StopLogic(TEXT("Revvy stop"));
	return true;
}
