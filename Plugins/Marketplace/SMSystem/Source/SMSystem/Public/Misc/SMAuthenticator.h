// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Misc/Build.h"

#if WITH_EDITOR

#include "IPluginWardenModule.h"

/** Authentication is required for this build type. */
#define LOGICDRIVER_IS_AUTH_BUILD 2

/**
 * An authenticator to validate Marketplace licenses using official Epic Games entitlement checks.
 * While this can be circumvented, the purpose is mainly to inform studios of per-seat violations of
 * code plugins purchased under the Epic Games content license.
 * 
 * For licensing information, please see https://logicdriver.com/licensing/
 */
class FSMAuthenticator final
{
public:
	FSMAuthenticator(FSMAuthenticator const&) = delete;
	void operator=(FSMAuthenticator const&) = delete;
private:
	FSMAuthenticator() {}

public:
	/**
	 * Singleton access to authenticator.
	 */
	SMSYSTEM_API static FSMAuthenticator& Get();

	/**
	 * Perform an entitlement check for the plugin against the user's Epic Games account.
	 *
	 * @param InOnAuthenticatedDelegate The delegate to fire when authentication has succeeded.
	 * @param InUnauthorizedErrorHandling The error handling when authentication fails.
	 * @param bForceAuthenticate If true authentication will run again, even if it already succeeded.
	 */
	FORCEINLINE void Authenticate(const FSimpleDelegate& InOnAuthenticatedDelegate,
		IPluginWardenModule::EUnauthorizedErrorHandling InUnauthorizedErrorHandling =
		IPluginWardenModule::EUnauthorizedErrorHandling::ShowMessageOpenStore,
		bool bForceAuthenticate = false)
	{
		if (IsAuthenticated() && !bForceAuthenticate)
		{
			InOnAuthenticatedDelegate.ExecuteIfBound();
			return;
		}

		bIsAuthenticated = false;

		const FString CatalogItem = TEXT("819543009be949c5b2d40236adcb8166");
		const FString PluginGuid = TEXT("9d8db9962594400988f8ddd3fb83cd88");

		IPluginWardenModule::Get().CheckEntitlementForPlugin(
			NSLOCTEXT("FSMAuth", "LogicDriverPluginName", "Logic Driver Pro"), CatalogItem, PluginGuid,
		NSLOCTEXT("FSMAuth", "UnauthorizedUse", "You are not authorized to use Logic Driver Pro. Marketplace plugin licenses are per-seat.\nWould you like to view the store page?"),
		InUnauthorizedErrorHandling, [&] ()
		{
			bIsAuthenticated = true;
			InOnAuthenticatedDelegate.ExecuteIfBound();
		});
	}

	/**
	 * Check if the current user is considered authenticated.
	 * 
	 * @param bRequireAuthenticationRun Requires authentication has run, rather than rely on the default state.
	 * Useful for non-marketplace builds that may be on a Marketplace license.
	 * 
	 * @return True if the user is considered authenticated.
	 */
	FORCEINLINE bool IsAuthenticated(bool bRequireAuthenticationRun = false) const
	{
		return bIsAuthenticated.Get(GetDefaultAuthenticationValueForBuild())
		&& (!bRequireAuthenticationRun || HasAuthenticationRun());
	}

	/** Return true if authentication has run and determined the authentication state. */
	FORCEINLINE bool HasAuthenticationRun() const
	{
		return bIsAuthenticated.IsSet();
	}
	
	/**
	 * Reset the authentication flag.
	 */
	FORCEINLINE void ResetAuthentication()
	{
		bIsAuthenticated.Reset();
	}

private:
	bool GetDefaultAuthenticationValueForBuild() const
	{
#if LOGICDRIVER_IS_MARKETPLACE_BUILD == LOGICDRIVER_IS_AUTH_BUILD && (PLATFORM_WINDOWS || PLATFORM_MAC)
		return false;
#else
		return true;
#endif
	}
	
private:
	TOptional<bool> bIsAuthenticated;
};

#endif