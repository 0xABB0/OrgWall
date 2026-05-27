# Blender Modifier Stack & Geometry Nodes Study

Source: `~/repo/suck/blender/source/blender/modifiers/` and `source/blender/nodes/`

## Modifier Stack Architecture

### The vtable: ModifierTypeInfo

Every modifier type is a statically allocated `ModifierTypeInfo` -- hand-rolled vtable:

```cpp
struct ModifierTypeInfo {
  char idname[64], name[64], struct_name[64];
  int struct_size;
  ModifierTypeType type;  // OnlyDeform | Constructive | Nonconstructive
  ModifierTypeFlag flags; // AcceptsMesh | AcceptsCVs | SupportsEditmode

  void (*copy_data)(const ModifierData*, ModifierData*, int flag);
  void (*deform_verts)(ModifierData*, const ModifierEvalContext*, Mesh*, MutableSpan<float3>);
  Mesh *(*modify_mesh)(ModifierData*, const ModifierEvalContext*, Mesh*);
  void (*modify_geometry_set)(ModifierData*, const ModifierEvalContext*, GeometrySet*);
  void (*init_data)(ModifierData*);
  void (*free_data)(ModifierData*);
  bool (*is_disabled)(const Scene*, ModifierData*, bool use_render_params);
  void (*required_data_mask)(ModifierData*, CustomData_MeshMasks*);
  void (*update_depsgraph)(ModifierData*, const ModifierUpdateDepsgraphContext*);
  // ...
};
```

Concrete modifier data uses C-style inheritance (first field):
```c
struct SubsurfModifierData {
  ModifierData modifier;  // first field
  short subdivType;
  // ...
};
```

### How the stack evaluates

Two-pass, eager, top-to-bottom:

1. **Pass 1 -- leading deform modifiers**: iterates `md->next` calling `deform_verts()` while `mti->type == OnlyDeform`. These never copy the mesh -- just deform positions.
2. **Pass 2 -- constructive modifiers**: for each remaining modifier, calls `modify_mesh()` (returns new `Mesh*`) or `modify_geometry_set()` (mutates in place).

Simple linked list walk. No lazy evaluation, no caching between modifiers.

`required_data_mask` pre-computes which CustomData layers each modifier needs so layers aren't dropped early.

### Registration

Compile-time, array-based:
```c
void modifier_type_init(ModifierTypeInfo *types[]) {
  types[eModifierType_Subsurf] = &modifierType_Subsurf;
  types[eModifierType_Nodes]   = &modifierType_Nodes;
  // ...
}
```

No runtime extensibility.

## Geometry Nodes System

### Node registration

Each node type is a `bNodeType` struct:
```cpp
struct bNodeType {
  std::string idname;
  NodeDeclareFunction declare;
  NodeGeometryExecFunction geometry_node_execute;
  NodeMultiFunctionBuildFunction build_multi_function;
  void (*initfunc)(bNodeTree*, bNode*);
  // ...
};
```

Registration via `NOD_REGISTER_NODE(fn)` macro. Build-time Python script discovers all instances, generates a single .cc file that calls them all. No runtime registration.

### Socket declarations

Fluent DSL builder:
```cpp
static void node_declare(NodeDeclarationBuilder &b) {
  b.add_input<decl::Geometry>("Mesh");
  b.add_input<decl::Int>("Vertices X").default_value(2).min(1).supports_field();
  b.add_output<decl::Geometry>("Mesh");
}
```

### Evaluation: lazy-function graph

The `bNodeTree` is compiled into a `lf::Graph` (lazy-function graph) and cached.

```cpp
class LazyFunction {
  Vector<lf::Input> inputs_;
  Vector<lf::Output> outputs_;
  virtual void execute_impl(lf::Params &params, const lf::Context &context) const = 0;
};
```

Key: `Params` allows:
- `try_get_input_data_ptr_or_request(index)` -- request input; if not ready, re-executed later
- `get_output_data_ptr(index)` + `output_set(index)` -- write output
- `get_output_usage(index)` -- check if output is needed

This enables **demand-driven, short-circuit evaluation**. Switch node requests only condition first, then only the needed branch. Unused branches never computed.

Node groups are NOT inlined -- remain separate `lf::FunctionNode`s for cache locality.

Graph executor drives multi-threaded execution.

### How the Nodes modifier calls it

`MOD_nodes.cc::modifyGeometry` builds `GeoNodesCallData`, calls `execute_geometry_nodes_on_geometry()` which retrieves cached lazy-function graph, constructs executor, feeds inputs, runs it.

## The Depsgraph

DAG of `OperationNode`s grouped into `ComponentNode`s (TRANSFORM, GEOMETRY, ANIMATION) grouped into `IDNode`s.

Modifiers declare dependencies via `update_depsgraph()` callback (e.g., boolean modifier depends on operand's geometry).

### Evaluation

`DEG_evaluate_on_refresh` runs:
1. Copy-on-eval pass
2. Dynamic visibility evaluation
3. Threaded evaluation of tagged operations
4. Single-threaded workaround pass

Operations scheduled into task pool. Finished nodes decrement pending-count on children.

### Invalidation

Tag-based at ID level (`IDRecalcFlag`): `ID_RECALC_TRANSFORM`, `ID_RECALC_GEOMETRY`, `ID_RECALC_SHADING`, etc.

`DEG_id_tag_update(id, flag)` marks dirty. On next evaluate, tags flush along dependency edges, then all tagged operations run.

## Data Flow and Ownership

### GeometrySet

Universal carrier. Map of `GeometryComponent` subtypes (Mesh, PointCloud, Instance, Volume, Curve, GreasePencil).

Each component uses `ImplicitSharingPtr` (refcount COW). Read-only when refcount > 1; writing calls `ensure_owns_direct_data()` which copies.

### Copy-on-eval (depsgraph)

Every ID has original (user edits) and evaluated (rendering/display) versions. Depsgraph makes shallow copy of every ID. Modifiers only see evaluated data. Runtime caches backed up and restored across re-evaluations.

## Key Patterns

- **Two-pass modifier evaluation**: deform-only fast path (no mesh copy), then constructive
- **Lazy-function graph**: demand-driven, short-circuit, multi-threaded node evaluation
- **Compile-time registration**: no runtime extensibility for modifiers or nodes
- **ImplicitSharing COW**: fine-grained at attribute array level
- **Depsgraph tag-based invalidation**: ID-level granularity, flushes along dependency edges
