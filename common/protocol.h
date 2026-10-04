#ifndef PROTOCOL_H
#define PROTOCOL_H

#define DEFAULT_AGENT_PORT 5001
#define DEFAULT_TRAP_PORT 6000
#define BUFFER_SIZE 1024
#define SOCKET_TIMEOUT_SEC 2
#define AUTH_TOKEN "miniSNMP2026"
#define TLS_DEFAULT 0

#define MSG_AUTH      "AUTH"
#define MSG_AUTH_OK   "AUTH_OK"
#define MSG_GET       "GET"
#define MSG_RESPONSE  "RESPONSE"
#define MSG_ERROR     "ERROR"
#define MSG_SET       "SET"
#define MSG_SET_OK    "SET_OK"
#define MSG_PING      "PING"
#define MSG_PONG      "PONG"
#define MSG_TRAP      "TRAP"

#endif
