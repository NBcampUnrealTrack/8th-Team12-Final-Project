// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "BlueprintEditorModes.h"

class FSMStateMachineBlueprintEditor;

struct FSMViewSummonerBase : public FWorkflowTabFactory
{
	FSMViewSummonerBase(FName InIdentifier, TSharedPtr<FSMStateMachineBlueprintEditor> InHostingApp, TSharedPtr<SWidget> TabWidgetIn);
	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;
protected:
	TWeakPtr<FSMStateMachineBlueprintEditor> BlueprintEditor;
	TWeakPtr<SWidget> TabWidget;
};

struct FSMPreviewDefaultsViewSummoner : public FSMViewSummonerBase
{
	FSMPreviewDefaultsViewSummoner(TSharedPtr<FSMStateMachineBlueprintEditor> InHostingApp, TSharedPtr<SWidget> TabWidgetIn);
};

struct FSMPreviewViewportViewSummoner : public FSMViewSummonerBase
{
	FSMPreviewViewportViewSummoner(TSharedPtr<FSMStateMachineBlueprintEditor> InHostingApp, TSharedPtr<SWidget> TabWidgetIn);
};

struct FSMPreviewAdvancedDetailsViewSummoner : public FSMViewSummonerBase
{
	FSMPreviewAdvancedDetailsViewSummoner(TSharedPtr<FSMStateMachineBlueprintEditor> InHostingApp, TSharedPtr<SWidget> TabWidgetIn);
};