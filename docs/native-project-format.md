# Native DatumLoop project format (`.dld`)

The native application saves projects as UTF-8 JSON in a `.dld` file. Schema
version 2 is provisional and stores the document state supported by the current
native core:

- project UUID and name;
- object UUID, name, optional parent UUID, and typed scalar properties;
- object translation in millimetres;
- exact OCCT B-Rep geometry, encoded as Base64 with its OCCT stream version;
- persistent face, edge, and vertex references with owner identity, optional
  feature identity, geometric/topological signatures, and parent references.

Example structure (the geometry payload is abbreviated):

```json
{
  "format": "datumloop-project",
  "schemaVersion": 2,
  "project": { "id": "…", "name": "Assembly" },
  "objects": [{
    "id": "…",
    "name": "Base",
    "parentId": null,
    "properties": {
      "material": { "type": "string", "value": "6061-T6" },
      "reference": { "type": "bool", "value": true }
    },
    "featureDefinitions": [],
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
    "featureId": null,
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

Property types in version 2 are `bool`, signed `int64`, finite `double`, and
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
topology-reference array. The optional feature UUID is stored but cannot be
resolved until the native feature graph exists.

`DldProjectStore` writes OCCT's current ASCII B-Rep stream with triangulation
and normals disabled. The display mesh is never saved. Loading validates the
entire file into a temporary document, recomputes shape metrics, then the UI
replaces its document and clears the old command history. A failed load leaves
the open document untouched. Saving uses `QSaveFile`; the document becomes
clean only after the atomic file commit succeeds.

Schema version 2 migrates version 1 projects by capturing initial face, edge,
and vertex references from each exact B-Rep object. On load, an object missing
object-level topology references receives an initial set. Unknown schema
versions are rejected. Version 2 still
does **not** implement parametric feature definitions, object-to-object
references, rotational transforms, assemblies, engineering entities, simulation
state, or a general migration framework. The per-object `featureDefinitions`
and `references` arrays are written empty; non-empty entries are rejected
rather than silently losing data. The topology matcher is an initial strategy,
not a guarantee of persistent naming through arbitrary feature edits; changed
or ambiguous topology remains unresolved until a later repair workflow selects
the intended entity. Geometry is embedded as Base64, which keeps this version
atomic but increases file size; a future version may move large geometry
payloads into assets.

The tests cover exact-shape round-trip, UUID identity, hierarchy, translation,
all four property types, dirty tracking, schema-v1 migration, face/edge/vertex
reference resolution and point picking, ambiguity rejection, parent
relationships, display tessellation regeneration, and rejection of unsupported
or malformed data.
