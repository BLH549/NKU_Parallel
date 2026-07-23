#include "md5.h"
#include <arm_neon.h>  // NEON SIMD指令集
#include <iomanip>
#include <assert.h>
#include <chrono>

using namespace std;
using namespace chrono;

// NEON helper宏：用于在O0下规避helper函数调用边界
#define VFN(x, y, z) vorrq_u32(vandq_u32((x), (y)), vandq_u32(vmvnq_u32((x)), (z)))
#define VGN(x, y, z) vorrq_u32(vandq_u32((x), (z)), vandq_u32((y), vmvnq_u32((z))))
#define VHN(x, y, z) veorq_u32(veorq_u32((x), (y)), (z))
#define VIN(x, y, z) veorq_u32((y), vorrq_u32((x), vmvnq_u32((z))))
#define VROTATEL(val, n) vorrq_u32(vshlq_n_u32((val), (n)), vshrq_n_u32((val), (32 - (n))))
#define VADD(a, b) vaddq_u32((a), (b))

#define VFF(a, b, c, d, x, s, ac) { \
    (a) = VADD((a), VFN((b), (c), (d))); \
    (a) = VADD((a), (x)); \
    (a) = VADD((a), vdupq_n_u32(static_cast<uint32_t>(ac))); \
    (a) = VROTATEL((a), (s)); \
    (a) = VADD((a), (b)); \
}

#define VGG(a, b, c, d, x, s, ac) { \
    (a) = VADD((a), VGN((b), (c), (d))); \
    (a) = VADD((a), (x)); \
    (a) = VADD((a), vdupq_n_u32(static_cast<uint32_t>(ac))); \
    (a) = VROTATEL((a), (s)); \
    (a) = VADD((a), (b)); \
}

#define VHH(a, b, c, d, x, s, ac) { \
    (a) = VADD((a), VHN((b), (c), (d))); \
    (a) = VADD((a), (x)); \
    (a) = VADD((a), vdupq_n_u32(static_cast<uint32_t>(ac))); \
    (a) = VROTATEL((a), (s)); \
    (a) = VADD((a), (b)); \
}

#define VII(a, b, c, d, x, s, ac) { \
    (a) = VADD((a), VIN((b), (c), (d))); \
    (a) = VADD((a), (x)); \
    (a) = VADD((a), vdupq_n_u32(static_cast<uint32_t>(ac))); \
    (a) = VROTATEL((a), (s)); \
    (a) = VADD((a), (b)); \
}

/**
 * StringProcess: 将单个输入字符串转换成MD5计算所需的消息数组
 * @param input 输入
 * @param[out] n_byte 用于给调用者传递额外的返回值，即最终Byte数组的长度
 * @return Byte消息数组
 */
Byte *StringProcess(const string& input, int *n_byte)
{
	int length = input.length();
	// paddedLength = ceil((length + 9) / 64) * 64, 使 length + 1(0x80) + k(zero) + 8(sizefield) 为64的倍数
	int paddedLength = ((length + 72) / 64) * 64;

	Byte *paddedMessage = new Byte[paddedLength];
	memcpy(paddedMessage, input.c_str(), length);

	paddedMessage[length] = 0x80;
	int zeroBytes = paddedLength - length - 9;
	if (zeroBytes > 0)
		memset(paddedMessage + length + 1, 0, zeroBytes);

	uint64_t bitLength = (uint64_t)length * 8;
	for (int i = 0; i < 8; ++i)
		paddedMessage[paddedLength - 8 + i] = (bitLength >> (i * 8)) & 0xFF;

	*n_byte = paddedLength;
	return paddedMessage;
}


/**
 * MD5Hash: 将单个输入字符串转换成MD5
 * @param input 输入
 * @param[out] state 用于给调用者传递额外的返回值，即最终的缓冲区，也就是MD5的结果
 * @return Byte消息数组
 */
void MD5Hash(const string& input, bit32 *state)
{

	Byte *paddedMessage;
	int messageLength;
	paddedMessage = StringProcess(input, &messageLength);
	int n_blocks = messageLength / 64;

	// bit32* state= new bit32[4];
	state[0] = 0x67452301;
	state[1] = 0xefcdab89;
	state[2] = 0x98badcfe;
	state[3] = 0x10325476;

	// 逐block地更新state
	for (int i = 0; i < n_blocks; i += 1)
	{
		bit32 x[16];

		// 下面的处理，在理解上较为复杂
		for (int i1 = 0; i1 < 16; ++i1)
		{
			x[i1] = (paddedMessage[4 * i1 + i * 64]) |
					(paddedMessage[4 * i1 + 1 + i * 64] << 8) |
					(paddedMessage[4 * i1 + 2 + i * 64] << 16) |
					(paddedMessage[4 * i1 + 3 + i * 64] << 24);
		}

		bit32 a = state[0], b = state[1], c = state[2], d = state[3];

		auto start = system_clock::now();
		/* Round 1 */
		FF(a, b, c, d, x[0], s11, 0xd76aa478);
		FF(d, a, b, c, x[1], s12, 0xe8c7b756);
		FF(c, d, a, b, x[2], s13, 0x242070db);
		FF(b, c, d, a, x[3], s14, 0xc1bdceee);
		FF(a, b, c, d, x[4], s11, 0xf57c0faf);
		FF(d, a, b, c, x[5], s12, 0x4787c62a);
		FF(c, d, a, b, x[6], s13, 0xa8304613);
		FF(b, c, d, a, x[7], s14, 0xfd469501);
		FF(a, b, c, d, x[8], s11, 0x698098d8);
		FF(d, a, b, c, x[9], s12, 0x8b44f7af);
		FF(c, d, a, b, x[10], s13, 0xffff5bb1);
		FF(b, c, d, a, x[11], s14, 0x895cd7be);
		FF(a, b, c, d, x[12], s11, 0x6b901122);
		FF(d, a, b, c, x[13], s12, 0xfd987193);
		FF(c, d, a, b, x[14], s13, 0xa679438e);
		FF(b, c, d, a, x[15], s14, 0x49b40821);

		/* Round 2 */
		GG(a, b, c, d, x[1], s21, 0xf61e2562);
		GG(d, a, b, c, x[6], s22, 0xc040b340);
		GG(c, d, a, b, x[11], s23, 0x265e5a51);
		GG(b, c, d, a, x[0], s24, 0xe9b6c7aa);
		GG(a, b, c, d, x[5], s21, 0xd62f105d);
		GG(d, a, b, c, x[10], s22, 0x2441453);
		GG(c, d, a, b, x[15], s23, 0xd8a1e681);
		GG(b, c, d, a, x[4], s24, 0xe7d3fbc8);
		GG(a, b, c, d, x[9], s21, 0x21e1cde6);
		GG(d, a, b, c, x[14], s22, 0xc33707d6);
		GG(c, d, a, b, x[3], s23, 0xf4d50d87);
		GG(b, c, d, a, x[8], s24, 0x455a14ed);
		GG(a, b, c, d, x[13], s21, 0xa9e3e905);
		GG(d, a, b, c, x[2], s22, 0xfcefa3f8);
		GG(c, d, a, b, x[7], s23, 0x676f02d9);
		GG(b, c, d, a, x[12], s24, 0x8d2a4c8a);

		/* Round 3 */
		HH(a, b, c, d, x[5], s31, 0xfffa3942);
		HH(d, a, b, c, x[8], s32, 0x8771f681);
		HH(c, d, a, b, x[11], s33, 0x6d9d6122);
		HH(b, c, d, a, x[14], s34, 0xfde5380c);
		HH(a, b, c, d, x[1], s31, 0xa4beea44);
		HH(d, a, b, c, x[4], s32, 0x4bdecfa9);
		HH(c, d, a, b, x[7], s33, 0xf6bb4b60);
		HH(b, c, d, a, x[10], s34, 0xbebfbc70);
		HH(a, b, c, d, x[13], s31, 0x289b7ec6);
		HH(d, a, b, c, x[0], s32, 0xeaa127fa);
		HH(c, d, a, b, x[3], s33, 0xd4ef3085);
		HH(b, c, d, a, x[6], s34, 0x4881d05);
		HH(a, b, c, d, x[9], s31, 0xd9d4d039);
		HH(d, a, b, c, x[12], s32, 0xe6db99e5);
		HH(c, d, a, b, x[15], s33, 0x1fa27cf8);
		HH(b, c, d, a, x[2], s34, 0xc4ac5665);

		/* Round 4 */
		II(a, b, c, d, x[0], s41, 0xf4292244);
		II(d, a, b, c, x[7], s42, 0x432aff97);
		II(c, d, a, b, x[14], s43, 0xab9423a7);
		II(b, c, d, a, x[5], s44, 0xfc93a039);
		II(a, b, c, d, x[12], s41, 0x655b59c3);
		II(d, a, b, c, x[3], s42, 0x8f0ccc92);
		II(c, d, a, b, x[10], s43, 0xffeff47d);
		II(b, c, d, a, x[1], s44, 0x85845dd1);
		II(a, b, c, d, x[8], s41, 0x6fa87e4f);
		II(d, a, b, c, x[15], s42, 0xfe2ce6e0);
		II(c, d, a, b, x[6], s43, 0xa3014314);
		II(b, c, d, a, x[13], s44, 0x4e0811a1);
		II(a, b, c, d, x[4], s41, 0xf7537e82);
		II(d, a, b, c, x[11], s42, 0xbd3af235);
		II(c, d, a, b, x[2], s43, 0x2ad7d2bb);
		II(b, c, d, a, x[9], s44, 0xeb86d391);

		state[0] += a;
		state[1] += b;
		state[2] += c;
		state[3] += d;
	}

	// 下面的处理，在理解上较为复杂
	for (int i = 0; i < 4; i++)
	{
		uint32_t value = state[i];
		state[i] = ((value & 0xff) << 24) |		 // 将最低字节移到最高位
				   ((value & 0xff00) << 8) |	 // 将次低字节左移
				   ((value & 0xff0000) >> 8) |	 // 将次高字节右移
				   ((value & 0xff000000) >> 24); // 将最高字节移到最低位
	}

	// 输出最终的hash结果
	// for (int i1 = 0; i1 < 4; i1 += 1)
	// {
	// 	cout << std::setw(8) << std::setfill('0') << hex << state[i1];
	// }
	// cout << endl;

	delete[] paddedMessage;
}

/**
 * SIMD批量MD5哈希 - NEON版本
 * 每次并行处理4条消息，栈内联填充消除new[]/delete[]开销
 */
void MD5Hash_SIMD(const string inputs[], int count, bit32 states[][4])
{
    const int BATCH_SIZE = 4;

    for (int batch_start = 0; batch_start < count; batch_start += BATCH_SIZE) {
        int batch_count = (count - batch_start < BATCH_SIZE) ? count - batch_start : BATCH_SIZE;

        // 栈缓冲区: 128字节可容纳最多119字符的口令, 覆盖所有常见口令长度
        alignas(16) Byte padded_buf[4][128];
        Byte* padded[4];
        int msg_lens[4];
        bool on_heap[4] = {false};

        // 内联填充padded消息 — 零new[]调用，数据保持L1热度
        for (int i = 0; i < batch_count; i++) {
            int len = inputs[batch_start + i].length();
            int plen = ((len + 72) / 64) * 64;
            msg_lens[i] = plen;

            if (plen <= 128) {
                padded[i] = padded_buf[i];
            } else {
                padded[i] = new Byte[plen];
                on_heap[i] = true;
            }

            memcpy(padded[i], inputs[batch_start + i].c_str(), len);
            padded[i][len] = 0x80;
            int zeroBytes = plen - len - 9;
            if (zeroBytes > 0)
                memset(padded[i] + len + 1, 0, zeroBytes);
            uint64_t bitLen = (uint64_t)len * 8;
            for (int j = 0; j < 8; j++)
                padded[i][plen - 8 + j] = (bitLen >> (j * 8)) & 0xFF;
        }

        // 补齐不足4条的批次 (指针别名，只读语义)
        for (int i = batch_count; i < BATCH_SIZE; i++) {
            padded[i] = padded[batch_count - 1];
            msg_lens[i] = msg_lens[batch_count - 1];
        }

        int n_blocks = msg_lens[0] / 64;

        uint32x4_t A = vdupq_n_u32(0x67452301);
        uint32x4_t B = vdupq_n_u32(0xefcdab89);
        uint32x4_t C = vdupq_n_u32(0x98badcfe);
        uint32x4_t D = vdupq_n_u32(0x10325476);

        for (int blk = 0; blk < n_blocks; blk++) {
            uint32x4_t a = A, b = B, c = C, d = D;

            // NEON 矩阵转置加载 (AoS to SoA)
            uint32x4_t x[16];
            for (int k = 0; k < 4; k++) {
                int off = blk * 64 + k * 16;

                uint32x4_t m0 = vld1q_u32((const uint32_t*)&padded[0][off]);
                uint32x4_t m1 = vld1q_u32((const uint32_t*)&padded[1][off]);
                uint32x4_t m2 = vld1q_u32((const uint32_t*)&padded[2][off]);
                uint32x4_t m3 = vld1q_u32((const uint32_t*)&padded[3][off]);

                uint32x4x2_t t0 = vzipq_u32(m0, m2);
                uint32x4x2_t t1 = vzipq_u32(m1, m3);

                uint32x4x2_t u0 = vzipq_u32(t0.val[0], t1.val[0]);
                uint32x4x2_t u1 = vzipq_u32(t0.val[1], t1.val[1]);

                x[k * 4 + 0] = u0.val[0];
                x[k * 4 + 1] = u0.val[1];
                x[k * 4 + 2] = u1.val[0];
                x[k * 4 + 3] = u1.val[1];
            }

            /* Round 1 */
            VFF(a, b, c, d, x[0], s11, 0xd76aa478u);
            VFF(d, a, b, c, x[1], s12, 0xe8c7b756u);
            VFF(c, d, a, b, x[2], s13, 0x242070dbu);
            VFF(b, c, d, a, x[3], s14, 0xc1bdceeeu);
            VFF(a, b, c, d, x[4], s11, 0xf57c0fafu);
            VFF(d, a, b, c, x[5], s12, 0x4787c62au);
            VFF(c, d, a, b, x[6], s13, 0xa8304613u);
            VFF(b, c, d, a, x[7], s14, 0xfd469501u);
            VFF(a, b, c, d, x[8], s11, 0x698098d8u);
            VFF(d, a, b, c, x[9], s12, 0x8b44f7afu);
            VFF(c, d, a, b, x[10], s13, 0xffff5bb1u);
            VFF(b, c, d, a, x[11], s14, 0x895cd7beu);
            VFF(a, b, c, d, x[12], s11, 0x6b901122u);
            VFF(d, a, b, c, x[13], s12, 0xfd987193u);
            VFF(c, d, a, b, x[14], s13, 0xa679438eu);
            VFF(b, c, d, a, x[15], s14, 0x49b40821u);

            /* Round 2 */
            VGG(a, b, c, d, x[1], s21, 0xf61e2562u);
            VGG(d, a, b, c, x[6], s22, 0xc040b340u);
            VGG(c, d, a, b, x[11], s23, 0x265e5a51u);
            VGG(b, c, d, a, x[0], s24, 0xe9b6c7aau);
            VGG(a, b, c, d, x[5], s21, 0xd62f105du);
            VGG(d, a, b, c, x[10], s22, 0x02441453u);
            VGG(c, d, a, b, x[15], s23, 0xd8a1e681u);
            VGG(b, c, d, a, x[4], s24, 0xe7d3fbc8u);
            VGG(a, b, c, d, x[9], s21, 0x21e1cde6u);
            VGG(d, a, b, c, x[14], s22, 0xc33707d6u);
            VGG(c, d, a, b, x[3], s23, 0xf4d50d87u);
            VGG(b, c, d, a, x[8], s24, 0x455a14edu);
            VGG(a, b, c, d, x[13], s21, 0xa9e3e905u);
            VGG(d, a, b, c, x[2], s22, 0xfcefa3f8u);
            VGG(c, d, a, b, x[7], s23, 0x676f02d9u);
            VGG(b, c, d, a, x[12], s24, 0x8d2a4c8au);

            /* Round 3 */
            VHH(a, b, c, d, x[5], s31, 0xfffa3942u);
            VHH(d, a, b, c, x[8], s32, 0x8771f681u);
            VHH(c, d, a, b, x[11], s33, 0x6d9d6122u);
            VHH(b, c, d, a, x[14], s34, 0xfde5380cu);
            VHH(a, b, c, d, x[1], s31, 0xa4beea44u);
            VHH(d, a, b, c, x[4], s32, 0x4bdecfa9u);
            VHH(c, d, a, b, x[7], s33, 0xf6bb4b60u);
            VHH(b, c, d, a, x[10], s34, 0xbebfbc70u);
            VHH(a, b, c, d, x[13], s31, 0x289b7ec6u);
            VHH(d, a, b, c, x[0], s32, 0xeaa127fau);
            VHH(c, d, a, b, x[3], s33, 0xd4ef3085u);
            VHH(b, c, d, a, x[6], s34, 0x04881d05u);
            VHH(a, b, c, d, x[9], s31, 0xd9d4d039u);
            VHH(d, a, b, c, x[12], s32, 0xe6db99e5u);
            VHH(c, d, a, b, x[15], s33, 0x1fa27cf8u);
            VHH(b, c, d, a, x[2], s34, 0xc4ac5665u);

            /* Round 4 */
            VII(a, b, c, d, x[0], s41, 0xf4292244u);
            VII(d, a, b, c, x[7], s42, 0x432aff97u);
            VII(c, d, a, b, x[14], s43, 0xab9423a7u);
            VII(b, c, d, a, x[5], s44, 0xfc93a039u);
            VII(a, b, c, d, x[12], s41, 0x655b59c3u);
            VII(d, a, b, c, x[3], s42, 0x8f0ccc92u);
            VII(c, d, a, b, x[10], s43, 0xffeff47du);
            VII(b, c, d, a, x[1], s44, 0x85845dd1u);
            VII(a, b, c, d, x[8], s41, 0x6fa87e4fu);
            VII(d, a, b, c, x[15], s42, 0xfe2ce6e0u);
            VII(c, d, a, b, x[6], s43, 0xa3014314u);
            VII(b, c, d, a, x[13], s44, 0x4e0811a1u);
            VII(a, b, c, d, x[4], s41, 0xf7537e82u);
            VII(d, a, b, c, x[11], s42, 0xbd3af235u);
            VII(c, d, a, b, x[2], s43, 0x2ad7d2bbu);
            VII(b, c, d, a, x[9], s44, 0xeb86d391u);

            A = vaddq_u32(A, a);
            B = vaddq_u32(B, b);
            C = vaddq_u32(C, c);
            D = vaddq_u32(D, d);
        }

        // 输出 (小头转大头)
        uint32_t A_vals[4], B_vals[4], C_vals[4], D_vals[4];
        vst1q_u32(A_vals, A);
        vst1q_u32(B_vals, B);
        vst1q_u32(C_vals, C);
        vst1q_u32(D_vals, D);

        for (int i = 0; i < batch_count; i++) {
            uint32_t a_val = A_vals[i];
            uint32_t b_val = B_vals[i];
            uint32_t c_val = C_vals[i];
            uint32_t d_val = D_vals[i];

            states[batch_start + i][0] = ((a_val & 0xff) << 24) | ((a_val & 0xff00) << 8) | ((a_val & 0xff0000) >> 8) | ((a_val & 0xff000000) >> 24);
            states[batch_start + i][1] = ((b_val & 0xff) << 24) | ((b_val & 0xff00) << 8) | ((b_val & 0xff0000) >> 8) | ((b_val & 0xff000000) >> 24);
            states[batch_start + i][2] = ((c_val & 0xff) << 24) | ((c_val & 0xff00) << 8) | ((c_val & 0xff0000) >> 8) | ((c_val & 0xff000000) >> 24);
            states[batch_start + i][3] = ((d_val & 0xff) << 24) | ((d_val & 0xff00) << 8) | ((d_val & 0xff0000) >> 8) | ((d_val & 0xff000000) >> 24);
        }

        // 栈缓冲区无需释放, 仅清理堆回退
        for (int i = 0; i < batch_count; i++) {
            if (on_heap[i]) delete[] padded[i];
        }
    }
}
