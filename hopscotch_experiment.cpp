#include "tabulation.hpp"
#include <ctime>
#include <iostream>
#include <random>
#include <vector>
using std::vector;

// #define TABULATION

class stripped_hopscotch
{
protected:
    static const size_t neighborhood_size = 15;

public:
    explicit stripped_hopscotch(size_t bucket_count):
        bucket_count_{bucket_count},
        size_{0}
    {
        buckets_.resize(bucket_count_);
    }

    float load_factor() const {
        return (float)size_/(float)bucket_count_;
    }

    struct bucket_type
    {
        void has_value(bool has_value) {
            hop_info[neighborhood_size] = has_value;
        }

        bool has_value() const {
            return hop_info[neighborhood_size];
        }

        std::bitset<neighborhood_size+1> hop_info;
    };

    std::vector<bucket_type> buckets_;
    size_t bucket_count_;
    size_t size_;

    size_t index_add(size_t index, size_t x) const {
        return (index + x) & (bucket_count_ - 1);
    }

    size_t index_sub(size_t index, size_t x) const {
        return (index - x) & (bucket_count_ - 1);
    }

    size_t next_hop(const bucket_type& bucket, int prev = -1) const
    {
        const size_t mask = 0xffffffffffffffff << (prev + 1);
        const size_t hop_info = bucket.hop_info.to_ulong();
        return __builtin_ffsl(hop_info & mask) - 1;
    }

    bool insert(size_t virtual_index)
    {
        virtual_index &= (bucket_count_ - 1);

        // Find the nearest free bucket, wrapping if we move past the end.
        size_t free_dist = 0;
        size_t free_index = virtual_index;
        while (buckets_[free_index].has_value()) {
            free_dist += 1;
            free_index = index_add(free_index, 1);
        }

        // Move buckets until we have a free bucket in the neighborhood of our
        // virtual bucket.
        while (free_dist > neighborhood_size - 1)
        {
            // Find a virtual bucket that has values stored in a bucket before
            // the free bucket we found.
            size_t virtual_move_dist = neighborhood_size - 1;
            size_t virtual_move_index =
                index_sub(free_index, virtual_move_dist);

            size_t move_hop;

            while (true)
            {
                auto& virtual_move_bucket = buckets_[virtual_move_index];
                auto hop_info = virtual_move_bucket.hop_info.to_ulong();
                move_hop = __builtin_ffsl(hop_info) - 1;

                if (move_hop < virtual_move_dist) {
                    break;
                } else {
                    // No luck, continue searching.
                    virtual_move_dist -= 1;
                    virtual_move_index = index_add(virtual_move_index, 1);

                    if (virtual_move_dist == 0)
                    {
                        return false;
                    }
                }
            }

            // Move.
            const size_t move_dist = virtual_move_dist - move_hop;
            const size_t move_index = index_add(virtual_move_index, move_hop);

            buckets_[move_index].has_value(false);
            buckets_[free_index].has_value(true);

            auto& virtual_move = buckets_[virtual_move_index];
            virtual_move.hop_info[move_hop] = false;
            virtual_move.hop_info[virtual_move_dist] = true;

            // The free bucket is now in the position of the moved bucket.
            free_dist -= move_dist;
            free_index = index_sub(free_index, move_dist);
        }

        // We should have a free bucket in the neighborhood now.
        buckets_[free_index].has_value(true);
        buckets_[virtual_index].hop_info[free_dist] = true;
        ++size_;
        return true;
    }
};

int main(int, char**)
{
    for (size_t exp = 8; exp <= 30; ++exp) {
        const size_t count = static_cast<size_t>(1) << exp;
        stripped_hopscotch hs{count};

#ifdef TABULATION
        vector<uint64_t> elements(count);
        for (size_t i = 0; i < count; ++i) { elements[i] = i; }
        std::srand(std::time(nullptr));
        std::random_shuffle(elements.begin(), elements.end());
        tabulation<uint64_t> t;
#else
        std::mt19937_64 mt(std::time(nullptr));
#endif

        for (size_t i = 0; i < count; ++i) {
#ifdef TABULATION
            if (!hs.insert(t(elements[i]))) {
#else
            if (!hs.insert(mt())) {
#endif
                std::cout << "\r"
                          << exp << " "
                          << i << "/" << count << " "
                          << (float)i/(float)count << std::endl;
                break;
            } else {
                for (int p = 0; p < 10; ++p) {
                    if (i == p*count/10) {
                        std::cerr << "\r0." << p;
                    }
                }
            }
        }
    }
}

