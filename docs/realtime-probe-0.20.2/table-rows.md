# Real-device callback observations: 48 kHz / 256 samples

Formal captures only: 6 cases × 3 rounds, each 3 s requested warmup and 60 s measured. The per-block deadline is 5.333333 ms. Values below are milliseconds. Percentiles use nearest-rank; warmup is excluded from callback timings. Measurement consistency does not establish real-time reliability or absence of dropouts.

Native output xrun counters are unsupported (-1); their absence is unknown, not zero. Manager estimates include warmup, outer-manager work and a short detach margin. They are not driver xrun counts and need not equal measured over-budget counts. Short captures in short/ are excluded from all tables below.

## Three-round case summary

| Case | Process mean range | Max process p99 | Max process | Body mean range | Max body p99 | Max body | Process > deadline (total) | Body > deadline (total) | Manager estimate (total) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| baseline | 0.553660–0.555464 | 0.809800 | 1.542500 | 0.554837–0.556645 | 0.811200 | 1.546100 | 0 | 0 | 0 |
| unison | 0.968280–0.986786 | 1.669900 | 2.917400 | 0.969461–0.988037 | 1.672100 | 2.921000 | 0 | 0 | 0 |
| matrix | 2.972829–3.006438 | 3.603600 | 8.390000 | 2.974012–3.007632 | 3.604900 | 8.393300 | 27 | 27 | 28 |
| chaos | 3.287423–3.298568 | 3.944700 | 7.225700 | 3.288695–3.299775 | 3.946000 | 7.229000 | 40 | 40 | 40 |
| chaos-rossler | 3.246762–3.256377 | 3.831400 | 7.182700 | 3.247967–3.257563 | 3.832500 | 7.186400 | 58 | 58 | 58 |
| granular | 3.726528–3.747002 | 4.667700 | 9.470300 | 3.727722–3.748194 | 4.669000 | 9.473500 | 81 | 81 | 85 |

## All 18 runs: processBlock

| Round | Case | Mean | p50 | p95 | p99 | Max | Process > deadline | Body > deadline | Manager estimate |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | baseline | 0.555464 | 0.547600 | 0.596000 | 0.809800 | 1.496500 | 0 | 0 | 0 |
| 1 | unison | 0.974901 | 0.960900 | 1.040400 | 1.275700 | 2.595000 | 0 | 0 | 0 |
| 1 | matrix | 2.972829 | 2.946900 | 3.107200 | 3.540600 | 8.272100 | 9 | 9 | 9 |
| 1 | chaos | 3.290051 | 3.259800 | 3.421300 | 3.944700 | 7.195200 | 14 | 14 | 14 |
| 1 | chaos-rossler | 3.249616 | 3.221900 | 3.343100 | 3.800900 | 7.146000 | 15 | 15 | 15 |
| 1 | granular | 3.726528 | 3.676200 | 3.963100 | 4.635400 | 9.468100 | 36 | 36 | 36 |
| 2 | unison | 0.968280 | 0.955500 | 1.023900 | 1.245200 | 2.828400 | 0 | 0 | 0 |
| 2 | matrix | 2.985522 | 2.961500 | 3.105500 | 3.520800 | 8.276300 | 6 | 6 | 6 |
| 2 | chaos | 3.287423 | 3.262400 | 3.405500 | 3.837000 | 7.164300 | 5 | 5 | 5 |
| 2 | chaos-rossler | 3.256377 | 3.224100 | 3.359800 | 3.831400 | 7.125600 | 27 | 27 | 27 |
| 2 | granular | 3.747002 | 3.705300 | 3.947600 | 4.660700 | 9.412200 | 8 | 8 | 8 |
| 2 | baseline | 0.554897 | 0.547700 | 0.593600 | 0.797400 | 1.490700 | 0 | 0 | 0 |
| 3 | matrix | 3.006438 | 2.978500 | 3.141600 | 3.603600 | 8.390000 | 12 | 12 | 13 |
| 3 | chaos | 3.298568 | 3.267500 | 3.409700 | 3.861400 | 7.225700 | 21 | 21 | 21 |
| 3 | chaos-rossler | 3.246762 | 3.218400 | 3.349300 | 3.808600 | 7.182700 | 16 | 16 | 16 |
| 3 | granular | 3.729838 | 3.680900 | 3.974000 | 4.667700 | 9.470300 | 37 | 37 | 41 |
| 3 | baseline | 0.553660 | 0.546000 | 0.595300 | 0.802300 | 1.542500 | 0 | 0 | 0 |
| 3 | unison | 0.986786 | 0.957600 | 1.108100 | 1.669900 | 2.917400 | 0 | 0 | 0 |

## All 18 runs: Instrumented callback body

| Round | Case | Mean | p50 | p95 | p99 | Max | Process > deadline | Body > deadline | Manager estimate |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | baseline | 0.556645 | 0.548700 | 0.597400 | 0.811200 | 1.499900 | 0 | 0 | 0 |
| 1 | unison | 0.976080 | 0.962000 | 1.042100 | 1.276900 | 2.598000 | 0 | 0 | 0 |
| 1 | matrix | 2.974012 | 2.948100 | 3.108400 | 3.541900 | 8.275400 | 9 | 9 | 9 |
| 1 | chaos | 3.291288 | 3.261100 | 3.422700 | 3.946000 | 7.198400 | 14 | 14 | 14 |
| 1 | chaos-rossler | 3.250796 | 3.223100 | 3.344200 | 3.803200 | 7.149600 | 15 | 15 | 15 |
| 1 | granular | 3.727722 | 3.677400 | 3.964300 | 4.636700 | 9.471300 | 36 | 36 | 36 |
| 2 | unison | 0.969461 | 0.956700 | 1.025100 | 1.246500 | 2.831600 | 0 | 0 | 0 |
| 2 | matrix | 2.986695 | 2.962600 | 3.106800 | 3.522200 | 8.279800 | 6 | 6 | 6 |
| 2 | chaos | 3.288695 | 3.263700 | 3.407100 | 3.840600 | 7.167400 | 5 | 5 | 5 |
| 2 | chaos-rossler | 3.257563 | 3.225200 | 3.361100 | 3.832500 | 7.128900 | 27 | 27 | 27 |
| 2 | granular | 3.748194 | 3.706600 | 3.948900 | 4.662000 | 9.415500 | 8 | 8 | 8 |
| 2 | baseline | 0.556119 | 0.548900 | 0.594900 | 0.801200 | 1.493900 | 0 | 0 | 0 |
| 3 | matrix | 3.007632 | 2.979700 | 3.142800 | 3.604900 | 8.393300 | 12 | 12 | 13 |
| 3 | chaos | 3.299775 | 3.268600 | 3.411700 | 3.862700 | 7.229000 | 21 | 21 | 21 |
| 3 | chaos-rossler | 3.247967 | 3.219500 | 3.350900 | 3.809800 | 7.186400 | 16 | 16 | 16 |
| 3 | granular | 3.731054 | 3.682200 | 3.975800 | 4.669000 | 9.473500 | 37 | 37 | 41 |
| 3 | baseline | 0.554837 | 0.547200 | 0.596700 | 0.803600 | 1.546100 | 0 | 0 | 0 |
| 3 | unison | 0.988037 | 0.958800 | 1.109700 | 1.672100 | 2.921000 | 0 | 0 | 0 |

The callback body ends before final record publication and excludes the outer JUCE/driver stack. Hardware output was cleared; these records are not listening or audible-dropout evidence. No allocation, lock-freedom or DAW conclusion is made.
