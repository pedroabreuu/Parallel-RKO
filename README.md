# ParallelRKO

Estudo de desempenho e eficiência energética da paralelização em memória compartilhada (OpenMP) do decoder do RKO/BRKGA para o alpha-neighbor p-median (instâncias pmed1–pmed40).

## Requisitos

- GNU Make, g++ com C++20 e OpenMP;
- Linux com contador RAPL (`/sys/class/powercap/intel-rapl:0`), `perf` e `lm-sensors`;
- Python 3 para a análise.

## Compilação

```bash
cd Program
make release        # -O3 -DNDEBUG -march=native -fopenmp
```

## Execução

```bash
./runRKO INSTANCIA TEMPO_MAX_S [CONFIG]
./runRKO ../Instances/aNpMP/pmed1.txt 60 config/config_baseline.conf
```

Chave `threads` da config: `0` = decoder sequencial (sem OpenMP); `N >= 1` = decoder OpenMP com N threads.

## Reprodução dos experimentos

A cada boot (permissões não persistem):

```bash
sudo chmod a+r /sys/class/powercap/intel-rapl:0/energy_uj
sudo sysctl kernel.perf_event_paranoid=2
```

Em `Program/`:

```bash
./record_environment.sh     # registra hardware, build e ambiente
./run_campaign.sh           # todas as configurações, intercaladas, 10 repetições
```

- `run_campaign.sh`: threads `0 1 2 4 8 12` × `OMP_WAIT_POLICY` `active`/`passive`, trabalho fixo (gerações por instância em `config/generations.txt`), seed fixa, aquecimento, espera por temperatura e medição de potência em repouso. Retoma de onde parou.
- `calibrate_generations.sh`: gera `config/generations.txt` (~60 s por execução sequencial).

Análise (tabelas e figuras em `Results/analysis/`):

```bash
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/python analysis/analyze.py
```

## Solver exato

`ExactSolver/` (Gurobi) fornece os ótimos em `ExactSolver/optimal_results.csv`, usados apenas como referência de qualidade.

## Créditos e licença

Baseado no RKO (Random-Key Optimizer), distribuído sob licença MIT (ver `LICENSE` e `Authors`):

Chaves, A.A., Resende, M.G.C., Schuetz, M.J.A., Brubaker, J.K., Katzgraber, H.G., Arruda, E.F., Silva, R.M.A. A Random-Key Optimizer for Combinatorial Optimization. *Journal of Heuristics*, v. 31, n. 4, p. 32, 2025. https://doi.org/10.1007/s10732-025-09568-z
