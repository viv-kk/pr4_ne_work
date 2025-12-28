#include "JsonParser.h"
#include <cctype>
#include <string>
#include <cstdlib>
#include <sstream>
#include <iostream>

bool isPotentialValidNumber(const std::string& str) {
    if (str.empty()) return false;

    size_t i = 0;
    if (i < str.length() && (str[i] == '+' || str[i] == '-')) {
        i++;
    }

    bool hasDigits = false;
    while (i < str.length() && std::isdigit(str[i])) {
        hasDigits = true;
        i++;
    }

    if (i < str.length() && str[i] == '.') {
        i++;
        while (i < str.length() && std::isdigit(str[i])) {
            hasDigits = true;
            i++;
        }
    }

    if (!hasDigits) return false;

    if (i < str.length() && (str[i] == 'e' || str[i] == 'E')) {
        i++;
        if (i < str.length() && (str[i] == '+' || str[i] == '-')) {
            i++;
        }
        bool hasExpDigits = false;
        while (i < str.length() && std::isdigit(str[i])) {
            hasExpDigits = true;
            i++;
        }
        if (!hasExpDigits) return false;
    }

    if (i < str.length() && std::isalpha(str[i])) {
        return false;
    }

    return i == str.length();
}

std::string parseNumber(std::istream& is) {
    std::string numStr;
    char c;

    auto startPos = is.tellg();
    if (is.peek() == '+' || is.peek() == '-') {
        is.get(c);
        numStr += c;
    }

    bool hasDigit = false;
    while (std::isdigit(is.peek())) {
        is.get(c);
        numStr += c;
        hasDigit = true;
    }

    if (hasDigit && std::isalpha(is.peek())) {
        is.seekg(startPos);
        throw std::runtime_error("Not a valid number");
    }

    if (!hasDigit && std::isalpha(is.peek())) {
        is.seekg(startPos);
        throw std::runtime_error("Not a valid number");
    }

    if (hasDigit && !std::isdigit(is.peek()) && is.peek() != '.' && is.peek() != 'e' && is.peek() != 'E') {
        if (std::isalpha(is.peek())) {
            is.seekg(startPos);
            throw std::runtime_error("Not a valid number");
        }
    }

    if (is.peek() == '.') {
        is.get(c);
        numStr += c;
        while (std::isdigit(is.peek())) {
            is.get(c);
            numStr += c;
        }
    }

    if (is.peek() == 'e' || is.peek() == 'E') {
        is.get(c);
        numStr += c;
        if (is.peek() == '+' || is.peek() == '-') {
            is.get(c);
            numStr += c;
        }
        while (std::isdigit(is.peek())) {
            is.get(c);
            numStr += c;
        }
    }

    return numStr;
}

std::string parseString(std::istream& is) {
    std::string str;
    char c;
    is.get(c); 
    while (is.get(c) && c != '"') {
        if (c == '\\') {
            if (is.get(c)) {
                switch (c) {
                    case 't':
                        str += '\t';
                        break;
                    case 'n':
                        str += '\n';
                        break;
                    case 'r':
                        str += '\r';
                        break;
                    case 'b':
                        str += '\b';
                        break;
                    case 'f':
                        str += '\f';
                        break;
                    case '"':
                        str += '"';
                        break;
                    case '\\':
                        str += '\\';
                        break;
                    case '/':
                        str += '/';
                        break;
                    case 'u': {
                        std::string unicode = "u";
                        for (int i = 0; i < 4; ++i) {
                            if (is.get(c)) {
                                unicode += c;
                            } else {
                                throw std::runtime_error("Invalid Unicode escape sequence");
                            }
                        }
                        str += "\\u" + unicode.substr(1);
                        break;
                    }
                    default:
                        str += c;
                        break;
                }
            }
        } else {
            str += c;
        }
    }
    return str;
}

std::string extractJsonValue(const std::string& jsonStr, const std::string& key) {
    std::istringstream iss(jsonStr);
    char c;
    std::string currentKey;
    bool inValue = false;
    int nestingLevel = 0;
    std::string value;

    while (iss.get(c)) {
        if (!inValue) {
            if (c == '"') {
                currentKey = parseString(iss);
                while (std::isspace(iss.peek()) || iss.peek() == ':') {
                    if (iss.get(c) && iss.peek() != ':') continue;
                    else break;
                }
                if (currentKey == key) {
                    inValue = true;
                }
            }
        } else {
            if (c == '{') {
                nestingLevel++;
                value += c;
            } else if (c == '}') {
                nestingLevel--;
                value += c;
                if (nestingLevel == 0) {
                    return value;
                }
            } else if (c == '[') {
                nestingLevel++;
                value += c;
            } else if (c == ']') {
                nestingLevel--;
                value += c;
                if (nestingLevel == 0) {
                    return value;
                }
            } else if (c == '"') {
                value += '"';
                value += parseString(iss);
                value += '"';
                if (nestingLevel == 0) {
                    return value;
                }
            } else if (std::isdigit(c) || c == '+' || c == '-' || c == '.') {
                std::string numCheck = c + std::string(1, static_cast<char>(iss.peek()));
                if (isPotentialValidNumber(numCheck)) {
                    try {
                        std::string num = parseNumber(iss);
                        value += num;
                        if (nestingLevel == 0) {
                            return value;
                        }
                    } catch (const std::exception&) {
                        value += c;
                    }
                } else {
                    value += c;
                }
            } else if (c == 't' || c == 'f' || c == 'n') { 
                std::string literal;
                literal += c;
                for(int i = 0; i < 4; ++i) { 
                    if (std::isalpha(iss.peek())) {
                        iss.get(c);
                        literal += c;
                    }
                }
                if (literal.substr(0, 4) == "true" || literal.substr(0, 5) == "false" || literal.substr(0, 4) == "null") {
                    value += literal;
                    if (nestingLevel == 0) {
                        return value;
                    }
                } else {
                    value += literal;
                }
            } else if (!std::isspace(c)) {
                value += c;
            }
        }
    }
    return "";
}

Vector<std::string> parsestringArray(const std::string& jsonString) {
    Vector<std::string> result;
    std::istringstream iss(jsonString);
    char c;

    while (std::isspace(iss.peek())) {
        iss.get(c);
    }
    if (iss.get(c) && c != '[') {
        return result; 
    }

    std::string currentValue;
    int nestingLevel = 0;
    bool inString = false;

    while (iss.get(c)) {
        if (c == '"') {
            if (!inString) {
                inString = true;
                currentValue += c;
                while (iss.get(c) && c != '"') {
                    currentValue += c;
                    if (c == '\\') {
                        if (iss.get(c)) {
                            currentValue += c;
                        }
                    }
                }
                currentValue += c; 
                inString = false;
            }
        } else if (c == '[' || c == '{') {
            nestingLevel++;
            currentValue += c;
        } else if (c == ']' || c == '}') {
            nestingLevel--;
            currentValue += c;
        } else if (c == ',' && nestingLevel == 0 && !inString) {
            result.push_back(currentValue);
            currentValue.clear();
            while (std::isspace(iss.peek())) {
                iss.get(c);
            }
            continue;
        } else if (!inString && (std::isdigit(c) || c == '+' || c == '-' || c == '.')) {
            std::string numCheck = c + std::string(1, static_cast<char>(iss.peek()));
            if (isPotentialValidNumber(numCheck)) {
                auto posBeforeParse = iss.tellg(); 
                try {
                    std::string num = parseNumber(iss);
                    currentValue += num;
                } catch (const std::exception&) {
                    iss.seekg(posBeforeParse);
                    currentValue += c;
                }
            } else {
                currentValue += c;
            }
        } else {
            currentValue += c;
        }

        if (c == ']' && nestingLevel == -1) {
            if (!currentValue.empty()) {
                size_t start = currentValue.find_first_not_of(" \t\n\r");
                if (start != std::string::npos) {
                    currentValue = currentValue.substr(start);
                }
                result.push_back(currentValue);
            }
            break;
        }
    }

    return result;
}

HashMap<string, string> JsonParser::parseSingleObject() {
    HashMap<string, string> result;

    if (jsonStr[pos] != '{') return result;
    pos++;

    while (pos < jsonStr.length()) {
        skipWhitespace();
        if (jsonStr[pos] == '}') {
            pos++;
            break;
        }

        string key = parsestring();
        skipWhitespace();

        if (jsonStr[pos] != ':') {
            // Пропускаем некорректные данные
            while (pos < jsonStr.length() && jsonStr[pos] != ',' && jsonStr[pos] != '}') {
                pos++;
            }
            if (jsonStr[pos] == ',') {
                pos++;
                continue;
            } else if (jsonStr[pos] == '}') {
                pos++;
                break;
            }
        }

        pos++; // пропускаем ':'
        skipWhitespace();

        if (jsonStr[pos] == '"') {
            // Строковое значение
            string value = parsestring();
            result.put(key, value);
        } else if ((isdigit(jsonStr[pos]) || jsonStr[pos] == '-' || jsonStr[pos] == '+')) {
            // Проверяем, не является ли это датой ISO 8601
            if (pos + 4 < jsonStr.length() && 
                isdigit(jsonStr[pos]) && isdigit(jsonStr[pos+1]) && 
                isdigit(jsonStr[pos+2]) && isdigit(jsonStr[pos+3]) &&
                jsonStr[pos+4] == '-') {
                // Это похоже на дату ISO 8601, читаем как строку
                size_t start = pos;
                while (pos < jsonStr.length() && 
                    jsonStr[pos] != ',' && jsonStr[pos] != '}' && 
                    jsonStr[pos] != ']' && !isspace(jsonStr[pos])) {
                    pos++;
                }
                string dateStr = jsonStr.substr(start, pos - start);
                result.put(key, dateStr);
            } else {
                // Обычное число
                std::string numStr = getCurrentNumberString();
                if (isPotentialValidNumber(numStr)) {
                    result.put(key, numStr);
                    pos += numStr.length();
                } else {
                    pos++;
                }
            }
        } else if (jsonStr[pos] == 't' || jsonStr[pos] == 'f') {
            // Булево значение
            bool b = parseBoolean();
            result.put(key, b ? "true" : "false");
        } else if (jsonStr[pos] == 'n') {
            // null значение
            parseNull();
            result.put(key, "null");
        } else if (jsonStr[pos] == '[') {
            // Массив
            Vector<string> arrayValues = parsestringArray();
            string arrayStr = "[";
            for (size_t i = 0; i < arrayValues.size(); i++) {
                if (i > 0) arrayStr += ",";
                
                // Проверяем, нужно ли экранировать элемент массива
                const string& elem = arrayValues[i];
                if (elem.empty() || 
                    (elem[0] != '{' && elem[0] != '[' &&
                     elem != "true" && elem != "false" &&
                     elem != "null" && !isdigit(elem[0]) &&
                     elem[0] != '-')) {
                    // Это строка - экранируем
                    arrayStr += "\"" + elem + "\"";
                } else {
                    // Это JSON объект/массив или примитив
                    arrayStr += elem;
                }
            }
            arrayStr += "]";
            result.put(key, arrayStr);
        } else if (jsonStr[pos] == '{') {
            // Вложенный объект
            size_t start = pos;
            int braceCount = 0;
            bool inString = false;
            bool escaped = false;

            do {
                char c = jsonStr[pos];
                
                if (escaped) {
                    escaped = false;
                    pos++;
                    continue;
                }
                
                if (c == '\\') {
                    escaped = true;
                    pos++;
                    continue;
                }
                
                if (c == '"' && !escaped) {
                    inString = !inString;
                    pos++;
                    continue;
                }
                
                if (!inString) {
                    if (c == '{') braceCount++;
                    else if (c == '}') braceCount--;
                }
                
                pos++;
            } while (pos < jsonStr.length() && braceCount > 0);

            if (braceCount == 0) {
                string objStr = jsonStr.substr(start, pos - start);
                result.put(key, objStr);
            } else {
                // Невалидный JSON, пропускаем
                while (pos < jsonStr.length() && jsonStr[pos] != ',' && jsonStr[pos] != '}') {
                    pos++;
                }
            }
        } else {
            // Нераспознанное значение, пропускаем
            while (pos < jsonStr.length() && jsonStr[pos] != ',' && jsonStr[pos] != '}') {
                pos++;
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
    return result;
}

std::string JsonParser::getCurrentNumberString() {
    size_t start = pos;

    if (pos < jsonStr.length() && (jsonStr[pos] == '-' || jsonStr[pos] == '+')) {
        pos++;
    }

    bool hasDigits = false;
    while (pos < jsonStr.length() && isdigit(jsonStr[pos])) {
        pos++;
        hasDigits = true;
    }

    if (!hasDigits) {
        pos = start; 
        return "";
    }

    if (pos < jsonStr.length() && jsonStr[pos] == '.') {
        pos++;
        while (pos < jsonStr.length() && isdigit(jsonStr[pos])) {
            pos++;
        }
    }

    if (pos < jsonStr.length() && (jsonStr[pos] == 'e' || jsonStr[pos] == 'E')) {
        pos++;
        if (pos < jsonStr.length() && (jsonStr[pos] == '+' || jsonStr[pos] == '-')) {
            pos++;
        }
        while (pos < jsonStr.length() && isdigit(jsonStr[pos])) {
            pos++;
        }
    }

    // Проверяем, что после числа нет букв
    if (pos < jsonStr.length() && isalpha(jsonStr[pos])) {
        // Это не число, откатываем позицию
        pos = start;
        return "";
    }

    std::string result = jsonStr.substr(start, pos - start);

    // Откатываем pos обратно, так как этот метод только проверяет
    pos = start;

    return result;
}

bool JsonParser::isPotentialValidNumber(const std::string& str) {
    if (str.empty()) return false;

    size_t i = 0;
    if (i < str.length() && (str[i] == '+' || str[i] == '-')) {
        i++;
    }

    bool hasDigits = false;
    while (i < str.length() && isdigit(str[i])) {
        hasDigits = true;
        i++;
    }

    if (!hasDigits) return false;

    if (i < str.length() && str[i] == '.') {
        i++;
        while (i < str.length() && isdigit(str[i])) {
            hasDigits = true;
            i++;
        }
    }

    if (!hasDigits) return false;

    if (i < str.length() && (str[i] == 'e' || str[i] == 'E')) {
        i++;
        if (i < str.length() && (str[i] == '+' || str[i] == '-')) {
            i++;
        }
        bool hasExpDigits = false;
        while (i < str.length() && isdigit(str[i])) {
            hasExpDigits = true;
            i++;
        }
        if (!hasExpDigits) return false;
    }

    // Проверяем, что после числа нет букв
    if (i < str.length() && isalpha(str[i])) {
        return false;
    }

    return i == str.length();
}

void JsonParser::skipWhitespace() {
    while (pos < jsonStr.length() && isspace(jsonStr[pos])) {
        pos++;
    }
}

string JsonParser::parsestring() {
    if (jsonStr[pos] != '"') return "";
    pos++;

    string result;
    while (pos < jsonStr.length() && jsonStr[pos] != '"') {
        if (jsonStr[pos] == '\\') {
            pos++;
            if (pos >= jsonStr.length()) break;
            switch(jsonStr[pos]) {
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case 'r': result += '\r'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case '\\': result += '\\'; break;
                case '"': result += '"'; break;
                case '/': result += '/'; break;
                case 'u':
                    pos++;
                    for (int i = 0; i < 4 && pos < jsonStr.length(); i++) {
                        if (isxdigit(jsonStr[pos])) pos++;
                    }
                    result += '?';
                    break;
                default:
                    result += jsonStr[pos];
                    break;
            }
        } else {
            result += jsonStr[pos];
        }
        pos++;
    }

    if (pos < jsonStr.length() && jsonStr[pos] == '"') {
        pos++;
    }
    return result;
}

double JsonParser::parseNumber() {
    size_t start = pos;
    size_t original_pos = pos; 

    if (pos < jsonStr.length() && (jsonStr[pos] == '-' || jsonStr[pos] == '+')) {
        pos++;
    }

    bool hasDigits = false;
    while (pos < jsonStr.length() && isdigit(jsonStr[pos])) {
        pos++;
        hasDigits = true;
    }

    if (!hasDigits) {
        pos = original_pos;
        return 0.0;
    }

    if (pos < jsonStr.length() && jsonStr[pos] == '.') {
        pos++;
        while (pos < jsonStr.length() && isdigit(jsonStr[pos])) {
            pos++;
        }
    }
    if (pos < jsonStr.length() && (jsonStr[pos] == 'e' || jsonStr[pos] == 'E')) {
        pos++;
        if (pos < jsonStr.length() && (jsonStr[pos] == '+' || jsonStr[pos] == '-')) {
            pos++;
        }
        while (pos < jsonStr.length() && isdigit(jsonStr[pos])) {
            pos++;
        }
    }
    size_t check_pos = pos;
    if (check_pos < jsonStr.length()) {
        char next_char = jsonStr[check_pos];
        if (isalpha(next_char) && next_char != 'e' && next_char != 'E') {
            pos = original_pos; 
            return 0.0; 
        }
    }

    string numStr = jsonStr.substr(start, pos - start);
    try {
        return stod(numStr);
    } catch (...) {
        pos = original_pos;
        return 0.0;
    }
}

bool JsonParser::parseBoolean() {
    if (pos + 4 <= jsonStr.length() && jsonStr.substr(pos, 4) == "true") {
        pos += 4;
        return true;
    } else if (pos + 5 <= jsonStr.length() && jsonStr.substr(pos, 5) == "false") {
        pos += 5;
        return false;
    }
    return false;
}

void JsonParser::parseNull() {
    if (pos + 4 <= jsonStr.length() && jsonStr.substr(pos, 4) == "null") {
        pos += 4;
    }
}

Vector<string> JsonParser::parsestringArray() {
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
        } else if (jsonStr[pos] == 'n') {
            parseNull();
            result.push_back("null");
        } else if ((isdigit(jsonStr[pos]) || jsonStr[pos] == '-' || jsonStr[pos] == '+')) {
            std::string numStr = getCurrentNumberString();
            if (isPotentialValidNumber(numStr)) {
                double num = parseNumber();
                result.push_back(to_string(num));
            } else {
                pos++;
            }
        } else if (jsonStr[pos] == 't' || jsonStr[pos] == 'f') {
            bool b = parseBoolean();
            result.push_back(b ? "true" : "false");
        } else if (jsonStr[pos] == '{') {
            size_t start = pos;
            int braceCount = 0;
            bool inString = false;

            do {
                if (jsonStr[pos] == '"' && (pos == 0 || jsonStr[pos-1] != '\\')) {
                    inString = !inString;
                }
                if (!inString) {
                    if (jsonStr[pos] == '{') braceCount++;
                    else if (jsonStr[pos] == '}') braceCount--;
                }
                pos++;
            } while (pos < jsonStr.length() && braceCount > 0);

            if (braceCount == 0) {
                string objStr = jsonStr.substr(start, pos - start);
                result.push_back(objStr);
            }
        } else if (jsonStr[pos] == '[') {
            size_t start = pos;
            int bracketCount = 0;
            bool inString = false;

            do {
                if (jsonStr[pos] == '"' && (pos == 0 || jsonStr[pos-1] != '\\')) {
                    inString = !inString;
                }
                if (!inString) {
                    if (jsonStr[pos] == '[') bracketCount++;
                    else if (jsonStr[pos] == ']') bracketCount--;
                }
                pos++;
            } while (pos < jsonStr.length() && bracketCount > 0);

            if (bracketCount == 0) {
                string arrStr = jsonStr.substr(start, pos - start);
                result.push_back(arrStr);
            }
        } else {
            pos++;
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


HashMap<string, string> JsonParser::parseObject() {
    pos = 0;
    skipWhitespace();
    return parseSingleObject();
}

Vector<HashMap<string, string>> JsonParser::parseArray(const string& json) {
    jsonStr = json;
    pos = 0;
    skipWhitespace();

    Vector<HashMap<string, string>> result;

    if (jsonStr[pos] != '[') return result;
    pos++;

    while (pos < jsonStr.length()) {
        skipWhitespace();
        if (jsonStr[pos] == ']') {
            pos++;
            break;
        }

        if (jsonStr[pos] == '{') {
            HashMap<string, string> obj = parseSingleObject();
            result.push_back(obj);
        } else if (jsonStr[pos] == 'n') {
            parseNull();
        } else {
            while (pos < jsonStr.length() && jsonStr[pos] != ',' && jsonStr[pos] != ']') {
                pos++;
            }
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

HashMap<string, string> JsonParser::parse(const string& json) {
    jsonStr = json;
    pos = 0;
    skipWhitespace();
    return parseSingleObject();
}

string JsonParser::extractJsonValue(const string& json) {
    jsonStr = json;
    pos = 0;
    skipWhitespace();

    if (pos >= jsonStr.length()) return "";

    if (jsonStr[pos] == '"') {
        return parsestring();
    } else if ((isdigit(jsonStr[pos]) || jsonStr[pos] == '-' || jsonStr[pos] == '+')) {
        std::string numStr = getCurrentNumberString();
        if (isPotentialValidNumber(numStr)) {
            double num = parseNumber();
            return to_string(num);
        } else {
            pos++;
            return jsonStr.substr(pos-1, 1);
        }
    } else if (jsonStr[pos] == 't' || jsonStr[pos] == 'f') {
        bool b = parseBoolean();
        return b ? "true" : "false";
    } else if (jsonStr[pos] == 'n') {
        parseNull();
        return "null";
    } else if (jsonStr[pos] == '{') {
        size_t start = pos;
        int braceCount = 0;
        bool inString = false;
        bool escaped = false;

        for (size_t i = pos; i < jsonStr.length(); i++) {
            char c = jsonStr[i];

            if (escaped) {
                escaped = false;
                continue;
            }

            if (c == '\\') {
                escaped = true;
                continue;
            }

            if (c == '"' && !escaped) {
                inString = !inString;
                continue;
            }

            if (!inString) {
                if (c == '{') braceCount++;
                else if (c == '}') braceCount--;
            }

            if (braceCount == 0) {
                pos = i + 1;
                return jsonStr.substr(start, pos - start);
            }
        }
    } else if (jsonStr[pos] == '[') {
        size_t start = pos;
        int bracketCount = 0;
        bool inString = false;
        bool escaped = false;

        for (size_t i = pos; i < jsonStr.length(); i++) {
            char c = jsonStr[i];

            if (escaped) {
                escaped = false;
                continue;
            }

            if (c == '\\') {
                escaped = true;
                continue;
            }

            if (c == '"' && !escaped) {
                inString = !inString;
                continue;
            }

            if (!inString) {
                if (c == '[') bracketCount++;
                else if (c == ']') bracketCount--;
            }

            if (bracketCount == 0) {
                pos = i + 1;
                return jsonStr.substr(start, pos - start);
            }
        }
    }

    return "";
}

Vector<string> JsonParser::parseStringArray(const string& json) {
    jsonStr = json;
    pos = 0;
    skipWhitespace();
    return parsestringArray();
}