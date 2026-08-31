// Copyright Max Harris
// Cluster wire format mirrors PCGExtendedToolkit, Copyright 2026 Timothe Lapetite and contributors (MIT).

#pragma once

#include "CoreMinimal.h"

/**
 * The convention PCGEx uses to recognise a pair of point datas as a cluster.
 *
 * Reproduced here rather than depended upon: PCGUtilsFracture must not link against PCGExtendedToolkit, and
 * the whole contract is two int64 attributes plus three tags. Emitting it means every PCGEx cluster node -
 * flood fill and its heuristics library, pathfinding, refinement, connectivity filters - works on a fractured
 * Geometry Collection with no bridge node and no dependency in either direction.
 *
 * A cluster half is ordinary point data on an ordinary Point pin. PCGEx declares its cluster pins - inputs and
 * outputs alike - with PCGEX_PIN_POINTS, i.e. FPCGDataTypeInfoPoint, and that is what keeps a cluster usable
 * by every other point node in the graph. Do not introduce a data subtype for it: that narrows the pin and
 * buys nothing, because what makes the data a cluster is the tags and attributes below, not its class.
 *
 * Verified against PCGExClusterCommon.h (labels), PCGExH.h (packing), PCGExGraphHelpers.cpp (the readers),
 * PCGExDataValue.cpp (value-tag flattening) and PCGExClustersProcessor.cpp (pin declarations) in
 * PCGExtendedToolkit as vendored in this project.
 */
namespace PCGUtilsClusterInterop
{
	/** PCGEx/VData - int64 on each vtx point. */
	PCGUTILSFRACTURE_API extern const FName VtxDataAttribute;

	/** PCGEx/EData - int64 on each edge point. */
	PCGUTILSFRACTURE_API extern const FName EdgeDataAttribute;

	/** Raw tag marking a point data as the vtx half. */
	PCGUTILSFRACTURE_API extern const FString VtxTag;

	/** Raw tag marking a point data as the edges half. */
	PCGUTILSFRACTURE_API extern const FString EdgesTag;

	/** Key of the value tag pairing the two halves; flattens to "PCGEx/Cluster:<id>". */
	PCGUTILSFRACTURE_API extern const FString ClusterPairTagKey;

	/**
	 * PCGEx packs two 32-bit values into one int64 as (A << 32) | B - PCGEx::H64. Reproduced rather than
	 * included so this module stays dependency-free.
	 */
	FORCEINLINE int64 PackPair(uint32 A, uint32 B)
	{
		return static_cast<int64>((static_cast<uint64>(A) << 32) | static_cast<uint64>(B));
	}

	/**
	 * Value written to PCGEx/VData.
	 *
	 * InVtxId is NOT required to be the point's index: PCGEx's reader (BuildEndpointsLookup) builds a
	 * VtxId -> point-index map from it, so any unique id works. We pass the Geometry Collection bone index,
	 * which makes the PCGEx vertex id and GC_BoneIndex literally the same number - so a selection round-tripped
	 * through PCGEx cluster nodes still resolves through Select Bones From Points unchanged.
	 */
	FORCEINLINE int64 MakeVtxData(int32 InVtxId, int32 InDegree)
	{
		return PackPair(static_cast<uint32>(InVtxId), static_cast<uint32>(InDegree));
	}

	/** Value written to PCGEx/EData: the two vtx ids this edge connects. */
	FORCEINLINE int64 MakeEdgeData(int32 InStartVtxId, int32 InEndVtxId)
	{
		return PackPair(static_cast<uint32>(InStartVtxId), static_cast<uint32>(InEndVtxId));
	}

	/** Builds the "PCGEx/Cluster:<id>" value tag that pairs a vtx data with its edges data. */
	PCGUTILSFRACTURE_API FString MakeClusterPairTag(int64 InPairId);
}
