# GC | Dataflow Processor (experimental)

Evaluates a Dataflow asset as an editor-only processing step inside PCG. The Dataflow asset remains the place
where processing is authored; the PCG node only supplies named inputs, variable overrides, and retrieves GC
outputs. It does not invoke asset-writing terminals, modify the source asset, or preserve GC selection identity.

## First graph

1. Create a Dataflow asset. The plugin enables the engine's Geometry/Dataflow authoring plugin for editor targets.
2. Add **PCG Input GC**, **PCG Input Points**, and **PCG Output GC** from the **PCGUtils** Dataflow category.
   Their default pin names are `GC`, `Sites`, and `GC`. Input and output names are separate namespaces.
3. Connect the input Collection and point Positions to a Dataflow Voronoi Fracture node. Connect its Collection
   to PCG Output GC. Connect input Materials to output Materials, or provide the material array authored by the
   Dataflow graph. Collections carry material indices; the material array must accompany them explicitly.
4. In PCG, add **GC | Dataflow Processor**, assign the asset, and click **Refresh Interface**. This discovers
   bridge input names/types, GC output names, and exposed asset variables. Connect your GC and sites datasets.
5. Generate the PCG graph. Output collections can feed existing GC conversion, selection, and edit nodes.

Bridge inputs need a PCG execution context. Evaluating them directly in the Dataflow preview reports a missing
PCG binding. Use normal Dataflow nodes to prototype a graph, then connect the bridge inputs for PCG execution.

## Named pins and datasets

The **Inputs** array contains names and types: GC or Points. **Outputs** contains GC output names. These must
match the bridge nodes in the asset. Names must be nonempty and unique per direction; `Parameters` is reserved
on inputs. Refreshing the interface imports names from the Dataflow asset; renaming pins may require reconnecting
PCG wires. Lock names before using the experimental interface in production graphs.

Each required input must contain data. Let N be the largest connected input dataset count, including Parameters.
Every input must have N datasets or exactly one. Execution pairs datasets by index and broadcasts singletons.
`3 GC / 3 Points / 1 Parameters` and `1 GC / 1 Points / 3 Parameters` both execute three times; `3 / 2 / 1` fails.
Each point dataset becomes one array of positions/transforms; its point count does not determine N. A graph
with no geometry inputs executes once, or once per parameter dataset when Parameters is connected.

Points are converted from PCG world space to the target actor's collection-local space by default. Disable
**Points In Collection Space** when the incoming point transforms are already in the space expected by the
Dataflow graph. The bridge exposes both Positions and Transforms. It does not transfer point metadata.

## Inline parameters and optional metadata overrides

The inline parameter list comes from exposed **Dataflow asset variables**, not arbitrary node properties.
Use the normal Dataflow Get Variable node to read those variables inside the asset.

Each entry shows its name, type, value, an **Override** checkbox, and its PCG **Attribute Name** (initially the
variable name). Enable Override to use a local value. With Override disabled, execution reads the current asset
default, even if the serialized display value is older. Asset variable edits refresh loaded parameter lists;
Refresh Interface also synchronizes them, preserving local overrides and attribute mappings when name/type match.

Connect optional **Parameters** data to override none, some, or all entries. Each parameter dataset must contain
exactly one metadata row. Missing attributes retain the inline/default value; unrelated attributes are ignored.
Set an entry's Attribute Name to None to disable metadata overrides for that entry.

Precedence: **metadata attribute → enabled local override → current asset default**.

Supported inline types: bool, int32, int64, float, double, name, string. Integer metadata accepts int32/int64 with
range checking; real metadata accepts float/double. Names require name attributes, strings require strings,
and bools require bools. UE 5.8's Dataflow Double variable getter outputs float; overrides follow that engine
behavior and reject nonfinite/out-of-float-range values. Arrays, objects, enums, and other structs remain visible
as unsupported and use the asset default. Supplying an override for an unsupported entry fails explicitly.

## Execution and identity

- Editor-only evaluation runs synchronously on the game thread. The runtime settings/element remain loadable;
  execution outside the editor reports an error and emits no output. The asset reference is editor-only data.
- Every batch uses a fresh Dataflow context and private transient variable owner. PCG element caching is disabled.
  Dataflow node edits notify the loaded settings; the referenced asset is also registered with PCG asset tracking.
  Frozen Dataflow outputs are rejected because they bypass fresh context evaluation. Unfreeze them before running.
- Inputs are copied into Dataflow values. Every returned GC is validated and passed through PublishNewLineage,
  with new collection/state/bone identities and an empty piece-mesh cache. No incoming selection remains valid.
- Material arrays must be wired explicitly; arbitrary collection composition cannot infer which input's table
  describes the output. GC input tags are unioned with other geometry/point input tags for that batch.
- Named outputs are collected per batch, in order. Validation or Dataflow errors discard the entire result,
  including prior successful batches. Dataflow warnings are forwarded to PCG graph diagnostics.
- This preliminary bridge supports GC outputs, GC inputs, and point-array inputs. DynMesh, selection, point-data
  outputs, asset-writing terminals, and runtime Dataflow execution are outside its contract.

Automation: `PCGUtils.Fracture.Dataflow` exercises a real Voronoi graph, variable precedence, batching, identity,
immutable inputs, and rejection paths. `PCGUtils.Palette.SearchContract` covers the palette family convention.
