// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"

/**
 * Serialization version for PCGUtils settings objects. Settings bases call Ar.UsingCustomVersion(GUID) in
 * Serialize, and PostLoad compares GetLinkerCustomVersion(GUID) against an entry here to decide whether a
 * one-off data migration still has to run. Append entries; never reorder or remove them.
 */
struct PCGUTILSCORE_API FPCGUtilsCustomVersion
{
	enum Type : int32
	{
		/** Assets saved before PCGUtils recorded a version. */
		BeforeCustomVersionWasAdded = 0,

		/** Constant settings became Constant-or-Attribute input shorthands (Extrude Faces' Distance first). */
		InputShorthands,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	static const FGuid GUID;

private:
	FPCGUtilsCustomVersion() = delete;
};
