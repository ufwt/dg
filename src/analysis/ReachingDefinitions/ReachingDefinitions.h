#ifndef _DG_REACHING_DEFINITIONS_ANALYSIS_H_
#define _DG_REACHING_DEFINITIONS_ANALYSIS_H_

#include <vector>
#include <set>
#include <cassert>
#include <cstring>

#include "analysis/SubgraphNode.h"
#include "analysis/PointsTo/PointerSubgraph.h"
#include "analysis/Offset.h"

#include "ADT/Queue.h"
#include "RDMap.h"

namespace dg {
namespace analysis {
namespace rd {

class RDNode;
class ReachingDefinitionsAnalysis;

// here the types are for type-checking (optional - user can do it
// when building the graph) and for later optimizations
enum class RDNodeType {
        // for backward compatibility
        NONE,
        // these are nodes that just represent memory allocation sites
        // we need to have them even in reaching definitions analysis,
        // so that we can use them as targets in DefSites
        ALLOC,
        STORE,
        DYN_ALLOC,
        PHI,
        // return from the subprocedure
        RETURN,
        // call node
        CALL,
        // return from the call (in caller)
        CALL_RETURN,
        // dummy nodes
        NOOP
};

extern RDNode *UNKNOWN_MEMORY;

class RDNode : public SubgraphNode<RDNode> {
    RDNodeType type;

    // marks for DFS/BFS
    unsigned int dfsid;
public:

    RDNode(RDNodeType t = RDNodeType::NONE) : type(t), dfsid(0) {}

    // this is the gro of this node, so make it public
    DefSiteSetT defs;
    // this is a subset of defs that are strong update
    // on this node
    DefSiteSetT overwrites;

    RDMap def_map;

    RDNodeType getType() const { return type; }
    DefSiteSetT& getDefines() { return defs; }
    DefSiteSetT& getOverwrites() { return overwrites; }
    const DefSiteSetT& getDefines() const { return defs; }

    bool defines(RDNode *target, const Offset& off = UNKNOWN_OFFSET) const
    {
        // FIXME: this is not efficient implementation,
        // use the ordering on the nodes
        // (see old DefMap.h in llvm/)
        if (off.isUnknown()) {
            for (const DefSite& ds : defs)
                if (ds.target == target)
                    return true;
        } else {
            for (const DefSite& ds : defs)
                if (ds.target == target
                    && off.inRange(*ds.offset, *ds.offset + *ds.len))
                    return true;
        }

        return false;
    }

    void addDef(const DefSite& ds, bool strong_update = false)
    {
        defs.insert(ds);
        def_map.update(ds, this);

        // XXX maybe we could do it by some flag in DefSite?
        // instead of strong new copy... but it should not
        // be big overhead this way... we'll see in the future
        if (strong_update)
            overwrites.insert(ds);
    }

    void addDef(RDNode *target,
                const Offset& off = UNKNOWN_OFFSET,
                const Offset& len = UNKNOWN_OFFSET,
                bool strong_update = false)
    {
        addDef(DefSite(target, off, len), strong_update);
    }

    void addOverwrites(RDNode *target,
                       const Offset& off = UNKNOWN_OFFSET,
                       const Offset& len = UNKNOWN_OFFSET)
    {
        addOverwrites(DefSite(target, off, len));
    }

    void addOverwrites(const DefSite& ds)
    {
        overwrites.insert(ds);
    }

    const RDMap& getReachingDefinitions() const { return def_map; }
    RDMap& getReachingDefinitions() { return def_map; }
    size_t getReachingDefinitions(RDNode *n, const Offset& off,
                                  const Offset& len, std::set<RDNode *>& ret)
    {
        return def_map.get(n, off, len, ret);
    }

    bool isUnknown() const
    {
        return this == UNKNOWN_MEMORY;
    }


    friend class ReachingDefinitionsAnalysis;
};

class ReachingDefinitionsAnalysis
{
    RDNode *root;
    unsigned int dfsnum;
    bool strong_update_unknown;
    uint32_t max_set_size;

public:
    ReachingDefinitionsAnalysis(RDNode *r,
                                bool field_insens = false,
                                uint32_t max_set_sz = ~((uint32_t)0))
    : root(r), dfsnum(0), strong_update_unknown(field_insens), max_set_size(max_set_sz)
    {
        assert(r && "Root cannot be null");
        // with max_set_size == 0 (everything is defined on unknown location)
        // we get unsound results with vararg functions and similar weird stuff
        assert(max_set_size > 0 && "The set size must be at least 1");
    }

    void getNodes(std::set<RDNode *>& cont)
    {
        assert(root && "Do not have root");

        ++dfsnum;

        ADT::QueueLIFO<RDNode *> lifo;
        lifo.push(root);
        root->dfsid = dfsnum;

        while (!lifo.empty()) {
            RDNode *cur = lifo.pop();
            cont.insert(cur);

            for (RDNode *succ : cur->successors) {
                if (succ->dfsid != dfsnum) {
                    succ->dfsid = dfsnum;
                    lifo.push(succ);
                }
            }
        }
    }

    // get nodes in BFS order and store them into
    // the container
    std::vector<RDNode *> getNodes(RDNode *start_node = nullptr,
                                   std::vector<RDNode *> *start_set = nullptr,
                                   unsigned expected_num = 0)
    {
        assert(root && "Do not have root");
        assert(!(start_set && start_node)
               && "Need either starting set or starting node, not both");

        ++dfsnum;
        ADT::QueueFIFO<RDNode *> fifo;

        if (start_set) {
            for (RDNode *s : *start_set) {
                fifo.push(s);
                s->dfsid = dfsnum;
            }
        } else {
            if (!start_node)
                start_node = root;

            fifo.push(start_node);
            start_node->dfsid = dfsnum;
        }

        std::vector<RDNode *> cont;
        if (expected_num != 0)
            cont.reserve(expected_num);

        while (!fifo.empty()) {
            RDNode *cur = fifo.pop();
            cont.push_back(cur);

            for (RDNode *succ : cur->successors) {
                if (succ->dfsid != dfsnum) {
                    succ->dfsid = dfsnum;
                    fifo.push(succ);
                }
            }
        }

        return cont;
    }


    RDNode *getRoot() const { return root; }
    void setRoot(RDNode *r) { root = r; }

    bool processNode(RDNode *n);
    void run();
};

} // namespace rd
} // namespace analysis
} // namespace dg

#endif //  _DG_REACHING_DEFINITIONS_ANALYSIS_H_
