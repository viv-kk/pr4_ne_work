#include "database.h"
#include <sys/stat.h>
#include <sys/types.h>

Database::Database(const string& dbName) : name(dbName) {
    ensureDirectory();
}

Database::~Database() {
    auto items = collections.items();
    for (size_t i = 0; i < items.size(); i++) {
        delete items[i].second;
    }
}

void Database::ensureDirectory() {
    struct stat st;
    if (stat(name.c_str(), &st) != 0) {
#ifdef _WIN32
        _mkdir(name.c_str());
#else
        mkdir(name.c_str(), 0755);
#endif
    }
}

Collection& Database::getCollection(const string& collectionName) {
    Collection* coll = nullptr;
    if (collections.get(collectionName, coll)) {
        return *coll;
    } else {
        //cоздаем новую коллекцию
        string fullPath = name + "/" + collectionName;
        Collection* newCollection = new Collection(fullPath);
        collections.put(collectionName, newCollection);
        return *newCollection;
    }
}
