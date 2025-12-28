#include "NosqlDbms.h"
#include "JsonParser.h"
#include <iostream>

NoSQLDBMS::NoSQLDBMS() : currentDb(nullptr) {}

NoSQLDBMS::~NoSQLDBMS() {
    delete currentDb;
}

string NoSQLDBMS::executeCommand(int argc, char* argv[]) {
    if (argc < 3) {
        return "Error: Invalid number of arguments. Usage: ./no_sqL_dbms <database> <command> <data>";
    }
    
    string databaseName = argv[1];
    string command = argv[2];
    
    if (currentDb == nullptr || currentDb->getName() != databaseName) {//переключение бд
        delete currentDb;
        currentDb = new Database(databaseName);
    }
    
    if (command == "insert") {
        if (argc < 4) return "Error: Missing JSON data for insert";
        Collection& coll = currentDb->getCollection("default");
        return coll.insert(argv[3]);
    }
    else if (command == "find") {
        if (argc < 4) return "Error: Missing condition for find";
        Collection& coll = currentDb->getCollection("default");
        ConditionParser parser;
        QueryCondition condition = parser.parse(argv[3]);
        Vector<Document> results = coll.find(condition);
        
        string response = "Found " + to_string(results.size()) + " document(s):\n";
        for (size_t i = 0; i < results.size(); i++) {
            response += results[i].to_json() + "\n";
        }
        return response;
    }
    else if (command == "delete") {
        if (argc < 4) return "Error: Missing condition for delete";
        Collection& coll = currentDb->getCollection("default");
        ConditionParser parser;
        QueryCondition condition = parser.parse(argv[3]);
        return coll.remove(condition);
    }
    else {
        return "Error: Unknown command '" + command + "'";
    }
}
