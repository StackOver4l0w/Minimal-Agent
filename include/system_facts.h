#pragma once

#include "types.h"
#include "protocol.h"

typedef struct {
    CHAR hostname[ID_HOSTNAME_SIZE];
    CHAR username[ID_USERNAME_SIZE];
    CHAR os_version[ID_OS_VERSION_SIZE];
} system_facts;

void collect_system_facts(system_facts *facts);
