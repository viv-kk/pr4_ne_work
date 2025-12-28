#ifndef NETWORK_PROTOCOL_H
#define NETWORK_PROTOCOL_H

#include "vector.h"
#include "HashMap.h"
#include <string>

using namespace std;

string escapeJsonString(const string& str);

class Request {
public:
    string database;
    string operation;
    string collection;
    string query;
    Vector<string> data;
    // Поля пагинации
    int page = 1;
    int limit = 50;
    
    string toJson() const;
    static Request fromJson(const string& jsonStr);
};

class Response {
public:
    string status;
    string message;
    size_t count = 0;
    Vector<string> data;
    // Поля пагинации
    int total_pages = 0;
    int current_page = 1;
    int per_page = 50;
    size_t total_count = 0;
    
    string toJson() const;
    static Response fromJson(const string& jsonStr);
};

bool isValidJsonString(const string& str);

#endif // NETWORK_PROTOCOL_H