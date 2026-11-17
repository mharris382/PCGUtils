// Copyright Max Harris
// Cluster wire format mirrors PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#include "Data/PCGUtilsClusterInterop.h"

namespace PCGUtilsClusterInterop
{
	// PCGExCommon::PCGExPrefix is "PCGEx/". Spelled out rather than composed so a reader can grep for the
	// literal string that ends up in the graph.
	const FName VtxDataAttribute = TEXT("PCGEx/VData");
	const FName EdgeDataAttribute = TEXT("PCGEx/EData");
	const FString VtxTag = TEXT("PCGEx/Vtx");
	const FString EdgesTag = TEXT("PCGEx/Edges");
	const FString ClusterPairTagKey = TEXT("PCGEx/Cluster");

	FString MakeClusterPairTag(int64 InPairId)
	{
		// PCGEx flattens a value tag as "Key:Value" - see TDataValue<T>::Flatten.
		return FString::Printf(TEXT("%s:%lld"), *ClusterPairTagKey, InPairId);
	}
}
