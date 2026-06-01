#include "runner.h"

#include <stdio.h>

int mel_graph_index(Mel_Graph* g, const char* name)
{
    for (size_t i = 0; i < g->nodes.len; i++)
        if (strcmp(g->nodes.items[i].t->name, name) == 0)
            return (int)i;
    return -1;
}

static bool visit(Mel_Graph* g, size_t i, char* state, Mel_IdxVec* order)
{
    if (state[i] == 2)
        return true;
    if (state[i] == 1)
    {
        fprintf(stderr, "build: dependency cycle through '%s'\n", g->nodes.items[i].t->name);
        return false;
    }
    state[i] = 1;
    Mel_Target* t = g->nodes.items[i].t;
    for (size_t k = 0; k < t->deps.len; k++)
    {
        int j = mel_graph_index(g, t->deps.items[k]);
        if (j < 0)
        {
            fprintf(stderr, "build: '%s' depends on unknown target '%s'\n", t->name, t->deps.items[k]);
            return false;
        }
        if (!visit(g, (size_t)j, state, order))
            return false;
    }
    state[i] = 2;
    mel_da_push(order, i);
    return true;
}

bool mel_topo_closure(Mel_Graph* g, const char* root, Mel_IdxVec* order)
{
    int r = mel_graph_index(g, root);
    if (r < 0)
    {
        fprintf(stderr, "build: unknown target '%s'\n", root);
        return false;
    }
    char* state = calloc(g->nodes.len, 1);
    if (!state)
        abort();
    bool ok = visit(g, (size_t)r, state, order);
    free(state);
    return ok;
}
