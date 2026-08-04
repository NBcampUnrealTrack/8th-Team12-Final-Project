// Copyright Recursoft LLC. All Rights Reserved.

#include "Helpers/SMDragDropHelpers.h"

#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/SMPropertyGraph.h"
#include "ISMSystemEditorModule.h"
#include "SMGraphProperty_Base.h"

#include "BPVariableDragDropAction.h"
#include "EditorStyleSet.h"
#include "SMUnrealTypeDefs.h"

#define LOCTEXT_NAMESPACE "SMDragDropHelpers"

bool FSMDragDropHelpers::IsDragDropValidForPropertyNode(const USMGraphK2Node_PropertyNode_Base* PropertyNode,
	const FDragDropEvent& DragDropEvent, bool bAllowUneditableGraph)
{
	if (PropertyNode)
	{
		const ISMSystemEditorModule& SMBlueprintEditorModule = FModuleManager::GetModuleChecked<ISMSystemEditorModule>(LOGICDRIVER_EDITOR_MODULE_NAME);
		if (SMBlueprintEditorModule.IsPlayingInEditor())
		{
			return false;
		}
		
		if (const FSMGraphProperty_Base* GraphNode = PropertyNode->GetPropertyNodeConst())
		{
			if (GraphNode->IsVariableReadOnly())
			{
				return false;
			}
		}

		const TSharedPtr<FKismetVariableDragDropAction> VariableDragDrop = DragDropEvent.GetOperationAs<FKismetVariableDragDropAction>();
		if (VariableDragDrop.IsValid() && PropertyNode->GetPropertyGraph())
		{
			return PropertyNode->GetPropertyGraph()->IsGraphBeingUsedToEdit() || bAllowUneditableGraph;
		}

		const TSharedPtr<FKismetFunctionDragDropAction> FunctionDragDrop = DragDropEvent.GetOperationAs<FKismetFunctionDragDropAction>();
		if (FunctionDragDrop.IsValid() && PropertyNode->GetPropertyGraph() && PropertyNode->IsInputProperty() /* Only support input drops for now */)
		{
			if (!PropertyNode->GetPropertyGraph()->IsGraphBeingUsedToEdit() && !bAllowUneditableGraph)
			{
				return false;
			}

			UFunction const* Function = FSMDragDropAction_Function::GetFunction(FunctionDragDrop.Get());
			if (!Function->HasAnyFunctionFlags(FUNC_BlueprintPure))
			{
				return false;
			}

			return true;
		}
	}

	return false;
}

void FSMDragDropHelpers::SetDragDropMessage(const FDragDropEvent& DragDropEvent, bool bSuccess)
{
	if (const TSharedPtr<FKismetVariableDragDropAction> VariableDragDrop = DragDropEvent.GetOperationAs<FKismetVariableDragDropAction>())
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("DraggedVariableName"), FText::FromString(VariableDragDrop->GetVariableProperty()->GetName()));

		if (bSuccess)
		{
			const FSlateBrush* StatusSymbol = FSMUnrealAppStyle::Get().GetBrush(TEXT("Graph.ConnectorFeedback.OK"));
			VariableDragDrop->SetSimpleFeedbackMessage(StatusSymbol, FLinearColor::White,
				FText::Format(LOCTEXT("PlaceVariableOK", "Place variable '{DraggedVariableName}'"), Args));
		}
		else
		{
			const FSlateBrush* StatusSymbol = FSMUnrealAppStyle::Get().GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
			VariableDragDrop->SetSimpleFeedbackMessage(StatusSymbol, FLinearColor::White,
				FText::Format(LOCTEXT("PlaceVariableFail", "Cannot place variable '{DraggedVariableName}'"), Args));
		}
	}
	else if (const TSharedPtr<FKismetFunctionDragDropAction> FunctionDragDrop = DragDropEvent.GetOperationAs<FKismetFunctionDragDropAction>())
	{
		if (UFunction const* Function = FSMDragDropAction_Function::GetFunction(FunctionDragDrop.Get()))
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("DraggedFunctionName"), FText::FromString(Function->GetName()));

			if (bSuccess)
			{
				const FSlateBrush* StatusSymbol = FSMUnrealAppStyle::Get().GetBrush(TEXT("Graph.ConnectorFeedback.OK"));
				FunctionDragDrop->SetSimpleFeedbackMessage(StatusSymbol, FLinearColor::White,
				FText::Format(LOCTEXT("PlaceFunctionOK", "Place function '{DraggedFunctionName}'"), Args));
			}
			else
			{
				const FSlateBrush* StatusSymbol = FSMUnrealAppStyle::Get().GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
				FunctionDragDrop->SetSimpleFeedbackMessage(StatusSymbol, FLinearColor::White,
				FText::Format(LOCTEXT("PlaceFunctionFail", "Cannot place function '{DraggedFunctionName}'"), Args));
			}
		}
	}
}

bool FSMDragDropHelpers::IsDragDropEventBPVariableOrFunction(const FDragDropEvent& DragDropEvent)
{
	return DragDropEvent.GetOperationAs<FKismetVariableDragDropAction>()
		|| DragDropEvent.GetOperationAs<FKismetFunctionDragDropAction>();
}

FMyBlueprintItemDragDropAction_DEFINITION

#undef LOCTEXT_NAMESPACE
