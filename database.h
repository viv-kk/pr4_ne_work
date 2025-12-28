#ifndef DATABASE_H
#define DATABASE_H

#include "collection.h"
#include "HashMap.h"
#include <filesystem>
using namespace std;

class Database {
private:
    string name;
    HashMap<string, Collection*> collections;
    
    void ensureDirectory();

public:
    Database(const string& dbName);
    ~Database();
    Collection& getCollection(const string& collectionName);
    string getName() const { return name; }
};

#endif
