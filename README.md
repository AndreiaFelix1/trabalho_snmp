# Mini-SNMP - Trabalho Prático

Implementação em C de um sistema simplificado de gerenciamento de rede inspirado no SNMP.

## Funcionalidades

- Arquitetura Manager-Agent
- TCP sockets
- Protocolo próprio
- GET / RESPONSE / ERROR
- MIB simplificada
- CPU
- Memória
- Uptime
- Conexões TCP ativas
- Tráfego de rede RX/TX
- Pacotes RX/TX
- Monitoramento periódico
- Histórico CSV
- Detecção de OFFLINE/TIMEOUT
- Dashboard no terminal
- Autenticação por token
- Configuração remota de thresholds
- Traps simplificados para CPU/memória
- Suporte natural a múltiplos Agents
- Suporte a múltiplos Managers consultando os mesmos Agents
- Medição de latência

## Compilação

```bash
make
```

## Executar Agents

Abra três terminais:

```bash
./bin/agent 5001 Agent1 127.0.0.1 6000
```

```bash
./bin/agent 5002 Agent2 127.0.0.1 6000
```

```bash
./bin/agent 5003 Agent3 127.0.0.1 6000
```

## Executar Manager

Em outro terminal:

```bash
./bin/manager manager/agents.conf 5
```

O último argumento é o intervalo de monitoramento em segundos.

Para aplicar uma configuração remota:

```bash
./bin/manager manager/agents.conf 5 --set 0 3.1 75
```

## Múltiplos Managers

Mais de um Manager pode consultar simultaneamente os mesmos Agents. Para evitar conflito entre os receptores de traps, cada Manager pode usar uma porta de trap diferente:

```bash
./bin/manager manager/agents.conf 5 --trap-port 6000
./bin/manager manager/agents.conf 5 --trap-port 6001
```

O Agent deve ser configurado para enviar traps para a porta do Manager desejado.

## Autenticação

O token está em:

```text
common/protocol.h
```

Token inicial:

```text
miniSNMP2026
```

## MIB

### Sistema

- 1.1 CPU
- 1.2 Memória
- 1.3 Uptime
- 1.4 Conexões TCP ativas

### Rede

- 2.1 Bytes recebidos
- 2.2 Bytes enviados
- 2.3 Pacotes recebidos
- 2.4 Pacotes enviados

### Configuração

- 3.1 Limite de CPU
- 3.2 Limite de memória

## Configuração remota

O Manager também permite configurar remotamente um Agent pela linha de comando:

```bash
./bin/manager manager/agents.conf 5 --set 0 3.1 80
```

Onde `0` é o índice do Agent no arquivo `agents.conf`.

Os OIDs de configuração são:

```text
SET 3.1 80
SET 3.2 90
```

## Histórico

É criado automaticamente:

```text
logs/metrics.csv
```

## Traps

Os Agents podem enviar:

```text
TRAP <agent> <oid> <valor> <limite>
```

para o Manager quando CPU ou memória ultrapassam o threshold configurado.

Os alertas são registrados em:

```text
logs/alerts.log
```

## Teste de falha

1. Execute os três Agents.
2. Execute o Manager.
3. Observe todos como ONLINE.
4. Encerre um Agent com Ctrl+C.
5. O Manager deverá indicar OFFLINE após a próxima consulta.

## Estrutura

```text
mini-snmp/
├── agent/
├── manager/
├── common/
├── logs/
├── bin/
└── Makefile
```


## Dashboard web

O núcleo do projeto permanece em C. O dashboard web é uma automação auxiliar em Python usando apenas a biblioteca padrão.

Com o Manager em execução, abra outro terminal:

```bash
python3 dashboard/server.py
```

Depois abra no navegador:

```text
http://127.0.0.1:8080
```

O dashboard lê `logs/metrics.csv` e `logs/alerts.log`, atualizando automaticamente a cada 3 segundos.

Não é necessário instalar Flask ou outra biblioteca externa.


## TLS / Criptografia

O projeto possui suporte opcional a TLS 1.2+ usando OpenSSL.

1. Gere os certificados:

```bash
./generate_certs.sh
```

2. Compile novamente:

```bash
make clean
make
```

3. Inicie os Agents com TLS:

```bash
./bin/agent 5001 Agent1 127.0.0.1 6000 --tls
./bin/agent 5002 Agent2 127.0.0.1 6000 --tls
./bin/agent 5003 Agent3 127.0.0.1 6000 --tls
```

4. Inicie o Manager com TLS:

```bash
./bin/manager manager/agents.conf 5 --tls
```

O TLS usa uma CA local para autenticação dos certificados. O Manager e os Agents possuem certificados próprios e verificam a identidade do par, protegendo a comunicação de gerenciamento e os traps.

## Experimentos automatizados

O projeto inclui uma pequena automação Python apenas para executar os testes e coletar resultados. O sistema Manager/Agent continua sendo implementado em C.

Teste completo sem TLS:

```bash
make test
```

Teste completo com TLS:

```bash
./generate_certs.sh
make test-tls
```

Os resultados são gravados em `experiments/results/`. Os cenários incluem protocolo, MIB, SET remoto, traps, overhead, escalabilidade com 1/3/5/10 Agents, falha, dashboard, múltiplos Managers e comparação TLS.
# trabalho_snmp
