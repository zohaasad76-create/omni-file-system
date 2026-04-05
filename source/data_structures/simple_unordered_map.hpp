
#pragma once
#include <vector>
#include <string>
#include <functional>
#include <utility>
template<typename V>
class SimpleHashMap {
public:
    using key_type = std::string;
    using value_type = V;
    using pair_type = std::pair<key_type, value_type>;
    explicit SimpleHashMap(size_t bucket_count = 101) : buckets(bucket_count), _size(0) {}
    bool insert(const key_type& key, const value_type& value) {
        auto &bucket = buckets[hash_key(key) % buckets.size()];
        for (auto &p : bucket) {
            if (p.first == key) {
                p.second = value; 
                return false; 
            }
        }
        bucket.emplace_back(key, value);
        ++_size;
        return true;
    }
    bool erase(const key_type& key) {
        auto &bucket = buckets[hash_key(key) % buckets.size()];
        for (size_t i = 0; i < bucket.size(); ++i) {
            if (bucket[i].first == key) {
                bucket.erase(bucket.begin() + i);
                --_size;
                return true;
            }
        }
        return false;
    }

    bool contains(const key_type& key) const {
        auto &bucket = buckets[hash_key(key) % buckets.size()];
        for (auto &p : bucket) {
            if (p.first == key) return true;
        }
        return false;
    }
    value_type* find(const key_type& key) {
        auto &bucket = buckets[hash_key(key) % buckets.size()];
        for (auto &p : bucket) {
            if (p.first == key) return &p.second;
        }
        return nullptr;
    }

    const value_type* find(const key_type& key) const {
        auto &bucket = buckets[hash_key(key) % buckets.size()];
        for (auto &p : bucket) {
            if (p.first == key) return &p.second;
        }
        return nullptr;
    }

    size_t size() const { return _size; }

    void clear() {
        for (auto &b : buckets) b.clear();
        _size = 0;
    }
    std::vector<pair_type> get_all() const {
        std::vector<pair_type> out;
        out.reserve(_size);
        for (const auto &b : buckets) {
            for (const auto &p : b) out.push_back(p);
        }
        return out;
    }

private:
    std::vector<std::vector<pair_type>> buckets;
    size_t _size;
    std::hash<key_type> hash_key;
};
