# Native DatumLoop project format (`.dld`)

The native application saves projects as UTF-8 JSON in a `.dld` file. Schema
version 3 is provisional and stores the document state supported by the current
native core:

- project UUID and name;
- object UUID, name, optional parent UUID, and typed scalar properties;
- object translation in millimetres;
- object-scoped feature definitions and selected output feature UUID;
- exact OCCT B-Rep geometry, encoded as Base64 with its OCCT stream version;
- persistent face, edge, and vertex references with owner identity, optional
  feature identity, geometric/topological signatures, and parent references.

Example structure (the geometry payload is abbreviated):

```json
{
  "format": "datumloop-project",
  "schemaVersion": 3,
  "project": { "id": "…", "name": "Assembly" },
  "objects": [{
    "id": "…",
    "name": "Base",
    "parentId": null,
    "properties": {
      "material": { "type": "string", "value": "6061-T6" },
      "reference": { "type": "bool", "value": true }
    },
    "featureDefinitions": [{
      "id": "…",
      "ownerObjectId": "…",
      "type": "primitive.box",
      "inputFeatureIds": [],
      "parameters": { "xMm": 40, "yMm": 30, "zMm": 20 },
      "kernelBackend": "occt"
    }],
    "outputFeatureId": "…",
    "references": [],
    "transform": { "translationMm": [0, 0, 0] },
    "geometry": {
      "format": "occt-brep-ascii-v1",
      "occtFormatVersion": 3,
      "encoding": "base64",
      "data": "…"
    }
  }],
  "topologyReferences": [{
    "id": "…",
    "objectId": "…",
    "featureId": "…",
    "subshapeType": "face",
    "topologySignature": "face|plane|…",
    "geometrySignature": {
      "geometryType": "plane",
      "measure": 1200,
      "centroidMm": [20, 0, 0],
      "direction": [1, 0, 0],
      "adjacent": []
    },
    "parentReferenceIds": [],
    "transientIndexHint": 0
  }]
}
```

Property types in version 3 are `bool`, signed `int64`, finite `double`, and
UTF-8 `string`. Signed 64-bit values are decimal strings in JSON so integers
above JavaScript's exact-number range remain lossless. Parent objects must
precede children in the object array; this both validates parent identity and
prevents hierarchy cycles in the current append-only document model.

The topology resolver captures faces, edges, and vertices from exact OCCT
shapes. Its initial deterministic matcher compares subshape kind and geometry
type, relative area or length (mm² for faces, mm for edges, zero for vertices),
centroid in millimetres, direction when available, and sorted adjacency
descriptors. It accepts only a unique best match under declared
tolerances. A tie or a poor match remains unresolved; the transient enumeration
index is diagnostic and never breaks a tie. Parent-reference IDs retain local
adjacency relationships (edges can name incident faces; vertices can name
incident edges). Parent references must appear before child references in the
topology-reference array. Feature-backed references are matched only against
their current output feature; the resolver does not yet migrate them across
arbitrary graph edits.

`DldProjectStore` writes OCCT's current ASCII B-Rep stream with triangulation
and normals disabled. The display mesh is never saved. Loading validates the
entire file into a temporary document, recomputes shape metrics, then the UI
replaces its document and clears the old command history. A failed load leaves
the open document untouched. Saving uses `QSaveFile`; the document becomes
clean only after the atomic file commit succeeds.

Schema version 3 reads versions 1 and 2. Version 1 files receive initial face,
edge, and vertex references; version 2 references and object identities are
preserved. Unknown schema versions are rejected. An object feature graph is an
append-ordered DAG with explicit feature UUIDs, owner, type, input feature UUIDs,
parameters in millimetres, and a geometry backend identifier. The first native
OCCT backend supports box, cylinder, sphere, and cone primitives, translation,
and Boolean union, cut, and intersection. Inputs must refer to earlier features
in the same object's graph, making recompute deterministic and cycle-free.

Each in-memory feature records recompute status, an error string, and its exact
generated B-Rep. The `.dld` stores definitions plus the object's final exact
B-Rep; individual intermediate B-Reps and display tessellation are not stored.
Loading recomputes the graph and checks the final shape's volume, surface area,
center of mass, and bounds against the persisted B-Rep before accepting it.
These metrics provide a consistency guard, not proof of exact geometric
identity. An
unsupported backend or failed node is never silently routed through OCCT;
failed recomputes retain their status/error and do not replace the last valid
object shape.

Feature graph replacement is an undoable document command. It creates a new
initial topology-reference set for that object's output; it does not yet
preserve subshape UUIDs through arbitrary feature changes. Object-to-object
references, sketch and manufacturing features, rotational transforms,
assemblies, engineering entities, simulation state, and a general migration
framework remain unsupported. Non-empty per-object `references` arrays are
rejected rather than silently discarded. Geometry is embedded as Base64, which
keeps the write atomic but increases file size; a future version may move large
geometry payloads into assets.

The tests cover exact-shape and feature-graph round-trip, primitive and Boolean
recompute, UUID identity, hierarchy, translation, all four property types, dirty
tracking, schema-v1/v2 migration, face/edge/vertex reference resolution and
point picking, ambiguity rejection, parent relationships, display tessellation
regeneration, and rejection of unsupported or malformed data.
