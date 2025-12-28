#ifndef SIEM_AGENT_H
#define SIEM_AGENT_H

#include "HashMap.h"
#include "vector.h"
#include <string>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <cstdint>
#include "event_processor.h"
#include "persistent_buffer.h"
#include "inotify_wrapper.h"

class DBClient;

struct AgentConfig {
    std::string server_host;
    int server_port;
    std::string database;
    std::string collection;
    std::string agent_id;
    std::string log_file;
    int send_interval;
    int batch_size;
    int max_buffer_size;
    Vector<std::string> enabled_sources;
    HashMap<std::string, std::string> source_paths;
    Vector<std::string> exclude_patterns;
    std::string persistent_buffer_path;

    static AgentConfig loadFromFile(const std::string& config_path);
};

struct SecurityEvent {
    std::string timestamp;
    std::string hostname;
    std::string source;
    std::string event_type;
    std::string severity;
    std::string user;
    std::string process;
    std::string command;
    std::string raw_log;
    std::string agent_id;

    std::string toJson() const;
    HashMap<std::string, std::string> toHashMap() const;
};

class LogCollector {
private:
    std::string source_name;
    std::string log_path;
    std::string pattern;
    int inotify_fd;
    int watch_fd;
    static HashMap<std::string, size_t> file_positions;
    static HashMap<std::string, std::string> file_inodes; // Добавляем отслеживание inode

public:
    LogCollector(const std::string& name, const std::string& path, const std::string& pattern = "");
    ~LogCollector();

    Vector<SecurityEvent> collectNewEvents();
    bool setupInotify();
    string extractUsernameFromPath(const string& path);
    bool checkForChanges();
    std::string getSourceName() const { return source_name; }

private:
    bool loadPosition();
    bool savePosition();
    Vector<SecurityEvent> readFromSpecificPath(const std::string& specific_path);
    Vector<std::string> expandPathPattern();
    bool handleFileRotation(const std::string& path); // Добавляем обработку ротации файлов
    void updateFilePosition(const std::string& path, size_t position); // Обновляем позицию файла
};

class SIEMAgent {
private:
    AgentConfig config;
    std::atomic<bool> running;
    EventProcessor* processor;
    PersistentBuffer* buffer;
    DBClient* db_client;
    Vector<LogCollector*> collectors;
    std::thread monitor_thread;
    std::thread sender_thread;
    std::condition_variable cv;
    std::mutex cv_mutex;
    bool stop_requested;

public:
    SIEMAgent(const std::string& config_path);
    ~SIEMAgent();

    bool start();
    void stop();
    void run();

private:
    void initializeCollectors();
    bool connectToDB();
    void sendEventsToDB(const Vector<SecurityEvent>& events);
    void logMessage(const std::string& message, const std::string& level = "INFO");
    void monitoringLoop();
    void sendingLoop();
    void handleLogRotation(const std::string& source_name);
};

#endif