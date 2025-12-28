#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include "HashMap.h"
#include "vector.h"
#include <string>
using namespace std;

class JsonParser {
private:
    string jsonStr;
    size_t pos;
    
    void skipWhitespace();
    string parsestring();
    double parseNumber();
    bool parseBoolean();
    void parseNull();
    Vector<string> parsestringArray(); 
    HashMap<string, string> parseSingleObject();
    bool isPotentialValidNumber(const std::string& str);
    std::string getCurrentNumberString();
    
public:
    JsonParser() : pos(0) {}
    
    HashMap<string, string> parseObject();
    Vector<HashMap<string, string>> parseArray(const string& json);
    HashMap<string, string> parse(const string& json);
    string extractJsonValue(const string& json); 
    
    Vector<string> parseStringArray(const string& json);
};

#endif