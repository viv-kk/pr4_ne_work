#include "collection.h"
#include "JsonParser.h"
#include <fstream>
#include <cstdio>
#include <string>
#include <algorithm>

Collection::Collection(const string& collectionName) : name(collectionName) {
    loadFromDisk();
}

bool Collection::loadFromDisk() {
    string filename = getFilename();
    std::ifstream file(filename.c_str());
    if (!file.is_open()) {
        return true;
    }
    
    string jsonContent;
    char buffer[4096];
    while (file.read(buffer, sizeof(buffer))) {
        jsonContent += string(buffer, file.gcount());
    }
    if (file.gcount() > 0) {
        jsonContent += string(buffer, file.gcount());
    }
    file.close();
    
    if (jsonContent.empty()) {
        return true;
    }
    
    //парсинг массива доков
    JsonParser parser;
    Vector<HashMap<string, string>> documentsArray = parser.parseArray(jsonContent);

    documents.clear();
    
    for (size_t i = 0; i < documentsArray.size(); i++) {//загрузка доков из массива
        HashMap<string, string> docData = documentsArray[i];
        string docId;
        
        if (!docData.get("_id", docId)) {
            static int counter = 0;
            docId = "doc_" + to_string(counter++);
        }
        
        Document doc(docData, docId);//создаем документ объекты в хэш мап
        documents.put(docId, doc);
    }
    
    return true;
}

bool Collection::saveToDisk() {
    string filename = getFilename();
    std::ofstream file(filename.c_str());
    if (!file.is_open()) {
        return false;
    }
    
    file << "[" << std::endl;
    auto items = documents.items();
    bool first = true;
    
    for (size_t i = 0; i < items.size(); i++) {
        if (!first) {
            file << "," << std::endl;
        }
        string jsonStr = items[i].second.to_json();
        file << " " << jsonStr.c_str();
        first = false;
    }
    file << "]" << std::endl;
    file.close();
    return true;
}

string Collection::getFilename() const {
    return name + ".json";
}

string Collection::insert(const string& jsonData) {
    JsonParser parser;
    HashMap<string, string> newDocData = parser.parse(jsonData);

    static int counter = 0;
    string docId = "doc_" + to_string(static_cast<int>(std::time(nullptr))) + 
                   "_" + to_string(std::rand() % 10000) + "_" + to_string(counter++);

    newDocData.put("_id", docId);
    
    Document newDoc(newDocData, docId);
    documents.put(docId, newDoc);
    
    if (saveToDisk()) {
        return string("Document inserted successfully.");
    } else {
        return string("Error: Failed to save document to disk.");
    }
}

Vector<Document> Collection::find(const QueryCondition& condition) {
    Vector<Document> results;
    auto items = documents.items();//все доки коллекции
    
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].second.matchesCondition(condition)) {
            results.push_back(items[i].second);//добавляем подходящие доки
        }
    }
    return results;
}

Vector<Document> Collection::find(const QueryCondition& condition, int page, int limit) {
    Vector<Document> allResults;
    auto items = documents.items();//все доки коллекции
    
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].second.matchesCondition(condition)) {
            allResults.push_back(items[i].second);//добавляем подходящие доки
        }
    }
    
    // Применяем пагинацию
    if (page > 0 && limit > 0) {
        int start_index = (page - 1) * limit;
        int end_index = std::min(start_index + limit, (int)allResults.size());
        
        if (start_index >= (int)allResults.size()) {
            return Vector<Document>(); // Пустой результат, если страница за пределами
        }
        
        Vector<Document> paginatedResults;
        for (int i = start_index; i < end_index; i++) {
            paginatedResults.push_back(allResults[i]);
        }
        return paginatedResults;
    }
    
    return allResults;
}

size_t Collection::count(const QueryCondition& condition) {
    size_t count = 0;
    auto items = documents.items();
    
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].second.matchesCondition(condition)) {
            count++;
        }
    }
    
    return count;
}

string Collection::remove(const QueryCondition& condition) {
    Vector<Document> toRemove = find(condition);// находим что удалить
    size_t count = toRemove.size();
    
    for (size_t i = 0; i < toRemove.size(); i++) {
        documents.remove(toRemove[i].getId());//удаляем из памяти
    }
    
    if (count > 0) {
        if (saveToDisk()) {
            return to_string(count) + string(" document(s) deleted successfully.");
        } else {
            return string("Error: Failed to save changes to disk.");
        }
    } else {
        return "No documents found matching the condition.";
    }
}

size_t Collection::size() const {
    return documents.size();
}