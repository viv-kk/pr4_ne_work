#ifndef VECTOR_H
#define VECTOR_H

#include <string>
using namespace std;

template<typename T>
class Vector {
private:
    T* data;
    size_t capacity;
    size_t sizeVal;

public:
    Vector();
    ~Vector();
    Vector(const Vector& other);//копир
    Vector& operator=(const Vector& other);//присванвание
    Vector(Vector&& other) noexcept;//перемещение
    Vector& operator=(Vector&& other) noexcept;//операторп рисваивания перемещением
    void push_back(const T& value);
    void push_back(T&& value);  // новая перегрузка для перемещения
    void pop_back();  // новый метод
    T& back();  // новый метод
    const T& back() const;  // новый метод
    T& operator[](size_t index);
    const T& operator[](size_t index) const;
    size_t size() const;
    bool empty() const;
    void clear();

    class Iterator {
    private:
        T* ptr;
    public:
        Iterator(T* p);
        T& operator*();
        Iterator& operator++();
        bool operator!=(const Iterator& other);
    };
    
    Iterator begin();
    Iterator end();
};

#include "VectorImpl.h"

#endif