#include "NosqlDbms.h"
#include <iostream>

int main(int argc, char* argv[]) {
    NoSQLDBMS dbms;
    string result = dbms.executeCommand(argc, argv);
    cout << result.c_str() << endl;
    return 0;
}
