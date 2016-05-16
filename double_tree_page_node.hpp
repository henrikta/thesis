// A tree based node that fits in a memory page

#pragma once
#include "double_tree_line_node.hpp"
#include "extract.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>

namespace double_tree
{

namespace detail
{

// Define here the size in bytes of, as well as the types used to index inside
// them.  A page node index should be able to index all values in an array of
// line nodes of size equal to the page node.  See below for the exact
// requirements, which are a little less because of the size used up for
// bookkeeping data in each case

constexpr size_t page_node_size = 4096;
using page_index = uint8_t;
constexpr page_index page_index_nil = std::numeric_limits<page_index>::max();

// An index into a page node combined with an index into the line node at that
// index constitutes a position of an element in the page node

struct page_position
{
    page_position() = default;

    page_position(const page_position& other):
        line{other.line},
        elem{other.elem}
    {}

    page_position(page_index line_, line_index elem_):
        line{line_},
        elem{elem_}
    {}

    bool operator==(const page_position& other) const
    {
        return line == other.line && elem == other.elem;
    }

    bool operator!=(const page_position& other) const
    {
        return line != other.line || elem != other.elem;
    }

    page_index line;
    line_index elem;
};


template <
    typename Element,
    typename ElementWrite,
    typename Key,
    typename KeyExtract,
    typename ValExtract,
    typename Aux>
struct alignas(page_node_size) PageNode
{
    // Auxiliary structures for the line nodes.  A stem node does not need any
    // extra data, while the leaf nodes of a page are linked together in a
    // linked list, so they need the index of their previous and next nodes
    struct stem_aux
    {};

    struct leaf_aux
    {
        page_index prev_index;
        page_index next_index;
    };

    // The actual node types can now be defined
    using stem_node = line_node<
        std::pair<const Key, page_index>,
        std::pair<Key, page_index>,
        Key,
        extract::first,
        extract::second,
        stem_aux>;
    using leaf_node = line_node<
        Element,
        ElementWrite,
        Key,
        KeyExtract,
        ValExtract,
        leaf_aux>;

    // MEMORY SYSTEM

    // An entry in the pool memory.  Each can hold either a stem node, a leaf
    // node or a page index.  The page index is only used within the memory
    // system
    struct alignas(line_node_size) PoolEntry
    {
        PoolEntry() {}

        union {
            stem_node stem;
            leaf_node leaf;
            page_index prev_head;
        };
    };

    // Calculate how many entries the pool memory can hold
    static constexpr int pool_size =
        page_node_size - 6*sizeof(page_index) - 1 - sizeof(Aux);
    static constexpr int pool_count = pool_size/sizeof(PoolEntry);

    // The pool memory.  The back index points at the highest entry that has
    // never been allocated.  The head index points at the next entry to
    // allocate, and is either equal to the back index or lower.  If it is
    // equal the next head index is found by incrementing.  If it is lower it
    // points to an entry that has been deallocated, and when that was done the
    // previous head index was stored there, and we restore it.
    page_index allocate() {
        assert(free_count > 0);
        --free_count;
        page_index allocate_index = head_index;
        if (head_index == back_index) { head_index = ++back_index; }
        else { head_index = pool_memory[head_index].prev_head; }
        return allocate_index;
    }

    void deallocate(page_index deallocate_index) {
        ++free_count;
        pool_memory[deallocate_index].prev_head = head_index;
        head_index = deallocate_index;
    }

    // SMALL, LARGE, OVERSIZED NODES

    static constexpr int branchout = stem_node::max_count;

    // Here n is the pool entries left and b is the total branchout of the
    // previous level.  By subtracting b from n and multiplying b by the
    // branchout until we cover the rest of the nodes we figure out how many
    // levels of stem nodes we will maximally need
    static constexpr int max_stem_levels_helper(int n, int b) {
        return (n > b) ? 1 + max_stem_levels_helper(n - b, b*branchout) : 0;
    }
    static constexpr int max_stem_levels = max_stem_levels_helper(pool_count, 1);
    static constexpr int max_levels = max_stem_levels + 1;

    bool small() const {
        return free_count > 2*max_levels - 1;
    }

    bool large() const {
        return free_count <= 2*max_levels - 1;
    }

    bool oversized() const {
        return free_count <= max_levels - 1;
    }

    // DATA

    std::array<PoolEntry, pool_count> pool_memory;
    page_index head_index;
    page_index back_index;
    page_index free_count;

    page_index root_index;
    page_index min_leaf_index;
    page_index max_leaf_index;
    uint8_t stem_levels;
    KeyExtract key_extract_;
    Aux aux;

    // CONSTRUCTOR

    PageNode()
    : head_index{0},
      back_index{0},
      free_count{pool_count},
      root_index{allocate()},
      min_leaf_index{root_index},
      max_leaf_index{root_index},
      stem_levels{0}
    {
        auto& root = get_leaf(root_index);
        root.init();
        root.aux.prev_index = page_index_nil;
        root.aux.next_index = page_index_nil;
    }

    // ACCESSORS

    stem_node& get_stem(const page_index index) {
        return pool_memory[index].stem;
    }
    leaf_node& get_leaf(const page_index index) {
        return pool_memory[index].leaf;
    }

    const stem_node& get_stem(const page_index index) const {
        return pool_memory[index].stem;
    }
    const leaf_node& get_leaf(const page_index index) const {
        return pool_memory[index].leaf;
    }

    const Key& key(const page_position position) const {
        return get_leaf(position.line).key(position.elem);
    }
    Element& elem(const page_position position) {
        return get_leaf(position.line).elem(position.elem);
    }
    const Element& elem(const page_position position) const {
        return get_leaf(position.line).elem(position.elem);
    }

    void set_key(const page_position position, const Key& new_key) {
        const Key old_key = get_leaf(position.line).key(position.elem);
        get_leaf(position.line).set_key(position.elem, new_key);
        if (position.elem == 0) {
            const auto path = find_path(old_key);
            update_key(
                path, stem_levels - 1, path[stem_levels - 1].elem, new_key);
        }
    }

    page_position min_position() const {
        return {min_leaf_index, get_leaf(min_leaf_index).min_index()};
    }
    page_position max_position() const {
        return {max_leaf_index, get_leaf(max_leaf_index).max_index()};
    }
    page_position end_position() const {
        return {max_leaf_index,
            (line_index)(get_leaf(max_leaf_index).max_index() + 1)};
    }

    page_position prev_position(const page_position position) const {
        const leaf_node& node = get_leaf(position.line);
        if (node.aux.prev_index != page_index_nil &&
            position.elem == node.min_index())
        {
            return {node.aux.prev_index,
                get_leaf(node.aux.prev_index).max_index()};
        }
        else
        {
            return {position.line, (line_index)(position.elem - 1)};
        }
    }

    page_position next_position(const page_position position) const {
        const leaf_node& node = get_leaf(position.line);
        if (node.aux.next_index != page_index_nil &&
            position.elem == node.max_index())
        {
            return {node.aux.next_index,
                get_leaf(node.aux.next_index).min_index()};
        }
        else
        {
            return {position.line, (line_index)(position.elem + 1)};
        }
    }

    const Key& min_key() const {
        return get_leaf(min_leaf_index).min_key();
    }

    const Element& min_elem() const {
        return get_leaf(min_leaf_index).min_elem();
    }

    const Key& max_key() const {
        return get_leaf(max_leaf_index).max_key();
    }

    const Element& max_elem() const {
        return get_leaf(max_leaf_index).max_elem();
    }

    // PREDICATES

    bool empty() const {
        return stem_levels == 0 && get_leaf(root_index).empty();
    }

    // OPERATIONS

    // Returns the position of the greatest key less than or equal to the one
    // given, or the minimum position if all keys are greater than the one
    // given
    page_position find(const Key& find_key) const
    {
        auto search_index = root_index;
        for (int depth = 0; depth < stem_levels; ++depth)
        {
            const auto& search_stem = get_stem(search_index);
            search_index = search_stem.elem(search_stem.find(find_key)).second;
        }
        const auto& search_leaf = get_leaf(search_index);
        return {search_index, search_leaf.find(find_key)};
    }

private:
    // This structure is used to record a path down the tree to some specific
    // element
    using Path = std::array<page_position, max_levels>;

    // Construct the path taken to find the key given
    Path find_path(const Key& find_key) const
    {
        Path result;
        auto search_index = root_index;
        for (int depth = 0; depth < stem_levels; ++depth)
        {
            const auto& search_stem = get_stem(search_index);
            result[depth].line = search_index;
            result[depth].elem = search_stem.find(find_key);
            search_index = search_stem.elem(result[depth].elem).second;
        }
        const auto& search_leaf = get_leaf(search_index);
        result[stem_levels].line = search_index;
        result[stem_levels].elem = search_leaf.find(find_key);
        return result;
    }

    // Construct the leftmost path down the stem
    Path min_path() const
    {
        // Record the leftmost path down the stem.
        Path result;
        auto search_index = root_index;
        for (int depth = 0; depth < stem_levels; ++depth)
        {
            const auto& search_stem = get_stem(search_index);
            result[depth].line = search_index;
            result[depth].elem = search_stem.min_index();
            search_index = search_stem.elem(result[depth].elem).second;
        }
        const auto& search_leaf = get_leaf(search_index);
        result[stem_levels].line = search_index;
        result[stem_levels].elem = search_leaf.min_index();
        return result;
    }

    // Construct the rightmost path down the stem
    Path max_path() const
    {
        // Record the leftmost path down the stem.
        Path result;
        auto search_index = root_index;
        for (int depth = 0; depth < stem_levels; ++depth)
        {
            const auto& search_stem = get_stem(search_index);
            result[depth].line = search_index;
            result[depth].elem = search_stem.max_index();
            search_index = search_stem.elem(result[depth].elem).second;
        }
        const auto& search_leaf = get_leaf(search_index);
        result[stem_levels].line = search_index;
        result[stem_levels].elem = search_leaf.max_index();
        return result;
    }

    void split_root()
    {
        if (stem_levels > 0)
        {
            const auto old_root_index = root_index;
            auto& old_root = get_stem(old_root_index);

            if (old_root.full())
            {
                const auto new_index = allocate();
                auto& new_stem = get_stem(new_index);
                old_root.split(new_stem);

                root_index = allocate();
                auto& new_root = get_stem(root_index);
                new_root.init();
                new_root.insert({old_root.min_key(), old_root_index});
                new_root.insert({new_stem.min_key(), new_index});

                ++stem_levels;
            }
        }
        else
        {
            const auto old_root_index = root_index;
            auto& old_root = get_leaf(old_root_index);

            if (old_root.full())
            {
                const auto new_index = allocate();
                auto& new_leaf = get_leaf(new_index);
                old_root.split(new_leaf);

                old_root.aux.next_index = new_index;
                new_leaf.aux.prev_index = old_root_index;
                new_leaf.aux.next_index = page_index_nil;
                max_leaf_index = new_index;

                root_index = allocate();
                auto& new_root = get_stem(root_index);
                new_root.init();
                new_root.insert({old_root.min_key(), old_root_index});
                new_root.insert({new_leaf.min_key(), new_index});

                ++stem_levels;
            }
        }
    }

public:
    // Insert a new element
    void insert(const Element& new_elem)
    {
        split_root();

        const auto& new_key = key_extract_(new_elem);

        if (stem_levels > 0)
        {
            auto current_index = root_index;
            for (int depth = 0; depth < stem_levels - 1; ++depth)
            {
                auto& current_stem = get_stem(current_index);

                const auto target_pos = current_stem.find(new_key);
                const auto target_index = current_stem.elem(target_pos).second;
                auto& target_stem = get_stem(target_index);

                if (new_key < target_stem.min_key()) {
                    current_stem.set_key(target_pos, new_key);
                }

                // Split target stem?
                if (target_stem.full()) {
                    const auto new_index = allocate();
                    auto& new_stem = get_stem(new_index);
                    target_stem.split(new_stem);

                    current_stem.insert({new_stem.min_key(), new_index});

                    if (new_key >= new_stem.min_key()) {
                        current_index = new_index;
                    } else {
                        current_index = target_index;
                    }
                } else {
                    current_index = target_index;
                }
            }

            auto& current_stem = get_stem(current_index);

            const auto target_pos = current_stem.find(new_key);
            const auto target_index = current_stem.elem(target_pos).second;
            auto& target_leaf = get_leaf(target_index);

            if (new_key < target_leaf.min_key()) {
                current_stem.set_key(target_pos, new_key);
            }

            // Split target leaf?
            if (target_leaf.full()) {
                const auto new_index = allocate();
                auto& new_leaf = get_leaf(new_index);
                target_leaf.split(new_leaf);

                current_stem.insert({new_leaf.min_key(), new_index});

                // Adjust the linked leaf list to fit in the new leaf
                if (target_leaf.aux.next_index != page_index_nil) {
                    get_leaf(target_leaf.aux.next_index).aux.prev_index =
                        new_index;
                }
                new_leaf.aux.next_index = target_leaf.aux.next_index;
                new_leaf.aux.prev_index = target_index;
                target_leaf.aux.next_index = new_index;

                // If the target leaf was the max leaf, the new max is the new
                // leaf
                if (max_leaf_index == target_index) {
                    max_leaf_index = new_index;
                }

                if (new_key >= new_leaf.min_key()) {
                    new_leaf.insert(new_elem);
                } else {
                    target_leaf.insert(new_elem);
                }
            } else {
                target_leaf.insert(new_elem);
            }
        }
        else
        {
            auto& current_leaf = get_leaf(root_index);
            current_leaf.insert(new_elem);
        }
    }

    void insert_min_leaf(const Key new_min_key, const page_index new_index)
    {
        if (stem_levels > 0)
        {
            split_root();

            auto current_index = root_index;
            for (int depth = 0; depth < stem_levels - 1; ++depth)
            {
                auto& current_stem = get_stem(current_index);
                const auto target_pos = current_stem.min_index();
                const auto target_index = current_stem.elem(target_pos).second;
                auto& target_stem = get_stem(target_index);
                current_stem.set_key(target_pos, new_min_key);

                // Split target stem?
                if (target_stem.full()) {
                    const auto new_index = allocate();
                    auto& new_stem = get_stem(new_index);
                    target_stem.split(new_stem);
                    current_stem.insert({new_stem.min_key(), new_index});
                }

                current_index = target_index;
            }

            auto& current_stem = get_stem(current_index);
            current_stem.insert({new_min_key, new_index});
        }
        else
        {
            const auto old_root_index = root_index;
            auto& old_root = get_leaf(old_root_index);

            root_index = allocate();
            auto& new_root = get_stem(root_index);
            new_root.init();
            new_root.insert({new_min_key, new_index});
            new_root.insert({old_root.min_key(), old_root_index});

            ++stem_levels;
        }
    }

    void insert_max_leaf(const Key new_min_key, const page_index new_index)
    {
        if (stem_levels > 0)
        {
            split_root();

            auto current_index = root_index;
            for (int depth = 0; depth < stem_levels - 1; ++depth)
            {
                auto& current_stem = get_stem(current_index);
                const auto target_pos = current_stem.max_index();
                const auto target_index = current_stem.elem(target_pos).second;
                auto& target_stem = get_stem(target_index);

                // Split target stem?
                if (target_stem.full()) {
                    const auto new_index = allocate();
                    auto& new_stem = get_stem(new_index);
                    target_stem.split(new_stem);
                    current_stem.insert({new_stem.min_key(), new_index});
                    current_index = new_index;
                } else {
                    current_index = target_index;
                }
            }

            auto& current_stem = get_stem(current_index);
            current_stem.insert({new_min_key, new_index});
        }
        else
        {
            const auto old_root_index = root_index;
            auto& old_root = get_leaf(old_root_index);

            root_index = allocate();
            auto& new_root = get_stem(root_index);
            new_root.init();
            new_root.insert({old_root.min_key(), old_root_index});
            new_root.insert({new_min_key, new_index});

            ++stem_levels;
        }
    }

    // Erase an element.  The page might be left thin in which case the caller
    // should decide what to do about that
    void erase(const Key& erase_key)
    {
        const auto path = find_path(erase_key);

        const auto line = path[stem_levels].line;
        const auto elem = path[stem_levels].elem;
        auto& erase_leaf = get_leaf(line);

        // If the node is not root and thin, we must either merge or borrow
        if (stem_levels > 0 && erase_leaf.thin())
        {
            // The parent node, and index of the node in the parent
            const auto parent_line_index = path[stem_levels - 1].elem;

            // Do we have a previous sibling?
            if (erase_leaf.aux.prev_index != page_index_nil)
            {
                const auto prev_index = erase_leaf.aux.prev_index;
                auto& prev_leaf = get_leaf(prev_index);

                // Can we merge?
                if (erase_leaf.count() + prev_leaf.count()
                    <= leaf_node::max_count)
                {
                    // Merge the leaf with the element into its previous
                    // sibling
                    erase_leaf.merge_prev_erase(elem, prev_leaf);

                    // Adjust the linked leaf list to erase the merged node
                    if (erase_leaf.aux.next_index != page_index_nil) {
                        get_leaf(erase_leaf.aux.next_index).aux.prev_index =
                            prev_index;
                    }
                    prev_leaf.aux.next_index = erase_leaf.aux.next_index;

                    // If the erased node was the max leaf, the new max is the
                    // previous sibling
                    if (max_leaf_index == line) {
                        max_leaf_index = prev_index;
                    }

                    deallocate(line);

                    // Now we must erase the erased node from the stem
                    // structure
                    erase_node(path, stem_levels - 1, parent_line_index);
                }
                // If we can not merge, borrow
                else
                {
                    erase_leaf.borrow_prev_erase(elem, prev_leaf);

                    // Since the node with the element has a new minimum
                    // element we must update the representative keys up the
                    // tree
                    update_key(path, stem_levels - 1,
                        parent_line_index, erase_leaf.min_key());
                }
            }
            // If we do not have a previous sibling, we should have a next
            else
            {
                const auto next_index = erase_leaf.aux.next_index;
                auto& next_leaf = get_leaf(next_index);

                // Can we merge?
                if (erase_leaf.count() + next_leaf.count()
                    <= leaf_node::max_count)
                {
                    // Merge the next sibling into the leaf with the element
                    erase_leaf.merge_next_erase(elem, next_leaf);

                    // Adjust the linked leaf list to erase the merged node
                    if (next_leaf.aux.next_index != page_index_nil) {
                        get_leaf(next_leaf.aux.next_index).aux.prev_index =
                            line;
                    }
                    erase_leaf.aux.next_index = next_leaf.aux.next_index;

                    // If the erased node was the max leaf, the new max is the
                    // leaf with the element in it
                    if (max_leaf_index == next_index) {
                        max_leaf_index = line;
                    }

                    deallocate(next_index);

                    // If we erased the minimum element we must update the
                    // representative keys up the tree
                    if (elem == 0) {
                        update_key(path, stem_levels - 1,
                            parent_line_index, erase_leaf.min_key());
                    }

                    // Now we must erase the erased node from its parent
                    erase_node(path, stem_levels - 1, parent_line_index + 1);
                }
                // If we can not merge, borrow
                else
                {
                    erase_leaf.borrow_next_erase(elem, next_leaf);

                    // Since the next leaf has a new minimum we must update the
                    // representative keys up the tree
                    update_key(path, stem_levels - 1,
                        parent_line_index + 1, next_leaf.min_key());

                    // If we erased the minimum element we must update the
                    // representative keys up the tree
                    if (elem == 0) {
                        update_key(path, stem_levels - 1,
                            parent_line_index, erase_leaf.min_key());
                    }
                }
            }
        }
        // Either we are the root or the stem to erase from is not thin
        else
        {
            erase_leaf.erase(elem);

            // If we are not the root and we erased the minimum element, we
            // must update the representative keys up the tree
            if (stem_levels > 0 && elem == 0)
            {
                const auto parent_line_index = path[stem_levels - 1].elem;
                update_key(path, stem_levels - 1,
                    parent_line_index, erase_leaf.min_key());
            }
        }
    }

    // Erase a node from the stem structure.  The depth given should be the
    // depth of the node to erase from
    void erase_node(const Path& path, const int depth, const line_index elem)
    {
        const auto line = path[depth].line;
        auto& erase_stem = get_stem(line);

        // If the node is not root and thin, we must either merge or borrow
        if (depth > 0 && erase_stem.thin())
        {
            // The parent node, and index of the node in the parent
            const auto parent_page_index = path[depth - 1].line;
            const auto parent_line_index = path[depth - 1].elem;
            const auto& parent = get_stem(parent_page_index);

            // Do we have a previous sibling?
            if (parent_line_index > 0)
            {
                const auto prev_index =
                    parent.elem(parent_line_index - 1).second;
                auto& prev_stem = get_stem(prev_index);

                // Can we merge?
                if (erase_stem.count() + prev_stem.count()
                    <= stem_node::max_count)
                {
                    // Merge the stem with the node into its previous sibling
                    erase_stem.merge_prev_erase(elem, prev_stem);

                    deallocate(line);

                    // Now we must erase the erased node from the stem
                    // structure
                    erase_node(path, depth - 1, parent_line_index);
                }
                // If we can not merge, borrow
                else
                {
                    erase_stem.borrow_prev_erase(elem, prev_stem);

                    // Since the stem with the node has a new minimum element
                    // we must update the representative keys up the tree
                    update_key(path, depth - 1,
                        parent_line_index, erase_stem.min_key());
                }
            }
            // If we do not have a previous sibling, we should have a next
            else
            {
                const auto next_index =
                    parent.elem(parent_line_index + 1).second;
                auto& next_leaf = get_stem(next_index);

                // Can we merge?
                if (erase_stem.count() + next_leaf.count()
                    <= stem_node::max_count)
                {
                    // Merge the next sibling into the stem with the node
                    erase_stem.merge_next_erase(elem, next_leaf);

                    deallocate(next_index);

                    // If we erased the minimum element we must update the
                    // representative keys up the tree
                    if (elem == 0) {
                        update_key(path, depth - 1,
                            parent_line_index, erase_stem.min_key());
                    }

                    // Now we must erase the erased node from its parent
                    erase_node(path, depth - 1, parent_line_index + 1);
                }
                // If we can not merge, borrow
                else
                {
                    erase_stem.borrow_next_erase(elem, next_leaf);

                    // Since the next stem has a new minimum we must update the
                    // representative keys up the tree
                    update_key(path, depth - 1,
                        parent_line_index + 1, next_leaf.min_key());

                    // If we erased the minimum element we must update the
                    // representative keys up the tree
                    if (elem == 0) {
                        update_key(path, depth - 1,
                            parent_line_index, erase_stem.min_key());
                    }
                }
            }
        }
        // Either we are the root or the stem to erase from is not thin
        else
        {
            erase_stem.erase(elem);

            // If we are not the root and we erased the minimum element, we
            // must update the representative keys up the tree
            if (depth > 0 && elem == 0) {
                const auto parent_line_index = path[depth - 1].elem;
                update_key(path, depth - 1,
                    parent_line_index, erase_stem.min_key());
            }

            // If we are the root and we now have only one child we should
            // collapse this level of the tree
            if (depth == 0 && erase_stem.count() == 1) {
                root_index = erase_stem.min_elem().second;
                deallocate(line);
                --stem_levels;
            }
        }
    }

    void update_key(const Path& path, const int depth,
        const line_index elem, const Key& new_key)
    {
        auto& stem = get_stem(path[depth].line);
        stem.set_key(elem, new_key);
        if (depth > 0 && elem == 0)
        {
            update_key(path, depth - 1, path[depth - 1].elem, new_key);
        }
    }

    void borrow_prev(PageNode& prev_page)
    {
        const auto prev_path = prev_page.max_path();
        const auto old_index = prev_path[prev_page.stem_levels].line;
        auto& old_leaf = prev_page.get_leaf(old_index);

        if (old_leaf.count() < leaf_node::min_count) {
            // TODO: This could be done more efficiently
            for (int i = 0; i < old_leaf.count(); ++i) {
                insert(old_leaf.elem(i));
            }
        } else {
            const auto this_path = min_path();

            // Copy the leaf
            auto new_index = allocate();
            auto& new_leaf = get_leaf(new_index);

            const auto next_index = this_path[stem_levels].line;
            auto& next_leaf = get_leaf(next_index);
            next_leaf.aux.prev_index = new_index;
            new_leaf.aux.prev_index = page_index_nil;
            new_leaf.aux.next_index = next_index;

            new_leaf.init_from(old_leaf);

            min_leaf_index = new_index;

            // Insert into this pages stem structure
            insert_min_leaf(new_leaf.min_key(), new_index);
        }

        // Erase the node from the other pages stem structure
        if (prev_page.stem_levels != 0) {
            prev_page.max_leaf_index = old_leaf.aux.prev_index;
            prev_page.get_leaf(prev_page.max_leaf_index).aux.next_index =
                page_index_nil;
            prev_page.deallocate(old_index);
            prev_page.erase_node(prev_path, prev_page.stem_levels - 1,
                prev_path[prev_page.stem_levels - 1].elem);
        } else {
            old_leaf.reset();
        }
    }

    void borrow_next(PageNode& next_page)
    {
        const auto next_path = next_page.min_path();
        const auto old_index = next_path[next_page.stem_levels].line;
        auto& old_leaf = next_page.get_leaf(old_index);

        if (old_leaf.count() < leaf_node::min_count) {
            // TODO: This could be done more efficiently
            for (int i = 0; i < old_leaf.count(); ++i) {
                insert(old_leaf.elem(i));
            }
        } else {
            const auto this_path = max_path();

            // Copy the leaf
            const auto new_index = allocate();
            auto& new_leaf = get_leaf(new_index);

            const auto prev_index = this_path[stem_levels].line;
            auto& prev_leaf = get_leaf(prev_index);
            prev_leaf.aux.next_index = new_index;
            new_leaf.aux.prev_index = prev_index;
            new_leaf.aux.next_index = page_index_nil;

            new_leaf.init_from(old_leaf);

            max_leaf_index = new_index;

            // Update the keys on the path
            auto new_key = next_page.min_key();
            for (int depth = 0; depth < next_page.stem_levels - 1; ++depth)
            {
                auto& stem = next_page.get_stem(next_path[depth].line);
                stem.set_key(next_path[depth].elem, new_key);
            }

            // Insert into this pages stem structure
            insert_max_leaf(new_leaf.min_key(), new_index);
        }

        // Erase the node from the other pages stem structure
        if (next_page.stem_levels != 0) {
            next_page.min_leaf_index = old_leaf.aux.next_index;
            next_page.get_leaf(next_page.min_leaf_index).aux.prev_index =
                page_index_nil;
            next_page.deallocate(old_index);
            next_page.erase_node(next_path, next_page.stem_levels - 1,
                next_path[stem_levels - 1].elem);
        } else {
            old_leaf.reset();
        }
    }

    PageNode* split_one_leaf()
    {
        const auto this_path = max_path();

        // Copy the leaf
        const auto new_page_pointer = new PageNode;
        auto& new_page = *new_page_pointer;
        auto new_index = new_page.root_index;
        auto& new_leaf = new_page.get_leaf(new_index);

        const auto old_index = this_path[stem_levels].line;
        auto& old_leaf = get_leaf(old_index);
        new_leaf.init_from(old_leaf);

        // Erase the node from this pages stem structure
        if (stem_levels != 0) {
            max_leaf_index = old_leaf.aux.prev_index;
            get_leaf(max_leaf_index).aux.next_index = page_index_nil;
            deallocate(old_index);
            erase_node(this_path, stem_levels - 1,
                this_path[stem_levels - 1].elem);
        } else {
            old_leaf.reset();
        }

        return new_page_pointer;
    }

    void print() const
    {
        print_node(root_index, 0);
    }

    void print_tabs(int num) const
    {
        for (int i = 0; i < num; ++i)
        {
            std::cout << "   ";
        }
    }

    void print_node(page_index line, int depth) const
    {
        if (depth < stem_levels)
        {
            const stem_node& stem = get_stem(line);
            print_tabs(depth);
            std::cout << "stem (" << depth << ") ";
            stem.print();
            for (int i = 0; i < stem.count(); ++i) {
                print_node(stem.elem(i).second, depth + 1);
            }
        }
        else
        {
            const leaf_node& leaf = get_leaf(line);
            print_tabs(depth);
            std::cout << "leaf (" << depth << ") ";
            leaf.print();
        }
    }
};

} // namespace detail

} // namespace double_tree

