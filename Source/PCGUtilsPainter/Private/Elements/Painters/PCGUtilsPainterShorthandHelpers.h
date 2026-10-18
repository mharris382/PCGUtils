// Copyright Max Harris

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGUtilsInputShorthands.h"
#include "Serialization/ArchiveCrc32.h"

namespace PCGUtilsPainterShorthands
{
	/** Adds a shorthand's mode, constant and attribute selector to a Painter factory CRC. */
	template <typename TConstant>
	void AddToCrc(FArchiveCrc32& Ar, const FPCGUtilsInputShorthandBase& Shorthand, TConstant Constant)
	{
		uint8 Mode = static_cast<uint8>(Shorthand.Input);
		Ar << Mode;
		Ar << Constant;
		FString Selector = Shorthand.Attribute.ToString();
		Ar << Selector;
	}
}
