/*----------------------------------------------------------------------
C++26 契约（contracts）的公共入口。

契约把"调用方必须满足、否则结果未定义"的前提写成代码里能检查的东西：
违约时走 handle_contract_violation()，报出条件原文与源码位置，然后终止。

用法：

    #include "yafu_contracts.h"
    int f(int x) pre(x > 0) { ... }

契约语法要 -std=c++26（GCC 16 起可用）。别的标准下这个头里的东西全部退化为
空，代码照常编译。
----------------------------------------------------------------------*/

#ifndef YAFU_CONTRACTS_H
#define YAFU_CONTRACTS_H

#if defined(__cpp_contracts)

#include <contracts>

// 违约处理函数全局只能有一个实现，放在 ytools.cpp（每个链接目标都带着它）。
void handle_contract_violation(const std::contracts::contract_violation& v);

#endif // __cpp_contracts

#endif // YAFU_CONTRACTS_H
