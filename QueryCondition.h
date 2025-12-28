#ifndef QUERYCONDITION_H
#define QUERYCONDITION_H

#include "vector.h"
#include "HashMap.h"
using namespace std;

enum class ConditionType {
    EQUAL,
    GREATER_THAN,
    LESS_THAN,
    LIKE,
    IN,
    AND,
    OR
};

struct QueryCondition {
    ConditionType type;
    string field;
    string value;
    Vector<string> inValues;
    Vector<QueryCondition> subConditions;
    QueryCondition();
    
    QueryCondition(ConditionType t, const string& f = "", const string& v = "");
    QueryCondition(const QueryCondition& other);
    QueryCondition& operator=(const QueryCondition& other);
    QueryCondition(QueryCondition&& other) noexcept;
    QueryCondition& operator=(QueryCondition&& other) noexcept;
    
    ~QueryCondition() = default;    
};

class ConditionParser {
private:
    string jsonStr;
    size_t pos;
   
    void skipWhitespace();
    string parsestring();
    double parseNumber();
    bool parseBoolean();
    Vector<string> parseArray();
    QueryCondition parseConditionObject();
    
public:
    QueryCondition parse(const string& json);
};

#endif
