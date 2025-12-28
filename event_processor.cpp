#include "event_processor.h"
#include "siem_agent.h"
#include <iostream>
#include <regex>
#include <ctime>
#include <cstring>
#include <algorithm>
#include <unistd.h>

using namespace std;

EventProcessor::EventProcessor(const Vector<string>& filters) 
    : exclude_patterns(filters) {
}

string generateEventId(const SecurityEvent& event) {
    string unique_str = event.timestamp + event.source + event.raw_log;
    
    size_t hash = 0;
    for (char c : unique_str) {
        hash = hash * 31 + c;
    }
    
    return "evt_" + to_string(hash);
}

SecurityEvent EventProcessor::processLogLine(const string& source, 
                                           const string& log_line,
                                           const string& agent_id) {
    SecurityEvent event;
    
    if (shouldExclude(log_line)) {
        return event; 
    }
    
    if (source == "auditd") {
        return processAuditdLog(log_line, agent_id);
    } else if (source == "syslog" || source == "auth") {
        return processSyslog(log_line, agent_id);
    } else if (source == "bash_history" || source == "bash_history_user") { 
        string username = "unknown";
        if (source == "bash_history") username = "root";
        else if (source == "bash_history_user") username = "user"; 
        
        return processBashHistory(log_line, agent_id, username);
    }
    
    event.source = source;
    event.agent_id = agent_id;
    event.raw_log = log_line;
    
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    event.hostname = hostname;
    
    time_t now = time(nullptr);
    char time_buf[100];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    event.timestamp = time_buf;
    
    event.event_type = determineEventType(source, log_line);
    event.severity = determineSeverity(event.event_type, log_line);
    event.user = extractUser(log_line);
    event.process = extractProcess(log_line);
    event.command = extractCommand(log_line);
    
    return event;
}

SecurityEvent EventProcessor::processAuditdLog(const string& log_line, const string& agent_id) {
    SecurityEvent event;
    event.source = "auditd";
    event.agent_id = agent_id;
    event.raw_log = log_line;
    
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    event.hostname = hostname;
    
    size_t msg_pos = log_line.find("msg=audit(");
    if (msg_pos != string::npos) {
        size_t start = msg_pos + 10;
        size_t end = log_line.find("):", start);
        if (end != string::npos) {
            string audit_time = log_line.substr(start, end - start);
            event.timestamp = normalizeTimestamp(audit_time);
        }
    }
    
    if (event.timestamp.empty()) {
        time_t now = time(nullptr);
        char time_buf[100];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
        event.timestamp = time_buf;
    }
    
    event.event_type = extractAuditdField(log_line, "type");
    if (event.event_type.empty()) {
        event.event_type = determineEventType("auditd", log_line);
    }
    
    event.user = extractAuditdField(log_line, "auid");
    if (event.user.empty() || event.user == "unset") {
        event.user = extractAuditdField(log_line, "uid");
    }
    
    event.process = extractAuditdField(log_line, "exe");
    event.command = extractAuditdField(log_line, "cmd");
    
    event.severity = determineSeverity(event.event_type, log_line);
    
    return event;
}

SecurityEvent EventProcessor::processSyslog(const string& log_line, const string& agent_id) {
    SecurityEvent event;
    event.source = "syslog";
    event.agent_id = agent_id;
    event.raw_log = log_line;
    
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    event.hostname = hostname;
    
    regex syslog_regex("^(\\w+\\s+\\d+\\s+\\d+:\\d+:\\d+)\\s+(\\S+)\\s+(\\S+?)\\[(\\d+)\\]:\\s+(.*)$");
    smatch match;
    
    if (regex_search(log_line, match, syslog_regex) && match.size() > 5) {
        string log_timestamp = match[1].str();
        string log_hostname = match[2].str();
        event.process = match[3].str();
        string pid = match[4].str();
        string message = match[5].str();
        
        event.timestamp = normalizeTimestamp(log_timestamp);
        
        event.event_type = determineEventType("syslog", message);
        event.severity = determineSeverity(event.event_type, message);
        event.user = extractUser(message);
        event.command = extractCommand(message);
    } else {
        time_t now = time(nullptr);
        char time_buf[100];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
        event.timestamp = time_buf;
        
        event.event_type = determineEventType("syslog", log_line);
        event.severity = determineSeverity(event.event_type, log_line);
    }
    
    return event;
}

SecurityEvent EventProcessor::processBashHistory(const string& log_line, const string& agent_id,
                                               const string& username) {
    SecurityEvent event;
    
    if (log_line.empty()) {
        return event; 
    }
    
    event.source = "bash_history";
    event.agent_id = agent_id;
    event.raw_log = log_line;
    event.user = username.empty() ? "unknown" : username;
    
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    event.hostname = hostname;
   
    time_t now = time(nullptr);
    char time_buf[100];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    event.timestamp = time_buf;
    
    event.event_type = "shell_command";
    event.severity = determineSeverity("shell_command", log_line); 
    event.process = "bash";
    event.command = log_line;
    
    
    return event;
}

bool EventProcessor::shouldExclude(const string& log_line) {
    for (size_t i = 0; i < exclude_patterns.size(); i++) {
        if (log_line.find(exclude_patterns[i]) != string::npos) {
            return true;
        }
    }
    return false;
}

string EventProcessor::determineEventType(const string& source, const string& log_line) {
    if (source == "auditd") {
        if (log_line.find("USER_LOGIN") != string::npos) return "user_login";
        if (log_line.find("USER_CMD") != string::npos) return "command_execution";
        if (log_line.find("SYSCALL") != string::npos) return "system_call";
        if (log_line.find("EXECVE") != string::npos) return "process_execution";
        if (log_line.find("PROCTITLE") != string::npos) return "process_title";
        if (log_line.find("PATH") != string::npos) return "file_access";
        return "audit_event";
    }
    else if (source == "syslog" || source == "auth") {
        string line_lower = log_line;
        transform(line_lower.begin(), line_lower.end(), line_lower.begin(), ::tolower);
        
        if (line_lower.find("failed password") != string::npos) return "failed_login";
        if (line_lower.find("accepted password") != string::npos) return "successful_login";
        if (line_lower.find("invalid user") != string::npos) return "invalid_user";
        if (line_lower.find("sudo") != string::npos) return "sudo_command";
        if (line_lower.find("session opened") != string::npos) return "session_opened";
        if (line_lower.find("session closed") != string::npos) return "session_closed";
        if (line_lower.find("authentication failure") != string::npos) return "auth_failure";
        return "system_event";
    }
    else if (source == "shell_command") {
        string line_lower = log_line;
        transform(line_lower.begin(), line_lower.end(), line_lower.begin(), ::tolower);
        
        if (line_lower.find("sudo") != string::npos) return "medium";
        if (line_lower.find("rm -rf") != string::npos) return "high";
        if (line_lower.find("chmod 777") != string::npos) return "high";
        if (line_lower.find("passwd") != string::npos) return "medium";
        if (line_lower.find("/etc/shadow") != string::npos) return "high";
        if (line_lower.find("ssh") != string::npos) return "medium";
    }
    
    return "unknown";
}

string EventProcessor::determineSeverity(const string& event_type, const string& log_line) {
    if (event_type == "failed_login" || 
        event_type == "auth_failure" ||
        event_type == "invalid_user") {
        return "high";
    }
    
    if (event_type == "sudo_command" || 
        event_type == "user_login" ||
        event_type == "command_execution" ||
        event_type == "system_call") {
        return "medium";
    }
    
    return "low";
}

string EventProcessor::extractUser(const string& log_line) {
    regex auditd_user_regex("\\b(?:auid|uid)=(\\S+)");
    smatch match;
    if (regex_search(log_line, match, auditd_user_regex) && match.size() > 1) {
        string user = match[1].str();
        if (user != "unset" && user != "-1") {
            return user;
        }
    }
    
    regex syslog_user_regex("user=(\\S+)");
    if (regex_search(log_line, match, syslog_user_regex) && match.size() > 1) {
        return match[1].str();
    }
    
    if (log_line.find("sudo:") != string::npos) {
        regex sudo_user_regex("sudo:\\s+(\\S+)");
        if (regex_search(log_line, match, sudo_user_regex) && match.size() > 1) {
            return match[1].str();
        }
    }
    
    return "unknown";
}

string EventProcessor::extractProcess(const string& log_line) {
    regex auditd_exe_regex("\\bexe=\"([^\"]+)\"");
    smatch match;
    if (regex_search(log_line, match, auditd_exe_regex) && match.size() > 1) {
        string exe = match[1].str();
        size_t last_slash = exe.find_last_of('/');
        if (last_slash != string::npos) {
            return exe.substr(last_slash + 1);
        }
        return exe;
    }
    
    regex syslog_process_regex("(\\S+?)\\[\\d+\\]:");
    if (regex_search(log_line, match, syslog_process_regex) && match.size() > 1) {
        return match[1].str();
    }
    
    return "unknown";
}

string EventProcessor::extractCommand(const string& log_line) {
    regex auditd_cmd_regex("\\bcmd=\"([^\"]+)\"");
    smatch match;
    if (regex_search(log_line, match, auditd_cmd_regex) && match.size() > 1) {
        return match[1].str();
    }
    
    if (log_line.find("/.bash_history") != string::npos) {
        return log_line;
    }
    
    if (log_line.find("COMMAND=") != string::npos) {
        size_t start = log_line.find("COMMAND=") + 8;
        size_t end = log_line.find(" ", start);
        if (end == string::npos) end = log_line.length();
        return log_line.substr(start, end - start);
    }
    
    return "";
}

string EventProcessor::extractAuditdField(const string& log_line, const string& field) {
    string pattern = "\\b" + field + "=([^\\s\"]+|\"[^\"]+\")";
    regex field_regex(pattern);
    smatch match;
    
    if (regex_search(log_line, match, field_regex) && match.size() > 1) {
        string value = match[1].str();
        if (value.length() >= 2 && value[0] == '"' && value[value.length()-1] == '"') {
            value = value.substr(1, value.length() - 2);
        }
        return value;
    }
    
    return "";
}

string EventProcessor::normalizeTimestamp(const string& timestamp) {
    if (timestamp.empty()) {
        time_t now = time(nullptr);
        char buf[100];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
        return string(buf);
    }
    
    if (timestamp.find('.') != string::npos) {
        try {
            double epoch_seconds = stod(timestamp);
            time_t time_sec = static_cast<time_t>(epoch_seconds);
            
            char buf[100];
            strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&time_sec));
            return string(buf);
        } catch (...) {
        }
    }
    
    struct tm tm_time = {};
    if (strptime(timestamp.c_str(), "%b %d %H:%M:%S", &tm_time)) {
        time_t current_time = time(nullptr);
        struct tm* current_tm = localtime(&current_time);
        tm_time.tm_year = current_tm->tm_year;
        
        time_t t = mktime(&tm_time); 
        
        char buf[100];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
        return string(buf);
    }
    
    if (!timestamp.empty() && isdigit(timestamp[0])) {
        try {
            time_t t = static_cast<time_t>(stoll(timestamp));
            char buf[100];
            strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
            return string(buf);
        } catch (...) {
        }
    }
    
    time_t now = time(nullptr);
    char buf[100];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    return string(buf);
}