#include "db_client.h"
#include <iostream>
#include <cstring>
#include <cstdlib>

using namespace std;

void printHelp() {
    cout << "=== NoSQL Database Client ===" << endl;
    cout << endl;
    cout << "Usage:" << endl;
    cout << "  Interactive mode: ./db_client --host <host> --port <port> --database <db>" << endl;
    cout << "  Single command:   ./db_client --host <host> --port <port> --database <db> \\" << endl;
    cout << "                     --command <cmd> --collection <coll> --data <json>" << endl;
    cout << endl;
    cout << "Arguments:" << endl;
    cout << "  --host <host>       Server hostname or IP (default: localhost)" << endl;
    cout << "  --port <port>       Server port (default: 8080)" << endl;
    cout << "  --database <db>     Database name (required)" << endl;
    cout << "  --command <cmd>     Command to execute (insert|find|delete)" << endl;
    cout << "  --collection <coll> Collection name" << endl;
    cout << "  --data <json>       JSON data for insert or query for find/delete" << endl;
    cout << "  --help              Show this help message" << endl;
    cout << endl;
    cout << "Examples:" << endl;
    cout << "  Interactive mode:" << endl;
    cout << "    ./db_client --host localhost --port 8080 --database mydb" << endl;
    cout << endl;
    cout << "  Single commands:" << endl;
    cout << "    ./db_client --host localhost --port 8080 --database mydb \\" << endl;
    cout << "      --command insert --collection users --data '{\"name\":\"John\",\"age\":30}'" << endl;
    cout << "    ./db_client --host localhost --port 8080 --database mydb \\" << endl;
    cout << "      --command find --collection users --data '{\"age\":{\"$gt\":25}}'" << endl;
    cout << "    ./db_client --host localhost --port 8080 --database mydb \\" << endl;
    cout << "      --command delete --collection users --data '{\"name\":\"John\"}'" << endl;
}

int main(int argc, char* argv[]) {
    string host = "localhost";
    int port = 8080;
    string database;
    string command;
    string collection;
    string data;
    
    bool interactive = true;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--database") == 0 && i + 1 < argc) {
            database = argv[++i];
        } else if (strcmp(argv[i], "--command") == 0 && i + 1 < argc) {
            command = argv[++i];
            interactive = false;
        } else if (strcmp(argv[i], "--collection") == 0 && i + 1 < argc) {
            collection = argv[++i];
        } else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            data = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printHelp();
            return 0;
        } else {
            cerr << "Error: Unknown argument '" << argv[i] << "'" << endl;
            printHelp();
            return 1;
        }
    }
    
    if (database.empty()) {
        cerr << "Error: Database name is required. Use --database <name>" << endl;
        printHelp();
        return 1;
    }
    
    if (interactive) {
        DBClient client(host, port, database);
        
        if (!client.connect()) {
            cerr << "Error: Failed to connect to server at " << host << ":" << port << endl;
            return 1;
        }
        
        client.interactiveMode();
    } else {
        if (command.empty() || collection.empty()) {
            cerr << "Error: Command and collection are required for single command mode" << endl;
            printHelp();
            return 1;
        }
        
        Response resp = DBClient::executeSingleCommand(host, port, database, command, collection, data);
        
        cout << "Результат" << endl;
        cout << "Status: " << resp.status << endl;
        cout << "Message: " << resp.message << endl;
        
        if (resp.count > 0) {
            cout << "Count: " << resp.count << endl;
        }
        
        if (!resp.data.empty()) {
            cout << endl << "Data:" << endl;
            for (size_t i = 0; i < resp.data.size(); i++) {
                cout << resp.data[i] << endl;
            }
        }
        if (resp.status != "success") {
            return 1;
        }
    }
    
    return 0;
}