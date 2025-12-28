#ifndef PERSISTENT_BUFFER_H
#define PERSISTENT_BUFFER_H

#include "HashMap.h"
#include "vector.h"
#include <string>
#include <mutex>

struct SecurityEvent;

class PersistentBuffer {
private:
    std::string storage_path;   
    size_t max_memory_size;     
    Vector<SecurityEvent> memory_buffer;
    mutable std::mutex buffer_mutex;
    size_t total_events_stored;
    
public:
    PersistentBuffer(size_t max_size, const std::string& path);
    ~PersistentBuffer();
    size_t getMemorySize() const;
    bool addEvent(const SecurityEvent& event);
    bool addEvents(const Vector<SecurityEvent>& events);
    Vector<SecurityEvent> getBatch(size_t batch_size);
    size_t size() const;
    void clear();
    bool isEmpty() const;
    size_t getTotalStored() const { return total_events_stored; }
    
private:
    void persistToDisk();
    void loadFromDisk();
    Vector<SecurityEvent> loadFromDiskBatch(size_t batch_size);
    size_t getDiskEventCount() const;
    std::string getStorageFilename() const;
};

#endif 