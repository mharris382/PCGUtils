# Param | Get MPC Values

Reads parameters of a Material Parameter Collection (MPC) into an attribute set with one entry, one attribute per
listed parameter. Use it to drive a graph from the same global values your materials read, such as wind,
wetness, season or a world tint, so the graph and the materials stay in step.

```
Get MPC Values (Wetness -> Wet) -> Attribute Noise / Density Filter / any node that reads @Data or a param
```

The node has no inputs. It lives in the `PCGUtils|Params` palette category.

## Settings

| Setting | Default | Meaning |
|---|---|---|
| Collection | none | Required. The Material Parameter Collection to read. Overridable. |
| Source | Defaults | **Defaults** reads the values saved in the asset. **World** reads what the world's instance of the collection holds now: whatever Blueprints or code have set with Set Scalar/Vector Parameter Value. In an editor world nothing usually sets them, so they match the defaults. |
| Parameters | empty | One entry per parameter to read (see below). |

**Add All Parameters** (a button under Settings) adds an entry for every collection parameter not already listed,
writing an attribute of the same name. Existing entries are left alone.

Each **Parameters** entry has:

| Field | Meaning |
|---|---|
| Parameter | Required. A scalar or vector parameter of the collection, picked from a dropdown. |
| Attribute Name | Required. The attribute written on the output. Filled in with the parameter's name when you pick a parameter and leave this empty. |
| Vector Output | For vector parameters: **Vector4** (default, X=R Y=G Z=B W=A), **Vector** (RGB), **Vector2** (RG), or one channel **R / G / B / A** as a double. Ignored for scalars, which always write a double. |

## Errors

Each check reports on its own and names the entry, so `Parameters[2]` means the third entry:

- Collection is not set, or the asset could not be loaded (the message gives its path).
- `Parameters[i]`: Parameter is None, or is not a scalar or vector parameter of the collection.
- `Parameters[i]`: Attribute Name is None, is not a valid PCG attribute name, or is already written by an earlier
  entry.
- Source is World, but the graph is not running in a world, or the world has no instance of the collection.

An empty Parameters list is a warning; the node still outputs an attribute set with no attributes.

## Caching and refresh

With **Defaults**, the result is cached like any other node. Editing the collection asset's defaults does not
trigger a regenerate: regenerate the graph (or touch the node) to pick up the new values.

With **World**, the node is never cached, so every generation reads the current values. A value changing at runtime
still does not regenerate the graph by itself; whatever changes the value should also trigger the generation that
needs it. For that reason Source is not overridable: caching is decided before overrides apply.

The node runs on the game thread, since it loads the asset and reads the world.

Coverage: `PCGUtils.Params.GetMPCValues` (defaults and every vector output, validation messages, live world values,
and the editor helpers).
