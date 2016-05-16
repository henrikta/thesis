#include "double_tree.hpp"
#include "tabulation.hpp"
#include "hopscotch.hpp"
#include "linear.hpp"
#include "longrand.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <stx/btree_map.h>

using std::cerr;
using std::cout;
using std::endl;
using std::flush;
using std::pair;

#define DOUBLE_BTREE
// #define SLOW_INSERT
// #define SLOW_ERASE

template <typename K, typename V, size_t nodesize>
class btree_traits
{
public:
    static const bool selfverify = false;
    static const bool debug = false;
    static const int leafslots = nodesize / (sizeof(K) + sizeof(V));
    static const int innerslots = nodesize / (sizeof(K) + sizeof(void*));
    static const size_t binsearch_threshold = 256;
};

int main(int, char**)
{
    const size_t count = 1000000;

#if defined(HOPSCOTCH)
    hopscotch::unordered_map<uint64_t, uint64_t, tabulation<uint64_t>> map;
#elif defined(LINEAR)
    linear::unordered_map<uint64_t, uint64_t, tabulation<uint64_t>> map;
#elif defined(DOUBLE_BTREE)
    double_tree::map<uint64_t, uint64_t> map;
#endif

    // Insertion test
    srand(19);
    for (size_t i = 0; i < count; ++i)
    {
        cerr << "\rinsert " << i << "/" << count << flush;

        uint64_t key = rand();
        uint64_t val = rand();

        map.insert({key, val});
#if !defined(DOUBLE_BTREE)
        assert(map.size() == i + 1);
#endif

#if defined(SLOW_INSERT)
        srand(19);
        for (size_t j = 0; j < i + 1; ++j) {
            uint64_t key = rand();
            uint64_t val = rand();
            if (map[key] != val) {
                cout << "while inserting " << key << endl;
                cout << i << ": could not find " << val << " (" << j <<
                    "). found " << map[key] << endl;
                assert(false);
            }
        }
#endif  // SLOW_INSERT
    }

    // Iterator test
#if defined(DOUBLE_BTREE) || defined(TRAD_BTREE_CACHE) \\
    || defined(TRAD_BTREE_DISK)
    int itnr = 0;
    uint64_t prev;
    for (auto it = map.begin(); it != map.end(); ++it) {
        cerr << "\riterate " << itnr << "/" << count << flush;
        ++itnr;

        if (it != map.begin()) {
            assert(it->first > prev);
        }
        prev = it->first;
    }
#endif

    // Find test
    srand(19);
    for (size_t i = 0; i < count; ++i)
    {
        cerr << "\rfind " << i << "/" << count << flush;

        uint64_t key = rand();
        uint64_t val = rand();

        if (map[key] != val) {
            cout << endl << "could not find " << val <<"." << endl
                 << "found " << map[key] << "." << endl;
            assert(false);
        }
    }

    srand(19);
    for (size_t i = 0; i < count; ++i)
    {
        cerr << "\rerase " << i << "/" << count << flush;

        uint64_t key = rand();
        rand();

        map.erase(key);

#if defined(SLOW_ERASE)
        srand(19);

        for (size_t j = 0; j < i + 1; ++j) {
            rand(); rand();
        }
        for (size_t j = i + 1; j < count; ++j) {
            uint64_t key = rand();
            uint64_t val = rand();
            if (map[key] != val) {
                cout << i << ": could not find " << key << " (" << j <<
                    "). found " << map[key].first << endl;
                assert(false);
            }
        }
        for (size_t j = 0; j < i + 1; ++j) {
            rand(); rand();
        }
#endif  // SLOW_ERASE
    }

    assert(map.empty());
}

