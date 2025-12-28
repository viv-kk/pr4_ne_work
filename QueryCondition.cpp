#include "QueryCondition.h"
#include <cctype>

QueryCondition::QueryCondition()
    : type(ConditionType::EQUAL), field(""), value("") {
}

QueryCondition::QueryCondition(ConditionType t, const string& f, const string& v) 
    : type(t), field(f), value(v) {}


QueryCondition::QueryCondition(const QueryCondition& other)
    : type(other.type), field(other.field), value(other.value) {
   

    for (size_t i = 0; i < other.inValues.size(); i++) {
        inValues.push_back(other.inValues[i]);
    }
    

    for (size_t i = 0; i < other.subConditions.size(); i++) {
        subConditions.push_back(other.subConditions[i]);
    }
}


QueryCondition& QueryCondition::operator=(const QueryCondition& other) {
    if (this != &other) {
        type = other.type;
        field = other.field;
        value = other.value;
        

        inValues.clear();
        for (size_t i = 0; i < other.inValues.size(); i++) {
            inValues.push_back(other.inValues[i]);
        }
        
        subConditions.clear();
        for (size_t i = 0; i < other.subConditions.size(); i++) {
            subConditions.push_back(other.subConditions[i]);
        }
    }
    return *this;
}

QueryCondition::QueryCondition(QueryCondition&& other) noexcept
    : type(other.type), 
      field(std::move(other.field)), 
      value(std::move(other.value)),
      inValues(std::move(other.inValues)),
      subConditions(std::move(other.subConditions)) {
}

QueryCondition& QueryCondition::operator=(QueryCondition&& other) noexcept {
    if (this != &other) {
        type = other.type;
        field = std::move(other.field);
        value = std::move(other.value);
        inValues = std::move(other.inValues);
        subConditions = std::move(other.subConditions);
    }
    return *this;
}

void ConditionParser::skipWhitespace() {
    while (pos < jsonStr.length() && isspace(jsonStr[pos])) {
        pos++;
    }
}

string ConditionParser::parsestring() {
    if (jsonStr[pos] != '"') return "";
    pos++;
    
    string result;
    while (pos < jsonStr.length() && jsonStr[pos] != '"') {
        if (jsonStr[pos] == '\\') {
            pos++;
        }
        result += jsonStr[pos];
        pos++;
    }
    pos++;
    return result;
}

double ConditionParser::parseNumber() {
    size_t start = pos;
    while (pos < jsonStr.length() && 
           (isdigit(jsonStr[pos]) || jsonStr[pos] == '.' || 
            jsonStr[pos] == '-' || jsonStr[pos] == '+')) {
        pos++;
    }
    string numStr = jsonStr.substr(start, pos - start);
    return stod(numStr);
}

bool ConditionParser::parseBoolean() {
    if (pos + 4 <= jsonStr.length() && jsonStr.substr(pos, 4) == "true") {
        pos += 4;
        return true;
    } else if (pos + 5 <= jsonStr.length() && jsonStr.substr(pos, 5) == "false") {
        pos += 5;
        return false;
    }
    return false;
}

Vector<string> ConditionParser::parseArray() {
    Vector<string> result;
    
    if (jsonStr[pos] != '[') return result;
    pos++;
    
    while (pos < jsonStr.length()) {
        skipWhitespace();
        if (jsonStr[pos] == ']') {
            pos++;
            break;
        }
        
        if (jsonStr[pos] == '"') {
            result.push_back(parsestring());
        } else if (isdigit(jsonStr[pos]) || jsonStr[pos] == '-' || jsonStr[pos] == '+') {
            double num = parseNumber();
            result.push_back(to_string(num));
        }
        
        skipWhitespace();
        if (jsonStr[pos] == ',') {
            pos++;
        } else if (jsonStr[pos] == ']') {
            pos++;
            break;
        }
    }
    return result;
}

QueryCondition ConditionParser::parseConditionObject() {
    skipWhitespace();
    
    if (jsonStr[pos] == '{') {
        pos++;
        skipWhitespace();
    }
    
    QueryCondition condition(ConditionType::AND);
    
    while (pos < jsonStr.length()) {
        skipWhitespace();
        if (jsonStr[pos] == '}') {
            pos++;
            break;
        }
        
        string key = parsestring();
        skipWhitespace();
        
        if (jsonStr[pos] != ':') break;
        pos++;
        skipWhitespace();
        
        if (key == "$or") {
            if (jsonStr[pos] == '[') {
                pos++;
                QueryCondition orCondition(ConditionType::OR);
                
                while (pos < jsonStr.length()) {
                    skipWhitespace();
                    if (jsonStr[pos] == ']') {
                        pos++;
                        break;
                    }
                    
                    orCondition.subConditions.push_back(parseConditionObject());
                    skipWhitespace();
                    
                    if (jsonStr[pos] == ',') {
                        pos++;
                    } else if (jsonStr[pos] == ']') {
                        pos++;
                        break;
                    }
                }
                condition.subConditions.push_back(orCondition);
            }
        }
        else if (key == "$and") {
            if (jsonStr[pos] == '[') {
                pos++;
                QueryCondition andCondition(ConditionType::AND);
                
                while (pos < jsonStr.length()) {
                    skipWhitespace();
                    if (jsonStr[pos] == ']') {
                        pos++;
                        break;
                    }
                    
                    andCondition.subConditions.push_back(parseConditionObject());
                    skipWhitespace();
                    
                    if (jsonStr[pos] == ',') {
                        pos++;
                    } else if (jsonStr[pos] == ']') {
                        pos++;
                        break;
                    }
                }
                condition.subConditions.push_back(andCondition);
            }
        }
        else {
            if (jsonStr[pos] == '{') {
                pos++;
                skipWhitespace();
                
                string operatorKey = parsestring();
                skipWhitespace();
                
                if (jsonStr[pos] != ':') break;
                pos++;
                skipWhitespace();
                
                QueryCondition subCondition(ConditionType::EQUAL, key, "");
                
                if (operatorKey == "$eq") {
                    subCondition.type = ConditionType::EQUAL;
                    if (jsonStr[pos] == '"') {
                        subCondition.value = parsestring();
                    } else {
                        double num = parseNumber();
                        subCondition.value = to_string(num);
                    }
                }
                else if (operatorKey == "$gt") {
                    subCondition.type = ConditionType::GREATER_THAN;
                    if (jsonStr[pos] == '"') {
                        subCondition.value = parsestring();
                    } else {
                        double num = parseNumber();
                        subCondition.value = to_string(num);
                    }
                }
                else if (operatorKey == "$lt") {
                    subCondition.type = ConditionType::LESS_THAN;
                    if (jsonStr[pos] == '"') {
                        subCondition.value = parsestring();
                    } else {
                        double num = parseNumber();
                        subCondition.value = to_string(num);
                    }
                }
                else if (operatorKey == "$like") {
                    subCondition.type = ConditionType::LIKE;
                    subCondition.value = parsestring();
                }
                else if (operatorKey == "$in") {
                    subCondition.type = ConditionType::IN;
                    subCondition.inValues = parseArray();
                }
                
                condition.subConditions.push_back(subCondition);
                skipWhitespace();
                if (jsonStr[pos] == '}') pos++;
            } else {
                QueryCondition subCondition(ConditionType::EQUAL, key, "");
                if (jsonStr[pos] == '"') {
                    subCondition.value = parsestring();
                } else {
                    double num = parseNumber();
                    subCondition.value = to_string(num);
                }
                condition.subConditions.push_back(subCondition);
            }
        }
        
        skipWhitespace();
        if (jsonStr[pos] == ',') {
            pos++;
        } else if (jsonStr[pos] == '}') {
            pos++;
            break;
        }
    }
    
    return condition;
}

QueryCondition ConditionParser::parse(const string& json) {
    jsonStr = json;
    pos = 0;
    skipWhitespace();
    return parseConditionObject();
}
