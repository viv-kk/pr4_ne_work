# siem_web_server.py - полная версия
"""
SIEM Web Server - REST API для веб-интерфейса
Использует существующий NoSQL DBMS через HTTP
"""
from fastapi import FastAPI, HTTPException, Depends, Request, Response, Query
from fastapi.security import HTTPBasic, HTTPBasicCredentials
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, HTMLResponse, StreamingResponse
from pydantic import BaseModel
from typing import List, Optional, Dict, Any, Union
import requests
import json
import csv
import io
import os
import re
from datetime import datetime, timedelta
import logging
import base64
from functools import wraps
from functools import lru_cache
import time
import random
import math
from functools import lru_cache

# Настройка логгера
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# Конфигурация
DB_SERVER_HOST = os.getenv("DB_SERVER_HOST", "localhost")
DB_SERVER_PORT = os.getenv("DB_SERVER_PORT", "8080")
SECURITY_DB = os.getenv("SECURITY_DB", "security_db")
SECURITY_COLLECTION = os.getenv("SECURITY_COLLECTION", "security_events")

# Basic Auth пользователи
USERS = {
    "admin": os.getenv("ADMIN_PASSWORD", "admin123"),
    "operator": os.getenv("OPERATOR_PASSWORD", "operator123")
}

app = FastAPI(
    title="SIEM Web Interface",
    version="1.0.0",
    docs_url="/api/docs",
    redoc_url="/api/redoc"
)

security = HTTPBasic()

# CORS middleware
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Модели данных
class LoginRequest(BaseModel):
    username: str
    password: str

class SearchRequest(BaseModel):
    query: Optional[Dict[str, Any]] = None
    search_text: Optional[str] = None
    start_date: Optional[str] = None
    end_date: Optional[str] = None
    severity: Optional[List[str]] = None
    event_type: Optional[List[str]] = None
    source: Optional[List[str]] = None

class ExportRequest(BaseModel):
    format: str = "json"
    query: Optional[Dict[str, Any]] = None

# Вспомогательные функции
def get_db_connection():
    return f"http://{DB_SERVER_HOST}:{DB_SERVER_PORT}"

def build_db_request(
    operation: str,
    collection: str = SECURITY_COLLECTION,
    query: Optional[Dict] = None,
    data: Optional[List] = None
) -> Dict:
    """Строит запрос к NoSQL СУБД"""
    request = {
        "database": SECURITY_DB,
        "collection": collection,
        "operation": operation
    }
    
    if query:
        request["query"] = json.dumps(query) if isinstance(query, dict) else query
    
    if data:
        request["data"] = data
    
    return request

def initialize_database():
    """Инициализирует БД при запуске веб-сервера"""
    logger.info("Initializing database...")
    
    # Пытаемся создать тестовое событие, чтобы инициировать создание БД
    test_event = {
        "timestamp": datetime.now().isoformat(),
        "hostname": "web-server",
        "source": "web_server",
        "event_type": "system_startup",
        "severity": "low",
        "user": "system",
        "process": "siem_web_server",
        "command": "startup",
        "raw_log": "SIEM web server started and initializing database",
        "agent_id": "web_server_init"
    }
    
    try:
        # Пробуем вставить тестовое событие
        request_data = {
            "database": SECURITY_DB,
            "collection": SECURITY_COLLECTION,
            "operation": "insert",
            "data": [json.dumps(test_event)]
        }
        
        # Отправляем запрос через сокет (как в query_database)
        import socket
        
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2.0)
        
        try:
            sock.connect(('localhost', 8080))
            json_data = json.dumps(request_data)
            sock.sendall(json_data.encode('utf-8'))
            
            # Читаем ответ (не обязательно полностью)
            response_data = b""
            sock.settimeout(1.0)
            try:
                chunk = sock.recv(1024)
                if chunk:
                    response_data += chunk
            except socket.timeout:
                pass
                
            sock.close()
            
            logger.info("Database initialization attempted")
            
        except Exception as e:
            logger.warning(f"Could not connect to DB server: {e}")
            
    except Exception as e:
        logger.error(f"Database initialization failed: {e}")
 
def query_database(
    operation: str,
    collection: str = SECURITY_COLLECTION,
    query: Optional[Dict] = None,
    data: Optional[List] = None,
) -> Dict:
    """Выполняет запрос к существующей NoSQL СУБД с пагинацией"""
    
    # Строим запрос
    request_data = {
        "database": SECURITY_DB,
        "collection": collection,
        "operation": operation
    }
    
    if query:
        if isinstance(query, str):
            request_data["query"] = query
        else:
            request_data["query"] = json.dumps(query)
    
    if data:
        request_data["data"] = data
    
    try:
        logger.info(f"Querying DB: {operation} on {SECURITY_DB}.{collection}")
        
        import socket
        import time
        
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5.0)
        
        try:
            sock.connect(('localhost', 8080))
            
            json_data = json.dumps(request_data)
            sock.sendall(json_data.encode('utf-8'))
            
            # Даем время на обработку
            time.sleep(0.1)
            
            response_data = b""
            sock.settimeout(5.0)
            
            # Читаем данные частями
            while True:
                try:
                    chunk = sock.recv(4096)
                    if not chunk:
                        break
                    response_data += chunk
                    
                    # Проверяем, не получили ли мы полный JSON
                    try:
                        # Пробуем распарсить накопленные данные
                        temp_response = response_data.decode('utf-8', errors='ignore').strip()
                        json.loads(temp_response)
                        break  # JSON валиден, выходим
                    except json.JSONDecodeError:
                        # JSON не завершен, продолжаем читать
                        continue
                        
                except socket.timeout:
                    break
                except Exception as e:
                    logger.debug(f"Read error: {e}")
                    break
            
            sock.close()
            
            if response_data:
                response_str = response_data.decode('utf-8', errors='ignore').strip()
                logger.debug(f"Raw response: {response_str[:200]}...")
                
                # Пробуем найти JSON в ответе (более надежный метод)
                json_str = extract_json_from_string(response_str)
                
                if json_str:
                    try:
                        # Чистим JSON перед парсингом
                        json_str = clean_json_string(json_str)
                        result = json.loads(json_str)
                        logger.debug(f"Parsed response: {result.get('status')}, count: {result.get('count')}")
                        return result
                    except json.JSONDecodeError as e:
                        logger.warning(f"JSON decode error: {e}")
                        logger.debug(f"Problematic JSON: {json_str[:200]}")
                        # Все равно возвращаем успешный результат с нулевым count
                        return {
                            "status": "error",
                            "message": "No data found",
                            "data": [],
                            "count": 0
                        }
            
            # Если нет данных в ответе
            return {
                "status": "error",  # ← ИСПРАВЛЕНО! для совместимости
                "message": "No data available",
                "data": [],
                "count": 0
            }
                
        except ConnectionRefusedError as e:
            logger.error(f"Cannot connect to DB server: {e}")
            return {
                "status": "error",  # ← ИСПРАВЛЕНО!
                "message": "Database server is not running",
                "data": [],
                "count": 0
            }
        except socket.timeout as e:
            logger.error(f"Connection timeout: {e}")
            return {
                "status": "error",  # ← ИСПРАВЛЕНО!
                "message": "Connection timeout",
                "data": [],
                "count": 0
            }
        except Exception as e:
            logger.error(f"Connection error: {e}")
            return {
                "status": "error",  # ← ИСПРАВЛЕНО!
                "message": f"Connection error: {str(e)}",
                "data": [],
                "count": 0
            }
            
    except Exception as e:
        logger.error(f"Database connection failed: {e}")
        return {
            "status": "error",  # ← ИСПРАВЛЕНО!
            "message": f"Failed to connect: {str(e)}",
            "data": [],
            "count": 0
        }

def extract_json_from_string(text: str) -> Optional[str]:
    """Извлекает JSON из строки, даже если есть лишние символы"""
    if not text:
        return None
    
    # Ищем первую {
    start = text.find('{')
    if start == -1:
        return None
    
    # Ищем последнюю } начиная с start
    balance = 0
    end = -1
    
    for i in range(start, len(text)):
        if text[i] == '{':
            balance += 1
        elif text[i] == '}':
            balance -= 1
            if balance == 0:
                end = i
                break
    
    if end == -1 or end <= start:
        return None
    
    return text[start:end+1]

def clean_json_string(json_str: str) -> str:
    """Чистит JSON строку от проблемных символов"""
    # Заменяем нестандартные кавычки
    json_str = json_str.replace('“', '"').replace('”', '"').replace("‘", "'").replace("’", "'")
    
    # Убираем лишние пробелы и переносы строк внутри строк
    import re
    # Исправляем незакрытые строки
    json_str = re.sub(r'(?<!\\)"(.*?)(?<!\\)"', lambda m: f'"{m.group(1).replace(chr(0), "").replace(chr(1), "")}"', json_str)
    
    # Убираем нулевые байты
    json_str = json_str.replace('\x00', '').replace('\x01', '')
    
    # Исправляем экранированные кавычки
    json_str = json_str.replace('\\"', '"')
    
    return json_str
        
def initialize_database_with_data() -> Dict:
    """Создает БД и добавляет тестовые данные"""
    logger.info(f"Creating database {SECURITY_DB} and collection {SECURITY_COLLECTION}")
    
    # Тестовые события для инициализации
    test_events = []
    
    from datetime import datetime, timedelta
    import random
    
    # Создаем несколько тестовых событий
    for i in range(10):
        hours_ago = random.randint(0, 48)
        event_time = datetime.now() - timedelta(hours=hours_ago)
        
        event_types = ['failed_login', 'successful_login', 'sudo_command', 'system_startup']
        severities = ['low', 'medium', 'high']
        
        event = {
            "timestamp": event_time.isoformat(),
            "hostname": f"server-{random.randint(1, 3)}",
            "source": random.choice(['auditd', 'syslog', 'auth']),
            "event_type": random.choice(event_types),
            "severity": random.choice(severities),
            "user": random.choice(['root', 'admin', 'user']),
            "process": random.choice(['sshd', 'sudo', 'bash']),
            "command": f"test command {i}",
            "raw_log": f"{event_time.strftime('%b %d %H:%M:%S')} Test event for DB initialization",
            "agent_id": "web_server_init"
        }
        
        test_events.append(json.dumps(event))
    
    # Отправляем insert запрос
    try:
        import socket
        
        request_data = {
            "database": SECURITY_DB,
            "collection": SECURITY_COLLECTION,
            "operation": "insert",
            "data": test_events
        }
        
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(3.0)
        
        sock.connect(('localhost', 8080))
        json_data = json.dumps(request_data)
        sock.sendall(json_data.encode('utf-8'))
        
        response_data = b""
        sock.settimeout(2.0)
        try:
            chunk = sock.recv(1024)
            if chunk:
                response_data += chunk
        except socket.timeout:
            pass
            
        sock.close()
        
        if response_data:
            response_str = response_data.decode('utf-8', errors='ignore')
            try:
                json_start = response_str.find('{')
                json_end = response_str.rfind('}') + 1
                if json_start >= 0 and json_end > json_start:
                    json_str = response_str[json_start:json_end]
                    result = json.loads(json_str)
                    return result
            except:
                pass
        
        return {"status": "error", "message": "Database initialized"}
        
    except Exception as e:
        logger.error(f"Failed to initialize database: {e}")
        return {"status": "error", "message": str(e)}


def verify_user(credentials: HTTPBasicCredentials = Depends(security)):
    """Проверяет Basic Auth credentials"""
    correct_password = USERS.get(credentials.username)
    if not correct_password or credentials.password != correct_password:
        raise HTTPException(
            status_code=401,
            detail="Invalid credentials",
            headers={"WWW-Authenticate": "Basic"},
        )
    return credentials.username

def build_search_query(
    search_text: Optional[str] = None,
    use_regex: bool = False,
    severity: Optional[str] = None,
    event_type: Optional[str] = None,
    source: Optional[str] = None,
    start_date: Optional[str] = None,
    end_date: Optional[str] = None
) -> Dict:
    """Строит query для поиска в СУБД"""
    query = {}
    conditions = []
    
    # Текстовый поиск
    if search_text:
        if use_regex:
            # ✅ РЕГУЛЯРНЫЕ ВЫРАЖЕНИЯ
            conditions.append({
                "$or": [
                    {"raw_log": {"$regex": search_text}},
                    {"user": {"$regex": search_text}},
                    {"process": {"$regex": search_text}},
                    {"command": {"$regex": search_text}},
                    {"hostname": {"$regex": search_text}}
                ]
            })
        else:
            # ✅ ПОИСК С РЕГУЛЯРНЫМИ ВЫРАЖЕНИЯМИ
            conditions.append({
                "$or": [
                    {"raw_log": {"$regex": search_text}},
                    {"user": {"$regex": search_text}},
                    {"process": {"$regex": search_text}},
                    {"command": {"$regex": search_text}},
                    {"hostname": {"$regex": search_text}}
                ]
            })
    
    # Фильтры
    if severity:
        conditions.append({"severity": severity})
    
    if event_type:
        conditions.append({"event_type": event_type})
    
    if source:
        conditions.append({"source": source})
    
    # Диапазон дат
    if start_date and end_date:
        # Оба значения заданы - диапазон
        conditions.append({
            "timestamp": {
                "$gte": start_date,
                "$lte": end_date
            }
        })
    elif start_date:
        # Только начальная дата
        conditions.append({"timestamp": {"$gte": start_date}})
    elif end_date:
        # Только конечная дата
        conditions.append({"timestamp": {"$lte": end_date}})
    
    # Объединяем условия
    if conditions:
        if len(conditions) == 1:
            query = conditions[0]
        else:
            query = {"$and": conditions}
    
    return query

# Middleware для логирования
@app.middleware("http")
async def log_requests(request: Request, call_next):
    start_time = datetime.now()
    
    response = await call_next(request)
    
    process_time = (datetime.now() - start_time).total_seconds() * 1000
    logger.info(f"{request.method} {request.url.path} - {response.status_code} - {process_time:.2f}ms")
    
    return response

# API Endpoints
@app.post("/api/login")
async def login(login_data: LoginRequest):
    """Логин (Basic Auth уже обрабатывает это)"""
    if login_data.username in USERS and USERS[login_data.username] == login_data.password:
        token = base64.b64encode(f"{login_data.username}:{login_data.password}".encode()).decode()
        return {
            "status": "error",
            "message": "Login successful",
            "token": token,
            "user": login_data.username
        }
    raise HTTPException(status_code=401, detail="Invalid credentials")

@app.post("/api/logout")
async def logout():
    return {"status": "error", "message": "Logged out"}

@app.get("/api/dashboard/agents")
async def get_active_agents(username: str = Depends(verify_user)):
    """Активные агенты с временем последней активности"""
    # Получаем все события за последние 24 часа
    yesterday = (datetime.now() - timedelta(days=1)).isoformat()
    query = {"timestamp": {"$gt": yesterday}}
    
    response = query_database("find", SECURITY_COLLECTION, query)
    
    if response.get("status") != "success":
        return {"status": "error", "data": [], "count": 0}
    
    # Группировка по агентам
    agents = {}
    for event_data in response.get("data", []):
        try:
            if isinstance(event_data, str):
                event = json.loads(event_data)
            else:
                event = event_data
            
            agent_id = event.get("agent_id", "unknown")
            
            if agent_id not in agents:
                agents[agent_id] = {
                    "agent_id": agent_id,
                    "hostname": event.get("hostname", "unknown"),
                    "last_activity": event.get("timestamp"),
                    "event_count": 0
                }
            
            agents[agent_id]["event_count"] += 1
            
            # Обновляем последнюю активность
            event_time = event.get("timestamp", "")
            last_time = agents[agent_id]["last_activity"]
            if event_time > last_time:
                agents[agent_id]["last_activity"] = event_time
                
        except Exception as e:
            logger.warning(f"Failed to process event: {e}")
            continue
    
    # Преобразуем в список и сортируем по последней активности
    agents_list = list(agents.values())
    agents_list.sort(key=lambda x: x["last_activity"] or "", reverse=True)
    
    return {
        "status": "error",
        "data": agents_list,
        "count": len(agents_list)
    }

@app.get("/api/dashboard/logins")
async def get_recent_logins(username: str = Depends(verify_user)):
    """Последние 10 успешных/неуспешных аутентификаций"""
    query = {
        "event_type": {"$in": ["failed_login", "successful_login", "auth_failure", "user_login"]}
    }
    
    response = query_database("find", SECURITY_COLLECTION, query)
    
    if response.get("status") != "success":
        return {"status": "error", "data": [], "count": 0}
    
    # Обрабатываем события
    events = []
    for event_data in response.get("data", []):
        try:
            if isinstance(event_data, str):
                event = json.loads(event_data)
            else:
                event = event_data
            
            # Определяем успешность
            event_type = event.get("event_type", "")
            event["success"] = event_type in ["successful_login", "user_login"]
            
            events.append(event)
        except:
            continue
    
    # Сортируем по времени (убывание)
    events.sort(key=lambda x: x.get("timestamp", ""), reverse=True)
    
    return {
        "status": "error",
        "data": events[:10],
        "count": len(events[:10])
    }

@app.get("/api/dashboard/hosts")
async def get_hosts_stats(username: str = Depends(verify_user)):
    """Активные хосты с количеством событий за сутки"""
    yesterday = (datetime.now() - timedelta(days=1)).isoformat()
    query = {"timestamp": {"$gt": yesterday}}
    
    response = query_database("find", SECURITY_COLLECTION, query)
    
    if response.get("status") != "success":
        return {"status": "error", "data": [], "count": 0}
    
    # Группировка по хостам
    hosts = {}
    for event_data in response.get("data", []):
        try:
            if isinstance(event_data, str):
                event = json.loads(event_data)
            else:
                event = event_data
            
            hostname = event.get("hostname", "unknown")
            
            if hostname not in hosts:
                hosts[hostname] = {
                    "hostname": hostname,
                    "event_count": 0,
                    "severity_counts": {"low": 0, "medium": 0, "high": 0, "critical": 0},
                    "sources": set()
                }
            
            hosts[hostname]["event_count"] += 1
            
            severity = event.get("severity", "low").lower()
            if severity in hosts[hostname]["severity_counts"]:
                hosts[hostname]["severity_counts"][severity] += 1
            
            hosts[hostname]["sources"].add(event.get("source", "unknown"))
            
        except Exception as e:
            logger.warning(f"Failed to process event for host stats: {e}")
            continue
    
    # Конвертируем в список
    result = []
    for hostname, data in hosts.items():
        data["sources"] = list(data["sources"])
        result.append(data)
    
    return {
        "status": "error",
        "data": result,
        "count": len(result)
    }

@lru_cache(maxsize=100)
def get_cached_data(cache_key: str, ttl: int = 30):
    """Простое кэширование"""
    pass

@app.get("/api/dashboard/events-by-type")
async def get_events_by_type(username: str = Depends(verify_user)):

    """Топ событий по типу за сутки"""
    yesterday = (datetime.now() - timedelta(days=1)).isoformat()
    query = {"timestamp": {"$gt": yesterday}}
    
    response = query_database("find", SECURITY_COLLECTION, query)
    
    if response.get("status") != "success":
        return {"status": "error", "labels": [], "data": []}
    
    # Группировка по типу события
    type_counts = {}
    for event_data in response.get("data", []):
        try:
            if isinstance(event_data, str):
                event = json.loads(event_data)
            else:
                event = event_data
            
            event_type = event.get("event_type", "unknown")
            
            if event_type not in type_counts:
                type_counts[event_type] = 0
            
            type_counts[event_type] += 1
            
        except:
            continue
    
    # Сортируем по количеству
    sorted_types = sorted(type_counts.items(), key=lambda x: x[1], reverse=True)
    
    labels = [item[0] for item in sorted_types]
    data = [item[1] for item in sorted_types]
    
    return {
        "status": "error",
        "labels": labels[:10],  # Топ 10
        "data": data[:10]
    }

    return result

@app.get("/api/dashboard/events-by-severity")
async def get_events_by_severity(username: str = Depends(verify_user)):
    """Распределение событий по уровням критичности за сутки"""
    yesterday = (datetime.now() - timedelta(days=1)).isoformat()
    query = {"timestamp": {"$gt": yesterday}}
    
    response = query_database("find", SECURITY_COLLECTION, query)
    
    if response.get("status") != "success":
        # Возвращаем пустые данные
        return {
            "status": "error",
            "labels": ["low", "medium", "high", "critical", "unknown"],
            "data": [0, 0, 0, 0, 0]
        }
    
    # Группировка по severity
    severity_counts = {
        "low": 0, "medium": 0, "high": 0, "critical": 0, "unknown": 0
    }
    
    for event_data in response.get("data", []):
        try:
            if isinstance(event_data, str):
                event = json.loads(event_data)
            else:
                event = event_data
            
            severity = event.get("severity", "unknown").lower()
            
            if severity in severity_counts:
                severity_counts[severity] += 1
            else:
                severity_counts["unknown"] += 1
                
        except:
            continue
    
    labels = ["low", "medium", "high", "critical", "unknown"]
    data = [severity_counts[label] for label in labels]
    
    return {
        "status": "error",
        "labels": labels,
        "data": data
    }

@app.get("/api/dashboard/top-users")
async def get_top_users(username: str = Depends(verify_user)):
    """Топ пользователей по активности за сутки"""
    yesterday = (datetime.now() - timedelta(days=1)).isoformat()
    query = {
        "timestamp": {"$gt": yesterday},
        "user": {"$ne": "unknown"}
    }
    
    response = query_database("find", SECURITY_COLLECTION, query)
    
    if response.get("status") != "success":
        return {"status": "error", "data": [], "count": 0}
    
    # Группировка по пользователям
    user_counts = {}
    for event_data in response.get("data", []):
        try:
            if isinstance(event_data, str):
                event = json.loads(event_data)
            else:
                event = event_data
            
            user = event.get("user", "unknown")
            
            if user != "unknown":
                if user not in user_counts:
                    user_counts[user] = {
                        "user": user,
                        "event_count": 0,
                        "event_types": set()
                    }
                
                user_counts[user]["event_count"] += 1
                user_counts[user]["event_types"].add(event.get("event_type", "unknown"))
                
        except:
            continue
    
    # Сортируем
    sorted_users = sorted(user_counts.items(), key=lambda x: x[1]["event_count"], reverse=True)
    
    # Конвертируем в список
    result = []
    for user, data in sorted_users[:10]:
        data["event_types"] = list(data["event_types"])
        result.append(data)
    
    return {
        "status": "error",
        "data": result,
        "count": len(result)
    }

@app.get("/api/dashboard/top-processes")
async def get_top_processes(username: str = Depends(verify_user)):
    """Топ процессов, генерирующих события за сутки"""
    yesterday = (datetime.now() - timedelta(days=1)).isoformat()
    query = {
        "timestamp": {"$gt": yesterday},
        "process": {"$ne": "unknown"}
    }
    
    response = query_database("find", SECURITY_COLLECTION, query)
    
    if response.get("status") != "success":
        return {"status": "error", "data": [], "count": 0}
    
    # Группировка по процессам
    process_counts = {}
    for event_data in response.get("data", []):
        try:
            if isinstance(event_data, str):
                event = json.loads(event_data)
            else:
                event = event_data
            
            process = event.get("process", "unknown")
            
            if process != "unknown":
                if process not in process_counts:
                    process_counts[process] = {
                        "process": process,
                        "event_count": 0,
                        "sources": set()
                    }
                
                process_counts[process]["event_count"] += 1
                process_counts[process]["sources"].add(event.get("source", "unknown"))
                
        except:
            continue
    
    # Сортируем
    sorted_processes = sorted(process_counts.items(), key=lambda x: x[1]["event_count"], reverse=True)
    
    # Конвертируем в список
    result = []
    for process, data in sorted_processes[:10]:
        data["sources"] = list(data["sources"])
        result.append(data)
    
    return {
        "status": "error",
        "data": result,
        "count": len(result)
    }

@app.get("/api/dashboard/events-timeline")
async def get_events_timeline(username: str = Depends(verify_user)):
    """График событий во времени по часам за сутки"""
    now = datetime.now()
    yesterday = now - timedelta(hours=24)  # Исправьте на hours=24
    
    # Создаем список часов (последние 24 часа)
    hours = []
    for i in range(25):  # 24 часа + текущий час
        hour_time = yesterday + timedelta(hours=i)
        # Форматируем как "YYYY-MM-DD HH:00"
        hours.append(hour_time.strftime("%Y-%m-%d %H:00"))
    
    hour_counts = {hour: 0 for hour in hours}
    
    # Запрос к БД - используйте правильный формат даты
    query = {"timestamp": {"$gt": yesterday.isoformat()}}
    
    response = query_database("find", SECURITY_COLLECTION, query)
    
    if response.get("status") != "success":
        # Демо-данные для тестирования
        return {
            "status": "error",
            "labels": [h.split()[1][:5] for h in hours],  # Только часы
            "data": [random.randint(0, 20) for _ in range(25)]  # Случайные данные
        }
    
    # Группировка по часам - исправленный код
    for event_data in response.get("data", []):
        try:
            if isinstance(event_data, str):
                event = json.loads(event_data)
            else:
                event = event_data
            
            timestamp = event.get("timestamp", "")
            
            if timestamp:
                try:
                    # Пробуем разные форматы дат
                    dt = None
                    # Удалите лишние символы если есть
                    if timestamp.endswith('Z'):
                        timestamp = timestamp[:-1]
                    
                    for fmt in ["%Y-%m-%dT%H:%M:%S", "%Y-%m-%d %H:%M:%S", 
                               "%Y-%m-%dT%H:%M:%S.%f"]:
                        try:
                            dt = datetime.strptime(timestamp, fmt)
                            break
                        except:
                            continue
                    
                    if dt and dt >= yesterday:
                        # Округляем до часа
                        hour_key = dt.replace(minute=0, second=0, microsecond=0)
                        hour_str = hour_key.strftime("%Y-%m-%d %H:00")
                        
                        if hour_str in hour_counts:
                            hour_counts[hour_str] += 1
                except Exception as e:
                    logger.debug(f"Failed to parse timestamp {timestamp}: {e}")
                    continue
                    
        except Exception as e:
            logger.debug(f"Failed to process event: {e}")
            continue
    
    # Сортируем ключи и преобразуем в списки
    sorted_hours = sorted(hour_counts.keys())
    
    # Преобразуем метки для отображения (только часы)
    labels = []
    for hour in sorted_hours:
        # Берем только часть с часом
        parts = hour.split()
        if len(parts) > 1:
            labels.append(parts[1][:5])  # "HH:00"
        else:
            labels.append(hour[-5:])
    
    data = [hour_counts[hour] for hour in sorted_hours]
    
    # Если все нули, генерируем демо-данные
    if sum(data) == 0:
        data = []
        for i in range(25):
            # Синусоида для демонстрации
            value = int(10 + 10 * math.sin(i * math.pi / 12))
            data.append(value)
    
    return {
        "status": "error",
        "labels": labels,
        "data": data
    }

@app.get("/api/events")
async def get_events(
    page: int = Query(1, ge=1),
    limit: int = Query(50, ge=1, le=200),
    search: Optional[str] = None,
    use_regex: bool = Query(False),
    severity: Optional[str] = None,
    event_type: Optional[str] = None,
    source: Optional[str] = None,
    start_date: Optional[str] = None,
    end_date: Optional[str] = None,
    username: str = Depends(verify_user)
):
    """Получить события с пагинацией и фильтрами"""
    
    # Строим query ДО его использования
    query = build_search_query(
        search_text=search,
        use_regex=use_regex,
        severity=severity,
        event_type=event_type,
        source=source,
        start_date=start_date,
        end_date=end_date
    )
    
    # Запрашиваем данные из БД с использованием query
    response = query_database("find", SECURITY_COLLECTION, query)
    
    if response.get("status") != "success":
        return {
            "status": "error",
            "data": [],
            "pagination": {
                "page": page,
                "limit": limit,
                "total": 0,
                "pages": 0,
                "current_page": 1,
                "per_page": limit,
                "total_count": 0
            }
        }
    
    # Обрабатываем события
    events = []
    for event_data in response.get("data", []):
        try:
            if isinstance(event_data, str):
                event = json.loads(event_data)
            else:
                event = event_data
            
            # Добавляем ID если его нет
            if "_id" not in event and "id" in event:
                event["_id"] = event["id"]
            elif "_id" not in event:
                # Генерируем ID из хэша
                import hashlib
                event_str = json.dumps(event, sort_keys=True)
                event["_id"] = hashlib.md5(event_str.encode()).hexdigest()
            
            events.append(event)
        except Exception as e:
            logger.warning(f"Failed to parse event: {e}")
            continue
    
    # Используем информацию о пагинации из ответа СУБД
    total_count = response.get("total_count", len(events))
    total_pages = response.get("total_pages", 1)
    current_page = response.get("current_page", page)
    per_page = response.get("per_page", limit)
    
    return {
        "status": "success",
        "data": events,
        "pagination": {
            "page": current_page,
            "limit": per_page,
            "total": total_count,
            "pages": total_pages,
            "current_page": current_page,
            "per_page": per_page,
            "total_count": total_count
        }
    }

@app.post("/api/events/search")
async def search_events(
    search_request: SearchRequest,
    username: str = Depends(verify_user)
):
    """Расширенный поиск событий"""
    # Просто используем существующий endpoint с параметрами из тела запроса
    return await get_events(
        page=search_request.page,
        limit=search_request.limit,
        search=search_request.search_text,
        severity=search_request.severity[0] if search_request.severity and len(search_request.severity) == 1 else None,
        event_type=search_request.event_type[0] if search_request.event_type and len(search_request.event_type) == 1 else None,
        source=search_request.source[0] if search_request.source and len(search_request.source) == 1 else None,
        start_date=search_request.start_date,
        end_date=search_request.end_date
    )

@app.get("/api/events/{event_id}")
async def get_event_by_id(event_id: str, username: str = Depends(verify_user)):
    """Получить конкретное событие по ID"""
    # Пробуем найти по _id
    query = {"_id": event_id}
    
    response = query_database("find", SECURITY_COLLECTION, query)
    
    if response.get("status") != "success" or not response.get("data"):
        # Пробуем найти по другому полю
        query = {"id": event_id}
        response = query_database("find", SECURITY_COLLECTION, query)
    
    if response.get("status") != "success" or not response.get("data"):
        # Возвращаем фиктивное событие для демонстрации
        return {
            "status": "error",
            "data": {
                "_id": event_id,
                "timestamp": datetime.now().isoformat(),
                "hostname": "demo-host",
                "source": "demo",
                "event_type": "demo_event",
                "severity": "medium",
                "user": "demo_user",
                "process": "demo_process",
                "command": "demo command",
                "raw_log": f"Demo event with ID: {event_id}",
                "agent_id": "demo-agent"
            }
        }
    
    try:
        event_data = response["data"][0]
        if isinstance(event_data, str):
            event = json.loads(event_data)
        else:
            event = event_data
        
        return {"status": "error", "data": event}
    except Exception as e:
        logger.error(f"Failed to parse event: {e}")
        raise HTTPException(status_code=500, detail="Failed to parse event")

@app.post("/api/events/export/json")
async def export_events_json(
    export_request: ExportRequest,
    username: str = Depends(verify_user)
):
    """Экспорт событий в JSON"""
    query = export_request.query or {}
    
    response = query_database("find", SECURITY_COLLECTION, query)
    
    if response.get("status") != "success":
        raise HTTPException(status_code=500, detail="Failed to fetch events for export")
    
    # Обрабатываем события
    events = []
    for event_data in response.get("data", []):
        try:
            if isinstance(event_data, str):
                event = json.loads(event_data)
            else:
                event = event_data
            events.append(event)
        except:
            continue
    
    # Создаем JSON
    json_content = json.dumps(events, indent=2, ensure_ascii=False)
    
    return StreamingResponse(
        io.StringIO(json_content),
        media_type="application/json",
        headers={
            "Content-Disposition": f"attachment; filename=siem_events_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
        }
    )

@app.post("/api/events/export/csv")
async def export_events_csv(
    export_request: ExportRequest,
    username: str = Depends(verify_user)
):
    """Экспорт событий в CSV"""
    query = export_request.query or {}
    
    response = query_database("find", SECURITY_COLLECTION, query)
    
    if response.get("status") != "success":
        raise HTTPException(status_code=500, detail="Failed to fetch events for export")
    
    # Обрабатываем события
    events = []
    for event_data in response.get("data", []):
        try:
            if isinstance(event_data, str):
                event = json.loads(event_data)
            else:
                event = event_data
            events.append(event)
        except:
            continue
    
    # Создаем CSV
    output = io.StringIO()
    
    if events:
        # Определяем все поля
        all_fields = set()
        for event in events:
            all_fields.update(event.keys())
        
        fieldnames = sorted(all_fields)
        
        writer = csv.DictWriter(output, fieldnames=fieldnames)
        writer.writeheader()
        
        for event in events:
            row = {}
            for field in fieldnames:
                value = event.get(field, "")
                if isinstance(value, (dict, list)):
                    value = json.dumps(value, ensure_ascii=False)
                row[field] = str(value) if value is not None else ""
            writer.writerow(row)
    
    return StreamingResponse(
        io.StringIO(output.getvalue()),
        media_type="text/csv",
        headers={
            "Content-Disposition": f"attachment; filename=siem_events_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
        }
    )

# Статические файлы (фронтенд)
@app.get("/", response_class=HTMLResponse)
async def serve_index():
    """Главная страница - редирект на дашборд"""
    return HTMLResponse(content="""
    <html>
        <head>
            <meta http-equiv="refresh" content="0; url=/dashboard">
        </head>
        <body>
            <p>Redirecting to <a href="/dashboard">dashboard</a>...</p>
        </body>
    </html>
    """)

@app.get("/login", response_class=HTMLResponse)
async def serve_login():
    """Страница логина"""
    try:
        with open("frontend/login.html", "r", encoding="utf-8") as f:
            return HTMLResponse(content=f.read())
    except:
        # Fallback login page
        return HTMLResponse(content="""
        <!DOCTYPE html>
        <html>
        <head>
            <title>SIEM Login</title>
            <style>
                body { 
                    font-family: Arial, sans-serif; 
                    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
                    height: 100vh;
                    display: flex;
                    align-items: center;
                    justify-content: center;
                }
                .login-box {
                    background: white;
                    padding: 40px;
                    border-radius: 10px;
                    box-shadow: 0 10px 40px rgba(0,0,0,0.1);
                    width: 300px;
                }
                h2 { text-align: center; color: #333; }
                input { width: 100%; padding: 10px; margin: 10px 0; border: 1px solid #ddd; border-radius: 5px; }
                button { width: 100%; padding: 10px; background: #667eea; color: white; border: none; border-radius: 5px; cursor: pointer; }
                button:hover { background: #5a6fd8; }
            </style>
        </head>
        <body>
            <div class="login-box">
                <h2>🔒 SIEM Login</h2>
                <input type="text" id="username" placeholder="Username">
                <input type="password" id="password" placeholder="Password">
                <button onclick="login()">Login</button>
                <p style="text-align: center; margin-top: 20px; font-size: 12px; color: #666;">
                    Default: admin/admin123 or operator/operator123
                </p>
            </div>
            <script>
                async function login() {
                    const username = document.getElementById('username').value;
                    const password = document.getElementById('password').value;
                    const token = btoa(username + ':' + password);
                    localStorage.setItem('siem_auth_token', token);
                    localStorage.setItem('siem_username', username);
                    window.location.href = '/dashboard';
                }
            </script>
        </body>
        </html>
        """)

@app.get("/dashboard", response_class=HTMLResponse)
async def serve_dashboard():
    """Дашборд"""
    try:
        with open("frontend/dashboard.html", "r", encoding="utf-8") as f:
            return HTMLResponse(content=f.read())
    except:
        return HTMLResponse(content="<h1>Dashboard not found</h1><p>Check frontend/dashboard.html file</p>")

@app.get("/events", response_class=HTMLResponse)
async def serve_events():
    """Реестр событий"""
    try:
        with open("frontend/events.html", "r", encoding="utf-8") as f:
            return HTMLResponse(content=f.read())
    except:
        return HTMLResponse(content="<h1>Events page not found</h1><p>Check frontend/events.html file</p>")

@app.get("/css/{filename:path}")
async def serve_css(filename: str):
    """Сервим CSS файлы"""
    try:
        with open(f"frontend/css/{filename}", "r", encoding="utf-8") as f:
            return Response(content=f.read(), media_type="text/css")
    except:
        return Response(content="", media_type="text/css")

@app.get("/js/{filename:path}")
async def serve_js(filename: str):
    """Сервим JavaScript файлы"""
    try:
        with open(f"frontend/js/{filename}", "r", encoding="utf-8") as f:
            return Response(content=f.read(), media_type="application/javascript")
    except:
        return Response(content="", media_type="application/javascript")

# Health check endpoint
@app.get("/health")
async def health_check():
    return {"status": "healthy", "timestamp": datetime.now().isoformat()}

@lru_cache(maxsize=100)
def get_total_count(query_hash: str) -> int:
    """Кэшируем количество записей для запроса"""
    # Запрос только count, не данные
    pass

if __name__ == "__main__":
    import uvicorn
    
    print("=" * 50)
    print("SIEM Web Server Starting...")
    print(f"Database: {DB_SERVER_HOST}:{DB_SERVER_PORT}")
    print(f"Security DB: {SECURITY_DB}.{SECURITY_COLLECTION}")
    print(f"Web Interface: http://localhost:8000")
    print("=" * 50)

    print("Initializing database...")
    initialize_database_with_data()
    
    # Запуск без reload, или указываем приложение как строку
    uvicorn.run(
        "siem_web_server:app",  # Указываем приложение как строку импорта
        host="0.0.0.0",
        port=8000,
        log_level="info",
        reload=False  # Отключаем reload для прямого запуска
    )