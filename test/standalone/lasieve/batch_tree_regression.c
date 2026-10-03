#include "factor/lasieve5_64/batch_factor.c"
#include <assert.h>
int main(void) {
    bintree_t tree;
    tree.size = tree.alloc = 1;
    tree.nodes = calloc(1, sizeof(*tree.nodes));
    assert(tree.nodes != NULL);
    tree.nodes[0].low = 0;
    tree.nodes[0].high = 9;
    tree.nodes[0].left_id = -1;
    tree.nodes[0].right_id = -1;
    mpz_init(tree.nodes[0].prod);
    assert(getNode(&tree, 0, 9) == 0);
    assert(getNode(&tree, 0, 4) == -1);
    addNode(&tree, 0, 0, 0, 4, NULL);
    assert(tree.alloc == 2 && tree.size == 2);
    assert(tree.nodes[0].left_id == 1);
    assert(getNode(&tree, 0, 4) == 1);
    mpz_clear(tree.nodes[0].prod);
    mpz_clear(tree.nodes[1].prod);
    free(tree.nodes);
    return 0;
}
