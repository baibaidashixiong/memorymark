# MemoryMark 1.1

MemoryMark 是 C11 内存子系统微基准工具。借鉴 CoreMark 的固定负载、重复测量和单一评分理念，提供一个默认可直接运行的 MemoryMark 分数，同时保留各分项结果以便解释和诊断。它不复用 CoreMark 的负载或评分规则。运行时不依赖第三方库，不需要管理员权限，不修改系统配置。

## 构建与首次运行

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/memorymark
./build/memorymark --score --json
./build/memorymark --test all --size 8M --duration 0.1 --repeat 3
```

未指定构建类型时默认 Release。不带参数运行会执行固定评分套件，约需 15 至 30 秒，结束时先显示分数和稳定性，再显示六项分项。评分至少需要 192 MiB 内存预算；默认预算为 1 GiB。显式使用 `--test` 时进入高级诊断模式，不会生成总分。默认诊断测试为单线程顺序读、每线程 8 MiB、3 轮、每轮目标 0.1 秒。Python 3 只用于集成测试；没有 Python 时 CMake 会跳过集成测试，仍可构建工具和 C 单元测试。

## 一键评分

```text
MemoryMark Score: 546.3 MMark
Status: STABLE (max variability 1.3%, verification PASSED)
Profile: fixed-v1 | 1 thread | 128 MiB/thread | 8M accesses/pass | 0.5 s x 3 | warm 0.1 s

Sequential read         50.29 GB/s     6286.44 Mops/s
Sequential write        28.31 GB/s     3538.57 Mops/s
70/30 read/write        37.49 GB/s     4685.89 Mops/s
Random read              1.40 GB/s      174.46 Mops/s
Random write             1.17 GB/s      146.71 Mops/s
Dependent latency        0.08 GB/s        9.96 Mops/s  (100.37 ns/access)
```

上述数字是开发机示例，不是参考硬件排名。固定评分规格为：

- 单工作线程、每项 128 MiB 工作集、步长 8 字节、seed=1。
- 随机读、随机写和指针追逐每遍 8,388,608 次访问（8M）；顺序和 mix 访问全部槽。
- 每项预热至少 0.1 秒，正式目标 0.5 秒，共 3 个测量轮。
- 顺序读、顺序写、70/30 mix、随机读、随机写、依赖式指针追逐共 6 项。
- 所有项均通过计时外数据验证；任一项验证、分配、线程或计时失败则不给分。

每项先换算为“每秒百万次逻辑 8 字节访问”（Mops/s）。指针追逐的访问率定义为 `1000 / ns_per_access`。总分为六项 Mops/s 的几何平均，因此单位定义为 **1 MMark = 几何平均每秒一百万次逻辑访问**：

```text
Score = (read × write × mix × random_read × random_write × latency_rate)^(1/6)
```

几何平均会惩罚任一项明显偏低，且不会出现“顺序带宽很高就掩盖随机延迟很差”的情况。分数没有人为参考基线，不宣称可直接与 CoreMark、STREAM 或其他工具横向比较；应只比较同一 MemoryMark 版本、Release 构建、相近编译器和受控环境下的结果。

`Status: STABLE` 要求六项吞吐量的测量变异系数均不超过 10%；否则仍输出中位数计算的分数，但标记 `UNSTABLE`，应让系统空闲后重跑。若需要 JSON/CSV 或固定 CPU：

```sh
./build/memorymark --score --json
./build/memorymark --score --csv
./build/memorymark --score --cpu-affinity 4
```

评分模式只接受输出、`--memory-limit` 和最多一个 CPU 亲和性参数；不能混入 `--test`、`--size`、`--duration` 等工作负载参数，否则退出码为 2。这是为了确保所有 MMark 使用相同规格。需要自定义测试规模和多线程信息时使用详细模式。

```sh
# Debug
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j
ctest --test-dir build-debug --output-on-failure

# Clang Release
cmake -S . -B build-clang -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Release
cmake --build build-clang -j
ctest --test-dir build-clang --output-on-failure

# AddressSanitizer + UndefinedBehaviorSanitizer，包括泄漏检查
cmake -S . -B build-sanitize -DCMAKE_C_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=Debug -DMM_SANITIZE=ON
cmake --build build-sanitize -j
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-sanitize --output-on-failure
```

不要使用 Debug、Sanitizer 构建做性能比较。默认没有 `-march=native`、LTO 或非临时写入，使用构建工具链的 Release 优化标志。结果包含编译器版本和构建类型；完整标志可通过 `cmake --build build --verbose` 和 `build/CMakeCache.txt` 查看，自定义标志需自行随结果存档。

## CLI

所有选项均为 `--名称 值` 形式，无短选项，不接受 `--名称=值`。容量支持字节整数及 K/M/G、KiB/MiB/GiB、KB/MB/GB 后缀，均按 1024 进位；大小、步长、线性扫描增量必须为 8 的倍数。

| 参数 | 默认 | 含义 |
|---|---|---|
| `--help` / `--version` | | 帮助 / 版本 |
| `--score` | 无参数时自动 | 运行固定 v1 评分套件；不能混入工作负载调优参数 |
| `--test` | `seq-read` | 详细模式：`seq-read`、`seq-write`、`mix`、`random-read`、`random-write`、`latency`、`all` |
| `--size` | `8M` | **每线程**缓冲区大小，不是所有线程总和 |
| `--threads` | `1` | 1 至 256 个线程，私有缓冲区 |
| `--thread-sweep` | 关闭 | 从 1 到指定线程数逐一运行 |
| `--iterations` | 未指定 | 每线程、每轮精确执行的内核遍数，正整数，覆盖 duration |
| `--duration` | `0.1` | 每线程、每轮目标秒数，范围 0.001 至 3600，整遍执行，可能超时 |
| `--warmup` | `0.02` | 每轮正式计时前最低预热秒数，0 至 3600；即使 0 也运行一遍 |
| `--repeat` | `3` | 1 至 100 轮，各轮重新预热，但不重新分配 |
| `--seed` | `1` | 无符号 64 位整数，0 也合法 |
| `--stride` | `8` | 字节步长，至少 8，不得大于最小测试工作集 |
| `--accesses` | 可用槽数 | 随机/指针追逐每遍访问次数；顺序和 mix 忽略此项 |
| `--read-ratio` | `50` | mix 的读取百分比，整数 0 至 100 |
| `--sweep` | 关闭 | 扫描工作集；指定 min/max/scale/step 也会开启 |
| `--min-size` | `4K` | 扫描最小每线程工作集 |
| `--max-size` | `64M` | 扫描最大每线程工作集 |
| `--scale` | `2` | 倍增系数，范围 (1,1024] |
| `--step` | 未指定 | 正线性字节增量，覆盖 scale |
| `--memory-limit` | `1G` | 所有线程数据和地址/排列辅助数组的保守总预算 |
| `--cpu-affinity` | `none` | CPU 编号列表，如 `2,4,6,8`；列表必须覆盖线程数 |
| `--json` / `--csv` | 文本 | 互斥，机器可读标准输出 |

扫描最后总会包含 max；倍增结果向下对齐到 8 字节，若舍入无进展则增加 8 字节。每次规模都重新构建数据，不根据工作集大小硬编码任何 Cache 层级。若超出预算则失败退出，不会无限申请；提高预算之前应检查机器可用内存。

Linux 内存 overcommit 和外部 OOM killer 不受普通用户态分配器控制，任何程序都无法保证在任意压力下免于被系统终止。分配返回失败会清理并报错，默认预算降低风险，但不是系统级物理内存预留。预算不包括线程栈、小型结果记录及系统开销；指针环排列的临时内存按所有线程同时使用保守估算。

## 测试方法

所有测试元素为 64 位、对齐分配基址为 64 字节。若缓冲区有 N 个 64 位元素，步长为 S 个元素，可访问槽数为 `1 + (N-1)/S`；最后一段不足步长不会越界。每次访问只算 8 个逻辑字节，不按整条 Cache line 或整个缓冲区大小计费。

| 内核 | 每遍工作量 | 关键语义 |
|---|---|---|
| seq-read | 按步长读遍所有槽 | 四个独立累加器，结果进入可观察校验和；连续模式可被编译器自动向量化 |
| seq-write | 按步长写遍所有槽 | 普通缓存写，每遍写不同值，测量后验证最终内容 |
| mix | 每槽一次读或写 | 每 100 槽一组，前 `floor(组长度×ratio/100)` 槽读取，其余写入；尾组舍入，实际读比例可能略低 |
| random-read | accesses 次读 | 预生成有放回均匀随机槽索引，独立请求，四累加器，测吞吐而非串行延迟 |
| random-write | accesses 次写 | 预生成同样的索引流，单线程内可重复地址，线程间私有区域，无写数据竞争 |
| latency | accesses 次依赖读取 | 随机排列连接成完整单环，每次读出下一元素索引，地址依赖前一次结果 |

随机数使用定义明确的 SplitMix64，无偏拒绝采样映射至槽范围。线程种子由基础 seed 和线程编号确定；随机地址序列在**相同工作集、步长、accesses、seed、线程编号和 size_t 宽度**下可复现。不同轮重复同一地址序列，这意味着地址索引数组本身也会缓存。索引流读取带来额外带宽与 Cache 压力，但不计入主数据逻辑字节数；不能将该测试当作无软件开销的硬件随机访问能力。

指针追逐使用 Fisher-Yates 随机排列，将排列相邻槽连接，末尾回到开头；从槽 0 沿链验证恰好 count 次后才首次回到 0。构建排列临时数组在计时前释放，正式循环不产生随机数。默认 stride=8 会在一条 Cache line 内放置多个节点；用 stride=64 可减少同一行多节点效应，但 64 只是用户选择，不是假设所有平台 Cache line 一定为 64。

mix 是固定读前缀/写后缀的分块流，不是每个元素都先读再写，也不是随机概率分支；70/30 在完整 100 槽块上是精确比例。每次读或写均按 8 字节计算，50/50 不会被当成 16 字节处理量。

### 计时与同步

1. 工作线程先绑定 CPU（若指定），随后独立分配、初始化和首触碰内存。
2. 每轮预热至少一整遍。预热计时独立于正式测量，不强制冷缓存，也不驱逐 LLC。
3. 所有线程到达同一条件变量起跑门。主线程在释放起跑门前记录总起点；工作线程释放后各自记录起点。
4. 每个线程执行完整遍数或达到 duration；duration 模式每遍检查时钟，因此小工作集的测量含明显计时调用成本。iterations 模式只在测量两端读时钟，适合用足够大的遍数降低成本。
5. 内核函数在独立翻译单元内，默认无 LTO，读结果、调用计数和最终写数据都有可观察用途。写序列在终点前执行 C11 全序内存屏障；该屏障不等于 Cache flush 或持久化。
6. 线程结束时记录结束时间。总耗时为最晚线程终点减统一起点，包含释放门与实际调度偏差，不包含线程创建/分配/预热/校验/线程回收。
7. 所有线程结束计时后才校验数据，且本轮校验和结果收集结束后才允许下一轮预热，避免额外流量重叠正式测量。

计时采用 Linux/POSIX `CLOCK_MONOTONIC`，Windows 路径为 QueryPerformanceCounter。工作线程状态独立 64 字节对齐分配，热循环不使用共享可写数据；只有轮边界操作受互斥锁保护。无法绑定指定 CPU 时会报错，不会静默退化。

短 `--iterations` 测试只是功能验证，计时器、循环和调度可能主导结果。正式比较建议每轮至少 0.1 至 1 秒、至少 5 轮；一次指针环遍历本身可能长于 duration。多线程耗时目标各自独立，较快线程提前结束后负载会有变化，所有线程的持续时间与遍数都在输出中保留。

### 结果与校验

默认文本评分把总分、稳定性、固定规格和六个分项放在同一屏内。`--score --json` 的顶层包含 `score`、`score_unit`、`status`、`verified`、`max_variability`、固定 `profile` 和 `components`；CSV 写一行六项汇总。详细模式的 JSON 与 CSV 保持原有逐轮和逐线程结构，不会把 MMark 总分混入自定义结果。

`GB/s = 逻辑字节数 / 秒 / 10^9`，1 GB = 10^9 bytes；容量 K/M/G 按二进制。普通缓存写**不计入**写分配读、脏行回写、总线协议和额外索引流；屏障不保证数据已经写回 DRAM。小工作集报告的主要可能是 Cache 性能，不是真实 DRAM 总线流量。

只有 latency 输出 ns/access：每线程耗时除以依赖访问次数；多线程汇总为各线程 ns/access 的算术平均，不是总吞吐的倒数。该值包括缓存/TLB/地址转换/循环与处理器执行影响，不是内存控制器的纯 DRAM 固有延迟。

最小值、中位数、均值、总体标准差均基于独立测量轮计算，带宽不默认挑选最好值。吞吐扩展效率为 `该线程数带宽中位数 / 单线程带宽中位数 / 线程数`，只在 `--thread-sweep` 有同一工作集/内核基线时计算；直接运行多线程时 JSON 为 null、CSV 留空。单线程效率为 1。对 latency 的效率也仅表示总吞吐扩展，不表示延迟改善。

所有模式都执行计时外验证：读测试检查原始数据完整性，写测试检查最终写值，mix 检查写槽，指针测试检查完整环。读内核另有累加校验和防优化；验证不声称可检测所有硬件错误。`sequence_hash` 为计时前地址序列哈希的逐线程 XOR，顺序模式为 0；完整单环还单独验证。哈希用于复现诊断而非密码学证明。checksum 含预热及正式遍数的累积状态，duration 模式或非零计时预热导致遍数不同，checksum 不保证跨运行相同；用固定 iterations 且 warmup=0 可做确定性对照。

JSON 顶层为 `schema_version` 和 `results`，每项有配置、编译器、OS/架构、缓存/NUMA策略、统计以及 `rounds[].total` / `rounds[].workers[]`。每条 sample 含耗时、遍数、访问次数、逻辑字节、GB/s、校验和及适用的 ns/access。总遍数为各线程遍数之和。worker 数组位置即线程编号。无普通日志混入 JSON。CSV 每轮先输出 worker=-1 的汇总行，再输出各线程行，重复配置和统计方便分析；ns/access 不适用时留空。CSV affinity 使用分号分隔 CPU 编号。

错误写 stderr，参数错误退出码 2，运行/输出错误 1，成功 0。扫描中失败会保留前面完成的结果，JSON 仍正常封口；必须同时检查退出码，不能因 JSON 可解析就认为所有测试完成。可用重定向单独保存结果：

```sh
./build/memorymark --test all --json > results.json
./build/memorymark --test seq-read --csv > results.csv
```

## 示例

```sh
# 一键评分与机器可读结果
./build/memorymark
./build/memorymark --score --json > score.json
./build/memorymark --score --csv > score.csv

# 常规顺序带宽
./build/memorymark --test seq-read --size 32M --duration 0.5 --repeat 5
./build/memorymark --test seq-write --size 32M --duration 0.5 --repeat 5
./build/memorymark --test mix --size 32M --read-ratio 70

# 固定工作量、固定访问序列
./build/memorymark --test random-read --size 8M --accesses 1000000 \
  --seed 42 --iterations 20 --warmup 0 --repeat 3
./build/memorymark --test random-write --size 8M --seed 42

# 从小工作集到主存规模的依赖访问扫描
./build/memorymark --test latency --min-size 4K --max-size 64M \
  --scale 2 --stride 64 --duration 0.2 --json > latency.json

# 多线程扩展，每线程 16 MiB，线程数 1、2、3、4
./build/memorymark --test seq-read --size 16M --threads 4 --thread-sweep

# 线性扫描
./build/memorymark --test seq-write --min-size 4M --max-size 32M --step 4M

# 仅在 CPU 2/4 确实属于当前进程允许 CPU 集合时使用
./build/memorymark --test seq-read --threads 2 --cpu-affinity 2,4

# 可选较大工作集：确认物理内存充足后运行
./build/memorymark --test seq-read --size 1G --memory-limit 2G --duration 1
```

## 影响可比性的因素与范围

- 缓存状态、硬件预取器、TLB、透明大页会改变结果；本工具不关闭缓存、不要求 hugepage、不主动清空缓存。
- 初始化在工作线程中完成，遵循 OS first-touch 策略；CPU 迁移、NUMA 自动迁移和系统分配策略均可能改变实际位置。亲和性不等价于显式 NUMA 内存绑定。
- 内存频率/通道数、CPU 睿频与降频、温度、功耗约束、SMT、后台负载和调度会影响结果。记录机器型号、内存配置、内核、CPU 列表、编译标志与命令行；不要只比较一个 GB/s 数字。
- 每线程工作集固定，增加线程同时增加总工作集；并非固定总容量分片实验。当前不提供共享缓冲区模式。
- 没有特权性能计数器支持，不测真实 DRAM 流量；MMark 是逻辑访问率几何平均，不是 DRAM 总线吞吐分数。
- 未实现显式 NUMA 本地/远端策略：这是可选项，需要拓扑发现、受限容器策略及内存放置验证；不以 first-touch 冒充验证过的远端测试。
- 未实现流式/非临时写入：当前为可移植普通缓存写路径，不以屏障冒充 streaming store。
- Linux x86_64 已实际验证 GCC/Clang；POSIX 线程和计时路径保留其他 Unix 可移植性，但 Linux 之外亲和性明确不支持。Windows 有原生线程、条件变量、对齐分配和 QPC 条件编译路径；尚无 Windows/MSVC 实机或交叉编译验证，MSVC 的 C11 原子支持需使用较新版本。Windows CPU 绑定当前限单个处理器组位掩码。

## 模块与测试

`include/mm.h` 为公共接口。`cli.c` 负责解析和边界；`timer.c`、`platform.c` 隔离平台；`memory.c` 管理工作集、随机数、地址数组和环；三个 bench 内核文件实现访问；`bench_parallel.c` 管理线程、阶段同步、验证及统计；`main.c` 扫描并处理退出码；`output.c` 提供三种输出。

`tests/unit.c` 测容量溢出、统计、已知随机数向量、固定种子、地址范围、不同步长的完整环及损坏环。`tests/integration.py` 实际启动程序，覆盖全部模式/比例、8 字节和 64 MiB×2、三线程、两种扫描、真实时长、CPU 亲和性、JSON/CSV 解析及各类非法参数。测试使用显式 CHECK 而非可被 Release 的 NDEBUG 消除的 assert。

验证结果及实际命令见 [TESTING.md](TESTING.md)。
