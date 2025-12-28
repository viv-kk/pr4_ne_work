#ifndef HASHMAPIMPL_H
#define HASHMAPIMPL_H

#include "HashMap.h"

template<typename K, typename V>
HashMap<K, V>::Node::Node(const K& k, const V& v) : key(k), value(v), next(nullptr) {}

//констр копирования
template<typename K, typename V>
HashMap<K, V>::HashMap(const HashMap& other) 
    : bucketCount(other.bucketCount), itemCount(0) {
    //инициализация связных списков
    for (size_t i = 0; i < bucketCount; i++) {
        buckets.push_back(nullptr);//пустые
    }
    //коопируем все элементы из other
    auto otherItems = other.items();
    for (size_t i = 0; i < otherItems.size(); i++) {
        put(otherItems[i].first, otherItems[i].second);
    }
}
//оператор присваивания
template<typename K, typename V>
HashMap<K, V>& HashMap<K, V>::operator=(const HashMap& other) {
    if (this != &other) {
        clear();
        bucketCount = other.bucketCount;//новый размер
        itemCount = 0;
        //и нициализация св списков
        buckets.clear();
        for (size_t i = 0; i < bucketCount; i++) {
            buckets.push_back(nullptr);
        }
        //копируем
        auto otherItems = other.items();
        for (size_t i = 0; i < otherItems.size(); i++) {
            put(otherItems[i].first, otherItems[i].second);
        }
    }
    return *this;
}

//конст перемещения
template<typename K, typename V>
HashMap<K, V>::HashMap(HashMap&& other) noexcept 
    : buckets(std::move(other.buckets)), 
      bucketCount(other.bucketCount), 
      itemCount(other.itemCount) {
    other.bucketCount = 0;
    other.itemCount = 0;
}

//оператор перемещения
template<typename K, typename V>
HashMap<K, V>& HashMap<K, V>::operator=(HashMap&& other) noexcept {
    if (this != &other) {
        clear();
        //перемещаем из other
        buckets = std::move(other.buckets);
        bucketCount = other.bucketCount;
        itemCount = other.itemCount;
        other.bucketCount = 0;
        other.itemCount = 0;
    }
    return *this;
}

template<typename K, typename V>
size_t HashMap<K, V>::customHash(const string& str) const {
    size_t hash = 5381;
    const char* cStr = str.c_str();
    for (size_t i = 0; i < str.length(); i++) {
        hash = ((hash << 5) + hash) + cStr[i];
    }
    return hash;
}

template<typename K, typename V>
size_t HashMap<K, V>::getBucketIndex(const K& key) const {
    if (bucketCount == 0) return 0;
    return customHash(key) % bucketCount;
}

template<typename K, typename V>
void HashMap<K, V>::resize() {
    size_t newBucketCount = bucketCount * 2;
    if (newBucketCount == 0) newBucketCount = 8;//нач размер
    Vector<Node*> newBuckets;
    for (size_t i = 0; i < newBucketCount; i++) {
        newBuckets.push_back(nullptr);//пустые списки
    }
    for (size_t i = 0; i < bucketCount; i++) {//хеш
        Node* node = buckets[i];
        while (node) {
            Node* next = node->next;
            size_t newIndex = customHash(node->key) % newBucketCount;
            node->next = newBuckets[newIndex];//узел в начало цепочки нового списка
            newBuckets[newIndex] = node;
            node = next;
        }
    }
    
    buckets = newBuckets;
    bucketCount = newBucketCount;
}

template<typename K, typename V>
HashMap<K, V>::HashMap() : bucketCount(8), itemCount(0) {
    for (size_t i = 0; i < bucketCount; i++) {
        buckets.push_back(nullptr);
    }
}

template<typename K, typename V>
HashMap<K, V>::~HashMap() {
    clear();
}

template<typename K, typename V>
void HashMap<K, V>::put(const K& key, const V& value) {
    if (bucketCount == 0 || (double)itemCount / bucketCount > loadFactor) {
        resize();
    }
    
    size_t index = getBucketIndex(key);
    Node* node = buckets[index];
    
    while (node) {
        if (node->key == key) {
            node->value = value;
            return;
        }
        node = node->next;
    }
    
    Node* newNode = new Node(key, value);
    newNode->next = buckets[index];
    buckets[index] = newNode;
    itemCount++;
}

template<typename K, typename V>
bool HashMap<K, V>::get(const K& key, V& value) const {
    if (bucketCount == 0) return false;
    
    size_t index = getBucketIndex(key);
    Node* node = buckets[index];
    
    while (node) {
        if (node->key == key) {
            value = node->value;
            return true;
        }
        node = node->next;
    }
    return false;
}

template<typename K, typename V>
bool HashMap<K, V>::remove(const K& key) {
    if (bucketCount == 0) return false;
    
    size_t index = getBucketIndex(key);
    Node* node = buckets[index];
    Node* prev = nullptr;
    
    while (node) {
        if (node->key == key) {
            if (prev) {//середина или конец
                prev->next = node->next;
            } else {//начало
                buckets[index] = node->next;
            }
            delete node;
            itemCount--;
            return true;
        }
        prev = node;
        node = node->next;
    }
    return false;
}

template<typename K, typename V>
Vector<pair<K, V>> HashMap<K, V>::items() const {
    Vector<pair<K, V>> result;
    for (size_t i = 0; i < bucketCount; i++) {
        Node* node = buckets[i];
        while (node) {
            result.push_back(make_pair(node->key, node->value));
            node = node->next;
        }
    }
    return result;
}

template<typename K, typename V>
size_t HashMap<K, V>::size() const {
    return itemCount;
}

template<typename K, typename V>
void HashMap<K, V>::clear() {
    for (size_t i = 0; i < bucketCount; i++) {
        Node* node = buckets[i];
        while (node) {
            Node* next = node->next;
            delete node;
            node = next;
        }
        buckets[i] = nullptr;
    }
    itemCount = 0;
}

template<typename K, typename V>
bool HashMap<K, V>::contains(const K& key) const {
    V value;
    return get(key, value);
}

#endif
