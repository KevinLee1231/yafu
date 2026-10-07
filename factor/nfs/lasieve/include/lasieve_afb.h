/* lasieve_afb.h -- <jobfile>.afb.<side> 因子基缓存的文件格式。
 *
 * 格式只有一份，却原来各写各的：写的一侧是 siever
 * （lasieve/gnfs-lasieve4e.cpp），读的一侧是 yafu 的 sieving 阶段
 * （nfs_sieving.cpp）。两边 magic 各有一个宏、trailer 长度一个写死 20
 * 一个写 AFB_EXT_WORDS * 4，legacy 长度公式 2*4 + 2*n*4 也各算一遍。
 * 改了一边忘了另一边，缓存就会被当成坏文件删掉。
 *
 * 文件布局：
 *   [u32 fbsize]
 *   [fbsize 个 u32 素数]
 *   [fbsize 个 u32 平方根]
 *   [u32 xFBs]
 *   以上是 legacy 部分；
 *   紧跟 5 个 u32 的 trailer：magic、缓存用的 FB_bound、多项式指纹的
 *   低 32 位与高 32 位、覆盖前面记录与 trailer 字段的校验和。
 *
 * trailer 由 siever 追加，legacy 读方到不了这里；没有 trailer 的文件
 * 除非 LASIEVE_AFB_ALLOW_LEGACY=1 否则不收，因为完整性无从校验。
 */
#ifndef YAFU_LASIEVE_AFB_H
#define YAFU_LASIEVE_AFB_H

#include <cstddef>
#include <cstdint>

/* trailer 布局与读方一致，改这里两边同时改。 */
#define AFB_EXT_MAGIC 0xafb00004u
#define AFB_EXT_WORDS 5
#define AFB_EXT_BYTES (AFB_EXT_WORDS * sizeof(std::uint32_t))

/* trailer 之前那部分的字节数：两个计数/长度字 + 素数与平方根各一份。 */
constexpr std::size_t afb_legacy_bytes(std::size_t fbsize) noexcept
{
	return 2 * sizeof(std::uint32_t) + 2 * fbsize * sizeof(std::uint32_t);
}

#endif /* YAFU_LASIEVE_AFB_H */
