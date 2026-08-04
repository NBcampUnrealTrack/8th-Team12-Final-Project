// Copyright Recursoft LLC. All Rights Reserved.

#include "Properties/SMEditorPropertyUtils.h"

#include "Configuration/SMProjectEditorSettings.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/Nodes/SMGraphNode_StateNode.h"
#include "Graph/Nodes/SMGraphNode_StateNodeBase.h"
#include "NodeStack/NodeStackContainer.h"
#include "Utilities/SMBlueprintEditorUtils.h"
#include "Utilities/SMNodeInstanceUtils.h"

#include "SMTypes.h"
#include "SMUtils.h"

#include "ISinglePropertyView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"

TSharedPtr<ISinglePropertyView> LD::Editor::PropertyUtils::CreatePropertyViewForProperty(UObject* InObjectOwner,
                                                                                         const FName& InPropertyName)
{
	check(InObjectOwner);

	const FSinglePropertyParams InitParams;
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	TSharedPtr<ISinglePropertyView> PropertyView = PropertyEditorModule.CreateSingleProperty(
		InObjectOwner,
		InPropertyName,
		InitParams);

	check(PropertyView.IsValid());
	return PropertyView;
}

/** Sets a single property value with no array handling. */
void SetSinglePropertyValueImpl(const FProperty* InProperty, const FString& InValue, uint8* Container,
                                UObject* InObject)
{
	if (!FBlueprintEditorUtils::PropertyValueFromString_Direct(InProperty, InValue, Container, InObject))
	{
		// Fallback to generic import, don't log as this is common when adding variables and not changing their defaults.
		InProperty->ImportText_Direct(*InValue, Container, InObject, PPF_SerializedAsImportText);
	}
}

void LD::Editor::PropertyUtils::SetPropertyValue(FProperty* InProperty, const FString& InValue, UObject* InObject,
                                         int32 InArrayIndex)
{
	if (InObject)
	{
		InObject->Modify();
	}

	// The final property that will have its value set.
	FProperty* PropertyToSet = InProperty;

	// The owner of the container which could be the object or the container if an array.
	uint8* ContainerOwner = reinterpret_cast<uint8*>(InObject);

	// The immediate container of the property.
	uint8* Container = nullptr;

	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(InProperty))
	{
		FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<uint8>(InObject));
		if (Helper.IsValidIndex(InArrayIndex))
		{
			PropertyToSet = ArrayProperty->Inner;
			Container = Helper.GetRawPtr(InArrayIndex);
			ContainerOwner = Container;
		}
	}
	else
	{
		Container = InProperty->ContainerPtrToValuePtr<uint8>(ContainerOwner, InArrayIndex);
	}

	// Check for extended graph property.
	if (FProperty* ExtendGraphResultProperty = GetExtendedGraphPropertyResult(PropertyToSet, ContainerOwner))
	{
		Container = ExtendGraphResultProperty->ContainerPtrToValuePtr<uint8>(Container);
		PropertyToSet = ExtendGraphResultProperty;
	}

	if (ensure(Container))
	{
		SetSinglePropertyValueImpl(PropertyToSet, InValue, Container, InObject);
	}
}

FString LD::Editor::PropertyUtils::GetPropertyValue(FProperty* InProperty, UObject* InObject, int32 InArrayIndex)
{
	// The final property that will have its value returned.
	FProperty* PropertyToGet = InProperty;

	// The owner of the container which could be the object or the container if an array.
	uint8* ContainerOwner = reinterpret_cast<uint8*>(InObject);

	// The immediate container of the property.
	uint8* Container = nullptr;

	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(InProperty))
	{
		FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<uint8>(InObject));
		if (!(ensureMsgf(Helper.IsValidIndex(InArrayIndex),
		                 TEXT("Invalid array index given to GetPropertyValue for array %s."), *InProperty->GetName())))
		{
			return FString();
		}

		PropertyToGet = ArrayProperty->Inner;
		Container = Helper.GetRawPtr(InArrayIndex);
		ContainerOwner = Container;
	}
	else
	{
		Container = InProperty->ContainerPtrToValuePtr<uint8>(ContainerOwner, InArrayIndex);
	}

	// Check for extended graph property.
	if (FProperty* ExtendGraphResultProperty = GetExtendedGraphPropertyResult(PropertyToGet, ContainerOwner))
	{
		Container = ExtendGraphResultProperty->ContainerPtrToValuePtr<uint8>(Container);
		PropertyToGet = ExtendGraphResultProperty;
	}

	FString Result;
	if (ensure(Container))
	{
		FBlueprintEditorUtils::PropertyValueToString_Direct(PropertyToGet, Container, Result, InObject);
	}

	return MoveTemp(Result);
}

FProperty* LD::Editor::PropertyUtils::GetExtendedGraphPropertyResult(FProperty* InProperty, uint8* InContainer)
{
	if (const FStructProperty* StructProperty = GetGraphPropertyFromProperty(InProperty))
	{
		check(InContainer);

		// Access the graph property instance so can find the virtual result property name.
		const FSMGraphProperty_Base* GraphProperty = InProperty->ContainerPtrToValuePtr<FSMGraphProperty_Base>(InContainer);
		check(GraphProperty);

		const FName ResultPropertyName = GraphProperty->GetResultPropertyName();
		if (!ResultPropertyName.IsNone())
		{
			// A result property indicates this is a custom graph property that has a sub property managing the value.
			FProperty* ResultProperty = StructProperty->Struct->FindPropertyByName(ResultPropertyName);
			check(ResultProperty);

			return ResultProperty;
		}
	}

	return nullptr;
}

bool LD::Editor::PropertyUtils::IsObjectPropertyInstanced(const FObjectProperty* ObjectProperty)
{
	return ObjectProperty && ObjectProperty->HasAnyPropertyFlags(CPF_InstancedReference | CPF_ExportObject);
}

UObject* LD::Editor::PropertyUtils::FPropertyRetrieval::GetObjectValue() const
{
	if (ObjectProperty && ObjectContainer)
	{
		return ObjectProperty->GetObjectPropertyValue(ObjectContainer);
	}
	return nullptr;
}

void LD::Editor::PropertyUtils::FPropertyRetrieval::SetObjectValue(UObject* NewValue) const
{
	if (ObjectProperty && ObjectContainer)
	{
		return ObjectProperty->SetObjectPropertyValue(const_cast<void*>(ObjectContainer), NewValue);
	}
}

namespace LD::Editor::PropertyUtils
{
	void GetAllObjectPropertiesImpl(const void* InObject, const UStruct* InPropertySource,
	                                TArray<FPropertyRetrieval>& OutProperties,
	                                TSet<const void*>* ObjectsChecked, const FPropertyRetrievalArgs& InArgs)
	{
		if (!InObject || ObjectsChecked->Contains(InObject))
		{
			return;
		}

		ObjectsChecked->Add(InObject);

		auto ProcessProperty = [&](FProperty* Property, const void* Object)
		{
			if (!Object)
			{
				return;
			}

			ObjectsChecked->Add(Object);

			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				const void* StructAddressInObject = StructProperty->ContainerPtrToValuePtr<const void>(Object);
				GetAllObjectPropertiesImpl(StructAddressInObject, StructProperty->Struct, OutProperties,
				                           ObjectsChecked, InArgs);
			}
			else if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
			{
				// Check for any filters.
				if (InArgs.IncludePropertyFlags != CPF_None && !Property->HasAllPropertyFlags(InArgs.IncludePropertyFlags))
				{
					return;
				}
				if (InArgs.ExcludePropertyFlags != CPF_None && Property->HasAnyPropertyFlags(InArgs.ExcludePropertyFlags))
				{
					return;
				}

				const void* ObjectContainer = ObjectProperty->ContainerPtrToValuePtr<const void>(Object);
				const FPropertyRetrieval PropertyRetrieval{ObjectProperty, ObjectContainer};
				OutProperties.Add(PropertyRetrieval);

				if (IsObjectPropertyInstanced(ObjectProperty))
				{
					// Only check property instances stored within this object.
					if (const UObject* ObjectValue = PropertyRetrieval.GetObjectValue())
					{
						GetAllObjectPropertiesImpl(ObjectValue, ObjectValue->GetClass(), OutProperties,
						                           ObjectsChecked, InArgs);
					}
				}
			}
		};

		for (TFieldIterator<FProperty> PropertyIterator(InPropertySource); PropertyIterator; ++PropertyIterator)
		{
			FProperty* Property = *PropertyIterator;

			if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				FScriptArrayHelper ArrayHelper(ArrayProperty,
				                               ArrayProperty->ContainerPtrToValuePtr<void>(InObject));
				for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
				{
					const uint8* ArrayData = ArrayHelper.GetRawPtr(Index);
					ProcessProperty(ArrayProperty->Inner, ArrayData);
				}
			}
			/*
				else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
				{
					// TODO: MapProperty
				}
				else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
				{
					// TODO: SetProperty
				}*/
			else
			{
				ProcessProperty(Property, InObject);
			}
		}
	}
}

void LD::Editor::PropertyUtils::GetAllObjectProperties(const void* InObject, const UStruct* InPropertySource,
                                                       TArray<FPropertyRetrieval>& OutProperties,
                                                       const FPropertyRetrievalArgs& InArgs)
{
	TSet<const void*> ObjectsCheckedSource;
	GetAllObjectPropertiesImpl(InObject, InPropertySource, OutProperties, &ObjectsCheckedSource, InArgs);
}

void LD::Editor::PropertyUtils::ForEachInstancedSubObject(const UObject* InObject, const TFunction<void(UObject*)>& Function)
{
	// Verify there are sub objects first before finding properties. This saves a call to GetAllObjectProperties which
	// is much slower.
	bool bHasSubObjects = false;
	ForEachObjectWithOuter(InObject, [&](const UObject* Child)
	{
		if (IsValid(Child))
		{
			bHasSubObjects = true;
		}
	});

	if (bHasSubObjects)
	{
		TArray<FPropertyRetrieval> ObjectProperties;
		FPropertyRetrievalArgs Args;
		Args.IncludePropertyFlags = CPF_InstancedReference;
		Args.ExcludePropertyFlags = CPF_Transient;
		GetAllObjectProperties(InObject, InObject->GetClass(),
		                       ObjectProperties, MoveTemp(Args));
		for (FPropertyRetrieval& PropertyRetrieval : ObjectProperties)
		{
			if (!PropertyRetrieval.ObjectProperty ||
				!ensure(!PropertyRetrieval.ObjectProperty->HasAnyPropertyFlags(CPF_Transient)))
			{
				continue;
			}

			if (UObject* ObjectValue = PropertyRetrieval.GetObjectValue())
			{
				Function(ObjectValue);
			}
		}
	}
}

const FGuid& LD::Editor::PropertyUtils::SetGraphPropertyFromProperty(FSMGraphProperty_Base& GraphProperty, FProperty* Property,
                                                             const USMNodeInstance* NodeInstance, int32 Index,
                                                             bool bSetGuid, bool bUseTemplateInGuid,
                                                             bool bUseTempNativeGuid)
{
	check(NodeInstance)
	check(Property);

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	GraphProperty.bIsInArray = Property->IsA<FArrayProperty>() || Property->GetOwnerProperty()->IsA<FArrayProperty>();

	SetGraphPropertyFlagsFromNativeProperty(GraphProperty, Property);

	GraphProperty.VariableName = Property->GetFName();
	GraphProperty.MemberReference.SetFromField<FProperty>(Property, false);
	K2Schema->ConvertPropertyToPinType(Property, GraphProperty.VariableType);

	// TemplateGuid is used to calculate final guid.
	GraphProperty.SetTemplateGuid(NodeInstance->GetTemplateGuid());

	if (!bSetGuid)
	{
		return GraphProperty.GetGuid();
	}

	if (GraphProperty.MemberReference.GetMemberGuid().IsValid())
	{
		// Blueprint variable
		return GraphProperty.SetGuid(GraphProperty.MemberReference.GetMemberGuid(), Index, bUseTemplateInGuid);
	}

	// Search string Taken from FMemberReference::GetReferenceSearchString of engine CL 17816129.
	auto GetTempNativeSearchString = [&](UClass* InFieldOwner)
	{
		const FGuid MemberGuid = GraphProperty.MemberReference.GetMemberGuid();
		const FName MemberName = GraphProperty.MemberReference.GetMemberName();
		if (!GraphProperty.MemberReference.IsLocalScope())
		{
			if (InFieldOwner)
			{
				if (MemberGuid.IsValid())
				{
					return FString::Printf(
						TEXT(
							"Nodes(VariableReference(MemberName=+\"%s\" && MemberGuid(A=%i && B=%i && C=%i && D=%i)) || Name=\"(%s)\") || Pins(Binding=\"%s\") || Binding=\"%s\""),
						*MemberName.ToString(), MemberGuid.A, MemberGuid.B, MemberGuid.C, MemberGuid.D,
						*MemberName.ToString(), *MemberName.ToString(), *MemberName.ToString());
				}
				else
				{
					FString ExportMemberParentName = InFieldOwner->GetClass()->GetName();
					ExportMemberParentName.AppendChar('\'');
					ExportMemberParentName += InFieldOwner->GetAuthoritativeClass()->GetPathName();
					ExportMemberParentName.AppendChar('\'');

					return FString::Printf(
						TEXT(
							"Nodes(VariableReference(MemberName=+\"%s\" && (MemberParent=\"%s\" || bSelfContext=true) ) || Name=\"(%s)\") || Pins(Binding=\"%s\") || Binding=\"%s\""),
						*MemberName.ToString(), *ExportMemberParentName, *MemberName.ToString(), *MemberName.ToString(),
						*MemberName.ToString());
				}
			}
			else if (MemberGuid.IsValid())
			{
				return FString::Printf(
					TEXT(
						"Nodes(VariableReference(MemberName=+\"%s\" && MemberGuid(A=%i && B=%i && C=%i && D=%i)) || Name=\"(%s)\") || Pins(Binding=\"%s\") || Binding=\"%s\""),
					*MemberName.ToString(), MemberGuid.A, MemberGuid.B, MemberGuid.C, MemberGuid.D,
					*MemberName.ToString(), *MemberName.ToString(), *MemberName.ToString());
			}
			else
			{
				return FString::Printf(
					TEXT(
						"Nodes(VariableReference(MemberName=+\"%s\") || Name=\"(%s)\") || Pins(Binding=\"%s\") || Binding=\"%s\""),
					*MemberName.ToString(), *MemberName.ToString(), *MemberName.ToString(), *MemberName.ToString());
			}
		}
		else
		{
			return FString::Printf(
				TEXT("Nodes(VariableReference((MemberName=+\"%s\" && MemberScope=+\"%s\"))) || Binding=\"%s\""),
				*MemberName.ToString(), *GraphProperty.MemberReference.GetMemberScopeName(), *MemberName.ToString());
		}
	};

	// Previous search string, from 4.27 & below. This is what is used currently.
	auto GetNativeSearchString = [&](UClass* InFieldOwner)
	{
		const FGuid MemberGuid = GraphProperty.MemberReference.GetMemberGuid();
		const FName MemberName = GraphProperty.MemberReference.GetMemberName();
		if (!GraphProperty.MemberReference.IsLocalScope())
		{
			if (InFieldOwner)
			{
				if (MemberGuid.IsValid())
				{
					return FString::Printf(
						TEXT(
							"Nodes(VariableReference(MemberName=+\"%s\" && MemberGuid(A=%i && B=%i && C=%i && D=%i) ))"),
						*MemberName.ToString(), MemberGuid.A, MemberGuid.B, MemberGuid.C, MemberGuid.D);
				}
				else
				{
					FString ExportMemberParentName = InFieldOwner->GetClass()->GetName();
					ExportMemberParentName.AppendChar('\'');
					ExportMemberParentName += InFieldOwner->GetAuthoritativeClass()->GetPathName();
					ExportMemberParentName.AppendChar('\'');

					return FString::Printf(
						TEXT(
							"Nodes(VariableReference(MemberName=+\"%s\" && (MemberParent=\"%s\" || bSelfContext=true) ))"),
						*MemberName.ToString(), *ExportMemberParentName);
				}
			}
			else if (MemberGuid.IsValid())
			{
				return FString::Printf(
					TEXT("Nodes(VariableReference(MemberName=+\"%s\" && MemberGuid(A=%i && B=%i && C=%i && D=%i)))"),
					*MemberName.ToString(), MemberGuid.A, MemberGuid.B, MemberGuid.C, MemberGuid.D);
			}
			else
			{
				return FString::Printf(TEXT("Nodes(VariableReference(MemberName=+\"%s\"))"), *MemberName.ToString());
			}
		}
		else
		{
			return FString::Printf(
				TEXT("Nodes(VariableReference(MemberName=+\"%s\" && MemberScope=+\"%s\"))"), *MemberName.ToString(),
				*GraphProperty.MemberReference.GetMemberScopeName());
		}
	};

	//  Native variable.
	const FString SearchString = bUseTempNativeGuid
		                             ? GetTempNativeSearchString(Property->GetOwnerClass())
		                             : GetNativeSearchString(Property->GetOwnerClass());
	return GraphProperty.SetGuid(USMUtils::PathToGuid(SearchString), Index, bUseTemplateInGuid);
}

void LD::Editor::PropertyUtils::SetGraphPropertyFlagsFromNativeProperty(FSMGraphProperty_Base& InOutGraphProperty, const FProperty* InProperty)
{
	if (InProperty && InProperty->IsNative())
	{
		const FProperty* PropertyWithMetaData = InProperty;

		// Check if we belong to an array, and lookup to the array's meta specifier.
		if (!PropertyWithMetaData->IsA<FArrayProperty>() && PropertyWithMetaData->GetOwnerProperty()->IsA<FArrayProperty>())
		{
			PropertyWithMetaData = PropertyWithMetaData->GetOwnerProperty();
		}

		ESMGraphPropertyFlags FoundDirectionFlags = ESMGraphPropertyFlags::None;
		
		if (const FString* PropertyDirectionStringPtr = PropertyWithMetaData->FindMetaData(TEXT("LD_PropertyDirection")))
		{
			TArray<FString> SplitValues;
			PropertyDirectionStringPtr->ParseIntoArray(SplitValues, TEXT(","), true);

			for (FString& SplitValue : SplitValues)
			{
				const FString Value = SplitValue.TrimStartAndEnd();
				if (Value.Equals(TEXT("Both"), ESearchCase::IgnoreCase))
				{
					constexpr ESMGraphPropertyFlags Flags = ESMGraphPropertyFlags::Input | ESMGraphPropertyFlags::Output;
					if (ensureMsgf(InOutGraphProperty.CanAcceptPropertyFlag(Flags),
						TEXT("Graph property '%s' has unsupported metadata specifier '%s' present. This specific type of property may not support Output variables."),
						*InOutGraphProperty.VariableName.ToString(), TEXT("Both")))
					{
						EnumAddFlags(FoundDirectionFlags, Flags);
					}
					break;
				}
				else if (Value.Equals(TEXT("Output"), ESearchCase::IgnoreCase))
				{
					constexpr ESMGraphPropertyFlags Flags = ESMGraphPropertyFlags::Output;
					if (ensureMsgf(InOutGraphProperty.CanAcceptPropertyFlag(Flags),
						TEXT("Graph property '%s' has unsupported metadata specifier '%s' present. This specific type of property may not support Output variables."),
						*InOutGraphProperty.VariableName.ToString(), TEXT("Output")))
					{
						EnumAddFlags(FoundDirectionFlags, Flags);
					}
				}
				else if (Value.Equals(TEXT("Input"), ESearchCase::IgnoreCase))
				{
					// As of 2.10 there is no reason a property can't be input only... this is mainly for consistency
					// or if a licensee creates a custom graph property type that doesn't accept input connections.
					constexpr ESMGraphPropertyFlags Flags = ESMGraphPropertyFlags::Input;
					if (ensureMsgf(InOutGraphProperty.CanAcceptPropertyFlag(Flags),
						TEXT("Graph property '%s' has unsupported metadata specifier '%s' present."),
						*InOutGraphProperty.VariableName.ToString(), TEXT("Input")))
					{
						EnumAddFlags(FoundDirectionFlags, Flags);
					}
				}
			}
		}

		if (FoundDirectionFlags == ESMGraphPropertyFlags::None)
		{
			// Input is default
			FoundDirectionFlags = ESMGraphPropertyFlags::Input;
		}
		
		InOutGraphProperty.RemovePropertyFlags(ESMGraphPropertyFlags::Input | ESMGraphPropertyFlags::Output);
		InOutGraphProperty.AddPropertyFlags(FoundDirectionFlags);
	}
}

bool LD::Editor::PropertyUtils::IsPropertyExposedToGraphNode(const FProperty* Property)
{
	if (!Property)
	{
		return false;
	}

	if (const UScriptStruct* OwnerStruct = Cast<UScriptStruct>(Property->GetOwnerStruct()))
	{
		// Properties that belong to a struct are never displayed on the node.
		return false;
	}

	return !Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance) && Property->HasAllPropertyFlags(CPF_BlueprintVisible)
		&& !Property->HasMetaData(TEXT("HideOnNode"));
}

bool LD::Editor::PropertyUtils::IsPropertyHandleExposedContainer(const TSharedPtr<IPropertyHandle>& InHandle)
{
	// TODO Containers: If supporting maps or sets this needs to be updated.
	check(InHandle.IsValid());
	return InHandle->AsArray().IsValid() && IsPropertyExposedToGraphNode(InHandle->GetProperty());
}

FStructProperty* LD::Editor::PropertyUtils::GetGraphPropertyFromProperty(FProperty* Property)
{
	if (Property->HasMetaData(TEXT("HideOnNode")))
	{
		// Assume this node never wants to be displayed.
		return nullptr;
	}

	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		if (StructProperty->Struct->IsChildOf(FSMGraphProperty_Base_Runtime::StaticStruct()))
		{
			return StructProperty;
		}
	}

	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		if (FStructProperty* StructProperty = CastField<FStructProperty>(ArrayProperty->Inner))
		{
			if (StructProperty->Struct->IsChildOf(FSMGraphProperty_Base_Runtime::StaticStruct()))
			{
				return StructProperty;
			}
		}
	}

	return nullptr;
}

bool LD::Editor::PropertyUtils::IsPropertyGraphProperty(const FProperty* Property)
{
	return GetGraphPropertyFromProperty(const_cast<FProperty*>(Property)) != nullptr;
}

namespace LD::Editor::PropertyUtils
{
	// Helper to support both meta=(TagName) and meta=(TagName=true) syntaxes
	TOptional<bool> GetTagOrBoolMetadata(const FProperty* Property, const TCHAR* TagName)
	{
		TOptional<bool> bResult;

		if (Property->HasMetaData(TagName))
		{
			bResult = true;

			const FString ValueString = Property->GetMetaData(TagName);
			if (!ValueString.IsEmpty())
			{
				if (ValueString == TEXT("true"))
				{
					bResult = true;
				}
				else if (ValueString == TEXT("false"))
				{
					bResult = false;
				}
			}
		}

		return bResult;
	}
}

void LD::Editor::PropertyUtils::SetPropertyOverride(USMNodeInstance* InNodeCDO, const FPropertyOverrideArgs& InArgs)
{
	check(InNodeCDO);
	TSharedPtr<ISinglePropertyView> PropView;
	const TSharedPtr<IPropertyHandle> GraphPropertyHandle = FSMNodeInstanceUtils::FindOrAddExposedPropertyOverrideByName(InNodeCDO,
		InArgs.PropertyName,
		PropView);

	if(GraphPropertyHandle.IsValid())
	{
		void* Data;
		const FPropertyAccess::Result Result = GraphPropertyHandle->GetValueData(Data);
		check(Result == FPropertyAccess::Success);

		const FSMGraphProperty_Base* GraphProperty = static_cast<FSMGraphProperty_Base*>(Data);
		
		const TSharedPtr<IPropertyHandle> ReadOnlyHandle = GraphPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FSMGraphProperty, bReadOnly));
		const TSharedPtr<IPropertyHandle> HiddenHandle = GraphPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FSMGraphProperty, bHidden));
		const TSharedPtr<IPropertyHandle> PropertyFlagHandle = GraphPropertyHandle->GetChildHandle(FSMGraphProperty_Base_Runtime::GetPropertyFlagsPropertyName());

		if (InArgs.PropertyFlags.IsSet() && GraphProperty->CanAcceptPropertyFlag(InArgs.PropertyFlags.GetValue()))
		{
			const uint8 PropertyFlags = static_cast<uint8>(InArgs.PropertyFlags.GetValue());
			check(PropertyFlagHandle->SetValue(PropertyFlags) == FPropertyAccess::Result::Success);
		}

		if (InArgs.bReadOnly.IsSet())
		{
			check(ReadOnlyHandle->SetValue(InArgs.bReadOnly.GetValue()) == FPropertyAccess::Result::Success);
		}

		if (InArgs.bHidden.IsSet())
		{
			check(HiddenHandle->SetValue(InArgs.bHidden.GetValue()) == FPropertyAccess::Result::Success);
		}
	}
}

bool LD::Editor::PropertyUtils::ShouldGraphPropertyDisplayThumbnail(const USMGraphK2Node_PropertyNode_Base* InGraphPropertyNode)
{
	TOptional<bool> DisplayThumbnail;

	if (InGraphPropertyNode)
	{
		// Check blueprint flags first
		if (const FSMGraphProperty_Base* GraphProperty = InGraphPropertyNode->GetPropertyNodeConst())
		{
			if (const FSMNodeWidgetInfo* WidgetInfo = GraphProperty->GetWidgetInfo())
			{
				if (WidgetInfo->DisplayThumbnail == ESMOptionalBool::IsFalse)
				{
					DisplayThumbnail = false;
				}
				else if (WidgetInfo->DisplayThumbnail == ESMOptionalBool::IsTrue)
				{
					DisplayThumbnail = true;
				}
			}
		}

		// Check native metadata
		if (!DisplayThumbnail.IsSet())
		{
			if (const FProperty* Property = InGraphPropertyNode->GetProperty())
			{
				DisplayThumbnail = GetTagOrBoolMetadata(Property, TEXT("DisplayThumbnail"));
			}
		}
	}

	return DisplayThumbnail.Get(FSMBlueprintEditorUtils::GetProjectEditorSettings()->bDisplayThumbnailsByDefault);
}

FText LD::Editor::PropertyUtils::GetPropertyDisplayName(const FText& InPropertyText, const int32 InArrayIndex)
{
	if (InArrayIndex >= 0)
	{
		return FText::Format(NSLOCTEXT("SMPropertyData", "PropertyNameArray", "{0} {1}"), InPropertyText,
			FText::FromString(*FString::FromInt(InArrayIndex)));
	}
	
	return InPropertyText;
}

void LD::Editor::PropertyUtils::MapNodeTemplatesToProperties(USMGraphNode_StateNodeBase* InStateNode,
                                                                      TMap<USMNodeInstance*, TArray<USMGraphK2Node_PropertyNode_Base*>>& OutTemplatePropertyMap)
{
	// Add custom property widgets sorted by user specification.
	USMGraphNode_StateNodeBase* StateNodeBase = InStateNode;
	TArray<USMGraphK2Node_PropertyNode_Base*> GraphPropertyNodes = StateNodeBase->GetAllPropertyGraphNodesAsArray();
	
	TArray<USMGraphK2Node_PropertyNode_Base*> GraphPropertyBPVariablesOrdered;
	// Each property node mapped to its instance.
	TMap<USMGraphK2Node_PropertyNode_Base*, USMNodeInstance*> PropertiesToTemplates;
	
	// Populate all used state classes, in order.
	TArray<USMNodeInstance*> NodeTemplates;

	auto IsValidClass = [](const USMGraphNode_Base* Node, const UClass* NodeClass) {return NodeClass && NodeClass != Node->GetDefaultNodeClass(); };

	if (IsValidClass(StateNodeBase, StateNodeBase->GetNodeClass()))
	{
		if (USMNodeInstance* NodeTemplate = StateNodeBase->GetNodeTemplate())
		{
			NodeTemplates.Add(NodeTemplate);
		}
	}
	
	if (USMGraphNode_StateNode* StateNode = Cast<USMGraphNode_StateNode>(StateNodeBase))
	{
		for (const FStateStackContainer& StackTemplate : StateNode->StateStack)
		{
			if (StackTemplate.NodeStackInstanceTemplate)
			{
				if (IsValidClass(StateNodeBase, StackTemplate.NodeStackInstanceTemplate->GetClass()))
				{
					NodeTemplates.Add(StackTemplate.NodeStackInstanceTemplate);
				}
			}
		}
	}

	/*
	 * Look for array types and build out templates.
	 */
	auto ExpandAndSortProperty = [&GraphPropertyNodes, &GraphPropertyBPVariablesOrdered, &PropertiesToTemplates]
	(USMGraphK2Node_PropertyNode_Base* GraphProperty, USMNodeInstance* NodeTemplate)
	{
		if (GraphProperty)
		{
			FSMGraphProperty_Base* RealPropertyNode = GraphProperty->GetPropertyNode();
			if (!RealPropertyNode)
			{
				return false;
			}

			// Look for array items that may belong to this property.
			TArray<USMGraphK2Node_PropertyNode_Base*> ArrayItems = GraphPropertyNodes.
				FilterByPredicate([&RealPropertyNode](const USMGraphK2Node_PropertyNode_Base* PropertyNode)
			{
				const FSMGraphProperty_Base* TestProperty = PropertyNode->GetPropertyNodeConst();
				if (!TestProperty)
				{
					return false;
				}
				return TestProperty->VariableName == RealPropertyNode->VariableName && TestProperty->GetTemplateGuid() == RealPropertyNode->GetTemplateGuid();
			});
			ArrayItems.Sort([&](USMGraphK2Node_PropertyNode_Base& lhs, USMGraphK2Node_PropertyNode_Base& rhs)
			{
				// Should never be null unless something was forcibly deleted or an underlying class removed.
				const FSMGraphProperty_Base* PropertyLhs = lhs.GetPropertyNode();
				if (!PropertyLhs)
				{
					return false;
				}

				const FSMGraphProperty_Base* PropertyRhs = rhs.GetPropertyNode();
				if (!PropertyRhs)
				{
					return false;
				}

				return PropertyLhs->ArrayIndex <= PropertyRhs->ArrayIndex;
			});

			// Add on array items in the correct order directly after the first element.
			for (USMGraphK2Node_PropertyNode_Base* ArrItem : ArrayItems)
			{
				GraphPropertyNodes.Remove(ArrItem);
				PropertiesToTemplates.Add(ArrItem, NodeTemplate);
			}
			
			GraphPropertyBPVariablesOrdered.Append(ArrayItems);
			PropertiesToTemplates.Add(GraphProperty, NodeTemplate);
			
			return true;
		}

		return false;
	};
	
	for (USMNodeInstance* NodeTemplate : NodeTemplates)
	{
		// Always add the template, this allows us to easily draw it later even if there are no properties present.
		OutTemplatePropertyMap.FindOrAdd(NodeTemplate);
		
		{
			// Check native properties first. Reference PropertyEditor/Private/PropertyEditorHelpers.cpp  ~ OrderPropertiesFromMetadata()
			
			TArray<FProperty*> NativeProperties;
			TArray<TTuple<FProperty*, int32>> NativePropertiesOrdered;
			TMap<FName, TArray<TTuple<FProperty*, int32>>> DisplayAfterPropertyMap;
			for (TFieldIterator<FProperty> It(NodeTemplate->GetClass()); It; ++It)
			{
				FProperty* NativeProperty = *It;
				if (!NativeProperty->GetOwnerUField()->HasAnyInternalFlags(EInternalObjectFlags::Native) ||
					(!IsPropertyExposedToGraphNode(NativeProperty) && !GetGraphPropertyFromProperty(NativeProperty)))
				{
					// Blueprint properties checked later.
					continue;
				}

				NativeProperties.Add(NativeProperty);
			}
			
			NativePropertiesOrdered.Reserve(NativeProperties.Num());
			for (FProperty* NativeProperty : NativeProperties)
			{
				// DisplayPriority and DisplayAfter handling should be almost identical to OrderPropertiesFromMetadata
				// under PropertyEditorHelpers.cpp since details panel ordering and node ordering should be consistent.

				// The overall sorting with blueprint variables won't handle the same as the details panel however,
				// since we handle blueprint variables separately after native processing. This means a native property
				// could potentially use DisplayAfter="BP_Child_Prop" and the engine would support that, but we do not.
				
				// Sort native properties based on their display order only.
				const FString& DisplayPriorityStr = NativeProperty->GetMetaData(TEXT("DisplayPriority"));
				int32 DisplayPriority = (DisplayPriorityStr.IsEmpty() ? MAX_int32 : FCString::Atoi(*DisplayPriorityStr));
				if (DisplayPriority == 0 && !FCString::IsNumeric(*DisplayPriorityStr))
				{
					// If there was a malformed display priority str Atoi will say it is 0, but we want to treat it as unset
					DisplayPriority = MAX_int32;
				}

				auto InsertProperty = [NativeProperty, DisplayPriority](TArray<TTuple<FProperty*, int32>>& InsertToArray)
				{
					bool bInserted = false;
					if (DisplayPriority != MAX_int32)
					{
						for (int32 InsertIndex = 0; InsertIndex < InsertToArray.Num(); ++InsertIndex)
						{
							const int32 PriorityAtIndex = InsertToArray[InsertIndex].Get<1>();
							if (DisplayPriority < PriorityAtIndex)
							{
								InsertToArray.Insert(MakeTuple(NativeProperty, DisplayPriority), InsertIndex);
								bInserted = true;
								break;
							}
						}
					}

					if (!bInserted)
					{
						InsertToArray.Emplace(MakeTuple(NativeProperty, DisplayPriority));
					}
				};

				const FString& DisplayAfterStr = NativeProperty->GetMetaData(TEXT("DisplayAfter"));
				if (DisplayAfterStr.IsEmpty())
				{
					InsertProperty(NativePropertiesOrdered);
				}
				else
				{
					TArray<TPair<FProperty*, int32>>& DisplayAfterProperties = DisplayAfterPropertyMap.FindOrAdd(FName(*DisplayAfterStr));
					InsertProperty(DisplayAfterProperties);
				}
			}

			// While there are still properties that need insertion seek out the property they should be listed after and insert them in their pre-display priority sorted order
			int32 RemainingDisplayAfterNodes = -1; // avoid infinite loop caused by cycles or missing dependencies by tracking that the map shrunk each iteration
			while (DisplayAfterPropertyMap.Num() > 0 && DisplayAfterPropertyMap.Num() != RemainingDisplayAfterNodes)
			{
				RemainingDisplayAfterNodes = DisplayAfterPropertyMap.Num();

				for (int32 InsertIndex = 0; InsertIndex < NativePropertiesOrdered.Num(); ++InsertIndex)
				{
					FProperty* Prop = NativePropertiesOrdered[InsertIndex].Get<0>();

					if (TArray<TTuple<FProperty*, int32>>* DisplayAfterProperties = DisplayAfterPropertyMap.Find(Prop->GetFName()))
					{
						NativePropertiesOrdered.Insert(MoveTemp(*DisplayAfterProperties), InsertIndex + 1);
						DisplayAfterPropertyMap.Remove(Prop->GetFName());
						if (DisplayAfterPropertyMap.Num() == 0)
						{
							break;
						}
					}
				}
			}

			// Reload back into array.
			NativeProperties.Reset();
			for (const TTuple<FProperty*, int32>& Property : NativePropertiesOrdered)
			{
				NativeProperties.Add(Property.Get<0>());
			}

			if (DisplayAfterPropertyMap.Num() != 0)
			{
				// If we hit this there is either a cycle or a dependency on something that doesn't exist, so just put them at the end of the list
				for (TPair<FName, TArray<TTuple<FProperty*, int32>>>& DisplayAfterProperties : DisplayAfterPropertyMap)
				{
					for (const TTuple<FProperty*, int32>& Property : DisplayAfterProperties.Value)
					{
						NativeProperties.Add(Property.Get<0>());
					}
				}
			}

			for (FProperty* NativeProperty : NativeProperties)
			{
				FSMGraphProperty_Base Property;
				Property.SetTemplateGuid(NodeTemplate->GetTemplateGuid());
				SetGraphPropertyFromProperty(Property, NativeProperty, NodeTemplate);

				const FGuid& CalculatedGuid = Property.GetGuid();

				// First lookup by var guid. This is the standard lookup.
				USMGraphK2Node_PropertyNode_Base* GraphProperty = StateNodeBase->GetGraphPropertyNode(CalculatedGuid);

				// Attempt lookup by name -- Only can happen on extended graph properties.
				if (!GraphProperty)
				{
					// TODO: This may not be necessary, at least for native properties.
					GraphProperty = StateNodeBase->GetGraphPropertyNode(NativeProperty->GetFName(), NodeTemplate);
				}

				if (!ExpandAndSortProperty(GraphProperty, NodeTemplate))
				{
					continue;
				}
			}
		}

		// Blueprint variable sorting. Grab the blueprint and all parents.
		{
			TArray<UBlueprint*> BlueprintParents;
			UBlueprint::GetBlueprintHierarchyFromClass(NodeTemplate->GetClass(), BlueprintParents);

			TArray<FBPVariableDescription> Variables;
			for (UBlueprint* Blueprint : BlueprintParents)
			{
				Variables.Append(Blueprint->NewVariables);
			}

			// Check blueprint properties.
			for (const FBPVariableDescription& Variable : Variables)
			{
				FSMGraphProperty_Base Property;
				Property.SetTemplateGuid(NodeTemplate->GetTemplateGuid());
				const FGuid& CalculatedGuid = Property.SetGuid(Variable.VarGuid, 0);

				// First lookup by var guid. This is the standard lookup.
				USMGraphK2Node_PropertyNode_Base* GraphProperty = StateNodeBase->GetGraphPropertyNode(CalculatedGuid);

				// Attempt lookup by name -- Only can happen on extended graph properties.
				if (!GraphProperty)
				{
					GraphProperty = StateNodeBase->GetGraphPropertyNode(Variable.VarName, NodeTemplate);
				}

				if (!ExpandAndSortProperty(GraphProperty, NodeTemplate))
				{
					continue;
				}
			}
		}
	}

	// GraphPropertyNodes are just native / non-variable properties. Add sorted BP on after.
	GraphPropertyNodes.Append(MoveTemp(GraphPropertyBPVariablesOrdered));

	TMap<int32, TArray<USMGraphK2Node_PropertyNode_Base*>> CustomOrderMap;
	{
		// Perform custom sorting using widget vertical order override. Maintain the desired order accounting for combined states.
		// TODO deprecate: Vertical order attribute is now deprecated in favor of DisplayOrder (native) or variable order in BP. Remove this code block eventually.
		int32 BaseCount = 0;
		int32 TotalCount = 0;
		USMNodeInstance* LastTemplate = nullptr;
		for (USMGraphK2Node_PropertyNode_Base* PropertyNode : GraphPropertyNodes)
		{
			USMNodeInstance* CurrentTemplate = PropertiesToTemplates.FindRef(PropertyNode);
			if (CurrentTemplate && CurrentTemplate != LastTemplate && CurrentTemplate->GetTemplateGuid().IsValid())
			{
				BaseCount = TotalCount;
			} 
			TotalCount++;
			LastTemplate = CurrentTemplate;

			if (FSMGraphProperty_Base* Property = PropertyNode->GetPropertyNode())
			{
				PRAGMA_DISABLE_DEPRECATION_WARNINGS
				const int32 Order = Property->GetVerticalDisplayOrder();
				PRAGMA_ENABLE_DEPRECATION_WARNINGS
				if (Order != 0)
				{
					// Look for all related elements since this could be an array that is being re-ordered.
					TArray<USMGraphK2Node_PropertyNode_Base*> PropertiesToMove = GraphPropertyNodes.
						FilterByPredicate([&Property](USMGraphK2Node_PropertyNode_Base* PropertyNode)
					{
						const FSMGraphProperty_Base* TestProperty = PropertyNode->GetPropertyNodeConst();
						if (!TestProperty)
						{
							return false;
						}
						return TestProperty->VariableName == Property->VariableName && TestProperty->GetTemplateGuid() == Property->GetTemplateGuid();
					});

					CustomOrderMap.Add(BaseCount + Order, MoveTemp(PropertiesToMove));
				}
			}
		}
	}

	// Insert or add the elements to the array.
	for (TPair<int32, TArray<USMGraphK2Node_PropertyNode_Base*>>& KeyVal : CustomOrderMap)
	{
		TArray<USMGraphK2Node_PropertyNode_Base*>& PropertyNodes = KeyVal.Value;
		for (USMGraphK2Node_PropertyNode_Base* PropertyNode : PropertyNodes)
		{
			GraphPropertyNodes.Remove(PropertyNode);
		}

		const int32 Index = FMath::Clamp(KeyVal.Key, 0, GraphPropertyNodes.Num());
		if (Index > GraphPropertyNodes.Num() - 1)
		{
			GraphPropertyNodes.Append(PropertyNodes);
		}
		else
		{
			GraphPropertyNodes.Insert(PropertyNodes, Index);
		}
	}

	for (USMGraphK2Node_PropertyNode_Base* PropertyNode : GraphPropertyNodes)
	{
		check(PropertyNode);
		USMNodeInstance* OwningTemplate = PropertyNode->GetOwningTemplate();
		if (OwningTemplate == nullptr)
		{
			LDEDITOR_LOG_ERROR(TEXT("Missing OwningTemplate for PropertyNode %s."), *PropertyNode->GetName())
			continue;
		}

		TArray<USMGraphK2Node_PropertyNode_Base*>& TemplateProperties = OutTemplatePropertyMap.FindOrAdd(OwningTemplate);
		TemplateProperties.Add(PropertyNode);
	}
}
