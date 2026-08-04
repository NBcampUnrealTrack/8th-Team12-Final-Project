// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

class FSMStateMachineBlueprintEditor;

class FSMBlueprintEditorToolbar : public TSharedFromThis<FSMBlueprintEditorToolbar> {
public:
	FSMBlueprintEditorToolbar(TSharedPtr<FSMStateMachineBlueprintEditor> InEditor)
		: Editor(InEditor) {
	}

	void AddModesToolbar(TSharedPtr<FExtender> Extender);
	void AddPreviewToolbar(TSharedPtr<FExtender> Extender);

protected:
	void FillModesToolbar(FToolBarBuilder& ToolbarBuilder);
	void FillPreviewToolbar(FToolBarBuilder& ToolbarBuilder);

private:
	TWeakPtr<FSMStateMachineBlueprintEditor> Editor;
};
