// Copyright Max Harris

#include "Factories/PCGUtilsFractureFactory.h"

PCG_DEFINE_TYPE_INFO(FPCGUtilsFractureFactoryDataTypeInfo, UPCGUtilsFractureFactoryData)

namespace PCGUtilsFractureFactories
{
	const TSet<FPCGDataTypeBaseId>& GetFractureFactoryTypes()
	{
		static const TSet<FPCGDataTypeBaseId> Types = {FPCGUtilsFractureFactoryDataTypeInfo::AsId()};
		return Types;
	}
}
