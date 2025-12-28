#ifndef DOCUMENT_H
#define DOCUMENT_H

#include "HashMap.h"
#include "QueryCondition.h"
#include <string>
#include <ctime>
#include <cstdlib>
using namespace std;

class Document {
private:
    HashMap<string, string> data;
    string id;

    bool evaluateCondition(const QueryCondition& condition, const HashMap<string, string>& docData) const;
    bool compareValues(const string& actual, const string& expected, ConditionType op) const;
    bool likeMatch(const string& value, const string& pattern) const;

public:
    Document();
    Document(const string& jsonStr);
    Document(const HashMap<string, string>& dataMap, const string& docId = "");
    Document(const Document& other) = default;
    Document& operator=(const Document& other) = default;
    Document(Document&& other) noexcept = default;
    Document& operator=(Document&& other) noexcept = default;
    string getId() const;
    void setData(const HashMap<string, string>& newData);
    HashMap<string, string> getData() const;
    string to_json() const;
    bool matchesCondition(const QueryCondition& condition) const;
};

#endif
