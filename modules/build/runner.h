#ifndef MEL_BUILD_RUNNER_H
#define MEL_BUILD_RUNNER_H

#include "internal.h"

char *mel_str_dup(const char *s);
char *mel_str_fmt(const char *fmt, ...);
bool  mel_path_is_dir(const char *p);
bool  mel_path_is_file(const char *p);
bool  mel_path_exists(const char *p);
char *mel_path_join(const char *a, const char *b);

bool mel_when_match(Mel_When w, const Mel_Variant *v);
void mel_glob(const char *base, const char *pattern, Mel_StrVec *out);

int   mel_run(char *const argv[]);
int   mel_run_quiet(char *const argv[]);
int   mel_run_vec(Mel_StrVec *cmd);
void  mel_mkdirs(const char *path);
char *mel_read_file(const char *path);
bool  mel_write_file(const char *path, const char *data);
bool  mel_copy_file(const char *src, const char *dst);

bool  mel_package(Mel_Target *t, const Mel_Variant *v, const char *outdir, const char *exe);
char *mel_win32_resource(Mel_Target *t, const char *outdir);

typedef struct {
    Mel_Target *t;
    void             *dll;
    char             *so;
    char             *build_c;
} Mel_Node;

typedef MEL_VEC(Mel_Node) Mel_NodeVec;

typedef struct {
    Mel_NodeVec nodes;
} Mel_Graph;

typedef MEL_VEC(size_t) Mel_IdxVec;

bool              mel_discover_dir(Mel_Graph *g, const char *dir);
void              mel_discover(Mel_Graph *g);
Mel_Target *mel_graph_find(Mel_Graph *g, const char *name);
int               mel_graph_index(Mel_Graph *g, const char *name);
bool              mel_topo_closure(Mel_Graph *g, const char *root, Mel_IdxVec *order);

Mel_Variant mel_variant_native(Mel_Platform platform, const char *config);
const char *mel_platform_name(Mel_Platform p);
bool        mel_target_available(Mel_Target *t, const Mel_Variant *v);

typedef struct {
    char       *cc;
    char       *ar;
    char       *base_cflags;
    char       *base_ldflags;
    const char *exe_ext;
} Mel_Toolchain;

Mel_Toolchain mel_toolchain(const Mel_Variant *v);

bool mel_gather_compile(Mel_Graph *g, size_t idx, const Mel_Variant *v, Mel_StrVec *srcs,
                        Mel_StrVec *cflags);
void mel_gather_link(Mel_Graph *g, size_t idx, const Mel_Variant *v, Mel_StrVec *ldflags);

char *mel_target_outdir(const char *target_dir, const Mel_Variant *v);
bool  mel_prepare_thirdparty(Mel_Graph *g, Mel_IdxVec *order, const Mel_Variant *v);
bool  mel_emit_and_build(Mel_Graph *g, const char *root, const Mel_Variant *v);

#endif
