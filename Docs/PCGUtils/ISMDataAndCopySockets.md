# Get ISM Data and Utils | Copy Sockets

Two nodes that chain: read placed instances back into PCG, then turn each instance into points at its mesh's
sockets.

```
Get ISM Data (Mesh per point) -> Utils | Copy Sockets (Mesh Attribute = Mesh) -> spawn at the sockets
```

## Attribute names pick their domain

Output names on these nodes follow PCG selector syntax. A plain name (`Mesh`) is written on every point; a name
prefixed with `@Data.` (`@Data.Mesh`) is written once for the data set. There is no separate domain setting:
rename the attribute to move it. A name that selects a property (`$Position`) or has an accessor (`Mesh.X`) is
an error naming the setting. The shared helper is `PCGUtilsAttributeTargets` (`PCGUtilsAttributeTargets.h`).

## Get ISM Data

A Get Actor Data node for instanced static mesh components (ISM and HISM). The actor selector and component
selector work as on Get Actor Data: "ISM components tagged X" is the component selector's tag filter.

Each component becomes one point data set, tagged with the component's and actor's tags. Each instance is one
point at its world transform, seeded from its position. Components with no instances produce nothing.

| Setting | Default | Meaning |
| --- | --- | --- |
| Skip PCG Debug Components | on | Skip components tagged `PCG Generated Debug Component`. |
| Ignore PCG Generated Components (inherited) | **off** | Off here so ISMs that PCG spawned can be read back. Turn on to skip everything PCG generated. |
| Mesh Attribute Name | on, `Mesh` | Soft Object Path to the component's static mesh. Per point by default. |
| Material Attribute Name | off, `@Data.Material` | Soft Object Path to the material the component renders in Material Slot (override, else the mesh's). Once per data set by default. |
| Material Slot | 0 | Slot read for the material. Out of range writes an empty path and warns. |
| Bounds From Mesh | on | Each point gets the mesh's local bounding box; off keeps unit bounds. |

PCGUtils also has the older **Get Static Mesh Data**, whose Instanced mode reads the same instances into legacy
point data, writes its attributes per point only, and has the extra visibility and collision filters.

## Utils | Copy Sockets

One input pin. Each input point is treated as an instance of the static mesh in its Mesh Attribute, and is
replaced by one point per selected socket, at `SocketLocal * PointTransform`. Like Copy Points, with the mesh's
sockets as the source points.

- **Metadata:** every output point keeps its source point's attributes and native properties (density, color,
  steepness). `@Data`, tags and the target actor carry over. Seeds are recombined per socket. Bounds are reset to
  unit bounds, since a socket is a location rather than the mesh.
- **Order:** copies of one point are contiguous, in Socket Names order (Socket Names mode) or the mesh's socket order.
- **Dropped points:** a point whose mesh is empty, fails to load, is not a Static Mesh, or has no selected
  sockets produces no points. Each case is reported once per mesh, with the input index.

| Setting | Default | Meaning |
| --- | --- | --- |
| Mesh Attribute | `Mesh` | Required. Soft Object Path or string. `@Data.Mesh` uses one mesh for the whole data set. |
| Socket Selection | Socket Names | **Socket Names**: the listed sockets (missing names warn). **Socket Tag**: sockets whose Tag equals Socket Tag. **All Sockets**: every socket. Only the matching setting is shown. |
| Socket Name Attribute Name | on, `SocketName` | Per point Name of the socket. |
| Socket Tag Attribute Name | off, `SocketTag` | Per point String: the socket's Tag. |
| Synchronous Load (advanced) | off | Load meshes on the game thread instead of asynchronously. |

The two output names must be point attributes (they differ per point), so an `@Data.` prefix is an error.

The node subtitle shows `AllSockets`, `SocketTag: {tag}`, the socket name when exactly one is listed, or
`Multiple Sockets`.

Meshes are loaded in PrepareData through PCG's async loading. Execution stays on the game thread because it
resolves meshes and reads their sockets. The per-point work (placing copies, writing socket attributes) runs in
`ParallelFor`.

Tests: `PCGUtils.Points.CopySockets.*`, `PCGUtils.Points.GetISMData.InstancePoints`.
