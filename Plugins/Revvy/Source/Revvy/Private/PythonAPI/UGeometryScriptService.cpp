// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UGeometryScriptService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyGeomScript, Log, All);

#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"

// GeometryScript dynamic mesh + libraries
#include "UDynamicMesh.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "GeometryScript/MeshBasicEditFunctions.h"
#include "GeometryScript/GeometryScriptTypes.h"

#if WITH_EDITOR
#include "GeometryScript/MeshAssetFunctions.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#endif

// =================================================================
// Static state
// =================================================================

TMap<FString, TObjectPtr<UDynamicMesh>> UGeometryScriptService::MeshRegistry;

// =================================================================
// Helpers
// =================================================================

void UGeometryScriptService::WarnIfNotGameThread(const TCHAR* Op)
{
	if (!IsInGameThread())
	{
		UE_LOG(LogRevvyGeomScript, Warning,
			TEXT("UGeometryScriptService::%s called off GameThread — registry access is not thread-safe."),
			Op);
	}
}

UDynamicMesh* UGeometryScriptService::FindMesh(const FString& Handle)
{
	if (Handle.IsEmpty())
	{
		UE_LOG(LogRevvyGeomScript, Warning, TEXT("FindMesh: empty handle rejected."));
		return nullptr;
	}

	if (TObjectPtr<UDynamicMesh>* Found = MeshRegistry.Find(Handle))
	{
		UDynamicMesh* Mesh = Found->Get();
		if (!Mesh)
		{
			UE_LOG(LogRevvyGeomScript, Warning,
				TEXT("FindMesh: handle '%s' registered but underlying mesh is null."), *Handle);
		}
		return Mesh;
	}

	UE_LOG(LogRevvyGeomScript, Warning,
		TEXT("FindMesh: no mesh registered for handle '%s'."), *Handle);
	return nullptr;
}

// =================================================================
// Lifecycle
// =================================================================

FString UGeometryScriptService::CreateDynamicMesh(const FString& Handle)
{
	WarnIfNotGameThread(TEXT("CreateDynamicMesh"));

	if (Handle.IsEmpty())
	{
		UE_LOG(LogRevvyGeomScript, Warning, TEXT("CreateDynamicMesh: handle must be non-empty."));
		return FString();
	}

	if (MeshRegistry.Contains(Handle))
	{
		UE_LOG(LogRevvyGeomScript, Warning,
			TEXT("CreateDynamicMesh: handle '%s' already registered."), *Handle);
		return FString();
	}

	UDynamicMesh* Mesh = NewObject<UDynamicMesh>(GetTransientPackage());
	if (!Mesh)
	{
		UE_LOG(LogRevvyGeomScript, Error,
			TEXT("CreateDynamicMesh: NewObject<UDynamicMesh> failed for handle '%s'."), *Handle);
		return FString();
	}

	Mesh->AddToRoot();
	MeshRegistry.Add(Handle, Mesh);

	UE_LOG(LogRevvyGeomScript, Log, TEXT("CreateDynamicMesh: registered handle '%s'."), *Handle);
	return Handle;
}

bool UGeometryScriptService::DisposeDynamicMesh(const FString& Handle)
{
	WarnIfNotGameThread(TEXT("DisposeDynamicMesh"));

	TObjectPtr<UDynamicMesh> Existing;
	if (!MeshRegistry.RemoveAndCopyValue(Handle, Existing))
	{
		UE_LOG(LogRevvyGeomScript, Warning,
			TEXT("DisposeDynamicMesh: handle '%s' not found."), *Handle);
		return false;
	}

	if (UDynamicMesh* Mesh = Existing.Get())
	{
		if (Mesh->IsRooted())
		{
			Mesh->RemoveFromRoot();
		}
	}

	Existing = nullptr;
	UE_LOG(LogRevvyGeomScript, Log, TEXT("DisposeDynamicMesh: removed handle '%s'."), *Handle);
	return true;
}

// =================================================================
// Primitives
// =================================================================

bool UGeometryScriptService::AppendBox(
	const FString& Handle,
	FTransform Transform,
	FVector Dimensions,
	int32 StepsX,
	int32 StepsY,
	int32 StepsZ)
{
	WarnIfNotGameThread(TEXT("AppendBox"));

	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh)
	{
		return false;
	}

	FGeometryScriptPrimitiveOptions Options;

	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
		Mesh,
		Options,
		Transform,
		static_cast<float>(Dimensions.X),
		static_cast<float>(Dimensions.Y),
		static_cast<float>(Dimensions.Z),
		StepsX,
		StepsY,
		StepsZ,
		EGeometryScriptPrimitiveOriginMode::Base,
		nullptr);

	return true;
}

bool UGeometryScriptService::AppendSphere(
	const FString& Handle,
	FTransform Transform,
	float Radius,
	int32 StepsPhi,
	int32 StepsTheta)
{
	WarnIfNotGameThread(TEXT("AppendSphere"));

	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh)
	{
		return false;
	}

	FGeometryScriptPrimitiveOptions Options;

	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(
		Mesh,
		Options,
		Transform,
		Radius,
		StepsPhi,
		StepsTheta,
		EGeometryScriptPrimitiveOriginMode::Center,
		nullptr);

	return true;
}

bool UGeometryScriptService::AppendCylinder(
	const FString& Handle,
	FTransform Transform,
	float Radius,
	float Height,
	int32 RadialSteps,
	int32 HeightSteps)
{
	WarnIfNotGameThread(TEXT("AppendCylinder"));

	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh)
	{
		return false;
	}

	FGeometryScriptPrimitiveOptions Options;

	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(
		Mesh,
		Options,
		Transform,
		Radius,
		Height,
		RadialSteps,
		HeightSteps,
		/*bCapped*/ true,
		EGeometryScriptPrimitiveOriginMode::Base,
		nullptr);

	return true;
}

bool UGeometryScriptService::AppendTorus(
	const FString& Handle,
	FTransform Transform,
	float MajorRadius,
	float MinorRadius,
	int32 MajorSteps,
	int32 MinorSteps,
	float RevolveDegrees,
	float DegreeOffset,
	bool bFillEndCaps)
{
	WarnIfNotGameThread(TEXT("AppendTorus"));

	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh)
	{
		return false;
	}

	if (!FMath::IsFinite(RevolveDegrees) || RevolveDegrees <= 0.0f || RevolveDegrees > 360.0f)
	{
		UE_LOG(LogRevvyGeomScript, Warning,
			TEXT("AppendTorus: RevolveDegrees must be finite and in the range (0, 360]; received %g for handle '%s'."),
			RevolveDegrees,
			*Handle);
		return false;
	}

	if (!FMath::IsFinite(DegreeOffset))
	{
		UE_LOG(LogRevvyGeomScript, Warning,
			TEXT("AppendTorus: DegreeOffset must be finite; received %g for handle '%s'."),
			DegreeOffset,
			*Handle);
		return false;
	}

	FGeometryScriptPrimitiveOptions Options;
	FGeometryScriptRevolveOptions RevolveOptions;
	RevolveOptions.RevolveDegrees = RevolveDegrees;
	RevolveOptions.DegreeOffset = DegreeOffset;
	RevolveOptions.bFillPartialRevolveEndcaps = bFillEndCaps;

	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendTorus(
		Mesh,
		Options,
		Transform,
		RevolveOptions,
		MajorRadius,
		MinorRadius,
		MajorSteps,
		MinorSteps,
		EGeometryScriptPrimitiveOriginMode::Center,
		nullptr);

	UE_LOG(LogRevvyGeomScript, Verbose,
		TEXT("AppendTorus: appended %.3f-degree torus at offset %.3f for handle '%s' (end caps: %s)."),
		RevolveDegrees,
		DegreeOffset,
		*Handle,
		bFillEndCaps ? TEXT("true") : TEXT("false"));

	return true;
}

// =================================================================
// Mesh Import
// =================================================================

bool UGeometryScriptService::AppendMeshFromStaticMesh(
	const FString& Handle,
	const FString& StaticMeshAssetPath,
	FTransform Transform,
	int32 LODIndex)
{
	WarnIfNotGameThread(TEXT("AppendMeshFromStaticMesh"));

	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh)
	{
		return false;
	}

#if WITH_EDITOR
	UStaticMesh* SourceStaticMesh = LoadObject<UStaticMesh>(nullptr, *StaticMeshAssetPath);
	if (!SourceStaticMesh)
	{
		UE_LOG(LogRevvyGeomScript, Warning,
			TEXT("AppendMeshFromStaticMesh: failed to load '%s'."), *StaticMeshAssetPath);
		return false;
	}

	// Copy into a scratch UDynamicMesh, then append it to the target with placement transform.
	UDynamicMesh* TempMesh = NewObject<UDynamicMesh>(GetTransientPackage());
	if (!TempMesh)
	{
		return false;
	}

	FGeometryScriptCopyMeshFromAssetOptions FromAssetOptions;
	FGeometryScriptMeshReadLOD RequestedLOD;
	RequestedLOD.LODType = EGeometryScriptLODType::SourceModel;
	RequestedLOD.LODIndex = LODIndex;

	EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
	UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMesh(
		SourceStaticMesh,
		TempMesh,
		FromAssetOptions,
		RequestedLOD,
		Outcome,
		nullptr);

	if (Outcome != EGeometryScriptOutcomePins::Success)
	{
		UE_LOG(LogRevvyGeomScript, Warning,
			TEXT("AppendMeshFromStaticMesh: CopyMeshFromStaticMesh failed for '%s'."), *StaticMeshAssetPath);
		return false;
	}

	FGeometryScriptAppendMeshOptions AppendOptions;
	UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMeshTransformed(
		Mesh,
		TempMesh,
		{ Transform },
		FTransform::Identity,
		/*bConstantTransformIsRelative*/ false,
		/*bDeferChangeNotifications*/ false,
		AppendOptions,
		nullptr);

	return true;
#else
	UE_LOG(LogRevvyGeomScript, Warning,
		TEXT("AppendMeshFromStaticMesh: editor-only operation not available in this build."));
	return false;
#endif
}

// =================================================================
// Boolean
// =================================================================

bool UGeometryScriptService::BooleanUnion(const FString& TargetHandle, const FString& OtherHandle)
{
	WarnIfNotGameThread(TEXT("BooleanUnion"));

	UDynamicMesh* Target = FindMesh(TargetHandle);
	UDynamicMesh* Other = FindMesh(OtherHandle);
	if (!Target || !Other)
	{
		return false;
	}

	FGeometryScriptMeshBooleanOptions Options;
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
		Target,
		FTransform::Identity,
		Other,
		FTransform::Identity,
		EGeometryScriptBooleanOperation::Union,
		Options,
		nullptr);
	return true;
}

bool UGeometryScriptService::BooleanSubtract(const FString& TargetHandle, const FString& OtherHandle)
{
	WarnIfNotGameThread(TEXT("BooleanSubtract"));

	UDynamicMesh* Target = FindMesh(TargetHandle);
	UDynamicMesh* Other = FindMesh(OtherHandle);
	if (!Target || !Other)
	{
		return false;
	}

	FGeometryScriptMeshBooleanOptions Options;
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
		Target,
		FTransform::Identity,
		Other,
		FTransform::Identity,
		EGeometryScriptBooleanOperation::Subtract,
		Options,
		nullptr);
	return true;
}

bool UGeometryScriptService::BooleanIntersect(const FString& TargetHandle, const FString& OtherHandle)
{
	WarnIfNotGameThread(TEXT("BooleanIntersect"));

	UDynamicMesh* Target = FindMesh(TargetHandle);
	UDynamicMesh* Other = FindMesh(OtherHandle);
	if (!Target || !Other)
	{
		return false;
	}

	FGeometryScriptMeshBooleanOptions Options;
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
		Target,
		FTransform::Identity,
		Other,
		FTransform::Identity,
		EGeometryScriptBooleanOperation::Intersection,
		Options,
		nullptr);
	return true;
}

// =================================================================
// Export
// =================================================================

FString UGeometryScriptService::ConvertDynamicMeshToStaticMesh(const FString& Handle, const FString& OutputAssetPath)
{
	WarnIfNotGameThread(TEXT("ConvertDynamicMeshToStaticMesh"));

	UDynamicMesh* Mesh = FindMesh(Handle);
	if (!Mesh)
	{
		return FString();
	}

#if WITH_EDITOR
	if (OutputAssetPath.IsEmpty() || !FPackageName::IsValidLongPackageName(OutputAssetPath))
	{
		UE_LOG(LogRevvyGeomScript, Warning,
			TEXT("ConvertDynamicMeshToStaticMesh: invalid package path '%s'."), *OutputAssetPath);
		return FString();
	}

	const FString AssetName = FPackageName::GetShortName(OutputAssetPath);

	FScopedTransaction Transaction(NSLOCTEXT("Revvy", "GeomScriptConvertToStaticMesh",
		"Convert DynamicMesh to StaticMesh"));

	UStaticMesh* TargetStaticMesh = LoadObject<UStaticMesh>(nullptr, *OutputAssetPath);
	if (!TargetStaticMesh)
	{
		UPackage* Package = CreatePackage(*OutputAssetPath);
		if (!Package)
		{
			UE_LOG(LogRevvyGeomScript, Error,
				TEXT("ConvertDynamicMeshToStaticMesh: failed to create package '%s'."), *OutputAssetPath);
			return FString();
		}
		Package->FullyLoad();

		TargetStaticMesh = NewObject<UStaticMesh>(
			Package,
			*AssetName,
			RF_Public | RF_Standalone | RF_Transactional);
		if (!TargetStaticMesh)
		{
			UE_LOG(LogRevvyGeomScript, Error,
				TEXT("ConvertDynamicMeshToStaticMesh: NewObject<UStaticMesh> failed for '%s'."),
				*OutputAssetPath);
			return FString();
		}

		FAssetRegistryModule::AssetCreated(TargetStaticMesh);
	}

	TargetStaticMesh->Modify();

	FGeometryScriptCopyMeshToAssetOptions ToAssetOptions;
	ToAssetOptions.bReplaceMaterials = false;
	ToAssetOptions.bEnableRecomputeNormals = true;
	ToAssetOptions.bEnableRecomputeTangents = true;
	ToAssetOptions.bEnableRemoveDegenerates = true;

	FGeometryScriptMeshWriteLOD TargetLOD;
	TargetLOD.LODIndex = 0;

	EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
	UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshToStaticMesh(
		Mesh,
		TargetStaticMesh,
		ToAssetOptions,
		TargetLOD,
		Outcome,
		/*bUseSectionMaterials*/ true,
		nullptr);

	if (Outcome != EGeometryScriptOutcomePins::Success)
	{
		UE_LOG(LogRevvyGeomScript, Warning,
			TEXT("ConvertDynamicMeshToStaticMesh: CopyMeshToStaticMesh failed for '%s'."), *OutputAssetPath);
		return FString();
	}

	TargetStaticMesh->PostEditChange();
	TargetStaticMesh->MarkPackageDirty();

	UE_LOG(LogRevvyGeomScript, Log,
		TEXT("ConvertDynamicMeshToStaticMesh: wrote asset '%s'."), *OutputAssetPath);
	return OutputAssetPath;
#else
	UE_LOG(LogRevvyGeomScript, Warning,
		TEXT("ConvertDynamicMeshToStaticMesh: editor-only operation not available in this build."));
	return FString();
#endif
}
