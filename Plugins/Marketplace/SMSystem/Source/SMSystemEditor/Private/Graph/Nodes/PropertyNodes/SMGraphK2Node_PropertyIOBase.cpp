// Copyright Recursoft LLC. All Rights Reserved.

#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyIOBase.h"

#include "Compilers/SMKismetCompiler.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/Nodes/SMGraphNode_Base.h"
#include "Graph/SMPropertyGraph.h"
#include "Utilities/SMBlueprintEditorUtils.h"

#include "K2Node_MakeStruct.h"

#define LOCTEXT_NAMESPACE "SMPropertyIOBase"

bool USMGraphK2Node_PropertyIOBase::IsCompatibleWithGraph(UEdGraph const* Graph) const
{
	return Graph->IsA<USMPropertyGraph>();
}

void USMGraphK2Node_PropertyIOBase::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);

	if (IsPropertySelectionPin(Pin) && bCallUpdatePropertySelectionOnValueChange)
	{
		UpdatePropertySelectionPin(Pin, true);
	}
}

void USMGraphK2Node_PropertyIOBase::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);
}

void USMGraphK2Node_PropertyIOBase::DestroyNode()
{
	LD::Editor::PropertyUtils::IO::BreakAllPropertyConnections(this);
	Super::DestroyNode();
}

void USMGraphK2Node_PropertyIOBase::ReconstructNode()
{
	Super::ReconstructNode();
	CleanInvalidReferencedIONodes();
}

void USMGraphK2Node_PropertyIOBase::PreCompileValidate(FCompilerResultsLog& MessageLog)
{
	Super::PreCompileValidate(MessageLog);

	USMGraphK2Node_PropertyNode_Base* OwningPropertyNode = GetOwningPropertyNode();

	if (ReferencedIONodes.Num() > GetMaxReferencedIONodes())
	{
		const FString Message = FString::Printf(TEXT("Current number of referenced IO nodes: %s, max allowed: %s for node @@."),
			*FString::FromInt(ReferencedIONodes.Num()), *FString::FromInt(GetMaxReferencedIONodes()));
		MessageLog.Warning(*Message, this);
		OwningPropertyNode->SetNotificationAndHighlight(true, ESMLogType::Warning, Message, true);
	}

	const TMap<FGuid, TWeakObjectPtr<USMGraphK2Node_PropertyNode_Base>> ReferencedIONodesCopy = ReferencedIONodes;
	for (const TTuple<FGuid, TWeakObjectPtr<USMGraphK2Node_PropertyNode_Base>>& ReferencedIONode : ReferencedIONodesCopy)
	{
		if (!ReferencedIONode.Value.IsValid())
		{
			if (UEdGraphPin* Pin = FindPinById(ReferencedIONode.Key))
			{
				TSharedRef<FTokenizedMessage> TokenizedMessage = MessageLog.Error(
					TEXT("Pin '@@' on property IO node '@@' does not reference a valid destination."), Pin, this);
				OwningPropertyNode->LogPropertyFromTokenizedMessage(TokenizedMessage);
			}
			else
			{
				MessageLog.Warning(TEXT("Unused IO reference on node @@, cleaning up."), this);
				RemoveReferencedIONode(ReferencedIONode.Key);
			}
		}
	}
}

void USMGraphK2Node_PropertyIOBase::PreCompile()
{
	CleanInvalidReferencedIONodes();
}

void USMGraphK2Node_PropertyIOBase::UpdatePropertySelectionPin(UEdGraphPin* InPropertyPin, const bool bIsUserAction)
{
	check(IsPropertySelectionPin(InPropertyPin));
	
	using namespace LD::Editor::PropertyUtils::IO;
	
	if (UEdGraphPin* PropertyConnectorPin = FindOppositePin(InPropertyPin))
	{
		if (const UEdGraph* Graph = GetGraph())
		{
			const UEdGraphSchema_K2* K2Schema = CastChecked<UEdGraphSchema_K2>(Graph->GetSchema());

			auto AttemptReconnection = [&](const bool bHideConnectorPin)
			{
				// Attempt a reconnection to make sure the new pin type is compatible or
				// conversion nodes need to be placed.
				if (UEdGraphPin* LinkedToPin = TryGetOwningPropertyNodeRelatedPin())
				{
					K2Schema->BreakPinLinks(*PropertyConnectorPin, true);

					if (ShouldBreakResultPinLinksOnTypeChange())
					{
						K2Schema->BreakPinLinks(*LinkedToPin, true);
					}
					
					if (!bHideConnectorPin)
					{
						K2Schema->TryCreateConnection(LinkedToPin, PropertyConnectorPin);
					}
				}
			};
			
			if (const TSharedPtr<FPropertyData_Base> PropertyData = GetPropertyDataFromPin(InPropertyPin))
			{
				if (PropertyData->Property)
				{
					const bool bHideConnectorPin = !CanDisplayConnectorPin(PropertyData, InPropertyPin);
			
					if (bIsUserAction)
					{
						PropertyConnectorPin->Modify();
					}
					// Can't manage individual connections to graph properties.
					PropertyConnectorPin->bNotConnectable = bHideConnectorPin;

					const FProperty* Property = PropertyData->Property;

					// If this is a property on a node then we can't use the array directly and need to go by the element.
					if (const TSharedPtr<FNodePropertyData> NodePropertyData = PropertyData->As<FNodePropertyData>())
					{
						if (NodePropertyData->ArrayIndex != INDEX_NONE)
						{
							if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
							{
								Property = ArrayProperty->Inner;
							}
						}
					}
				
					FEdGraphPinType VariablePinType;
					if (K2Schema->ConvertPropertyToPinType(Property, VariablePinType))
					{
						const bool bTypeChanged = PropertyConnectorPin->PinType != VariablePinType;
						if (bTypeChanged)
						{
							PropertyConnectorPin->PinType = VariablePinType;
							PinTypeChanged(PropertyConnectorPin);
						}
						if (bIsUserAction)
						{
							if (PropertyConnectorPin->LinkedTo.Num() > 0)
							{
								if (bTypeChanged || bHideConnectorPin)
								{
									AttemptReconnection(bHideConnectorPin);
								}
							}
							else if (!bHideConnectorPin)
							{
								// Wire a new connection.
								if (UEdGraphPin* OwningPropertyPin = TryGetOwningPropertyNodeRelatedPin())
								{
									K2Schema->TryCreateConnection(OwningPropertyPin, PropertyConnectorPin);
								}
							}
						}
					}
					else
					{
						LDEDITOR_LOG_ERROR(TEXT("Failed to determine pin type for property %s."), *PropertyData->Property->GetName());
					}
				}
				else
				{
					if (bIsUserAction)
					{
						PropertyConnectorPin->Modify();
						K2Schema->BreakPinLinks(*PropertyConnectorPin, true);
					}
			
					PropertyConnectorPin->bNotConnectable = true;
				}
			}
			else
			{
				// No property selected, revert to wild card.
				if (PropertyConnectorPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard)
				{
					PropertyConnectorPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
					PinTypeChanged(PropertyConnectorPin);
					const bool bHideConnectorPin = !CanDisplayConnectorPin(nullptr, InPropertyPin);
					AttemptReconnection(bHideConnectorPin);
				}
			}
		}
	}
	else
	{
		LDEDITOR_LOG_ERROR(TEXT("Could not find matching property connector pin for property pin %s."), *InPropertyPin->GetName())
	}

	if (bIsUserAction && !bProgramaticallySettingPropertyPinValue)
	{
		const TSharedPtr<FPropertyData_Base> PropertyData = GetPropertyDataFromPin(InPropertyPin);
		USMGraphK2Node_PropertyNode_Base* PropertyNode = GetPropertyNode(PropertyData);
		if (PropertyData && PropertyNode)
		{
			TryMakePropertyConnection(InPropertyPin, PropertyNode);
		}
		else
		{
			BreakPropertyConnection(InPropertyPin);
			if (PropertyData && !PropertyNode)
			{
				// We're linking to a non-node property value. Make sure to set the pin value since it was reset
				// during the BreakPropertyConnection above. In this case we're not managing the connection, just
				// pointing to the other property.
				SetPropertySelectionPinValue(InPropertyPin, PropertyData);
			}
		}
		
		ReconstructNode();
	}
}

void USMGraphK2Node_PropertyIOBase::SetPropertySelectionPinValue(UEdGraphPin* InPropertyPin,
	const TSharedPtr<LD::Editor::PropertyUtils::IO::FPropertyData_Base>& InPropertyValue,
	const TOptional<FName>& InPropertyNameOverride)
{
	if (bProgramaticallySettingPropertyPinValue)
	{
		return;
	}
	
	check(InPropertyPin && !InPropertyPin->bWasTrashed);
	check(IsPropertySelectionPin(InPropertyPin));

	const FString SerializedValue = InPropertyValue.IsValid()
		? SerializePropertyDataToString(InPropertyValue, InPropertyNameOverride) : TEXT("");

	Modify();
	TGuardValue SetPinValueGuard(bProgramaticallySettingPropertyPinValue, true);
	if (InPropertyNameOverride.IsSet())
	{
		bCallUpdatePropertySelectionOnValueChange = false;
	}
	
	GetSchema()->TrySetDefaultValue(*InPropertyPin, SerializedValue);
	
	bCallUpdatePropertySelectionOnValueChange = true;
	
	// Check if we're handling node property data.
	if (USMGraphK2Node_PropertyNode_Base* FromPropertyNode = GetPropertyNode(InPropertyValue))
	{
		AddReferencedIONode(InPropertyPin->PinId, FromPropertyNode);
	}
	else
	{
		RemoveReferencedIONode(InPropertyPin->PinId);
	}
}

TSharedPtr<LD::Editor::PropertyUtils::IO::FNodePropertyData> USMGraphK2Node_PropertyIOBase::GetOwningPropertyNodePropertyData() const
{
	using namespace LD::Editor::PropertyUtils::IO;
	
	TSharedPtr<FNodePropertyData> PropertyData = MakeShared<FNodePropertyData>();

	if (USMGraphK2Node_PropertyNode_Base* OwningPropertyNode = GetOwningPropertyNode())
	{
		PropertyData->NodeGuid = OwningPropertyNode->GetOwningNodeGuidChecked();
		PropertyData->TemplateGuid = OwningPropertyNode->GetOwningTemplateGuid();
		PropertyData->SourceObject = OwningPropertyNode;

		if (const FProperty* Property = OwningPropertyNode->GetProperty())
		{
			PropertyData->Property = Property;
		}

		if (OwningPropertyNode->GetPropertyNodeConstChecked()->bIsInArray)
		{
			PropertyData->ArrayIndex = OwningPropertyNode->GetPropertyNodeConstChecked()->ArrayIndex;
		}
	}
	
	return PropertyData;
}

USMGraphK2Node_PropertyNode_Base* USMGraphK2Node_PropertyIOBase::GetOwningPropertyNode() const
{
	if (const UEdGraph* OwningGraph = GetGraph())
	{
		return FSMBlueprintEditorUtils::GetFirstNodeOfClassNested<USMGraphK2Node_PropertyNode_Base>(OwningGraph);
	}

	return nullptr;
}

USMGraphNode_Base* USMGraphK2Node_PropertyIOBase::GetOwningGraphNode() const
{
	if (const USMGraphK2Node_PropertyNode_Base* OwningPropertyNode = GetOwningPropertyNode())
	{
		return OwningPropertyNode->GetOwningGraphNode();
	}

	return nullptr;
}

FName USMGraphK2Node_PropertyIOBase::GetPropertySelectionPinIdentifierType()
{
	return UEdGraphSchema_K2::PC_String;
}

bool USMGraphK2Node_PropertyIOBase::IsPropertySelectionPin(const UEdGraphPin* Pin) const
{
	return false;
}

bool USMGraphK2Node_PropertyIOBase::IsPropertyConnectorPin(const UEdGraphPin* Pin) const
{
	return false;
}

bool USMGraphK2Node_PropertyIOBase::CanAcceptConnection(
	const USMGraphK2Node_PropertyIOBase* OtherPropertyIONode) const
{
	return OtherPropertyIONode
	&& this != OtherPropertyIONode
	&& !OtherPropertyIONode->IsA(GetClass());
}

bool USMGraphK2Node_PropertyIOBase::CanAcceptConnection(
	const USMGraphK2Node_PropertyNode_Base* OtherPropertyNode) const
{
	if (OtherPropertyNode)
	{
		// todo output variables check if actual pin connection allowed (ie cast)
		return true;
	}
	return false;
}

TArray<UEdGraphPin*> USMGraphK2Node_PropertyIOBase::GetAllPropertySelectionPins() const
{
	TArray<UEdGraphPin*> PropertyPins;

	for (UEdGraphPin* Pin : Pins)
	{
		if (IsPropertySelectionPin(Pin))
		{
			PropertyPins.Add(Pin);
		}
	}

	return PropertyPins;
}

void USMGraphK2Node_PropertyIOBase::AddReferencedIONode(const FGuid& InPinGuid, USMGraphK2Node_PropertyNode_Base* InNode)
{
	check(InNode);
	if (!ReferencedIONodes.Contains(InPinGuid))
	{
		Modify();
		ReferencedIONodes.Add(InPinGuid, InNode);
		ensure(ReferencedIONodes.Num() <= GetMaxReferencedIONodes());
	}
}

void USMGraphK2Node_PropertyIOBase::RemoveReferencedIONode(const FGuid& InPinGuid)
{
	check(InPinGuid.IsValid());
	Modify();
	ReferencedIONodes.Remove(InPinGuid);
}

int32 USMGraphK2Node_PropertyIOBase::GetIOReferenceCount(const USMGraphK2Node_PropertyNode_Base* InPropertyNode) const
{
	int32 Count = 0;
	const TArray<USMGraphK2Node_PropertyNode_Base*> PropertyNodes = GetAllReferencedIONodes();
	for (const USMGraphK2Node_PropertyNode_Base* PropertyNode : PropertyNodes)
	{
		if (PropertyNode == InPropertyNode)
		{
			Count++;
		}
	}
	return Count;
}

USMGraphK2Node_PropertyNode_Base* USMGraphK2Node_PropertyIOBase::GetReferencedIONode(
	const FGuid& InPinGuid) const
{
	if (const TWeakObjectPtr<USMGraphK2Node_PropertyNode_Base>* PropertyNode = ReferencedIONodes.Find(InPinGuid))
	{
		return PropertyNode->Get();
	}
	
	return nullptr;
}

TArray<USMGraphK2Node_PropertyNode_Base*> USMGraphK2Node_PropertyIOBase::GetAllReferencedIONodes() const
{
	TArray<USMGraphK2Node_PropertyNode_Base*> RawPropertyNodes;
	RawPropertyNodes.Reserve(ReferencedIONodes.Num());

	for (const TPair<FGuid, TWeakObjectPtr<USMGraphK2Node_PropertyNode_Base>>& ReferencedNode : ReferencedIONodes)
	{
		if (USMGraphK2Node_PropertyNode_Base* Node = ReferencedNode.Value.Get())
		{
			RawPropertyNodes.Add(Node);
		}
	}
	
	return RawPropertyNodes;
}

void USMGraphK2Node_PropertyIOBase::ReapplyConnections()
{
	for (const TPair<FGuid, TWeakObjectPtr<USMGraphK2Node_PropertyNode_Base>>& ReferencedNode : ReferencedIONodes)
	{
		UEdGraphPin* Pin = FindPinByIdChecked(ReferencedNode.Key);
		LD::Editor::PropertyUtils::IO::TryMakePropertyConnection(Pin, ReferencedNode.Value.Get());
	}
}

void USMGraphK2Node_PropertyIOBase::CleanInvalidReferencedIONodes()
{
	Modify();
	for (auto It = ReferencedIONodes.CreateIterator(); It; ++It)
	{
		if (!It->Value.IsValid()
			// The weak object ptr may not be cleared until a save or reload, but the graph itself will no longer
			// exist. This can occur if the user marks the owning property as no longer public.
			|| (!It->Value->FindOwningPropertyGraph()))
		{
			It.RemoveCurrent();
		}
	}
}

void USMGraphK2Node_PropertyIOBase::VerifyCorrectPinData(const UEdGraphPin* InPin,
                                                                  FCompilerResultsLog& InMessageLog) const
{
	check(InPin);
	using namespace LD::Editor::PropertyUtils::IO;
	if (!InPin->IsDefaultAsStringEmpty())
	{
		USMGraphK2Node_PropertyNode_Base* OwningPropertyNode = GetOwningPropertyNode();
		check(OwningPropertyNode);
		if (const TSharedPtr<FPropertyData_Base> Data = GetPropertyDataFromPin(InPin))
		{
			if (Data->PropertyOwnerType == EPropertyOwnerType::NodeInstance)
			{
				if (USMGraphK2Node_PropertyNode_Base* ReferencedNode = GetReferencedIONode(InPin->PinId))
				{
					if (USMGraphK2Node_PropertyIOBase* ConnectedIONode =
						FindPropertyIONodeThatShouldLinkToPropertyOutput(ReferencedNode, this))
					{
						if (ConnectedIONode->GetIOReferenceCount(GetOwningPropertyNode()) == 0)
						{
							const TSharedRef<FTokenizedMessage> TokenizedMessage = InMessageLog.Error(TEXT("IO node @@ references property @@ through connection @@, but a connection from @@ to @@ is not established."),
								this, ReferencedNode, ConnectedIONode, ConnectedIONode, this);

							ReferencedNode->LogPropertyFromTokenizedMessage(TokenizedMessage);
							OwningPropertyNode->LogPropertyFromTokenizedMessage(TokenizedMessage);
						}
					}
					else
					{
						const TSharedRef<FTokenizedMessage> TokenizedMessage = InMessageLog.Error(
							TEXT("IO node @@ references @@, but @@ does not reference @@."),
							this, ReferencedNode, ReferencedNode, this);
						
						ReferencedNode->LogPropertyFromTokenizedMessage(TokenizedMessage);
						OwningPropertyNode->LogPropertyFromTokenizedMessage(TokenizedMessage);
					}
				}
				else
				{
					const TSharedRef<FTokenizedMessage> TokenizedMessage = InMessageLog.Error(TEXT("Could not retrieve property data for pin @@ on IO node @@."), InPin, this);
					OwningPropertyNode->LogPropertyFromTokenizedMessage(TokenizedMessage);
				}
			}
		}
		else
		{
			const TSharedRef<FTokenizedMessage> TokenizedMessage = InMessageLog.Error(TEXT("Could not retrieve property data for @@."), this);
			OwningPropertyNode->LogPropertyFromTokenizedMessage(TokenizedMessage);
		}
	}
}

UK2Node_MakeStruct* USMGraphK2Node_PropertyIOBase::MakeAndWirePropertyPathStruct(FSMKismetCompilerContext& CompilerContext,
                                                                                          const TSharedPtr<LD::Editor::PropertyUtils::IO::FPropertyData_Base>& PropertyData, UEdGraphPin* OutputToPin)
{
	using namespace LD::Editor::PropertyUtils::IO;
	
	// Create a MakeStruct node for FSMPropertyPath.
	UK2Node_MakeStruct* MakeStructNode = CompilerContext.SpawnIntermediateNode<UK2Node_MakeStruct>(this, CompilerContext.ConsolidatedEventGraph);
	MakeStructNode->PostPlacedNewNode();
	MakeStructNode->StructType = FSMProxyPropertyPath::StaticStruct();
	MakeStructNode->VariableReference.SetSelfMember(FSMProxyPropertyPath::StaticStruct()->GetFName());
	MakeStructNode->AllocateDefaultPins();

	CompilerContext.MessageLog.NotifyIntermediateObjectCreation(MakeStructNode, this);
	
	// Populate the individual struct properties (PathGuid, TemplateGuid, PropertyName).
	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();

	FGuid PropertyNodeGuid, PropertyTemplateGuid;
	int32 ArrayIndex = INDEX_NONE;
	
	if (const TSharedPtr<FNodePropertyData> NodePropertyData = PropertyData->As<FNodePropertyData>())
	{
		PropertyNodeGuid = NodePropertyData->NodeGuid;
		PropertyTemplateGuid = NodePropertyData->TemplateGuid;
		ArrayIndex = NodePropertyData->ArrayIndex;
	}
	
	// PathGuid -- requires pin wire since this isn't guaranteed to be known until runtime.
	{
		UEdGraphPin* PathGuidPin = MakeStructNode->FindPinChecked(GET_MEMBER_NAME_CHECKED(FSMProxyPropertyPath, PathGuid));
		Schema->TrySetDefaultValue(*PathGuidPin, PropertyNodeGuid.ToString(), false);
	}

	// TemplateGuid (State stack)
	{
		UEdGraphPin* TemplateGuidPin = MakeStructNode->FindPinChecked(GET_MEMBER_NAME_CHECKED(FSMProxyPropertyPath, TemplateGuid));
		Schema->TrySetDefaultValue(*TemplateGuidPin, PropertyTemplateGuid.ToString(), false);
	}

	// PropertyName.
	{
		UEdGraphPin* PropertyNamePin = MakeStructNode->FindPinChecked(GET_MEMBER_NAME_CHECKED(FSMProxyPropertyPath, PropertyName));
		Schema->TrySetDefaultValue(*PropertyNamePin, PropertyData->Property->GetName(), false);
	}

	// ArrayIndex.
	{
		UEdGraphPin* ArrayIndexPin = MakeStructNode->FindPinChecked(GET_MEMBER_NAME_CHECKED(FSMProxyPropertyPath, ArrayIndex));
		Schema->TrySetDefaultValue(*ArrayIndexPin, FString::FromInt(ArrayIndex), false);
	}
	
	// Connect the MakeStruct node to the destination pin.
	ensure(Schema->TryCreateConnection(MakeStructNode->GetValuePin(), OutputToPin));

	return MakeStructNode;
}

void USMGraphK2Node_PropertyIOBase::SetPropertySelectionPinDefaults(UEdGraphPin* Pin)
{
	Pin->bNotConnectable = true;
	Pin->bDefaultValueIsIgnored = false;
}

void USMGraphK2Node_PropertyIOBase::SetPropertyConnectorPinDefaults(UEdGraphPin* Pin)
{
	Pin->bNotConnectable = true;
	Pin->bDefaultValueIsIgnored = true;
}

UEdGraphPin* USMGraphK2Node_PropertyIOBase::FindOppositePin(const UEdGraphPin* InPin) const
{
	return FindOppositePin(InPin->PinName);
}

UEdGraphPin* USMGraphK2Node_PropertyIOBase::FindOppositePin(const FName& InPinName) const
{
	// Get the base name of the pin by removing the "Out_" prefix.
	const FString InPinNameString = InPinName.ToString();
	FString BaseName, ExpectedOppositePinName;
	
	if (InPinNameString.StartsWith(GetInputPinPrefix()))
	{
		BaseName = InPinNameString.Replace(*GetInputPinPrefix(), TEXT(""), ESearchCase::CaseSensitive);
		ExpectedOppositePinName = GetOutputPinPrefix() + BaseName;
	}
	else
	{
		check(InPinNameString.StartsWith(GetOutputPinPrefix()));
		BaseName = InPinNameString.Replace(*GetOutputPinPrefix(), TEXT(""), ESearchCase::CaseSensitive);
		ExpectedOppositePinName = GetInputPinPrefix() + BaseName;
	}
	
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin && Pin != GetExecPin())
		{
			if (Pin->PinName.ToString() == ExpectedOppositePinName)
			{
				return Pin;
			}
		}
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE