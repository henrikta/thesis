#include "tabulation.hpp"

#include "hopscotch.hpp"
#include "linear.hpp"

#include "performance_clock.hpp"
#include "longrand.hpp"

#include <algorithm>
#include <cassert>
#include <iomanip>
#include <iostream>
#include <vector>

#include <stx/btree_map.h>

using std::cerr;
using std::cout;
using std::endl;
using std::flush;
using std::pair;
using std::vector;

// #define DENSE
#define TRAD_BTREE_CACHE

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
    const size_t count = static_cast<size_t>(1)<<26;

    srand(35);
    vector<uint64_t> elements(count);

#ifdef DENSE
    // Create a list of randomly ordered numbers from a dense interval (0..count)
    for (size_t i = 0; i < count; ++i) { elements[i] = i; }
    std::random_shuffle(elements.begin(), elements.end());
#else
    // Create a list of random numbers
    for (size_t i = 0; i < count; ++i) { elements[i] = longrand(); }
#endif

    // Create a map with the chosen collision resolution method and hash function
#if defined(LINEAR)
    linear::unordered_map<uint64_t, uint64_t, tabulation<uint64_t>> map;
#elif defined(HOPSCOTCH)
    hopscotch::unordered_map<uint64_t, uint64_t, tabulation<uint64_t>> map;
#elif defined(DOUBLE_BTREE)
    double_tree::map<uint64_t, uint64_t> map;
#elif defined(TRAD_BTREE_CACHE)
    stx::btree_map<uint64_t, uint64_t, std::less<uint64_t>,
        btree_traits<uint64_t, uint64_t, 256>> map;
#elif defined(TRAD_BTREE_DISK)
    stx::btree_map<uint64_t, uint64_t, std::less<uint64_t>,
        btree_traits<uint64_t, uint64_t, 4096>> map;
#endif

    cout << std::fixed << std::setprecision(0);

    const size_t round_count = static_cast<size_t>(1)<<18;
    const size_t rounds = count/round_count;

    for (size_t i = 0; i < rounds; ++i) {
        // Insert
        performance_clock::interval insert_interval;
        insert_interval.before();
        for (size_t j = 0; j < round_count; ++j) {
            map.insert(std::make_pair(
                elements[i*round_count + j], i*round_count + j));
        }
        insert_interval.after();
        cout << "insert\t" << i;
        cout << "\t" << insert_interval.wall_time()/(double)round_count;
        cout << "\t" << insert_interval.usr_time()/(double)round_count;
        cout << "\t" << insert_interval.sys_time()/(double)round_count;
        cout << endl;

        // Find
        performance_clock::interval search_interval;
        search_interval.before();
        size_t search_i = rand()%(i+1);
        for (size_t j = 0; j < round_count; ++j) {
            volatile auto _ = map[elements[search_i*round_count + j]];
            (void)_;  // Silence 'unused variable'
        }
        search_interval.after();
        cout << "search\t" << i;
        cout << "\t" << search_interval.wall_time()/(double)round_count;
        cout << "\t" << search_interval.usr_time()/(double)round_count;
        cout << "\t" << search_interval.sys_time()/(double)round_count;
        cout << endl;

#if defined(DOUBLE_BTREE) || defined(TRAD_BTREE_CACHE) \\
        || defined(TRAD_BTREE_DISK)
        // Iterate
        performance_clock::interval iterate_interval;
        iterate_interval.before();
        size_t iterate_i = rand()%(i+1);
        size_t iterate_j = rand()%(round_count);
        auto it = map.find(map[elements[iterate_i*round_count + iterate_j]]);
        for (size_t j = 0; j < round_count; ++j) {
            volatile auto _ = *it;
            (void)_;  // Silence 'unused variable'
            ++it;
            if (it == map.end()) {
                it = map.begin();
            }
        }
        iterate_interval.after();
        cout << "iterate\t" << i;
        cout << "\t" << iterate_interval.wall_time()/(double)round_count;
        cout << "\t" << iterate_interval.usr_time()/(double)round_count;
        cout << "\t" << iterate_interval.sys_time()/(double)round_count;
        cout << endl;
#endif
    }

    // Shuffle the elements for erasing
    std::random_shuffle(elements.begin(), elements.end());

    for (size_t i = 0; i < rounds; ++i) {
        // Erase
        performance_clock::interval erase_interval;
        erase_interval.before();
        for (size_t j = 0; j < round_count; ++j) {
            map.erase(elements[i*round_count + j]);
        }
        erase_interval.after();
        cout << "erase\t" << i;
        cout << "\t" << erase_interval.wall_time()/(double)round_count;
        cout << "\t" << erase_interval.usr_time()/(double)round_count;
        cout << "\t" << erase_interval.sys_time()/(double)round_count;
        cout << endl;
    }
}


