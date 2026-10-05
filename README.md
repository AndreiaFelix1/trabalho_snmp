# Mini-SNMP - Trabalho Prático

Implementação em C de um sistema simplificado de gerenciamento e monitoramento de redes, inspirado no protocolo SNMP (Simple Network Management Protocol).

O sistema utiliza uma arquitetura **Manager-Agent**, comunicação por **sockets TCP** e um protocolo próprio baseado em mensagens estruturadas.

---

# 1. Funcionalidades

O projeto implementa:

- Arquitetura Manager-Agent
- Comunicação via TCP sockets
- Protocolo próprio baseado em mensagens
- Autenticação por token
- Mensagens `AUTH`, `GET`, `RESPONSE`, `ERROR`, `SET`, `SET_OK`, `PING`, `PONG` e `TRAP`
- MIB simplificada
- Monitoramento de CPU
- Monitoramento de memória
- Uptime
- Conexões TCP ativas
- Tráfego de rede RX/TX
- Pacotes RX/TX
- Monitoramento periódico
- Medição de latência
- Detecção de Agents `OFFLINE`
- Detecção de `TIMEOUT`
- Histórico das métricas em CSV
- Configuração remota de thresholds
- Traps para CPU e memória
- Suporte a múltiplos Agents
- Suporte a múltiplos Managers
- Dashboard web
- TLS utilizando OpenSSL
- Comparação de desempenho com e sem TLS
- Testes automatizados de protocolo, MIB, falhas, escalabilidade e overhead

---

# 2. Estrutura do projeto

```text
mini-snmp/
├── agent/
│   └── agent.c
├── manager/
│   ├── manager.c
│   └── agents.conf
├── common/
│   ├── protocol.h
│   └── mib.h
├── dashboard/
│   └── server.py
├── experiments/
│   ├── run_experiments.py
│   └── results/
├── logs/
├── certs/
├── bin/
├── generate_certs.sh
└── Makefile
