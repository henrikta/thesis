// An array based node that fits in a cache line

#pragma once
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>

namespace double_tree
{

namespace detail
{

// Define here the size in bytes of line nodes, as well as the types used to
// index inside them.  A line node index should be able to index all values in
// an array of key-value pairs of size equal to the line node.  See below for
// the exact requirements, which are a little less because of the size used up
// for bookkeeping data in each case

constexpr size_t line_node_size = 256;
using line_index = uint8_t;
constexpr line_index line_index_nil = std::numeric_limits<line_index>::max();

template <
    typename Element,
    typename ElementWrite,
    typename Key,
    typename KeyExtract,
    typename ValExtract,
    typename Aux>
struct alignas(line_node_size) line_node
{
public:
    // CONSTANTS

    static constexpr int max_count =
        (line_node_size - sizeof(line_index) - sizeof(Aux)) / sizeof(Element);
    static constexpr int min_count = max_count/2;

    // DATA

private:
    std::array<Element, max_count> elems_;
    line_index count_;
    KeyExtract key_extract_;
public:
    Aux aux;

    // ACCESSORS

public:
    line_index count() const { return count_; }

    void reset() { count_ = 0; }

    const Key& key(const line_index index) const {
        return key_extract_(elems_[index]);
    }

    Element& elem(const line_index index) {
        return elems_[index];
    }

    const Element& elem(const line_index index) const {
        return elems_[index];
    }

    void set_key(const line_index index, const Key& new_key) {
        key_extract_(elem_write(index)) = new_key;
    }

    void set_elem(const line_index index, const Element& new_element) {
        elem_write(index) = new_element;
    }

    line_index min_index() const { return 0; }
    line_index max_index() const { return (count_ == 0) ? 0 : count_ - 1; }
    line_index end_index() const { return count_; }

    const Key& min_key() const { return key(min_index()); }
    const Element& min_elem() const { return elem(min_index()); }

    const Key& end_key() const { return key(end_index()); }
    const Element& end_elem() const { return elem(end_index()); }

private:
    ElementWrite& elem_write(const line_index index) {
        return reinterpret_cast<ElementWrite&>(elems_[index]);
    }

    ElementWrite& min_elem_write() {
        return elem_write(min_index());
    }

    // PREDICATES

public:
    bool empty() const { return count_ == 0; }

    // Is node at maximum capacity?
    bool full() const { return count_ == max_count; }

    // Is node at minimum capacity?
    bool thin() const { return count_ < min_count; }

    // OPERATIONS

    // Return the index of the greatest key less than or equal to the one
    // given, or the minimum index if all keys are greater than the one given
    line_index find(const Key& find_key) const
    {
        if (min_key() > find_key) {
            return min_index();
        }
        for (int i = min_index() + 1; i < end_index(); ++i) {
            if (key(i) > find_key) {
                return i - 1;
            }
        }
        return count_ - 1;
    }

private:
    // Move all the elements from one index to another (inclusive) back one
    // place
    void move_one_back(const line_index begin, const line_index end)
    {
        std::move_backward(&elem(begin), &elem(end), &elem_write(end + 1));
    }

    // Move all the elements from one index to another (inclusive) forward one
    // place
    void move_one_forward(const line_index begin, const line_index end)
    {
        std::move(&elem(begin), &elem(end), &elem_write(begin - 1));
    }

    // Move all the elements from one index to another (inclusive) to another
    // node, starting from some index in that node
    void move_to(const line_index begin, const line_index end,
        line_node& dest_node, const line_index dest)
    {
        std::move(&elem(begin), &elem(end), &dest_node.elem_write(dest));
    }

public:
    void init()
    {
        count_ = 0;
    }

    void init_from(const Element* begin, const Element* end)
    {
        std::move(begin, end, &min_elem_write());
        count_ = end - begin;
    }

    void init_from(const line_node& other)
    {
        std::move(&other.min_elem(), &other.end_elem(), &min_elem_write());
        count_ = other.count_;
    }

    // Insert a new element.  Assumes the node is not full
    void insert(const Element& new_elem)
    {
        line_index insert_index = end_index();
        for (int i = min_index(); i < end_index(); ++i) {
            if (key(i) > key_extract_(new_elem)) {
                insert_index = i;
                break;
            }
        }

        move_one_back(insert_index, end_index());

        set_elem(insert_index, new_elem);

        ++count_;
    }

    // Split node in half
    void split(line_node& split_node)
    {
        // This node takes half and the odd of the elements
        const auto new_count = count_/2 + count_%2;

        // The split node takes the the half
        split_node.count_ = count_/2;

        move_to(new_count, end_index(), split_node, split_node.min_index());

        count_ = new_count;
    }

    // Erase an element.  If node is thin this will put the node under capacity
    void erase(const line_index erase_index)
    {
        if (erase_index < end_index()) {
            move_one_forward(erase_index + 1, end_index());
        }

        --count_;
    }

    // Erase an element while merging node with the previous node.  The
    // elements go into the previous node, so this one is left empty
    void merge_prev_erase(const line_index erase_index, line_node& prev_node)
    {
        move_to(0, erase_index,
            prev_node, prev_node.end_index());
        move_to(erase_index + 1, end_index(),
            prev_node, prev_node.end_index() + erase_index);

        prev_node.count_ += count_ - 1;
        count_ = 0;
    }

    // Erase an element while merging node with the next node.  The elements go
    // into this node, so the next node is left empty
    void merge_next_erase(const line_index erase_index, line_node& next_node)
    {
        move_one_forward(erase_index + 1, end_index());

        next_node.move_to(
            next_node.min_index(), next_node.end_index(),
            *this, end_index() - 1);

        count_ += next_node.count_ - 1;
        next_node.count_ = 0;
    }

    // Erase an element while borrowing one from the previous node
    void borrow_prev_erase(const line_index erase_index, line_node& prev_node)
    {
        move_one_back(min_index(), erase_index);
        prev_node.move_to(
            prev_node.end_index() - 1, prev_node.end_index(),
            *this, min_index());

        prev_node.count_ -= 1;
    }

    // Erase an element while borrowing one from the next node
    void borrow_next_erase(const line_index erase_index, line_node& next_node)
    {
        move_one_forward(erase_index + 1, end_index());
        next_node.move_to(
            next_node.min_index(), next_node.min_index() + 1,
            *this, end_index() - 1);

        next_node.move_one_forward(
            next_node.min_index() + 1, next_node.end_index());
        next_node.count_ -= 1;
    }

    // Print all the keys in a comma-seperated list followed by a newline
    void print() const
    {
        if (count_ > 0)
        {
            for (int index = 0; index < end_index(); ++index)
            {
                std::cout << key(index);
                if (index < end_index() - 1)
                {
                    std::cout << ", ";
                }
            }
        }
        std::cout << std::endl;
    }
};

} // namespace detail

} // namespace double_tree

