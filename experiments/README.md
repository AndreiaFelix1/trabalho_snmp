# Experimentos Mini-SNMP

`run_experiments.py` é apenas uma automação auxiliar. O sistema avaliado continua sendo implementado em C.

## Pré-requisitos

Na raiz do projeto:

```bash
make
```

Para testes TLS, os certificados precisam existir:

```bash
./generate_certs.sh
make
```

## Execução sem TLS

```bash
python3 experiments/run_experiments.py
```

## Execução com TLS

```bash
python3 experiments/run_experiments.py --tls
```

Por padrão são avaliados 1, 3, 5 e 10 Agents. Para o experimento ampliado de escalabilidade, use 1, 3, 5, 10, 20, 50 e 100 Agents. Para uma execução mais longa:

```bash
python3 experiments/run_experiments.py --tls --duration 10
```

Os resultados ficam em `experiments/results/`.

## Cenários

- protocolo: autenticação, PING/PONG e OID inválido;
- overhead: custo das mensagens no nível da aplicação;
- escalabilidade: 1, 3, 5 e 10 Agents;
- falha: parada do Agent 2 e medição do tempo até o Manager registrar a indisponibilidade;
- dashboard: verificação da API HTTP;
- múltiplos gerentes: dois Managers consultando simultaneamente os mesmos Agents;
- TLS: comparação da latência com e sem TLS.
- configuração remota: `SET` de limiares;
- traps: geração e recebimento de alerta por limiar.

O overhead medido pelo script é **somente no nível da aplicação**. Ele não representa os bytes adicionais dos cabeçalhos TCP/IP nem dos registros TLS.


### Teste de escalabilidade com muitos Agents

O Manager suporta até 128 Agents por arquivo de configuração. Para testar apenas escalabilidade, sem executar os demais testes:

```bash
python3 experiments/run_experiments.py --only-scalability --counts 50,100 --duration 60
```

O parâmetro `--duration` define quantos segundos o cenário permanece ativo após a primeira coleta. Com 50/100 Agents, recomenda-se um valor maior, pois o Manager realiza as consultas sequencialmente.


## Comparação de escalabilidade com e sem TLS

Para executar exatamente os mesmos cenários sem TLS e com TLS e gerar uma tabela comparativa: 

```bash
python3 experiments/run_experiments.py --compare-tls --counts 1,3,5,10,20,50,100 --duration 60
```

São gerados:

- `experiments/results/results_plain.csv`
- `experiments/results/results_tls.csv`
- `experiments/results/scalability_tls_comparison.csv`

O último arquivo compara, para cada quantidade de Agents, a taxa de sucesso, a latência média e a diferença percentual entre TLS e comunicação sem TLS.

A janela inicial de subida dos Agents é desconsiderada da métrica de escalabilidade, evitando que uma falha transitória durante a inicialização seja confundida com falha de monitoramento.
