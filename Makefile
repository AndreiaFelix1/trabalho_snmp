CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
LDFLAGS = -pthread

BIN_DIR = bin

all: $(BIN_DIR)/agent $(BIN_DIR)/manager

logs:
	mkdir -p logs

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/agent: agent/agent.c agent/metrics.c agent/metrics.h common/protocol.h common/mib.h | $(BIN_DIR)
	$(CC) $(CFLAGS) agent/agent.c agent/metrics.c common/tls.c -o $@ -lssl -lcrypto

$(BIN_DIR)/manager: manager/manager.c common/protocol.h common/mib.h | $(BIN_DIR)
	$(CC) $(CFLAGS) manager/manager.c common/tls.c -o $@ $(LDFLAGS) -lssl -lcrypto

test: all logs
	python3 experiments/run_experiments.py

test-tls: all logs
	python3 experiments/run_experiments.py --tls

clean:
	rm -rf $(BIN_DIR)
	rm -f logs/metrics.csv logs/alerts.log

run-agent1:
	./bin/agent 5001 Agent1 127.0.0.1 6000

run-agent2:
	./bin/agent 5002 Agent2 127.0.0.1 6000

run-agent3:
	./bin/agent 5003 Agent3 127.0.0.1 6000

run-manager:
	./bin/manager manager/agents.conf 5
