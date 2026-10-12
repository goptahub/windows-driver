#pragma once
#include "regfltr.h"


NTSTATUS ParseConfigXmlFromRegistry(
    HANDLE KeyHandle,
    PRULES_DATA rules
);

