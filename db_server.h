#ifndef DB_SERVER_H
#define DB_SERVER_H

#include "database.h"
#include "network_protocol.h"
#include "HashMap.h"
#include "vector.h"
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <memory>

class ConnectionManager {
private:
    bool running;
    int serverSocket;
    
    HashMap<string, Database*> databases;
    HashMap<string, mutex*> dbMutexes; 
    mutex mapMutex;
    
    queue<pair<int, string>> requestQueue;
    mutex queueMutex;
    condition_variable queueCV;
    
    Vector<thread> workerThreads; 
    
    bool isValidJsonRequest(const string& jsonStr);
    
    void workerThread();
    void processRequest(int clientSocket, const string& requestData);
    
    Response insertDocument(const Request& req);
    Response findDocuments(const Request& req);
    Response deleteDocuments(const Request& req);
    
public:
    ConnectionManager();
    ~ConnectionManager();
    
    bool start(int port, int numWorkers = 4);
    void stop();
};

#endif // DB_SERVER_H