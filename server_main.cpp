#include "db_server.h"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <memory>

using namespace std;

shared_ptr<ConnectionManager> server;

void signalHandler(int signum) {
    cout << endl << "[MAIN] Interrupt signal received. Shutting down server..." << endl;
    if (server) {
        server->stop();
    }
    exit(signum);
}

void printHelp() {
    cout << "=== NoSQL Database Server ===" << endl;
    cout << "./db_server [port] [workers]" << endl;
    cout << endl;
    cout << "Запуск сервера:" << endl;
    cout << "./db_server" << endl;
    cout << "./db_server 9000" << endl;
    cout << "./db_server 9000 10" << endl;
    cout << endl;
    cout << "Доступные команды:" << endl;
    cout << "status - Статус сервера" << endl;
    cout << "stop - Остановка сервера" << endl;
    cout << "help - Доступные команды" << endl;
}

int main(int argc, char* argv[]) {
    int port = 8080;
    int workers = 5;
    
    if (argc > 1) {
        if (string(argv[1]) == "--help" || string(argv[1]) == "-h") {
            printHelp();
            return 0;
        }
        port = atoi(argv[1]);
    }
    
    if (argc > 2) {
        workers = atoi(argv[2]);
    }
    
    if (port < 1 || port > 65535) {
        cerr << "Error: Invalid port number. Must be between 1 and 65535" << endl;
        return 1;
    }
    
    if (workers < 1 || workers > 50) {
        cerr << "Error: Invalid worker count. Must be between 1 and 50" << endl;
        return 1;
    }
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    cout << "NoSQL Database Server" << endl;
    cout << "Порт: " << port << endl;
    cout << "Рабочие потоки: " << workers << endl;
    cout << endl;
    cout << "'help' - доступные команды, Ctrl+C - остановить сервер" << endl;
    cout << endl;

    server = make_shared<ConnectionManager>();//запуск сервера
    
    if (!server->start(port, workers)) {
        cerr << "Failed to start server on port " << port << endl;
        return 1;
    }

    string command;
    while (true) {
        cout << "server> ";
        
        if (!getline(cin, command)) {
            break;
        }
        
        if (command == "stop" || command == "exit") {
            cout << "Остановка сервера" << endl;
            break;
        } else if (command == "status") {
            cout << "Сервер запущен на порту " << port << endl;
            cout << "Рабочих потоков: " << workers << endl;
        } else if (command == "help") {
            printHelp();
        } else if (!command.empty()) {
            cout << "Unknown command. Type 'help' for available commands." << endl;
        }
    }
    server->stop();
    
    cout << "Server stopped." << endl;
    return 0;
}