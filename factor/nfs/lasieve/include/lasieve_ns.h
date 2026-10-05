/* 每个 I 值的筛法器是一份独立的 namespace。
 *
 * 以前这里是 509 个 -D名字=名字I<N>：同一份源文件编六遍，预处理器给符号加
 * I 后缀。命令行长到两千多字符，符号表里全是 xxxI11，改一个符号要同时想
 * 到六份。
 *
 * 现在改成真命名空间：per-I 的编译单元整份包在 namespace lasieve_ns 里，
 * Makefile 传 -Dlasieve_ns=lasieve_I<N>。不传就是 lasieve_single，给那些
 * 不按 I 值编译的单份对象用（mpqstest 等独立测试驱动链的就是它们）。
 */
#ifndef lasieve_ns
#define lasieve_ns lasieve_single
#endif