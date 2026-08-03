/* POLPhone - visibilidade da fronteira DLL do núcleo. GPL-2.0-only. */

#pragma once

#if defined(POLPHONE_CORE_SHARED_EXPORTS)
#define POLPHONE_CORE_API __declspec(dllexport)
#elif defined(POLPHONE_CORE_SHARED_IMPORTS)
#define POLPHONE_CORE_API __declspec(dllimport)
#else
#define POLPHONE_CORE_API
#endif
