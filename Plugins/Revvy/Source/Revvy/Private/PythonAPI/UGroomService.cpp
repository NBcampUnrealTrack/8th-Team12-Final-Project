// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UGroomService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyGroom, Log, All);

// Editor / world / actor infrastructure
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"

// Asset infrastructure
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Factories/Factory.h"
#include "IAssetTools.h"
#include "ScopedTransaction.h"

// Shared Revvy helpers
#include "Core/URevvyCommonHelpers.h"

// HairStrands (default-enabled plugin in UE 5.8)
#include "GroomAsset.h"           // UGroomAsset
#include "GroomBindingAsset.h"    // UGroomBindingAsset
#include "GroomComponent.h"       // UGroomComponent

// JSON serialization
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// ============================================================================
// Helpers (file-local)
// ============================================================================

namespace
{
	FString GroomSerializeJsonArray(const TArray<TSharedPtr<FJsonValue>>& Arr)
	{
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer
			= TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Arr, Writer);
		return Out;
	}

	/** Split "/Game/Foo/Bar" into ("/Game/Foo", "Bar"). Returns false on empty. */
	bool GroomSplitPackagePath(const FString& AssetPath, FString& OutPackageDir, FString& OutAssetName)
	{
		if (AssetPath.IsEmpty())
		{
			return false;
		}
		OutPackageDir = FPackageName::GetLongPackagePath(AssetPath);
		OutAssetName  = FPackageName::GetShortName(AssetPath);
		return !OutPackageDir.IsEmpty() && !OutAssetName.IsEmpty();
	}

	UGroomAsset* LoadGroomChecked(const FString& Path, const TCHAR* CallSite)
	{
		if (Path.IsEmpty())
		{
			UE_LOG(LogRevvyGroom, Warning, TEXT("%s: GroomAssetPath is empty"), CallSite);
			return nullptr;
		}
		UObject* Loaded = UEditorAssetLibrary::LoadAsset(Path);
		if (!Loaded)
		{
			UE_LOG(LogRevvyGroom, Warning,
				TEXT("%s: LoadAsset returned null for '%s'"), CallSite, *Path);
			return nullptr;
		}
		UGroomAsset* Groom = Cast<UGroomAsset>(Loaded);
		if (!Groom)
		{
			UE_LOG(LogRevvyGroom, Warning,
				TEXT("%s: asset '%s' (%s) is not a UGroomAsset"),
				CallSite, *Path, *Loaded->GetClass()->GetName());
		}
		return Groom;
	}

	UGroomBindingAsset* LoadGroomBindingChecked(const FString& Path, const TCHAR* CallSite)
	{
		if (Path.IsEmpty())
		{
			return nullptr; // empty is a valid "no binding" state — do not warn
		}
		UObject* Loaded = UEditorAssetLibrary::LoadAsset(Path);
		if (!Loaded)
		{
			UE_LOG(LogRevvyGroom, Warning,
				TEXT("%s: LoadAsset returned null for '%s'"), CallSite, *Path);
			return nullptr;
		}
		UGroomBindingAsset* Binding = Cast<UGroomBindingAsset>(Loaded);
		if (!Binding)
		{
			UE_LOG(LogRevvyGroom, Warning,
				TEXT("%s: asset '%s' (%s) is not a UGroomBindingAsset"),
				CallSite, *Path, *Loaded->GetClass()->GetName());
		}
		return Binding;
	}

	USkeletalMesh* GroomLoadSkelMeshChecked(const FString& Path, const TCHAR* CallSite)
	{
		if (Path.IsEmpty())
		{
			UE_LOG(LogRevvyGroom, Warning, TEXT("%s: SkelMeshAssetPath is empty"), CallSite);
			return nullptr;
		}
		USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(UEditorAssetLibrary::LoadAsset(Path));
		if (!SkelMesh)
		{
			UE_LOG(LogRevvyGroom, Warning,
				TEXT("%s: failed to load USkeletalMesh at '%s'"), CallSite, *Path);
		}
		return SkelMesh;
	}
}

// ============================================================================
// Private helpers (class scope)
// ============================================================================

bool UGroomService::EnsureHairStrandsAvailable(const TCHAR* CallSite)
{
	// HairStrands is a default-enabled UE plugin. Guard defensively so this
	// service degrades gracefully on trimmed engine builds.
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("HairStrandsCore")))
	{
		UE_LOG(LogRevvyGroom, Warning,
			TEXT("%s: HairStrandsCore module is not loaded — HairStrands plugin "
				 "must be enabled for this operation."), CallSite);
		return false;
	}
	return true;
}

// ============================================================================
// Create
// ============================================================================

FString UGroomService::CreateGroomBindingAsset(
	const FString& OutputAssetPath,
	const FString& GroomAssetPath,
	const FString& TargetSkelMeshPath)
{
	if (!EnsureHairStrandsAvailable(TEXT("CreateGroomBindingAsset")))
	{
		return FString();
	}

	FString PackageDir, AssetName;
	if (!GroomSplitPackagePath(OutputAssetPath, PackageDir, AssetName))
	{
		UE_LOG(LogRevvyGroom, Warning,
			TEXT("CreateGroomBindingAsset: invalid OutputAssetPath '%s'."),
			*OutputAssetPath);
		return FString();
	}

	if (UEditorAssetLibrary::DoesAssetExist(OutputAssetPath))
	{
		UE_LOG(LogRevvyGroom, Warning,
			TEXT("CreateGroomBindingAsset: asset already exists at '%s'."),
			*OutputAssetPath);
		return FString();
	}

	UGroomAsset* Groom = LoadGroomChecked(GroomAssetPath, TEXT("CreateGroomBindingAsset"));
	if (!Groom)
	{
		return FString();
	}

	USkeletalMesh* SkelMesh = GroomLoadSkelMeshChecked(TargetSkelMeshPath, TEXT("CreateGroomBindingAsset"));
	if (!SkelMesh)
	{
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("GroomService",
		"CreateGroomBindingAsset", "Create Groom Binding Asset"));

	// Prefer IAssetTools + UGroomBindingAssetFactory when available (editor
	// module). Fall back to CreatePackage + NewObject so this compiles even if
	// the editor factory class is not linked.
	UGroomBindingAsset* NewBinding = nullptr;

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();

	// Locate the factory class by name to avoid a hard editor-module dep here.
	UClass* FactoryClass = FindObject<UClass>(nullptr, TEXT("/Script/HairStrandsEditor.GroomBindingAssetFactory"));
	if (FactoryClass)
	{
		UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
		if (Factory)
		{
			UObject* Created = AssetTools.CreateAsset(AssetName, PackageDir, UGroomBindingAsset::StaticClass(), Factory);
			NewBinding = Cast<UGroomBindingAsset>(Created);
		}
	}

	if (!NewBinding)
	{
		// Fallback path: raw package + NewObject
		const FString FullPackageName = PackageDir / AssetName;
		UPackage* Package = CreatePackage(*FullPackageName);
		if (!Package)
		{
			UE_LOG(LogRevvyGroom, Warning,
				TEXT("CreateGroomBindingAsset: failed to create package '%s'."),
				*FullPackageName);
			return FString();
		}

		NewBinding = NewObject<UGroomBindingAsset>(
			Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (!NewBinding)
		{
			UE_LOG(LogRevvyGroom, Warning,
				TEXT("CreateGroomBindingAsset: failed to instantiate UGroomBindingAsset."));
			return FString();
		}

		FAssetRegistryModule::AssetCreated(NewBinding);
		Package->SetDirtyFlag(true);
	}

	NewBinding->Modify();
	NewBinding->SetGroom(Groom);
	NewBinding->SetTargetSkeletalMesh(SkelMesh);

#if WITH_EDITOR
	// Editor-only build hook: newer HairStrandsEditor exposes Build() on the
	// binding asset. Resolve dynamically via UFunction so this compiles even if
	// the specific helper is not present in older engine variants.
	if (UFunction* BuildFn = NewBinding->FindFunction(TEXT("Build")))
	{
		NewBinding->ProcessEvent(BuildFn, nullptr);
	}
#endif

	NewBinding->PostEditChange();
	NewBinding->MarkPackageDirty();

	UEditorAssetLibrary::SaveLoadedAsset(NewBinding, /*bOnlyIfDirty=*/false);

	UE_LOG(LogRevvyGroom, Log,
		TEXT("CreateGroomBindingAsset: '%s' bound Groom '%s' -> SkelMesh '%s'"),
		*NewBinding->GetPathName(), *GroomAssetPath, *TargetSkelMeshPath);

	return NewBinding->GetPathName();
}

// ============================================================================
// Attach
// ============================================================================

FString UGroomService::AttachGroomToActor(
	const FString& ActorNameOrPath,
	const FString& GroomAssetPath,
	const FString& BindingAssetPath,
	const FString& SocketName)
{
	if (!EnsureHairStrandsAvailable(TEXT("AttachGroomToActor")))
	{
		return FString();
	}

	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Actor)
	{
		UE_LOG(LogRevvyGroom, Warning,
			TEXT("AttachGroomToActor: actor '%s' not found."), *ActorNameOrPath);
		return FString();
	}

	UGroomAsset* Groom = LoadGroomChecked(GroomAssetPath, TEXT("AttachGroomToActor"));
	if (!Groom)
	{
		return FString();
	}

	UGroomBindingAsset* Binding = LoadGroomBindingChecked(BindingAssetPath, TEXT("AttachGroomToActor"));

	USceneComponent* Root = Actor->GetRootComponent();
	if (!Root)
	{
		UE_LOG(LogRevvyGroom, Warning,
			TEXT("AttachGroomToActor: actor '%s' has no root scene component."),
			*ActorNameOrPath);
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("GroomService",
		"AttachGroomToActor", "Attach Groom To Actor"));

	Actor->Modify();

	UGroomComponent* Comp = NewObject<UGroomComponent>(Actor, NAME_None, RF_Transactional);
	if (!Comp)
	{
		UE_LOG(LogRevvyGroom, Warning,
			TEXT("AttachGroomToActor: failed to instantiate UGroomComponent."));
		return FString();
	}

	Comp->SetGroomAsset(Groom);
	if (Binding)
	{
		Comp->SetBindingAsset(Binding);
	}

	Comp->SetupAttachment(Root, SocketName.IsEmpty() ? NAME_None : FName(*SocketName));
	Comp->RegisterComponent();

	if (!SocketName.IsEmpty())
	{
		Comp->AttachToComponent(
			Root,
			FAttachmentTransformRules::KeepRelativeTransform,
			FName(*SocketName));
	}

	Actor->AddInstanceComponent(Comp);
	Comp->PostEditChange();

	Actor->MarkPackageDirty();

	UE_LOG(LogRevvyGroom, Log,
		TEXT("AttachGroomToActor: attached UGroomComponent '%s' to actor '%s' (socket '%s')."),
		*Comp->GetName(), *ActorNameOrPath, *SocketName);

	return Comp->GetName();
}

// ============================================================================
// Config
// ============================================================================

bool UGroomService::SetGroomEnabled(
	const FString& ActorNameOrPath,
	int32 ComponentIndex,
	bool bEnabled)
{
	if (!EnsureHairStrandsAvailable(TEXT("SetGroomEnabled")))
	{
		return false;
	}

	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Actor)
	{
		UE_LOG(LogRevvyGroom, Warning,
			TEXT("SetGroomEnabled: actor '%s' not found."), *ActorNameOrPath);
		return false;
	}

	TArray<UGroomComponent*> Comps;
	Actor->GetComponents<UGroomComponent>(Comps);
	if (!Comps.IsValidIndex(ComponentIndex))
	{
		UE_LOG(LogRevvyGroom, Warning,
			TEXT("SetGroomEnabled: index %d out of range (actor '%s' has %d groom component(s))."),
			ComponentIndex, *ActorNameOrPath, Comps.Num());
		return false;
	}

	UGroomComponent* Comp = Comps[ComponentIndex];
	if (!Comp)
	{
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("GroomService",
		"SetGroomEnabled", "Set Groom Enabled"));

	Comp->Modify();
	Comp->SetVisibility(bEnabled, /*bPropagateToChildren=*/true);
	Comp->SetActive(bEnabled);
	Comp->PostEditChange();

	Actor->MarkPackageDirty();

	UE_LOG(LogRevvyGroom, Log,
		TEXT("SetGroomEnabled: '%s' groom[%d] -> %s"),
		*ActorNameOrPath, ComponentIndex, bEnabled ? TEXT("enabled") : TEXT("disabled"));

	return true;
}

// ============================================================================
// Query
// ============================================================================

FString UGroomService::ListGroomComponents(const FString& ActorNameOrPath)
{
	TArray<TSharedPtr<FJsonValue>> Items;

	if (!EnsureHairStrandsAvailable(TEXT("ListGroomComponents")))
	{
		return GroomSerializeJsonArray(Items);
	}

	AActor* Actor = URevvyCommonHelpers::FindActorByNameOrPath(nullptr, ActorNameOrPath);
	if (!Actor)
	{
		UE_LOG(LogRevvyGroom, Warning,
			TEXT("ListGroomComponents: actor '%s' not found."), *ActorNameOrPath);
		return GroomSerializeJsonArray(Items);
	}

	TArray<UGroomComponent*> Comps;
	Actor->GetComponents<UGroomComponent>(Comps);

	for (int32 Index = 0; Index < Comps.Num(); ++Index)
	{
		UGroomComponent* Comp = Comps[Index];
		if (!Comp) continue;

		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetNumberField(TEXT("index"),        Index);
		Item->SetStringField(TEXT("component"),    Comp->GetName());
		Item->SetStringField(TEXT("groom_path"),
			Comp->GroomAsset ? Comp->GroomAsset->GetPathName() : FString());
		Item->SetStringField(TEXT("binding_path"),
			Comp->BindingAsset ? Comp->BindingAsset->GetPathName() : FString());
		Item->SetBoolField(TEXT("is_visible"),     Comp->IsVisible());
		Item->SetBoolField(TEXT("is_active"),      Comp->IsActive());
		Item->SetStringField(TEXT("socket"),       Comp->GetAttachSocketName().ToString());

		Items.Add(MakeShared<FJsonValueObject>(Item));
	}

	UE_LOG(LogRevvyGroom, Log,
		TEXT("ListGroomComponents: '%s' -> %d groom component(s)."),
		*ActorNameOrPath, Items.Num());

	return GroomSerializeJsonArray(Items);
}
