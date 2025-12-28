#ifndef VECTORIMPL_H
#define VECTORIMPL_H

#include "vector.h"

//конст копирования
template<typename T>
Vector<T>::Vector(const Vector& other) 
    : capacity(other.capacity), sizeVal(other.sizeVal) {
    data = new T[capacity];
    for (size_t i = 0; i < sizeVal; i++) {
        data[i] = other.data[i];
    }
}

//опер присваивания
template<typename T>
Vector<T>& Vector<T>::operator=(const Vector& other) {
    if (this != &other) {
        delete[] data;
        capacity = other.capacity;
        sizeVal = other.sizeVal;
        data = new T[capacity];
        for (size_t i = 0; i < sizeVal; i++) {
            data[i] = other.data[i];
        }
    }
    return *this;
}

//конст перемещения
template<typename T>
Vector<T>::Vector(Vector&& other) noexcept 
    : data(other.data), capacity(other.capacity), sizeVal(other.sizeVal) {
    other.data = nullptr;
    other.capacity = 0;
    other.sizeVal = 0;
}

//оператор перемещения
template<typename T>
Vector<T>& Vector<T>::operator=(Vector&& other) noexcept {
    if (this != &other) {
        delete[] data;
        data = other.data;
        capacity = other.capacity;
        sizeVal = other.sizeVal;
        other.data = nullptr;
        other.capacity = 0;
        other.sizeVal = 0;
    }
    return *this;
}

template<typename T>
Vector<T>::Vector() : data(nullptr), capacity(0), sizeVal(0) {}

template<typename T>
Vector<T>::~Vector() {
    delete[] data;
}

template<typename T>
void Vector<T>::push_back(const T& value) {
    if (sizeVal >= capacity) {
        size_t newCapacity = capacity == 0 ? 1 : capacity * 2;
        T* newData = new T[newCapacity];
        for (size_t i = 0; i < sizeVal; i++) {
            newData[i] = std::move(data[i]);  // используем перемещение
        }
        delete[] data;
        data = newData;
        capacity = newCapacity;
    }
    data[sizeVal++] = value;
}

// Новая перегрузка для перемещения
template<typename T>
void Vector<T>::push_back(T&& value) {
    if (sizeVal >= capacity) {
        size_t newCapacity = capacity == 0 ? 1 : capacity * 2;
        T* newData = new T[newCapacity];
        for (size_t i = 0; i < sizeVal; i++) {
            newData[i] = std::move(data[i]);  // используем перемещение
        }
        delete[] data;
        data = newData;
        capacity = newCapacity;
    }
    data[sizeVal++] = std::move(value);  // используем перемещение
}

// Новый метод pop_back
template<typename T>
void Vector<T>::pop_back() {
    if (sizeVal > 0) {
        sizeVal--;
    }
}

// Новый метод back
template<typename T>
T& Vector<T>::back() {
    return data[sizeVal - 1];
}

template<typename T>
const T& Vector<T>::back() const {
    return data[sizeVal - 1];
}

template<typename T>
T& Vector<T>::operator[](size_t index) {
    return data[index];
}

template<typename T>
const T& Vector<T>::operator[](size_t index) const {
    return data[index];
}

template<typename T>
size_t Vector<T>::size() const {
    return sizeVal;
}

template<typename T>
bool Vector<T>::empty() const {
    return sizeVal == 0;
}

template<typename T>
void Vector<T>::clear() {
    delete[] data;
    data = nullptr;
    capacity = 0;
    sizeVal = 0;
}

template<typename T>
Vector<T>::Iterator::Iterator(T* p) : ptr(p) {}

template<typename T>
T& Vector<T>::Iterator::operator*() { 
    return *ptr; 
}

template<typename T>
typename Vector<T>::Iterator& Vector<T>::Iterator::operator++() { 
    ptr++; 
    return *this; 
}

template<typename T>
bool Vector<T>::Iterator::operator!=(const Iterator& other) { 
    return ptr != other.ptr; 
}

template<typename T>
typename Vector<T>::Iterator Vector<T>::begin() { 
    return Iterator(data); 
}

template<typename T>
typename Vector<T>::Iterator Vector<T>::end() { 
    return Iterator(data + sizeVal); 
}

#endif