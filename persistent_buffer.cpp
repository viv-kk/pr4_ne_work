#include "persistent_buffer.h"
#include "siem_agent.h"
#include "JsonParser.h"
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <sstream>
#include <ctime>
#include <algorithm>

using namespace std;

PersistentBuffer::PersistentBuffer(size_t max_size, const std::string& path) 
    : max_memory_size(max_size), storage_path(path), total_events_stored(0) {
    
    size_t last_slash = path.find_last_of('/');
    if (last_slash != string::npos) {
        string dir = path.substr(0, last_slash);
        struct stat st;
        if (stat(dir.c_str(), &st) != 0) {
#ifdef _WIN32
            _mkdir(dir.c_str());
#else
            mkdir(dir.c_str(), 0755);
#endif
        }
    }
    
    loadFromDisk();
}

PersistentBuffer::~PersistentBuffer() {
    if (!memory_buffer.empty()) {
        persistToDisk();
    }
}

bool PersistentBuffer::addEvent(const SecurityEvent& event) {
    lock_guard<mutex> lock(buffer_mutex);
    
    if (memory_buffer.size() >= max_memory_size) {
        persistToDisk();
        
        memory_buffer.clear();
    }
    
    memory_buffer.push_back(event);
    total_events_stored++;
    
    return true;
}

bool PersistentBuffer::addEvents(const Vector<SecurityEvent>& events) {
    lock_guard<mutex> lock(buffer_mutex);
    
    if (memory_buffer.size() + events.size() > max_memory_size) {
        persistToDisk();
        memory_buffer.clear();
    }
    
    for (size_t i = 0; i < events.size(); i++) {
        memory_buffer.push_back(events[i]);
        total_events_stored++;
    }
    return true;
}

Vector<SecurityEvent> PersistentBuffer::getBatch(size_t batch_size) {
    lock_guard<mutex> lock(buffer_mutex);
    
    Vector<SecurityEvent> batch;
    
    size_t from_memory = min(batch_size, memory_buffer.size());//из памяти
    for (size_t i = 0; i < from_memory; i++) {
        batch.push_back(memory_buffer[i]);
    }
    
    if (from_memory > 0) {//удаляем
        Vector<SecurityEvent> new_buffer;
        for (size_t i = from_memory; i < memory_buffer.size(); i++) {
            new_buffer.push_back(memory_buffer[i]);
        }
        memory_buffer = new_buffer;
    }
    
    if (batch.size() < batch_size) {
        Vector<SecurityEvent> disk_events = loadFromDiskBatch(batch_size - batch.size());
        for (size_t i = 0; i < disk_events.size(); i++) {
            batch.push_back(disk_events[i]);
        }
    }
    
    return batch;
}

size_t PersistentBuffer::size() const {
    lock_guard<mutex> lock(buffer_mutex);
    size_t disk_size = getDiskEventCount();
    return memory_buffer.size() + disk_size;
}

void PersistentBuffer::clear() {
    lock_guard<mutex> lock(buffer_mutex);
    memory_buffer.clear();
    
    string data_file = storage_path + "_data.json";
    string index_file = storage_path + "_index.json";
    
    remove(data_file.c_str());
    remove(index_file.c_str());
    
    total_events_stored = 0;
}

size_t PersistentBuffer::getMemorySize() const {
    lock_guard<mutex> lock(buffer_mutex);
    return memory_buffer.size();
}

bool PersistentBuffer::isEmpty() const {
    lock_guard<mutex> lock(buffer_mutex);
    return memory_buffer.empty() && getDiskEventCount() == 0;
}

void PersistentBuffer::persistToDisk() {
    if (memory_buffer.empty()) {
        return;
    }
    
    string data_file = storage_path + "_data.json";
    
    ofstream data_out(data_file, ios::app);
    if (!data_out.is_open()) {
        cerr << "ERROR: Cannot open data file: " << data_file << endl;
        return;
    }
    
    for (size_t i = 0; i < memory_buffer.size(); i++) {
        string json = memory_buffer[i].toJson();
        data_out << json << "\n";
    }
    
    data_out.close();
    
}

void PersistentBuffer::loadFromDisk() {
    string data_file = storage_path + "_data.json";
    ifstream data_in(data_file);
    if (!data_in.is_open()) {
        return; 
    }
    
    string line;
    size_t count = 0;
    while (getline(data_in, line)) {
        if (!line.empty()) {
            count++;
        }
    }
    
    total_events_stored = count;
}

Vector<SecurityEvent> PersistentBuffer::loadFromDiskBatch(size_t batch_size) {
    Vector<SecurityEvent> events;
    string data_file = storage_path + "_data.json";
    
    ifstream data_in(data_file);
    if (!data_in.is_open()) {
        return events;
    }
    
    JsonParser parser;
    size_t loaded = 0;
    string line;
    
    while (loaded < batch_size && getline(data_in, line)) {//загрузка события
        if (!line.empty()) {
            try {
                HashMap<string, string> event_map = parser.parse(line);
                SecurityEvent event;
                
                string value;
                if (event_map.get("timestamp", value)) event.timestamp = value;
                if (event_map.get("hostname", value)) event.hostname = value;
                if (event_map.get("source", value)) event.source = value;
                if (event_map.get("event_type", value)) event.event_type = value;
                if (event_map.get("severity", value)) event.severity = value;
                if (event_map.get("user", value)) event.user = value;
                if (event_map.get("process", value)) event.process = value;
                if (event_map.get("command", value)) event.command = value;
                if (event_map.get("raw_log", value)) event.raw_log = value;
                if (event_map.get("agent_id", value)) event.agent_id = value;
                
                events.push_back(event);
                loaded++;
            } catch (...) {
                cerr << "ERROR: Failed to parse event from disk" << endl;
            }
        }
    }
    
    return events;
}

size_t PersistentBuffer::getDiskEventCount() const {
    string data_file = storage_path + "_data.json";
    ifstream data_in(data_file);
    if (!data_in.is_open()) {
        return 0;
    }
    
    string line;
    size_t count = 0;
    while (getline(data_in, line)) {
        if (!line.empty()) {
            count++;
        }
    }
    
    return count;
}

string PersistentBuffer::getStorageFilename() const {
    time_t now = time(nullptr);
    tm* local = localtime(&now);
    
    char buffer[100];
    strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", local);
    
    return storage_path + "_" + string(buffer) + ".json";
}