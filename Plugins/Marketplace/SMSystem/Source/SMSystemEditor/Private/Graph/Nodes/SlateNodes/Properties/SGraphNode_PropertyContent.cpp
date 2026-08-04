// Copyright Recursoft LLC. All Rights Reserved.

#include "SGraphNode_PropertyContent.h"

#include "Configuration/SMEditorSettings.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/Nodes/SlateNodes/Properties/SSMGraphProperty_Base.h"
#include "Graph/Nodes/SMGraphNode_StateNode.h"
#include "Properties/SMEditorPropertyUtils.h"
#include "SSMGraphPropertyTreeView.h"
#include "Utilities/SMBlueprintEditorUtils.h"
#include "Utilities/SMNodeInstanceUtils.h"

#include "SMNodeInstance.h"

#include "SMUnrealTypeDefs.h"

#define LOCTEXT_NAMESPACE "SSMGraphNode_PropertyContent"

SSMGraphNode_PropertyContent::SSMGraphNode_PropertyContent(): GraphNode(nullptr)
{
}

SSMGraphNode_PropertyContent::~SSMGraphNode_PropertyContent()
{
	GraphNode.Reset();
	PropertyWidgets.Empty();
}

void SSMGraphNode_PropertyContent::Construct(const FArguments& InArgs)
{
	GraphNode = InArgs._GraphNode;
	ChildSlot
	[
		CreateContent().ToSharedRef()
	];
}

void SSMGraphNode_PropertyContent::Finalize()
{
	for (const auto& PropertyWidget : NodeInstanceProperties)
	{
		PropertyWidget.Value->FinalizePropertyWidgets();
	}	
}

bool SSMGraphNode_PropertyContent::RefreshAllProperties()
{
	TMap<USMNodeInstance*, TArray<USMGraphK2Node_PropertyNode_Base*>> TemplatePropertyMap;
	LD::Editor::PropertyUtils::MapNodeTemplatesToProperties(CastChecked<USMGraphNode_StateNodeBase>(GraphNode), TemplatePropertyMap);
	
	if (NodeInstanceProperties.Num() != TemplatePropertyMap.Num())
	{
		return false;
	}
	
	for (const auto& KeyVal : TemplatePropertyMap)
	{
		const USMNodeInstance* CurrentTemplate = KeyVal.Key;
		const TArray<USMGraphK2Node_PropertyNode_Base*>& PropertyNodes = KeyVal.Value;

		if (const TSharedPtr<SSMNodeInstancePropertyView>* PropertyView = NodeInstanceProperties.Find(CurrentTemplate))
		{
			(*PropertyView)->RefreshPropertyWidgets(PropertyNodes);
			continue;
		}
		
		return false;
	}

	return true;
}

TSharedPtr<SWidget> SSMGraphNode_PropertyContent::CreateContent()
{
	TSharedPtr<SVerticalBox> Content;
	TSharedPtr<SVerticalBox> NodePropertiesBox;
	SAssignNew(Content, SVerticalBox)
	+SVerticalBox::Slot()
	.AutoHeight()
	[
		SAssignNew(NodePropertiesBox, SVerticalBox)
	];

	PropertyWidgets.Reset();

	const USMGraphNode_StateNodeBase* StateNodeBase = CastChecked<USMGraphNode_StateNodeBase>(GraphNode);
	const USMEditorSettings* EditorSettings = FSMBlueprintEditorUtils::GetEditorSettings();

	TMap<USMNodeInstance*, TArray<USMGraphK2Node_PropertyNode_Base*>> TemplatePropertyMap;
	LD::Editor::PropertyUtils::MapNodeTemplatesToProperties(CastChecked<USMGraphNode_StateNodeBase>(GraphNode), TemplatePropertyMap);

	NodeInstanceProperties.Reset();
	NodeInstanceProperties.Reserve(TemplatePropertyMap.Num());
	
	for (const auto& KeyVal : TemplatePropertyMap)
	{
		USMNodeInstance* CurrentTemplate = KeyVal.Key;
		const TArray<USMGraphK2Node_PropertyNode_Base*>& PropertyNodes = KeyVal.Value;
		
		TSharedPtr<SSMNodeInstancePropertyView> PropertyView;
		SAssignNew(PropertyView, SSMNodeInstancePropertyView, CurrentTemplate, PropertyNodes)
		.GraphNode(GraphNode.Get());
		
		NodeInstanceProperties.Add(CurrentTemplate, PropertyView);

		for (const TTuple<USMGraphK2Node_PropertyNode_Base*, TSharedPtr<SSMGraphProperty_Base>>&
		     PropertyKeyVal : PropertyView->GetPropertyWidgets())
		{
			PropertyWidgets.Add(PropertyKeyVal.Value, PropertyKeyVal.Key);
		}

		TSharedPtr<SVerticalBox> NodeStackBox;
		
		// State stack handling.
		if (CurrentTemplate->GetTemplateGuid().IsValid())
		{
			FLinearColor BackgroundColor = StateNodeBase->GetBackgroundColorForNodeInstance(CurrentTemplate);
			BackgroundColor.A *= 0.25f;

			const FString StateStackName = FSMNodeInstanceUtils::GetNodeDisplayName(CurrentTemplate);
			const bool bDisplayStateStackClassNames = EditorSettings->bDisplayStateStackClassNames;

			// If not displaying names and there are no properties we don't need to render anything for the state stack.
			if (bDisplayStateStackClassNames || PropertyNodes.Num() > 0)
			{
				TSharedPtr<SWidget> StackBox;
				
				Content->AddSlot()
					.AutoHeight()
					[
						SNew(SBorder)
						.BorderImage(FSMUnrealAppStyle::Get().GetBrush("Graph.StateNode.Body"))
						.Padding(2)
						.BorderBackgroundColor(BackgroundColor)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.f, 1.f, 0.f, 0.f)
							[
								SAssignNew(StackBox, STextBlock)
								.Text(FText::FromString(StateStackName))
								.TextStyle(FSMUnrealAppStyle::Get(), "NormalText.Important")
								.Visibility(bDisplayStateStackClassNames ? EVisibility::Visible : EVisibility::Collapsed)
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SAssignNew(NodeStackBox, SVerticalBox)
							]
						]
					];
				
				const USMGraphNode_StateNode* StateNode = CastChecked<USMGraphNode_StateNode>(GraphNode);
				const int32 TemplateIndex = StateNode->GetIndexOfTemplate(CurrentTemplate->GetTemplateGuid());
				check(StateNode->GetAllNodeStackTemplates().IsValidIndex(TemplateIndex));
				StateNode->GetAllNodeStackTemplates()[TemplateIndex].StackWidget = StackBox;
			}
		}

		const TSharedPtr<SVerticalBox> ContentBoxToUse = NodeStackBox.IsValid() ? NodeStackBox : NodePropertiesBox;
		
		ContentBoxToUse->AddSlot()
		[
			PropertyView.ToSharedRef()
		];
	}

	return Content;
}

#undef LOCTEXT_NAMESPACE
