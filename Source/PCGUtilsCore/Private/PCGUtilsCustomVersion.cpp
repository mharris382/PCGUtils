// Copyright Max Harris

#include "PCGUtilsCustomVersion.h"

#include "Serialization/CustomVersion.h"

const FGuid FPCGUtilsCustomVersion::GUID(0x76D6A7BE, 0xA9024782, 0xB8FF70A5, 0xF3FE4C3D);

static FCustomVersionRegistration GRegisterPCGUtilsCustomVersion(
	FPCGUtilsCustomVersion::GUID, FPCGUtilsCustomVersion::LatestVersion, TEXT("PCGUtilsVer"));
