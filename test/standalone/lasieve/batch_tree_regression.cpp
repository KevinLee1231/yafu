#include "factor/nfs/lasieve/batch_factor.cpp"
#include <assert.h>

/* 这个驱动链的是单份对象，lasieve_ns 解析成 lasieve_single；per-I 的
 * 名字现在都在命名空间里，所以要显式引进来。 */
using namespace lasieve_ns;
int main(void) {
    bintree_t tree;
    tree.size = tree.alloc = 1;
    tree.nodes = (bintree_element_t *)calloc(1, sizeof(*tree.nodes));
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
