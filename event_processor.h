#ifndef EVENT_PROCESSOR_H
#define EVENT_PROCESSOR_H

#include "HashMap.h"
#include "vector.h"
#include <string>

struct SecurityEvent; 
string generateEventId(const SecurityEvent& event);
class EventProcessor {
private:
    Vector<std::string> exclude_patterns;
    
public:
    EventProcessor(const Vector<std::string>& filters);
    
    SecurityEvent processLogLine(const std::string& source, 
                               const std::string& log_line,
                               const std::string& agent_id);
    
    SecurityEvent processAuditdLog(const std::string& log_line, const std::string& agent_id);
    SecurityEvent processSyslog(const std::string& log_line, const std::string& agent_id);
    SecurityEvent processBashHistory(const std::string& log_line, const std::string& agent_id, 
                                   const std::string& username);
    
private:
    bool shouldExclude(const std::string& log_line);
    std::string determineEventType(const std::string& source, const std::string& log_line);
    std::string determineSeverity(const std::string& event_type, const std::string& log_line);
    std::string extractUser(const std::string& log_line);
    std::string extractProcess(const std::string& log_line);
    std::string extractCommand(const std::string& log_line);
    std::string extractAuditdField(const std::string& log_line, const std::string& field);
    std::string normalizeTimestamp(const std::string& timestamp);
};

#endif // EVENT_PROCESSOR_H