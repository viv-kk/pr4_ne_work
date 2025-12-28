#include "siem_agent.h"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <fstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace std;

SIEMAgent* agent = nullptr;

void daemonize() {
    pid_t pid = fork();
    
    if (pid < 0) {
        cerr << "ERROR: Fork failed" << endl;
        exit(1);
    }
    
    if (pid > 0) {
        exit(0); 
    }
    
    if (setsid() < 0) {//новая сессия
        cerr << "ERROR: Failed to create new session" << endl;
        exit(1);
    }
    
    pid = fork();
    if (pid < 0) {
        exit(1);
    }
    
    if (pid > 0) {
        exit(0);
    }
    
    chdir("/");
    
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    int fd = open("/var/log/siem_agent_daemon.log", O_WRONLY | O_CREAT | O_APPEND, 0644);//перенаправляем в файл
    if (fd >= 0) {
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
    
    fd = open("/dev/null", O_RDONLY);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        close(fd);
    }
    
    umask(0);
}

void signalHandler(int signum) {
    cout << "\nSignal " << signum << " received, shutting down gracefully..." << endl;
    
    if (agent) {
        agent->stop();
        
        // Просто ждем 3 секунды на завершение операций
        this_thread::sleep_for(chrono::seconds(3));
        
        delete agent;
        agent = nullptr;
    }
    
    exit(0);
}

int main(int argc, char* argv[]) {
    cout << "=== SIEM Agent Starting ===" << endl;
    
    string config_path = "/etc/siem_agent/config.json";
    bool daemon_mode = false;
    string log_level = "INFO";
    
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--daemon") {
            daemon_mode = true;
        } else if (arg == "--loglevel" && i + 1 < argc) {
            log_level = argv[++i];
        } else if (arg == "--help") {
            cout << "SIEM Agent - Security Information and Event Management Agent" << endl;
            cout << "Usage: " << argv[0] << " [options]" << endl;
            cout << "Options:" << endl;
            cout << "  --config <path>    Configuration file path" << endl;
            cout << "  --daemon           Run as daemon" << endl;
            cout << "  --loglevel <level> Log level (INFO, WARN, ERROR)" << endl;
            cout << "  --help             Show this help message" << endl;
            return 0;
        }
    }
    
    cout << "Configuration: " << config_path << endl;
    cout << "Daemon mode: " << (daemon_mode ? "yes" : "no") << endl;
    
    ifstream config_check(config_path);
    if (!config_check.is_open()) {
        cerr << "ERROR: Cannot open config file: " << config_path << endl;
        
        if (config_path != "./siem_config.json") {
            config_path = "./siem_config.json";
            config_check.open(config_path);
            if (config_check.is_open()) {
                cout << "Using local config file: " << config_path << endl;
                config_check.close();
            } else {
                cerr << "ERROR: No configuration file found" << endl;
                return 1;
            }
        } else {
            return 1;
        }
    } else {
        config_check.close();
        cout << "Config file exists: YES" << endl;
    }
    
    if (daemon_mode) {
        cout << "Running as daemon..." << endl;
        daemonize();
    }
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGHUP, signalHandler); 
    
    try {
        cout << "Creating SIEMAgent instance..." << endl;
        agent = new SIEMAgent(config_path);
        
        cout << "Starting agent..." << endl;
        if (!agent->start()) {
            cerr << "ERROR: Failed to start agent" << endl;
            delete agent;
            return 1;
        }
        
        cout << "Running agent main loop..." << endl;
        agent->run();
        
    } catch (const exception& e) {
        cerr << "Exception: " << e.what() << endl;
        if (agent) {
            delete agent;
        }
        return 1;
    }
    
    return 0;
}