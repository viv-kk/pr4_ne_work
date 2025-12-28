#include "db_client.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>
#include "JsonParser.h"
#include "network_protocol.h"
using std::cout;
using namespace std;

static string normalizeJson(const string& json);

CommandParser::ParsedCommand CommandParser::parse(const string& input) {
    ParsedCommand cmd;
    
    if (input.empty()) {
        return cmd;
    }
    
    size_t opEnd = input.find(' ');//операция
    if (opEnd == string::npos) {
        cmd.operation = input;
        transform(cmd.operation.begin(), cmd.operation.end(), cmd.operation.begin(), ::toupper);
        return cmd;
    }
    
    cmd.operation = input.substr(0, opEnd);
    transform(cmd.operation.begin(), cmd.operation.end(), cmd.operation.begin(), ::toupper);
    
    size_t collectionStart = opEnd + 1;//пробелы
    while (collectionStart < input.length() && isspace(input[collectionStart])) {
        collectionStart++;
    }

    size_t collectionEnd = collectionStart;
    while (collectionEnd < input.length() && !isspace(input[collectionEnd])) {
        collectionEnd++;
    }
    
    if (collectionEnd > collectionStart) {
        cmd.collection = input.substr(collectionStart, collectionEnd - collectionStart);
    } else {
        return cmd;
    }

    size_t dataStart = collectionEnd;
    while (dataStart < input.length() && isspace(input[dataStart])) {
        dataStart++;
    }
    
    if (dataStart < input.length()) {
        string data = input.substr(dataStart);
        JsonParser parser;
        
        if (!data.empty() && data[0] == '{') {
            try {
                HashMap<string, string> parsed = parser.parse(data);
                if (parsed.size() > 0 || data == "{}") {
                    if (cmd.operation == "INSERT") {
                        cmd.data = data;
                    } else if (cmd.operation == "FIND" || cmd.operation == "DELETE") {
                        cmd.query = data;
                    }
                    return cmd;
                }
            } catch (...) {
            }
        }
        if (cmd.operation == "INSERT") {
            cmd.data = data;
        } else if (cmd.operation == "FIND" || cmd.operation == "DELETE") {
            cmd.query = data;
        }
    } 
    
    return cmd;
}

DBClient::DBClient(const string& host, int port, const string& db)
    : host(host), port(port), currentDatabase(db), socketFd(-1) {
}

DBClient::~DBClient() {
    disconnect();
}

// В функции connectToServer в db_client.cpp
bool DBClient::connectToServer() {
    socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd < 0) {
        cerr << "[CLIENT] Socket creation failed" << endl;
        return false;
    }
    
    // Устанавливаем опции сокета ПЕРЕД подключением
    int flag = 1;
    if (setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) < 0) {
        cerr << "[CLIENT] Warning: Failed to set SO_REUSEADDR" << endl;
    }
    
    // Увеличиваем буферы отправки/приема
    int sendBufSize = 65536;
    if (setsockopt(socketFd, SOL_SOCKET, SO_SNDBUF, &sendBufSize, sizeof(sendBufSize)) < 0) {
        cerr << "[CLIENT] Warning: Failed to set send buffer size" << endl;
    }
    
    int recvBufSize = 65536;
    if (setsockopt(socketFd, SOL_SOCKET, SO_RCVBUF, &recvBufSize, sizeof(recvBufSize)) < 0) {
        cerr << "[CLIENT] Warning: Failed to set receive buffer size" << endl;
    }
    
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0) {
        cerr << "[CLIENT] Invalid address: " << host << endl;
        close(socketFd);
        socketFd = -1;
        return false;
    }
    
    // Таймаут на подключение
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    
    if (setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        cerr << "[CLIENT] Warning: Failed to set send timeout" << endl;
    }
    
    if (setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        cerr << "[CLIENT] Warning: Failed to set receive timeout" << endl;
    }
    
    cout << "[CLIENT] Connecting to " << host << ":" << port << "..." << endl;
    
    if (::connect(socketFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "[CLIENT] Connect failed, errno: " << errno << endl;
        close(socketFd);
        socketFd = -1;
        return false;
    }
    
    cout << "[CLIENT] Connected successfully" << endl;
    return true;
}

void DBClient::disconnect() {
    if (socketFd >= 0) {
        close(socketFd);
        socketFd = -1;
    }
}

bool DBClient::connect() {
    return connectToServer();
}

void DBClient::reconnectIfNeeded() {
    if (socketFd < 0) {
        connectToServer();
    }
}

// Добавьте эти функции в db_client.cpp

// Вспомогательная функция для проверки полного JSON
static bool isCompleteJson(const string& str) {
    if (str.empty() || str[0] != '{') return false;
    
    int braceCount = 0;
    bool inString = false;
    bool escaped = false;
    
    for (size_t i = 0; i < str.length(); i++) {
        char c = str[i];
        
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
        }
    }
    
    return braceCount == 0 && !inString;
}

// В функции readFullResponse()
static string readFullResponse(int socketFd) {
    string response;
    char chunk[8192]; // Увеличить размер буфера
    
    struct timeval tv;
    tv.tv_sec = 10; // Уменьшить таймаут с 30 до 10 секунд
    tv.tv_usec = 0;
    setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    
    // Добавить счетчик попыток
    int attempts = 0;
    while (attempts < 3) {
        int bytesRead = recv(socketFd, chunk, sizeof(chunk) - 1, 0);
        if (bytesRead > 0) {
            chunk[bytesRead] = '\0';
            response.append(chunk, bytesRead);
            
            if (isCompleteJson(response)) {
                cout << "[CLIENT] Complete JSON received (" << response.length() << " bytes)" << endl;
                break;
            }
        } else if (bytesRead == 0) {
            cout << "[CLIENT] Connection closed by server" << endl;
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                attempts++;
                if (!response.empty()) {
                    cout << "[CLIENT] Timeout with partial response, attempt " << attempts << endl;
                    if (attempts >= 3) {
                        break;
                    }
                }
            } else {
                cout << "[CLIENT] Recv error: " << strerror(errno) << endl;
                break;
            }
        }
    }
    
    return response;
}

// В функции sendRequest в db_client.cpp
// В db_client.cpp, в функции sendRequest
Response DBClient::sendRequest(const Request& req) {
    if (socketFd < 0) {
        Response resp;
        resp.status = "error";
        resp.message = "Not connected to server";
        return resp;
    }

    string jsonRequest = req.toJson();
    
    cout << "[CLIENT] Sending request of size: " << jsonRequest.length() << " bytes" << endl;
    
    // Отправляем все данные
    size_t totalSent = 0;
    size_t toSend = jsonRequest.length();
    const char* data = jsonRequest.c_str();
    
    while (totalSent < toSend) {
        int bytesSent = send(socketFd, data + totalSent, toSend - totalSent, 0);
        if (bytesSent < 0) {
            cout << "[CLIENT] Send failed, errno: " << errno << endl;
            disconnect();
            Response resp;
            resp.status = "error";
            resp.message = "Failed to send request to server";
            return resp;
        }
        totalSent += bytesSent;
    }
    cout << "[CLIENT] Total sent: " << totalSent << "/" << toSend << " bytes" << endl;
    
    // Используем новую функцию для чтения ответа
    string fullResponse = readFullResponse(socketFd);
    
    if (fullResponse.empty()) {
        disconnect();
        Response resp;
        resp.status = "error";
        resp.message = "No response from server";
        return resp;
    }
    
    cout << "[CLIENT] Response size: " << fullResponse.length() << " bytes" << endl;
    
    // Для отладки показываем начало и конец ответа
    if (fullResponse.length() > 200) {
        cout << "[CLIENT] Response start: " << fullResponse.substr(0, 100) << "..." << endl;
        cout << "[CLIENT] Response end: ..." << fullResponse.substr(fullResponse.length() - 100) << endl;
    } else {
        cout << "[CLIENT] Full response: " << fullResponse << endl;
    }
    
    try {
        Response resp = Response::fromJson(fullResponse);
        return resp;
    }
    catch (const exception& e) {
        cout << "[CLIENT] Parse error: " << e.what() << endl;
        Response resp;
        resp.status = "error";
        resp.message = string("Failed to parse server response: ") + e.what();
        return resp;
    }
}

Response DBClient::insert(const string& collection, const Vector<string>& documents) {
    Request req;
    req.database = currentDatabase;
    req.operation = "insert";
    req.collection = collection;
    
    JsonParser parser;
    for (size_t i = 0; i < documents.size(); i++) {
        string jsonStr = normalizeJson(documents[i]);
        
        try {
            HashMap<string, string> parsed = parser.parse(jsonStr);
            if (parsed.size() == 0 && jsonStr != "{}") {
            }
        } catch (const exception& e) {
            Response resp;
            resp.status = "error";
            resp.message = "Invalid JSON document: " + string(e.what());
            return resp;
        }
        
        req.data.push_back(jsonStr);
    }
    
    return sendRequest(req);
}

Response DBClient::find(const string& collection, const string& query) {    
    Request req;
    req.database = currentDatabase;
    req.operation = "find";
    req.collection = collection;
    
    JsonParser parser;
    string normalizedQuery = normalizeJson(query);
    
    if (!normalizedQuery.empty() && normalizedQuery[0] == '{') {
        try {
            HashMap<string, string> parsed = parser.parse(normalizedQuery);
            req.query = normalizedQuery;
        } catch (...) {
            req.query = query;
        }
    } else {
        req.query = query;
    }
    
    return sendRequest(req);
}

Response DBClient::remove(const string& collection, const string& query) {
    Request req;
    req.database = currentDatabase;
    req.operation = "delete";
    req.collection = collection;
    
    JsonParser parser;
    string normalizedQuery = normalizeJson(query);
    
    if (!normalizedQuery.empty() && normalizedQuery[0] == '{') {
        try {
            HashMap<string, string> parsed = parser.parse(normalizedQuery);
            req.query = normalizedQuery;
        } catch (...) {
            req.query = query;
        }
    } else {
        req.query = query;
    }
    
    return sendRequest(req);
}

void DBClient::interactiveMode() {
    cout << "NoSQL Database" << endl;
    cout << "Сервер: " << host << ":" << port << endl;
    cout << "База данных: " << currentDatabase << endl;
    cout << endl;
    cout << endl << "Доступные команды:" << endl;
    cout << "INSERT <collection> <json_data> - Вставка документа" << endl;
    cout << "FIND <collection> <query> - Найти документы" << endl;
    cout << "DELETE <collection> <query> - Удалить документ" << endl;
    cout << "HELP - Доступные команды" << endl;
    cout << "EXIT/QUIT - Выход" << endl;
    cout << endl;
    
    string line;
    while (true) {
        cout << currentDatabase << "> ";
        string input;
        while (true) {
            if (!getline(cin, line)) {
                cout << endl << "Выход" << endl;
                return;
            }
            if (line == "EXIT" || line == "QUIT") {
                cout << "Выход" << endl;
                return;
            }
            
            if (line == "HELP") {
                cout << endl << "Доступные команды:" << endl;
                cout << "INSERT <collection> <json_data> - Вставка документа" << endl;
                cout << "FIND <collection> <query> - Найти документы" << endl;
                cout << "DELETE <collection> <query> - Удалить документ" << endl;
                cout << "HELP - Доступные команды" << endl;
                cout << "EXIT or QUIT - Выход" << endl;
                cout << currentDatabase << "> ";
                continue;
            }
            
            input += line;
            int braceCount = 0;
            int bracketCount = 0;
            bool inString = false;
            char stringChar = 0;
            
            for (size_t i = 0; i < input.size(); ++i) {
                char c = input[i];
                
                if (c == '"' || c == '\'') {
                    if (i == 0 || input[i-1] != '\\') {
                        if (!inString) {
                            inString = true;
                            stringChar = c;
                        } else if (c == stringChar) {
                            inString = false;
                        }
                    }
                }
                
                if (!inString) {
                    if (c == '{') {
                        braceCount++;
                    }
                    else if (c == '}') {
                        braceCount--;
                    }
                    else if (c == '[') {
                        bracketCount++;
                    }
                    else if (c == ']') {
                        bracketCount--;
                    }
                }
            }            
            if (braceCount == 0 && bracketCount == 0 && !inString) {
                break; 
            }
            cout << "... ";
        }        
        if (input.empty()) {
            continue;
        }
        CommandParser::ParsedCommand cmd = CommandParser::parse(input);
        
        if (cmd.operation.empty()) {
            cout << "Error: Invalid command format" << endl;
            continue;
        }        
        Response resp;
        
        if (cmd.operation == "INSERT") {
            if (cmd.collection.empty() || cmd.data.empty()) {
                cout << "Error: INSERT requires collection and data" << endl;
                continue;
            }
            
            JsonParser parser;
            string normalizedData = normalizeJson(cmd.data);
            try {
                HashMap<string, string> parsed = parser.parse(normalizedData);
                if (parsed.size() == 0 && normalizedData != "{}") {
                    cout << "Warning: Data may not be valid JSON" << endl;
                }
            } catch (const exception& e) {
                cout << "Error: Invalid JSON data: " << e.what() << endl;
                continue;
            }
            
            Vector<string> documents;
            documents.push_back(normalizedData);
            resp = insert(cmd.collection, documents);
            
        } else if (cmd.operation == "FIND") {
            if (cmd.collection.empty() || cmd.query.empty()) {
                cout << "Error: FIND requires collection and query" << endl;
                continue;
            }
            JsonParser parser;
            string normalizedQuery = normalizeJson(cmd.query);
            if (!normalizedQuery.empty() && normalizedQuery[0] == '{') {
                try {
                    HashMap<string, string> parsed = parser.parse(normalizedQuery);
                } catch (const exception& e) {
                    cout << "Warning: Query may not be valid JSON: " << e.what() << endl;
                }
            }
            
            resp = find(cmd.collection, normalizedQuery);
            
        } else if (cmd.operation == "DELETE") {
            if (cmd.collection.empty() || cmd.query.empty()) {
                cout << "Error: DELETE requires collection and query" << endl;
                continue;
            }
            JsonParser parser;
            string normalizedQuery = normalizeJson(cmd.query);
            if (!normalizedQuery.empty() && normalizedQuery[0] == '{') {
                try {
                    HashMap<string, string> parsed = parser.parse(normalizedQuery);
                } catch (const exception& e) {
                    cout << "Warning: Query may not be valid JSON: " << e.what() << endl;
                }
            }
            
            resp = remove(cmd.collection, normalizedQuery);
            
        } else {
            cout << "Error: Unknown operation '" << cmd.operation << "'" << endl;
            continue;
        }
        cout << "Status: " << resp.status << endl;
        cout << "Message: " << resp.message << endl;
        if (resp.count > 0) {
            cout << "Count: " << resp.count << endl;
        }
        if (!resp.data.empty()) {
            cout << "Data:" << endl;
            for (size_t i = 0; i < resp.data.size(); ++i) {
                cout << "  " << resp.data[i] << endl;
            }
        }
        cout << endl;
    }
}

Response DBClient::executeSingleCommand(const string& host, int port, 
                                        const string& db, const string& command,
                                        const string& collection, const string& data) {
    DBClient client(host, port, db);
    if (!client.connect()) {
        Response resp;
        resp.status = "error";
        resp.message = "Failed to connect to server";
        return resp;
    }
    string op;
    string query = data;
    
    if (command == "insert") {
        op = "insert";
    } else if (command == "find") {
        op = "find";
        query = data;
    } else if (command == "delete") {
        op = "delete";
        query = data;
    } else {
        Response resp;
        resp.status = "error";
        resp.message = "Unknown command: " + command;
        return resp;
    }
    JsonParser parser;
    string normalizedData = normalizeJson(data);
    
    if (op == "insert") {
        try {
            HashMap<string, string> parsed = parser.parse(normalizedData);
            if (parsed.size() == 0 && normalizedData != "{}") {
            }
        } catch (const exception& e) {
            Response resp;
            resp.status = "error";
            resp.message = "Invalid JSON data: " + string(e.what());
            return resp;
        }
        
        Vector<string> documents;
        documents.push_back(normalizedData);
        return client.insert(collection, documents);
    } else if (op == "find") {
        if (!normalizedData.empty() && normalizedData[0] == '{') {
            try {
                HashMap<string, string> parsed = parser.parse(normalizedData);
            } catch (const exception& e) {
            }
        }
        return client.find(collection, normalizedData);
    } else {
        if (!normalizedData.empty() && normalizedData[0] == '{') {
            try {
                HashMap<string, string> parsed = parser.parse(normalizedData);
            } catch (const exception& e) {
            }
        }
        return client.remove(collection, normalizedData);
    }
}

static string normalizeJson(const string& json) {
    if (json.empty()) {
        return json;
    }
    
    string result;
    bool inString = false;
    bool escaped = false;
    
    for (size_t i = 0; i < json.length(); i++) {
        char c = json[i];
        
        if (escaped) {
            result += c;
            escaped = false;
            continue;
        }
        
        if (c == '\\') {
            escaped = true;
            result += c;
            continue;
        }
        
        if (c == '"' || c == '\'') {
            result += '"';
            inString = !inString;
        } else if (!inString && isspace(c)) {
            if (!result.empty() && !isspace(result.back())) {
                result += ' ';
            }
        } else {
            result += c;
        }
    }
    size_t pos2 = 0;
    while ((pos2 = result.find(" : ", pos2)) != string::npos) {
        result.replace(pos2, 3, ":");
        pos2 += 1;
    }
    
    pos2 = 0;
    while ((pos2 = result.find(": ", pos2)) != string::npos) {
        result.replace(pos2, 2, ":");
        pos2 += 1;
    }
    
    pos2 = 0;
    while ((pos2 = result.find(" :", pos2)) != string::npos) {
        result.replace(pos2, 2, ":");
        pos2 += 1;
    }
    pos2 = 0;
    while ((pos2 = result.find("  ", pos2)) != string::npos) {
        result.replace(pos2, 2, " ");
        pos2 += 1;
    }
    while (!result.empty() && isspace(result[0])) {
        result.erase(0, 1);
    }
    while (!result.empty() && isspace(result.back())) {
        result.pop_back();
    }
    
    return result;
}