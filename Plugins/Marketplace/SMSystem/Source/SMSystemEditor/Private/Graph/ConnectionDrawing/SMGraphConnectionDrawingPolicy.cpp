// Copyright Recursoft LLC. All Rights Reserved.

#include "SMGraphConnectionDrawingPolicy.h"

#include "Graph/Nodes/SlateNodes/SGraphNode_TransitionEdge.h"
#include "Graph/Nodes/SMGraphNode_StateMachineEntryNode.h"
#include "Graph/Nodes/SMGraphNode_StateNodeBase.h"
#include "Graph/Nodes/SMGraphNode_TransitionEdge.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Rendering/DrawElements.h"

FSMGraphConnectionDrawingPolicy::FSMGraphConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj)
	: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, ZoomFactor, InClippingRect, InDrawElements)
	, GraphObj(InGraphObj)
{
	
}

void FSMGraphConnectionDrawingPolicy::DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params)
{
	Params.AssociatedPin1 = OutputPin;
	Params.AssociatedPin2 = InputPin;
	Params.WireThickness = 1.5f;

	if (InputPin)
	{
		if (USMGraphNode_TransitionEdge* TransitionNode = Cast<USMGraphNode_TransitionEdge>(InputPin->GetOwningNode()))
		{
			TransitionNode->bIsHoveredByUser = HoveredPins.Num() > 0;
			
			if (USMTransitionInstance* Instance = Cast<USMTransitionInstance>(TransitionNode->GetNodeTemplate()))
			{
				if (Instance->IsIconHidden())
				{
					// Only track hover information if the icon is hidden.
					// For now that's the only time we care about when the node was last hovered.
					
					if (HoveredPins.Num() > 0)
					{
						TransitionNode->TimeSinceHover = 0.f;
						TransitionNode->LastHoverTimeStamp = FDateTime::UtcNow();
					}
					else
					{
						const FDateTime CurrentTimeStamp = FDateTime::UtcNow();
						TransitionNode->TimeSinceHover = (CurrentTimeStamp - TransitionNode->LastHoverTimeStamp).GetTotalSeconds();
					}

					TransitionNode->bIsHoveredByUser = TransitionNode->TimeSinceHover < 0.5; // Half second buffer to be considered hovered.
				}
			}
			
			Params.WireColor = TransitionNode->GetTransitionColor(HoveredPins.Contains(InputPin));
			Params.bDrawBubbles = TransitionNode->WasEvaluating();
			// Transition connects same states -- special case drawing.
			if (TransitionNode->GetFromState() == TransitionNode->GetToState())
			{
				Params.bUserFlag2 = true;
			}
			
			if (TransitionNode->ShouldRunParallel())
			{
				Params.bUserFlag1 = true;
			}
		}
	}

	const bool bDeemphasizeUnhoveredPins = HoveredPins.Num() > 0;
	if (bDeemphasizeUnhoveredPins)
	{
		ApplyHoverDeemphasis(OutputPin, InputPin, /*inout*/ Params.WireThickness, /*inout*/ Params.WireColor);
	}

	// Make the transition that is currently relinked, semi-transparent.
	if (InputPin != nullptr && OutputPin != nullptr)
	{
		for (const FRelinkConnection& Connection : RelinkConnections)
		{
			const FGraphPinHandle SourcePinHandle = Connection.SourcePin;
			const FGraphPinHandle TargetPinHandle = Connection.TargetPin;

			// Skip all transitions that don't start at the node our dragged and relink transition starts from
			if (OutputPin->GetOwningNode()->NodeGuid == SourcePinHandle.NodeGuid)
			{
				if (const USMGraphNode_TransitionEdge* TransitionNode = Cast<USMGraphNode_TransitionEdge>(InputPin->GetOwningNode()))
				{
					if (const UEdGraphPin* TransitionOutputPin = TransitionNode->GetOutputPin())
					{
						if (TargetPinHandle.NodeGuid == TransitionOutputPin->GetOwningNode()->NodeGuid)
						{
							Params.WireColor.A *= 0.2f;
						}
					}
				}
			}
		}
	}
}

void FSMGraphConnectionDrawingPolicy::Draw(TMap<TSharedRef<SWidget>, FArrangedWidget>& InPinGeometries, FArrangedChildren& ArrangedNodes)
{
	// Build an acceleration structure to quickly find geometry for the nodes
	NodeWidgetMap.Empty();
	for (int32 NodeIndex = 0; NodeIndex < ArrangedNodes.Num(); ++NodeIndex)
	{
		FArrangedWidget& CurWidget = ArrangedNodes[NodeIndex];
		TSharedRef<SGraphNode> ChildNode = StaticCastSharedRef<SGraphNode>(CurWidget.Widget);
		NodeWidgetMap.Add(ChildNode->GetNodeObj(), NodeIndex);
	}

	FConnectionDrawingPolicy::Draw(InPinGeometries, ArrangedNodes);
}

FVector2f FSMGraphConnectionDrawingPolicy::ComputeSplineTangent(const FVector2f& Start, const FVector2f& End) const
{
	const FVector2f Delta = End - Start;
	const FVector2f NormDelta = Delta.GetSafeNormal();

	return NormDelta;
}

void FSMGraphConnectionDrawingPolicy::DetermineLinkGeometry(FArrangedChildren& ArrangedNodes, TSharedRef<SWidget>& OutputPinWidget,
	UEdGraphPin* OutputPin, UEdGraphPin* InputPin, FArrangedWidget*& StartWidgetGeometry, FArrangedWidget*& EndWidgetGeometry)
{
	if (USMGraphNode_StateMachineEntryNode* EntryNode = Cast<USMGraphNode_StateMachineEntryNode>(OutputPin->GetOwningNode()))
	{
		StartWidgetGeometry = PinGeometries->Find(OutputPinWidget);

		USMGraphNode_StateNodeBase* State = CastChecked<USMGraphNode_StateNodeBase>(InputPin->GetOwningNode());
		if (const int32* StateIndex = NodeWidgetMap.Find(State))
		{
			/*
			 * This used to be FindChecked, but there's an edge case that crashes...
			 * Create parent node, open node, create a new state, while the state is still being named press back on the mouse so you go to the parent
			 * Hover mouse over parent. It tries to draw the graph in a popup, but because the state hasn't been committed it doesn't exist yet.
			 * Presumably fails with references too.
			 */
			EndWidgetGeometry = &ArrangedNodes[*StateIndex];
		}
	}
	else if (USMGraphNode_TransitionEdge* EdgeNode = Cast<USMGraphNode_TransitionEdge>(InputPin->GetOwningNode()))
	{
		USMGraphNode_StateNodeBase* Start = EdgeNode->GetFromState(true);
		USMGraphNode_StateNodeBase* End = EdgeNode->GetToState(true);
		if (Start != nullptr && End != nullptr)
		{
			int32* StartNodeIndex = NodeWidgetMap.Find(Start);
			int32* EndNodeIndex = NodeWidgetMap.Find(End);
			if (StartNodeIndex && EndNodeIndex)
			{
				StartWidgetGeometry = &ArrangedNodes[*StartNodeIndex];
				EndWidgetGeometry = &ArrangedNodes[*EndNodeIndex];
			}
		}
	}
	else
	{
		StartWidgetGeometry = PinGeometries->Find(OutputPinWidget);

		if (TSharedPtr<SGraphPin>* pTargetWidget = PinToPinWidgetMap.Find(InputPin))
		{
			TSharedRef<SGraphPin> InputWidget = (*pTargetWidget).ToSharedRef();
			EndWidgetGeometry = PinGeometries->Find(InputWidget);
		}
	}

	// Cancel out if the widgets are both still being constructed. Prevents a flicker drawing the connections initially.
	if (StartWidgetGeometry && EndWidgetGeometry && StartWidgetGeometry->Widget->NeedsPrepass() && EndWidgetGeometry->Widget->NeedsPrepass())
	{
		// If the widgets are off screen we should try rendering it since the connection could still be visible.
		if (ClippingRect.ContainsPoint(StartWidgetGeometry->Geometry.GetAbsolutePosition()) ||
			ClippingRect.ContainsPoint(EndWidgetGeometry->Geometry.GetAbsolutePosition()))
		{
			// At least one widget is on screen and at least one widget is being constructed. Cancel the geometry out so it won't be rendered.
			StartWidgetGeometry = EndWidgetGeometry = nullptr;
		}
	}
}

void FSMGraphConnectionDrawingPolicy::DrawPreviewConnector(const FGeometry& PinGeometry, const FVector2f& StartPoint, const FVector2f& EndPoint, UEdGraphPin* Pin)
{
	FConnectionParams Params;
	DetermineWiringStyle(Pin, nullptr, /*inout*/ Params);

	const FVector2f SeedPoint = EndPoint;
	const FVector2f AdjustedStartPoint = FGeometryHelper::FindClosestPointOnGeom(PinGeometry, SeedPoint);

	DrawSplineWithArrow(AdjustedStartPoint, EndPoint, Params);
}

void FSMGraphConnectionDrawingPolicy::DrawSplineWithArrow(const FVector2f& StartAnchorPoint, const FVector2f& EndAnchorPoint, const FConnectionParams& Params)
{
	if (Params.bUserFlag2)
	{
		// Looping back to self.
		Internal_DrawCurvedLineWithArrow(StartAnchorPoint, Params);
	}
	else
	{
		// Connecting different points.
		Internal_DrawLineWithArrow(StartAnchorPoint, EndAnchorPoint, Params);
	}
}

void FSMGraphConnectionDrawingPolicy::DrawSplineWithArrow(const FGeometry& StartGeom, const FGeometry& EndGeom, const FConnectionParams& Params)
{
	// Get a reasonable seed point (halfway between the boxes)
	const FVector2f StartCenter = FGeometryHelper::CenterOf(StartGeom);

	// This is a curve connecting itself.
	if (Params.bUserFlag2)
	{
		// Look for a point diagonally down and right. We want the bottom right corner chosen.
		FVector2f StartAnchorPoint = FGeometryHelper::FindClosestPointOnGeom(StartGeom, StartCenter * 100);
		StartAnchorPoint.X -= 20.f * ZoomFactor; // Move to the left so its not exactly on the corner overlapping connected transitions.
		DrawSplineWithArrow(StartAnchorPoint, StartAnchorPoint, Params);

		return;
	}
	
	const FVector2f EndCenter = FGeometryHelper::CenterOf(EndGeom);
	const FVector2f SeedPoint = (StartCenter + EndCenter) * 0.5f;

	// Find the (approximate) closest points between the two boxes
	const FVector2f StartAnchorPoint = FGeometryHelper::FindClosestPointOnGeom(StartGeom, SeedPoint);
	const FVector2f EndAnchorPoint = FGeometryHelper::FindClosestPointOnGeom(EndGeom, SeedPoint);

	DrawSplineWithArrow(StartAnchorPoint, EndAnchorPoint, Params);
}

void FSMGraphConnectionDrawingPolicy::Internal_DrawLineWithArrow(const FVector2f& StartAnchorPoint, const FVector2f& EndAnchorPoint, const FConnectionParams& Params)
{
	const float LineSeparationAmount = 4.5f;

	const FVector2f DeltaPos = EndAnchorPoint - StartAnchorPoint;
	const FVector2f UnitDelta = DeltaPos.GetSafeNormal();
	const FVector2f Normal = FVector2f(DeltaPos.Y, -DeltaPos.X).GetSafeNormal();

	// Come up with the final start/end points
	const FVector2f DirectionBias = Normal * LineSeparationAmount;
	const FVector2f LengthBias = ArrowRadius.X * UnitDelta;
	const FVector2f StartPoint = StartAnchorPoint + DirectionBias + LengthBias;
	const FVector2f EndPoint = EndAnchorPoint + DirectionBias - LengthBias;

	const bool bIsParallel = Params.bUserFlag1;
	if (bIsParallel)
	{
		DrawConnection(WireLayerID, StartPoint - Normal * 2.5f, EndPoint - Normal * 2.5f, Params);
		DrawConnection(WireLayerID, StartPoint + Normal * 2.5f, EndPoint + Normal * 2.5f, Params);
	}
	else
	{
		DrawConnection(WireLayerID, StartPoint, EndPoint, Params);
	}

	FLinearColor ArrowHeadColor = Params.WireColor;
	
	// Draw the transition grab handles in case the mouse is hovering the transition.
	{
		const FVector FVecMousePos = FVector(AbsoluteMousePosition.X, AbsoluteMousePosition.Y, 0.0f);
		const FVector ClosestPoint = FMath::ClosestPointOnSegment(FVecMousePos,
			FVector(StartPoint.X, StartPoint.Y, 0.0f),
			FVector(EndPoint.X, EndPoint.Y, 0.0f));

		if ((ClosestPoint - FVecMousePos).Length() < RelinkHandleHoverRadius)
		{
			const bool bEndHovered = FVector2f(EndPoint - AbsoluteMousePosition).Length() < RelinkHandleHoverRadius;

			// Set the hovered pin results. This will be used by the SGraphPanel again.
			const float SquaredDistToPin1 = (Params.AssociatedPin1 != nullptr) ? (StartPoint - AbsoluteMousePosition).SizeSquared() : FLT_MAX;
			const float SquaredDistToPin2 = (Params.AssociatedPin2 != nullptr) ? (EndPoint - AbsoluteMousePosition).SizeSquared() : FLT_MAX;
			if (bEndHovered)
			{
				SplineOverlapResult = FGraphSplineOverlapResult(Params.AssociatedPin1, Params.AssociatedPin2, SquaredDistToPin2, SquaredDistToPin1, SquaredDistToPin2, true);
			}

			// Draw grab handles only in case no relinking operation is performed
			if (RelinkConnections.IsEmpty() && !Params.bUserFlag2 && Params.AssociatedPin2 != nullptr)
			{
				if (bEndHovered)
				{
					// Draw solid orange circle behind the arrow head in case the arrow head is hovered (area that enables a relink).
					const FSlateRoundedBoxBrush RoundedBoxBrush = FSlateRoundedBoxBrush(
						FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), 9.0f, FStyleColors::AccentOrange, 100.0f);

					FSlateDrawElement::MakeBox(DrawElementsList,
						ArrowLayerID - 1, // Draw behind the arrow
						FPaintGeometry(EndPoint - ArrowRadius, BubbleImage->ImageSize * ZoomFactor, ZoomFactor),
						&RoundedBoxBrush);

					ArrowHeadColor = FLinearColor::Black;
				}
				else
				{
					// Draw circle around the arrow in case the transition is hovered (mouse close or over transition line or arrow head).
					constexpr int32 CircleLineSegments = 16;
					constexpr float CircleRadius = 10.0f;
					const FVector2f CircleCenter = EndPoint - UnitDelta * 2.0f;
					Internal_DrawCircle(CircleCenter, CircleRadius, Params.WireColor, CircleLineSegments);
				}
			}
		}
	}

	Internal_DrawArrow(EndPoint, DeltaPos, ArrowHeadColor);
}

void FSMGraphConnectionDrawingPolicy::Internal_DrawCurvedLineWithArrow(const FVector2f& StartAnchorPoint, const FConnectionParams& Params)
{
	const float MaxX = 45.f * ZoomFactor;
	const float MaxY = 45.f * ZoomFactor;

	const FVector P1(StartAnchorPoint.X, StartAnchorPoint.Y, 0.f);
	const FVector P2(StartAnchorPoint.X + MaxX, StartAnchorPoint.Y + MaxY, 0.f);
	const FVector P3(StartAnchorPoint.X - MaxX, StartAnchorPoint.Y + MaxY, 0.f);
	const FVector P4(StartAnchorPoint.X - 12.f * ZoomFactor, StartAnchorPoint.Y, 0.f);

	FVector ControlPoints[]{ P1, P2, P3, P4 };

	constexpr int32 NumPoints = 12;

	TArray<FVector> CurvePoints;
	FVector::EvaluateBezier(ControlPoints, NumPoints, CurvePoints);

	for (int32 Idx = 1; Idx < NumPoints; ++Idx)
	{
		const FVector2f Start(CurvePoints[Idx - 1].X, CurvePoints[Idx - 1].Y);
		const FVector2f End(CurvePoints[Idx].X, CurvePoints[Idx].Y);

		DrawConnection(WireLayerID, Start, End, Params);
	}

	const FVector DeltaPos = CurvePoints[NumPoints - 1] - CurvePoints[NumPoints - 2];
	const FVector2f End(CurvePoints[NumPoints - 1].X, CurvePoints[NumPoints - 1].Y);

	Internal_DrawArrow(End, FVector2f(DeltaPos.X, DeltaPos.Y), Params.WireColor);
}

void FSMGraphConnectionDrawingPolicy::Internal_DrawArrow(const FVector2f Pos, const FVector2f &DeltaPos, const FLinearColor& InArrowheadColor)
{
	const FVector2f ArrowDrawPos = Pos - ArrowRadius;
	const float AngleInRadians = FMath::Atan2(DeltaPos.Y, DeltaPos.X);

	FSlateDrawElement::MakeRotatedBox(
		DrawElementsList,
		ArrowLayerID,
		FPaintGeometry(ArrowDrawPos, ArrowImage->ImageSize * ZoomFactor, ZoomFactor),
		ArrowImage,
		ESlateDrawEffect::None,
		AngleInRadians,
		TOptional<FVector2f>(),
		FSlateDrawElement::RelativeToElement,
		InArrowheadColor
	);
}

void FSMGraphConnectionDrawingPolicy::Internal_DrawCircle(const FVector2f& Center, float Radius, const FLinearColor& Color, const int32 NumLineSegments)
{
	TempPoints.Empty();

	const float NumFloatLineSegments = static_cast<float>(NumLineSegments);
	for (int32 Idx = 0; Idx <= NumLineSegments; ++Idx)
	{
		const float Angle = (Idx / NumFloatLineSegments) * TWO_PI;

		FVector2f PointOnCircle;
		PointOnCircle.X = cosf(Angle) * Radius;
		PointOnCircle.Y = sinf(Angle) * Radius;
		TempPoints.Add(PointOnCircle);
	}

	FSlateDrawElement::MakeLines(
		DrawElementsList,
		ArrowLayerID + 1,
		FPaintGeometry(Center, FVector2f(Radius, Radius) * ZoomFactor, ZoomFactor),
		TempPoints,
		ESlateDrawEffect::None,
		Color
	);
}
