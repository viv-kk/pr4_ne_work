#ifndef NOSQLDBMS_H
#define NOSQLDBMS_H

#include "database.h"
#include "QueryCondition.h"
using namespace std;

class NoSQLDBMS {
private:
    Database* currentDb;

public:
    NoSQLDBMS();
    ~NoSQLDBMS();
    string executeCommand(int argc, char* argv[]);
};

#endif
