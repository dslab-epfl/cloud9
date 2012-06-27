/*
 * Cloud9 Parallel Symbolic Execution Engine
 *
 * Copyright (c) 2011, Dependable Systems Laboratory, EPFL
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Dependable Systems Laboratory, EPFL nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE DEPENDABLE SYSTEMS LABORATORY, EPFL BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * All contributors are listed in CLOUD9-AUTHORS file.
 *
*/

#ifndef EXECUTIONTREE_H_
#define EXECUTIONTREE_H_

#include <cassert>
#include <cstring>
#include <vector>
#include <stack>
#include <set>
#include <algorithm>
#include <iostream>
#include <string>

#include "cloud9/Protocols.h"
#include "cloud9/ExecutionPath.h"

namespace cloud9 {

template<class, int, int>
class TreeNode;


/* An adaptation of boost::intrusive_ptr */
template<class NodeType>
class NodePin {
private:
    typedef NodePin this_type;

public:
    NodePin(int layer) :
        p_(0), layer_(layer) {
    }

    NodePin(NodeType * p, int layer) :
        p_(p), layer_(layer) {
        if (p_ != 0)
          node_pin_add_ref(p_, layer_);
    }

    NodePin(NodePin const & rhs) :
        p_(rhs.p_), layer_(rhs.layer_) {
        if (p_ != 0)
          node_pin_add_ref(p_, layer_);
    }

    ~NodePin() {
        if (p_ != 0)
            node_pin_release(p_, layer_);
    }

    NodePin & operator=(NodePin const & rhs) {
        this_type(rhs).swap(*this);
        return *this;
    }

    NodeType * get() const {
        return p_;
    }

    int layer() const {
        return layer_;
    }

    void reset() {
        this_type(layer_).swap(*this);
    }

    NodeType & operator*() const {
        assert( p_ != 0 );
        return *p_;
    }

    NodeType * operator->() const {
        assert( p_ != 0 );
        return p_;
    }

    bool operator!() const {
        return p_ == 0;
    }

    typedef NodeType * NodePin::*unspecified_bool_type;

    operator unspecified_bool_type() const {
        return p_ == 0 ? 0 : &this_type::p_;
    }

    void swap(NodePin & rhs) {
        assert(layer_ == rhs.layer_);

        NodeType * tmp = p_;
        p_ = rhs.p_;
        rhs.p_ = tmp;
    }

private:

    NodeType * p_;
    int layer_;
};

template<class T, class U> inline bool operator==(NodePin<T> const & a, NodePin<U> const & b)
{
    return a.get() == b.get();
}

template<class T, class U> inline bool operator!=(NodePin<T> const & a, NodePin<U> const & b)
{
    return a.get() != b.get();
}

template<class T, class U> inline bool operator==(NodePin<T> const & a, U * b)
{
    return a.get() == b;
}

template<class T, class U> inline bool operator!=(NodePin<T> const & a, U * b)
{
    return a.get() != b;
}

template<class T, class U> inline bool operator==(T * a, NodePin<U> const & b)
{
    return a == b.get();
}

template<class T, class U> inline bool operator!=(T * a, NodePin<U> const & b)
{
    return a != b.get();
}

template<class T> inline bool operator<(NodePin<T> const & a, NodePin<T> const & b)
{
    return std::less<T *>()(a.get(), b.get());
}


template<class NodeInfo, int Layers, int Degree>
class TreeNode {
    template<class, int, int>
    friend class ExecutionTree;

    template<class NI, int L, int D>
    friend void node_pin_add_ref(TreeNode<NI, L, D> * p, int layer);

    template<class NI, int L, int D>
    friend void node_pin_release(TreeNode<NI, L, D> * p, int layer);

public:
    typedef NodePin<TreeNode<NodeInfo, Layers, Degree> > Pin;

    typedef TreeNode<NodeInfo, Layers, Degree> *ptr;
private:
    ptr childrenNodes[Degree];      // Pointers to the children of the node
    ptr parent;             // Pointer to the parent of the node

    unsigned int level;             // Node level in the tree
    unsigned int index;             // The index of the child in the parent children vector

    unsigned int count[Layers];     // The number of children per each layer
    unsigned int totalCount;        // The total number of children (used for internal ref-counting)

    bool children[Degree][Layers];
    bool exists[Layers];    // Whether the current node exists on a specific layer

    unsigned int _label;    // Internal

    /*
     * Basically, the difference between count and _refCount is that count keeps
     * track of the internal references (pointers from other nodes), while
     * _refCount keeps track of external, persistent references to the node.
     *
     * The reason of doing this is to control the destruction of nodes in cascade
     * and avoid performance issues when the tree grows very large.
     */
    unsigned int _refCount[Layers];
    unsigned int _totalRefCount;

    NodeInfo *_info;

    /*
     * Creates a new node and connects it in position "index" in a parent
     * node
     */
    TreeNode(TreeNode* p, int index) :
        parent(p), totalCount(0), _label(0), _totalRefCount(0) {

        memset(childrenNodes, 0, Degree*sizeof(TreeNode*));
        memset(children, 0, Degree*Layers*sizeof(bool));
        memset(count, 0, Layers*sizeof(unsigned int));
        memset(_refCount, 0, Layers*sizeof(unsigned int));
        memset(exists, 0, Layers*sizeof(bool));

        _info = new NodeInfo();

        if (p != NULL) {
            p->childrenNodes[index] = this;

            level = p->level + 1;
            this->index = index;
        } else {
            level = 0;
            this->index = 0;
        }
    }

    void incCount(int layer) { count[layer]++; totalCount++; }
    void decCount(int layer) { count[layer]--; totalCount--; }

    void _incRefCount(int layer) { _refCount[layer]++; _totalRefCount++; }
    void _decRefCount(int layer) { _refCount[layer]--; _totalRefCount--; }

    void makeNode(int layer) {
        assert(!exists[layer]);

        if (parent != NULL) {
            assert(!parent->children[index][layer]);

            parent->children[index][layer] = true;
            parent->incCount(layer);
        }

        exists[layer] = true;
    }

    void clearNode(int layer) {
        assert(exists[layer]);

        exists[layer] = false;

        if (parent != NULL) {
            assert(parent->children[index][layer]);

            parent->children[index][layer] = false;
            parent->decCount(layer);
        }
    }
public:

    virtual ~TreeNode() { }

    ptr getParent() const { return parent; }


    ptr getChild(int layer, int index) const {
        assert(exists[layer]);

        if (children[index][layer])
            return childrenNodes[index];
        else
            return NULL;
    }

    ptr getLeft(int layer) const {
        return getChild(layer, 0);
    }

    ptr getRight(int layer) const {
        return getChild(layer, Degree-1);
    }

    int getLevel() const { return level; }
    int getIndex() const { return index; }
    int getCount(int layer) const { return count[layer]; }
    int getTotalCount() const { return totalCount; }

    bool layerExists(int layer) const { return exists[layer]; }
    bool isLeaf(int layer) const { return count[layer] == 0; }


    NodeInfo& operator*() {
        return *_info;
    }

    const NodeInfo& operator*() const {
        return *_info;
    }

    Pin pin(int layer) {
        assert(exists[layer]);
        return Pin(this, layer);
    }

};

template<class NodeInfo, int Layers, int Degree>
class ExecutionTree {
    template<class NI, int L, int D>
    friend void node_pin_release(TreeNode<NI, L, D> * p, int layer);

#define BEGIN_LAYERED_DFS_SCAN(layer, root, node) \
        std::stack<Node*> __dfs_nodes; \
        __dfs_nodes.push((root)); \
        while (!__dfs_nodes.empty()) { \
            Node *node = __dfs_nodes.top(); \
            __dfs_nodes.pop();

#define END_LAYERED_DFS_SCAN(layer, root, node) \
            for (int __i = 0; __i < Degree; __i++) { \
                Node *__child = node->getChild((layer), __i); \
                if (__child) \
                    __dfs_nodes.push(__child); \
            } \
        }

#define BEGIN_DFS_SCAN(root, node) \
        std::stack<Node*> __dfs_nodes; \
        __dfs_nodes.push((root)); \
        while (!__dfs_nodes.empty()) { \
          Node *node = __dfs_nodes.top(); \
          __dfs_nodes.pop();

#define END_DFS_SCAN(root, node) \
          for (int __i = 0; __i < Degree; __i++) { \
            Node *__child = node->childrenNodes[__i]; \
            if (__child) \
              __dfs_nodes.push(__child); \
          } \
        }

public:
    typedef TreeNode<NodeInfo, Layers, Degree> Node;
    typedef typename TreeNode<NodeInfo, Layers, Degree>::Pin NodePin;
private:
    typedef std::map<std::string, std::string> deco_t;
    typedef std::vector<std::pair<Node*, deco_t> > edge_deco_t;

private:
    Node* root;

    Node *getNode(int layer, ExecutionPath *p, Node* root, int pos) {
        Node *crtNode = root;
        assert(root->exists[layer]);

        if (p->parent != NULL) {
            crtNode = getNode(layer, p->parent, root, p->parentIndex);
        }

        for (ExecutionPath::path_iterator it = p->path.begin();
                it != p->path.end(); it++) {

            if (pos == 0)
                return crtNode;

            Node *newNode = getNode(layer, crtNode, *it);

            crtNode = newNode;
            pos--;
        }

        return crtNode;
    }

    static void removeSupportingBranch(int layer, Node *node, Node *root) {
        // Checking for node->parent ensures that we will never delete the
        // root node
      std::vector<NodeInfo*> tailDeletions;
      while (node->parent && node != root) {
        if (node->count[layer] > 0 || node->_refCount[layer] > 0) // Stop when joining another branch, or hitting the job root
          break;

        Node *temp = node;
        node = node->parent;
        NodeInfo* info = removeNode(layer, temp);
        if (info)
          tailDeletions.push_back(info);
      }

      for (typename std::vector<NodeInfo*>::iterator it = tailDeletions.begin();
          it != tailDeletions.end(); it++) {
        delete *it;
      }
    }

    static NodeInfo *removeNode(int layer, Node *node) {
      NodeInfo *result = NULL;
      assert(node->count[layer] == 0);
      assert(node->_refCount[layer] == 0);

      node->clearNode(layer);

      if (node->totalCount == 0 && node->_totalRefCount == 0) {
        assert(node->parent);
        node->parent->childrenNodes[node->index] = NULL;
        result = node->_info;
        delete node; // Clean it for good, nobody references it anymore
      }

      return result;
    }

    template<typename NodeIterator>
    void cleanupLabels(NodeIterator begin, NodeIterator end) {

      for (NodeIterator it = begin; it != end; it++) {
          Node *crtNode = *it;

          while (crtNode != root) {
              if (crtNode->_label == 0)
                  break;
              else {
                  crtNode->_label = 0;
                  crtNode = crtNode->parent;
              }
          }
      }
    }

public:
    ExecutionTree() {
        root = new Node(NULL, 0);

        // Create a root node in each layer
        for (int layer = 0; layer < Layers; layer++)
            root->makeNode(layer);
    }

    virtual ~ExecutionTree() { }

    Node* getRoot() const {
        return root;
    }

    Node *getNode(int layer, ExecutionPathPin p) {
        return getNode(layer, p.get(), root, p->path.size());
    }

    Node *getNode(int layer, Node *root, int index) {
        assert(root->exists[layer]);

        Node *result = root->childrenNodes[index];

        if (result == NULL)
            result = new Node(root, index);

        if (!root->children[index][layer])
            result->makeNode(layer);

        return result;
    }

    Node *getNode(int layer, Node *node) {
        Node *crtNode = node;
        while (!crtNode->exists[layer]) {
            crtNode->makeNode(layer);
            crtNode = crtNode->parent;
        }

        return node;
    }

    // WARNING: Expensive operation, use it with caution
    void collapseNode(Node *node) {
      int index = -1;

      // Check that the operation is valid
      assert(node->_totalRefCount == 0);

      for (int i = 0; i < Degree; i++) {
        if (node->childrenNodes[i] != NULL) {
          assert(index < 0);
          index = i;
        }
      }
      assert(index >= 0);

      for (int layer = 0; layer < Layers; layer++) {
        assert((!node->parent) || (!node->exists[layer]) || node->children[index][layer]);
      }

      // Perform the pointer manipulation
      Node *parent = node->parent;
      Node *child = node->childrenNodes[index];

      child->parent = parent;
      child->index = node->index;

      BEGIN_DFS_SCAN(child, descendant)
      descendant->level = descendant->level - 1;
      END_DFS_SCAN(child, descendant)

      if (parent != NULL)
        parent->childrenNodes[node->index] = child;
      else {
        root = child;

        for (int layer = 0; layer < Layers; layer++) {
          if (!root->exists[layer])
            root->makeNode(layer);
        }
      }

      delete node;
    }

    unsigned int countLeaves(int layer, Node *root) {
        assert(root->exists[layer]);
        unsigned int result = 0;

        BEGIN_LAYERED_DFS_SCAN(layer, root, node)

        if (node->isLeaf(layer))
            result++;

        END_LAYERED_DFS_SCAN(layer, root, node)

        return result;
    }

    template<class Predicate>
    unsigned int countLeaves(int layer, Node *root, Predicate pred) {
        assert(root->exists[layer]);
        unsigned int result = 0;

        BEGIN_LAYERED_DFS_SCAN(layer, root, node)

        if (node->isLeaf(layer) && pred(node))
            result++;

        END_LAYERED_DFS_SCAN(layer, root, node)

        return result;
    }

    template<typename NodeCollection>
    void getLeaves(int layer, Node *root, NodeCollection &nodes) {
        assert(root->exists[layer]);

        BEGIN_LAYERED_DFS_SCAN(layer, root, node)

        if (node->isLeaf(layer))
            nodes.push_back(node);

        END_LAYERED_DFS_SCAN(layer, root, node)
    }

    template<typename NodeCollection, typename Predicate>
    void getLeaves(int layer, Node *root, Predicate pred, unsigned int maxCount, NodeCollection &nodes) {
        assert(root->exists[layer]);
        bool unlimited = (maxCount == 0);

        BEGIN_LAYERED_DFS_SCAN(layer, root, node)

        if (node->isLeaf(layer) && pred(node)) {
            if (unlimited || maxCount > 0)
                nodes.push_back(node);

            if (!unlimited) {
                if (maxCount > 0)
                    maxCount--;
                else
                    break;
            }
        }

        END_LAYERED_DFS_SCAN(layer, root, node)
    }

    template<class Generator>
    Node* selectRandomLeaf(int layer, Node *root, Generator &gen,
        int layerMask = 0) {

      assert(root->exists[layer]);
      Node *crtNode = root;
      int crtCount;

      while (1) {
        Node *candidate = NULL;
        if (!layerMask)
          crtCount = crtNode->getCount(layer);
        else {
          crtCount = 0;
          for (int i = 0; i < Degree; i++) {
            Node *child = crtNode->getChild(layer, i);
            if (child && child->exists[layerMask]) {
              candidate = child;
              crtCount++;
            }
          }
        }

        if (!crtCount)
          break;
        if (layerMask && crtCount == 1) {
          crtNode = candidate;
          continue;
        }

        int index = gen.getInt32() % crtCount;
        for (int i = 0; i < Degree; i++) {
          Node *child = crtNode->getChild(layer, i);
          if (child && (!layerMask || child->exists[layerMask])) {
            if (index == 0) {
              crtNode = child;
              break;
            } else {
              index--;
            }
          }
        }
      }

      return crtNode;

    }

    template<class Generator, typename NodeIterator>
    Node *selectRandomLeaf(int layer, Node *root, Generator &gen,
        NodeIterator begin, NodeIterator end) {

      assert(root->exists[layer]);

      for (NodeIterator it = begin; it != end; it++) {
        Node *crtNode = *it;
        assert(crtNode->exists[layer]);

        while (crtNode != root) {
          if (crtNode->_label == 1)
            break;

          crtNode->_label = 1;
          crtNode = crtNode->parent;
        }
      }

      Node *crtNode = root;

      while(1) {
        int crtCount = 0;
        Node *candidate = NULL;
        for (int i = 0; i < Degree; i++) {
          Node *child = crtNode->getChild(layer, i);
          if (child && child->_label == 1) {
            candidate = child;
            crtCount++;
          }
        }

        if (!crtCount)
          break;
        if (crtCount == 1) {
          crtNode = candidate;
          continue;
        }

        int index = gen.getInt32() % crtCount;
        for (int i = 0; i < Degree; i++) {
          Node *child = crtNode->getChild(layer, i);
          if (child && child->_label == 1) {
            if (!index) {
              crtNode = child;
              break;
            } else {
              index--;
            }
          }
        }
      }

      cleanupLabels(begin, end);

      return crtNode;
    }

    template<typename NodeIterator, typename NodeMap>
    ExecutionPathSetPin buildPathSet(NodeIterator begin, NodeIterator end,
        NodeMap *encodeMap) {
        ExecutionPathSet *set = new ExecutionPathSet();

        std::vector<Node*> processed; // XXX: Require a random access iterator

        int i = 0;
        for (NodeIterator it = begin; it != end; it++) {
            Node* crtNode = *it;

            ExecutionPath *path = new ExecutionPath();

            ExecutionPath *p = NULL;
            int pIndex = 0;

            while (crtNode != root) {
                if (crtNode->_label > 0) {
                    // We hit an already built path
                    p = set->paths[crtNode->_label - 1];
                    pIndex = p->path.size() -
                            (processed[crtNode->_label - 1]->level - crtNode->level);
                    break;
                } else {
                    path->path.push_back(crtNode->index);
                    crtNode->_label = i + 1;

                    crtNode = crtNode->parent;
                }
            }

            std::reverse(path->path.begin(), path->path.end());
            path->parent = p;
            path->parentIndex = pIndex;

            if (encodeMap) {
              (*encodeMap)[*it] = set->paths.size();
            }

            set->paths.push_back(path);
            processed.push_back(*it);
            i++;
        }

        cleanupLabels(begin, end);

        return ExecutionPathSetPin(set);
    }

    void buildPath(int layer, Node *node, Node *pathRoot, std::vector<int> &path) {
      assert(node->layerExists(layer));
      path.clear();

      while (node != pathRoot) {
        path.push_back(node->getIndex());
        node = node->getParent();
      }

      std::reverse(path.begin(), path.end());
    }

    template<typename NodeCollection, typename NodeMap>
    void getNodes(int layer, ExecutionPathSetPin pathSet, NodeCollection &nodes,
        NodeMap *decodeMap) {
        nodes.clear();

        for (unsigned i = 0; i < pathSet->paths.size(); i++) {
            Node *crtNode = getNode(layer, pathSet->paths[i], root, pathSet->paths[i]->path.size());
            nodes.push_back(crtNode);
            if (decodeMap) {
              (*decodeMap)[i] = crtNode;
            }
        }
    }

    template<typename Decorator>
    void dumpDotGraph(Node *root, std::ostream &os, Decorator decorator) {
      if (!root)
        root = this->root;

      std::map<Node*, std::string> names;
      std::stack<std::pair<Node*, std::string> > namesStack;
      namesStack.push((std::make_pair(root, "r")));

      while (!namesStack.empty()) {
        std::pair<Node*, std::string> node = namesStack.top();
        namesStack.pop();
        names[node.first] = node.second;

        for (int i = 0; i < Degree; i++) {
          Node *child = node.first->childrenNodes[i];
          if (child) {
            std::string name(node.second);
            name.push_back('0' + i);
            namesStack.push(std::make_pair(child, name));
          }
        }
      }

      // Write the Dot header
      os << "digraph symbex {" << std::endl;

      std::stack<Node*> nodesStack;
      nodesStack.push(root);

      while (!nodesStack.empty()) {
        Node *node = nodesStack.top();
        nodesStack.pop();

        deco_t deco;
        edge_deco_t edges;
        decorator(node, deco, edges);

        os << "  " << names[node] << " [";

         for (deco_t::iterator it = deco.begin(); it != deco.end(); it++) {
           if (it != deco.begin())
             os << ",";
           os << it->first << "=" << it->second;
         }

         os << "];" << std::endl;

         for (int i = 0; i < Degree; i++) {
           Node *child = node->childrenNodes[i];
           if (child) {
             nodesStack.push(child);
           }
         }

         for (typename edge_deco_t::iterator it = edges.begin(); it != edges.end(); it++) {

           os << "  " << names[it->first] << " -> " << names[node];
           if (it->second.size() > 0) {
             os << " [";
             for (deco_t::iterator eit = it->second.begin(); eit != it->second.end(); eit++) {
               if (eit != it->second.begin())
                 os << ",";
               os << eit->first << "=" << eit->second;
             }
             os << "]";
           }
           os << ";" << std::endl;
         }
       }

      // Write the Dot footer
      os << "}" << std::endl;
    }

#undef BEGIN_LAYERED_DFS_SCAN
#undef END_LAYERED_DFS_SCAN

#undef BEGIN_DFS_SCAN
#undef END_DFS_SCAN

}; // End of ExecutionTree class


template<class Node>
class DotNodeDefaultDecorator {
public:
  typedef std::map<std::string, std::string> deco_t;
  typedef std::vector<std::pair<Node*, deco_t> > edge_deco_t;
private:
  int fillLayer;
  int peripheryLayer;
  Node *highlight;
public:
  DotNodeDefaultDecorator(int _fillLayer, int _peripheryLayer, Node *_highlight)
    : fillLayer(_fillLayer), peripheryLayer(_peripheryLayer), highlight(_highlight) { }
  virtual ~DotNodeDefaultDecorator() { }

  virtual void operator() (Node *node, deco_t &deco, edge_deco_t &inEdges) {
    deco["label"] = "\"\"";
    deco["shape"] = "circle";
    deco["width"] = "0.4";

    if (node->layerExists(fillLayer)) {
      deco["style"] = "filled";
      if (node->isLeaf(fillLayer)) {
        deco["fillcolor"] = "gray25";
        deco["width"] = "0.7";
      } else {
        deco["fillcolor"] = "gray75";
      }
    }

    if (node->layerExists(peripheryLayer)) {
      deco["penwidth"] = "3";
      if (node->isLeaf(peripheryLayer)) {
        deco["shape"] = "square";
      }
    }
    if (node == highlight) {
      deco["color"] = "red";
      deco["shape"] = "square";
    }
  }
};


template<class NI, int L, int D>
void getASCIINode(const TreeNode<NI, L, D> &node, std::string &result) {
    result.push_back('<');

    const TreeNode<NI, L, D> *crtNode = &node;

    while (crtNode->getParent() != NULL) {
        result.push_back(crtNode->getIndex() ? '1' : '0');

        crtNode = crtNode->getParent();
    }

    result.push_back('>');

    std::reverse(result.begin() + 1, result.end() - 1);
}

template<class NI, int L, int D>
std::ostream& operator<<(std::ostream &os,
        const TreeNode<NI, L, D> &node) {

    std::string str;
    getASCIINode(node, str);
    os << str;

    return os;
}

template<class NodeType>
std::ostream& operator<<(std::ostream &os, const NodePin<NodeType> &pin) {
    os << *(pin.get());
    return os;
}

template<class NI, int L, int D>
void node_pin_add_ref(TreeNode<NI, L, D> *p, int layer) {
    assert(p);

    p->_incRefCount(layer);

    //if (layer == 0) CLOUD9_DEBUG("New inc ref count " << p->_refCount[0] << " for node " << *p);
}

template<class NI, int L, int D>
void node_pin_release(TreeNode<NI, L, D> *p, int layer) {
  assert(p);
  assert(p->_refCount[layer] > 0);

  p->_decRefCount(layer);

  //if (layer == 0) CLOUD9_DEBUG("New dec ref count " << p->_refCount[0] << " for node " << *p);

  if (p->_refCount[layer] == 0) {
    ExecutionTree<NI, L, D>::removeSupportingBranch(layer, p, NULL);
  }
}

#if 1 // XXX: debug
#include <boost/lexical_cast.hpp>

template<typename NodeIterator>
std::string getASCIINodeSet(NodeIterator begin, NodeIterator end) {
    std::string result;
    bool first = true;

    result.push_back('[');

    for (NodeIterator it = begin; it != end; it++) {
        if (!first)
            result.append(", ");
        else
            first = false;

        std::string nodeStr;
        getASCIINode(**it, nodeStr);

        result.append(nodeStr);
    }

    result.push_back(']');

    return result;
}

template<typename DataIterator>
std::string getASCIIDataSet(DataIterator begin, DataIterator end) {
    std::string result;

    bool first = true;
    result.push_back('[');

    for (DataIterator it = begin; it != end; it++) {
        if (!first)
            result.append(", ");
        else
            first = false;

        result.append(boost::lexical_cast<std::string>(*it));
    }

    result.push_back(']');

    return result;
}

#endif

}

#endif /* EXECUTIONTREE_H_ */
