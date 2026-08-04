// Copyright Recursoft LLC. All Rights Reserved.

#include "SMEditorAuthentication.h"

#include "SMSystemEditorLog.h"

#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "SMEditorAuthentication"

namespace LD::EditorAuth
{
	struct FRequiredPluginMessageNotification
	{
		TWeakPtr<SNotificationItem> Notification;

		void OpenPluginsWindow()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FName("PluginsEditor"));
			DismissNotification();
		};
		
		void DismissNotification()
		{
			if (Notification.IsValid())
			{
				Notification.Pin()->SetCompletionState(SNotificationItem::CS_None);
				Notification.Pin()->SetExpireDuration(0.0f);
				Notification.Pin()->SetFadeOutDuration(0.0f);
				Notification.Pin()->ExpireAndFadeout();
			}

			Notification.Reset();
		}
	};
}

bool LD::EditorAuth::CheckForRequiredPlugins()
{
	const FString PluginName = TEXT("UdpMessaging");
	IPluginManager& PluginManager = IPluginManager::Get();

	const TSharedPtr<IPlugin> Plugin = PluginManager.FindPlugin(PluginName);
	if (!Plugin.IsValid() || !Plugin->IsEnabled())
	{
		LDEDITOR_LOG_ERROR(TEXT("The UDP Messaging plugin is disabled. This plugin is required for validating the Logic Driver Pro Marketplace license. Please enable it by navigating to the Plugins window."))
		if (FApp::CanEverRender())
		{
			static FRequiredPluginMessageNotification MessageNotification;
			// Dismiss in case this is stacking.
			MessageNotification.DismissNotification();

			const FText Message = LOCTEXT("UDPPluginDisabled", "The UDP Messaging plugin is disabled. This plugin is required for validating the Logic Driver Pro Marketplace license. Please enable it by navigating to the Plugins window.");
			FNotificationInfo Info(Message);
			Info.bFireAndForget = false;
			Info.ExpireDuration = 0.0f;
			Info.bUseThrobber = false;
			Info.bUseSuccessFailIcons = false;
			Info.Image = FCoreStyle::Get().GetBrush(TEXT("MessageLog.Error"));
			
			Info.ButtonDetails.Add(FNotificationButtonInfo(
				LOCTEXT("ManagePluginsLabel", "Manage Plugins..."),
				LOCTEXT("ManagePluginsTooltip", "Click to open the Plugins window"),
				FSimpleDelegate::CreateRaw(&MessageNotification, &FRequiredPluginMessageNotification::OpenPluginsWindow)));
		
			Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("UDPPluginDisabledPopupDismissLabel", "Dismiss"),
				LOCTEXT("UDPPluginDisabledPopupDismissTooltip", "Dismiss this notification"),
				FSimpleDelegate::CreateRaw(&MessageNotification, &FRequiredPluginMessageNotification::DismissNotification)));

			MessageNotification.Notification = FSlateNotificationManager::Get().AddNotification(MoveTemp(Info));
			if (MessageNotification.Notification.IsValid())
			{
				MessageNotification.Notification.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
			}
		}

		return false;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE