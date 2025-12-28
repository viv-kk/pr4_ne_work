#ifndef DB_CLIENT_H
#define DB_CLIENT_H

#include "network_protocol.h"
#include "vector.h"
#include <string>

using namespace std;

static string readFullResponse(int socketFd);
static bool isCompleteJson(const string& str);

class CommandParser {
public:
    struct ParsedCommand {
        string operation;
        string collection;
        string data;
        string query;
    };
    
    static ParsedCommand parse(const string& input);
};

class DBClient {
private:
    string host;
    int port;
    string currentDatabase;
    int socketFd;
    
    
public:
    DBClient(const string& host, int port, const string& db);
    ~DBClient();
    
    bool connect();
    void disconnect();
    bool connectToServer();
    bool isConnected() const { return socketFd >= 0; }
    
    void reconnectIfNeeded();
    
    Response insert(const string& collection, const Vector<string>& documents);
    Response find(const string& collection, const string& query);
    Response remove(const string& collection, const string& query);
    Response sendRequest(const Request& req);
    void interactiveMode();
    static Response executeSingleCommand(const string& host, int port, 
                                        const string& db, const string& command,
                                        const string& collection, const string& data);
};

#endif