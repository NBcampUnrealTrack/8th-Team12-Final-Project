// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Configuration/SMEditorSettings.h"
#include "SMBlueprintEditorUtils.h"
#include "Misc/SMAuthenticator.h"

namespace LD::EditorAuth
{
	enum LDAuthStatus
	{
		Never = 0,
		Succeeded = 1,
		Failed = 2,
	};

	/** Retrieve the last recorded authentication state. */
	inline LDAuthStatus GetUserMarketplaceAuthenticationStatus()
	{
		return static_cast<LDAuthStatus>(FSMBlueprintEditorUtils::GetEditorSettings()->UserMarketplaceAuthenticationState);
	}
	
	/** Notify the user if required plugins for authentication are disabled. */
	SMSYSTEMEDITOR_API bool CheckForRequiredPlugins();

	/**
	 * Perform an entitlement check and record the result to the user's editor settings.
	 * @return False if authentication was not performed due to a missing requirement. True if authentication ran.
	 */
	inline bool AuthenticateAndRecordResult(const FSimpleDelegate& InOnAuthenticatedDelegate,
		IPluginWardenModule::EUnauthorizedErrorHandling InUnauthorizedErrorHandling =
		IPluginWardenModule::EUnauthorizedErrorHandling::ShowMessageOpenStore,
		bool bForceAuthenticate = false)
	{
		FSMAuthenticator& Authenticator = FSMAuthenticator::Get();
		if ((!Authenticator.IsAuthenticated() || bForceAuthenticate) && !CheckForRequiredPlugins())
		{
			return false;
		}
		
#if LOGICDRIVER_IS_MARKETPLACE_BUILD == LOGICDRIVER_IS_AUTH_BUILD
		FSMBlueprintEditorUtils::GetMutableEditorSettings()->UserMarketplaceAuthenticationState = LDAuthStatus::Failed;
		FSMBlueprintEditorUtils::GetMutableEditorSettings()->SaveConfig();
#endif
		Authenticator.Authenticate(FSimpleDelegate::CreateLambda([InOnAuthenticatedDelegate]()
		{
#if LOGICDRIVER_IS_MARKETPLACE_BUILD == LOGICDRIVER_IS_AUTH_BUILD
			FSMBlueprintEditorUtils::GetMutableEditorSettings()->UserMarketplaceAuthenticationState = LDAuthStatus::Succeeded;
			FSMBlueprintEditorUtils::GetMutableEditorSettings()->SaveConfig();
#endif
			InOnAuthenticatedDelegate.ExecuteIfBound();
		}),
		InUnauthorizedErrorHandling, bForceAuthenticate);

		return true;
	}

	/**
	 * Perform an entitlement check and record the result to the user's editor settings.
	 * @return False if authentication was not performed due to a missing requirement. True if authentication ran.
	 */
	inline bool AuthenticateAndRecordResultSilently()
	{
		return AuthenticateAndRecordResult(FSimpleDelegate(), IPluginWardenModule::EUnauthorizedErrorHandling::Silent);
	}

	/**
	 * Perform an entitlement check and record the result to the user's editor settings, only if this hasn't been done before.
	 * @return False if initial authentication was not performed due to a missing requirement. True if authentication has ever run.
	 */
	inline bool AuthenticateAndRecordResultOnceSilently()
	{
		if (GetUserMarketplaceAuthenticationStatus() == LD::EditorAuth::Never)
		{
			return AuthenticateAndRecordResultSilently();
		}
		return true;
	}
}

