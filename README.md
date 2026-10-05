# Mini-SNMP - Trabalho Prático

Implementação em C de um sistema simplificado de gerenciamento e monitoramento de redes, inspirado no protocolo SNMP (Simple Network Management Protocol).

O sistema utiliza uma arquitetura **Manager-Agent**, comunicação por **sockets TCP** e um protocolo próprio baseado em mensagens estruturadas.

O projeto foi desenvolvido para demonstrar conceitos de gerenciamento de redes, comunicação cliente-servidor, monitoramento periódico, detecção de falhas, configuração remota, geração de alertas, escalabilidade, múltiplos gerentes e comunicação segura utilizando TLS.

---

# 1. Funcionalidades

O projeto implementa as seguintes funcionalidades:

- Arquitetura Manager-Agent
- Comunicação via TCP sockets
- Protocolo próprio baseado em mensagens
- Autenticação por token
- Mensagens `AUTH`
- Mensagens `AUTH_OK`
- Mensagens `GET`
- Mensagens `RESPONSE`
- Mensagens `ERROR`
- Mensagens `SET`
- Mensagens `SET_OK`
- Mensagens `PING`
- Mensagens `PONG`
- Mensagens `TRAP`
- MIB simplificada
- Monitoramento de CPU
- Monitoramento de memória
- Monitoramento de uptime
- Monitoramento de conexões TCP ativas
- Monitoramento de tráfego de rede RX/TX
- Monitoramento de pacotes RX/TX
- Monitoramento periódico
- Medição de latência
- Detecção de Agents `OFFLINE`
- Detecção de `TIMEOUT`
- Histórico das métricas em CSV
- Configuração remota de thresholds
- Traps para CPU
- Traps para memória
- Suporte a múltiplos Agents
- Suporte a múltiplos Managers
- Dashboard web
- Autenticação
- Comunicação protegida por TLS
- Certificados digitais
- Autoridade certificadora (CA) local
- Testes automatizados
- Testes manuais
- Testes de falha
- Testes de timeout
- Testes de escalabilidade
- Testes de overhead
- Testes de múltiplos Managers
- Testes de SET remoto
- Testes de TRAP
- Comparação de comunicação com e sem TLS

---

# 2. Arquitetura

O sistema utiliza uma arquitetura baseada em **Manager-Agent**.

O Manager é responsável pelo gerenciamento e monitoramento dos Agents.

Os Agents são responsáveis por:

- coletar informações do sistema local;
- responder às requisições do Manager;
- armazenar os thresholds de monitoramento;
- detectar condições de alerta;
- enviar TRAPs quando os thresholds são ultrapassados.

A comunicação ocorre utilizando sockets TCP.

A arquitetura pode ser representada da seguinte forma:

```text
                    +------------------+
                    |     Manager      |
                    |                  |
                    | Monitoramento    |
                    | GET / SET        |
                    | PING / PONG      |
                    | Recepção TRAP    |
                    +--------+---------+
                             |
                       TCP / TLS
                             |
              +--------------+--------------+
              |              |              |
              v              v              v
        +-----------+  +-----------+  +-----------+
        |  Agent 1  |  |  Agent 2  |  |  Agent 3  |
        | Port 5001 |  | Port 5002 |  | Port 5003 |
        +-----------+  +-----------+  +-----------+
