#include "document.h"
#include "JsonParser.h"

Document::Document() {
    static int counter = 0;
    id = "doc_" + to_string(counter++);
}

Document::Document(const string& jsonStr) {
    static int counter = 0;
    id = "doc_" + to_string(counter++);
    JsonParser parser;
    data = parser.parse(jsonStr);
}

Document::Document(const HashMap<string, string>& dataMap, const string& docId) {
    if (docId.empty()) {
        static int counter = 0;
        id = "doc_" + to_string(counter++);
    } else {
        id = docId;
    }
    data = dataMap;
}

string Document::getId() const {
    return id;
}

void Document::setData(const HashMap<string, string>& newData) {
    data = newData;
}

HashMap<string, string> Document::getData() const {
    return data;
}

string Document::to_json() const {
    string json = "{";
    auto items = data.items();
    bool first = true;
    
    if (!first) json += ",";//1-id
    json += "\"_id\":\"" + id + "\"";
    first = false;
    
    for (size_t i = 0; i < items.size(); i++) {//остальные поля
        if (items[i].first != "_id") {
            if (!first) {
                json += ",";
            }
            json += "\"" + items[i].first + "\":\"" + items[i].second + "\"";
            first = false;
        }
    }
    json += "}";
    return json;
}

bool Document::likeMatch(const string& value, const string& pattern) const {
    const char* valueStr = value.c_str();
    const char* patternStr = pattern.c_str();
    
    size_t vPos = 0, pPos = 0;
    size_t vLen = value.length(), pLen = pattern.length();
    
    while (pPos < pLen && vPos < vLen) {
        if (patternStr[pPos] == '%') {
            if (pPos == pLen - 1) return true;//в конце всегда тру
            
            size_t nextP = pPos + 1;
            while (nextP < pLen && patternStr[nextP] == '%') nextP++;//если повторяются
            if (nextP >= pLen) return true;
            //след символ после процента в стр значения
            while (vPos < vLen && valueStr[vPos] != patternStr[nextP]) {
                vPos++;
            }
            if (vPos >= vLen) return false;
            
            pPos = nextP;
        }
        else if (patternStr[pPos] == '_') {
            vPos++;
            pPos++;
        }
        else if (patternStr[pPos] == valueStr[vPos]) {//совпадение
            vPos++;
            pPos++;
        }
        else {
            return false;
        }
    }
    
    return (vPos == vLen && pPos == pLen) || 
           (pPos == pLen - 1 && patternStr[pPos] == '%');
}

bool Document::compareValues(const string& actual, const string& expected, ConditionType op) const {
    switch (op) {
        case ConditionType::EQUAL://равенство строк
            return actual == expected;
        
        case ConditionType::GREATER_THAN://больше
            try {
                double a = stod(actual);
                double b = stod(expected);
                return a > b;
            } catch (...) {
                return actual > expected;
            }
        
        case ConditionType::LESS_THAN://меньше
            try {
                double a = stod(actual);
                double b = stod(expected);
                return a < b;
            } catch (...) {
                return actual < expected;
            }
        
        case ConditionType::LIKE:
            return likeMatch(actual, expected);
        
        default:
            return false;
    }
}

bool Document::evaluateCondition(const QueryCondition& condition, const HashMap<string, string>& docData) const {
    switch (condition.type) {
        case ConditionType::EQUAL:
        case ConditionType::GREATER_THAN:
        case ConditionType::LESS_THAN:
        case ConditionType::LIKE: {
            string actualValue;
            if (!docData.get(condition.field, actualValue)) {
                return false;
            }
            return compareValues(actualValue, condition.value, condition.type);
        }
        
        case ConditionType::IN: {
            string actualValue;
            if (!docData.get(condition.field, actualValue)) {
                return false;
            }
            for (size_t i = 0; i < condition.inValues.size(); i++) {
                if (actualValue == condition.inValues[i]) {
                    return true;//совпало
                }
            }
            return false;//не нашли
        }
        
        case ConditionType::AND: {
            for (size_t i = 0; i < condition.subConditions.size(); i++) {
                if (!evaluateCondition(condition.subConditions[i], docData)) {
                    return false;
                }
            }
            return true;
        }
        
        case ConditionType::OR: {
            for (size_t i = 0; i < condition.subConditions.size(); i++) {
                if (evaluateCondition(condition.subConditions[i], docData)) {
                    return true;
                }
            }
            return false;
        }
        
        default:
            return false;
    }
}

bool Document::matchesCondition(const QueryCondition& condition) const {
    return evaluateCondition(condition, data);
}
