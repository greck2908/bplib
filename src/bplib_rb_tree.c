/************************************************************************
 * File: bplib_rb_tree.c
 *
 *  Copyright 2019 United States Government as represented by the
 *  Administrator of the National Aeronautics and Space Administration.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * Maintainer(s):
 *  Alexander Meade, Code 582 NASA GSFC
 *
 *************************************************************************/


/******************************************************************************
 INCLUDES
 ******************************************************************************/ 

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "bplib_rb_tree.h"

/******************************************************************************
 LOCAL FUNCTIONS
 ******************************************************************************/ 

/*--------------------------------------------------------------------------------------
 * pop_free_node - Retrieves a block of unallocated memory to assign an rb_node if available. 
 *
 * tree: A ptr to a rb_tree. [OUTPUT]
 * returns: A ptr to a free block of memory to allocate an rb_node. If no space is available
 *      a NULL ptr is returned. If the ptr is non NULL the tree's size is incremented.
 *-------------------------------------------------------------------------------------*/
static rb_node* pop_free_node(struct rb_tree* tree)
{
    rb_node* free_node = tree->free_node_tail;

    if (free_node != NULL) {
        tree->free_node_tail = free_node->left;
        if (tree->free_node_tail == NULL)
        {
            tree->free_node_head = NULL;
        }
        tree->size += 1;
    }    

    return free_node;
}

/*--------------------------------------------------------------------------------------
 * push_free_node - Recovers a block of unallocated memory for assigning future rb_nodes to.
 *
 * tree: A ptr to a rb_tree. The tree's size is decremented after pushing the node. [OUTPUT] 
 * node: A ptr to a rb_node to reassign that can now be treated as unallocated memory.
 *-------------------------------------------------------------------------------------*/
static void push_free_node(struct rb_tree* tree, struct rb_node* node)
{
    // Push the node into the queue.
    node->right = tree->free_node_head;
    node->left = NULL;
    tree->size -= 1;

    if (tree->free_node_head == NULL) {
        // If head and tail are currently NULL make this node both.
        tree->free_node_head = node;
        tree->free_node_tail = node;
    }
    else 
    {
        // If head exists reassign head to point to this new node.
        tree->free_node_head->left = node;
        tree->free_node_head = node;
    }
}

/*--------------------------------------------------------------------------------------
 * set_black -
 *
 * node: A ptr to an rb_node to set the color to black. [OUTPUT] 
 *-------------------------------------------------------------------------------------*/
static void set_black(struct rb_node* node)
{
    node->color = false;
}

/*--------------------------------------------------------------------------------------
 * set_red -
 *
 * node: A ptr to an rb_node to set the color to red. [OUTPUT]
 *-------------------------------------------------------------------------------------*/
static void set_red(struct rb_node* node)
{
    node->color = true;
}

/*--------------------------------------------------------------------------------------
 * swap_colors -
 *
 * n1: A ptr to the first node to swap colors. [OUTPUT]
 * n2: A ptr to the second node to swap colors. [OUTPUT]
 *-------------------------------------------------------------------------------------*/
static void swap_colors(struct rb_node* n1, struct rb_node* n2)
{
    bool temp_color = n1->color;
    n1->color = n2->color;
    n2->color = temp_color;
}

/*--------------------------------------------------------------------------------------
 * is_black -
 *
 * node: A rb_node to check its color. [INPUT]
 * returns: Whether or not the provided rb_node is black.  
 *-------------------------------------------------------------------------------------*/
static bool is_black(struct rb_node* node)
{
    return node == NULL || !(node->color);
}

/*--------------------------------------------------------------------------------------
 * is_red -
 *
 * node: A rb_node to check its color. [INPUT]
 * returns: Whether or not the provided rb_node is red.  
 *-------------------------------------------------------------------------------------*/
static bool is_red(struct rb_node* node)
{
    return node != NULL && node->color;
}


/*--------------------------------------------------------------------------------------
 * get_grandparent -
 *
 * node: A ptr to an rb_node to get its grandparent. [INPUT]
 * returns: A ptr to the node grandparent or NULL
 *-------------------------------------------------------------------------------------*/
struct rb_node* get_grandparent(struct rb_node* node)
{
    struct rb_node* parent = node->parent;
    if (parent == NULL)
    {
        return NULL;
    }
    return parent->parent;
}

/*--------------------------------------------------------------------------------------
 * is_root -
 *
 * node: A ptr to an rb_node to check if it is the root of an red black tree. [INPUT]
 * returns: Whether or not the provided node is the tree root.  
 *-------------------------------------------------------------------------------------*/
static bool is_root(struct rb_node* node)
{
    return node->parent == NULL;
}

/*--------------------------------------------------------------------------------------
 * is_left_child -
 *
 * node: A ptr to an rb_node to check if it is the left child of its parent node. [INPUT]
 * returns: Whether or not the provided node is the left child of its parent. If the
 *      provided node is root then this returns false. 
 *-------------------------------------------------------------------------------------*/
static bool is_left_child(struct rb_node* node)
{
    return !is_root(node) && node == node->parent->left;
}

/*--------------------------------------------------------------------------------------
 * get_sibling - A sibling is defined as a parents other child within the rb_tree.
 *
 * node: A ptr to an rb_node to get its sibling. [INPUT]
 * returns: A ptr to the sibling or NULL of node.
 *-------------------------------------------------------------------------------------*/
struct rb_node* get_sibling(struct rb_node* node)
{
    struct rb_node* parent = node->parent;
    if (parent == NULL)
    {
        return NULL;
    }
    else if (is_left_child(node))
    {
        return parent->right;
    }
    else {
        return parent->left;
    }
}

/*--------------------------------------------------------------------------------------
 * get_uncle - An uncle within an rb_tree is a nodes parent's parent's sibling.
 *
 * node: A ptr to an rb_node to get its uncle. [INPUT]
 * returns: A ptr to the nodes uncle or or NULL.  
 *-------------------------------------------------------------------------------------*/
struct rb_node* get_uncle(struct rb_node* node)
{
    struct rb_node* parent = node->parent;
    if (parent == NULL)
    {
        return NULL;
    }
    struct rb_node* grandparent = get_grandparent(node);
    if (grandparent == NULL)
    {
        return NULL;
    }
    return get_sibling(parent);
}

/*--------------------------------------------------------------------------------------
 * has_left_child -
 *
 * node: A ptr to an rb_node to check if it has a left child. [INPUT]
 * returns: Whether or not the provided node has a non NULL left child.   
 *-------------------------------------------------------------------------------------*/
static bool has_left_child(struct rb_node* node)
{
    return node->left != NULL;
}

/*--------------------------------------------------------------------------------------
 * has_right_child -
 *
 * node: A ptr to an rb_node to check if it has a right child. [INPUT]
 * returns: Whether or not the provided node has a non NULL right child.   
 *-------------------------------------------------------------------------------------*/
static bool has_right_child(struct rb_node* node)
{
    return node->right != NULL;
}

/*--------------------------------------------------------------------------------------
 * swap_parents - Swaps the parents of two nodes which are currently parent and child
 *      of eachother.
 *
 * Example:
 *
 *     3                                     3
 *    / \                                   / \
 *   1   6      swap_parents(6, 8) --->    1   8
 *        \                                   / 
 *         8                                 6    
 *
 * node_1: A ptr to an rb_node to move down in the tree when swapping parents. [OUTPUT]
 * node_2: A ptr to an rb_node that was previously the child of node_1 and should be moved up
 *      in the tree when swapping parents. [OUTPUT]
 * tree: A ptr to the red black to within which to swap parents. [OUTPUT]
 *-------------------------------------------------------------------------------------*/
static void swap_parents(struct rb_node* node_1, struct rb_node* node_2, struct rb_tree* tree)
{
    node_2->parent = node_1->parent;

    if (is_root(node_1))
    {
        tree->root = node_2;
    }
    else if (is_left_child(node_1))
    {
        node_1->parent->left = node_2;
    }
    else
    {
        node_1->parent->right = node_2;
    }
    
    node_1->parent = node_2;
}

/*--------------------------------------------------------------------------------------
 * rotate_left - Rotates the red black tree left around a given node.
 *
 * Example:
 *          
 *         5                                    5
 *        /  \                                 /  \
 *       0   15     rotate_left(5, 15) --->   0   20 
 *          /  \                                 /  \
 *        10   20                               15   22
 *            /  \                             /  \
 *           17  22                           10  17
 * tree: A ptr to the rb_tree to rotate. [OUTPUT]
 * node: A ptr to an rb_node about which to rotate the red black tree left. [OUTPUT]
 *-------------------------------------------------------------------------------------*/
static void rotate_left(struct rb_tree* tree, struct rb_node* node)
{
    struct rb_node* new_parent = node->right;
    node->right = new_parent->left;
    new_parent->left = node;

    if (has_right_child(node))
    {
        node->right->parent = node;
    }
    swap_parents(node, new_parent, tree);
}

/*--------------------------------------------------------------------------------------
 * rotate_right - Rotates the red black tree right around a given node.
 *
 * Example:
 *          
 *         5                                     5 
 *        /  \                                  /  \
 *       0   15     rotate_right(5, 15) --->   0   10 
 *          /  \                                  /  \
 *        10    20                               7   15
 *       / \                                         / \
 *      7  13                                       13  20
 * tree: A ptr to the rb_tree to rotate. [OUTPUT]
 * node: A ptr to an rb_node about which to rotate the red black tree right. [OUTPUT]
 *-------------------------------------------------------------------------------------*/
static void rotate_right(struct rb_tree* tree, struct rb_node* node)
{
    struct rb_node* new_parent = node->left;
    node->left = new_parent->right;
    new_parent->right = node;

    if (has_left_child(node))
    {
        node->left->parent = node;
    }
    
    swap_parents(node, new_parent, tree);
}

/*-------------------------------------------------------------------------------------
 * populate_rb_node - Instantiates the values of an rb_node ptr.
 *
 * value: The value to assign to the rb_node. [INPUT]
 * node: A ptr to an rb_node to instantiate with defaults and set its value. [OUTPUT]
 *-------------------------------------------------------------------------------------*/
static void populate_rb_node(uint32_t value, struct rb_node* node)
{
    node->value = value;
    node->offset = 1;
    node->parent = NULL;
    node->left = NULL;
    node->right = NULL;
}

/*-------------------------------------------------------------------------------------
 * create_black_node - Creates a black colored rb_node with value and with no children.
 *
 * value: The value to assign to the new rb_node. [INPUT]
 * tree: A ptr to a rb_tree from which to obtain free memory blocks for the new node. [OUTPUT]
 * returns: A ptr to the newly created rb_node or NULL if no memory exists in the rb_tree.
 *-------------------------------------------------------------------------------------*/
static rb_node* create_black_node(uint32_t value, struct rb_tree* tree)
{
    rb_node* node = pop_free_node(tree);
    if (node == NULL)
    {
        // Return early since no memory exists within the tree to create a new node.
        return NULL;
    }
    populate_rb_node(value, node);
    set_black(node);
    return node;
}

/*-------------------------------------------------------------------------------------
 * create_red_node - Creates a red colored rb_node with value and with no children.
 *
 * value: The value to assign to the new rb_node. [INPUT]
 * tree: A ptr to a rb_tree from which to obtain free memory blocks for the new node. [OUTPUT]
 * returns: A ptr to the newly created rb_node or NULL if no memory exists in the rb_tree.
 *-------------------------------------------------------------------------------------*/
static rb_node* create_red_node(uint32_t value, struct rb_tree* tree)
{
    rb_node* node = pop_free_node(tree);
    if (node == NULL)
    {
        // Return early since no memory exists within the tree to create a new node.
        return NULL;
    }
    populate_rb_node(value, node);
    set_red(node);
    return node;
}

/*-------------------------------------------------------------------------------------
 * insert_child - Creates a parent and child relationship between two nodes.
 *
 * node: A ptr to the node to insert as a child. [OUTPUT]
 * parent: A ptr to the parent of node. [OUTPUT]
 * child_ptr: A ptr to a ptr owned by parent that will be assigned to its new child node. 
 *      This ptr should correspond to either parent->left or parent->right. [OUTPUT]
 *-------------------------------------------------------------------------------------*/
static void insert_child(struct rb_node* node,
                         struct rb_node* parent,
                         struct rb_node** child_ptr)
{
    node->parent = parent;
    *child_ptr = node;
}

/*--------------------------------------------------------------------------------------
 * apply_inorder - Applies a function in order to nodes in a rb_tree. 
 * 
 * node: A ptr to a rb_node to apply a function to and recurse its subtrees. [INPUT]
 * func. A function pointer accepting a ptr to an rb_node. [INPUT]
 *--------------------------------------------------------------------------------------*/  
static void apply_inorder(struct rb_node* node, void (*func)(rb_node*))
{
    if (has_left_child(node))
    {
        // Apply function to left subtree.
        apply_inorder(node->left, func);
    }

    // Apply function to the current node.
    func(node);

    if (has_right_child(node))
    {
        // Apply function to right subtree.
        apply_inorder(node->right, func);
    }
}

/*-------------------------------------------------------------------------------------
 * get_left_sucessor - Gets the sucessor node in the left subtree of the provided node.
 *      The left sucessor is defined as the right most node in the left subtree.
 *
 * Example:
 *         10
 *        /  \
 *       7    13    get_left_sucessor(10) --> 5.
 *        \
 *         5
 *
 * node: A ptr to the node to obtain its sucessor. [INPUT]
 * returns: A ptr to the sucessor node with a value less than that contained by node. If
 *      no sucessor exists, NULL is returned.
 *-------------------------------------------------------------------------------------*/
static struct rb_node* get_left_sucessor(struct rb_node* node)
{
    if (!has_left_child(node))
    {
        return NULL;
    }

    rb_node* sucessor = node->left;
    while (has_right_child(sucessor))
    {
        sucessor = sucessor->right;
    }
    return sucessor;
}

/*-------------------------------------------------------------------------------------
 * get_right_sucessor - Gets the sucessor node in the left subtree of the provided node.
 *      The right sucessor is defined as the left most node in the right subtree.
 *
 * Example:
 *         10
 *        /  \
 *       7    14    get_right_sucessor(10) --> 12.
 *            /
 *           12
 *
 * node: A ptr to the node to obtain its sucessor. [INPUT]
 * returns: A ptr to the sucessor node with a value less than that contained by node. If
 *      no sucessor exists, NULL is returned.
 *-------------------------------------------------------------------------------------*/
static struct rb_node* get_right_sucessor(struct rb_node* node)
{
    if (!has_right_child(node))
    {
        return NULL;
    }

    rb_node* sucessor = node->right;
    while (has_left_child(sucessor))
    {
        sucessor = sucessor->left;
    }
    return sucessor;
}

/*-------------------------------------------------------------------------------------
 * gets_sucessor - Gets the left or right sucessor node of a given node.
 *
 * node: A ptr to the node to obtain its sucessor. [INPUT]
 * returns: A ptr to the sucessor node. If no sucessor exists, NULL is returned.
 *-------------------------------------------------------------------------------------*/
static struct rb_node* get_sucessor(struct rb_node* node)
{
    rb_node* sucessor = get_left_sucessor(node);
    if (sucessor == NULL)
    {
        // The left subtree has no sucessor. Check the right subtree.
        sucessor = get_right_sucessor(node);
    }
    return sucessor;
}

/*-------------------------------------------------------------------------------------
 * swap_values - Swaps the values stored by two nodes.
 *
 * n1: A ptr to the first rb_node to swap its value. [OUTPUT]
 * n2: A ptr to the second rb_node to swap its value. [OUTPUT]
 *-------------------------------------------------------------------------------------*/
static void swap_values(struct rb_node* n1, struct rb_node* n2)
{
    uint32_t temp = n1->value;
    n1->value = n2->value;
    n2->value = temp;
}

/*-------------------------------------------------------------------------------------
 * swap_offsets - Swaps the offsets stored by two nodes.
 *
 * n1: A ptr to the first rb_node to swap its offset. [OUTPUT]
 * n2: A ptr to the second rb_node to swap its offset. [OUTPUT]
 *-------------------------------------------------------------------------------------*/
static void swap_offsets(struct rb_node* n1, struct rb_node* n2)
{
    uint32_t temp = n1->offset;
    n1->offset = n2->offset;
    n2->offset = temp;
}

/*-------------------------------------------------------------------------------------
 * replace_node - Replaces a node with its child.
 *
 * node: A ptr to the rb_node to replace with its child. [OUTPUT]
 * child: A ptr to an rb_node that is currently a child of node and will replace it. [OUTPUT]
 *-------------------------------------------------------------------------------------*/
static void replace_node(struct rb_node* node, struct rb_node* child)
{
    struct rb_node* parent = node->parent;

    if (is_left_child(node))
    {
        // Node is the left child of its parent.
        parent->left = child;
    }
    else
    {
        // Node is the right child of its parent.
        parent->right = child;
    }

    if (child != NULL)
    {
        // Child node is a leaf. In this case there is no need to assign its parent.
        child->parent = parent;
    }
}

/*--------------------------------------------------------------------------------------
 * delete_case_6 - Sibling is black. Node is a left child of parent and sibling has a 
 *      right red child or node is a right child and sibling has a left red child. 
 *      In these cases parent and sibling swap colors and the red child of sibling is
 *      recolored black. The tree is then rotated such that sibling becomes nodes grandparent.
 *     
 *            P       -->     S    
 *           / \             / \
 *          BN  BS          BP  BSR
 *               \          /    
 *                RSR      BN
 *
 * root: A ptr to the rb_tree within which we need to rebalance to maintain 
 *      its properties. [OUTPUT]
 * node: A ptr to the rb_node to rebalance. [OUTPUT]
 *--------------------------------------------------------------------------------------*
 * CITATION: The code in this function was adated from the following source.
 *
 * Wikipedia contributors. (2019, June 16). Red–black tree. In Wikipedia, The Free Encyclopedia.
 * Retrieved 14:34, June 20, 2019, 
 * from https://en.wikipedia.org/w/index.php?title=Red%E2%80%93black_tree&oldid=902059008
 *--------------------------------------------------------------------------------------*/ 
static void delete_case_6(struct rb_tree* tree, struct rb_node* node)
{
    struct rb_node* sibling =  get_sibling(node);
    struct rb_node* parent = node->parent;
    sibling->color = parent->color;
    set_black(parent);

    if (is_left_child(node))
    {
        set_black(sibling->right);
        rotate_left(tree, parent);
    }
    else
    {
        set_black(sibling->left);
        rotate_right(tree, parent);
    }
}

/*--------------------------------------------------------------------------------------
 * delete_case_5 - Sibling is a black left child and has a red left child and a black 
 *      right child. The mirror case is also applicable. In these situations the tree is 
 *      rotated such sibling becomes the child of its red child and then its color is
 *      swapped with the red child (new parent). If this case is not met, this function 
 *      casecades into case 6.
 *     
 *            BS        -->   BSL    
 *           / \               \
 *          RSL BSR            RS
 *                              \
 *                              BSL
 *
 * root: A ptr to the rb_tree within which we need to rebalance . [OUTPUT]
 * node: A ptr to the rb_node to rebalance. [OUTPUT]
 *--------------------------------------------------------------------------------------*
 * CITATION: The code in this function was adated from the following source.
 *
 * Wikipedia contributors. (2019, June 16). Red–black tree. In Wikipedia, The Free Encyclopedia.
 * Retrieved 14:34, June 20, 2019,
 * from https://en.wikipedia.org/w/index.php?title=Red%E2%80%93black_tree&oldid=902059008
 *--------------------------------------------------------------------------------------*/ 
static void delete_case_5(struct rb_tree* tree, struct rb_node* node)
{
    struct rb_node* sibling = get_sibling(node);
    
    if (is_black(sibling))
    {
        bool is_left = is_left_child(node);
        if (is_left && is_black(sibling->right) && is_red(sibling->left))
        {
            set_red(sibling);
            set_black(sibling->left);
            rotate_right(tree, sibling);
        }
        else if (!is_left && is_black(sibling->left) && is_red(sibling->right))
        {
            rotate_left(tree, sibling);
        }
    }
    delete_case_6(tree, node);
}

/*--------------------------------------------------------------------------------------
 * delete_case_4 - Parent is red but sibling and its children are black. In this case
 *      the colors of parent and sibling can be swapped and node can then be deleted preserving
 *      black depth. If this is not the case then this function casecades in cases 5 & 6. 
 *     
 *          RP                   BP
 *         / \                  /  \
 *        BN BS         -->    BN   RS
 *           / \                    / \
 *          BSL BSR               BSL  BSR
 *
 *
 * root: A ptr to the rb_tree within which we need to rebalance. [OUTPUT]
 * node: A ptr to the rb_node to rebalance. [OUTPUT]
 *
 *--------------------------------------------------------------------------------------*
 * CITATION: The code in this function was adated from the following source.
 *
 * Wikipedia contributors. (2019, June 16). Red–black tree. In Wikipedia, The Free Encyclopedia.
 * Retrieved 14:34, June 20, 2019, from 
 * https://en.wikipedia.org/w/index.php?title=Red%E2%80%93black_tree&oldid=902059008
 *--------------------------------------------------------------------------------------*/ 
static void delete_case_4(struct rb_tree* tree, struct rb_node* node)
{
    struct rb_node* sibling = get_sibling(node);
    struct rb_node* parent = node->parent;

    if (is_red(parent) &&
        is_black(sibling) &&
        is_black(sibling->left) && 
        is_black(sibling->right))
    {
        set_red(sibling);
        set_black(parent);
    }
    else
    {
        delete_case_5(tree, node);
    }
}

// This is a function declaration for delete_case_1. For details on the intended usage see
// the function definition.
static void delete_case_1(struct rb_tree *tree, struct rb_node* node);

/*--------------------------------------------------------------------------------------
 * delete_case_3 - If parent, its siblings and its children are black then sibling can
 *      be recolored such that pathes through sibling have one less black node which allows
 *      us to delete node and maintain balance in tree. Pathes through parent however now
 *      have one fewer black node than other pathes and so delete_case_1 must be called to
 *      fix parent. If nodes are not recolored as described above, this function casecades
 *      into cases 4, 5 & 6.
 *
 *     
 *          BP                   BP
 *         / \                  /  \
 *        BN BS         -->    BN   RS
 *           / \                    / \
 *          BSL BSR                BSL BSR
 *
 *
 * root: A ptr to the rb_tree within which we need to rebalance. [OUTPUT]
 * node: A ptr to the rb_node to rebalance. [OUTPUT]
 *
 *--------------------------------------------------------------------------------------*
 * CITATION: The code in this function was adated from the following source.
 *
 * Wikipedia contributors. (2019, June 16). Red–black tree. In Wikipedia, The Free Encyclopedia.
 * Retrieved 14:34, June 20, 2019, 
 * from https://en.wikipedia.org/w/index.php?title=Red%E2%80%93black_tree&oldid=902059008
 *--------------------------------------------------------------------------------------*/ 
static void delete_case_3(struct rb_tree* tree, struct rb_node* node)
{
    struct rb_node* sibling = get_sibling(node);
    struct rb_node* parent = node->parent;
    if (is_black(parent) &&
        is_black(sibling) && 
        is_black(sibling->left) &&
        is_black(sibling->right))
    {
        set_red(sibling);
        delete_case_1(tree, parent);
    }
    else
    {
        delete_case_4(tree, node);
    }
}

/*--------------------------------------------------------------------------------------
 * delete_case_2 - If sibling is red the colors of it and parent are swapped and
 *      and the tree is rotated such that sibling becomes node's grandparent. This stage
 *      prepares the tree for the follow on cases, 3, 4, 5 & 6.
 *     
 *          BP                   BS
 *         / \                  /  \
 *        BN RS         -->    RP  BSR
 *           / \              / \
 *          BSL BSR          BN  BSL
 *
 *
 * root: A ptr to the rb_tree within which we need to rebalance. [OUTPUT]
 * node: A ptr to the rb_node to rebalance. [OUTPUT]
 *
 *--------------------------------------------------------------------------------------*
 * CITATION: The code in this function was adated from the following source.
 *
 * Wikipedia contributors. (2019, June 16). Red–black tree. In Wikipedia, The Free Encyclopedia.
 * Retrieved 14:34, June 20, 2019,
 * from https://en.wikipedia.org/w/index.php?title=Red%E2%80%93black_tree&oldid=902059008
 *--------------------------------------------------------------------------------------*/ 
static void delete_case_2(struct rb_tree *tree, struct rb_node* node)
{
    struct rb_node* sibling = get_sibling(node);
    struct rb_node* parent = node->parent;

    if (is_red(sibling))
    {
        // Swap colors between parent and sibling.
        set_red(parent);
        set_black(sibling);

        // Rotate sibling into the grandparent position.
        if (is_left_child(node))
        {
            rotate_left(tree, parent);
        }
        else
        {
            rotate_right(tree, parent);
        }
    }

    delete_case_3(tree, node);
}


/*--------------------------------------------------------------------------------------
 * delete_case_1 - Checks if a node is root. If it is rebalancing for deletion is complete.
 *      Otherwise, rebalancing progresses into different cases.
 *
 * root: A ptr to the rb_tree within which we need to rebalance. [OUTPUT]
 * node: A ptr to the rb_node to delete. [OUTPUT]
 *
 *--------------------------------------------------------------------------------------*
 * CITATION: The code in this function was adated from the following source.
 *
 * Wikipedia contributors. (2019, June 16). Red–black tree. In Wikipedia, The Free Encyclopedia.
 * Retrieved 14:34, June 20, 2019, 
 * from https://en.wikipedia.org/w/index.php?title=Red%E2%80%93black_tree&oldid=902059008
 *--------------------------------------------------------------------------------------*/ 
static void delete_case_1(struct rb_tree *tree, struct rb_node* node)
{
    if (is_root(node))
    {
        // Node is root. No more rebalancing is needed.
        return; 
    }

    delete_case_2(tree, node);
}

/*--------------------------------------------------------------------------------------
 * delete_one_child - Deletes a node from a rb_tree containing at most one child.
 *
 * tree: A ptr to the rb_tree from which to delete a node. [OUTPUT] 
 * node: A ptr to the rb_node to delete. [OUTPUT]
 *
 *--------------------------------------------------------------------------------------*
 * CITATION: The code in this function was adated from the following source.
 *
 * Wikipedia contributors. (2019, June 16). Red–black tree. In Wikipedia, The Free Encyclopedia.
 * Retrieved 14:34, June 20, 2019, 
 * from https://en.wikipedia.org/w/index.php?title=Red%E2%80%93black_tree&oldid=902059008
 *--------------------------------------------------------------------------------------*/ 
static void delete_one_child(struct rb_tree* tree, struct rb_node* node)
{
    struct rb_node* child = has_left_child(node) ? node->left : node->right;

    if (child == NULL)
    {
        // The current node is a leaf with no children.

        if (is_black(node))
        {
            // If the node is a black leaf with no children then deleting it
            // requires rebalancing.
            delete_case_1(tree, node);
        }
       
        // Replace the current node with its NULl child. In the case where the current
        // node was red this step results in a trivial deletion.
        replace_node(node, child);
    }
    else {
        
        // Replace the current node with its non-null child and rebalance.
        replace_node(node, child);

        if (is_black(node))
        {
            // Rebalancing is only required upon deleting a black node 
            // since a red node results in a trivial swap and deletion.

            if (is_red(child))
            {
                // If node is a black and child is red then simply recolor
                // child to black to preserve the black depth of the tree.
                set_black(child);
            }
            else
            {
                // Node and its child are black. The tree must be rebalanced to 
                // account for the change in black depth.
                delete_case_1(tree, child);
            }
        }
    }
    
    // Free the block of memory for the deleted node.
    push_free_node(tree, node); 
}

/*--------------------------------------------------------------------------------------
 * delete_node - Deletes an rb_node from an rb_tree and rebalances the tree accordingly. 
 *
 * tree: A ptr to the rb_tree from which to delete an rb_node. [OUTPUT] 
 * node: A ptr to the rb_node to delete from the tree. [OUTPUT]
 *--------------------------------------------------------------------------------------*/ 
static void delete_node(struct rb_tree* tree, struct rb_node* node)
{
    // Attempt to find a sucessor node for the current node. 
    struct rb_node* replace_node = get_sucessor(node);

    if (replace_node == NULL && is_root(node))
    {
        // The current node is root and has no sucessors so deleting it is trivial.
        push_free_node(tree, node);
        tree->root = NULL;
        return;
    }
    else if (replace_node != NULL)
    {
        /* There exists a leaf node within the subtrees of node such that
         * we can replace it with the node we want to delet and simply focus on 
         * the case of deleting the leaf node.
         */
        swap_values(node, replace_node);
        swap_offsets(node, replace_node);
        node = replace_node;
    }

    // Node now points to a node with at most one child that we want to delete.
    delete_one_child(tree, node);
}

/*--------------------------------------------------------------------------------------
 * are_consecutive - Checks if value_2 is the consecutive integer after value_1.
 *
 * value_1: The lesser of the two potentially consecutive values. [INPUT]
 * value_2: The greater of the two potentially consecutive values. [INPUT]
 * returns: Whether value_2 is the consecutive integer after value_1.
 *-------------------------------------------------------------------------------------*/
static bool are_consecutive(uint32_t value_1, uint32_t value_2)
{
    return value_1 + 1 == value_2;
}

/*--------------------------------------------------------------------------------------
 * try_binary_insert_or_merge - Attempts to insert a new value into a red black tree. If
 *      the value is a duplicate it is ignored. If the value is a consecutive number with
 *      any existing node values then those nodes are merged and their offsets are incremented
 *      to reflect the range of values the modified nodes will represent.
 *
 * value: The value of the node to attempt to insert or merge into the red black tree. [INPUT]
 * tree: A ptr to the rb_tree to insert values into. [OUTPUT]
 * returns: A ptr to the newly inserted node. NULL is returned if the value was a duplicate
 *      of if it was merged into any existing nodes.
 *-------------------------------------------------------------------------------------*/
static rb_node* try_binary_insert_or_merge(uint32_t value, struct rb_tree* tree)
{
    rb_node* node = tree->root;
    if (node == NULL) 
    {
        // The tree is empty and we can attempt to insert a new node.

        if(is_full(tree)) 
        {
            // The tree has no memory allocated to it.
            return false;
        }
        // The tree is empty and so a root node is created.
        tree->root = create_black_node(value, tree);
        return tree->root;
    }

    while (true)
    {  
        if (are_consecutive(value, node->value)) 
        {
            // The current node value is greater than value and consecutive and so we should
            // merge the new value into the tree.
            struct rb_node* sucessor = get_left_sucessor(node);
            if (sucessor != NULL &&
                are_consecutive(sucessor->value + sucessor->offset - 1, value))
            {
                // The left child of the current node is also consecutive with the new
                // value and so we can merge the three values into a single node.
                node->value = sucessor->value;
                node->offset += sucessor->offset + 1;
                delete_node(tree, sucessor);
                return NULL;
            }
            else {
                // Merge the new value into the current node.
                node->value = value;
                node->offset += 1;
                return NULL;
            }
        }
        else if (value < node->value)
        {
            // Value is less than the current node's value and is not consecutive so we either
            // search a subtree of the child or insert a new node.
            if (has_left_child(node))
            {
                // Node already has a left child and so we search the left subtree and repeat
                // the while loop.
                node = node->left;
            }
            else
            {
                // Memory was available for new node and it is added.
                rb_node* new_node = create_red_node(value, tree);
                if (new_node == NULL)
                {
                    // There was no memory remaining for inserting a new child.
                    return NULL;
                }
                insert_child(new_node, node, &node->left);
                return new_node;
            }
           
        }
        else if (are_consecutive(node->value + node->offset - 1, value))
        {
            // The current node value is lesser than value and consecutive and so we should
            // merge the new value into the tree.
            struct rb_node* sucessor = get_right_sucessor(node);
            if (sucessor != NULL && are_consecutive(value, sucessor->value))
            {
                // The right child of the current node is also consecutive the the new value
                // and so we can merge the three values into a single node.
                node->offset += sucessor->offset + 1;
                delete_node(tree, sucessor);
                return NULL;
            }
            else
            {
                node->offset += 1;
                return NULL;
            }             
        }
        else if (value > node->value)
        {    
            // Value is greater than the current node's value and is not consecutive and so we
            // either search a subtree of the child or insert a new node.
            if (has_right_child(node))
            {
                // Node already has a left child and so we search the right subtree and 
                // repeat the while loop.
                node = node->right;
            }
            else
            {
                // Memory was available for new node and it is added.
                rb_node* new_node = create_red_node(value, tree);
                if (new_node == NULL)
                {
                    // There was no memory remaining for inserting a new child.
                    return NULL;
                }
                insert_child(new_node, node, &node->right);
                return new_node;
            }
        }
        else
        {
            // Value already exists within the tree. Do not insert any duplicates.
            return NULL;
        }
    }
    // This case should never occur.
    return NULL;
}

/*--------------------------------------------------------------------------------------
 * try_insert_rebalance - Ensure that the red black tree adheres to all rules and rebalances
 *      the nodes within it accordingly. 
 *
 * root: A ptr to the rb_tree to rebalance. [OUTPUT]
 * node: A ptr to the last modified node in the red black tree. [OUTPUT]
 *
 *--------------------------------------------------------------------------------------*
 * CITATION: The code in this function was adated from the following source.
 *
 * Wikipedia contributors. (2019, June 16). Red–black tree. In Wikipedia, The Free Encyclopedia.
 * Retrieved 14:34, June 20, 2019, 
 * from https://en.wikipedia.org/w/index.php?title=Red%E2%80%93black_tree&oldid=902059008
 *--------------------------------------------------------------------------------------*/ 
static void try_insert_rebalance(struct rb_tree* tree, struct rb_node* node)
{
    while(true)
    {
        rb_node* parent = node->parent;
        rb_node* uncle = get_uncle(node);   
 
        if (parent == NULL)
        {
            // Case Root:
            // The inserted node is root. Set its color to black. 
            set_black(node);
            return;
        }
        else if (is_black(parent))
        {
            // Case Black Parent:
            // For insertion, this means we have not violated any rules.
            return;
        }
        else if (uncle != NULL && is_red(uncle))
        {
            // Case Red Parent and Red Uncle:
            // Both parent and uncle should be made black.
            set_black(parent);
            set_black(uncle);
            rb_node* grandparent = get_grandparent(node);
            set_red(grandparent);

            // Repeat the rebalancing steps to correct for red root case or red parent.
            node = grandparent;
        }
        else
        {     
            // Case Red Parent and Black Uncle:   
            // The goal of the below steps is to rotate the current node into the grandparent
            // position.
            rb_node* grandparent = get_grandparent(node);

            if (parent == grandparent->left && node == parent->right)
            {
                // If parent is a left child and node is a right child then rotate left
                // and reassign node to perform an additional rebalancing loop.
                rotate_left(tree, parent);
                node = node->left; 
            }
            else if (parent == grandparent->right && node == parent->left)
            {
                // If parent is a right child and node is a left child then rotate right
                // and reassign node to perform an additional rebalancing loop.
                rotate_right(tree, parent);
                node = node->right;
            }
            
            grandparent = get_grandparent(node);
            parent = node->parent;

            if (is_left_child(node))
            {
                rotate_right(tree, grandparent);
            }
            else
            {
                rotate_left(tree, grandparent);
            }
            set_black(parent);
            set_red(grandparent);
            return;
        }
    }
}

/******************************************************************************
 EXPORTED FUNCTIONS
 ******************************************************************************/

/*--------------------------------------------------------------------------------------
 * create_rb_tree -
 *
 * max_size: The maximum number of allowable nodes within the red black tree.
 * returns: An empty red black tree.
 *--------------------------------------------------------------------------------------*/ 
struct rb_tree* create_rb_tree(uint32_t max_size) 
{
    // Allocate the rb tree.
    rb_tree* tree = (rb_tree*) malloc(sizeof(rb_tree));
    // Size starts maxed out until free blocks are allocated.
    tree->size = max_size;
    tree->max_size = max_size;
    tree->root = NULL;
    tree->free_node_head = NULL;
    tree->free_node_tail = NULL;

    // Allocate a block of memory for the nodes in the tree and add them all to the
    // the free nodes queue.
    tree->node_block = (rb_node*) calloc(max_size, sizeof(rb_node));

    if (tree->node_block == NULL)
    {
        // If no memory is allocated return an empty tree.
        tree->free_node_head = NULL;
        tree->free_node_tail = NULL;
        return tree;
    }

    rb_node* start = tree->node_block;
    rb_node* end = start + max_size;
    
    for (; start < end; start++)
    {
        push_free_node(tree, start);
    }
    return tree;
}

/*--------------------------------------------------------------------------------------
 * is_empty - 
 *
 * tree: The rb_tree to check whether is empty.
 * returns: True when the rb_tree has no nodes, otherwise false.
 *--------------------------------------------------------------------------------------*/ 
bool is_empty(struct rb_tree *tree)
{
    return tree->size == 0;
}

/*--------------------------------------------------------------------------------------
 * is_full - 
 *
 * tree: The rb_tree to check whether is full.
 * returns: True when the rb_tree is full, otherwise false.
 *--------------------------------------------------------------------------------------*/ 
bool is_full(struct rb_tree *tree)
{
    return tree->size == tree->max_size;
}

/*--------------------------------------------------------------------------------------
 * insert - Inserts a value into the red black tree and rebalances it accordingly.
 *
 * value - The value to insert into the rb_tree. [INPUT]
 * tree: A ptr to a rb_tree to insert the value into. [OUTPUT]
 * returns: A bool indicating whether or not the insertion was sucessful. Insertion will be
 *      unsucessful for duplicates or when the tree has no more memory available to allocate.
 *--------------------------------------------------------------------------------------*/ 
bool insert(uint32_t value, struct rb_tree* tree)
{
    rb_node* inserted_node = try_binary_insert_or_merge(value, tree);

    if (inserted_node == NULL)
    {
        // No node was added to the tree due to memory constraints or duplicates. 
        return false;
    }

    // Correct any violations within the red black tree due to the insertion.
    try_insert_rebalance(tree, inserted_node);
    return true;
}

/*--------------------------------------------------------------------------------------
 * delete_rb_tree - Frees all memory allocated by a given rb_tree. 
 *
 * tree: A ptr to a rb_tree to free its memory. [OUTPUT]
 *--------------------------------------------------------------------------------------*/ 
void delete_rb_tree(struct rb_tree* tree)
{
    
    if (tree == NULL)
    {
        return;
    }

    if (tree->node_block == NULL)
    {
        return;
    }
    
    free(tree->node_block);
    free(tree);
}

#define RBTREETESTS 
#ifdef RBTREETESTS
/*--------------------------------------------------------------------------------------
 * DEFINES TEST AND HELPER FUNCTIONS FOR THE RED BLACK TREE
 * 
 * RUN TESTS COMMAND LINE RECIPE:
 *
 *  sudo make sudo make APP_COPT=-DRBTREETESTS unittest && ./bp.out
 *--------------------------------------------------------------------------------------*/


/******************************************************************************
 TEST AND DEBUGGING HELPER FUNCTIONS 
 ******************************************************************************/

/*--------------------------------------------------------------------------------------
 * are_nodes_equal -
 * 
 * n1: A ptr to an rb_node to check equality with n2. [INPUT]
 * n2: A ptr to an rb_node to check equality with n1. [INPUT]
 * returns: A bool indicating whether or not the two provided node ptrs point to equal
 *      nodes. NULL ptrs are considered equal with eachother.
 *--------------------------------------------------------------------------------------*/  
static bool are_nodes_equal(struct rb_node* n1, struct rb_node* n2)
{
    // Handles NULL root and leaf node comparisons.
    if (n1 == NULL || n2 == NULL)
    {
        if (n1 == NULL && n2 == NULL)
        {
            return true;
        }
        return false;
    }

    // Checks node equality.
    return (n1->value            == n2->value  &&
            n1->offset           == n2->offset &&
            has_left_child(n1)   == has_left_child(n2)  &&
            has_right_child(n1)  == has_right_child(n2) &&
            is_root(n1)          == is_root(n2));
}

/*--------------------------------------------------------------------------------------
 * assert_equal_inorder_traverse - Asserts two trees have equivalent nodes when traversed
 *      in order.
 * 
 * n1: A ptr to an rb_node to check equality with n2 and traverse its subtrees. [INPUT]
 * n2: A ptr to an rb_node to check equality with n1 and traverse its subtrees. [INPUT]
 *--------------------------------------------------------------------------------------*/  
static void assert_equal_inorder_traverse(struct rb_node* n1, struct rb_node* n2)
{
    // Check equality of nodes at the current point in the tree.
    assert(are_nodes_equal(n1, n2));

    if (has_left_child(n1))
    {
        // Check the left subtree of each node for equality.
        assert_equal_inorder_traverse(n1->left, n2->left);
    }
    if (has_right_child(n1))
    {
        // Check the right subtree of each node for equality.
        assert_equal_inorder_traverse(n1->right, n2->right);
    }
}

/*--------------------------------------------------------------------------------------
 * assert_rb_trees_equal -
 * 
 * t1: A ptr to an rb_tree to check equality with t2. [INPUT]
 * t2: A ptr to an rb_tree to check equality with t1. [INPUT]
 *--------------------------------------------------------------------------------------*/  
static void assert_rb_trees_equal(struct rb_tree* t1, struct rb_tree* t2)
{
    assert (t1->size == t2->size);
    assert(t1->max_size == t2->max_size);
    assert_equal_inorder_traverse(t1->root, t2->root);
}

/*--------------------------------------------------------------------------------------
 * print_node -
 * 
 * node: A ptr to an rb_node to print. [INPUT]
 *--------------------------------------------------------------------------------------*/  
static void print_node(struct rb_node* node)
{
    if (node == NULL)
    {
        printf("NULL NODE\n");
        return;
    }
    printf("[ C: %5s || N: %3d || P: %3d || L: %3d || R: %3d || O: %3d]\n",
        node->color ? "RED" : "BLACK",
        node != NULL ? node->value : -1,
        node->parent != NULL ? node->parent->value : -1,
        node->left != NULL ? node->left->value : -1,
        node->right != NULL ? node->right->value : -1,
        node->offset);
}

/*--------------------------------------------------------------------------------------
 * print_tree - 
 * 
 * tree: A ptr to an rb_tree to print. [INPUT]
 *--------------------------------------------------------------------------------------*/  
static void print_tree(struct rb_tree* tree)
{
    printf("##################################\n");
    printf("* Count: %d\n", tree->size);
    printf("**********************************\n");
   
    if (tree->size == 0) {
        return;
    }
    printf("* In Order Elements:               \n");
    printf("**********************************\n"); 
    apply_inorder(tree->root, print_node);
    printf("**********************************\n");
}

/*--------------------------------------------------------------------------------------
 * assert_inorder_values_are - Asserts that the nodes in tree match the provided values.
 *
 * node: A ptr to a rb_node to recursively check if its values match the values array.
 * nodes: An array of ptr to rb_nodes whose values, offsets and colors will be 
 *      compared to the nodes in the tree tree inorder.
 * length: The length of the nodes array.
 * index: A ptr to an integer index into the nodes array which is incremented within recursive
 *      calls to this function.
 * returns: The number of times the current index was incremented by its subtrees
 *--------------------------------------------------------------------------------------*/  
static int assert_inorder_nodes_are(struct rb_node* node, 
                                    struct rb_node** nodes, 
                                    int length, 
                                    int index)
{
    int index_offset = 0;

    if (node == NULL)
    {
        // In the case where root is empty we should have no nodes. 
        assert(length == 0);
        return 0;
    }

    if (has_left_child(node))
    {
        // Check left subtree values.
        index_offset += assert_inorder_nodes_are(node->left, nodes, length, index);
    }

    // Check that there are not more nodes than expected.
    assert(index < length);
    
    // Check that the nodes matches those provided in the list.
    assert(node->value == nodes[index + index_offset]->value);
    assert(node->offset == nodes[index + index_offset]->offset);   
    assert(node->color == nodes[index + index_offset]->color); 

    index_offset += 1;

    if (has_right_child(node))
    {
        // Check right subtre values.
        index_offset += assert_inorder_nodes_are(node->right, nodes, length, 
                                                 index + index_offset);
    }

    return index_offset;
}

/*--------------------------------------------------------------------------------------
 * assert_node_has_no_adjacent_red - Checks the condition that a given node has no adjacent
 *      red nodes among its children.
 * 
 * node: A ptr to check rb_node to check it and its subtrees for validity. [INPUT]
 *--------------------------------------------------------------------------------------*/  
static void assert_node_has_no_adjacent_red(struct rb_node* node)
{
    if (is_black(node))
    {
        // Return early since the node is black and cannot have an adajcent red.
        return;
    }
    if (has_left_child(node))
    {
        // Assert left child is black since current node is red.
        assert(is_black(node->left));
        assert_node_has_no_adjacent_red(node->left);
    }
    if (has_right_child(node))
    {
        // Assert right child is black since current node is red.
        assert(is_black(node->right));
        assert_node_has_no_adjacent_red(node->right);
    }
}

/*--------------------------------------------------------------------------------------
 * count_is_black - 
 * 
 * node: A ptr to an rb_node to check its color. [INPUT]
 * returns: 1 if the provided node is black or null, else 0.
 *--------------------------------------------------------------------------------------*/  
static uint32_t count_is_black(rb_node* node)
{
    return is_black(node) ? 1 : 0;
}

/*--------------------------------------------------------------------------------------
 * assert_tree_pathes_have_equal_black_depths - Asserts the validity condition that
 *      all pathes from a given node in a red black tree have equal black depths.
 * 
 * node: A ptr to a node to check its subtrees for equal black depths. [INPUT]
 * returns: The black depth in indicating the number of black nodes along a path to a 
 * leaf starting at node.
 *--------------------------------------------------------------------------------------*/  
static uint32_t assert_tree_pathes_have_equal_black_depths(struct rb_node* node)
{
    if (node == NULL)
    {   
        // If the node is NULL leaf or NULL root return early.
        return 0;
    }

    int left_count = 0;
    int right_count = 0;
    if (has_left_child(node))
    {
        // Node has a left child. Check that the black depths are
        // equal down the child subtree.
        left_count = assert_tree_pathes_have_equal_black_depths(node->left);
    }
    else {
        // If node has no left child the left tree has a black
        // depth of 1 since leaves are black.
        left_count += 1;
    }
    if (has_right_child(node))
    {
        // Node has a left child. Check that the black depths are equal
        // down the child subtree.
        right_count = assert_tree_pathes_have_equal_black_depths(node->right);
    }
    else
    {
        // Node has no right child and so the right subtree black depth
        // count is 1 since leaves are black.
        right_count += 1;
    }

    assert (left_count == right_count);
    // Need to only test one path since they are equal.
    return count_is_black(node) + left_count; 
}

/*--------------------------------------------------------------------------------------
 * assert_node_values_in_between_child - Asserts the red black tree validity condition that
 *      a given nodes left child has a value less than node and the right child has a value
 *      greater than node.
 * 
 * node: A ptr to an rb_node to compare its value to its children and also check the children's subtrees. [INPUT]
 *--------------------------------------------------------------------------------------*/  
static void assert_node_value_in_between_children(struct rb_node* node)
{
    if (node == NULL)
    {
        return;
    }

    if (has_left_child(node))
    {
        assert(node->value > node->left->value);
        assert_node_value_in_between_children(node->left);
    }
    if (has_right_child(node))
    {
        assert(node->value < node->right->value);
        assert_node_value_in_between_children(node->right);
    }
}

/*--------------------------------------------------------------------------------------
 * assert_rb_tree_is_valid - Checks all the validity conditions for a valid red black tree.
 * 
 * tree: A ptr to an rb_tree to check for validity. [INPUT]
 *--------------------------------------------------------------------------------------*/  
static void assert_rb_tree_is_valid(struct rb_tree* tree)
{
    assert(is_black(tree->root));
    assert_node_has_no_adjacent_red(tree->root);
    assert_tree_pathes_have_equal_black_depths(tree->root);
    assert_node_value_in_between_children(tree->root);
}

/*--------------------------------------------------------------------------------------
 * shuffle - Shuffles an array of integers randomly.
 * 
 * array: A ptr to the array to shuffle. [OUTPUT]
 * length: The length of the int array to shuffle. [INPUT]
 *--------------------------------------------------------------------------------------*/  
static void shuffle(uint32_t* array, int length)
{
    for (int i = 0; i < length; i++)
    {
        int j = i + rand() / (RAND_MAX / (length - i) + 1);
        int temp = array[j];
        array[j] = array[i];
        array[i] = temp;
    }
}

/******************************************************************************
 RB_TREE TESTS
 ******************************************************************************/

/*--------------------------------------------------------------------------------------
 * test_new_tree_empty - 
 *--------------------------------------------------------------------------------------*/  
static void test_new_tree_empty()
{
    rb_tree* tree = create_rb_tree(0);
    assert(is_empty(tree));
    assert(is_full(tree));
    assert(tree->root == NULL);
    delete_rb_tree(tree);
}

/*--------------------------------------------------------------------------------------
 * test_unable_to_insert_into_empty_tree - 
 *--------------------------------------------------------------------------------------*/  
static void test_unable_to_insert_into_empty_tree()
{
    rb_tree* tree = create_rb_tree(0);
    rb_node* tail_start = tree->free_node_tail;
    assert(is_full(tree));
    assert(!insert(0, tree));
    assert(tree->root == NULL); 
    // Ensure that no blocks have been allocated.
    assert(tail_start == tree->free_node_tail);
    delete_rb_tree(tree);
}

/*--------------------------------------------------------------------------------------
 * test_unable_to_insert_into_full_tree - 
 *--------------------------------------------------------------------------------------*/  
static void test_unable_to_insert_into_full_tree()
{
    rb_tree* tree = create_rb_tree(4);
    assert(tree->size == 0);
    assert(!is_full(tree));
    insert(0, tree);
    assert_rb_tree_is_valid(tree);
    insert(2, tree);
    assert_rb_tree_is_valid(tree);       
    insert(4, tree);
    assert_rb_tree_is_valid(tree);   
    insert(6, tree);
    assert_rb_tree_is_valid(tree);
    assert(tree->size == 4);
    assert(is_full(tree));
    // The final insert should fail when tree is full.
    assert(!insert(8, tree));
    assert_rb_tree_is_valid(tree);
    delete_rb_tree(tree);
}


/*--------------------------------------------------------------------------------------
 * test_deletes_tree -
 *--------------------------------------------------------------------------------------*/  
static void test_deletes_tree()
{
    // Tests deleting a tree with nodes.
    rb_tree* tree = create_rb_tree(5);
    insert(0, tree);
    insert(1, tree);
    insert(2, tree);
    insert(3, tree);
    delete_rb_tree(tree);
    
    // Tests deleting an empty tree.
    tree = create_rb_tree(0);
    delete_rb_tree(tree);
}

/*--------------------------------------------------------------------------------------
 * test_single_insertion -
 *--------------------------------------------------------------------------------------*/  
static void test_insert_root()
{
    rb_tree* tree = create_rb_tree(1);
    insert(5, tree);
    assert_rb_tree_is_valid(tree);
 
    struct rb_node n1 = {.value = 5, .offset = 1, .color=false};
    struct rb_node* nodes[] = {&n1};
    assert_inorder_nodes_are(tree->root, nodes, 1, 0);
    delete_rb_tree(tree);
}

/*--------------------------------------------------------------------------------------
 * test_insert_left_subtree -
 *--------------------------------------------------------------------------------------*/  
static void test_insert_left_subtree()
{
    // Tests a single insertion into the left subtree from root.
    rb_tree* tree = create_rb_tree(4);
    insert(7, tree);
    insert(5, tree);
    assert_rb_tree_is_valid(tree);
    struct rb_node n1 = {.value = 7, .offset = 1, .color = false};
    struct rb_node n2 = {.value = 5, .offset = 1, .color = true};
    struct rb_node* nodes_1[] = {&n2, &n1};
    assert_inorder_nodes_are(tree->root, nodes_1, 2, 0);
    
    // Tests inserting into a left subtree requiring a rebalancing.
    insert(3, tree);
    n1.color = true;
    n2.color = false;
    struct rb_node n3 = {.value = 3, .offset = 1, .color = true};
    struct rb_node* nodes_2[] = {&n3, &n2, &n1}; 
    assert_inorder_nodes_are(tree->root, nodes_2, 3, 0);

    // Tests an insertion into the left subtree when the parent node is not root.
    insert(1, tree); 
    n1.color = false;
    n2.color = false;
    n3.color = false;
    struct rb_node n4 = {.value = 1, .offset = 1, .color = true};
    struct rb_node* nodes_3[] = {&n4, &n3, &n2, &n1};
    assert_inorder_nodes_are(tree->root, nodes_3, 4, 0);
    delete_rb_tree(tree);
 }

/*--------------------------------------------------------------------------------------
 * test_insert_right_subtree -
 *--------------------------------------------------------------------------------------*/  
static void test_insert_right_subtree()
{
    // Tests a single insertion into the right subtree from root.
    rb_tree* tree = create_rb_tree(4);
    insert(1, tree);
    insert(3, tree);
    assert_rb_tree_is_valid(tree);
    struct rb_node n1 = {.value = 1, .offset = 1, .color = false};
    struct rb_node n2 = {.value = 3, .offset = 1, .color = true};
    struct rb_node* nodes_1[] = {&n1, &n2};
    assert_inorder_nodes_are(tree->root, nodes_1, 2, 0);
    
    // Tests inserting into a right subtree requiring a rebalancing.
    insert(5, tree);
    n1.color = true;
    n2.color = false;
    struct rb_node n3 = {.value = 5, .offset = 1, .color = true};
    struct rb_node* nodes_2[] = {&n1, &n2, &n3}; 
    assert_inorder_nodes_are(tree->root, nodes_2, 3, 0);

    // Tests an insertion into the right subtree when the parent node is not root.
    insert(7, tree); 
    n1.color = false;
    n2.color = false;
    n3.color = false;
    struct rb_node n4 = {.value = 7, .offset = 1, .color = true};
    struct rb_node* nodes_3[] = {&n1, &n2, &n3, &n4};
    assert_inorder_nodes_are(tree->root, nodes_3, 4, 0);
    delete_rb_tree(tree);
}

/*--------------------------------------------------------------------------------------
 * test_insert_merge_lower -
 *--------------------------------------------------------------------------------------*/  
static void test_insert_merge_lower()
{
    rb_tree* tree = create_rb_tree(3);
    insert(5, tree);
    insert(2, tree);
    insert(10, tree);
     
    struct rb_node n1 = {.value =  2, .offset = 1, .color = true};
    struct rb_node n2 = {.value =  5, .offset = 1, .color = false};
    struct rb_node n3 = {.value = 10, .offset = 1, .color = true};
    struct rb_node* nodes[] = {&n1, &n2, &n3};
    
    assert_rb_tree_is_valid(tree);
    assert_inorder_nodes_are(tree->root, nodes, 3, 0);
    
    // After inserting the following values all node values in the tree should be replaced
    // with the corresponding consecutive number that is lower. The offset should also be
    // incremented to indicate that each node stores multiple values.
    insert(4, tree);
    insert(1, tree);
    insert(9, tree);
    insert(8, tree);
    insert(7, tree);
    insert(0, tree);
   
    n1.value = 0;
    n1.offset = 3;

    n2.value = 4;
    n2.offset = 2;

    n3.value = 7;
    n3.offset = 4;

    assert_rb_tree_is_valid(tree);
    assert_inorder_nodes_are(tree->root, nodes, 3, 0);
    delete_rb_tree(tree);
}

/*--------------------------------------------------------------------------------------
 * test_insert_merge_upper -
 *--------------------------------------------------------------------------------------*/  
static void test_insert_merge_upper()
{
    rb_tree* tree = create_rb_tree(3);
    insert(5, tree);
    insert(2, tree);
    insert(10, tree);
     
    struct rb_node n1 = {.value =  2, .offset = 1, .color = true};
    struct rb_node n2 = {.value =  5, .offset = 1, .color = false};
    struct rb_node n3 = {.value = 10, .offset = 1, .color = true};
    struct rb_node* nodes[] = {&n1, &n2, &n3};
    
    assert_rb_tree_is_valid(tree);
    assert_inorder_nodes_are(tree->root, nodes, 3, 0);
    
    // After inserting the following values all node values in the tree should increase
    // have increased offsets reflecting the fact that the node stores a range of inserted
    // values.
    insert(6, tree);
    insert(7, tree);
    insert(3, tree);
    insert(11, tree);
    insert(12, tree);
    insert(13, tree);
    insert(14, tree);
    insert(15, tree);
   
    n1.offset = 2;
    n2.offset = 3;
    n3.offset = 6;

    assert_rb_tree_is_valid(tree);
    assert_inorder_nodes_are(tree->root, nodes, 3, 0);
    delete_rb_tree(tree);
}

/*--------------------------------------------------------------------------------------
 * test_insert_merge_lower_and_child -
 *--------------------------------------------------------------------------------------*/  
static void test_insert_merge_lower_and_child()
{
    rb_tree* tree = create_rb_tree(7);
    insert(20, tree);
    insert(15, tree);
    insert(25, tree);
    insert(10, tree);
    insert(30, tree);
    insert(5, tree);
    insert(35, tree);
 
    struct rb_node n1 = {.value =  5, .offset = 1, .color = true};
    struct rb_node n2 = {.value = 10, .offset = 1, .color = false}; 
    struct rb_node n3 = {.value = 15, .offset = 1, .color = true}; 
    struct rb_node n4 = {.value = 20, .offset = 1, .color = false};  
    struct rb_node n5 = {.value = 25, .offset = 1, .color = true};  
    struct rb_node n6 = {.value = 30, .offset = 1, .color = false}; 
    struct rb_node n7 = {.value = 35, .offset = 1, .color = true}; 
    struct rb_node* nodes_1[] = {&n1, &n2, &n3, &n4, &n5, &n6, &n7};

    assert_rb_tree_is_valid(tree);
    assert_inorder_nodes_are(tree->root, nodes_1, 7, 0);

    insert(11, tree);
    insert(12, tree);
    insert(13, tree);
    insert(14, tree);


    n2.offset = 6;    
    struct rb_node* nodes_2[] = {&n1, &n2, &n4, &n5, &n6, &n7};

    assert_rb_tree_is_valid(tree);
    assert_inorder_nodes_are(tree->root, nodes_2, 6, 0);
    delete_rb_tree(tree);
}

/*--------------------------------------------------------------------------------------
 * test_insert_merge_upper_and_child -
 *--------------------------------------------------------------------------------------*/  
static void test_insert_merge_upper_and_child()
{
    rb_tree* tree = create_rb_tree(4);
    insert(20, tree);
    insert(10, tree);
    insert(28, tree);
    insert(30, tree);
    
    struct rb_node n1 = {.value = 10, .offset = 1, .color = false};
    struct rb_node n2 = {.value = 20, .offset = 1, .color = false}; 
    struct rb_node n3 = {.value = 28, .offset = 1, .color = false}; 
    struct rb_node n4 = {.value = 30, .offset = 1, .color = true};  
    struct rb_node* nodes_1[] = {&n1, &n2, &n3, &n4};

    assert_rb_tree_is_valid(tree);
    assert_inorder_nodes_are(tree->root, nodes_1, 4, 0);

    insert(29, tree);
    
    n3.offset = 3;
    struct rb_node* nodes_2[] = {&n1, &n2, &n3};

    assert_rb_tree_is_valid(tree);
    assert_inorder_nodes_are(tree->root, nodes_2, 3, 0);
    delete_rb_tree(tree);
}

/*--------------------------------------------------------------------------------------
 * test_merge_to_single_node -
 *--------------------------------------------------------------------------------------*/  
static void test_merge_to_single_node()
{
    rb_tree* tree = create_rb_tree(10);
    insert(1, tree);
    assert_rb_tree_is_valid(tree);
    insert(3, tree);
    assert_rb_tree_is_valid(tree);
    insert(5, tree);
    assert_rb_tree_is_valid(tree);
    insert(7, tree);
    assert_rb_tree_is_valid(tree);
    insert(9, tree);
    assert_rb_tree_is_valid(tree);
    insert(11, tree);
    assert_rb_tree_is_valid(tree);
    insert(13, tree);
    assert_rb_tree_is_valid(tree);
    insert(15, tree);
    assert_rb_tree_is_valid(tree);
    insert(12, tree);
    assert_rb_tree_is_valid(tree);
    insert(8, tree);
    assert_rb_tree_is_valid(tree);
    insert(4, tree);
    assert_rb_tree_is_valid(tree);
    insert(14, tree);
    assert_rb_tree_is_valid(tree);
    insert(2, tree);
    assert_rb_tree_is_valid(tree);
    insert(6, tree);
    assert_rb_tree_is_valid(tree);
    insert(10, tree);
 
    struct rb_node final_node = {.value = 1, .offset = 15, .color = false};
    struct rb_node* nodes[] = {&final_node};

    assert_rb_tree_is_valid(tree);
    assert_inorder_nodes_are(tree->root, nodes, 1, 0);
    delete_rb_tree(tree);
}

/*--------------------------------------------------------------------------------------
 * test_no_duplicates -
 *--------------------------------------------------------------------------------------*/  
static void test_no_duplicates()
{
    rb_tree* tree = create_rb_tree(10);
    insert(5, tree);
    insert(10, tree);
    insert(15, tree);
    struct rb_node n1 = {.value =  5, .offset = 1, .color = true};
    struct rb_node n2 = {.value = 10, .offset = 1, .color = false}; 
    struct rb_node n3 = {.value = 15, .offset = 1, .color = true};
    struct rb_node* nodes[] = {&n1, &n2, &n3};

   
    assert(tree->size == 3);
    assert_rb_tree_is_valid(tree); 
    assert_inorder_nodes_are(tree->root, nodes, 3, 0);

    assert(!insert(5, tree));
    assert(!insert(5, tree));
    assert(!insert(10, tree));
    assert(!insert(10, tree));
    assert(!insert(15, tree));
    assert(!insert(15, tree));
 
    assert(tree->size == 3);
    assert_rb_tree_is_valid(tree); 
    assert_inorder_nodes_are(tree->root, nodes, 3, 0);
    delete_rb_tree(tree); 
}

/*--------------------------------------------------------------------------------------
 * test_random_stress -
 *--------------------------------------------------------------------------------------*/  
static void test_random_stress()
{
    int number_trees = 10;
    uint32_t max_bundles = 16000; 
    struct rb_node n = {.value = 0, .offset = max_bundles, .color = false};
    struct rb_node* nodes[] = {&n};

    uint32_t bundle_ids[max_bundles];
    for (int i = 0; i < max_bundles; i++)
    {
        bundle_ids[i] = i;
    }

    for (int i = 0; i < number_trees; i++)
    {
        struct rb_tree* tree = create_rb_tree(max_bundles);
        shuffle(bundle_ids, max_bundles);
        for (int j = 0; j < max_bundles; j++)
        {
            insert(bundle_ids[j], tree);
            assert_rb_tree_is_valid(tree);
        }
        assert_inorder_nodes_are(tree->root, nodes, 1, 0);
        delete_rb_tree(tree);
    }
}

/*--------------------------------------------------------------------------------------
 * run_tests - Run all rb_tree tests. 
 *--------------------------------------------------------------------------------------*/  
static void run_rb_tree_tests()
{
    printf("Running RB Tree Tests...\n");
    test_new_tree_empty();
    test_unable_to_insert_into_empty_tree();
    test_unable_to_insert_into_full_tree();
    test_deletes_tree();
    test_insert_root();
    test_insert_left_subtree();
    test_insert_right_subtree();
    test_insert_merge_lower();
    test_insert_merge_upper();
    test_insert_merge_lower_and_child();
    test_insert_merge_upper_and_child();
    test_merge_to_single_node();
    test_no_duplicates();
    test_random_stress();
    printf("All tests passed!\n");
}

int main() {    
    run_rb_tree_tests();
}

#endif // RBTREETESTS