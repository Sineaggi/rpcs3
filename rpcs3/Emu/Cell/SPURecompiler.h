#pragma once

#include "Emu/Cell/SPUOpcodes.h"
#include "Emu/Memory/Memory.h"
#include "Emu/Cell/SPUThread.h"
#include "Emu/SysCalls/SysCalls.h"

#define ASMJIT_STATIC

#include "asmjit.h"

using namespace asmjit;
using namespace asmjit::host;

#define UNIMPLEMENTED() UNK(__FUNCTION__)

struct SPUImmTable
{
	__m128i s19_to_s32[1 << 19];
	__m128i fsmbi_mask[1 << 16];
	__m128i u8_to_u8[256];
	__m128 scale_to_float[256];
	__m128 scale_to_int[256];
	__m128i min_int;
	__m128i max_int;

	SPUImmTable()
	{
		// signed numbers table
		for (u32 i = 0; i < sizeof(s19_to_s32) / sizeof(__m128i); i++)
		{
			const u32 v = (i & 0x40000) ? (i | 0xfff80000) : i;
			s19_to_s32[i].m128i_i32[0] = v;
			s19_to_s32[i].m128i_i32[1] = v;
			s19_to_s32[i].m128i_i32[2] = v;
			s19_to_s32[i].m128i_i32[3] = v;
		}
		// FSMBI mask table
		for (u32 i = 0; i < sizeof(fsmbi_mask) / sizeof(__m128i); i++)
		{
			for (u32 j = 0; j < 16; j++)
			{
				fsmbi_mask[i].m128i_i8[j] = ((i >> j) & 0x1) ? 0xff : 0;
			}
		}
		// scale table for (u)int -> float conversion
		for (s32 i = 0; i < sizeof(scale_to_float) / sizeof(__m128); i++)
		{
			const float v = pow(2, i - 155);
			scale_to_float[i].m128_f32[0] = v;
			scale_to_float[i].m128_f32[1] = v;
			scale_to_float[i].m128_f32[2] = v;
			scale_to_float[i].m128_f32[3] = v;
		}
		// scale table for float -> (u)int conversion
		for (s32 i = 0; i < sizeof(scale_to_int) / sizeof(__m128); i++)
		{
			const float v = pow(2, 173 - i);
			scale_to_int[i].m128_f32[0] = v;
			scale_to_int[i].m128_f32[1] = v;
			scale_to_int[i].m128_f32[2] = v;
			scale_to_int[i].m128_f32[3] = v;
		}
		// sign bit
		min_int.m128i_u32[0] = 0x80000000;
		min_int.m128i_u32[1] = 0x80000000;
		min_int.m128i_u32[2] = 0x80000000;
		min_int.m128i_u32[3] = 0x80000000;
		//
		max_int.m128i_u32[0] = 0x7fffffff;
		max_int.m128i_u32[1] = 0x7fffffff;
		max_int.m128i_u32[2] = 0x7fffffff;
		max_int.m128i_u32[3] = 0x7fffffff;
		// table for byte consts
		for (u32 i = 0; i < sizeof(u8_to_u8) / sizeof(__m128i); i++)
		{
			for (u32 j = 0; j < 16; j++)
			{
				u8_to_u8[i].m128i_u8[j] = i;
			}
		}
	}
};

extern const SPUImmTable g_spu_imm;

class SPURecompiler;

class SPURecompilerCore : public CPUDecoder
{
	SPURecompiler* m_enc;
	SPUThread& CPU;

public:
	SPUInterpreter* inter;
	JitRuntime runtime;
	bool first;

	struct SPURecEntry
	{
		//u16 host; // absolute position of first instruction of current block (not used now)
		u16 count; // count of instructions compiled from current point (and to be checked)
		u32 valid; // copy of valid opcode for validation
		void* pointer; // pointer to executable memory object
	};

	SPURecEntry entry[0x10000];

	SPURecompilerCore(SPUThread& cpu);

	~SPURecompilerCore();

	void Compile(u16 pos);

	virtual void Decode(const u32 code);

	virtual u8 DecodeMemory(const u64 address);
};

#define c (*compiler)

#define cpu_xmm(x) oword_ptr(*cpu_var, (sizeof((*(SPUThread*)nullptr).x) == 16) ? offsetof(SPUThread, x) : throw "sizeof("#x") != 16")
#define cpu_qword(x) qword_ptr(*cpu_var, (sizeof((*(SPUThread*)nullptr).x) == 8) ? offsetof(SPUThread, x) : throw "sizeof("#x") != 8")
#define cpu_dword(x) dword_ptr(*cpu_var, (sizeof((*(SPUThread*)nullptr).x) == 4) ? offsetof(SPUThread, x) : throw "sizeof("#x") != 4")
#define cpu_word(x) word_ptr(*cpu_var, (sizeof((*(SPUThread*)nullptr).x) == 2) ? offsetof(SPUThread, x) : throw "sizeof("#x") != 2")
#define cpu_byte(x) byte_ptr(*cpu_var, (sizeof((*(SPUThread*)nullptr).x) == 1) ? offsetof(SPUThread, x) : throw "sizeof("#x") != 1")

#define imm_xmm(x) oword_ptr(*imm_var, offsetof(SPUImmTable, x))

#define LOG_OPCODE(...) //ConLog.Write("Compiled "__FUNCTION__"(): "__VA_ARGS__)

#define LOG3_OPCODE(...) //ConLog.Write("Linked "__FUNCTION__"(): "__VA_ARGS__)

#define LOG4_OPCODE(...) //c.addComment(fmt::Format("SPU info: "__FUNCTION__"(): "__VA_ARGS__).c_str())

#define WRAPPER_BEGIN(a0, a1, a2, a3) struct opwr_##a0 \
{ \
	static void opcode(u32 a0, u32 a1, u32 a2, u32 a3) \
{ \
	SPUThread& CPU = *(SPUThread*)GetCurrentCPUThread();

#define WRAPPER_END(a0, a1, a2, a3) /*LOG2_OPCODE();*/ } \
}; \
	/*XmmRelease();*/ \
	if (#a0[0] == 'r') XmmInvalidate(a0); \
	if (#a1[0] == 'r') XmmInvalidate(a1); \
	if (#a2[0] == 'r') XmmInvalidate(a2); \
	if (#a3[0] == 'r') XmmInvalidate(a3); \
	X86X64CallNode* call##a0 = c.call(imm_ptr(&opwr_##a0::opcode), kFuncConvHost, FuncBuilder4<FnVoid, u32, u32, u32, u32>()); \
	call##a0->setArg(0, imm_u(a0)); \
	call##a0->setArg(1, imm_u(a1)); \
	call##a0->setArg(2, imm_u(a2)); \
	call##a0->setArg(3, imm_u(a3)); \
	LOG3_OPCODE(/*#a0"=%d, "#a1"=%d, "#a2"=%d, "#a3"=%d", a0, a1, a2, a3*/);


class SPURecompiler : public SPUOpcodes
{
private:
	SPUThread& CPU;
	SPURecompilerCore& rec;

public:
	Compiler* compiler;
	bool do_finalize;
	// input:
	GpVar* cpu_var;
	GpVar* ls_var;
	GpVar* imm_var;
	// (input) output:
	GpVar* pos_var;
	// temporary:
	GpVar* addr;
	GpVar* qw0;
	GpVar* qw1;

	struct XmmLink
	{
		XmmVar* data;
		s8 reg;
		bool taken;
		mutable bool got;

		XmmLink()
			: data(nullptr)
			, reg(-1)
			, taken(false)
		{
		}

		const XmmVar& get() const
		{
			assert(data);
			got = true;
			return *data;
		}
	} xmm_var[16];

	SPURecompiler(SPUThread& cpu, SPURecompilerCore& rec)
		: CPU(cpu)
		, rec(rec)
		, compiler(nullptr)
	{
	}

	const XmmLink& XmmAlloc() // get empty xmm register
	{
		for (u32 i = 0; i < 16; i++)
		{
			if ((xmm_var[i].reg == -1) && !xmm_var[i].taken)
			{
				xmm_var[i].taken = true;
				xmm_var[i].got = false;
				LOG4_OPCODE("free reg taken (i=%d)", i);
				xmm_var[i].reg = -1;
				return xmm_var[i];
			}
		}
		for (u32 i = 0; i < 16; i++)
		{
			if (!xmm_var[i].taken)
			{
				//(saving cached data?)
				//c.movaps(cpu_xmm(GPR[xmm_var[i].reg]), *xmm_var[i].data);
				xmm_var[i].taken = true;
				xmm_var[i].got = false;
				LOG4_OPCODE("cached reg taken (i=%d): GPR[%d] lost", i, xmm_var[i].reg);
				xmm_var[i].reg = -1;
				return xmm_var[i];
			}
		}
		assert(false);
		return *(XmmLink*)nullptr;
	}

	const XmmLink& XmmGet(s8 reg) // get xmm register with specific SPU reg
	{
		assert(reg >= 0);
		XmmLink* res = nullptr;
		for (u32 i = 0; i < 16; i++)
		{
			if (xmm_var[i].reg == reg)
			{
				res = &xmm_var[i];
				if (xmm_var[i].taken) throw "XmmGet(): xmm_var is taken";
				xmm_var[i].taken = true;
				xmm_var[i].got = false;
				xmm_var[i].reg = -1;
				for (u32 j = i + 1; j < 16; j++)
				{
					if (xmm_var[j].reg == reg) throw "XmmGet(): xmm_var duplicate";
				}
				LOG4_OPCODE("cached GPR[%d] used (i=%d)", reg, i);
				break;
			}
		}
		if (!res)
		{
			res = &(XmmLink&)XmmAlloc();
			c.movaps(*res->data, cpu_xmm(GPR[reg]));
			res->reg = -1;
			LOG4_OPCODE("* cached GPR[%d] not found", reg);
		}
		return *res;
	}

	const XmmLink& XmmCopy(const XmmLink& from) // XmmAlloc + mov
	{
		XmmLink* res = &(XmmLink&)XmmAlloc();
		c.movaps(*res->data, *from.data);
		res->reg = -1;
		LOG4_OPCODE("*");
		return *res;
	}

	const XmmLink* XmmRead(const s8 reg) const // get xmm register with specific SPU reg or nullptr
	{
		assert(reg >= 0);
		for (u32 i = 0; i < 16; i++)
		{
			if (xmm_var[i].reg == reg)
			{
				if (xmm_var[i].got && xmm_var[i].taken) throw "XmmRead(): wrong reuse";
				LOG4_OPCODE("GPR[%d] has been read (i=%d)", reg, i);
				return &xmm_var[i];
			}
		}
		LOG4_OPCODE("GPR[%d] not found", reg);
		return nullptr;
	}

	void XmmInvalidate(const s8 reg) // invalidate cached register
	{
		assert(reg >= 0);
		for (u32 i = 0; i < 16; i++)
		{
			if (xmm_var[i].reg == reg)
			{
				if (xmm_var[i].taken) throw "XmmInvalidate(): xmm_var is taken";
				LOG4_OPCODE("GPR[%d] invalidated (i=%d)", reg, i);
				xmm_var[i].reg = -1;
			}
		}
	}

	void XmmFinalize(const XmmLink& var, s8 reg = -1)
	{
		// invalidation
		if (reg >= 0) for (u32 i = 0; i < 16; i++)
		{
			if (xmm_var[i].reg == reg)
			{
				LOG4_OPCODE("GPR[%d] invalidated (i=%d)", reg, i);
				xmm_var[i].reg = -1;
			}
		}
		for (u32 i = 0; i < 16; i++)
		{
			if (xmm_var[i].data == var.data)
			{
				assert(xmm_var[i].taken);
				// save immediately:
				if (reg >= 0)
				{
					c.movaps(cpu_xmm(GPR[reg]), *xmm_var[i].data);
				}
				else
				{
				}
				LOG4_OPCODE("GPR[%d] finalized (i=%d), GPR[%d] replaced", reg, i, xmm_var[i].reg);
				// (to disable caching:)
				reg = -1;
				xmm_var[i].reg = reg;
				xmm_var[i].taken = false;
				return;
			}
		}
		assert(false);
	}

	void XmmRelease()
	{
		for (u32 i = 0; i < 16; i++)
		{
			if (xmm_var[i].reg >= 0)
			{
				//c.movaps(cpu_xmm(GPR[xmm_var[i].reg]), *xmm_var[i].data);
				LOG4_OPCODE("GPR[%d] released (i=%d)", xmm_var[i].reg, i);
				xmm_var[i].reg = -1;
			}
		}
	}

private:
	//0 - 10
	void STOP(u32 code)
	{
		struct STOP_wrapper
		{
			static void STOP(u32 code)
			{
				SPUThread& CPU = *(SPUThread*)GetCurrentCPUThread();
				CPU.DoStop(code);
				LOG2_OPCODE();
			}
		};
		c.mov(cpu_qword(PC), (u32)CPU.PC);
		X86X64CallNode* call = c.call(imm_ptr(&STOP_wrapper::STOP), kFuncConvHost, FuncBuilder1<FnVoid, u32>());
		call->setArg(0, imm_u(code));
		c.mov(*pos_var, (CPU.PC >> 2) + 1);
		do_finalize = true;
		LOG_OPCODE();
	}
	void LNOP()
	{
		LOG_OPCODE();
	}
	void SYNC(u32 Cbit)
	{
		c.mov(cpu_qword(PC), (u32)CPU.PC);
		// This instruction must be used following a store instruction that modifies the instruction stream.
		c.mfence();
		c.mov(*pos_var, (CPU.PC >> 2) + 1);
		do_finalize = true;
		LOG_OPCODE();
	}
	void DSYNC()
	{
		// This instruction forces all earlier load, store, and channel instructions to complete before proceeding.
		c.mfence();
		LOG_OPCODE();
	}
	void MFSPR(u32 rt, u32 sa)
	{
		WRAPPER_BEGIN(rt, sa, yy, zz);
		//If register is a dummy register (register labeled 0x0)
		if(sa == 0x0)
		{
			CPU.GPR[rt]._u128.hi = 0x0;
			CPU.GPR[rt]._u128.lo = 0x0;
		}
		else
		{
			CPU.GPR[rt]._u128.hi =  CPU.SPR[sa]._u128.hi;
			CPU.GPR[rt]._u128.lo =  CPU.SPR[sa]._u128.lo;
		}
		WRAPPER_END(rt, sa, 0, 0);
	}
	void RDCH(u32 rt, u32 ra)
	{
		c.mov(cpu_qword(PC), (u32)CPU.PC);
		WRAPPER_BEGIN(rt, ra, yy, zz);
		CPU.ReadChannel(CPU.GPR[rt], ra);
		WRAPPER_END(rt, ra, 0, 0);
		// TODO
	}
	void RCHCNT(u32 rt, u32 ra)
	{
		c.mov(cpu_qword(PC), (u32)CPU.PC);
		WRAPPER_BEGIN(rt, ra, yy, zz);
		CPU.GPR[rt].Reset();
		CPU.GPR[rt]._u32[3] = CPU.GetChannelCount(ra);
		WRAPPER_END(rt, ra, 0, 0);
		// TODO
	}
	void SF(u32 rt, u32 ra, u32 rb)
	{
		if (ra == rb)
		{
			// zero
			const XmmLink& v0 = XmmAlloc();
			c.xorps(v0.get(), v0.get());
			XmmFinalize(v0, rt);
		}
		else
		{
			// sub from
			const XmmLink& vb = XmmGet(rb);
			if (const XmmLink* va = XmmRead(ra))
			{
				c.psubd(vb.get(), va->get());
			}
			else
			{
				c.psubd(vb.get(), cpu_xmm(GPR[ra]));
			}
			XmmFinalize(vb, rt);
		}
		LOG_OPCODE();
	}
	void OR(u32 rt, u32 ra, u32 rb)
	{
		if (ra == rb)
		{
			// mov
			if (ra != rt)
			{
				const XmmLink& va = XmmGet(ra);
				XmmFinalize(va, rt);
			}
			// else nop
		}
		else
		{
			// or
			const XmmLink& vb = XmmGet(rb);
			if (const XmmLink* va = XmmRead(ra))
			{
				c.orps(vb.get(), va->get());
			}
			else
			{
				c.orps(vb.get(), cpu_xmm(GPR[ra]));
			}
			XmmFinalize(vb, rt);
		}
		LOG_OPCODE();
	}
	void BG(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._u32[0] = CPU.GPR[ra]._u32[0] > CPU.GPR[rb]._u32[0] ? 0 : 1;
		CPU.GPR[rt]._u32[1] = CPU.GPR[ra]._u32[1] > CPU.GPR[rb]._u32[1] ? 0 : 1;
		CPU.GPR[rt]._u32[2] = CPU.GPR[ra]._u32[2] > CPU.GPR[rb]._u32[2] ? 0 : 1;
		CPU.GPR[rt]._u32[3] = CPU.GPR[ra]._u32[3] > CPU.GPR[rb]._u32[3] ? 0 : 1;
		WRAPPER_END(rt, ra, rb, 0);

		/*XmmVar v0(c);
		if (ra == rb)
		{
			c.movaps(v0, imm_xmm(s19_to_s32[1]));
			c.movaps(cpu_xmm(GPR[rt]), v0);
		}
		else
		{
			XmmVar v1(c), v2(c);
			c.movdqa(v0, cpu_xmm(GPR[ra]));
			c.movdqa(v1, cpu_xmm(GPR[rb]));
			// compare if-greater-than
			
			c.movdqa(cpu_xmm(GPR[rt]), v0);
			
			// sign bits:
			// a b (b-a) -> (result of BG)
			// 0 0 0 -> 1
			// 0 0 1 -> 0
			// 0 1 0 -> 1
			// 0 1 1 -> 1
			// 1 0 0 -> 0
			// 1 0 1 -> 0
			// 1 1 0 -> 0
			// 1 1 1 -> 1
		}
		LOG_OPCODE();*/
	}
	void SFH(u32 rt, u32 ra, u32 rb)
	{
		if (ra == rb)
		{
			// zero
			const XmmLink& v0 = XmmAlloc();
			c.xorps(v0.get(), v0.get());
			XmmFinalize(v0, rt);
		}
		else
		{
			const XmmLink& vb = XmmGet(rb);
			if (const XmmLink* va = XmmRead(ra))
			{
				c.psubw(vb.get(), va->get());
			}
			else
			{
				c.psubw(vb.get(), cpu_xmm(GPR[ra]));
			}
			XmmFinalize(vb, rt);
		}
		LOG_OPCODE();
	}
	void NOR(u32 rt, u32 ra, u32 rb)
	{
		const XmmLink& va = XmmGet(ra);
		if (ra != rb) c.orps(va.get(), cpu_xmm(GPR[rb]));
		c.xorps(va.get(), imm_xmm(s19_to_s32[0x7ffff]));
		XmmFinalize(va, rt);
		LOG_OPCODE();
	}
	void ABSDB(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int b = 0; b < 16; b++)
			CPU.GPR[rt]._u8[b] = CPU.GPR[rb]._u8[b] > CPU.GPR[ra]._u8[b] ? CPU.GPR[rb]._u8[b] - CPU.GPR[ra]._u8[b] : CPU.GPR[ra]._u8[b] - CPU.GPR[rb]._u8[b];
		WRAPPER_END(rt, ra, rb, 0);
	}
	void ROT(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._u32[0] = (CPU.GPR[ra]._u32[0] << (CPU.GPR[rb]._u32[0] & 0x1f)) | (CPU.GPR[ra]._u32[0] >> (32 - (CPU.GPR[rb]._u32[0] & 0x1f)));
		CPU.GPR[rt]._u32[1] = (CPU.GPR[ra]._u32[1] << (CPU.GPR[rb]._u32[1] & 0x1f)) | (CPU.GPR[ra]._u32[1] >> (32 - (CPU.GPR[rb]._u32[1] & 0x1f)));
		CPU.GPR[rt]._u32[2] = (CPU.GPR[ra]._u32[2] << (CPU.GPR[rb]._u32[2] & 0x1f)) | (CPU.GPR[ra]._u32[2] >> (32 - (CPU.GPR[rb]._u32[2] & 0x1f)));
		CPU.GPR[rt]._u32[3] = (CPU.GPR[ra]._u32[3] << (CPU.GPR[rb]._u32[3] & 0x1f)) | (CPU.GPR[ra]._u32[3] >> (32 - (CPU.GPR[rb]._u32[3] & 0x1f)));
		WRAPPER_END(rt, ra, rb, 0);
	}
	void ROTM(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._u32[0] = ((0 - CPU.GPR[rb]._u32[0]) & 0x3f) < 32 ? CPU.GPR[ra]._u32[0] >> ((0 - CPU.GPR[rb]._u32[0]) & 0x3f) : 0;
		CPU.GPR[rt]._u32[1] = ((0 - CPU.GPR[rb]._u32[1]) & 0x3f) < 32 ? CPU.GPR[ra]._u32[1] >> ((0 - CPU.GPR[rb]._u32[1]) & 0x3f) : 0;
		CPU.GPR[rt]._u32[2] = ((0 - CPU.GPR[rb]._u32[2]) & 0x3f) < 32 ? CPU.GPR[ra]._u32[2] >> ((0 - CPU.GPR[rb]._u32[2]) & 0x3f) : 0;
		CPU.GPR[rt]._u32[3] = ((0 - CPU.GPR[rb]._u32[3]) & 0x3f) < 32 ? CPU.GPR[ra]._u32[3] >> ((0 - CPU.GPR[rb]._u32[3]) & 0x3f) : 0;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void ROTMA(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._i32[0] = ((0 - CPU.GPR[rb]._u32[0]) & 0x3f) < 32 ? CPU.GPR[ra]._i32[0] >> ((0 - CPU.GPR[rb]._u32[0]) & 0x3f) : CPU.GPR[ra]._i32[0] >> 31;
		CPU.GPR[rt]._i32[1] = ((0 - CPU.GPR[rb]._u32[1]) & 0x3f) < 32 ? CPU.GPR[ra]._i32[1] >> ((0 - CPU.GPR[rb]._u32[1]) & 0x3f) : CPU.GPR[ra]._i32[1] >> 31;
		CPU.GPR[rt]._i32[2] = ((0 - CPU.GPR[rb]._u32[2]) & 0x3f) < 32 ? CPU.GPR[ra]._i32[2] >> ((0 - CPU.GPR[rb]._u32[2]) & 0x3f) : CPU.GPR[ra]._i32[2] >> 31;
		CPU.GPR[rt]._i32[3] = ((0 - CPU.GPR[rb]._u32[3]) & 0x3f) < 32 ? CPU.GPR[ra]._i32[3] >> ((0 - CPU.GPR[rb]._u32[3]) & 0x3f) : CPU.GPR[ra]._i32[3] >> 31;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void SHL(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._u32[0] = (CPU.GPR[rb]._u32[0] & 0x3f) > 31 ? 0 : CPU.GPR[ra]._u32[0] << (CPU.GPR[rb]._u32[0] & 0x3f);
		CPU.GPR[rt]._u32[1] = (CPU.GPR[rb]._u32[1] & 0x3f) > 31 ? 0 : CPU.GPR[ra]._u32[1] << (CPU.GPR[rb]._u32[1] & 0x3f);
		CPU.GPR[rt]._u32[2] = (CPU.GPR[rb]._u32[2] & 0x3f) > 31 ? 0 : CPU.GPR[ra]._u32[2] << (CPU.GPR[rb]._u32[2] & 0x3f);
		CPU.GPR[rt]._u32[3] = (CPU.GPR[rb]._u32[3] & 0x3f) > 31 ? 0 : CPU.GPR[ra]._u32[3] << (CPU.GPR[rb]._u32[3] & 0x3f);
		WRAPPER_END(rt, ra, rb, 0);

		// AVX2: masking with 0x3f + VPSLLVD may be better
		/*for (u32 i = 0; i < 4; i++)
		{
			GpVar v0(c, kVarTypeUInt32);
			c.mov(v0, cpu_dword(GPR[ra]._u32[i]));
			GpVar shift(c, kVarTypeUInt32);
			c.mov(shift, cpu_dword(GPR[rb]._u32[i]));
			GpVar z(c);
			c.xor_(z, z);
			c.test(shift, 0x20);
			c.cmovnz(v0, z);
			c.shl(v0, shift);
			c.mov(cpu_dword(GPR[rt]._u32[i]), v0);
		}
		LOG_OPCODE();*/
	}
	void ROTH(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int h = 0; h < 8; h++) 
			CPU.GPR[rt]._u16[h] = (CPU.GPR[ra]._u16[h] << (CPU.GPR[rb]._u16[h] & 0xf)) | (CPU.GPR[ra]._u16[h] >> (16 - (CPU.GPR[rb]._u16[h] & 0xf)));
		WRAPPER_END(rt, ra, rb, 0);
	}
	void ROTHM(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int h = 0; h < 8; h++)
			CPU.GPR[rt]._u16[h] = ((0 - CPU.GPR[rb]._u16[h]) & 0x1f) < 16 ? CPU.GPR[ra]._u16[h] >> ((0 - CPU.GPR[rb]._u16[h]) & 0x1f) : 0;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void ROTMAH(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int h = 0; h < 8; h++)
			CPU.GPR[rt]._i16[h] = ((0 - CPU.GPR[rb]._u16[h]) & 0x1f) < 16 ? CPU.GPR[ra]._i16[h] >> ((0 - CPU.GPR[rb]._u16[h]) & 0x1f) : CPU.GPR[ra]._i16[h] >> 15;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void SHLH(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int h = 0; h < 8; h++)
			CPU.GPR[rt]._u16[h] = (CPU.GPR[rb]._u16[h] & 0x1f) > 15 ? 0 : CPU.GPR[ra]._u16[h] << (CPU.GPR[rb]._u16[h] & 0x1f);
		WRAPPER_END(rt, ra, rb, 0);
	}
	void ROTI(u32 rt, u32 ra, s32 i7)
	{
		WRAPPER_BEGIN(rt, ra, i7, zz);
		const int nRot = i7 & 0x1f;
		CPU.GPR[rt]._u32[0] = (CPU.GPR[ra]._u32[0] << nRot) | (CPU.GPR[ra]._u32[0] >> (32 - nRot));
		CPU.GPR[rt]._u32[1] = (CPU.GPR[ra]._u32[1] << nRot) | (CPU.GPR[ra]._u32[1] >> (32 - nRot));
		CPU.GPR[rt]._u32[2] = (CPU.GPR[ra]._u32[2] << nRot) | (CPU.GPR[ra]._u32[2] >> (32 - nRot));
		CPU.GPR[rt]._u32[3] = (CPU.GPR[ra]._u32[3] << nRot) | (CPU.GPR[ra]._u32[3] >> (32 - nRot));
		WRAPPER_END(rt, ra, i7, 0);
	}
	void ROTMI(u32 rt, u32 ra, s32 i7)
	{
		const int nRot = (0 - i7) & 0x3f;
		if (nRot > 31)
		{
			// zero
			const XmmLink& v0 = XmmAlloc();
			c.xorps(v0.get(), v0.get());
			XmmFinalize(v0, rt);
		}
		else if (nRot == 0)
		{
			// mov
			if (ra != rt)
			{
				const XmmLink& va = XmmGet(ra);
				XmmFinalize(va, rt);
			}
			// else nop
		}
		else
		{
			// shift right logical
			const XmmLink& va = XmmGet(ra);
			c.psrld(va.get(), nRot);
			XmmFinalize(va, rt);
		}
		LOG_OPCODE();
	}
	void ROTMAI(u32 rt, u32 ra, s32 i7)
	{
		const int nRot = (0 - i7) & 0x3f;
		if (nRot == 0)
		{
			// mov
			if (ra != rt)
			{
				const XmmLink& va = XmmGet(ra);
				XmmFinalize(va, rt);
			}
			// else nop
		}
		else
		{
			// shift right arithmetical
			const XmmLink& va = XmmGet(ra);
			c.psrad(va.get(), nRot);
			XmmFinalize(va, rt);
		}
		LOG_OPCODE();
	}
	void SHLI(u32 rt, u32 ra, s32 i7)
	{
		const int s = i7 & 0x3f;
		if (s > 31)
		{
			// zero
			const XmmLink& v0 = XmmAlloc();
			c.xorps(v0.get(), v0.get());
			XmmFinalize(v0, rt);
		}
		else if (s == 0)
		{
			// mov
			if (ra != rt)
			{
				const XmmLink& va = XmmGet(ra);
				XmmFinalize(va, rt);
			}
			// else nop
		}
		else
		{
			// shift left
			const XmmLink& va = XmmGet(ra);
			c.pslld(va.get(), s);
			XmmFinalize(va, rt);
		}
		LOG_OPCODE();
	}
	void ROTHI(u32 rt, u32 ra, s32 i7)
	{
		WRAPPER_BEGIN(rt, ra, i7, zz);
		const int nRot = i7 & 0xf;
		for (int h = 0; h < 8; h++)
			CPU.GPR[rt]._u16[h] = (CPU.GPR[ra]._u16[h] << nRot) | (CPU.GPR[ra]._u16[h] >> (16 - nRot));
		WRAPPER_END(rt, ra, i7, 0);
	}
	void ROTHMI(u32 rt, u32 ra, s32 i7)
	{
		WRAPPER_BEGIN(rt, ra, i7, zz);
		const int nRot = (0 - (s32)i7) & 0x1f;
		for (int h = 0; h < 8; h++)
			CPU.GPR[rt]._u16[h] = nRot < 16 ? CPU.GPR[ra]._u16[h] >> nRot : 0;
		WRAPPER_END(rt, ra, i7, 0);
	}
	void ROTMAHI(u32 rt, u32 ra, s32 i7)
	{
		WRAPPER_BEGIN(rt, ra, i7, zz);
		const int nRot = (0 - (s32)i7) & 0x1f;
		for (int h = 0; h < 8; h++)
			CPU.GPR[rt]._i16[h] = nRot < 16 ? CPU.GPR[ra]._i16[h] >> nRot : CPU.GPR[ra]._i16[h] >> 15;
		WRAPPER_END(rt, ra, i7, 0);
	}
	void SHLHI(u32 rt, u32 ra, s32 i7)
	{
		WRAPPER_BEGIN(rt, ra, i7, zz);
		const int nRot = i7 & 0x1f;
		for (int h = 0; h < 8; h++)
			CPU.GPR[rt]._u16[h] = nRot > 15 ? 0 : CPU.GPR[ra]._u16[h] << nRot;
		WRAPPER_END(rt, ra, i7, 0);
	}
	void A(u32 rt, u32 ra, u32 rb)
	{
		if (ra == rb)
		{
			const XmmLink& vb = XmmGet(rb);
			c.paddd(vb.get(), vb.get());
			XmmFinalize(vb, rt);
		}
		else
		{
			const XmmLink& vb = XmmGet(rb);
			if (const XmmLink* va = XmmRead(ra))
			{
				c.paddd(vb.get(), va->get());
			}
			else
			{
				c.paddd(vb.get(), cpu_xmm(GPR[ra]));
			}
			XmmFinalize(vb, rt);
		}
		LOG_OPCODE();
	}
	void AND(u32 rt, u32 ra, u32 rb)
	{
		if (ra == rb)
		{
			if (rt != ra)
			{
				// mov
				const XmmLink& va = XmmGet(ra);
				XmmFinalize(va, rt);
			}
			// else nop
		}
		else
		{
			// and
			const XmmLink& vb = XmmGet(rb);
			if (const XmmLink* va = XmmRead(ra))
			{
				c.andps(vb.get(), va->get());
			}
			else
			{
				c.andps(vb.get(), cpu_xmm(GPR[ra]));
			}
			XmmFinalize(vb, rt);
		}
		LOG_OPCODE();
	}
	void CG(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._u32[0] = ((CPU.GPR[ra]._u32[0] + CPU.GPR[rb]._u32[0]) < CPU.GPR[ra]._u32[0]) ? 1 : 0;
		CPU.GPR[rt]._u32[1] = ((CPU.GPR[ra]._u32[1] + CPU.GPR[rb]._u32[1]) < CPU.GPR[ra]._u32[1]) ? 1 : 0;
		CPU.GPR[rt]._u32[2] = ((CPU.GPR[ra]._u32[2] + CPU.GPR[rb]._u32[2]) < CPU.GPR[ra]._u32[2]) ? 1 : 0;
		CPU.GPR[rt]._u32[3] = ((CPU.GPR[ra]._u32[3] + CPU.GPR[rb]._u32[3]) < CPU.GPR[ra]._u32[3]) ? 1 : 0;
		WRAPPER_END(rt, ra, rb, 0);
		// TODO
	}
	void AH(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int h = 0; h < 8; h++)
			CPU.GPR[rt]._u16[h] = CPU.GPR[ra]._u16[h] + CPU.GPR[rb]._u16[h];
		WRAPPER_END(rt, ra, rb, 0);
	}
	void NAND(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._u32[0] = ~(CPU.GPR[ra]._u32[0] & CPU.GPR[rb]._u32[0]);
		CPU.GPR[rt]._u32[1] = ~(CPU.GPR[ra]._u32[1] & CPU.GPR[rb]._u32[1]);
		CPU.GPR[rt]._u32[2] = ~(CPU.GPR[ra]._u32[2] & CPU.GPR[rb]._u32[2]);
		CPU.GPR[rt]._u32[3] = ~(CPU.GPR[ra]._u32[3] & CPU.GPR[rb]._u32[3]);
		WRAPPER_END(rt, ra, rb, 0);
	}
	void AVGB(u32 rt, u32 ra, u32 rb)
	{
		const XmmLink& vb = XmmGet(rb);
		if (const XmmLink* va = XmmRead(ra))
		{
			c.pavgb(vb.get(), va->get());
		}
		else
		{
			c.pavgb(vb.get(), cpu_xmm(GPR[ra]));
		}
		XmmFinalize(vb, rt);
	}
	void MTSPR(u32 rt, u32 sa)
	{
		UNIMPLEMENTED();
		if(sa != 0)
		{
			CPU.SPR[sa]._u128.hi = CPU.GPR[rt]._u128.hi;
			CPU.SPR[sa]._u128.lo = CPU.GPR[rt]._u128.lo;
		}
	}
	void WRCH(u32 ra, u32 rt)
	{
		c.mov(cpu_qword(PC), (u32)CPU.PC);
		WRAPPER_BEGIN(ra, rt, yy, zz);
		CPU.WriteChannel(ra, CPU.GPR[rt]);
		WRAPPER_END(ra, rt, 0, 0);
		// TODO

		/*GpVar v(c, kVarTypeUInt32);
		c.mov(v, cpu_dword(GPR[rt]._u32[3]));
		switch (ra)
		{
		case MFC_LSA:
			c.mov(cpu_dword(MFC1.LSA.m_value[0]), v);
			break;
		
		case MFC_EAH:
			c.mov(cpu_dword(MFC1.EAH.m_value[0]), v);
			break;

		case MFC_EAL:
			c.mov(cpu_dword(MFC1.EAL.m_value[0]), v);
			break;

		case MFC_Size:
			c.mov(cpu_word(MFC1.Size_Tag.m_val16[1]), v);
			break;

		case MFC_TagID:
			c.mov(cpu_word(MFC1.Size_Tag.m_val16[0]), v);
			break;

		default:
		{
			X86X64CallNode* call = c.call(imm_ptr(&WRCH_wrapper::WRCH), kFuncConvHost, FuncBuilder2<FnVoid, u32, u32>());
			call->setArg(0, imm_u(ra));
			call->setArg(1, v);
		}
		}*/
	}
	void BIZ(u32 rt, u32 ra)
	{
		c.mov(cpu_qword(PC), (u32)CPU.PC);
		do_finalize = true;

		c.mov(*addr, (u32)CPU.PC + 4);
		c.mov(*pos_var, cpu_dword(GPR[ra]._u32[3]));
		c.cmp(cpu_dword(GPR[rt]._u32[3]), 0);
		c.cmovne(*pos_var, *addr);
		c.shr(*pos_var, 2);
		LOG_OPCODE();
	}
	void BINZ(u32 rt, u32 ra)
	{
		c.mov(cpu_qword(PC), (u32)CPU.PC);
		do_finalize = true;

		c.mov(*addr, (u32)CPU.PC + 4);
		c.mov(*pos_var, cpu_dword(GPR[ra]._u32[3]));
		c.cmp(cpu_dword(GPR[rt]._u32[3]), 0);
		c.cmove(*pos_var, *addr);
		c.shr(*pos_var, 2);
		LOG_OPCODE();
	}
	void BIHZ(u32 rt, u32 ra)
	{
		c.mov(cpu_qword(PC), (u32)CPU.PC);
		do_finalize = true;

		c.mov(*addr, (u32)CPU.PC + 4);
		c.mov(*pos_var, cpu_dword(GPR[ra]._u32[3]));
		c.cmp(cpu_word(GPR[rt]._u16[6]), 0);
		c.cmovne(*pos_var, *addr);
		c.shr(*pos_var, 2);
		LOG_OPCODE();
	}
	void BIHNZ(u32 rt, u32 ra)
	{
		c.mov(cpu_qword(PC), (u32)CPU.PC);
		do_finalize = true;

		c.mov(*addr, (u32)CPU.PC + 4);
		c.mov(*pos_var, cpu_dword(GPR[ra]._u32[3]));
		c.cmp(cpu_word(GPR[rt]._u16[6]), 0);
		c.cmove(*pos_var, *addr);
		c.shr(*pos_var, 2);
		LOG_OPCODE();
	}
	void STOPD(u32 rc, u32 ra, u32 rb)
	{
		UNIMPLEMENTED();
		Emu.Pause();
	}
	void STQX(u32 rt, u32 ra, u32 rb)
	{
		c.mov(*addr, cpu_dword(GPR[ra]._u32[3]));
		if (ra == rb)
		{
			c.add(*addr, *addr);
		}
		else
		{
			c.add(*addr, cpu_dword(GPR[rb]._u32[3]));
		}
		c.and_(*addr, 0x3fff0);
		c.mov(*qw0, cpu_qword(GPR[rt]._u64[0]));
		c.mov(*qw1, cpu_qword(GPR[rt]._u64[1]));
		c.bswap(*qw0);
		c.bswap(*qw1);
		c.mov(qword_ptr(*ls_var, *addr, 0, 0), *qw1);
		c.mov(qword_ptr(*ls_var, *addr, 0, 8), *qw0);
		LOG_OPCODE();
	}
	void BI(u32 ra)
	{
		c.mov(cpu_qword(PC), (u32)CPU.PC);
		do_finalize = true;

		c.mov(*pos_var, cpu_dword(GPR[ra]._u32[3]));
		c.shr(*pos_var, 2);
		LOG_OPCODE();
	}
	void BISL(u32 rt, u32 ra)
	{
		c.mov(cpu_qword(PC), (u32)CPU.PC);
		do_finalize = true;

		c.xor_(*pos_var, *pos_var);
		c.mov(cpu_dword(GPR[rt]._u32[0]), *pos_var);
		c.mov(cpu_dword(GPR[rt]._u32[1]), *pos_var);
		c.mov(cpu_dword(GPR[rt]._u32[2]), *pos_var);
		c.mov(*pos_var, cpu_dword(GPR[ra]._u32[3]));
		c.mov(cpu_dword(GPR[rt]._u32[3]), (u32)CPU.PC + 4);
		c.shr(*pos_var, 2);
		LOG_OPCODE();
	}
	void IRET(u32 ra)
	{
		UNIMPLEMENTED();
		//SetBranch(SRR0);
	}
	void BISLED(u32 rt, u32 ra)
	{
		UNIMPLEMENTED();
	}
	void HBR(u32 p, u32 ro, u32 ra)
	{
		LOG_OPCODE();
	}
	void GB(u32 rt, u32 ra)
	{
		WRAPPER_BEGIN(rt, ra, yy, zz);
		CPU.GPR[rt]._u32[3] =	(CPU.GPR[ra]._u32[0] & 1) |
										((CPU.GPR[ra]._u32[1] & 1) << 1) |
										((CPU.GPR[ra]._u32[2] & 1) << 2) |
										((CPU.GPR[ra]._u32[3] & 1) << 3);
		CPU.GPR[rt]._u32[2] = 0;
		CPU.GPR[rt]._u64[0] = 0;
		WRAPPER_END(rt, ra, 0, 0);
		// TODO
	}
	void GBH(u32 rt, u32 ra)
	{
		WRAPPER_BEGIN(rt, ra, yy, zz);
		u32 temp = 0;
		for (int h = 0; h < 8; h++)
			temp |= (CPU.GPR[ra]._u16[h] & 1) << h;
		CPU.GPR[rt]._u32[3] = temp;
		CPU.GPR[rt]._u32[2] = 0;
		CPU.GPR[rt]._u64[0] = 0;
		WRAPPER_END(rt, ra, 0, 0);
	}
	void GBB(u32 rt, u32 ra)
	{
		WRAPPER_BEGIN(rt, ra, yy, zz);
		u32 temp = 0;
		for (int b = 0; b < 16; b++)
			temp |= (CPU.GPR[ra]._u8[b] & 1) << b;
		CPU.GPR[rt]._u32[3] = temp;
		CPU.GPR[rt]._u32[2] = 0;
		CPU.GPR[rt]._u64[0] = 0;
		WRAPPER_END(rt, ra, 0, 0);
	}
	void FSM(u32 rt, u32 ra)
	{
		WRAPPER_BEGIN(rt, ra, yy, zz);
		const u32 pref = CPU.GPR[ra]._u32[3];
		for (int w = 0; w < 4; w++)
			CPU.GPR[rt]._u32[w] = (pref & (1 << w)) ? ~0 : 0;
		WRAPPER_END(rt, ra, 0, 0);
	}
	void FSMH(u32 rt, u32 ra)
	{
		WRAPPER_BEGIN(rt, ra, yy, zz);
		const u32 pref = CPU.GPR[ra]._u32[3];
		for (int h = 0; h < 8; h++)
			CPU.GPR[rt]._u16[h] = (pref & (1 << h)) ? ~0 : 0;
		WRAPPER_END(rt, ra, 0, 0);
	}
	void FSMB(u32 rt, u32 ra)
	{
		WRAPPER_BEGIN(rt, ra, yy, zz);
		const u32 pref = CPU.GPR[ra]._u32[3];
		for (int b = 0; b < 16; b++)
			CPU.GPR[rt]._u8[b] = (pref & (1 << b)) ? ~0 : 0;
		WRAPPER_END(rt, ra, 0, 0);
	}
	void FREST(u32 rt, u32 ra)
	{
		const XmmLink& vr = XmmAlloc();
		if (const XmmLink* va = XmmRead(ra))
		{
			c.rcpps(vr.get(), va->get());
		}
		else
		{
			c.rcpps(vr.get(), cpu_xmm(GPR[ra]));
		}
		XmmFinalize(vr, rt);
		LOG_OPCODE();
	}
	void FRSQEST(u32 rt, u32 ra)
	{
		const XmmLink& vr = XmmGet(ra);
		c.andps(vr.get(), imm_xmm(max_int)); // abs
		c.rsqrtps(vr.get(), vr.get());
		XmmFinalize(vr, rt);
		LOG_OPCODE();
	}
	void LQX(u32 rt, u32 ra, u32 rb)
	{
		c.mov(*addr, cpu_dword(GPR[ra]._u32[3]));
		if (ra == rb)
		{
			c.add(*addr, *addr);
		}
		else
		{
			c.add(*addr, cpu_dword(GPR[rb]._u32[3]));
		}
		c.and_(*addr, 0x3fff0);
		c.mov(*qw0, qword_ptr(*ls_var, *addr, 0, 0));
		c.mov(*qw1, qword_ptr(*ls_var, *addr, 0, 8));
		c.bswap(*qw0);
		c.bswap(*qw1);
		c.mov(cpu_qword(GPR[rt]._u64[0]), *qw1);
		c.mov(cpu_qword(GPR[rt]._u64[1]), *qw0);
		LOG_OPCODE();
	}
	void ROTQBYBI(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		const int s = (CPU.GPR[rb]._u32[3] >> 3) & 0xf;
		const SPU_GPR_hdr temp = CPU.GPR[ra];
		for (int b = 0; b < 16; b++)
			CPU.GPR[rt]._u8[b] = temp._u8[(b - s) & 0xf];
		WRAPPER_END(rt, ra, rb, 0);
	}
	void ROTQMBYBI(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		const int s = (0 - (CPU.GPR[rb]._u32[3] >> 3)) & 0x1f;
		const SPU_GPR_hdr temp = CPU.GPR[ra];
		CPU.GPR[rt].Reset();
		for (int b = 0; b < 16 - s; b++)
			CPU.GPR[rt]._u8[b] = temp._u8[(b + s) & 0xf];
		WRAPPER_END(rt, ra, rb, 0);
	}
	void SHLQBYBI(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		const int s = (CPU.GPR[rb]._u32[3] >> 3) & 0x1f;
		const SPU_GPR_hdr temp = CPU.GPR[ra];
		CPU.GPR[rt].Reset();
		for (int b = s; b < 16; b++)
			CPU.GPR[rt]._u8[b] = temp._u8[b - s];
		WRAPPER_END(rt, ra, rb, 0);
	}
	void CBX(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		const u32 t = (CPU.GPR[rb]._u32[3] + CPU.GPR[ra]._u32[3]) & 0xF;

		CPU.GPR[rt]._u64[0] = (u64)0x18191A1B1C1D1E1F;
		CPU.GPR[rt]._u64[1] = (u64)0x1011121314151617;
		CPU.GPR[rt]._u8[15 - t] = 0x03;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void CHX(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		const u32 t = (CPU.GPR[rb]._u32[3] + CPU.GPR[ra]._u32[3]) & 0xE;

		CPU.GPR[rt]._u64[0] = (u64)0x18191A1B1C1D1E1F;
		CPU.GPR[rt]._u64[1] = (u64)0x1011121314151617;
		CPU.GPR[rt]._u16[7 - (t >> 1)] = 0x0203;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void CWX(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		const u32 t = (CPU.GPR[ra]._u32[3] + CPU.GPR[rb]._u32[3]) & 0xC;
		
		CPU.GPR[rt]._u64[0] = (u64)0x18191A1B1C1D1E1F;
		CPU.GPR[rt]._u64[1] = (u64)0x1011121314151617;
		CPU.GPR[rt]._u32[3 - (t >> 2)] = 0x00010203;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void CDX(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		const u32 t = (CPU.GPR[rb]._u32[3] + CPU.GPR[ra]._u32[3]) & 0x8;

		CPU.GPR[rt]._u64[0] = (u64)0x18191A1B1C1D1E1F;
		CPU.GPR[rt]._u64[1] = (u64)0x1011121314151617;
		CPU.GPR[rt]._u64[1 - (t >> 3)] = (u64)0x0001020304050607;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void ROTQBI(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		const int t = CPU.GPR[rb]._u32[3] & 0x7;
		const SPU_GPR_hdr temp = CPU.GPR[ra];
		CPU.GPR[rt]._u32[0] = (temp._u32[0] << t) | (temp._u32[3] >> (32 - t));
		CPU.GPR[rt]._u32[1] = (temp._u32[1] << t) | (temp._u32[0] >> (32 - t));
		CPU.GPR[rt]._u32[2] = (temp._u32[2] << t) | (temp._u32[1] >> (32 - t));
		CPU.GPR[rt]._u32[3] = (temp._u32[3] << t) | (temp._u32[2] >> (32 - t));
		WRAPPER_END(rt, ra, rb, 0);
	}
	void ROTQMBI(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		const int t = (0 - CPU.GPR[rb]._u32[3]) & 0x7;
		const SPU_GPR_hdr temp = CPU.GPR[ra];
		CPU.GPR[rt]._u32[0] = (temp._u32[0] >> t) | (temp._u32[1] << (32 - t));
		CPU.GPR[rt]._u32[1] = (temp._u32[1] >> t) | (temp._u32[2] << (32 - t));
		CPU.GPR[rt]._u32[2] = (temp._u32[2] >> t) | (temp._u32[3] << (32 - t));
		CPU.GPR[rt]._u32[3] = (temp._u32[3] >> t);
		WRAPPER_END(rt, ra, rb, 0);
	}
	void SHLQBI(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		const int t = CPU.GPR[rb]._u32[3] & 0x7;
		const SPU_GPR_hdr temp = CPU.GPR[ra];
		CPU.GPR[rt]._u32[0] = (temp._u32[0] << t);
		CPU.GPR[rt]._u32[1] = (temp._u32[1] << t) | (temp._u32[0] >> (32 - t));
		CPU.GPR[rt]._u32[2] = (temp._u32[2] << t) | (temp._u32[1] >> (32 - t));
		CPU.GPR[rt]._u32[3] = (temp._u32[3] << t) | (temp._u32[2] >> (32 - t));
		WRAPPER_END(rt, ra, rb, 0);
	}
	void ROTQBY(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		const int s = CPU.GPR[rb]._u32[3] & 0xf;
		const SPU_GPR_hdr temp = CPU.GPR[ra];
		for (int b = 0; b < 16; ++b)
			CPU.GPR[rt]._u8[b] = temp._u8[(b - s) & 0xf];
		WRAPPER_END(rt, ra, rb, 0);
	}
	void ROTQMBY(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		const int s = (0 - CPU.GPR[rb]._u32[3]) & 0x1f;
		const SPU_GPR_hdr temp = CPU.GPR[ra];
		CPU.GPR[rt].Reset();
		for (int b = 0; b < 16 - s; b++)
			CPU.GPR[rt]._u8[b] = temp._u8[(b + s) & 0xf];
		WRAPPER_END(rt, ra, rb, 0);
	}
	void SHLQBY(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		const int s = CPU.GPR[rb]._u32[3] & 0x1f;
		const SPU_GPR_hdr temp = CPU.GPR[ra];
		CPU.GPR[rt].Reset();
		for (int b = s; b < 16; b++)
			CPU.GPR[rt]._u8[b] = temp._u8[b - s];
		WRAPPER_END(rt, ra, rb, 0);
	}
	void ORX(u32 rt, u32 ra)
	{
		WRAPPER_BEGIN(rt, ra, yy, zz);
		CPU.GPR[rt]._u32[3] = CPU.GPR[ra]._u32[0] | CPU.GPR[ra]._u32[1] | CPU.GPR[ra]._u32[2] | CPU.GPR[ra]._u32[3];
		CPU.GPR[rt]._u32[2] = 0;
		CPU.GPR[rt]._u64[0] = 0;
		WRAPPER_END(rt, ra, 0, 0);
	}
	void CBD(u32 rt, u32 ra, s32 i7)
	{
		WRAPPER_BEGIN(rt, ra, i7, zz);
		const int t = (CPU.GPR[ra]._u32[3] + i7) & 0xF;

		CPU.GPR[rt]._u64[0] = (u64)0x18191A1B1C1D1E1F;
		CPU.GPR[rt]._u64[1] = (u64)0x1011121314151617;
		CPU.GPR[rt]._u8[15 - t] = 0x03;
		WRAPPER_END(rt, ra, i7, 0);
	}
	void CHD(u32 rt, u32 ra, s32 i7)
	{
		WRAPPER_BEGIN(rt, ra, i7, zz);
		const int t = (CPU.GPR[ra]._u32[3] + (s32)i7) & 0xE;

		CPU.GPR[rt]._u64[0] = (u64)0x18191A1B1C1D1E1F;
		CPU.GPR[rt]._u64[1] = (u64)0x1011121314151617;
		CPU.GPR[rt]._u16[7 - (t >> 1)] = 0x0203;
		WRAPPER_END(rt, ra, i7, 0);
	}
	void CWD(u32 rt, u32 ra, s32 i7)
	{
		WRAPPER_BEGIN(rt, ra, i7, zz);
		const int t = (CPU.GPR[ra]._u32[3] + (s32)i7) & 0xC;

		CPU.GPR[rt]._u64[0] = (u64)0x18191A1B1C1D1E1F;
		CPU.GPR[rt]._u64[1] = (u64)0x1011121314151617;
		CPU.GPR[rt]._u32[3 - (t >> 2)] = 0x00010203;
		WRAPPER_END(rt, ra, i7, 0);
		// TODO
	}
	void CDD(u32 rt, u32 ra, s32 i7)
	{
		WRAPPER_BEGIN(rt, ra, i7, zz);
		const int t = (CPU.GPR[ra]._u32[3] + i7) & 0x8;

		CPU.GPR[rt]._u64[0] = (u64)0x18191A1B1C1D1E1F;
		CPU.GPR[rt]._u64[1] = (u64)0x1011121314151617;
		CPU.GPR[rt]._u64[1 - (t >> 3)] = (u64)0x0001020304050607;
		WRAPPER_END(rt, ra, i7, 0);
	}
	void ROTQBII(u32 rt, u32 ra, s32 i7)
	{
		WRAPPER_BEGIN(rt, ra, i7, zz);
		const int s = i7 & 0x7;
		const SPU_GPR_hdr temp = CPU.GPR[ra];
		CPU.GPR[rt]._u32[0] = (temp._u32[0] << s) | (temp._u32[3] >> (32 - s));
		CPU.GPR[rt]._u32[1] = (temp._u32[1] << s) | (temp._u32[0] >> (32 - s));
		CPU.GPR[rt]._u32[2] = (temp._u32[2] << s) | (temp._u32[1] >> (32 - s));
		CPU.GPR[rt]._u32[3] = (temp._u32[3] << s) | (temp._u32[2] >> (32 - s));
		WRAPPER_END(rt, ra, i7, 0);
	}
	void ROTQMBII(u32 rt, u32 ra, s32 i7)
	{
		WRAPPER_BEGIN(rt, ra, i7, zz);
		const int s = (0 - (s32)i7) & 0x7;
		const SPU_GPR_hdr temp = CPU.GPR[ra];
		CPU.GPR[rt]._u32[0] = (temp._u32[0] >> s) | (temp._u32[1] << (32 - s));
		CPU.GPR[rt]._u32[1] = (temp._u32[1] >> s) | (temp._u32[2] << (32 - s));
		CPU.GPR[rt]._u32[2] = (temp._u32[2] >> s) | (temp._u32[3] << (32 - s));
		CPU.GPR[rt]._u32[3] = (temp._u32[3] >> s);
		WRAPPER_END(rt, ra, i7, 0);
	}
	void SHLQBII(u32 rt, u32 ra, s32 i7)
	{
		WRAPPER_BEGIN(rt, ra, i7, zz);
		const int s = i7 & 0x7;
		const SPU_GPR_hdr temp = CPU.GPR[ra];
		CPU.GPR[rt]._u32[0] = (temp._u32[0] << s);
		CPU.GPR[rt]._u32[1] = (temp._u32[1] << s) | (temp._u32[0] >> (32 - s));
		CPU.GPR[rt]._u32[2] = (temp._u32[2] << s) | (temp._u32[1] >> (32 - s));
		CPU.GPR[rt]._u32[3] = (temp._u32[3] << s) | (temp._u32[2] >> (32 - s));
		WRAPPER_END(rt, ra, i7, 0);
	}
	void ROTQBYI(u32 rt, u32 ra, s32 i7)
	{
		const int s = i7 & 0xf;
		if (s == 0)
		{
			// mov
			if (ra != rt)
			{
				const XmmLink& va = XmmGet(ra);
				XmmFinalize(va, rt);
			}
			// else nop
		}
		else
		{
			const XmmLink& va = XmmGet(ra);
			const XmmLink& v1 = XmmCopy(va);
			c.pslldq(va.get(), s);
			c.psrldq(v1.get(), 16 - s);
			c.por(va.get(), v1.get());
			XmmFinalize(va, rt);
			XmmFinalize(v1);
		}
		LOG_OPCODE();
	}
	void ROTQMBYI(u32 rt, u32 ra, s32 i7)
	{
		const int s = (0 - i7) & 0x1f;
		if (s == 0)
		{
			if (ra != rt)
			{
				// mov
				const XmmLink& va = XmmGet(ra);
				XmmFinalize(va, rt);
			}
			// else nop
		}
		else if (s > 15)
		{
			// zero
			const XmmLink& v0 = XmmAlloc();
			c.xorps(v0.get(), v0.get());
			XmmFinalize(v0, rt);
		}
		else
		{
			// shift right
			const XmmLink& va = XmmGet(ra);
			c.psrldq(va.get(), s);
			XmmFinalize(va, rt);
		}
		LOG_OPCODE();
	}
	void SHLQBYI(u32 rt, u32 ra, s32 i7)
	{
		const int s = i7 & 0x1f;
		if (s == 0)
		{
			if (ra != rt)
			{
				// mov
				const XmmLink& va = XmmGet(ra);
				XmmFinalize(va, rt);
			}
			// else nop
		}
		else if (s > 15)
		{
			// zero
			const XmmLink& v0 = XmmAlloc();
			c.xorps(v0.get(), v0.get());
			XmmFinalize(v0, rt);
		}
		else
		{
			// shift left
			const XmmLink& va = XmmGet(ra);
			c.pslldq(va.get(), s);
			XmmFinalize(va, rt);
		}
		LOG_OPCODE();
	}
	void NOP(u32 rt)
	{
		LOG_OPCODE();
	}
	void CGT(u32 rt, u32 ra, u32 rb)
	{
		if (ra == rb)
		{
			// zero
			const XmmLink& v0 = XmmAlloc();
			c.xorps(v0.get(), v0.get());
			XmmFinalize(v0, rt);
		}
		else
		{
			const XmmLink& va = XmmGet(ra);
			if (const XmmLink* vb = XmmRead(rb))
			{
				c.pcmpgtd(va.get(), vb->get());
			}
			else
			{
				c.pcmpgtd(va.get(), cpu_xmm(GPR[rb]));
			}
			XmmFinalize(va, rt);
		}
		LOG_OPCODE();
	}
	void XOR(u32 rt, u32 ra, u32 rb)
	{
		if (ra == rb)
		{
			// zero
			const XmmLink& v0 = XmmAlloc();
			c.xorps(v0.get(), v0.get());
			XmmFinalize(v0, rt);
		}
		else
		{
			// xor
			const XmmLink& va = XmmGet(ra);
			if (const XmmLink* vb = XmmRead(rb))
			{
				c.xorps(va.get(), vb->get());
			}
			else
			{
				c.xorps(va.get(), cpu_xmm(GPR[rb]));
			}
			XmmFinalize(va, rt);
		}
		LOG_OPCODE();
	}
	void CGTH(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int h = 0; h < 8; h++)
			CPU.GPR[rt]._u16[h] = CPU.GPR[ra]._i16[h] > CPU.GPR[rb]._i16[h] ? 0xffff : 0;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void EQV(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int w = 0; w < 4; w++)
			CPU.GPR[rt]._u32[w] = CPU.GPR[ra]._u32[w] ^ (~CPU.GPR[rb]._u32[w]);
		WRAPPER_END(rt, ra, rb, 0);
	}
	void CGTB(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int b = 0; b < 16; b++)
			CPU.GPR[rt]._u8[b] = CPU.GPR[ra]._i8[b] > CPU.GPR[rb]._i8[b] ? 0xff : 0;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void SUMB(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		const SPU_GPR_hdr _a = CPU.GPR[ra];
		const SPU_GPR_hdr _b = CPU.GPR[rb];
		for (int w = 0; w < 4; w++)
		{
			CPU.GPR[rt]._u16[w*2] = _a._u8[w*4] + _a._u8[w*4 + 1] + _a._u8[w*4 + 2] + _a._u8[w*4 + 3];
			CPU.GPR[rt]._u16[w*2 + 1] = _b._u8[w*4] + _b._u8[w*4 + 1] + _b._u8[w*4 + 2] + _b._u8[w*4 + 3];
		}
		WRAPPER_END(rt, ra, rb, 0);
	}
	//HGT uses signed values.  HLGT uses unsigned values
	void HGT(u32 rt, s32 ra, s32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		if(CPU.GPR[ra]._i32[3] > CPU.GPR[rb]._i32[3])	CPU.Stop();
		WRAPPER_END(rt, ra, rb, 0);
		c.mov(*pos_var, (CPU.PC >> 2) + 1);
		do_finalize = true;
	}
	void CLZ(u32 rt, u32 ra)
	{
		WRAPPER_BEGIN(rt, ra, yy, zz);
		for (int w = 0; w < 4; w++)
		{
			int nPos;

			for (nPos = 0; nPos < 32; nPos++)
				if (CPU.GPR[ra]._u32[w] & (1 << (31 - nPos)))
					break;

			CPU.GPR[rt]._u32[w] = nPos;
		}
		WRAPPER_END(rt, ra, 0, 0);
	}
	void XSWD(u32 rt, u32 ra)
	{
		WRAPPER_BEGIN(rt, ra, yy, zz);
		CPU.GPR[rt]._i64[0] = (s64)CPU.GPR[ra]._i32[0];
		CPU.GPR[rt]._i64[1] = (s64)CPU.GPR[ra]._i32[2];
		WRAPPER_END(rt, ra, 0, 0);
	}
	void XSHW(u32 rt, u32 ra)
	{
		WRAPPER_BEGIN(rt, ra, yy, zz);
		for (int w = 0; w < 4; w++)
			CPU.GPR[rt]._i32[w] = (s32)CPU.GPR[ra]._i16[w*2];
		WRAPPER_END(rt, ra, 0, 0);
	}
	void CNTB(u32 rt, u32 ra)
	{
		WRAPPER_BEGIN(rt, ra, yy, zz);
		const SPU_GPR_hdr temp = CPU.GPR[ra];
		CPU.GPR[rt].Reset();
		for (int b = 0; b < 16; b++)
			for (int i = 0; i < 8; i++)
				CPU.GPR[rt]._u8[b] += (temp._u8[b] & (1 << i)) ? 1 : 0;
		WRAPPER_END(rt, ra, 0, 0);
	}
	void XSBH(u32 rt, u32 ra)
	{
		WRAPPER_BEGIN(rt, ra, yy, zz);
		for (int h = 0; h < 8; h++)
			CPU.GPR[rt]._i16[h] = (s16)CPU.GPR[ra]._i8[h*2];
		WRAPPER_END(rt, ra, 0, 0);
	}
	void CLGT(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (u32 i = 0; i < 4; ++i)
		{
			CPU.GPR[rt]._u32[i] = (CPU.GPR[ra]._u32[i] > CPU.GPR[rb]._u32[i]) ? 0xffffffff : 0x00000000;
		}
		WRAPPER_END(rt, ra, rb, 0);

		/*XmmVar v0(c);
		if (ra == rb)
		{
			// zero
			const XmmLink& v0 = XmmAlloc();
			c.xorps(v0.get(), v0.get());
			XmmFinalize(v0, rt);
		}
		else
		{
			// compare if-greater-then
			// c.movdqa(v0, cpu_xmm(GPR[rb]));
			// TODO
			// c.movdqa(cpu_xmm(GPR[rt]), v0);
		}
		LOG_OPCODE();*/
	}
	void ANDC(u32 rt, u32 ra, u32 rb)
	{
		if (ra == rb)
		{
			// zero
			const XmmLink& v0 = XmmAlloc();
			c.xorps(v0.get(), v0.get());
			XmmFinalize(v0, rt);
		}
		else
		{
			// and not
			const XmmLink& vb = XmmGet(rb);
			if (const XmmLink* va = XmmRead(ra))
			{
				c.andnps(vb.get(), va->get());
			}
			else
			{
				c.andnps(vb.get(), cpu_xmm(GPR[ra]));
			}
			XmmFinalize(vb, rt);
		}
		LOG_OPCODE();
	}
	void FCGT(u32 rt, u32 ra, u32 rb)
	{
		if (ra == rb)
		{
			// zero
			const XmmLink& v0 = XmmAlloc();
			c.xorps(v0.get(), v0.get());
			XmmFinalize(v0, rt);
		}
		else
		{
			// not-less-or-equal
			const XmmLink& va = XmmGet(ra);
			if (const XmmLink* vb = XmmRead(rb))
			{
				c.cmpps(va.get(), vb->get(), 6);
			}
			else
			{
				c.cmpps(va.get(), cpu_xmm(GPR[rb]), 6);
			}
			XmmFinalize(va, rt);
		}
		LOG_OPCODE();
	}
	void DFCGT(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._u64[0] = CPU.GPR[ra]._d[0] > CPU.GPR[rb]._d[0] ? 0xffffffffffffffff : 0;
		CPU.GPR[rt]._u64[1] = CPU.GPR[ra]._d[1] > CPU.GPR[rb]._d[1] ? 0xffffffffffffffff : 0;
		WRAPPER_END(rt, ra, rb, 0);;
	}
	void FA(u32 rt, u32 ra, u32 rb)
	{
		const XmmLink& va = XmmGet(ra);
		if (ra == rb)
		{
			c.addps(va.get(), va.get());
		}
		else
		{
			if (const XmmLink* vb = XmmRead(rb))
			{
				c.addps(va.get(), vb->get());
			}
			else
			{
				c.addps(va.get(), cpu_xmm(GPR[rb]));
			}
		}
		XmmFinalize(va, rt);
		LOG_OPCODE();
	}
	void FS(u32 rt, u32 ra, u32 rb)
	{
		if (ra == rb)
		{
			// zero
			const XmmLink& v0 = XmmAlloc();
			c.xorps(v0.get(), v0.get());
			XmmFinalize(v0, rt);
		}
		else
		{
			const XmmLink& va = XmmGet(ra);
			if (const XmmLink* vb = XmmRead(rb))
			{
				c.subps(va.get(), vb->get());
			}
			else
			{
				c.subps(va.get(), cpu_xmm(GPR[rb]));
			}
			XmmFinalize(va, rt);
		}
		LOG_OPCODE();
	}
	void FM(u32 rt, u32 ra, u32 rb)
	{
		if (ra == rb)
		{
			const XmmLink& va = XmmGet(ra);
			c.mulps(va.get(), va.get());
			XmmFinalize(va, rt);
		}
		else
		{
			const XmmLink& va = XmmGet(ra);
			if (const XmmLink* vb = XmmRead(rb))
			{
				c.mulps(va.get(), vb->get());
			}
			else
			{
				c.mulps(va.get(), cpu_xmm(GPR[rb]));
			}
			XmmFinalize(va, rt);
		}
		LOG_OPCODE();
	}
	void CLGTH(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int h = 0; h < 8; h++)
			CPU.GPR[rt]._u16[h] = CPU.GPR[ra]._u16[h] > CPU.GPR[rb]._u16[h] ? 0xffff : 0;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void ORC(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int w = 0; w < 4; w++)
			CPU.GPR[rt]._u32[w] = CPU.GPR[ra]._u32[w] | (~CPU.GPR[rb]._u32[w]);
		WRAPPER_END(rt, ra, rb, 0);
	}
	void FCMGT(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._u32[0] = fabs(CPU.GPR[ra]._f[0]) > fabs(CPU.GPR[rb]._f[0]) ? 0xffffffff : 0;
		CPU.GPR[rt]._u32[1] = fabs(CPU.GPR[ra]._f[1]) > fabs(CPU.GPR[rb]._f[1]) ? 0xffffffff : 0;
		CPU.GPR[rt]._u32[2] = fabs(CPU.GPR[ra]._f[2]) > fabs(CPU.GPR[rb]._f[2]) ? 0xffffffff : 0;
		CPU.GPR[rt]._u32[3] = fabs(CPU.GPR[ra]._f[3]) > fabs(CPU.GPR[rb]._f[3]) ? 0xffffffff : 0;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void DFCMGT(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._u64[0] = fabs(CPU.GPR[ra]._d[0]) > fabs(CPU.GPR[rb]._d[0]) ? 0xffffffffffffffff : 0;
		CPU.GPR[rt]._u64[1] = fabs(CPU.GPR[ra]._d[1]) > fabs(CPU.GPR[rb]._d[1]) ? 0xffffffffffffffff : 0;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void DFA(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._d[0] = CPU.GPR[ra]._d[0] + CPU.GPR[rb]._d[0];
		CPU.GPR[rt]._d[1] = CPU.GPR[ra]._d[1] + CPU.GPR[rb]._d[1];
		WRAPPER_END(rt, ra, rb, 0);
	}
	void DFS(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._d[0] = CPU.GPR[ra]._d[0] - CPU.GPR[rb]._d[0];
		CPU.GPR[rt]._d[1] = CPU.GPR[ra]._d[1] - CPU.GPR[rb]._d[1];
		WRAPPER_END(rt, ra, rb, 0);
	}
	void DFM(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._d[0] = CPU.GPR[ra]._d[0] * CPU.GPR[rb]._d[0];
		CPU.GPR[rt]._d[1] = CPU.GPR[ra]._d[1] * CPU.GPR[rb]._d[1];
		WRAPPER_END(rt, ra, rb, 0);
	}
	void CLGTB(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int b = 0; b < 16; b++)
			CPU.GPR[rt]._u8[b] = CPU.GPR[ra]._u8[b] > CPU.GPR[rb]._u8[b] ? 0xff : 0;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void HLGT(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		if(CPU.GPR[ra]._u32[3] > CPU.GPR[rb]._u32[3])	CPU.Stop();
		WRAPPER_END(rt, ra, rb, 0);
		c.mov(*pos_var, (CPU.PC >> 2) + 1);
		do_finalize = true;
	}
	void DFMA(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._d[0] += CPU.GPR[ra]._d[0] * CPU.GPR[rb]._d[0];
		CPU.GPR[rt]._d[1] += CPU.GPR[ra]._d[1] * CPU.GPR[rb]._d[1];
		WRAPPER_END(rt, ra, rb, 0);
	}
	void DFMS(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._d[0] = CPU.GPR[ra]._d[0] * CPU.GPR[rb]._d[0] - CPU.GPR[rt]._d[0];
		CPU.GPR[rt]._d[1] = CPU.GPR[ra]._d[1] * CPU.GPR[rb]._d[1] - CPU.GPR[rt]._d[1];
		WRAPPER_END(rt, ra, rb, 0);
	}
	void DFNMS(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._d[0] -= CPU.GPR[ra]._d[0] * CPU.GPR[rb]._d[0];
		CPU.GPR[rt]._d[1] -= CPU.GPR[ra]._d[1] * CPU.GPR[rb]._d[1];
		WRAPPER_END(rt, ra, rb, 0);
	}
	void DFNMA(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._d[0] = -(CPU.GPR[ra]._d[0] * CPU.GPR[rb]._d[0] + CPU.GPR[rt]._d[0]);
		CPU.GPR[rt]._d[1] = -(CPU.GPR[ra]._d[1] * CPU.GPR[rb]._d[1] + CPU.GPR[rt]._d[1]);
		WRAPPER_END(rt, ra, rb, 0);
	}
	void CEQ(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int w = 0; w < 4; w++)
			CPU.GPR[rt]._u32[w] = CPU.GPR[ra]._i32[w] == CPU.GPR[rb]._i32[w] ? 0xffffffff : 0;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void MPYHHU(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int w = 0; w < 4; w++)
			CPU.GPR[rt]._u32[w] = CPU.GPR[ra]._u16[w*2+1] * CPU.GPR[rb]._u16[w*2+1];
		WRAPPER_END(rt, ra, rb, 0);
	}
	void ADDX(u32 rt, u32 ra, u32 rb)
	{
		const XmmLink& vt = XmmGet(rt);
		c.pand(vt.get(), imm_xmm(s19_to_s32[1]));
		c.paddd(vt.get(), cpu_xmm(GPR[ra]));
		c.paddd(vt.get(), cpu_xmm(GPR[rb]));
		XmmFinalize(vt, rt);
		LOG_OPCODE();
	}
	void SFX(u32 rt, u32 ra, u32 rb)
	{
		const XmmLink& vt = XmmGet(rt);
		c.pandn(vt.get(), imm_xmm(s19_to_s32[1]));
		if (ra == rb)
		{
			// load zero
			const XmmLink& v0 = XmmAlloc();
			c.pxor(v0.get(), v0.get());
			c.psubd(v0.get(), vt.get());
			XmmFinalize(v0, rt);
		}
		else
		{
			// sub
			const XmmLink& vb = XmmGet(rb);
			c.psubd(vb.get(), cpu_xmm(GPR[ra]));
			c.psubd(vb.get(), vt.get());
			XmmFinalize(vb, rt);
		}
		XmmFinalize(vt);
		LOG_OPCODE();
	}
	void CGX(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int w = 0; w < 4; w++)
			CPU.GPR[rt]._u32[w] = ((u64)CPU.GPR[ra]._u32[w] + (u64)CPU.GPR[rb]._u32[w] + (u64)(CPU.GPR[rt]._u32[w] & 1)) >> 32;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void BGX(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		s64 nResult;
		
		for (int w = 0; w < 4; w++)
		{
			nResult = (u64)CPU.GPR[rb]._u32[w] - (u64)CPU.GPR[ra]._u32[w] - (u64)(1 - (CPU.GPR[rt]._u32[w] & 1));
			CPU.GPR[rt]._u32[w] = nResult < 0 ? 0 : 1;
		}
		WRAPPER_END(rt, ra, rb, 0);
	}
	void MPYHHA(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int w = 0; w < 4; w++)
			CPU.GPR[rt]._i32[w] += CPU.GPR[ra]._i16[w*2+1] * CPU.GPR[rb]._i16[w*2+1];
		WRAPPER_END(rt, ra, rb, 0);
	}
	void MPYHHAU(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int w = 0; w < 4; w++)
			CPU.GPR[rt]._u32[w] += CPU.GPR[ra]._u16[w*2+1] * CPU.GPR[rb]._u16[w*2+1];
		WRAPPER_END(rt, ra, rb, 0);
	}
	//Forced bits to 0, hence the shift:
	
	void FSCRRD(u32 rt)
	{
		/*CPU.GPR[rt]._u128.lo = 
			CPU.FPSCR.Exception0 << 20 &
			CPU.FPSCR.*/
		UNIMPLEMENTED();
	}
	void FESD(u32 rt, u32 ra)
	{
		WRAPPER_BEGIN(rt, ra, yy, zz);
		CPU.GPR[rt]._d[0] = (double)CPU.GPR[ra]._f[1];
		CPU.GPR[rt]._d[1] = (double)CPU.GPR[ra]._f[3];
		WRAPPER_END(rt, ra, 0, 0);
	}
	void FRDS(u32 rt, u32 ra)
	{
		WRAPPER_BEGIN(rt, ra, yy, zz);
		CPU.GPR[rt]._f[1] = (float)CPU.GPR[ra]._d[0];
		CPU.GPR[rt]._u32[0] = 0x00000000;
		CPU.GPR[rt]._f[3] = (float)CPU.GPR[ra]._d[1];
		CPU.GPR[rt]._u32[2] = 0x00000000;
		WRAPPER_END(rt, ra, 0, 0);
	}
	void FSCRWR(u32 rt, u32 ra)
	{
		UNIMPLEMENTED();
	}
	void DFTSV(u32 rt, u32 ra, s32 i7)
	{
		WRAPPER_BEGIN(rt, ra, i7, zz);
		const u64 DoubleExpMask = 0x7ff0000000000000;
		const u64 DoubleFracMask = 0x000fffffffffffff;
		const u64 DoubleSignMask = 0x8000000000000000;
		const SPU_GPR_hdr temp = CPU.GPR[ra];
		CPU.GPR[rt].Reset();
		if (i7 & 1) //Negative Denorm Check (-, exp is zero, frac is non-zero)
			for (int i = 0; i < 2; i++)
			{
				if (temp._u64[i] & DoubleFracMask)
					if ((temp._u64[i] & (DoubleSignMask | DoubleExpMask)) == DoubleSignMask)
						CPU.GPR[rt]._u64[i] = 0xffffffffffffffff;
			}
		if (i7 & 2) //Positive Denorm Check (+, exp is zero, frac is non-zero)
			for (int i = 0; i < 2; i++)
			{
				if (temp._u64[i] & DoubleFracMask)
					if ((temp._u64[i] & (DoubleSignMask | DoubleExpMask)) == 0)
						CPU.GPR[rt]._u64[i] = 0xffffffffffffffff;
			}
		if (i7 & 4) //Negative Zero Check (-, exp is zero, frac is zero)
			for (int i = 0; i < 2; i++)
			{
				if (temp._u64[i] == DoubleSignMask)
					CPU.GPR[rt]._u64[i] = 0xffffffffffffffff;
			}
		if (i7 & 8) //Positive Zero Check (+, exp is zero, frac is zero)
			for (int i = 0; i < 2; i++)
			{
				if (temp._u64[i] == 0)
					CPU.GPR[rt]._u64[i] = 0xffffffffffffffff;
			}
		if (i7 & 16) //Negative Infinity Check (-, exp is 0x7ff, frac is zero)
			for (int i = 0; i < 2; i++)
			{
				if (temp._u64[i] == (DoubleSignMask | DoubleExpMask))
					CPU.GPR[rt]._u64[i] = 0xffffffffffffffff;
			}
		if (i7 & 32) //Positive Infinity Check (+, exp is 0x7ff, frac is zero)
			for (int i = 0; i < 2; i++)
			{
				if (temp._u64[i] == DoubleExpMask)
					CPU.GPR[rt]._u64[i] = 0xffffffffffffffff;
			}
		if (i7 & 64) //Not-a-Number Check (any sign, exp is 0x7ff, frac is non-zero)
			for (int i = 0; i < 2; i++)
			{
				if (temp._u64[i] & DoubleFracMask)
					if ((temp._u64[i] & DoubleExpMask) == DoubleExpMask)
						CPU.GPR[rt]._u64[i] = 0xffffffffffffffff;
			}
		WRAPPER_END(rt, ra, i7, 0);
	}
	void FCEQ(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._u32[0] = CPU.GPR[ra]._f[0] == CPU.GPR[rb]._f[0] ? 0xffffffff : 0;
		CPU.GPR[rt]._u32[1] = CPU.GPR[ra]._f[1] == CPU.GPR[rb]._f[1] ? 0xffffffff : 0;
		CPU.GPR[rt]._u32[2] = CPU.GPR[ra]._f[2] == CPU.GPR[rb]._f[2] ? 0xffffffff : 0;
		CPU.GPR[rt]._u32[3] = CPU.GPR[ra]._f[3] == CPU.GPR[rb]._f[3] ? 0xffffffff : 0;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void DFCEQ(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._u64[0] = CPU.GPR[ra]._d[0] == CPU.GPR[rb]._d[0] ? 0xffffffffffffffff : 0;
		CPU.GPR[rt]._u64[1] = CPU.GPR[ra]._d[1] == CPU.GPR[rb]._d[1] ? 0xffffffffffffffff : 0;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void MPY(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int w = 0; w < 4; w++)
			CPU.GPR[rt]._i32[w] = CPU.GPR[ra]._i16[w*2] * CPU.GPR[rb]._i16[w*2];
		WRAPPER_END(rt, ra, rb, 0);
	}
	void MPYH(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int w = 0; w < 4; w++)
			CPU.GPR[rt]._i32[w] = (CPU.GPR[ra]._i16[w*2+1] * CPU.GPR[rb]._i16[w*2]) << 16;
		WRAPPER_END(rt, ra, rb, 0);
		// TODO
	}
	void MPYHH(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int w = 0; w < 4; w++)
			CPU.GPR[rt]._i32[w] = CPU.GPR[ra]._i16[w*2+1] * CPU.GPR[rb]._i16[w*2+1];
		WRAPPER_END(rt, ra, rb, 0);
	}
	void MPYS(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int w = 0; w < 4; w++)
			CPU.GPR[rt]._i32[w] = (CPU.GPR[ra]._i16[w*2] * CPU.GPR[rb]._i16[w*2]) >> 16;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void CEQH(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int h = 0; h < 8; h++)
			CPU.GPR[rt]._u16[h] = CPU.GPR[ra]._u16[h] == CPU.GPR[rb]._u16[h] ? 0xffff : 0;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void FCMEQ(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._u32[0] = fabs(CPU.GPR[ra]._f[0]) == fabs(CPU.GPR[rb]._f[0]) ? 0xffffffff : 0;
		CPU.GPR[rt]._u32[1] = fabs(CPU.GPR[ra]._f[1]) == fabs(CPU.GPR[rb]._f[1]) ? 0xffffffff : 0;
		CPU.GPR[rt]._u32[2] = fabs(CPU.GPR[ra]._f[2]) == fabs(CPU.GPR[rb]._f[2]) ? 0xffffffff : 0;
		CPU.GPR[rt]._u32[3] = fabs(CPU.GPR[ra]._f[3]) == fabs(CPU.GPR[rb]._f[3]) ? 0xffffffff : 0;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void DFCMEQ(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		CPU.GPR[rt]._u64[0] = fabs(CPU.GPR[ra]._d[0]) == fabs(CPU.GPR[rb]._d[0]) ? 0xffffffffffffffff : 0;
		CPU.GPR[rt]._u64[1] = fabs(CPU.GPR[ra]._d[1]) == fabs(CPU.GPR[rb]._d[1]) ? 0xffffffffffffffff : 0;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void MPYU(u32 rt, u32 ra, u32 rb)
	{
		const XmmLink& va = XmmGet(ra);
		if (ra == rb)
		{
			c.pand(va.get(), imm_xmm(s19_to_s32[0xffff]));
			c.pmulld(va.get(), va.get());
		}
		else
		{
			const XmmLink& v1 = XmmAlloc();
			c.movdqa(v1.get(), imm_xmm(s19_to_s32[0xffff])); // load mask
			c.pand(va.get(), v1.get()); // clear high words of each dword
			c.pand(v1.get(), cpu_xmm(GPR[rb]));
			c.pmulld(va.get(), v1.get());
			XmmFinalize(v1);
		}
		XmmFinalize(va, rt);
		LOG_OPCODE();
	}
	void CEQB(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		for (int b = 0; b < 16; b++)
			CPU.GPR[rt]._u8[b] = CPU.GPR[ra]._u8[b] == CPU.GPR[rb]._u8[b] ? 0xff : 0;
		WRAPPER_END(rt, ra, rb, 0);
	}
	void FI(u32 rt, u32 ra, u32 rb)
	{
		const XmmLink& vb = XmmGet(rb);
		XmmFinalize(vb, rt);
		LOG_OPCODE();
	}
	void HEQ(u32 rt, u32 ra, u32 rb)
	{
		WRAPPER_BEGIN(rt, ra, rb, zz);
		if(CPU.GPR[ra]._i32[3] == CPU.GPR[rb]._i32[3])	CPU.Stop();
		WRAPPER_END(rt, ra, rb, 0);
		c.mov(*pos_var, (CPU.PC >> 2) + 1);
		do_finalize = true;
	}

	//0 - 9
	void CFLTS(u32 rt, u32 ra, s32 i8)
	{
		const XmmLink& va = XmmGet(ra);
		if (i8 != 173)
		{
			c.mulps(va.get(), imm_xmm(scale_to_int[i8 & 0xff])); // scale
		}
		c.cvttps2dq(va.get(), va.get()); // convert to ints with truncation
		XmmFinalize(va, rt);
		LOG_OPCODE();
	}
	void CFLTU(u32 rt, u32 ra, s32 i8)
	{
		WRAPPER_BEGIN(rt, ra, i8, zz);
		const u32 scale = 173 - (i8 & 0xff); //unsigned immediate
		for (int i = 0; i < 4; i++)
		{
			u32 exp = ((CPU.GPR[ra]._u32[i] >> 23) & 0xff) + scale;

			if (exp > 255)
				exp = 255;

			if (CPU.GPR[ra]._u32[i] & 0x80000000) //if negative, result = 0
				CPU.GPR[rt]._u32[i] = 0;
			else
			{
				CPU.GPR[rt]._u32[i] = (CPU.GPR[ra]._u32[i] & 0x807fffff) | (exp << 23);

				if (CPU.GPR[rt]._f[i] > 0xffffffff) //if big, result = max
					CPU.GPR[rt]._u32[i] = 0xffffffff;
				else
					CPU.GPR[rt]._u32[i] = floor(CPU.GPR[rt]._f[i]);
			}
		}
		WRAPPER_END(rt, ra, i8, 0);

		/*XmmVar v0(c);
		c.movaps(v0, cpu_xmm(GPR[ra]));
		if (i8 != 173)
		{
			c.mulps(v0, imm_xmm(scale_to_int[i8 & 0xff])); // scale
		}
		// TODO: handle negative values and convert to unsigned value
		// c.int3();
		c.cvtps2dq(v0, v0); // convert to signed ints
		c.movdqa(cpu_xmm(GPR[rt]), v0);
		LOG_OPCODE();*/
	}
	void CSFLT(u32 rt, u32 ra, s32 i8)
	{
		const XmmLink& va = XmmGet(ra);
		c.cvtdq2ps(va.get(), va.get()); // convert to floats
		if (i8 != 155)
		{
			c.mulps(va.get(), imm_xmm(scale_to_float[i8 & 0xff])); // scale
		}
		XmmFinalize(va, rt);
		LOG_OPCODE();
	}
	void CUFLT(u32 rt, u32 ra, s32 i8)
	{
		WRAPPER_BEGIN(rt, ra, i8, zz);
		const u32 scale = 155 - (i8 & 0xff); //unsigned immediate
		for (int i = 0; i < 4; i++)
		{
			CPU.GPR[rt]._f[i] = (float)CPU.GPR[ra]._u32[i];
			u32 exp = ((CPU.GPR[rt]._u32[i] >> 23) & 0xff) - scale;

			if (exp > 255) //< 0
				exp = 0;

			CPU.GPR[rt]._u32[i] = (CPU.GPR[rt]._u32[i] & 0x807fffff) | (exp << 23);
		}
		WRAPPER_END(rt, ra, i8, 0);

		/*XmmVar v0(c);
		c.movdqa(v0, cpu_xmm(GPR[ra]));
		// TODO: convert from unsigned value
		// c.int3();
		c.cvtdq2ps(v0, v0); // convert to floats as signed
		if (i8 != 155)
		{
			c.mulps(v0, imm_xmm(scale_to_float[i8 & 0xff])); // scale
		}
		c.movaps(cpu_xmm(GPR[rt]), v0);
		LOG_OPCODE();*/
	}

	//0 - 8
	void BRZ(u32 rt, s32 i16)
	{
		c.mov(cpu_qword(PC), (u32)CPU.PC);
		do_finalize = true;

		c.mov(*addr, (CPU.PC >> 2) + 1);
		c.mov(*pos_var, branchTarget(CPU.PC, i16) >> 2);
		c.cmp(cpu_dword(GPR[rt]._u32[3]), 0);
		c.cmovne(*pos_var, *addr);
		LOG_OPCODE();
	}
	void STQA(u32 rt, s32 i16)
	{
		const u32 lsa = (i16 << 2) & 0x3fff0;
		c.mov(*qw0, cpu_qword(GPR[rt]._u64[0]));
		c.mov(*qw1, cpu_qword(GPR[rt]._u64[1]));
		c.bswap(*qw0);
		c.bswap(*qw1);
		c.mov(qword_ptr(*ls_var, lsa), *qw1);
		c.mov(qword_ptr(*ls_var, lsa + 8), *qw0);
		LOG_OPCODE();
	}
	void BRNZ(u32 rt, s32 i16)
	{
		c.mov(cpu_qword(PC), (u32)CPU.PC);
		do_finalize = true;

		c.mov(*addr, (CPU.PC >> 2) + 1);
		c.mov(*pos_var, branchTarget(CPU.PC, i16) >> 2);
		c.cmp(cpu_dword(GPR[rt]._u32[3]), 0);
		c.cmove(*pos_var, *addr);
		LOG_OPCODE();
	}
	void BRHZ(u32 rt, s32 i16)
	{
		c.mov(cpu_qword(PC), (u32)CPU.PC);
		do_finalize = true;

		c.mov(*addr, (CPU.PC >> 2) + 1);
		c.mov(*pos_var, branchTarget(CPU.PC, i16) >> 2);
		c.cmp(cpu_word(GPR[rt]._u16[6]), 0);
		c.cmovnz(*pos_var, *addr);
		LOG_OPCODE();
	}
	void BRHNZ(u32 rt, s32 i16)
	{
		c.mov(cpu_qword(PC), (u32)CPU.PC);
		do_finalize = true;

		c.mov(*addr, (CPU.PC >> 2) + 1);
		c.mov(*pos_var, branchTarget(CPU.PC, i16) >> 2);
		c.cmp(cpu_word(GPR[rt]._u16[6]), 0);
		c.cmovz(*pos_var, *addr);
		LOG_OPCODE();
	}
	void STQR(u32 rt, s32 i16)
	{
		const u32 lsa = branchTarget(CPU.PC, i16) & 0x3fff0;
		c.mov(*qw0, cpu_qword(GPR[rt]._u64[0]));
		c.mov(*qw1, cpu_qword(GPR[rt]._u64[1]));
		c.bswap(*qw0);
		c.bswap(*qw1);
		c.mov(qword_ptr(*ls_var, lsa), *qw1);
		c.mov(qword_ptr(*ls_var, lsa + 8), *qw0);
		LOG_OPCODE();
	}
	void BRA(s32 i16)
	{
		c.mov(cpu_qword(PC), (u32)CPU.PC);
		do_finalize = true;

		c.mov(*pos_var, branchTarget(0, i16) >> 2);
		LOG_OPCODE();
	}
	void LQA(u32 rt, s32 i16)
	{
		const u32 lsa = (i16 << 2) & 0x3fff0;
		c.mov(*qw0, qword_ptr(*ls_var, lsa));
		c.mov(*qw1, qword_ptr(*ls_var, lsa + 8));
		c.bswap(*qw0);
		c.bswap(*qw1);
		c.mov(cpu_qword(GPR[rt]._u64[0]), *qw1);
		c.mov(cpu_qword(GPR[rt]._u64[1]), *qw0);
		LOG_OPCODE();
	}
	void BRASL(u32 rt, s32 i16)
	{
		c.mov(cpu_qword(PC), (u32)CPU.PC);
		do_finalize = true;

		c.xor_(*addr, *addr); // zero
		c.mov(cpu_dword(GPR[rt]._u32[0]), *addr);
		c.mov(cpu_dword(GPR[rt]._u32[1]), *addr);
		c.mov(cpu_dword(GPR[rt]._u32[2]), *addr);
		c.mov(cpu_dword(GPR[rt]._u32[3]), (u32)CPU.PC + 4);
		c.mov(*pos_var, branchTarget(0, i16) >> 2);
		LOG_OPCODE();
	}
	void BR(s32 i16)
	{
		c.mov(cpu_qword(PC), (u32)CPU.PC);
		do_finalize = true;

		c.mov(*pos_var, branchTarget(CPU.PC, i16) >> 2);
		LOG_OPCODE();
	}
	void FSMBI(u32 rt, s32 i16)
	{
		if (i16 == 0)
		{
			// zero
			const XmmLink& v0 = XmmAlloc();
			c.xorps(v0.get(), v0.get());
			XmmFinalize(v0, rt);
		}
		else
		{
			const XmmLink& vr = XmmAlloc();
			c.movaps(vr.get(), imm_xmm(fsmbi_mask[i16 & 0xffff]));
			XmmFinalize(vr, rt);
		}
		LOG_OPCODE();
	}
	void BRSL(u32 rt, s32 i16)
	{
		c.mov(cpu_qword(PC), (u32)CPU.PC);
		do_finalize = true;

		c.xor_(*addr, *addr); // zero
		c.mov(cpu_dword(GPR[rt]._u32[0]), *addr);
		c.mov(cpu_dword(GPR[rt]._u32[1]), *addr);
		c.mov(cpu_dword(GPR[rt]._u32[2]), *addr);
		c.mov(cpu_dword(GPR[rt]._u32[3]), (u32)CPU.PC + 4);
		c.mov(*pos_var, branchTarget(CPU.PC, i16) >> 2);
		LOG_OPCODE();
	}
	void LQR(u32 rt, s32 i16)
	{
		const u32 lsa = branchTarget(CPU.PC, i16) & 0x3fff0;
		c.mov(*qw0, qword_ptr(*ls_var, lsa));
		c.mov(*qw1, qword_ptr(*ls_var, lsa + 8));
		c.bswap(*qw0);
		c.bswap(*qw1);
		c.mov(cpu_qword(GPR[rt]._u64[0]), *qw1);
		c.mov(cpu_qword(GPR[rt]._u64[1]), *qw0);
		LOG_OPCODE();
	}
	void IL(u32 rt, s32 i16)
	{
		const XmmLink& vr = XmmAlloc();
		if (i16 == 0)
		{
			c.xorps(vr.get(), vr.get());
		}
		else if (i16 == -1)
		{
			c.cmpps(vr.get(), vr.get(), 0);
		}
		else
		{
			c.movaps(vr.get(), imm_xmm(s19_to_s32[i16 & 0x7ffff]));
		}
		XmmFinalize(vr, rt);
		LOG_OPCODE();
	}
	void ILHU(u32 rt, s32 i16)
	{
		const XmmLink& vr = XmmAlloc();
		if (i16 == 0)
		{
			c.xorps(vr.get(), vr.get());
		}
		else if (i16 == -1)
		{
			c.cmpps(vr.get(), vr.get(), 0);
			c.pslld(vr.get(), 16);
		}
		else
		{
			c.movaps(vr.get(), imm_xmm(s19_to_s32[i16 & 0x7ffff]));
			c.pslld(vr.get(), 16);
		}
		XmmFinalize(vr, rt);
		LOG_OPCODE();
	}
	void ILH(u32 rt, s32 i16)
	{
		WRAPPER_BEGIN(rt, i16, yy, zz);
		for (int h = 0; h < 8; h++)
			CPU.GPR[rt]._i16[h] = (s32)i16;
		WRAPPER_END(rt, i16, 0, 0);
	}
	void IOHL(u32 rt, s32 i16)
	{
		if (i16 == 0)
		{
			// nop
		}
		else
		{
			const XmmLink& vt = XmmGet(rt);
			c.orps(vt.get(), imm_xmm(s19_to_s32[i16 & 0xffff]));
			XmmFinalize(vt, rt);
		}
		LOG_OPCODE();
	}
	

	//0 - 7
	void ORI(u32 rt, u32 ra, s32 i10)
	{
		if (i10 == -1)
		{
			// fill with 1
			const XmmLink& v1 = XmmAlloc();
			c.cmpps(v1.get(), v1.get(), 0);
			XmmFinalize(v1, rt);
		}
		else if (i10 == 0)
		{
			if (rt != ra)
			{
				// mov
				const XmmLink& va = XmmGet(ra);
				XmmFinalize(va, rt);
			}
			// else nop
		}
		else
		{
			const XmmLink& va = XmmGet(ra);
			c.orps(va.get(), imm_xmm(s19_to_s32[i10 & 0x7ffff]));
			XmmFinalize(va, rt);
		}
		LOG_OPCODE();
	}
	void ORHI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		for (int h = 0; h < 8; h++)
			CPU.GPR[rt]._i16[h] = CPU.GPR[ra]._i16[h] | (s32)i10;
		WRAPPER_END(rt, ra, i10, 0);
	}
	void ORBI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		for (int b = 0; b < 16; b++)
			CPU.GPR[rt]._i8[b] = CPU.GPR[ra]._i8[b] | (s32)i10;
		WRAPPER_END(rt, ra, i10, 0);
	}
	void SFI(u32 rt, u32 ra, s32 i10)
	{
		if (i10 == 0)
		{
			// zero
			const XmmLink& v0 = XmmAlloc();
			c.xorps(v0.get(), v0.get());
			c.psubd(v0.get(), cpu_xmm(GPR[ra]));
			XmmFinalize(v0, rt);
		}
		else if (i10 == -1)
		{
			// fill with 1
			const XmmLink& v1 = XmmAlloc();
			c.pcmpeqd(v1.get(), v1.get());
			c.psubd(v1.get(), cpu_xmm(GPR[ra]));
			XmmFinalize(v1, rt);
		}
		else
		{
			const XmmLink& vr = XmmAlloc();
			c.movdqa(vr.get(), imm_xmm(s19_to_s32[i10 & 0x7ffff]));
			c.psubd(vr.get(), cpu_xmm(GPR[ra]));
			XmmFinalize(vr, rt);
		}
		LOG_OPCODE();
	}
	void SFHI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		for (int h = 0; h < 8; h++)
			CPU.GPR[rt]._i16[h] = (s32)i10 - CPU.GPR[ra]._i16[h];
		WRAPPER_END(rt, ra, i10, 0);
	}
	void ANDI(u32 rt, u32 ra, s32 i10)
	{
		if (i10 == 0)
		{
			// zero
			const XmmLink& v0 = XmmAlloc();
			c.xorps(v0.get(), v0.get());
			XmmFinalize(v0, rt);
		}
		else if (i10 == -1)
		{
			// mov
			if (ra != rt)
			{
				const XmmLink& va = XmmGet(ra);
				XmmFinalize(va, rt);
			}
			// else nop
		}
		else
		{
			const XmmLink& va = XmmGet(ra);
			c.andps(va.get(), imm_xmm(s19_to_s32[i10 & 0x7ffff]));
			XmmFinalize(va, rt);
		}
		LOG_OPCODE();
	}
	void ANDHI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		for (int h = 0; h < 8; h++)
			CPU.GPR[rt]._i16[h] = CPU.GPR[ra]._i16[h] & (s32)i10;
		WRAPPER_END(rt, ra, i10, 0);
	}
	void ANDBI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		for (int b = 0; b < 16; b++)
			CPU.GPR[rt]._i8[b] = CPU.GPR[ra]._i8[b] & (s32)i10;
		WRAPPER_END(rt, ra, i10, 0);
	}
	void AI(u32 rt, u32 ra, s32 i10)
	{
		if (i10 == 0)
		{
			if (rt != ra)
			{
				// mov
				const XmmLink& va = XmmGet(ra);
				XmmFinalize(va, rt);
			}
			// else nop
		}
		else
		{
			// add
			const XmmLink& va = XmmGet(ra);
			c.paddd(va.get(), imm_xmm(s19_to_s32[i10 & 0x7ffff]));
			XmmFinalize(va, rt);
		}
		LOG_OPCODE();
	}
	void AHI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		for(u32 h = 0; h < 8; ++h)
			CPU.GPR[rt]._i16[h] = CPU.GPR[ra]._i16[h] + (s32)i10;
		WRAPPER_END(rt, ra, i10, 0);
	}
	void STQD(u32 rt, s32 i10, u32 ra) // i10 is shifted left by 4 while decoding
	{
		c.mov(*addr, cpu_dword(GPR[ra]._u32[3]));
		if (i10) c.add(*addr, i10);
		c.and_(*addr, 0x3fff0);
		c.mov(*qw0, cpu_qword(GPR[rt]._u64[0]));
		c.mov(*qw1, cpu_qword(GPR[rt]._u64[1]));
		c.bswap(*qw0);
		c.bswap(*qw1);
		c.mov(qword_ptr(*ls_var, *addr, 0, 0), *qw1);
		c.mov(qword_ptr(*ls_var, *addr, 0, 8), *qw0);
		LOG_OPCODE();
	}
	void LQD(u32 rt, s32 i10, u32 ra) // i10 is shifted left by 4 while decoding
	{
		c.mov(*addr, cpu_dword(GPR[ra]._u32[3]));
		if (i10) c.add(*addr, i10);
		c.and_(*addr, 0x3fff0);
		c.mov(*qw0, qword_ptr(*ls_var, *addr, 0, 0));
		c.mov(*qw1, qword_ptr(*ls_var, *addr, 0, 8));
		c.bswap(*qw0);
		c.bswap(*qw1);
		c.mov(cpu_qword(GPR[rt]._u64[0]), *qw1);
		c.mov(cpu_qword(GPR[rt]._u64[1]), *qw0);
		LOG_OPCODE();
	}
	void XORI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		for (int w = 0; w < 4; w++)
			CPU.GPR[rt]._i32[w] = CPU.GPR[ra]._i32[w] ^ (s32)i10;
		WRAPPER_END(rt, ra, i10, 0);
	}
	void XORHI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		for (int h = 0; h < 8; h++)
			CPU.GPR[rt]._i16[h] = CPU.GPR[ra]._i16[h] ^ (s32)i10;
		WRAPPER_END(rt, ra, i10, 0);
	}
	void XORBI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		for (int b = 0; b < 16; b++)
			CPU.GPR[rt]._i8[b] = CPU.GPR[ra]._i8[b] ^ (s32)i10;
		WRAPPER_END(rt, ra, i10, 0);
	}
	void CGTI(u32 rt, u32 ra, s32 i10)
	{
		const XmmLink& va = XmmGet(ra);
		c.pcmpgtd(va.get(), imm_xmm(s19_to_s32[i10 & 0x7ffff]));
		XmmFinalize(va, rt);
		LOG_OPCODE();
	}
	void CGTHI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		for (int h = 0; h < 8; h++)
			CPU.GPR[rt]._u16[h] = CPU.GPR[ra]._i16[h] > (s32)i10 ? 0xffff : 0;
		WRAPPER_END(rt, ra, i10, 0);
	}
	void CGTBI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		for (int b = 0; b < 16; b++)
			CPU.GPR[rt]._u8[b] = CPU.GPR[ra]._i8[b] > (s8)(i10 & 0xff) ? 0xff : 0;
		WRAPPER_END(rt, ra, i10, 0);
	}
	void HGTI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		if(CPU.GPR[ra]._i32[3] > (s32)i10)	CPU.Stop();
		WRAPPER_END(rt, ra, i10, 0);
		c.mov(*pos_var, (CPU.PC >> 2) + 1);
		do_finalize = true;
	}
	void CLGTI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		for (u32 i = 0; i < 4; ++i)
		{
			CPU.GPR[rt]._u32[i] = (CPU.GPR[ra]._u32[i] > (u32)i10) ? 0xffffffff : 0x00000000;
		}
		WRAPPER_END(rt, ra, i10, 0);

		/*XmmVar v0(c);
		if (i10 == -1)
		{
			// zero result
			const XmmLink& v0 = XmmAlloc();
			c.xorps(v0.get(), v0.get());
			XmmFinalize(v0, rt);
		}
		else
		{
			//c.movdqa(v0, imm_xmm(s19_to_s32[i10 & 0x7ffff]));
			// TODO
			//c.movdqa(cpu_xmm(GPR[rt]), v0);
		}*/
	}
	void CLGTHI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		for(u32 i = 0; i < 8; ++i)
		{
			CPU.GPR[rt]._u16[i] = (CPU.GPR[ra]._u16[i] > (u16)i10) ? 0xffff : 0x0000;
		}
		WRAPPER_END(rt, ra, i10, 0);
	}
	void CLGTBI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		for (int b = 0; b < 16; b++)
			CPU.GPR[rt]._u8[b] = CPU.GPR[ra]._u8[b] > (u8)(i10 & 0xff) ? 0xff : 0;
		WRAPPER_END(rt, ra, i10, 0);
	}
	void HLGTI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		if(CPU.GPR[ra]._u32[3] > (u32)i10)	CPU.Stop();
		WRAPPER_END(rt, ra, i10, 0);
		c.mov(*pos_var, (CPU.PC >> 2) + 1);
		do_finalize = true;
	}
	void MPYI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		for (int w = 0; w < 4; w++)
			CPU.GPR[rt]._i32[w] = CPU.GPR[ra]._i16[w*2] * (s32)i10;
		WRAPPER_END(rt, ra, i10, 0);
	}
	void MPYUI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		for (int w = 0; w < 4; w++)
			CPU.GPR[rt]._u32[w] = CPU.GPR[ra]._u16[w*2] * (u16)(i10 & 0xffff);
		WRAPPER_END(rt, ra, i10, 0);
	}
	void CEQI(u32 rt, u32 ra, s32 i10)
	{
		const XmmLink& va = XmmGet(ra);
		c.pcmpeqd(va.get(), imm_xmm(s19_to_s32[i10 & 0x7ffff]));
		XmmFinalize(va, rt);
		LOG_OPCODE();
	}
	void CEQHI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		for (int h = 0; h < 8; h++)
			CPU.GPR[rt]._u16[h] = (CPU.GPR[ra]._i16[h] == (s16)(s32)i10) ? 0xffff : 0;
		WRAPPER_END(rt, ra, i10, 0);
	}
	void CEQBI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		for (int b = 0; b < 16; b++)
			CPU.GPR[rt]._i8[b] = (CPU.GPR[ra]._i8[b] == (s8)(i10 & 0xff)) ? 0xff : 0;
		WRAPPER_END(rt, ra, i10, 0);
	}
	void HEQI(u32 rt, u32 ra, s32 i10)
	{
		WRAPPER_BEGIN(rt, ra, i10, zz);
		if(CPU.GPR[ra]._i32[3] == (s32)i10)	CPU.Stop();
		WRAPPER_END(rt, ra, i10, 0);
		c.mov(*pos_var, (CPU.PC >> 2) + 1);
		do_finalize = true;
	}


	//0 - 6
	void HBRA(s32 ro, s32 i16)
	{ //i16 is shifted left by 2 while decoding
		LOG_OPCODE();
	}
	void HBRR(s32 ro, s32 i16)
	{
		LOG_OPCODE();
	}
	void ILA(u32 rt, u32 i18)
	{
		const XmmLink& vr = XmmAlloc();
		if (i18 == 0)
		{
			c.xorps(vr.get(), vr.get());
		}
		else
		{
			c.movaps(vr.get(), imm_xmm(s19_to_s32[i18 & 0x3ffff]));
		}
		XmmFinalize(vr, rt);
		LOG_OPCODE();
	}

	//0 - 3
	void SELB(u32 rt, u32 ra, u32 rb, u32 rc)
	{
		const XmmLink& vb = XmmGet(rb);
		const XmmLink& vc = XmmGet(rc);
		c.andps(vb.get(), vc.get());
		c.andnps(vc.get(), cpu_xmm(GPR[ra]));
		c.orps(vb.get(), vc.get());
		XmmFinalize(vb, rt);
		XmmFinalize(vc);
		LOG_OPCODE();
	}
	void SHUFB(u32 rt, u32 ra, u32 rb, u32 rc)
	{
		/*WRAPPER_BEGIN(rc, rt, ra, rb);
		const SPU_GPR_hdr _a = CPU.GPR[ra];
		const SPU_GPR_hdr _b = CPU.GPR[rb];
		for (int i = 0; i < 16; i++)
		{
			u8 b = CPU.GPR[rc]._u8[i];
			if (b & 0x80)
			{
				if (b & 0x40)
				{
					if (b & 0x20)
						CPU.GPR[rt]._u8[i] = 0x80;
					else
						CPU.GPR[rt]._u8[i] = 0xFF;
				}
				else
					CPU.GPR[rt]._u8[i] = 0x00;
			}
			else
			{
				if (b & 0x10)
					CPU.GPR[rt]._u8[i] = _b._u8[15 - (b & 0x0F)];
				else
					CPU.GPR[rt]._u8[i] = _a._u8[15 - (b & 0x0F)];
			}
		}
		WRAPPER_END(rc, rt, ra, rb);*/

		const XmmLink& v0 = XmmGet(rc); // v0 = mask
		const XmmLink& v1 = XmmAlloc();
		const XmmLink& v2 = XmmCopy(v0); // v2 = mask
		const XmmLink& v3 = XmmAlloc();
		const XmmLink& v4 = XmmAlloc();
		const XmmLink& vFF = XmmAlloc();
		// generate specific values:
		c.movdqa(v1.get(), imm_xmm(u8_to_u8[0xe0])); // v1 = 11100000
		c.movdqa(v3.get(), imm_xmm(u8_to_u8[0x80])); // v3 = 10000000
		c.pand(v2.get(), v1.get()); // filter mask      v2 = mask & 11100000
		c.movdqa(vFF.get(), v2.get()); // and copy      vFF = mask & 11100000
		c.movdqa(v4.get(), imm_xmm(u8_to_u8[0xc0])); // v4 = 11000000
		c.pcmpeqb(vFF.get(), v4.get()); // gen 0xff     vFF = (mask & 11100000 == 11000000) ? 0xff : 0
		c.movdqa(v4.get(), v2.get()); // copy again     v4 = mask & 11100000
		c.pand(v4.get(), v3.get()); // filter mask      v4 = mask & 10000000
		c.pcmpeqb(v2.get(), v1.get()); //               v2 = (mask & 11100000 == 11100000) ? 0xff : 0
		c.pcmpeqb(v4.get(), v3.get()); //               v4 = (mask & 10000000 == 10000000) ? 0xff : 0
		c.pand(v2.get(), v3.get()); // generate 0x80    v2 = (mask & 11100000 == 11100000) ? 0x80 : 0
		c.por(vFF.get(), v2.get()); // merge 0xff, 0x80 vFF = (mask & 11100000 == 11000000) ? 0xff : (mask & 11100000 == 11100000) ? 0x80 : 0
		c.pandn(v1.get(), v0.get()); // filter mask     v1 = mask & 00011111
		// select bytes from [rb]:
		c.movdqa(v2.get(), imm_xmm(u8_to_u8[15])); //   v2 = 00001111
		c.pxor(v1.get(), imm_xmm(u8_to_u8[0x10])); //   v1 = (mask & 00011111) ^ 00010000
		c.psubb(v2.get(), v1.get()); //                 v2 = 00001111 - ((mask & 00011111) ^ 00010000)
		c.movdqa(v1.get(), cpu_xmm(GPR[rb])); //        v1 = rb
		c.pshufb(v1.get(), v2.get()); //                v1 = select(rb, 00001111 - ((mask & 00011111) ^ 00010000))
		// select bytes from [ra]:
		c.pxor(v2.get(), imm_xmm(u8_to_u8[0xf0])); //   v2 = (00001111 - ((mask & 00011111) ^ 00010000)) ^ 11110000
		c.movdqa(v3.get(), cpu_xmm(GPR[ra])); //        v3 = ra
		c.pshufb(v3.get(), v2.get()); //                v3 = select(ra, (00001111 - ((mask & 00011111) ^ 00010000)) ^ 11110000)
		c.por(v1.get(), v3.get()); //                   v1 = select(rb, 00001111 - ((mask & 00011111) ^ 00010000)) | (v3)
		c.pandn(v4.get(), v1.get()); // filter result   v4 = v1 & ((mask & 10000000 == 10000000) ? 0 : 0xff)
		c.por(vFF.get(), v4.get()); // final merge      vFF = (mask & 10000000 == 10000000) ? ((mask & 11100000 == 11000000) ? 0xff : (mask & 11100000 == 11100000) ? 0x80 : 0) : (v1)
		XmmFinalize(vFF, rt);
		XmmFinalize(v4);
		XmmFinalize(v3);
		XmmFinalize(v2);
		XmmFinalize(v1);
		XmmFinalize(v0);
		LOG_OPCODE();
	}
	void MPYA(u32 rt, u32 ra, u32 rb, u32 rc)
	{
		WRAPPER_BEGIN(rt, ra, rb, rc);
		for (int w = 0; w < 4; w++)
			CPU.GPR[rt]._i32[w] = CPU.GPR[ra]._i16[w*2] * CPU.GPR[rb]._i16[w*2] + CPU.GPR[rc]._i32[w];
		WRAPPER_END(rt, ra, rb, rc);
	}
	void FNMS(u32 rt, u32 ra, u32 rb, u32 rc)
	{
		const XmmLink& va = XmmGet(ra);
		const XmmLink& vc = (ra == rc) ? XmmCopy(va) : XmmGet(rc);

		if (ra == rb)
		{
			c.mulps(va.get(), va.get());
		}
		else if (rb == rc)
		{
			c.mulps(va.get(), vc.get());
		}
		else
		{
			if (const XmmLink* vb = XmmRead(rb))
			{
				c.mulps(va.get(), vb->get());
			}
			else
			{
				c.mulps(va.get(), cpu_xmm(GPR[rb]));
			}
		}
		c.subps(vc.get(), va.get());
		XmmFinalize(vc, rt);
		XmmFinalize(va);
		LOG_OPCODE();
	}
	void FMA(u32 rt, u32 ra, u32 rb, u32 rc)
	{
		if (ra != rb && rb != rc && rc != ra)
		{
			const XmmLink& va = XmmGet(ra);
			if (const XmmLink* vb = XmmRead(rb))
			{
				c.mulps(va.get(), vb->get());
			}
			else
			{
				c.mulps(va.get(), cpu_xmm(GPR[rb]));
			}
			if (const XmmLink* vc = XmmRead(rc))
			{
				c.addps(va.get(), vc->get());
			}
			else
			{
				c.addps(va.get(), cpu_xmm(GPR[rc]));
			}
			XmmFinalize(va, rt);
		}
		else if (ra == rb && rb == rc)
		{
			const XmmLink& va = XmmGet(ra);
			const XmmLink& vc = XmmCopy(va);
			c.mulps(va.get(), va.get());
			c.addps(va.get(), vc.get());
			XmmFinalize(va, rt);
			XmmFinalize(vc);
		}
		else if (ra == rb)
		{
			const XmmLink& va = XmmGet(ra);
			c.mulps(va.get(), va.get());
			if (const XmmLink* vc = XmmRead(rc))
			{
				c.addps(va.get(), vc->get());
			}
			else
			{
				c.addps(va.get(), cpu_xmm(GPR[rc]));
			}
			XmmFinalize(va, rt);
		}
		else if (rb == rc)
		{
			const XmmLink& va = XmmGet(ra);
			if (const XmmLink* vc = XmmRead(rc))
			{
				c.mulps(va.get(), vc->get());
				c.addps(va.get(), vc->get());
			}
			else
			{
				const XmmLink& vb = XmmGet(rb);
				c.mulps(va.get(), vb.get());
				c.addps(va.get(), vb.get());
				XmmFinalize(vb);
			}
			XmmFinalize(va, rt);
		}
		else if (ra == rc)
		{
			const XmmLink& va = XmmGet(ra);
			const XmmLink& vc = XmmCopy(va);
			if (const XmmLink* vb = XmmRead(rb))
			{
				c.mulps(va.get(), vb->get());
			}
			else
			{
				c.mulps(va.get(), cpu_xmm(GPR[rb]));
			}
			c.addps(va.get(), vc.get());
			XmmFinalize(va, rt);
			XmmFinalize(vc);
		}
		else
		{
			throw __FUNCTION__"(): invalid case";
		}
		LOG_OPCODE();
	}
	void FMS(u32 rt, u32 ra, u32 rb, u32 rc)
	{
		if (ra != rb && rb != rc && rc != ra)
		{
			const XmmLink& va = XmmGet(ra);
			if (const XmmLink* vb = XmmRead(rb))
			{
				c.mulps(va.get(), vb->get());
			}
			else
			{
				c.mulps(va.get(), cpu_xmm(GPR[rb]));
			}
			if (const XmmLink* vc = XmmRead(rc))
			{
				c.subps(va.get(), vc->get());
			}
			else
			{
				c.subps(va.get(), cpu_xmm(GPR[rc]));
			}
			XmmFinalize(va, rt);
		}
		else if (ra == rb && rb == rc)
		{
			const XmmLink& va = XmmGet(ra);
			const XmmLink& vc = XmmCopy(va);
			c.mulps(va.get(), va.get());
			c.subps(va.get(), vc.get());
			XmmFinalize(va, rt);
			XmmFinalize(vc);
		}
		else if (ra == rb)
		{
			const XmmLink& va = XmmGet(ra);
			c.mulps(va.get(), va.get());
			if (const XmmLink* vc = XmmRead(rc))
			{
				c.subps(va.get(), vc->get());
			}
			else
			{
				c.subps(va.get(), cpu_xmm(GPR[rc]));
			}
			XmmFinalize(va, rt);
		}
		else if (rb == rc)
		{
			const XmmLink& va = XmmGet(ra);
			if (const XmmLink* vc = XmmRead(rc))
			{
				c.mulps(va.get(), vc->get());
				c.subps(va.get(), vc->get());
			}
			else
			{
				const XmmLink& vb = XmmGet(rb);
				c.mulps(va.get(), vb.get());
				c.subps(va.get(), vb.get());
				XmmFinalize(vb);
			}
			XmmFinalize(va, rt);
		}
		else if (ra == rc)
		{
			const XmmLink& va = XmmGet(ra);
			const XmmLink& vc = XmmCopy(va);
			if (const XmmLink* vb = XmmRead(rb))
			{
				c.mulps(va.get(), vb->get());
			}
			else
			{
				c.mulps(va.get(), cpu_xmm(GPR[rb]));
			}
			c.subps(va.get(), vc.get());
			XmmFinalize(va, rt);
			XmmFinalize(vc);
		}
		else
		{
			throw __FUNCTION__"(): invalid case";
		}
		LOG_OPCODE();
	}

	void UNK(u32 code, u32 opcode, u32 gcode)
	{
		UNK(fmt::Format("(SPURecompiler) Unimplemented opcode! (0x%08x, 0x%x, 0x%x)", code, opcode, gcode));
	}

	void UNK(const std::string& err)
	{
		ConLog.Error(err + fmt::Format(" #pc: 0x%x", CPU.PC));
		c.mov(cpu_qword(PC), (u32)CPU.PC);
		do_finalize = true;
		Emu.Pause();
	}
};

#undef c