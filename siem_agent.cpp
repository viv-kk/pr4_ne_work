#include "siem_agent.h"
#include "db_client.h"
#include "JsonParser.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <ctime>
#include <iomanip>
#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cstring>
#include <regex>
#include <cstdio>
#include <sys/inotify.h>
#include <poll.h>
#include <algorithm>
#include <mutex>

using namespace std;

string SecurityEvent::toJson() const {
    ostringstream json;
    json << "{";
    
    auto addField = [&json](const string& name, const string& value, bool first = false) {
        if (!first) json << ",";
        
        string escaped;
        for (char c : value) {
            if (c == '"') {
                escaped += "\\\"";
            } else if (c == '\\') {
                escaped += "\\\\";
            } else if (c == '\n') {
                escaped += "\\n";
            } else if (c == '\r') {
                escaped += "\\r";
            } else if (c == '\t') {
                escaped += "\\t";
            } else if (c >= 0 && c < 32) {
                continue;
            } else {
                escaped += c;
            }
        }
        
        json << "\"" << name << "\":\"" << escaped << "\"";
    };
    
    addField("timestamp", timestamp, true);
    addField("hostname", hostname);
    addField("source", source);
    addField("event_type", event_type);
    addField("severity", severity);
    addField("user", user);
    addField("process", process);
    addField("command", command);
    addField("raw_log", raw_log);
    addField("agent_id", agent_id);
    
    json << "}";
    
    string result = json.str();
    int quote_count = 0;
    for (char c : result) {
        if (c == '"') quote_count++;
    }
    
    if (quote_count % 2 != 0) {
        cerr << "WARNING: Unbalanced quotes in JSON: " << quote_count << endl;
        if (quote_count % 2 != 0) {
            result += "\"";
        }
    }
    
    return result;
}

HashMap<string, string> SecurityEvent::toHashMap() const {
    HashMap<string, string> map;

    map.put("timestamp", timestamp);
    map.put("hostname", hostname);
    map.put("source", source);
    map.put("event_type", event_type);
    map.put("severity", severity);
    map.put("user", user);
    map.put("process", process);
    map.put("command", command);
    map.put("raw_log", raw_log);
    map.put("agent_id", agent_id);

    return map;
}

string getHostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return string(hostname);
    }
    return "unknown";
}

AgentConfig AgentConfig::loadFromFile(const string& config_path) {
    AgentConfig config;
    
    config.server_host = "127.0.0.1";
    config.server_port = 8080;
    config.database = "security_db";
    config.collection = "security_events";
    config.agent_id = "agent-" + getHostname() + "-01";
    config.log_file = "/var/log/siem_agent.log";
    config.send_interval = 30;
    config.batch_size = 100;
    config.max_buffer_size = 1000;
    config.persistent_buffer_path = "/var/lib/siem_agent/buffer";
    
    JsonParser parser;
    
    ifstream config_file(config_path);
    if (config_file.is_open()) {
        cout << "Loading config from: " << config_path << endl;
        stringstream buffer;
        buffer << config_file.rdbuf();
        string json_content = buffer.str();
        config_file.close();
        
        if (!json_content.empty()) {
            try {
                HashMap<string, string> config_map = parser.parse(json_content);
                
                string server_str;
                if (config_map.get("server", server_str)) {
                    HashMap<string, string> server_map = parser.parse(server_str);
                    
                    string value;
                    if (server_map.get("host", value)) {
                        config.server_host = value;
                    }
                    
                    if (server_map.get("port", value)) {
                        try {
                            config.server_port = stoi(value);
                        } catch (...) {
                            cerr << "WARNING: Invalid port, using default" << endl;
                        }
                    }
                }
                
                string agent_str;
                if (config_map.get("agent", agent_str)) {
                    HashMap<string, string> agent_map = parser.parse(agent_str);
                    
                    string value;
                    if (agent_map.get("id", value)) {
                        config.agent_id = value;
                    }
                }
                
                string sender_str;
                if (config_map.get("sender", sender_str)) {
                    HashMap<string, string> sender_map = parser.parse(sender_str);
                    
                    string value;
                    if (sender_map.get("batch_size", value)) {
                        try {
                            config.batch_size = stoi(value);
                        } catch (...) {
                            cerr << "WARNING: Invalid batch_size, using default" << endl;
                        }
                    }
                    
                    if (sender_map.get("send_interval", value)) {
                        try {
                            config.send_interval = stoi(value);
                        } catch (...) {
                            cerr << "WARNING: Invalid send_interval, using default" << endl;
                        }
                    }
                }
                
                string buffer_str;
                if (config_map.get("buffer", buffer_str)) {
                    HashMap<string, string> buffer_map = parser.parse(buffer_str);
                    
                    string value;
                    if (buffer_map.get("max_memory_events", value)) {
                        try {
                            config.max_buffer_size = stoi(value);
                        } catch (...) {
                            cerr << "WARNING: Invalid max_buffer_size, using default" << endl;
                        }
                    }
                    
                    if (buffer_map.get("disk_path", value)) {
                        config.persistent_buffer_path = value;
                    }
                }
                
                string sources_str;
                if (config_map.get("sources", sources_str)) {
                    cout << "Parsing sources array..." << endl;
                    
                    Vector<HashMap<string, string>> sources_array = parser.parseArray(sources_str);
                    
                    for (size_t i = 0; i < sources_array.size(); i++) {
                        HashMap<string, string> source_obj = sources_array[i];
                        
                        string name, enabled_str;
                        if (source_obj.get("name", name)) {
                            bool enabled = true;
                            if (source_obj.get("enabled", enabled_str)) {
                                enabled = (enabled_str == "true");
                            }
                            
                            if (enabled) {
                                config.enabled_sources.push_back(name);
                                cout << "  Added source: " << name << endl;
                                
                                string path, path_pattern;
                                if (source_obj.get("path", path)) {
                                    config.source_paths.put(name, path);
                                    cout << "    Path: " << path << endl;
                                } 
                                else if (source_obj.get("path_pattern", path_pattern)) {
                                    config.source_paths.put(name, path_pattern);
                                    cout << "    Path pattern: " << path_pattern << endl;
                                    
                                    if (name == "bash_history") {
                                        string users_str;
                                        if (source_obj.get("users", users_str)) {
                                            Vector<HashMap<string, string>> users_array = parser.parseArray(users_str);
                                            
                                            for (size_t j = 0; j < users_array.size(); j++) {
                                                HashMap<string, string> user_obj = users_array[j];
                                                string user;
                                                auto items = user_obj.items();
                                                if (!items.empty()) {
                                                    user = items[0].second;
                                                    
                                                    string actual_path = path_pattern;
                                                    size_t pos = actual_path.find("{USER}");
                                                    if (pos != string::npos) {
                                                        actual_path.replace(pos, 6, user);
                                                    }
                                                    
                                                    ifstream test_file(actual_path);
                                                    if (test_file.is_open()) {
                                                        config.source_paths.put(name + "_" + user, actual_path);
                                                        cout << "      User " << user << ": " << actual_path << " [EXISTS]" << endl;
                                                        test_file.close();
                                                    } else {
                                                        cout << "      User " << user << ": " << actual_path << " [NOT FOUND]" << endl;
                                                    }
                                                }
                                            }
                                        } else {
                                            config.source_paths.put(name, path_pattern);
                                        }
                                    } else {
                                        config.source_paths.put(name, path_pattern);
                                    }
                                }
                            }
                        }
                    }
                }
                
                cout << "\nConfig loaded successfully:" << endl;
                cout << "  Agent ID: " << config.agent_id << endl;
                cout << "  Server: " << config.server_host << ":" << config.server_port << endl;
                cout << "  Database: " << config.database << "." << config.collection << endl;
                cout << "  Sources found: " << config.enabled_sources.size() << endl;
                cout << "  Source paths in map: " << config.source_paths.size() << endl;
                
                auto path_items = config.source_paths.items();
                for (size_t i = 0; i < path_items.size(); i++) {
                    cout << "    " << path_items[i].first << " -> " << path_items[i].second << endl;
                }
                
            } catch (const exception& e) {
                cerr << "Config parse error: " << e.what() << endl;
                cerr << "Using default configuration" << endl;
            }
        }
    } else {
        cerr << "WARNING: Cannot open config file '" << config_path << "', using defaults" << endl;
    }
    
    if (config.enabled_sources.empty()) {
        cout << "No sources configured, using defaults" << endl;
        config.enabled_sources.push_back("auditd");
        config.enabled_sources.push_back("syslog");
        config.enabled_sources.push_back("auth");
        
        config.source_paths.put("auditd", "/var/log/audit/audit.log");
        config.source_paths.put("syslog", "/var/log/syslog");
        config.source_paths.put("auth", "/var/log/auth.log");
    }
    
    return config;
}

HashMap<string, size_t> LogCollector::file_positions;
HashMap<string, string> LogCollector::file_inodes;

LogCollector::LogCollector(const string& name, const string& path, const string& pat)
    : source_name(name), log_path(path), pattern(pat), inotify_fd(-1), watch_fd(-1) {

    cout << "LogCollector created: " << name << " -> " << path << endl;

    if (pattern.empty()) {//для шаблона без инотифи
        setupInotify();
    }

    loadPosition();
}

LogCollector::~LogCollector() {
    if (inotify_fd >= 0 && watch_fd >= 0) {
        inotify_rm_watch(inotify_fd, watch_fd);
    }
    if (inotify_fd >= 0) {
        close(inotify_fd);
    }
}

string LogCollector::extractUsernameFromPath(const string& path) {    
    size_t last_slash = path.find_last_of('/');
    if (last_slash == string::npos || last_slash == 0) {
        return "unknown";
    }
    
    size_t prev_slash = path.substr(0, last_slash).find_last_of('/');
    if (prev_slash == string::npos) {
        return "unknown";
    }
    
    return path.substr(prev_slash + 1, last_slash - prev_slash - 1);
}

bool LogCollector::setupInotify() {
    inotify_fd = inotify_init1(IN_NONBLOCK);
    if (inotify_fd < 0) {
        cerr << "ERROR: Failed to initialize inotify for " << source_name
             << " (" << strerror(errno) << ")" << endl;
        return false;
    }

    struct stat st;
    if (stat(log_path.c_str(), &st) != 0) {
        cerr << "WARNING: Log file does not exist yet: " << log_path << endl;
    }

    watch_fd = inotify_add_watch(inotify_fd, log_path.c_str(),
                                 IN_MODIFY | IN_DELETE_SELF | IN_MOVE_SELF | IN_CREATE);

    if (watch_fd < 0) {
        cerr << "ERROR: Failed to add inotify watch for " << log_path
             << " (" << strerror(errno) << ")" << endl;
        close(inotify_fd);
        inotify_fd = -1;
        return false;
    }

    cout << "Inotify setup for " << log_path << " (wd=" << watch_fd << ")" << endl;
    return true;
}

bool LogCollector::checkForChanges() {
    if (inotify_fd < 0) {
        return false;
    }

    struct pollfd pfd;
    pfd.fd = inotify_fd;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, 0);//неблок

    if (ret > 0 && (pfd.revents & POLLIN)) {
        char buffer[4096];
        read(inotify_fd, buffer, sizeof(buffer));
        return true;
    }

    return false;
}

Vector<SecurityEvent> LogCollector::collectNewEvents() {
    Vector<SecurityEvent> events;

    if (!pattern.empty()) {
        Vector<string> paths = expandPathPattern();
        for (size_t i = 0; i < paths.size(); i++) {
            string current_path = paths[i];
            Vector<SecurityEvent> path_events = readFromSpecificPath(current_path);
            for (size_t j = 0; j < path_events.size(); j++) {
                events.push_back(path_events[j]);
            }
        }
        return events;
    }

    struct stat st;
    if (stat(log_path.c_str(), &st) != 0) {
        cerr << "ERROR: Log file does not exist: " << log_path << endl;
        return events;
    }

    string position_key = source_name + "_" + log_path;
    size_t last_position = 0;
    if (!file_positions.get(position_key, last_position)) {
        last_position = 0;
    }

    string current_inode = to_string(st.st_ino);
    string saved_inode;
    file_inodes.get(position_key, saved_inode);

    if (!saved_inode.empty() && current_inode != saved_inode) {
        cout << "Log rotation detected for " << log_path
             << ", inode changed from " << saved_inode << " to " << current_inode << endl;
        last_position = 0; 
    }
    if ((size_t)st.st_size < last_position) {
        cout << "Log truncation detected for " << log_path
             << ", resetting position to 0" << endl;
        last_position = 0;
    }

    cout << "Reading " << log_path << " from position " << last_position 
     << ", file size: " << st.st_size 
     << ", last_position stored: " << file_positions.get(position_key, last_position) << endl;

    if (last_position == (size_t)st.st_size) {
        return events;
    }

    ifstream log_file(log_path);
    if (!log_file.is_open()) {
        cerr << "ERROR: Cannot open log: " << log_path << endl;
        return events;
    }

    log_file.seekg(last_position);
    string line;
    size_t lines_read = 0;

    while (getline(log_file, line)) {
        SecurityEvent event;
        event.source = source_name;
        event.raw_log = line;
        
        if (source_name.find("bash_history") != string::npos) {
            event.user = extractUsernameFromPath(log_path);
        }
        
        events.push_back(event);
        lines_read++;
    }

    if (lines_read > 0) {
        last_position = log_file.tellg();
        if (last_position == -1) {
            last_position = st.st_size;
        }
        file_positions.put(position_key, last_position);
        file_inodes.put(position_key, current_inode); 
        savePosition();
        cout << "Collected " << lines_read << " lines from " << source_name
             << ", new position: " << last_position << "/" << st.st_size << endl;
    } else {
        last_position = st.st_size;
        file_positions.put(position_key, last_position);
        file_inodes.put(position_key, current_inode);
        savePosition();
    }
    

    log_file.close();

    return events;
}
Vector<string> LogCollector::expandPathPattern() {
    Vector<string> paths;

    if (pattern.find('*') == string::npos) {
        paths.push_back(pattern);
        return paths;
    }

    size_t star_pos = pattern.find('*');
    if (star_pos != string::npos) {
        string dir_pattern = pattern.substr(0, star_pos);
        size_t slash_pos = pattern.find('/', star_pos);
        string file_pattern;
        if (slash_pos != string::npos) {
            file_pattern = pattern.substr(slash_pos + 1);
        } else {
            file_pattern = pattern.substr(star_pos + 1);
        }

        DIR* dir = opendir(dir_pattern.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (entry->d_type == DT_DIR) {
                    string username = entry->d_name;
                    if (username != "." && username != "..") {
                        string home_dir = dir_pattern + username;
                        string bash_history = home_dir + "/" + file_pattern;

                        struct stat st;
                        if (stat(bash_history.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                            paths.push_back(bash_history);
                        }
                    }
                }
            }
            closedir(dir);
        }
    }

    return paths;
}

Vector<SecurityEvent> LogCollector::readFromSpecificPath(const string& specific_path) {
    Vector<SecurityEvent> events;

    struct stat st;
    if (stat(specific_path.c_str(), &st) != 0) {
        return events;
    }

    string position_key = source_name + "_" + specific_path;
    size_t last_position = 0;
    if (!file_positions.get(position_key, last_position)) {
        last_position = 0;
    }

    string current_inode = to_string(st.st_ino);
    string saved_inode;
    file_inodes.get(position_key, saved_inode);

    if (!saved_inode.empty() && current_inode != saved_inode) {
        cout << "Log rotation detected for " << specific_path
             << ", inode changed from " << saved_inode << " to " << current_inode << endl;
        last_position = 0; 
    }

    if (last_position > (size_t)st.st_size) {
        last_position = 0;
    }

    ifstream log_file(specific_path);
    if (!log_file.is_open()) {
        return events;
    }

    log_file.seekg(last_position);
    string line;
    size_t lines_read = 0;

    while (getline(log_file, line)) {
        SecurityEvent event;
        event.source = source_name;
        event.raw_log = line;
        events.push_back(event);
        lines_read++;
    }

    if (lines_read > 0) {
        last_position = log_file.tellg();
        if (last_position == -1) {
            last_position = st.st_size; 
        }
        file_positions.put(position_key, last_position);
        file_inodes.put(position_key, current_inode);
        savePosition();
    }

    log_file.close();

    return events;
}

bool LogCollector::savePosition() {
    JsonParser parser;
    HashMap<string, string> position_map;

    auto items = file_positions.items();
    for (size_t i = 0; i < items.size(); i++) {
        string key = items[i].first;
        size_t position = items[i].second;

        string inode;
        file_inodes.get(key, inode);

        string save_key = key + ":pos";
        string save_value = to_string(position) + ":" + inode;

        position_map.put(save_key, save_value);
    }

    string pos_file = "/tmp/siem_positions.json";
    ofstream file(pos_file);
    if (file.is_open()) {
        string json = "{";
        auto pos_items = position_map.items();
        for (size_t i = 0; i < pos_items.size(); i++) {
            if (i > 0) json += ",";
            json += "\"" + pos_items[i].first + "\":\"" + pos_items[i].second + "\"";
        }
        json += "}";

        file << json;
        file.close();
        return true;
    }

    return false;
}

bool LogCollector::loadPosition() {
    JsonParser parser;
    string pos_file = "/tmp/siem_positions.json";

    ifstream file(pos_file);
    if (!file.is_open()) {
        return false;
    }

    stringstream buffer;
    buffer << file.rdbuf();
    string json_content = buffer.str();
    file.close();

    if (!json_content.empty()) {
        try {
            HashMap<string, string> position_map = parser.parse(json_content);

            auto items = position_map.items();
            for (size_t i = 0; i < items.size(); i++) {
                string key = items[i].first;
                string value = items[i].second;

                if (key.find(":pos") != string::npos) {
                    string original_key = key.substr(0, key.length() - 4);

                    size_t colon_pos = value.find(':');
                    if (colon_pos != string::npos) {
                        string position_str = value.substr(0, colon_pos);
                        string inode = value.substr(colon_pos + 1);

                        try {
                            size_t pos = stoull(position_str);
                            file_positions.put(original_key, pos);

                            if (!inode.empty()) {
                                file_inodes.put(original_key, inode);
                            }
                        } catch (...) {
                            file_positions.put(original_key, 0);
                        }
                    } else {
                        try {
                            size_t pos = stoull(value);
                            file_positions.put(original_key, pos);
                        } catch (...) {
                            file_positions.put(original_key, 0);
                        }
                    }
                }
            }

            return true;
        } catch (const exception& e) {
            cerr << "Load position error: " << e.what() << endl;
        }
    }

    return false;
}

bool LogCollector::handleFileRotation(const std::string& path) {
    string position_key = source_name + "_" + path;

    file_positions.put(position_key, 0);

    file_inodes.remove(position_key);

    cout << "Handled file rotation for " << path << endl;
    return true;
}

void LogCollector::updateFilePosition(const std::string& path, size_t position) {
    string position_key = source_name + "_" + path;
    file_positions.put(position_key, position);

    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        string inode = to_string(st.st_ino);
        file_inodes.put(position_key, inode);
    }

    savePosition();
}

SIEMAgent::SIEMAgent(const string& config_path) 
    : config(AgentConfig::loadFromFile(config_path)),
      running(false),
      stop_requested(false) {
    
    cout << "---------SIEMAgent Constructor---------" << endl;
    cout << "Agent ID: " << config.agent_id << endl;
    
    config.batch_size = 50;  
    config.send_interval = 2;   
    config.max_buffer_size = 5000;
    
    processor = new EventProcessor(config.exclude_patterns);
    buffer = new PersistentBuffer(config.max_buffer_size, config.persistent_buffer_path);
    db_client = new DBClient(config.server_host, config.server_port, config.database);
    
    cout << "Adjusted settings: batch_size=" << config.batch_size 
         << ", send_interval=" << config.send_interval << endl;

    cout << "Enabled sources from config:" << endl;
    for (size_t i = 0; i < config.enabled_sources.size(); i++) {
        string path;
        if (config.source_paths.get(config.enabled_sources[i], path)) {
            cout << "  - " << config.enabled_sources[i] << ": " << path << endl;
        }
    }
    
    initializeCollectors();
    cout << "---------SIEMAgent Constructor Complete---------" << endl;
}

SIEMAgent::~SIEMAgent() {
    cout << "---------SIEMAgent Destructor---------" << endl;
    stop();

    delete processor;
    delete buffer;
    delete db_client;

    for (size_t i = 0; i < collectors.size(); i++) {
        delete collectors[i];
    }

    if (monitor_thread.joinable()) {
        monitor_thread.join();
    }

    if (sender_thread.joinable()) {
        sender_thread.join();
    }

    cout << "---------SIEMAgent Destroyed---------" << endl;
}

void SIEMAgent::initializeCollectors() {
    cout << "---------initializeCollectors()---------" << endl;
    cout << "Enabled sources: " << config.enabled_sources.size() << endl;

    for (size_t i = 0; i < config.enabled_sources.size(); i++) {
        string source_name = config.enabled_sources[i];
        string source_path;

        if (config.source_paths.get(source_name, source_path)) {
            cout << "  Source " << i << ": " << source_name << " -> " << source_path << endl;

            if (source_path.find('*') == string::npos) {
                ifstream test_file(source_path);
                if (test_file.is_open()) {
                    cout << "    File exists: YES" << endl;
                    test_file.close();
                } else {
                    cout << "    File exists: NO (will monitor for creation)" << endl;
                }
            }

            LogCollector* collector = new LogCollector(source_name, source_path);
            collectors.push_back(collector);
        } else {
            cout << "  ERROR: No path found for source: " << source_name << endl;
        }
    }

    cout << "Total collectors created: " << collectors.size() << endl;
    cout << "---------initializeCollectors() Complete---------" << endl;
}

bool SIEMAgent::connectToDB() {
    cout << "---------connectToDB()---------" << endl;
    cout << "Connecting to " << config.server_host << ":" << config.server_port << endl;

    bool connected = db_client->connect();
    if (connected) {
        cout << "DB connection SUCCESS" << endl;
    } else {
        cout << "DB connection FAILED" << endl;
    }

    return connected;
}

void SIEMAgent::sendEventsToDB(const Vector<SecurityEvent>& events) {
    if (events.empty()) {
        return;
    }
    
    cout << "---------sendEventsToDB()---------" << endl;
    cout << "Sending " << events.size() << " events to DB" << endl;
    const size_t MAX_EVENTS_PER_BATCH = 50;
    size_t total_sent = 0;
    size_t batch_count = 0;
    
    for (size_t start = 0; start < events.size(); start += MAX_EVENTS_PER_BATCH) {
        size_t end = min(start + MAX_EVENTS_PER_BATCH, events.size());
        size_t batch_size = end - start;
        
        cout << "\nProcessing batch " << ++batch_count << " (" << batch_size << " events)" << endl;
        
        Request req;
        req.database = config.database;
        req.operation = "insert";
        req.collection = config.collection;
        
        for (size_t i = start; i < end; i++) {
            SecurityEvent event = events[i];
            string event_json = event.toJson();
            if (event_json.empty() || event_json[0] != '{') {
                cout << "ERROR: Invalid JSON from event: " << event_json.substr(0, 50) << endl;
                continue;
            }
            req.data.push_back(event_json);
            
            if (i == start) {
                cout << "  First event JSON (" << event_json.length() << " bytes): ";
                if (event_json.length() > 100) {
                    cout << event_json.substr(0, 100) << "..." << endl;
                } else {
                    cout << event_json << endl;
                }
            }
        }
        
        string request_json = req.toJson();
        cout << "  Batch request size: " << request_json.length() << " bytes" << endl;
        
        if (request_json.length() > 8000) {
            cout << "  WARNING: Batch is too large, reducing..." << endl;
            req.data.clear();
            for (size_t i = start; i < min(start + 5, events.size()); i++) {
                req.data.push_back(events[i].toJson());
            }
            request_json = req.toJson();
            cout << "  Reduced to: " << request_json.length() << " bytes" << endl;
        }
        
        Response response = db_client->sendRequest(req);
        
        cout << "  Response - Status: " << response.status
             << ", Message: " << response.message 
             << ", Count: " << response.count << endl;
        
        if (response.status == "success") {
            total_sent += response.count;
            cout << "  Successfully sent " << response.count << " events" << endl;
        } else {
            logMessage("Send error in batch " + to_string(batch_count) + ": " + response.message, "ERROR");
            
            Vector<SecurityEvent> failed_batch;
            for (size_t i = start; i < end; i++) {
                failed_batch.push_back(events[i]);
            }
            buffer->addEvents(failed_batch);
            
            this_thread::sleep_for(chrono::seconds(1));
            break;
        }
        
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    
    if (total_sent > 0) {
        logMessage("Total sent " + to_string(total_sent) + " events to DB");
    }
    
    cout << "---------sendEventsToDB() Complete---------" << endl;
}

void SIEMAgent::logMessage(const string& message, const string& level) {
    time_t now = time(nullptr);
    char time_buf[100];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));

    string log_entry = string(time_buf) + " [" + level + "] " + message;

    cout << log_entry << endl;

    ofstream log_file(config.log_file, ios::app);
    if (log_file.is_open()) {
        log_file << log_entry << endl;
        log_file.close();
    }
}

bool SIEMAgent::start() {
    cout << "---------SIEMAgent::start()---------" << endl;
    logMessage("Starting SIEM Agent: " + config.agent_id);

    if (!connectToDB()) {
        logMessage("DB connect failed", "ERROR");
        cout << "---------SIEMAgent::start() FAILED---------" << endl;
        return false;
    }

    logMessage("DB connected");
    running = true;

    monitor_thread = thread(&SIEMAgent::monitoringLoop, this);
    sender_thread = thread(&SIEMAgent::sendingLoop, this);

    cout << "---------SIEMAgent::start() SUCCESS---------" << endl;
    return true;
}

void SIEMAgent::stop() {
    if (!running) return;
    
    cout << "---------SIEMAgent::stop()---------" << endl;
    logMessage("Stopping SIEM Agent gracefully");
    
    running = false;
    stop_requested = true;
    
    if (monitor_thread.joinable()) {
        monitor_thread.join();
    }
    
    if (sender_thread.joinable()) {
        sender_thread.join();
    }
    
    if (!buffer->isEmpty()) {
        cout << "Sending remaining " << buffer->size() << " events..." << endl;
        Vector<SecurityEvent> remaining = buffer->getBatch(buffer->size());
        
        if (!db_client->isConnected()) {
            connectToDB();
        }
        
        if (db_client->isConnected()) {
            sendEventsToDB(remaining);
        } else {
            logMessage("Could not reconnect to save remaining events", "WARN");
        }
    }
    
    db_client->disconnect();
    cout << "---------SIEMAgent::stop() Complete---------" << endl;
}

void SIEMAgent::run() {
    if (!running) {
        logMessage("Agent not started", "ERROR");
        return;
    }

    cout << "---------SIEMAgent::run() Started---------" << endl;
    cout << "Number of collectors: " << collectors.size() << endl;
    cout << "Send interval: " << config.send_interval << " seconds" << endl;
    cout << "Batch size: " << config.batch_size << endl;
    cout << "Max buffer size: " << config.max_buffer_size << endl;

    logMessage("SIEM Agent running");

    while (running && !stop_requested) {
        this_thread::sleep_for(chrono::seconds(1));

        static int counter = 0;
        if (++counter % 30 == 0) {
            logMessage("Agent status: buffer size = " + to_string(buffer->size()) +
                      ", running = " + (running ? "yes" : "no"));
        }
    }

    cout << "---------SIEMAgent::run() Ended---------" << endl;
}

void SIEMAgent::monitoringLoop() {
    cout << "Monitoring loop started" << endl;

    InotifyWrapper inotify;

    for (size_t i = 0; i < collectors.size(); i++) {
        string path;
        if (config.source_paths.get(collectors[i]->getSourceName(), path)) {
            if (path.find('*') == string::npos) {
                inotify.addWatch(path, IN_MODIFY | IN_CREATE);
            }
        }
    }

    auto last_collection_time = chrono::steady_clock::now();

    while (running && !stop_requested) {
        auto events = inotify.readEvents(1000);

        if (!events.empty()) {
            cout << "Inotify detected " << events.size() << " changes" << endl;
        }

        auto now = chrono::steady_clock::now();
        if (!events.empty() ||
            chrono::duration_cast<chrono::seconds>(now - last_collection_time).count() >= 10) {

            cout << "Collecting logs..." << endl;

            for (size_t i = 0; i < collectors.size(); i++) {
                Vector<SecurityEvent> raw_events = collectors[i]->collectNewEvents(); 
                cout << "Collected " << raw_events.size() << " raw events from "
                     << collectors[i]->getSourceName() << endl;

                for (size_t j = 0; j < raw_events.size(); j++) {
                    SecurityEvent processed = processor->processLogLine(
                        raw_events[j].source,
                        raw_events[j].raw_log,
                        config.agent_id
                    );

                    if (!processed.source.empty()) {
                        if (processed.timestamp.empty()) {
                            auto now = std::chrono::system_clock::now();
                            auto time_t_now = std::chrono::system_clock::to_time_t(now);
                            std::stringstream ss;
                            ss << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H:%M:%SZ");
                            processed.timestamp = ss.str();
                        }

                        buffer->addEvent(processed);
                    }
                }

                if (!raw_events.empty()) {
                    logMessage("Collected " + to_string(raw_events.size()) +
                              " events from " + collectors[i]->getSourceName());
                }
            }

            last_collection_time = now;
        }

        this_thread::sleep_for(chrono::milliseconds(100));
    }

    cout << "Monitoring loop ended" << endl;
}

void SIEMAgent::sendingLoop() {
    cout << "Sending loop started" << endl;
    
    while (running && !stop_requested) {
        size_t current_size = buffer->size();
        
        if (current_size > 0) {
            cout << "\n[SENDING] Buffer size: " << current_size << endl;
            
            if (db_client->isConnected()) {
                cout << "[SENDING] Connection OK" << endl;
            } else {
                cout << "[SENDING] Reconnecting..." << endl;
                if (!connectToDB()) {
                    cout << "[SENDING] Reconnect failed, waiting..." << endl;
                    this_thread::sleep_for(chrono::seconds(5));
                    continue;
                }
            }
            
            size_t batch_size = min((size_t)100, current_size); 
            cout << "[SENDING] Preparing batch of " << batch_size << " events" << endl;
            
            Vector<SecurityEvent> to_send = buffer->getBatch(batch_size);
            
            if (!to_send.empty()) {
                sendEventsToDB(to_send);
            }
        }
        
        this_thread::sleep_for(chrono::milliseconds(500));
    }
    
    cout << "Sending loop ended" << endl;
}