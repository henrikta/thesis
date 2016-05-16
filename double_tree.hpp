#pragma once
#include "double_tree_page_node.hpp"
#include <iterator>
#include <vector>

namespace double_tree
{

namespace detail
{

struct tree_position
{
    tree_position() = default;

    tree_position(const tree_position& other):
        page{other.page},
        sub_position{other.sub_position}
    {}

    tree_position(void* page_, page_position sub_position_):
        page{page_},
        sub_position{sub_position_}
    {}

    bool operator==(const tree_position& other) const
    {
        return page == other.page && sub_position == other.sub_position;
    }

    bool operator!=(const tree_position& other) const
    {
        return page != other.page || sub_position != other.sub_position;
    }

    void* page;
    page_position sub_position;
};

template <
    typename Element,
    typename ElementWrite,
    typename Key,
    typename KeyExtract,
    typename ValExtract>
struct kernel
{
private:
    // Defined below.
    template <typename T> class iterator_template;

    // Auxiliary structures for the page nodes.  A stem node does not need any
    // extra data, while the leaf nodes of a tree are linked together in a
    // linked list, so they need pointers to their previous and next nodes
    struct stem_aux
    {};

    struct leaf_aux
    {
        void* prev_pointer;
        void* next_pointer;
    };

    using stem_node = PageNode<
        std::pair<const Key, void*>,
        std::pair<Key, void*>,
        Key,
        extract::first,
        extract::second,
        stem_aux>;

    using leaf_node = PageNode<
        Element,
        ElementWrite,
        Key,
        KeyExtract,
        ValExtract,
        leaf_aux>;

    // DATA

    void* root_pointer;
    leaf_node* min_leaf_pointer;
    leaf_node* max_leaf_pointer;
    int stem_levels;
    KeyExtract key_extract_;
    ValExtract val_extract_;

public:
    // MEMBER TYPES

    using key_type        = Key;
    using value_type      = Element;
    using reference       = value_type&;
    using const_reference = const value_type&;
    using pointer         = value_type*;
    using const_pointer   = const value_type*;
    using iterator        = iterator_template<value_type>;
    using const_iterator  = iterator_template<const value_type>;

    // CONSTRUCTOR

    kernel()
    : root_pointer{new leaf_node},
      min_leaf_pointer{get_leaf_pointer(root_pointer)},
      max_leaf_pointer{get_leaf_pointer(root_pointer)},
      stem_levels{0}
    {
        auto& root = get_leaf(root_pointer);
        root.aux.prev_pointer = nullptr;
        root.aux.next_pointer = nullptr;
    }

    // ACCESSORS

public:
    auto& operator[](const Key& find_key) {
        auto position = find_implementation(find_key);
        return val_extract_(
            get_leaf(position.page).elem(position.sub_position));
    }
    const auto& operator[](const Key& find_key) const {
        auto position = find_implementation(find_key);
        return val_extract_(
            get_leaf(position.page).elem(position.sub_position));
    }

private:
    Element& elem(const tree_position position) {
        return get_leaf(position.page).elem(position.sub_position);
    }
    const Element& elem(const tree_position position) const {
        return get_leaf(position.page).elem(position.sub_position);
    }

    static stem_node* get_stem_pointer(void* const pointer) {
        return static_cast<stem_node*>(pointer);
    }
    static leaf_node* get_leaf_pointer(void* const pointer) {
        return static_cast<leaf_node*>(pointer);
    }

    static stem_node& get_stem(void* const pointer) {
        return *get_stem_pointer(pointer);
    }
    static leaf_node& get_leaf(void* const pointer) {
        return *get_leaf_pointer(pointer);
    }

    static const stem_node& get_stem(const void* const pointer) {
        return *static_cast<const stem_node*>(pointer);
    }
    static const leaf_node& get_leaf(const void* const pointer) {
        return *static_cast<const leaf_node*>(pointer);
    }

    // PREDICATES
public:
    bool empty() const
    {
        return stem_levels == 0 && get_leaf(root_pointer).empty();
    }

    // OPERATIONS
public:
    iterator find(const Key& find_key) {
        return {this, find_implementation(find_key)};
    }

    const_iterator find(const Key& find_key) const {
        return {this, find_implementation(find_key)};
    }

private:
    tree_position find_implementation(const Key& find_key) const
    {
        auto search_pointer = root_pointer;
        for (int depth = 0; depth < stem_levels; ++depth)
        {
            const auto& search_stem = get_stem(search_pointer);
            search_pointer =
                search_stem.elem(search_stem.find(find_key)).second;
        }
        const auto& leaf = get_leaf(search_pointer);
        return {search_pointer, leaf.find(find_key)};
    }

private:
    using Path = std::vector<tree_position>;

    Path find_path(const Key& find_key) const
    {
        Path result(stem_levels + 1);
        auto search_pointer = root_pointer;
        for (int depth = 0; depth < stem_levels; ++depth)
        {
            const auto& search_stem = get_stem(search_pointer);
            result[depth].page = search_pointer;
            result[depth].sub_position = search_stem.find(find_key);
            search_pointer =
                search_stem.elem(result[depth].sub_position).second;
        }
        const auto& search_leaf = get_leaf(search_pointer);
        result[stem_levels].page = search_pointer;
        result[stem_levels].sub_position = search_leaf.find(find_key);
        return result;
    }

    void split_root()
    {
        if (stem_levels > 0)
        {
            const auto old_root_pointer = root_pointer;
            auto& old_root = get_stem(old_root_pointer);

            if (old_root.oversized())
            {
                const auto new_pointer = old_root.split_one_leaf();
                auto& new_stem = get_stem(new_pointer);

                while (old_root.oversized())
                {
                    new_stem.borrow_prev(old_root);
                }

                root_pointer = new stem_node;
                auto& new_root = get_stem(root_pointer);

                new_root.insert({old_root.min_key(), old_root_pointer});
                new_root.insert({new_stem.min_key(), new_pointer});

                ++stem_levels;
            }
        }
        else
        {
            const auto old_root_pointer = root_pointer;
            auto& old_root = get_leaf(old_root_pointer);

            if (old_root.oversized())
            {
                const auto new_pointer = old_root.split_one_leaf();
                auto& new_leaf = get_leaf(new_pointer);

                while (old_root.oversized()) {
                    new_leaf.borrow_prev(old_root);
                }

                old_root.aux.next_pointer = new_pointer;
                new_leaf.aux.prev_pointer = old_root_pointer;
                max_leaf_pointer = new_pointer;

                root_pointer = new stem_node;
                auto& new_root = get_stem(root_pointer);

                new_root.insert({old_root.min_key(), old_root_pointer});
                new_root.insert({new_leaf.min_key(), new_pointer});

                ++stem_levels;
            }
        }
    }

public:
    void insert(const Element& new_elem)
    {
        split_root();

        const auto& new_key = key_extract_(new_elem);

        auto current_pointer = root_pointer;
        for (int depth = 0; depth < stem_levels - 1; ++depth)
        {
            auto& current_stem = get_stem(current_pointer);

            const auto target_pos = current_stem.find(new_key);
            const auto target_pointer = current_stem.elem(target_pos).second;
            auto& target_stem = get_stem(target_pointer);

            // Offload to previous sibling?
            if (target_stem.oversized() &&
                target_pos != current_stem.min_position())
            {
                const auto prev_pos = current_stem.prev_position(target_pos);
                const auto prev_pointer = current_stem.elem(prev_pos).second;
                auto& prev_stem = get_stem(prev_pointer);

                if (prev_stem.small()) {
                    while (target_stem.oversized()) {
                        prev_stem.borrow_next(target_stem);
                    }

                    current_stem.set_key(target_pos, target_stem.min_key());

                    if (new_key < target_stem.min_key()) {
                        if (new_key < prev_stem.min_key()) {
                            current_stem.set_key(prev_pos, new_key);
                        }
                        current_pointer = prev_pointer;
                    } else {
                        current_pointer = target_pointer;
                    }
                    continue;
                }
            }

            // Offload to next sibling?
            if (target_stem.oversized() &&
                target_pos != current_stem.max_position())
            {
                const auto next_pos = current_stem.next_position(target_pos);
                const auto next_pointer = current_stem.elem(next_pos).second;
                auto& next_stem = get_stem(next_pointer);

                if (next_stem.small()) {
                    while (target_stem.oversized()) {
                        next_stem.borrow_prev(target_stem);
                    }

                    current_stem.set_key(next_pos, next_stem.min_key());

                    if (new_key >= next_stem.min_key()) {
                        current_pointer = next_pointer;
                    } else {
                        if (new_key < target_stem.min_key()) {
                            current_stem.set_key(target_pos, new_key);
                        }
                        current_pointer = target_pointer;
                    }
                    continue;
                }
            }

            if (target_stem.oversized())
            {
                // Offload to new next sibling?
                const auto new_pointer = target_stem.split_one_leaf();
                auto& new_stem = get_stem(new_pointer);

                while (target_stem.oversized()) {
                    new_stem.borrow_prev(target_stem);
                }

                current_stem.insert({new_stem.min_key(), new_pointer});

                if (new_key >= new_stem.min_key()) {
                    current_pointer = new_pointer;
                } else {
                    if (new_key < target_stem.min_key()) {
                        current_stem.set_key(target_pos, new_key);
                    }
                    current_pointer = target_pointer;
                }
                continue;
            }

            if (new_key < target_stem.min_key()) {
                current_stem.set_key(target_pos, new_key);
            }
            current_pointer = target_pointer;
        }

        if (stem_levels > 0)
        {
            auto& current_stem = get_stem(current_pointer);

            const auto target_pos = current_stem.find(new_key);
            const auto target_pointer = current_stem.elem(target_pos).second;
            auto& target_leaf = get_leaf(target_pointer);

            // Offload to previous sibling?
            if (target_leaf.oversized() &&
                target_pos != current_stem.min_position())
            {
                const auto prev_pos = current_stem.prev_position(target_pos);
                const auto prev_pointer = current_stem.elem(prev_pos).second;
                auto& prev_leaf = get_leaf(prev_pointer);

                if (prev_leaf.small()) {
                    while (target_leaf.oversized()) {
                        prev_leaf.borrow_next(target_leaf);
                    }

                    current_stem.set_key(target_pos, target_leaf.min_key());

                    if (new_key < target_leaf.min_key()) {
                        if (new_key < prev_leaf.min_key()) {
                            current_stem.set_key(prev_pos, new_key);
                        }
                        prev_leaf.insert(new_elem);
                    } else {
                        target_leaf.insert(new_elem);
                    }
                    return;
                }
            }

            // Offload to next sibling?
            if (target_leaf.oversized() &&
                target_pos != current_stem.max_position())
            {
                const auto next_pos = current_stem.next_position(target_pos);
                const auto next_pointer = current_stem.elem(next_pos).second;
                auto& next_leaf = get_leaf(next_pointer);

                if (next_leaf.small()) {
                    while (target_leaf.oversized()) {
                        next_leaf.borrow_prev(target_leaf);
                    }

                    current_stem.set_key(next_pos, next_leaf.min_key());

                    if (new_key >= next_leaf.min_key()) {
                        next_leaf.insert(new_elem);
                    } else {
                        if (new_key < target_leaf.min_key()) {
                            current_stem.set_key(target_pos, new_key);
                        }
                        target_leaf.insert(new_elem);
                    }
                    return;
                }
            }

            // Offload to new next sibling?
            if (target_leaf.oversized())
            {
                const auto new_pointer = target_leaf.split_one_leaf();
                auto& new_leaf = get_leaf(new_pointer);

                while (target_leaf.oversized()) {
                    new_leaf.borrow_prev(target_leaf);
                }

                current_stem.insert({new_leaf.min_key(), new_pointer});

                if (target_leaf.aux.next_pointer != nullptr) {
                    get_leaf(target_leaf.aux.next_pointer).aux.prev_pointer =
                        new_pointer;
                }
                new_leaf.aux.prev_pointer = target_pointer;
                new_leaf.aux.next_pointer = target_leaf.aux.next_pointer;
                target_leaf.aux.next_pointer = new_pointer;

                if (max_leaf_pointer == target_pointer) {
                    max_leaf_pointer = new_pointer;
                }

                if (new_key >= new_leaf.min_key()) {
                    new_leaf.insert(new_elem);
                } else {
                    if (new_key < target_leaf.min_key()) {
                        current_stem.set_key(target_pos, new_key);
                    }
                    target_leaf.insert(new_elem);
                }
                return;
            }

            if (new_key < target_leaf.min_key()) {
                current_stem.set_key(target_pos, new_key);
            }
            target_leaf.insert(new_elem);
        }
        else
        {
            auto& current_leaf = get_leaf(current_pointer);
            current_leaf.insert(new_elem);
        }
    }

private:
    void insert_node_after(const Path& path, const int depth,
        const Key prev_min_key, void* const prev_pointer,
        const Key new_min_key, void* const new_pointer)
    {
        if (depth > 0)
        {
            const auto insert_pointer = path[depth - 1].page;
            auto& insert_stem = get_stem(insert_pointer);

            const auto split_pointer =
                insert_stem.insert(new_min_key, new_pointer);
            if (split_pointer)
            {
                auto& split_stem = *split_pointer;
                insert_node_after(path, depth - 1,
                    insert_stem.min_key(), insert_pointer,
                    split_stem.min_key(), split_pointer);
            }
        }
        else
        {
            root_pointer = new stem_node;
            auto& root = get_stem(root_pointer);

            root.insert(prev_min_key, prev_pointer);
            root.insert(new_min_key, new_pointer);

            ++stem_levels;
        }
    }

public:
    void erase(const Key& erase_key)
    {
        const auto path = find_path(erase_key);

        const auto erase_pointer = path[stem_levels].page;
        auto& erase_leaf = get_leaf(erase_pointer);

        const auto was_large = erase_leaf.large();
        // const auto old_key = erase_leaf.min_key();

        // Erase the element
        erase_leaf.erase(erase_key);
        // If this is the root we are done
        if (stem_levels == 0) { return; }

        auto parent_pos = path[stem_levels - 1].sub_position;
        auto& parent_stem = get_stem(path[stem_levels - 1].page);
        const auto parent_was_large = parent_stem.large();
        const auto old_key = parent_stem.key(parent_pos);

        // If the node is now empty
        if (erase_leaf.empty()) {
            delete &erase_leaf;
            parent_stem.erase(old_key);
        // Otherwise we must maintain the invariants
        } else {
            const auto prev_ptr = parent_pos == parent_stem.min_position() ?
                nullptr : static_cast<leaf_node*>(parent_stem.elem(
                        parent_stem.prev_position(parent_pos)).second);
            const auto prev_key = prev_ptr ? prev_ptr->min_key() : Key{};

            const auto next_ptr = parent_pos == parent_stem.max_position() ?
                nullptr : static_cast<leaf_node*>(parent_stem.elem(
                        parent_stem.next_position(parent_pos)).second);
            const auto next_key = next_ptr ? next_ptr->min_key() : Key{};

            bool did_grow = false;

            // If a large node turned small we must grow it large again
            if (was_large && erase_leaf.small()) {
                if (prev_ptr && prev_ptr->small()) {
                    while (erase_leaf.small() && !prev_ptr->empty()) {
                        erase_leaf.borrow_prev(*prev_ptr);
                    }
                }

                if (next_ptr && next_ptr->small()) {
                    while (erase_leaf.small() && !next_ptr->empty()) {
                        erase_leaf.borrow_next(*next_ptr);
                    }
                }

                did_grow = true;
            }

            if (prev_ptr && prev_ptr->empty()) {
                if (prev_ptr->aux.prev_pointer) {
                    get_leaf(prev_ptr->aux.prev_pointer).aux.next_pointer =
                        erase_pointer;
                }
                erase_leaf.aux.prev_pointer = prev_ptr->aux.prev_pointer;

                if (min_leaf_pointer == prev_ptr) {
                    min_leaf_pointer = &get_leaf(erase_pointer);
                }

                delete prev_ptr;
                parent_stem.erase(prev_key);
            }

            if (next_ptr && next_ptr->empty()) {
                if (next_ptr->aux.next_pointer) {
                    get_leaf(next_ptr->aux.next_pointer).aux.prev_pointer =
                        erase_pointer;
                }
                erase_leaf.aux.next_pointer = next_ptr->aux.next_pointer;

                if (max_leaf_pointer == next_ptr) {
                    max_leaf_pointer = &get_leaf(erase_pointer);
                }

                delete next_ptr;
                parent_stem.erase(next_key);
            }
            else if (next_ptr && next_ptr->min_key() != next_key) {
                parent_stem.set_key(
                    parent_stem.find(next_key), next_ptr->min_key());
            }

            if (erase_leaf.min_key() != old_key) {
                parent_stem.set_key(
                    parent_stem.find(old_key), erase_leaf.min_key());
            }
        }

        erase_helper(path, stem_levels - 1, parent_was_large);
    }

private:
    void root_collapse()
    {
        auto& root = get_stem(root_pointer);

        // If we have only one child we should collapse this level of th tree
        if (root.stem_levels == 0 &&
            root.get_leaf(root.min_leaf_index).count() == 1)
        {
            const auto old_root = root_pointer;
            root_pointer = root.elem({root.min_leaf_index, 0}).second;
            delete get_stem_pointer(old_root);
            --stem_levels;

            if (stem_levels > 0) root_collapse();
        }
    }

    void erase_helper(const Path& path, const int depth, bool was_large)
    {
        // If we are the root
        if (depth == 0) {
            root_collapse();
            return;
        }

        const auto erase_pointer = path[depth].page;
        auto& erase_stem = get_stem(erase_pointer);

        // const auto old_key = erase_stem.min_key();

        auto& parent_stem = get_stem(path[depth - 1].page);
        auto parent_pos = path[depth - 1].sub_position;
        const auto parent_was_large = parent_stem.large();
        const auto old_key = parent_stem.key(parent_pos);

        // If the node is now empty
        if (erase_stem.empty()) {
            delete &erase_stem;
            parent_stem.erase(old_key);
        // Otherwise we must maintain the invariants
        } else {
            const auto prev_ptr = parent_pos == parent_stem.min_position() ?
                nullptr : static_cast<stem_node*>(parent_stem.elem(
                        parent_stem.prev_position(parent_pos)).second);
            const auto prev_key = prev_ptr ? prev_ptr->min_key() : Key{};

            const auto next_ptr = parent_pos == parent_stem.max_position() ?
                nullptr : static_cast<stem_node*>(parent_stem.elem(
                        parent_stem.next_position(parent_pos)).second);
            const auto next_key = next_ptr ? next_ptr->min_key() : Key{};

            // If a large node turned small we must grow it large again
            if (was_large && erase_stem.small()) {
                if (prev_ptr && prev_ptr->small()) {
                    while (erase_stem.small() && !prev_ptr->empty()) {
                        erase_stem.borrow_prev(*prev_ptr);
                    }
                }

                if (next_ptr && next_ptr->small()) {
                    while (erase_stem.small() && !next_ptr->empty()) {
                        erase_stem.borrow_next(*next_ptr);
                    }
                }
            }

            if (prev_ptr && prev_ptr->empty()) {
                delete prev_ptr;
                parent_stem.erase(prev_key);
            }

            if (next_ptr && next_ptr->empty()) {
                delete next_ptr;
                parent_stem.erase(next_key);
            }
            else if (next_ptr && next_ptr->min_key() != next_key) {
                parent_stem.set_key(
                    parent_stem.find(next_key), next_ptr->min_key());
            }

            if (erase_stem.min_key() != old_key) {
                parent_stem.set_key(
                    parent_stem.find(old_key), erase_stem.min_key());
            }
        }

        erase_helper(path, depth - 1, parent_was_large);
    }

public:
    // ITERATOR GETTERS

    iterator begin() {
        return make_iterator({
            min_leaf_pointer,
            min_leaf_pointer->min_position()});
    }

    const_iterator begin() const {
        return make_const_iterator({
            min_leaf_pointer,
            min_leaf_pointer->min_position()});
    }

    const_iterator cbegin() const {
        return make_const_iterator({
            min_leaf_pointer,
            min_leaf_pointer->min_position()});
    }

    iterator end() {
        return make_iterator({
            max_leaf_pointer,
            max_leaf_pointer->end_position()});
    }

    const_iterator end() const {
        return make_const_iterator({
            max_leaf_pointer,
            max_leaf_pointer->end_position()});
    }

    const_iterator cend() const {
        return make_const_iterator({
            max_leaf_pointer,
            max_leaf_pointer->end_position()});
    }

private:
    iterator make_iterator(tree_position position) {
        return {this, position};
    }

    const_iterator make_const_iterator(tree_position position) const {
        return {this, position};
    }

    // ITERATOR TYPE

    template <typename T>
    class iterator_template : std::iterator<std::forward_iterator_tag, T>
    {
        friend kernel;

    public:
        iterator_template()
        : tree_(nullptr),
          position_()
        {}

        iterator_template(const iterator_template& other)
        : tree_(other.tree_),
          position_(other.position_)
        {}

        iterator_template& operator=(const iterator_template& other)
        {
            tree_ = other.tree_;
            position_ = other.position_;
        }

        ~iterator_template()
        {}

        reference operator*() {
            return tree_->elem(position_);
        }

        const_reference operator*() const {
            return tree_->elem(position_);
        }

        pointer operator->() {
            return &tree_->elem(position_);
        }

        const_pointer operator->() const {
            return &tree_->elem(position_);
        }

        iterator_template& operator++()
        {
            const auto& leaf = get_leaf(position_.page);
            if (leaf.aux.next_pointer != nullptr &&
                position_.sub_position == leaf.max_position())
            {
                position_.page = leaf.aux.next_pointer;
                const auto& next = get_leaf(position_.page);
                position_.sub_position = next.min_position();
            }
            else
            {
                position_.sub_position =
                    leaf.next_position(position_.sub_position);
            }
            return *this;
        }

        iterator_template operator++(int)
        {
            iterator_template old(*this);
            ++*this;
            return old;
        }

        bool operator==(const iterator_template& other) const {
            return tree_ == other.tree_ && position_ == other.position_;
        }

        bool operator!=(const iterator_template& other) const {
            return tree_ != other.tree_ || position_ != other.position_;
        }

    private:
        iterator_template(
            kernel* tree,
            tree_position position)
        : tree_(tree),
          position_(position)
        {}

        kernel* tree_;
        tree_position position_;
    };

public:
    void print() const
    {
        std::cout << "------------" << std::endl;
        print_node(root_pointer, 0);
    }

private:
    void print_node(void* pointer, int depth) const
    {
        if (depth < stem_levels) {
            const stem_node& stem = get_stem(pointer);
            std::cout << "treestem (" << depth << ", "
                << (int)stem.get_leaf(stem.min_leaf_index).count()
                << ") " << std::endl;
            std::cout << "--" << std::endl;
            stem.print();
            std::cout << "--" << std::endl;
            std::cout << std::endl;
            for (auto p = stem.min_position(); p != stem.end_position(); p =
                stem.next_position(p))
            {
                print_node(stem.elem(p).second, depth + 1);
            }
        } else {
            const leaf_node& leaf = get_leaf(pointer);
            std::cout << "treeleaf (" << depth << ") " << std::endl;
            std::cout << "--" << std::endl;
            leaf.print();
            std::cout << "--" << std::endl;
            std::cout << std::endl;
        }
    }
};

} // namespace detail

// SET

#define BASE detail::kernel<\
    Key,\
    Key,\
    Key,\
    extract::identity,\
    extract::identity>
template <
    class Key>
class set : public BASE
{};
#undef BASE

// MAP

#define BASE detail::kernel<\
    std::pair<const Key, T>,\
    std::pair<Key, T>,\
    Key,\
    extract::first,\
    extract::second>
template <
    class Key,
    class T>
class map : public BASE
{
public:
    using mapped_type = T;
};
#undef BASE

} // namespace double_tree

