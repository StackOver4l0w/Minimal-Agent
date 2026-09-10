#pragma once

#include "types.h"

#if defined(ENVIRONMENT_I386) && defined(LOGGING_ENABLED)

VOID PIC_ApplyFixups(VOID);

#else

#define PIC_ApplyFixups() ((void)0)

#endif
