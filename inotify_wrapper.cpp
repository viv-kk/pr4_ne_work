#include "inotify_wrapper.h"
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <iostream>
#include <cstring>
#include <sys/stat.h>
#include <dirent.h>

using namespace std;

InotifyWrapper::InotifyWrapper() {
    inotify_fd = inotify_init1(IN_NONBLOCK);
    if (inotify_fd < 0) {
        cerr << "Failed to initialize inotify: " << strerror(errno) << endl;
    }
}

InotifyWrapper::~InotifyWrapper() {
    for (auto it = path_to_wd.begin(); it != path_to_wd.end(); ++it) {
        int wd = it->second;
        inotify_rm_watch(inotify_fd, wd);
    }
    
    if (inotify_fd >= 0) {
        close(inotify_fd);
    }
}

bool InotifyWrapper::addWatch(const std::string& path, uint32_t mask) {
    if (inotify_fd < 0) {
        return false;
    }
    
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        cerr << "Path does not exist: " << path << endl;
        return false;
    }
    
    int wd = inotify_add_watch(inotify_fd, path.c_str(), mask);
    if (wd < 0) {
        cerr << "Failed to add watch for " << path << ": " << strerror(errno) << endl;
        return false;
    }
    
    watch_descriptors[wd] = path;
    path_to_wd[path] = wd;
    
    cout << "Added inotify watch for: " << path << " (wd=" << wd << ")" << endl;
    return true;
}

bool InotifyWrapper::removeWatch(const std::string& path) {
    auto it = path_to_wd.find(path);
    if (it == path_to_wd.end()) {
        return false;
    }
    
    int wd = it->second;
    
    if (inotify_rm_watch(inotify_fd, wd) < 0) {
        cerr << "Failed to remove watch for " << path << ": " << strerror(errno) << endl;
        return false;
    }
    
    watch_descriptors.erase(wd);
    path_to_wd.erase(path);
    
    return true;
}

Vector<std::pair<std::string, uint32_t>> InotifyWrapper::readEvents(int timeout_ms) {
    Vector<std::pair<std::string, uint32_t>> events;
    
    if (inotify_fd < 0) {
        return events;
    }
    
    struct pollfd pfd;//неблок чтение
    pfd.fd = inotify_fd;
    pfd.events = POLLIN;
    
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret <= 0) {
        return events;//таймаут\ошибка
    }
    
    if (!(pfd.revents & POLLIN)) {
        return events;
    }
    
    char buffer[BUF_LEN];
    ssize_t length = read(inotify_fd, buffer, BUF_LEN);
    
    if (length < 0) {
        if (errno != EAGAIN) {
            cerr << "Error reading inotify events: " << strerror(errno) << endl;
        }
        return events;
    }
    
    ssize_t i = 0;
    while (i < length) {
        struct inotify_event* event = (struct inotify_event*)&buffer[i];
        
        if (event->len > 0) {
            auto it = watch_descriptors.find(event->wd);
            if (it != watch_descriptors.end()) {
                string path = it->second;
                string full_path = path;
                if (event->len > 0) {
                    full_path += "/" + string(event->name);
                }
                
                events.push_back({full_path, event->mask});
                cout << "Inotify event: " << full_path << " mask=0x" << hex << event->mask << dec << endl;
            }
        }
        
        i += EVENT_SIZE + event->len;
    }
    
    return events;
}