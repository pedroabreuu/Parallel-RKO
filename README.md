# ParallelRKO

## Requisitos

- GNU Make;
- C++20;
- OpenMP.

## Compilação

```bash
cd Program
make release
```

Alvos disponíveis:

| Alvo | Uso |
|---|---|
| `release` | execução normal otimizada |
| `debug` | depuração sem otimização |
| `clean` | remove os executáveis gerados |

O modo de desempenho usa `-O3 -DNDEBUG -march=native`, C++20 e OpenMP.

## Execução normal

`Program/`:

```bash
mkdir -p ../Results
./runRKO INSTANCE TEMPO_SEGUNDOS [ARQUIVO_CONFIG]
```

Exemplo:

```bash
./runRKO ../Instances/aNpMP/pmed1.txt 60
```

Baseline sequencial com BRKGA, parâmetros offline e seed fixa:

```bash
./runRKO ../Instances/aNpMP/pmed1.txt 60 config/config_baseline.conf
```

As configs ficam em `Program/config/`. Terceiro argumento é opcional. Sem ele, o programa usa `config/config_tests.conf`.

## Solver exato

```bash
./ExactSolver/exact_anpmedian Instances/aNpMP/pmed1.txt 60
```
