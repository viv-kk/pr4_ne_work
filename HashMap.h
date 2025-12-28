#ifndef HASHMAP_H
#define HASHMAP_H

#include "vector.h"
#include <string>
#include <utility>
using namespace std;

class Document;
class Collection;

template<typename K, typename V>
class HashMap {
private:
    struct Node {
        K key;
        V value;
        Node* next;
        Node(const K& k, const V& v);
    };
    
    Vector<Node*> buckets;//массив связныхсписков
    size_t bucketCount;//размер таблицы
    size_t itemCount;//колво элементов
    const double loadFactor = 0.75;//для ресайза
    
    size_t customHash(const string& str) const;//хэш функция для стр
    size_t getBucketIndex(const K& key) const;//индекс связного списка
    void resize();

public:
    HashMap();
    ~HashMap();
    HashMap(const HashMap& other);
    HashMap& operator=(const HashMap& other);
    HashMap(HashMap&& other) noexcept;
    HashMap& operator=(HashMap&& other) noexcept;
    void put(const K& key, const V& value);
    bool get(const K& key, V& value) const;
    bool remove(const K& key);
    Vector<pair<K, V>> items() const;
    size_t size() const;
    void clear();
    bool contains(const K& key) const;
};

#include "HashMapImpl.h"

#endif
