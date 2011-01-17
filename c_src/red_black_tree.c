#include "red_black_tree.h"

/***********************************************************************/
/*  FUNCTION:  RBTreeCreate */
/**/
/*  INPUTS:  All the inputs are names of functions.  CompFunc takes to */
/*  void pointers to keys and returns 1 if the first arguement is */
/*  "greater than" the second.   DestFunc takes a pointer to a key and */
/*  destroys it in the appropriate manner when the node containing that */
/*  key is deleted.  InfoDestFunc is similiar to DestFunc except it */
/*  recieves a pointer to the info of a node and destroys it. */
/*  PrintFunc recieves a pointer to the key of a node and prints it. */
/*  PrintInfo recieves a pointer to the info of a node and prints it. */
/*  If RBTreePrint is never called the print functions don't have to be */
/*  defined and NullFunction can be used.  */
/**/
/*  OUTPUT:  This function returns a pointer to the newly created */
/*  red-black tree. */
/**/
/*  Modifies Input: none */
/***********************************************************************/

rb_red_blk_tree* RBTreeCreate( int (*CompFunc) (const void*,const void*),
			      void (*DestFunc) (void*),
			      void (*InfoDestFunc) (void*),
			      void (*PrintFunc) (const void*),
			      void (*PrintInfo)(void*)) {
  rb_red_blk_tree* newTree;
  rb_red_blk_node* temp;

  newTree=(rb_red_blk_tree*) SafeMalloc(sizeof(rb_red_blk_tree));
  newTree->Compare=  CompFunc;
  newTree->DestroyKey= DestFunc;
  newTree->PrintKey= PrintFunc;
  newTree->PrintInfo= PrintInfo;
  newTree->DestroyInfo= InfoDestFunc;

  /*  see the comment in the rb_red_blk_tree structure in red_black_tree.h */
  /*  for information on nil and root */
  temp=newTree->nil= (rb_red_blk_node*) SafeMalloc(sizeof(rb_red_blk_node));
  temp->parent=temp->left=temp->right=temp;
  temp->red=0;
  temp->key=0;
  temp=newTree->root= (rb_red_blk_node*) SafeMalloc(sizeof(rb_red_blk_node));
  temp->parent=temp->left=temp->right=newTree->nil;
  temp->key=0;
  temp->red=0;
  return(newTree);
}

/***********************************************************************/
/*  FUNCTION:  LeftRotate */
/**/
/*  INPUTS:  This takes a tree so that it can access the appropriate */
/*           root and nil pointers, and the node to rotate on. */
/**/
/*  OUTPUT:  None */
/**/
/*  Modifies Input: tree, x */
/**/
/*  EFFECTS:  Rotates as described in _Introduction_To_Algorithms by */
/*            Cormen, Leiserson, Rivest (Chapter 14).  Basically this */
/*            makes the parent of x be to the left of x, x the parent of */
/*            its parent before the rotation and fixes other pointers */
/*            accordingly. */
/***********************************************************************/

void LeftRotate(rb_red_blk_tree* tree, rb_red_blk_node* x) {
  rb_red_blk_node* y;
  rb_red_blk_node* nil=tree->nil;

  /*  I originally wrote this function to use the sentinel for */
  /*  nil to avoid checking for nil.  However this introduces a */
  /*  very subtle bug because sometimes this function modifies */
  /*  the parent pointer of nil.  This can be a problem if a */
  /*  function which calls LeftRotate also uses the nil sentinel */
  /*  and expects the nil sentinel's parent pointer to be unchanged */
  /*  after calling this function.  For example, when RBDeleteFixUP */
  /*  calls LeftRotate it expects the parent pointer of nil to be */
  /*  unchanged. */

  y=x->right;
  x->right=y->left;

  if (y->left != nil) y->left->parent=x; /* used to use sentinel here */
  /* and do an unconditional assignment instead of testing for nil */
  
  y->parent=x->parent;   

  /* instead of checking if x->parent is the root as in the book, we */
  /* count on the root sentinel to implicitly take care of this case */
  if( x == x->parent->left) {
    x->parent->left=y;
  } else {
    x->parent->right=y;
  }
  y->left=x;
  x->parent=y;

#ifdef DEBUG_ASSERT
  Assert(!tree->nil->red,"nil not red in LeftRotate");
#endif
}


/***********************************************************************/
/*  FUNCTION:  RighttRotate */
/**/
/*  INPUTS:  This takes a tree so that it can access the appropriate */
/*           root and nil pointers, and the node to rotate on. */
/**/
/*  OUTPUT:  None */
/**/
/*  Modifies Input?: tree, y */
/**/
/*  EFFECTS:  Rotates as described in _Introduction_To_Algorithms by */
/*            Cormen, Leiserson, Rivest (Chapter 14).  Basically this */
/*            makes the parent of x be to the left of x, x the parent of */
/*            its parent before the rotation and fixes other pointers */
/*            accordingly. */
/***********************************************************************/

void RightRotate(rb_red_blk_tree* tree, rb_red_blk_node* y) {
  rb_red_blk_node* x;
  rb_red_blk_node* nil=tree->nil;

  /*  I originally wrote this function to use the sentinel for */
  /*  nil to avoid checking for nil.  However this introduces a */
  /*  very subtle bug because sometimes this function modifies */
  /*  the parent pointer of nil.  This can be a problem if a */
  /*  function which calls LeftRotate also uses the nil sentinel */
  /*  and expects the nil sentinel's parent pointer to be unchanged */
  /*  after calling this function.  For example, when RBDeleteFixUP */
  /*  calls LeftRotate it expects the parent pointer of nil to be */
  /*  unchanged. */

  x=y->left;
  y->left=x->right;

  if (nil != x->right)  x->right->parent=y; /*used to use sentinel here */
  /* and do an unconditional assignment instead of testing for nil */

  /* instead of checking if x->parent is the root as in the book, we */
  /* count on the root sentinel to implicitly take care of this case */
  x->parent=y->parent;
  if( y == y->parent->left) {
    y->parent->left=x;
  } else {
    y->parent->right=x;
  }
  x->right=y;
  y->parent=x;

#ifdef DEBUG_ASSERT
  Assert(!tree->nil->red,"nil not red in RightRotate");
#endif
}

/***********************************************************************/
/*  FUNCTION:  TreeInsertHelp  */
/**/
/*  INPUTS:  tree is the tree to insert into and z is the node to insert */
/**/
/*  OUTPUT:  none */
/**/
/*  Modifies Input:  tree, z */
/**/
/*  EFFECTS:  Inserts z into the tree as if it were a regular binary tree */
/*            using the algorithm described in _Introduction_To_Algorithms_ */
/*            by Cormen et al.  This funciton is only intended to be called */
/*            by the RBTreeInsert function and not by the user */
/***********************************************************************/

void TreeInsertHelp(rb_red_blk_tree* tree, rb_red_blk_node* z) {
  /*  This function should only be called by InsertRBTree (see above) */
  rb_red_blk_node* x;
  rb_red_blk_node* y;
  rb_red_blk_node* nil=tree->nil;
  
  z->left=z->right=nil;
  y=tree->root;
  x=tree->root->left;
  while( x != nil) {
    y=x;
    if (1 == tree->Compare(x->key,z->key)) { /* x.key > z.key */
      x=x->left;
    } else { /* x,key <= z.key */
      x=x->right;
    }
  }
  z->parent=y;
  if ( (y == tree->root) ||
       (1 == tree->Compare(y->key,z->key))) { /* y.key > z.key */
    y->left=z;
  } else {
    y->right=z;
  }

#ifdef DEBUG_ASSERT
  Assert(!tree->nil->red,"nil not red in TreeInsertHelp");
#endif
}

/*  Before calling Insert RBTree the node x should have its key set */

/***********************************************************************/
/*  FUNCTION:  RBTreeInsert */
/**/
/*  INPUTS:  tree is the red-black tree to insert a node which has a key */
/*           pointed to by key and info pointed to by info.  */
/**/
/*  OUTPUT:  This function returns a pointer to the newly inserted node */
/*           which is guarunteed to be valid until this node is deleted. */
/*           What this means is if another data structure stores this */
/*           pointer then the tree does not need to be searched when this */
/*           is to be deleted. */
/**/
/*  Modifies Input: tree */
/**/
/*  EFFECTS:  Creates a node node which contains the appropriate key and */
/*            info pointers and inserts it into the tree. */
/***********************************************************************/

rb_red_blk_node * RBTreeInsert(rb_red_blk_tree* tree, void* key, void* info) {
  rb_red_blk_node * y;
  rb_red_blk_node * x;
  rb_red_blk_node * newNode;

  x=(rb_red_blk_node*) SafeMalloc(sizeof(rb_red_blk_node));
  x->key=key;
  x->info=info;

  TreeInsertHelp(tree,x);
  newNode=x;
  x->red=1;
  while(x->parent->red) { /* use sentinel instead of checking for root */
    if (x->parent == x->parent->parent->left) {
      y=x->parent->parent->right;
      if (y->red) {
	x->parent->red=0;
	y->red=0;
	x->parent->parent->red=1;
	x=x->parent->parent;
      } else {
	if (x == x->parent->right) {
	  x=x->parent;
	  LeftRotate(tree,x);
	}
	x->parent->red=0;
	x->parent->parent->red=1;
	RightRotate(tree,x->parent->parent);
      } 
    } else { /* case for x->parent == x->parent->parent->right */
      y=x->parent->parent->left;
      if (y->red) {
	x->parent->red=0;
	y->red=0;
	x->parent->parent->red=1;
	x=x->parent->parent;
      } else {
	if (x == x->parent->left) {
	  x=x->parent;
	  RightRotate(tree,x);
	}
	x->parent->red=0;
	x->parent->parent->red=1;
	LeftRotate(tree,x->parent->parent);
      } 
    }
  }
  tree->root->left->red=0;
  return(newNode);

#ifdef DEBUG_ASSERT
  Assert(!tree->nil->red,"nil not red in RBTreeInsert");
  Assert(!tree->root->red,"root not red in RBTreeInsert");
#endif
}

/***********************************************************************/
/*  FUNCTION:  TreeSuccessor  */
/**/
/*    INPUTS:  tree is the tree in question, and x is the node we want the */
/*             the successor of. */
/**/
/*    OUTPUT:  This function returns the successor of x or NULL if no */
/*             successor exists. */
/**/
/*    Modifies Input: none */
/**/
/*    Note:  uses the algorithm in _Introduction_To_Algorithms_ */
/***********************************************************************/
  
rb_red_blk_node* TreeSuccessor(rb_red_blk_tree* tree,rb_red_blk_node* x) { 
  rb_red_blk_node* y;
  rb_red_blk_node* nil=tree->nil;
  rb_red_blk_node* root=tree->root;

  if (nil != (y = x->right)) { /* assignment to y is intentional */
    while(y->left != nil) { /* returns the minium of the right subtree of x */
      y=y->left;
    }
    return(y);
  } else {
    y=x->parent;
    while(x == y->right) { /* sentinel used instead of checking for nil */
      x=y;
      y=y->parent;
    }
    if (y == root) return(0);
    return(y);
  }
}

/***********************************************************************/
/*  FUNCTION:  Treepredecessor  */
/**/
/*    INPUTS:  tree is the tree in question, and x is the node we want the */
/*             the predecessor of. */
/**/
/*    OUTPUT:  This function returns the predecessor of x or NULL if no */
/*             predecessor exists. */
/**/
/*    Modifies Input: none */
/**/
/*    Note:  uses the algorithm in _Introduction_To_Algorithms_ */
/***********************************************************************/

rb_red_blk_node* TreePredecessor(rb_red_blk_tree* tree, rb_red_blk_node* x) {
  rb_red_blk_node* y;
  rb_red_blk_node* nil=tree->nil;
  rb_red_blk_node* root=tree->root;

  if (nil != (y = x->left)) { /* assignment to y is intentional */
    while(y->right != nil) { /* returns the maximum of the left subtree of x */
      y=y->right;
    }
    return(y);
  } else {
    y=x->parent;
    while(x == y->left) { 
      if (y == root) return(nil); 
      x=y;
      y=y->parent;
    }
    return(y);
  }
}

/***********************************************************************/
/*  FUNCTION:  InorderTreePrint */
/**/
/*    INPUTS:  tree is the tree to print and x is the current inorder node */
/**/
/*    OUTPUT:  none  */
/**/
/*    EFFECTS:  This function recursively prints the nodes of the tree */
/*              inorder using the PrintKey and PrintInfo functions. */
/**/
/*    Modifies Input: none */
/**/
/*    Note:    This function should only be called from RBTreePrint */
/***********************************************************************/

void InorderTreePrint(rb_red_blk_tree* tree, rb_red_blk_node* x) {
  rb_red_blk_node* nil=tree->nil;
  rb_red_blk_node* root=tree->root;
  if (x != tree->nil) {
    InorderTreePrint(tree,x->left);
    printf("\r\nkey="); 
    tree->PrintKey(x->key);
    printf("  info=");
    tree->PrintInfo(x->info);
    printf("  l->key=");
    if( x->left == nil) printf("NULL"); else tree->PrintKey(x->left->key);
    printf("  r->key=");
    if( x->right == nil) printf("NULL"); else tree->PrintKey(x->right->key);
    printf("  p->key=");
    if( x->parent == root) printf("NULL"); else tree->PrintKey(x->parent->key);
    printf("  red=%i\n",x->red);
    InorderTreePrint(tree,x->right);
  }
}


/***********************************************************************/
/*  FUNCTION:  TreeDestHelper */
/**/
/*    INPUTS:  tree is the tree to destroy and x is the current node */
/**/
/*    OUTPUT:  none  */
/**/
/*    EFFECTS:  This function recursively destroys the nodes of the tree */
/*              postorder using the DestroyKey and DestroyInfo functions. */
/**/
/*    Modifies Input: tree, x */
/**/
/*    Note:    This function should only be called by RBTreeDestroy */
/***********************************************************************/

void TreeDestHelper(rb_red_blk_tree* tree, rb_red_blk_node* x) {
  rb_red_blk_node* nil=tree->nil;
  if (x != nil) {
    TreeDestHelper(tree,x->left);
    TreeDestHelper(tree,x->right);
    tree->DestroyKey(x->key);
    tree->DestroyInfo(x->info);
    free(x);
  }
}


/***********************************************************************/
/*  FUNCTION:  RBTreeDestroy */
/**/
/*    INPUTS:  tree is the tree to destroy */
/**/
/*    OUTPUT:  none */
/**/
/*    EFFECT:  Destroys the key and frees memory */
/**/
/*    Modifies Input: tree */
/**/
/***********************************************************************/

void RBTreeDestroy(rb_red_blk_tree* tree) {
  TreeDestHelper(tree,tree->root->left);
  free(tree->root);
  free(tree->nil);
  free(tree);
}


/***********************************************************************/
/*  FUNCTION:  RBTreePrint */
/**/
/*    INPUTS:  tree is the tree to print */
/**/
/*    OUTPUT:  none */
/**/
/*    EFFECT:  This function recursively prints the nodes of the tree */
/*             inorder using the PrintKey and PrintInfo functions. */
/**/
/*    Modifies Input: none */
/**/
/***********************************************************************/

void RBTreePrint(rb_red_blk_tree* tree) {
  InorderTreePrint(tree,tree->root->left);
}


/***********************************************************************/
/*  FUNCTION:  RBExactQuery */
/**/
/*    INPUTS:  tree is the tree to print and q is a pointer to the key */
/*             we are searching for */
/**/
/*    OUTPUT:  returns the a node with key equal to q.  If there are */
/*             multiple nodes with key equal to q this function returns */
/*             the one highest in the tree */
/**/
/*    Modifies Input: none */
/**/
/***********************************************************************/
  
rb_red_blk_node* RBExactQuery(rb_red_blk_tree* tree, void* q) {
  rb_red_blk_node* x=tree->root->left;
  rb_red_blk_node* nil=tree->nil;
  int compVal;
  if (x == nil) return(0);
  compVal=tree->Compare(x->key,(int*) q);
  /*XX: printf("C: x->key = %s, q = %s, compVal = %d\r\n", x->key, q, compVal);*/
  while(0 != compVal) {/*assignemnt*/
    if (1 == compVal) { /* x->key > q */
      x=x->left;
    } else {
      x=x->right;
    }
    if ( x == nil) return(0);
    compVal=tree->Compare(x->key,(int*) q);
    /*XX: printf("C: x->key = %s, q = %s, compVal = %d\r\n", x->key, q, compVal);*/
  }
  return(x);
}


/***********************************************************************/
/*  FUNCTION:  RBDeleteFixUp */
/**/
/*    INPUTS:  tree is the tree to fix and x is the child of the spliced */
/*             out node in RBTreeDelete. */
/**/
/*    OUTPUT:  none */
/**/
/*    EFFECT:  Performs rotations and changes colors to restore red-black */
/*             properties after a node is deleted */
/**/
/*    Modifies Input: tree, x */
/**/
/*    The algorithm from this function is from _Introduction_To_Algorithms_ */
/***********************************************************************/

void RBDeleteFixUp(rb_red_blk_tree* tree, rb_red_blk_node* x) {
  rb_red_blk_node* root=tree->root->left;
  rb_red_blk_node* w;

  while( (!x->red) && (root != x)) {
    if (x == x->parent->left) {
      w=x->parent->right;
      if (w->red) {
	w->red=0;
	x->parent->red=1;
	LeftRotate(tree,x->parent);
	w=x->parent->right;
      }
      if ( (!w->right->red) && (!w->left->red) ) { 
	w->red=1;
	x=x->parent;
      } else {
	if (!w->right->red) {
	  w->left->red=0;
	  w->red=1;
	  RightRotate(tree,w);
	  w=x->parent->right;
	}
	w->red=x->parent->red;
	x->parent->red=0;
	w->right->red=0;
	LeftRotate(tree,x->parent);
	x=root; /* this is to exit while loop */
      }
    } else { /* the code below is has left and right switched from above */
      w=x->parent->left;
      if (w->red) {
	w->red=0;
	x->parent->red=1;
	RightRotate(tree,x->parent);
	w=x->parent->left;
      }
      if ( (!w->right->red) && (!w->left->red) ) { 
	w->red=1;
	x=x->parent;
      } else {
	if (!w->left->red) {
	  w->right->red=0;
	  w->red=1;
	  LeftRotate(tree,w);
	  w=x->parent->left;
	}
	w->red=x->parent->red;
	x->parent->red=0;
	w->left->red=0;
	RightRotate(tree,x->parent);
	x=root; /* this is to exit while loop */
      }
    }
  }
  x->red=0;

#ifdef DEBUG_ASSERT
  Assert(!tree->nil->red,"nil not black in RBDeleteFixUp");
#endif
}


/***********************************************************************/
/*  FUNCTION:  RBDelete */
/**/
/*    INPUTS:  tree is the tree to delete node z from */
/**/
/*    OUTPUT:  none */
/**/
/*    EFFECT:  Deletes z from tree and frees the key and info of z */
/*             using DestoryKey and DestoryInfo.  Then calls */
/*             RBDeleteFixUp to restore red-black properties */
/**/
/*    Modifies Input: tree, z */
/**/
/*    The algorithm from this function is from _Introduction_To_Algorithms_ */
/***********************************************************************/

void RBDelete(rb_red_blk_tree* tree, rb_red_blk_node* z){
  rb_red_blk_node* y;
  rb_red_blk_node* x;
  rb_red_blk_node* nil=tree->nil;
  rb_red_blk_node* root=tree->root;

  y= ((z->left == nil) || (z->right == nil)) ? z : TreeSuccessor(tree,z);
  x= (y->left == nil) ? y->right : y->left;
  if (root == (x->parent = y->parent)) { /* assignment of y->p to x->p is intentional */
    root->left=x;
  } else {
    if (y == y->parent->left) {
      y->parent->left=x;
    } else {
      y->parent->right=x;
    }
  }
  if (y != z) { /* y should not be nil in this case */

#ifdef DEBUG_ASSERT
    Assert( (y!=tree->nil),"y is nil in RBDelete\n");
#endif
    /* y is the node to splice out and x is its child */

    if (!(y->red)) RBDeleteFixUp(tree,x);
  
    tree->DestroyKey(z->key);
    tree->DestroyInfo(z->info);
    y->left=z->left;
    y->right=z->right;
    y->parent=z->parent;
    y->red=z->red;
    z->left->parent=z->right->parent=y;
    if (z == z->parent->left) {
      z->parent->left=y; 
    } else {
      z->parent->right=y;
    }
    free(z); 
  } else {
    tree->DestroyKey(y->key);
    tree->DestroyInfo(y->info);
    if (!(y->red)) RBDeleteFixUp(tree,x);
    free(y);
  }
  
#ifdef DEBUG_ASSERT
  Assert(!tree->nil->red,"nil not black in RBDelete");
#endif
}


/***********************************************************************/
/*  FUNCTION:  RBDEnumerate */
/**/
/*    INPUTS:  tree is the tree to look for keys >= low */
/*             and <= high with respect to the Compare function */
/**/
/*    OUTPUT:  stack containing pointers to the nodes between [low,high] */
/**/
/*    Modifies Input: none */
/***********************************************************************/

stk_stack* RBEnumerate(rb_red_blk_tree* tree, void* low, void* high) {
  stk_stack* enumResultStack;
  rb_red_blk_node* nil=tree->nil;
  rb_red_blk_node* x=tree->root->left;
  rb_red_blk_node* lastBest=nil;

  enumResultStack=StackCreate();
  while(nil != x) {
    if ( 1 == (tree->Compare(x->key,high)) ) { /* x->key > high */
      x=x->left;
    } else {
      lastBest=x;
      x=x->right;
    }
  }
  while ( (lastBest != nil) && (1 != tree->Compare(low,lastBest->key))) {
    StackPush(enumResultStack,lastBest);
    lastBest=TreePredecessor(tree,lastBest);
  }
  return(enumResultStack);
}
      
/***********************************************************************/
/*  FUNCTION:  TreeFirst */
/**/
/*    INPUTS:  tree is the tree  */
/**/
/*    OUTPUT:  first node in the tree. */
/**/
/*    Modifies Input: none */
/**/
/***********************************************************************/
  
rb_red_blk_node* TreeFirst(rb_red_blk_tree* tree) {
  rb_red_blk_node* x=tree->root;
  rb_red_blk_node* nil=tree->nil;
  rb_red_blk_node* root=tree->root;

  /*XX: printf("C: TreeFirst line %d tree 0x%lx\r\n", __LINE__, tree);*/
  if (x == nil) return(0);
  /*XX: printf("C: TreeFirst line %d\r\n", __LINE__);*/
  while(x->left != nil) {
      /*XX: printf("C: TreeFirst line %d x->left 0x%lx\r\n", __LINE__, x->left);*/
      x=x->left;
  }
  /*XX: printf("C: TreeFirst line %d x 0x%lx nil 0x%lx\r\n", __LINE__, x, nil);*/
  if (x == root) return(0);
  return(x);
}

int gtEqExistsPredHelper(rb_red_blk_node* node, void* q,
                         rb_red_blk_node* nil, rb_red_blk_tree* tree)
{
    int compVal;

    if (node == nil)
        return(0);
    compVal = tree->Compare(node->key, q);
    if (compVal == 0) {
        return(1);
    } else if (compVal > 0) {
        return 1;
    } else if (compVal < 0 && node->right != nil) {
        return gtEqExistsPredHelper(node->right, q, nil, tree);
    } else {
        return(0);
    }
}

rb_red_blk_node* findSmallestHelper(rb_red_blk_node* node, rb_red_blk_node* nil)
{
    if (node == nil) {
        return(0);
    } else if (node->left == nil) {
        return node;
    } else {
        return findSmallestHelper(node->left, nil);
    }
}

rb_red_blk_node *treeNextHelper(rb_red_blk_node* node, void* q,
                                rb_red_blk_node* nil, rb_red_blk_tree* tree)
{
    int compVal;

    if (node == nil)
        return(0);

    compVal = tree->Compare(node->key, q);
    if (compVal == 0) {
        printf("WIN!  node->key = %s\r\n", node->key);
        return(TreeSuccessor(tree, node));
    } else if (compVal < 0) {  /* node->key < q */
        rb_red_blk_node *next = findSmallestHelper(node->right, nil);
        if (next == 0) {
            return(0);
        } else if (tree->Compare(next->key, q) > 0) {
            return(next);
        } else {
            return(0);
        }
    } else {                   /* node->key > q */
        if (gtEqExistsPredHelper(node->left, q, nil, tree)) {
printf("C: treeNextHelper @ %s true\r\n", node->key);
            return treeNextHelper(node->left, q, nil, tree);
        } else {
printf("C: treeNextHelper @ %s FALSE\r\n", node->key);
            return(node);
        }
    }
#ifdef  SLF_FOO
    if (compVal == 0) {
        return(TreeSuccessor(tree, x));
    } else if (compVal < 0) {  /* x->key < q */
        if (x->right == nil) {
            if (last_choice_was_left_p) {
                return(TreeSucessor(tree, x));
            } else {
                return(0);
            }
        } else {
            return(treeNextHelper(x->right, q, nil, 0));
        }
    } else {                   /* x->key > q */
        if (x->left == nil) {
            if (last_choice_was_left_p) {
                return(x);
            } else {
                return(TreeSuccessor(tree, x));
            }
        } else {
            return(treeNextHelper(node->left, q, nil, 1));
        }
    }
#endif  /* SLF_FOO */
}

/***********************************************************************/
/*  FUNCTION:  TreeNext */
/**/
/*    INPUTS:  tree is the tree, key is a key  */
/**/
/*    OUTPUT:  Next node in the tree where node->key > key. */
/**/
/*    Modifies Input: none */
/**/
/***********************************************************************/
  
rb_red_blk_node* TreeNext(rb_red_blk_tree* tree, void* q)
{
    if (tree->root->left == tree->nil)
        return(0);
    return treeNextHelper(tree->root->left, q, tree->nil, tree);
#ifdef SLF_FOO
    /*
    ** SLF: This is similar to RBEnumerate() but looking for lowest key first,
    ** not the highest key first.
    */
    stk_stack* enumResultStack;
    rb_red_blk_node* nil=tree->nil;
    rb_red_blk_node* x=tree->root->left;
    rb_red_blk_node* lastBest=nil;
    /*XX: rb_red_blk_node* biggest=nil;*/
    rb_red_blk_node* smallest=nil;

    enumResultStack=StackCreate();
    while(nil != x) {
        cmp = 
        StackPush(ResultStack, x);
        /*XX:
        if (biggest == nil || 1 == tree->Compare(x->key, biggest->key)) {
            biggest = x;
        } */
        if (smallest == nil || -1 == tree->Compare(x->key, smallest->key)) {
            smallest = x;
        }
        if ( 1 != (tree->Compare(q, x->key)) ) { /* q <= x->key */
            x=x->right;
        } else {
            x=x->left;
        }
    }
    while ( (lastBest != nil) && (1 != tree->Compare(low,lastBest->key))) {
        StackPush(enumResultStack,lastBest);
        lastBest=TreePredecessor(tree,lastBest);
    }
    return(enumResultStack);
#endif  /* SLF_FOO */
#ifdef SLF_FOO
  rb_red_blk_node* x=tree->root;
  rb_red_blk_node* nil=tree->nil;
  rb_red_blk_node* root=tree->root;
  int cmp;

  if (x == nil) return(0);
  cmp = tree->Compare(x->key, key);
  while (1) {
      if (cmp == 0) break;
      if (cmp < 0)
x->left != nil) {
      x=x->left;
  }
  if (x == root) return(0);
  return(x);
#endif  /* SLF_FOO */
}

#include <string.h>

#define MAX_NAME_SIZE 4567

typedef struct {
    char *name;
    int  val;
} test_t;

int memcmp_101(const char *a, const char *b, int len)
{
    int cmp = memcmp(a, b, len);
    if (cmp < 0)
        return -1;
    else if (cmp == 0)
        return 0;
    else
        return 1;
}

static int test_t_equal(const void *x, const void *y)
{
    const char *a = x, *b = y;
    int cmp, a_len, b_len;

    a_len = strlen(a);
    b_len = strlen(b);
    if (a_len < b_len) {
        cmp = memcmp_101(a, b, a_len);
        if (cmp == 0)
            return -1;
        else
            return cmp;
    } else if (a_len > b_len) {
        cmp = memcmp_101(a, b, b_len);
        if (cmp == 0)
            return 1;
        else
            return cmp;
    } else {
        return memcmp_101(a, b, a_len);
    }
}

static void test_t_destroy_key(void *x)
{
    char *a = x;

    free(a);
}

static void test_t_destroy_info(void *x)
{
    free(x);
}

static void test_t_print_key(const void *x)
{
    const char *a = x;

    printf("key: %s, ", a);
}

static void test_t_print_info(void *x)
{
    test_t *a = x;

    printf("val %d\r\n", a->val);
}

void test_print_node(rb_red_blk_node *node)
{
    test_t_print_key(node->key);
    test_t_print_info(node->info);
}

rb_red_blk_tree* test_new_tree(void)
{
    return RBTreeCreate(test_t_equal,
                        test_t_destroy_key,
                        test_t_destroy_info,
                        test_t_print_key,
                        test_t_print_info); 
}

rb_red_blk_node* test_insert(rb_red_blk_tree *tree, char *key, int val)
{
    rb_red_blk_node *node;

    test_t *t = (test_t *) malloc(sizeof(test_t));
    t->val = val;
    if ((node = RBExactQuery(tree, key)) != NULL) {
        test_t *old_t = (test_t *) node->info;
        t->name = old_t->name;
        node->info = t;
        return node;
    } else {
        t->name = strdup(key);
        return RBTreeInsert(tree, t->name, t);
    }
}

rb_red_blk_node* test_exact_query(rb_red_blk_tree *tree, char *key)
{
    /*XX: printf("C: test_exact_query: %s\r\n", key);*/
    /*XX: RBTreePrint(tree); */
    return RBExactQuery(tree, key);
}

int test_node_is_null(rb_red_blk_node *node)
{
    return node == NULL;
}

test_t test_node_to_test_t(rb_red_blk_node *node)
{
    /*XX: printf("C: node %lu\r\n", node);*/
    /*XX: printf("C: node->info %lu\r\n", node->info);*/
    test_t *n = node->info, res;

    /*XX: printf("C: n->name %s\r\n", n->name);*/
    res.name = n->name;
    /*XX: printf("C: n->val %d\r\n", n->val);*/
    res.val = n->val;
    return res;
}

int test_delete(rb_red_blk_tree *tree, char *key)
{
    rb_red_blk_node *node;

    if ((node = RBExactQuery(tree, key)) != NULL) {
        RBDelete(tree, node);
        return 1;
    } else {
        return 0;
    }
}

rb_red_blk_node* test_get_first(rb_red_blk_tree *tree)
{
    /*XX: printf("C: before print\r\n");*/
    /*XX: RBTreePrint(tree);*/
    /*XX: printf("C: after print\r\n");*/
    return TreeFirst(tree);
}

rb_red_blk_node* test_get_next(rb_red_blk_tree *tree, char *key)
{
    printf("C: before print\r\n");
    RBTreePrint(tree);
    printf("C: after print\r\n");
    return TreeNext(tree, key);
}

