// Copyright Recursoft LLC. All Rights Reserved.

#include "Misc/SMAuthenticator.h"

#if WITH_EDITOR

FSMAuthenticator& FSMAuthenticator::Get()
{
	static FSMAuthenticator Authenticator;
	return Authenticator;
}

#endif