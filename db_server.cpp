#include "db_server.h"
#include "QueryCondition.h"
#include <sys/socket.h>
#include "HashMap.h"
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>
#include <arpa/inet.h>
#include "JsonParser.h"
#include "vector.h"

using namespace std;

ConnectionManager::ConnectionManager() : running(false), serverSocket(-1) {
}

ConnectionManager::~ConnectionManager() {
    stop();
    auto dbItems = databases.items();
    for (size_t i = 0; i < dbItems.size(); i++) {
        delete dbItems[i].second;
    }
    
    auto mutexItems = dbMutexes.items();
    for (size_t i = 0; i < mutexItems.size(); i++) {
        delete mutexItems[i].second;//очистка мьютексов
    }
}

bool ConnectionManager::start(int port, int numWorkers) {
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        cerr << "[SERVER][ERROR] Failed to create socket, errno: " << errno << endl;
        return false;
    }
    
    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        cerr << "[SERVER][ERROR] Failed to set socket options, errno: " << errno << endl;
        close(serverSocket);
        return false;
    }
    
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);
    
    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {//сокет к адресу и порту
        cerr << "[SERVER][ERROR] Failed to bind socket to port " << port 
             << ", errno: " << errno << endl;
        close(serverSocket);
        return false;
    }
    
    if (listen(serverSocket, 10) < 0) {
        cerr << "[SERVER][ERROR] Failed to listen on socket, errno: " << errno << endl;
        close(serverSocket);
        return false;
    }

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        cerr << "[SERVER][WARN] Failed to set accept timeout, errno: " << errno << endl;
    } 
    
    running = true;
    for (int i = 0; i < numWorkers; ++i) {//запуск создение потоков
        workerThreads.push_back(thread(&ConnectionManager::workerThread, this));
    }
    
    cout << "[SERVER][SUCCESS] Started on port " << port 
         << " with " << numWorkers << " worker threads" << endl;

    thread([this, port]() {//прием покдключений
        struct timeval local_timeout;
        local_timeout.tv_sec = 1;
        local_timeout.tv_usec = 0;
        
        while (running) {
            sockaddr_in clientAddr;
            socklen_t clientLen = sizeof(clientAddr);

            int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientLen);
            
            if (clientSocket < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    if (running) {//таймаут на асепт
                        continue;
                    } else {
                        break;
                    }
                } else if (errno == EINTR) {
                    continue;//пррвано сигналом таймаут
                } else {
                    cerr << "[SERVER][ERROR] Accept failed, errno: " << errno << endl;
                    if (!running) break;
                    continue;
                }
            }
            
            char clientIP[INET_ADDRSTRLEN];//апйи в строку
            inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
            cout << "[SERVER] New client connected: socket=" << clientSocket 
                 << ", IP=" << clientIP << ":" << ntohs(clientAddr.sin_port) << endl;
            
            local_timeout.tv_sec = 30;//таймат чтения
            local_timeout.tv_usec = 0;
            if (setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &local_timeout, sizeof(local_timeout)) < 0) {
                cerr << "[SERVER][WARN] Failed to set receive timeout for client " << clientSocket 
                     << ", errno: " << errno << endl;
            }
            
            // Also set send timeout to ensure responses are sent properly
            if (setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, &local_timeout, sizeof(local_timeout)) < 0) {
                cerr << "[SERVER][WARN] Failed to set send timeout for client " << clientSocket 
                     << ", errno: " << errno << endl;
            }
            
            thread([this, clientSocket, clientIP]() {
                vector<char> buffer(65536);
                string requestStr;
                
                // Set socket to blocking mode for this client
                int flags = fcntl(clientSocket, F_GETFL, 0);
                fcntl(clientSocket, F_SETFL, flags & ~O_NONBLOCK);
                
                while (running) {
                    // Не очищаем буфер - recv перезапишет
                    int bytesRead = recv(clientSocket, buffer.data(), buffer.size() - 1, 0);
                    
                    if (bytesRead > 0) {
                        buffer[bytesRead] = '\0';
                        cout << "[SERVER] Received " << bytesRead << " bytes from client " << clientSocket << endl;
                        
                        // Собираем полный запрос
                        requestStr.append(buffer.data(), bytesRead);
                        
                        // Process all complete JSON requests in the buffer
                        size_t pos = 0;
                        while (pos < requestStr.length()) {
                            // Find the first '{' from position pos
                            size_t start = requestStr.find('{', pos);
                            if (start == string::npos) {
                                // No more JSON objects, keep remaining data
                                requestStr = requestStr.substr(pos);
                                break;
                            }
                            
                            // Try to extract a complete JSON starting from 'start'
                            string potentialJson = requestStr.substr(start);
                            
                            if (isValidJsonRequest(potentialJson)) {
                                // We found a complete JSON request
                                // Process it in the worker thread
                                {
                                    lock_guard<mutex> lock(queueMutex);
                                    requestQueue.push({clientSocket, potentialJson});
                                }
                                queueCV.notify_one();
                                
                                // Move position past this JSON
                                pos = start + potentialJson.length();
                                
                                // Check if there's more data after this JSON
                                if (pos < requestStr.length()) {
                                    // Keep remaining data for next iteration
                                    requestStr = requestStr.substr(pos);
                                    pos = 0; // Reset pos to process remaining data in this iteration
                                } else {
                                    // No more data, clear the string
                                    requestStr.clear();
                                    break;
                                }
                            } else {
                                // This is not a complete JSON, keep the rest and wait for more data
                                requestStr = requestStr.substr(start);
                                break;
                            }
                        }
                        
                    } else if (bytesRead == 0) {
                        cout << "[SERVER] Client " << clientSocket << " disconnected" << endl;
                        break;
                    } else {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            continue; // таймаут чтения
                        } else {
                            cerr << "[SERVER][ERROR] Failed to receive data from client " << clientSocket 
                                << ", errno: " << errno << endl;
                            break;
                        }
                    }
                }
                close(clientSocket);
            }).detach();
        }
    }).detach();
    
    return true;
}

bool ConnectionManager::isValidJsonRequest(const string& jsonStr) {
    if (jsonStr.empty()) {
        return false;
    }
    
    // Должен начинаться с '{'
    if (jsonStr[0] != '{') {
        cerr << "[SERVER][ERROR] JSON doesn't start with '{'" << endl;
        cerr << "[SERVER][ERROR] First 100 chars: " << jsonStr.substr(0, min(jsonStr.length(), 100ul)) << endl;
        return false;
    }
    
    int braceCount = 0;
    int bracketCount = 0;
    bool inString = false;
    bool escaped = false;
    
    for (size_t i = 0; i < jsonStr.length(); i++) {
        char c = jsonStr[i];
        
        if (escaped) {
            escaped = false;
            continue;
        }
        
        if (c == '\\') {
            escaped = true;
            continue;
        }
        
        if (c == '"') {
            inString = !inString;
            continue;
        }
        
        if (!inString) {
            if (c == '{') braceCount++;
            else if (c == '}') braceCount--;
            else if (c == '[') bracketCount++;
            else if (c == ']') bracketCount--;
        }
        
        if (braceCount < 0 || bracketCount < 0) {
            cerr << "[SERVER][ERROR] Unbalanced braces/brackets at position " << i << endl;
            cerr << "[SERVER][ERROR] Context: ..." 
                 << jsonStr.substr(max(0, (int)i-50), min(100ul, jsonStr.length()-max(0, (int)i-50))) 
                 << "..." << endl;
            return false;
        }
    }
    
    bool valid = (braceCount == 0 && bracketCount == 0 && !inString);
    if (!valid) {
        cerr << "[SERVER][ERROR] JSON validation failed: braces=" << braceCount 
             << ", brackets=" << bracketCount << ", inString=" << inString << endl;
        cerr << "[SERVER][ERROR] JSON length: " << jsonStr.length() << " bytes" << endl;
        
        // Выводим начало и конец для отладки
        if (jsonStr.length() > 200) {
            cerr << "[SERVER][ERROR] First 100 chars: " << jsonStr.substr(0, 100) << endl;
            cerr << "[SERVER][ERROR] Last 100 chars: " << jsonStr.substr(jsonStr.length() - 100) << endl;
        } else {
            cerr << "[SERVER][ERROR] Full JSON: " << jsonStr << endl;
        }
    }
    
    return valid;
}

void ConnectionManager::stop() {    
    if (!running) return;
    running = false;
    queueCV.notify_all();

    for (size_t i = 0; i < workerThreads.size(); ++i) {//завершение рабочих потоков
        if (workerThreads[i].joinable()) {
            workerThreads[i].join();
        }
    }
    
    if (serverSocket >= 0) {
        close(serverSocket);
        serverSocket = -1;
    }
    
    cout << "[SERVER] Stopped" << endl;
}

void ConnectionManager::workerThread() {    
    while (running) {
        pair<int, string> request;       
        {
            unique_lock<mutex> lock(queueMutex);
            queueCV.wait(lock, [this]() { //пока в очереди появ запрос или остановка сервера
                return !requestQueue.empty() || !running; 
            });
            
            if (!running && requestQueue.empty()) {
                break;
            }
            
            if (!requestQueue.empty()) {
                request = requestQueue.front();
                requestQueue.pop();
            } else {
                continue;
            }
        }
        processRequest(request.first, request.second);
    }
}

void ConnectionManager::processRequest(int clientSocket, const string& requestData) {
    try {
        Request req = Request::fromJson(requestData);
        Response resp;
        
        if (req.operation == "insert") {
            resp = insertDocument(req);
        } else if (req.operation == "find") {
            resp = findDocuments(req);
        } else if (req.operation == "delete") {
            resp = deleteDocuments(req);
        } else {
            cerr << "[SERVER][ERROR] Unknown operation: " << req.operation << endl;
            resp.status = "error";
            resp.message = "Unknown operation: " + req.operation;
        }
        
        string responseJson = resp.toJson();
        
        // Ensure the response is sent completely
        const char* responseData = responseJson.c_str();
        size_t totalLen = responseJson.length();
        size_t sentLen = 0;
        
        while (sentLen < totalLen) {
            int bytesSent = send(clientSocket, responseData + sentLen, totalLen - sentLen, 0);
            if (bytesSent < 0) {
                cerr << "[SERVER][ERROR] Failed to send response to client " << clientSocket 
                     << ", errno: " << errno << endl;
                break;
            }
            sentLen += bytesSent;
        }
        
        if (sentLen == totalLen) {
            cout << "[SERVER] Sent " << sentLen << " bytes response to client " << clientSocket << endl;
        } else {
            cerr << "[SERVER][ERROR] Partial response sent to client " << clientSocket 
                 << ", sent: " << sentLen << ", expected: " << totalLen << endl;
        }
        
    } catch (const exception& e) {
        cerr << "[SERVER][ERROR] Exception processing request from client " << clientSocket 
             << ": " << e.what() << endl;
        
        Response errorResp;
        errorResp.status = "error";
        errorResp.message = "Internal server error: " + string(e.what());
        
        string errorJson = errorResp.toJson();
        // Ensure the error response is sent completely
        const char* errorData = errorJson.c_str();
        size_t totalLen = errorJson.length();
        size_t sentLen = 0;
        
        while (sentLen < totalLen) {
            int bytesSent = send(clientSocket, errorData + sentLen, totalLen - sentLen, 0);
            if (bytesSent < 0) {
                cerr << "[SERVER][ERROR] Failed to send error response, errno: " << errno << endl;
                break;
            }
            sentLen += bytesSent;
        }
        
        if (sentLen != totalLen) {
            cerr << "[SERVER][ERROR] Partial error response sent, sent: " << sentLen 
                 << ", expected: " << totalLen << endl;
        }

    }
}
Response ConnectionManager::insertDocument(const Request& req) {    
    Response resp;
    Database* dbValue = nullptr;
    bool found = databases.get(req.database, dbValue);
    mutex* mutexPtr = nullptr;
    bool mutexFound = dbMutexes.get(req.database, mutexPtr);
    
    if (!mutexFound) {
        lock_guard<mutex> lock(mapMutex);
        mutex* newMutex = new mutex();
        dbMutexes.put(req.database, newMutex);
        mutexFound = dbMutexes.get(req.database, mutexPtr);
    }
    
    if (!mutexPtr) {
        resp.status = "error";
        resp.message = "Failed to get database mutex";
        resp.count = 0;
        return resp;
    }
    
    bool lockAcquired = false;//захват мютекса с таймаутом
    auto startTime = chrono::steady_clock::now();
    
    while (chrono::steady_clock::now() - startTime < chrono::seconds(3)) {
        if (mutexPtr->try_lock()) {
            lockAcquired = true;
            break;
        }
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    
    if (lockAcquired) {
        Database* db = nullptr;
        if (!found) {
            db = new Database(req.database);
            databases.put(req.database, db);
        } else {
            db = dbValue;
        }
        Collection& coll = db->getCollection(req.collection);

        int insertedCount = 0;
        Vector<string> insertedIds;
        
        for (size_t i = 0; i < req.data.size(); i++) {
            JsonParser parser;
            try {
                HashMap<string, string> docData = parser.parse(req.data[i]);
                if (docData.size() == 0 && req.data[i] != "{}") {
                    cerr << "[SERVER][WARN] Invalid JSON document: " << req.data[i] << endl;
                    continue;
                }
                
                string result = coll.insert(req.data[i]);
                
                if (result.find("successfully") != string::npos) {
                    insertedCount++;
                    size_t pos = result.find("doc_");
                    if (pos != string::npos) {
                        size_t end = result.find(" ", pos);
                        if (end != string::npos) {
                            string id = result.substr(pos, end - pos);
                            insertedIds.push_back(id);
                        }
                    }
                }
            } catch (const exception& e) {
                cerr << "[SERVER][ERROR] Failed to parse document: " << e.what() << endl;
                continue;
            }
        }
        
        resp.status = "success";
        resp.message = "Inserted " + to_string(insertedCount) + " document(s)";
        resp.count = insertedCount;
        for (size_t i = 0; i < insertedIds.size(); i++) {
            resp.data.push_back("{\"id\":\"" + insertedIds[i] + "\"}");
        }
        mutexPtr->unlock();
        
    } else {
        cerr << "[SERVER][ERROR] Database lock timeout for: " << req.database << endl;
        resp.status = "error";
        resp.message = "Database lock timeout for: " + req.database;
        resp.count = 0;
    }
    return resp;
}

Response ConnectionManager::findDocuments(const Request& req) {    
    Response resp;
    Database* dbValue = nullptr;
    bool dbFound = databases.get(req.database, dbValue);
    
    if (!dbFound) {
        cerr << "[SERVER][ERROR] Database not found: " << req.database << endl;
        resp.status = "error";
        resp.message = "Database not found: " + req.database;
        resp.count = 0;
        return resp;
    }
    mutex* mutexPtr = nullptr;
    bool mutexFound = dbMutexes.get(req.database, mutexPtr);
    
    if (!mutexFound || !mutexPtr) {
        cerr << "[SERVER][ERROR] Database mutex not found: " << req.database << endl;
        resp.status = "error";
        resp.message = "Database not initialized: " + req.database;
        resp.count = 0;
        return resp;
    }
    lock_guard<mutex> lock(*mutexPtr);//ссфлка мьютекс для чтения
    
    Database* db = dbValue;
    Collection& coll = db->getCollection(req.collection);

    ConditionParser parser;
    QueryCondition condition = parser.parse(req.query);

    // Получаем общее количество документов, соответствующих условию
    size_t total_count = coll.count(condition);
    
    // Получаем данные с пагинацией
    Vector<Document> results = coll.find(condition, req.page, req.limit);
    
    resp.status = "success";
    resp.message = "Found " + to_string(results.size()) + " document(s)";
    resp.count = results.size();
    resp.total_count = total_count;
    resp.current_page = req.page;
    resp.per_page = req.limit;
    
    // Вычисляем общее количество страниц
    if (req.limit > 0) {
        resp.total_pages = (total_count + req.limit - 1) / req.limit;
    } else {
        resp.total_pages = 1;
    }
    
    for (size_t i = 0; i < results.size(); i++) {
        resp.data.push_back(results[i].to_json());
    }
    
    return resp;
}

Response ConnectionManager::deleteDocuments(const Request& req) {    
    Response resp;
    mutex* mutexPtr = nullptr;
    bool mutexFound = dbMutexes.get(req.database, mutexPtr);
    
    if (!mutexFound || !mutexPtr) {
        cerr << "[SERVER][ERROR] Database not found: " << req.database << endl;
        resp.status = "error";
        resp.message = "Database not found: " + req.database;
        resp.count = 0;
        return resp;
    }
    
    bool lockAcquired = false;
    auto startTime = chrono::steady_clock::now();
    
    while (chrono::steady_clock::now() - startTime < chrono::seconds(3)) {
        if (mutexPtr->try_lock()) {
            lockAcquired = true;
            break;
        }
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    
    if (lockAcquired) {
        Database* dbValue = nullptr;
        bool dbFound = databases.get(req.database, dbValue);
        
        if (!dbFound) {
            resp.status = "error";
            resp.message = "Database not found: " + req.database;
            resp.count = 0;
            mutexPtr->unlock();
            return resp;
        }
        
        Database* db = dbValue;
        Collection& coll = db->getCollection(req.collection);

        ConditionParser parser;
        QueryCondition condition = parser.parse(req.query);

        string result = coll.remove(condition);

        if (result.find("successfully") != string::npos) {
            resp.status = "success";
            resp.message = result;
            size_t spacePos = result.find(' ');
            if (spacePos != string::npos) {
                string countStr = result.substr(0, spacePos);
                try {
                    resp.count = stoi(countStr);
                } catch (const exception& e) {
                    cerr << "[SERVER][ERROR] Failed to parse delete count: " << e.what() 
                         << ", countStr: '" << countStr << "'" << endl;
                    resp.count = 0;
                    for (char c : result) {
                        if (isdigit(c)) {
                            resp.count = resp.count * 10 + (c - '0');
                        }
                    }
                }
            } else {
                resp.count = 1;
            }
        } else if (result.find("No documents found") != string::npos) {
            resp.status = "success";
            resp.message = result;
            resp.count = 0;
        } else {
            resp.status = "error";
            resp.message = result;
            resp.count = 0;
        }

        mutexPtr->unlock();
        
    } else {
        cerr << "[SERVER][ERROR] Database lock timeout for delete: " << req.database << endl;
        resp.status = "error";
        resp.message = "Database lock timeout for: " + req.database;
        resp.count = 0;
    }
    return resp;
}

