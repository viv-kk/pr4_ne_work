#ifndef COLLECTION_H
#define COLLECTION_H

#include "document.h"
#include "HashMap.h"
#include "vector.h"
#include "QueryCondition.h"
#include <string>

using namespace std;

class Collection {
private:
    string name;
    HashMap<string, Document> documents;
    
    string getFilename() const;
    bool saveToDisk();
    
public:
    Collection(const string& collectionName);
    
    bool loadFromDisk();
    string insert(const string& jsonData);
    Vector<Document> find(const QueryCondition& condition);
    Vector<Document> find(const QueryCondition& condition, int page, int limit); // Новый метод с пагинацией
    size_t count(const QueryCondition& condition); // Новый метод для подсчета
    string remove(const QueryCondition& condition);
    size_t size() const;
};

#endif // COLLECTION_H