# 实际验收记录

日期：2026-10-26。环境：Linux x86_64，内核 7.1.8-200.fc44.x86_64，GCC 16.1.1，Clang 22.1.8，CMake 4.3.0，Python 3.12.13。

## 构建与自动化结果

| 构建目录 | 配置 | 最终结果 |
|---|---|---|
| build | GCC Release | 无编译告警；CTest 2/2，通过；集成 54 个进程 |
| build-debug | GCC Debug | 无编译告警；CTest 2/2，通过 |
| build-clang | Clang Release | 无编译告警；CTest 2/2，通过 |
| build-sanitize | Clang Debug + ASan/UBSan | CTest 2/2，通过；集成 53 个进程，无 sanitizer/泄漏报告 |
| build-tsan | Clang Debug + TSan | CTest 2/2，通过；集成 53 个进程，无数据竞争报告 |
| build-cmake313 | GCC Release，CMake 3.13.4 | `cmake ..` 配置、编译及 CTest 2/2 通过 |

普通构建额外使用 Linux RLIMIT_AS 限制子进程地址空间，验证分配失败返回 1 并清理退出。Sanitizer 运行跳过该用例，因为其运行时需要预留大型虚拟地址空间。TSan 通过不构成对所有可能并发执行的形式证明。

实际执行命令如下，最后又对所有目录重新构建并运行 CTest，以覆盖轮末屏障、计时后校验、输出 fflush 和新增错误回归：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure -V

cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j 4
ctest --test-dir build-debug --output-on-failure

cmake -S . -B build-clang -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Release
cmake --build build-clang -j 4
ctest --test-dir build-clang --output-on-failure

cmake -S . -B build-sanitize -DCMAKE_C_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=Debug -DMM_SANITIZE=ON
cmake --build build-sanitize -j 4
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-sanitize --output-on-failure -V

cmake -S . -B build-tsan -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS=-fsanitize=thread -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build build-tsan -j 4
ctest --test-dir build-tsan --output-on-failure -V
```

## 覆盖范围

- C 单元测试：容量后缀与溢出；架构快照字段与页大小；均值/中位数/总体标准差；评分几何平均；无参数进入评分模式；评分 JSON 参数；已知 PRNG 向量；无偏映射范围；固定种子；随机地址范围；8 至 4096 字节步长的完整环；人为损坏环检测；单调时间。
- 真实进程集成：全部六模式，三线程及逐线程计数核对，mix 的 0/30/50/70/100 比例，8 字节极小工作集，64 MiB 每线程双线程（随机模式辅助地址数组另计），非整步长/尾块，4 线程，随机访问次数，预热及 duration 实际耗时。
- 机器输出：标准库 JSON/CSV 解析、列数、样本字段、汇总/逐线程字节相等、非延迟模式不伪报 latency。
- 架构快照：单元测试验证机器标识、CPU 型号、内核、字节序、指针宽度和页大小；详细与评分模式的文本、JSON、CSV 均实测解析成功。
- 扫描：倍增、线性、最后不足一步仍包含 max、1 至 3 线程扩展、扫描中途预算失败后 JSON 合法封口且退出非零。
- 固定 seed：随机读、随机写、指针追逐在 iterations 固定和 warmup=0 时，访问序列哈希与校验和复现。
- 参数错误：负数、零、越界线程/重复数/比例、NaN、无效容量、溢出、步长、无效扫描、缺值、未知选项、JSON/CSV 冲突、无效 CPU 列表、超大计数和预算限制；评分模式混入测试/调优参数、多个 CPU 或低于 192 MiB 预算会快速失败。
- 亲和性：使用当前进程允许 CPU 集合中的一个 CPU 实测；不可用 CPU 显式失败。

## 手动烟雾测试

MemoryMark 1.1.0 固定评分套件在最终 GCC Release 构建中实际执行：

```sh
./build/memorymark
./build/memorymark --score --json
./build/memorymark --score --csv
```

最终文本运行耗时 22.8 秒，得到 `546.3 MMark`，状态 `STABLE`，最大变异 1.3%，六项验证全部通过。JSON 单对象和 CSV 单行分别实测成功；评分配置覆盖顺序读、顺序写、70/30 mix、随机读、随机写和依赖式指针追逐。早期 256 MiB 规格实测耗时 49.6 秒，因此调整为 128 MiB 工作集及每遍 8,388,608 次随机/延迟访问，兼顾主存覆盖、稳定性和一键运行时间。

架构输出实测识别到 `AMD Ryzen 7 8845HS w/ Radeon 780M Graphics`、Linux `7.1.8-200.fc44.x86_64`、x86_64、64-bit little-endian、4096 字节页、16 个允许 CPU、1 个 NUMA 节点以及 32 KiB L1d、1 MiB L2、16 MiB L3。评分 JSON 在测量前采集并保存该快照；评分 CSV 包含 51 列且可由标准 CSV 解析器读取。

此外使用 `build-sanitize/memorymark --score --json` 完整运行评分，在 ASan/UBSan 与泄漏检查下成功解析为 86.7 MMark、stable，无 sanitizer 报告。Sanitizer/Debug 分数只用于验证，不可与 Release 分数比较。

另行实际执行：

```sh
./build/memorymark --test all --size 8M --duration 0.1 --repeat 3
./build/memorymark --test seq-read --size 16M --threads 4 --thread-sweep \
  --duration 0.1 --repeat 3
```

两条命令均成功退出，全部 verification=passed，并输出各轮耗时、字节、带宽及校验和。运行与其他验收任务并行，明显受到竞争和调度影响，数值不能作为受控性能结论。本记录不提供未经控制环境测量的硬件排名。

开发过程中 Clang 曾提示浮点数隐式转布尔警告，已改为显式比较；同步检查发现下一轮预热和本轮校验可能干扰其他线程计时，已分别添加轮末确认与计时完成屏障。最终五组测试无失败。

## 未验证和未提供

Windows/MSVC、ARM、其他 Unix 未在本环境编译或运行验证；Windows 仅提供隔离的原生实现路径。可选显式 NUMA 本地/远端绑定、流式存储、共享工作区未实现，原因和当前语义见 README。未实际申请 GiB 级工作集；自动测试较大配置为 64 MiB 每线程、两线程，GiB 级预算拒绝和整数溢出路径已有测试。

README 保留为便于随源码版本审查的 Markdown 使用文档，不引入前端或文档生成依赖。
