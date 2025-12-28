#ifndef INOTIFY_WRAPPER_H
#define INOTIFY_WRAPPER_H

#include <string>
#include <cstdint>
#include <unordered_map>  
#include "vector.h"
#include <sys/inotify.h>

class InotifyWrapper {
private:
    int inotify_fd;
    std::unordered_map<int, std::string> watch_descriptors;
    std::unordered_map<std::string, int> path_to_wd;
    
public:
    InotifyWrapper();
    ~InotifyWrapper();
    
    bool addWatch(const std::string& path, uint32_t mask);
    bool removeWatch(const std::string& path);
    Vector<std::pair<std::string, uint32_t>> readEvents(int timeout_ms = -1);
    int getFileDescriptor() const { return inotify_fd; }
    
    static const uint32_t DEFAULT_MASK = IN_MODIFY | IN_DELETE_SELF | IN_MOVE_SELF | IN_CREATE;
    
private:
    static const int EVENT_SIZE = sizeof(struct inotify_event);
    static const int BUF_LEN = 1024 * (EVENT_SIZE + 16);
};

#endif 