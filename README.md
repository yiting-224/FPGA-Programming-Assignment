# FPGA-Programming-Assignment
Topology-Aware Multi-FPGA Partitioning (CS 516000 programming assignment).

## Build and run

```bash
make
./partitioner <input_file> <output_file> [time_limit_seconds]
```

The default time budget is 250 s (official limit: 300 s). Only the C++17
standard library is used (`std::thread` for parallel attempts, `-pthread`).

## Layout

| Path | Content |
| --- | --- |
| `Makefile` | builds `partitioner` |
| `src/hypergraph.*` | data structures, input parser, output writer, contraction |
| `src/coarsen.cpp` | clustering for coarsening |
| `src/initial.cpp` | initial partitioning, coarsening hierarchy, multilevel driver |
| `src/refiner.*` | incremental partition state, greedy refinement, simulated annealing |
| `src/main.cpp` | parallel multi-start driver and time management |
| `report/report.html` | report source (`node report/render.js` renders `report.pdf`) |
| `report.pdf` | report (English) |
| `report/report_zh.html`, `report_zh.pdf` | report (Traditional Chinese); `node report/render.js report/report_zh.html report_zh.pdf` |
| `report/make_docx.js`, `report_zh.docx` | report (Traditional Chinese, Word); `node report/make_docx.js` |
| `package.sh` | `./package.sh [student_id] [zh]` creates `<student_id>.zip` (default ID 115062556; `zh`: Chinese report as `report.pdf`) |
| `checker`, `testcases.zip` | provided checker and public test cases |

See `report.pdf` for the algorithm description and results.
