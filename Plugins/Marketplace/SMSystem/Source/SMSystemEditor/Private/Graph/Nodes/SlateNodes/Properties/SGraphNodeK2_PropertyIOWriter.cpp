// Copyright Recursoft LLC. All Rights Reserved.

#include "SGraphNodeK2_PropertyIOWriter.h"

#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyIOWriterNode.h"

#include "EdGraph/EdGraph.h"
#include "GraphEditorSettings.h"
#include "K2Node.h"
#include "K2Node_AddPinInterface.h"
#include "ScopedTransaction.h"
#include "SGraphPin.h"
#include "SMUnrealTypeDefs.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "SGraphNodeK2_PropertyIOWriter"

void SGraphNodeK2_PropertyIOWriter::Construct(const FArguments& InArgs, UK2Node* InNode)
{
	this->GraphNode = InNode;
	this->SetCursor(EMouseCursor::CardinalCross);
	this->UpdateGraphNode();
}

void SGraphNodeK2_PropertyIOWriter::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	const UEdGraphPin* GraphPin = PinToAdd->GetPinObj();
	const USMGraphK2Node_PropertyIOWriterNode* WriterNode = CastChecked<USMGraphK2Node_PropertyIOWriterNode>(GraphNode);

	constexpr float PrimaryInputHeight = 40.f;
	
	if (WriterNode->IsPropertySelectionPin(GraphPin))
	{
		if (RightNodeBox->NumSlots() == 1)
		{
			// Only exec slot processed so far. We need to space future pins out.
			RightNodeBox->AddSlot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(PrimaryInputHeight)
				[
					SNew(SVerticalBox)
					+SVerticalBox::Slot()
					.Padding(Settings->GetOutputPinPadding())
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("OutputText", "Output Targets"))
						.TextStyle(FSMUnrealAppStyle::Get(), NAME_DefaultPinLabelStyle)
						.ColorAndOpacity(FLinearColor::White)
					]
				]
			];
			RightNodeBox->AddSlot()
			.AutoHeight()
			[
				SNew(SSeparator)
				.Orientation(EOrientation::Orient_Horizontal)
				.Thickness(1.0f)
				.SeparatorImage(FAppStyle::GetBrush("WhiteBrush"))
			];
		}
	}

	if (USMGraphK2Node_PropertyIOWriterNode::IsPrimaryInputPin(GraphPin))
	{
		PinToAdd->SetOwner(SharedThis(this));

		check(PinToAdd->GetDirection() == EEdGraphPinDirection::EGPD_Input);

		LeftNodeBox->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(PrimaryInputHeight)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.Padding(Settings->GetInputPinPadding())
				[
					PinToAdd
				]
			]
		];

		if (WriterNode->GetNumOutputs() > 0)
		{
			LeftNodeBox->AddSlot()
			.AutoHeight()
			[
				SNew(SSeparator)
				.Orientation(EOrientation::Orient_Horizontal)
				.Thickness(1.0f)
				.SeparatorImage(FAppStyle::GetBrush("WhiteBrush"))
			];
		}
		
		InputPins.Add(PinToAdd);
	}
	else
	{
		SGraphNodeK2Base::AddPin(PinToAdd);
	}
}

void SGraphNodeK2_PropertyIOWriter::CreateOutputSideAddButton(TSharedPtr<SVerticalBox> OutputBox)
{
	const TSharedRef<SWidget> AddPinButton = AddPinButtonContent(
										LOCTEXT("IOWriterNodeAddPinButton", "Add Output"),
										LOCTEXT("IOWriterNodeAddPinButton_Tooltip", "Adds an output reference to the node"));

	FMargin AddPinPadding = Settings->GetOutputPinPadding();
	AddPinPadding.Top += 6.0f;

	OutputBox->AddSlot()
	.AutoHeight()
	.VAlign(VAlign_Center)
	.HAlign(HAlign_Right)
	.Padding(AddPinPadding)
	[
		AddPinButton
	];
}

FReply SGraphNodeK2_PropertyIOWriter::OnAddPin()
{
	// From SGraphNodeK2Sequence, which is the default used for IK2Node_AddPinInterface.
	
	IK2Node_AddPinInterface* AddPinNode = Cast<IK2Node_AddPinInterface>(GraphNode);
	ensure(AddPinNode);
	if (AddPinNode && AddPinNode->CanAddPin())
	{
		FScopedTransaction Transaction(NSLOCTEXT("SequencerNode", "AddPinTransaction", "Add Pin"));

		AddPinNode->AddInputPin();
		UpdateGraphNode();
		GraphNode->GetGraph()->NotifyNodeChanged(GraphNode);
	}
	
	return FReply::Handled();
}

EVisibility SGraphNodeK2_PropertyIOWriter::IsAddPinButtonVisible() const
{
	// From SGraphNodeK2Sequence, which is the default used for IK2Node_AddPinInterface.

	const IK2Node_AddPinInterface* AddPinNode = Cast<IK2Node_AddPinInterface>(GraphNode);
	ensure(AddPinNode);
	return ((AddPinNode && AddPinNode->CanAddPin()) ? EVisibility::Visible : EVisibility::Collapsed);
}

#undef LOCTEXT_NAMESPACE