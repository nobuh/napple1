/* napple1 ncurses Apple 1 emulator
 * Copyright (C) 2008 Nobu Hatano
 *
 * Pom1 Apple 1 Emulator
 * Copyright (C) 2000 Verhille Arnaud
 * Copyright (C) 2006 John D. Corrado
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#define _XOPEN_SOURCE 500
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <pthread.h>

#include "m6502.h"
#include "pia6820.h"
#include "memory.h"

#define N 0x80
#define V 0x40
#define M 0x20
#define B 0x10
#define D 0x08
#define I 0x04
#define Z 0x02
#define C 0x01

static unsigned char accumulator, xRegister, yRegister, statusRegister = 0x24, stackPointer;
static int IRQ = 0, NMI = 0;
static unsigned short programCounter;
static unsigned char btmp;
static unsigned short op, opH, opL, ptr, ptrH, ptrL, tmp;
static pthread_t thread;
static int cycles;

/* sync is a synchronization between real time and ideal time of apple 1 */
static int sync_cycles; /* cycles per 1 sync */
static int sync_interval; /* sync interval time in m sec */
static long long interval_start; /* interval start time in m sec from 0 time */

static unsigned short memReadAbsolute(unsigned short adr)
{
	return (memRead(adr) | memRead((unsigned short)(adr + 1)) << 8);
}

static void synchronize(void)
{
	int processed; /* processed real time in m sec */
	int delay; /* delay m sec to be added to real time */
	struct timeval t;

	gettimeofday(&t, NULL);
	processed = (int)(t.tv_usec / 1000 
			  + t.tv_sec * 1000 
			  - interval_start);
	if (processed < 0)
		processed = 0;
	delay = sync_interval - processed;
	if (delay < 0)
		delay = 0;
	usleep((unsigned int)(delay * 1000)); /* m sec to u sec */

	gettimeofday(&t, NULL);
	interval_start = (long long)(t.tv_usec / 1000 
				     + t.tv_sec * 1000);
}

static void pushProgramCounter(void)
{
	memWrite((unsigned short)(stackPointer + 0x100), (unsigned char)(programCounter >> 8));
	stackPointer--;
	memWrite((unsigned short)(stackPointer + 0x100), (unsigned char)programCounter);
	stackPointer--;
	cycles += 2;
}

static void popProgramCounter(void)
{
	stackPointer++;
	programCounter = memRead((unsigned short)(stackPointer + 0x100));
	stackPointer++;
	programCounter += memRead((unsigned short)(stackPointer + 0x100)) << 8;
	cycles += 2;
}

static void handleIRQ(void)
{
	pushProgramCounter();
	memWrite((unsigned short)(0x100 + stackPointer), (unsigned char)(statusRegister & ~0x10));
	stackPointer--;
	statusRegister |= I;
	programCounter = memReadAbsolute(0xFFFE);
	cycles += 8;
}

static void handleNMI(void)
{
	pushProgramCounter();
	memWrite((unsigned short)(0x100 + stackPointer), (unsigned char)(statusRegister & ~0x10));
	stackPointer--;
	statusRegister |= I;
	NMI = 0;
	programCounter = memReadAbsolute(0xFFFA);
	cycles += 8;
}

static void Imp(void)
{
	cycles++;
}

static void Imm(void)
{
	op = programCounter++;
}

static void Zero(void)
{
	op = memRead(programCounter++);
	cycles++;
}

static void ZeroX(void)
{
	op = (memRead(programCounter++) + xRegister) & 0xFF;
	cycles++;
}

static void ZeroY(void)
{
	op = (memRead(programCounter++) + yRegister) & 0xFF;
	cycles++;
}

static void Abs(void)
{
	op = memReadAbsolute(programCounter);
	programCounter += 2;
	cycles += 2;
}

static void AbsX(void)
{
	opL = memRead(programCounter++) + xRegister;
	opH = memRead(programCounter++) << 8;
	cycles += 2;

	if (opL >= 0x100)
		cycles++;

	op = opH + opL;
}

static void AbsY(void)
{
	opL = memRead(programCounter++) + yRegister;
	opH = memRead(programCounter++) << 8;
	cycles += 2;

	if (opL >= 0x100)
		cycles++;

	op = opH + opL;
}

static void Ind(void)
{
	ptrL = memRead(programCounter++);
	ptrH = memRead(programCounter++) << 8;
	op = memRead((unsigned short)(ptrH + ptrL));
	ptrL = (ptrL + 1) & 0xFF;
	op += memRead((unsigned short)(ptrH + ptrL)) << 8;
	cycles += 4;
}

static void IndZeroX(void)
{
	ptr = xRegister + memRead(programCounter++);
	op = memRead(ptr);
	op += memRead((unsigned short)((ptr + 1) & 0xFF)) << 8;
	cycles += 3;
}

static void IndZeroY(void)
{
	ptr = memRead(programCounter++);
	opL = memRead(ptr) + yRegister;
	opH = memRead((unsigned short)(ptr + 1)) << 8;
	cycles += 3;

	if (opL >= 0x100)
		cycles++;

	op = opH + opL;
}

static void Rel(void)
{
	op = memRead(programCounter++);

	if (op >= 0x80)
		op |= 0xFF00;

	op += programCounter;
	cycles++;
}

static void WAbsX(void)
{
	opL = memRead(programCounter++) + xRegister;
	opH = memRead(programCounter++) << 8;
	cycles += 3;
	op = opH + opL;
}

static void WAbsY(void)
{
	opL = memRead(programCounter++) + yRegister;
	opH = memRead(programCounter++) << 8;
	cycles += 3;
	op = opH + opL;
}

void static WIndZeroY(void)
{
	ptr = memRead(programCounter++);
	opL = memRead(ptr) + yRegister;
	opH = memRead((unsigned short)((ptr + 1) & 0xFF)) << 8;
	cycles += 4;
	op = opH + opL;
}

static void setStatusRegisterNZ(unsigned char val)
{
	if (val & 0x80)
		statusRegister |= N;
	else
		statusRegister &= ~N;

	if (!val)
		statusRegister |= Z;
	else
		statusRegister &= ~Z;
}

static void LDA(void)
{
	accumulator = memRead(op);
	setStatusRegisterNZ(accumulator);
	cycles++;
}

static void LDX(void)
{
	xRegister = memRead(op);
	setStatusRegisterNZ(xRegister);
	cycles++;
}

static void LDY(void)
{
	yRegister = memRead(op);
	setStatusRegisterNZ(yRegister);
	cycles++;
}

static void STA(void)
{
	memWrite(op, accumulator);
	cycles++;
}

static void STX(void)
{
	memWrite(op, xRegister);
	cycles++;
}

static void STY(void)
{
	memWrite(op, yRegister);
	cycles++;
}

static void setFlagCarry(unsigned short val)
{
	if (val & 0x100)
		statusRegister |= C;
	else
		statusRegister &= ~C;
}

static void ADC(void)
{
	unsigned short Op1 = accumulator, Op2 = memRead(op);
	cycles++;

	if (statusRegister & D)
	{
		if (!((Op1 + Op2 + (statusRegister & C ? 1 : 0)) & 0xFF))
			statusRegister |= Z;
		else
			statusRegister &= ~Z;

		tmp = (Op1 & 0x0F) + (Op2 & 0x0F) + (statusRegister & C ? 1 : 0);
		accumulator = tmp < 0x0A ? tmp : tmp + 6;
		tmp = (Op1 & 0xF0) + (Op2 & 0xF0) + (tmp & 0xF0);
		
		if (tmp & 0x80)
			statusRegister |= N;
		else
			statusRegister &= ~N;

		if ((Op1 ^ tmp) & ~(Op1 ^ Op2) & 0x80)
			statusRegister |= V;
		else
			statusRegister &= ~V;

		tmp = (accumulator & 0x0F) | (tmp < 0xA0 ? tmp : tmp + 0x60);
		
		if (tmp >= 0x100)
			statusRegister |= C;
		else
			statusRegister &= ~C;

		accumulator = tmp & 0xFF;
	}
	else
	{
		tmp = Op1 + Op2 + (statusRegister & C ? 1 : 0);
		accumulator = tmp & 0xFF;
		
		if ((Op1 ^ accumulator) & ~(Op1 ^ Op2) & 0x80)
			statusRegister |= V;
		else
			statusRegister &= ~V;

		setFlagCarry(tmp);
		setStatusRegisterNZ(accumulator);
	}
}

static void setFlagBorrow(unsigned short val)
{
	if (!(val & 0x100))
		statusRegister |= C;
	else
		statusRegister &= ~C;
}

static void SBC(void)
{
	unsigned short Op1 = accumulator, Op2 = memRead(op);
	cycles++;

	if (statusRegister & D)
	{
		tmp = (Op1 & 0x0F) - (Op2 & 0x0F) - (statusRegister & C ? 0 : 1);
		accumulator = !(tmp & 0x10) ? tmp : tmp - 6;
		tmp = (Op1 & 0xF0) - (Op2 & 0xF0) - (accumulator & 0x10);
		accumulator = (accumulator & 0x0F) | (!(tmp & 0x100) ? tmp : tmp - 0x60);
		tmp = Op1 - Op2 - (statusRegister & C ? 0 : 1);
		setFlagBorrow(tmp);
		setStatusRegisterNZ((unsigned char)tmp);
	}
	else
	{
		tmp = Op1 - Op2 - (statusRegister & C ? 0 : 1);
		accumulator = tmp & 0xFF;

		if ((Op1 ^ Op2) & (Op1 ^ accumulator) & 0x80)
			statusRegister |= V;
		else
			statusRegister &= ~V;
		
		setFlagBorrow(tmp);
		setStatusRegisterNZ(accumulator);
	}
}

static void CMP(void)
{
	tmp = accumulator - memRead(op);
	cycles++;
	setFlagBorrow(tmp);
	setStatusRegisterNZ((unsigned char)tmp);
}

static void CPX(void)
{
	tmp = xRegister - memRead(op);
	cycles++;
	setFlagBorrow(tmp);
	setStatusRegisterNZ((unsigned char)tmp);
}

static void CPY(void)
{
	tmp = yRegister - memRead(op);
	cycles++;
	setFlagBorrow(tmp);
	setStatusRegisterNZ((unsigned char)tmp);
}

static void AND(void)
{
	accumulator &= memRead(op);
	cycles++;
	setStatusRegisterNZ(accumulator);
}

static void ORA(void)
{
	accumulator |= memRead(op);
	cycles++;
	setStatusRegisterNZ(accumulator);
}

static void EOR(void)
{
	accumulator ^= memRead(op);
	cycles++;
	setStatusRegisterNZ(accumulator);
}

static void ASL(void)
{
	btmp = memRead(op);
	
	if (btmp & 0x80)
		statusRegister |= C;
	else
		statusRegister &= ~C;

	btmp <<= 1;
	setStatusRegisterNZ(btmp);
	memWrite(op, btmp);
	cycles += 3;
}

static void ASL_A(void)
{
	tmp = accumulator << 1;
	accumulator = tmp & 0xFF;
	setFlagCarry(tmp);
	setStatusRegisterNZ(accumulator);
}

static void LSR(void)
{
	btmp = memRead(op);
	
	if (btmp & 1)
		statusRegister |= C;
	else
		statusRegister &= ~C;

	btmp >>= 1;
	setStatusRegisterNZ(btmp);
	memWrite(op, btmp);
	cycles += 3;
}

static void LSR_A(void)
{
	if (accumulator & 1)
		statusRegister |= C;
	else
		statusRegister &= ~C;

	accumulator >>= 1;
	setStatusRegisterNZ(accumulator);
}

static void ROL(void)
{
	int newCarry;

	btmp = memRead(op);
	newCarry = btmp & 0x80;
	btmp = (btmp << 1) | (statusRegister & C ? 1 : 0);

	if (newCarry)
		statusRegister |= C;
	else
		statusRegister &= ~C;
	
	setStatusRegisterNZ(btmp);
	memWrite(op, btmp);
	cycles += 3;
}

static void ROL_A(void)
{
	tmp = (accumulator << 1) | (statusRegister & C ? 1 : 0);
	accumulator = tmp & 0xFF;
	setFlagCarry(tmp);
	setStatusRegisterNZ(accumulator);
}

static void ROR(void)
{
	int newCarry;

	btmp = memRead(op);
	newCarry = btmp & 1;
	btmp = (btmp >> 1) | (statusRegister & C ? 0x80 : 0);
	
	if (newCarry)
		statusRegister |= C;
	else
		statusRegister &= ~C;

	setStatusRegisterNZ(btmp);
	memWrite(op, btmp);
	cycles += 3;
}

static void ROR_A(void)
{
	tmp = accumulator | (statusRegister & C ? 0x100 : 0);
	
	if (accumulator & 1)
		statusRegister |= C;
	else
		statusRegister &= ~C;

	accumulator = tmp >> 1;
	setStatusRegisterNZ(accumulator);
}

static void INC(void)
{
	btmp = memRead(op);
	btmp++;
	setStatusRegisterNZ(btmp);
	memWrite(op, btmp);
	cycles += 2;
}

static void DEC(void)
{
	btmp = memRead(op);
	btmp--;
	setStatusRegisterNZ(btmp);
	memWrite(op, btmp);
	cycles += 2;
}

static void INX(void)
{
	xRegister++;
	setStatusRegisterNZ(xRegister);
}

static void INY(void)
{
	yRegister++;
	setStatusRegisterNZ(yRegister);
}

static void DEX(void)
{
	xRegister--;
	setStatusRegisterNZ(xRegister);
}

static void DEY(void)
{
	yRegister--;
	setStatusRegisterNZ(yRegister);
}

static void BIT(void)
{
	btmp = memRead(op);

	if (btmp & 0x40)
		statusRegister |= V;
	else
		statusRegister &= ~V;
	
	if (btmp & 0x80)
		statusRegister |= N;
	else
		statusRegister &= ~N;

	if (!(btmp & accumulator))
		statusRegister |= Z;
	else
		statusRegister &= ~Z;

	cycles++;
}

static void PHA(void)
{
	memWrite((unsigned short)(0x100 + stackPointer), accumulator);
	stackPointer--;
	cycles++;
}

static void PHP(void)
{
	memWrite((unsigned short)(0x100 + stackPointer), statusRegister);
	stackPointer--;
	cycles++;
}

static void PLA(void)
{
	stackPointer++;
	accumulator = memRead((unsigned short)(stackPointer + 0x100));
	setStatusRegisterNZ(accumulator);
	cycles += 2;
}

static void PLP(void)
{
	stackPointer++;
	statusRegister = memRead((unsigned short)(stackPointer + 0x100));
	cycles += 2;
}

static void BRK(void)
{
	pushProgramCounter();
	PHP();
	statusRegister |= B;
	programCounter = memReadAbsolute(0xFFFE);
	cycles += 3;
}

static void RTI(void)
{
	PLP();
	popProgramCounter();
	cycles++;
}

static void JMP(void)
{
	programCounter = op;
}

static void RTS(void)
{
	popProgramCounter();
	programCounter++;
	cycles += 2;
}

static void JSR(void)
{
	opL = memRead(programCounter++);
	pushProgramCounter();
	programCounter = opL + (memRead(programCounter) << 8);
	cycles += 3;
}

static void branch(void)
{
	cycles++;

	if ((programCounter & 0xFF00) != (op & 0xFF00))
		cycles++;

	programCounter = op;
}

static void BNE(void)
{
	if (!(statusRegister & Z))
		branch();
}

static void BEQ(void)
{
	if (statusRegister & Z)
		branch();
}

static void BVC(void)
{
	if (!(statusRegister & V))
		branch();
}

static void BVS(void)
{
	if (statusRegister & V)
		branch();
}

static void BCC(void)
{
	if (!(statusRegister & C))
		branch();
}

static void BCS(void)
{
	if (statusRegister & C)
		branch();
}

static void BPL(void)
{
	if (!(statusRegister & N))
		branch();
}

static void BMI(void)
{
	if (statusRegister & N)
		branch();
}

static void TAX(void)
{
	xRegister = accumulator;
	setStatusRegisterNZ(accumulator);
}

static void TXA(void)
{
	accumulator = xRegister;
	setStatusRegisterNZ(accumulator);
}

static void TAY(void)
{
	yRegister = accumulator;
	setStatusRegisterNZ(accumulator);
}

static void TYA(void)
{
	accumulator = yRegister;
	setStatusRegisterNZ(accumulator);
}

static void TXS(void)
{
	stackPointer = xRegister;
}

static void TSX(void)
{
	xRegister = stackPointer;
	setStatusRegisterNZ(xRegister);
}

static void CLC(void)
{
	statusRegister &= ~C;
}

static void SEC(void)
{
	statusRegister |= C;
}

static void CLI(void)
{
	statusRegister &= ~I;
}

static void SEI(void)
{
	statusRegister |= I;
}

static void CLV(void)
{
	statusRegister &= ~V;
}

static void CLD(void)
{
	statusRegister &= ~D;
}

static void SED(void)
{
	statusRegister |= D;
}

static void NOP(void)
{
}

static void Unoff(void)
{
}

static void Unoff1(void)
{
}

static void Unoff2(void)
{
	programCounter++;
}

static void Unoff3(void)
{
	programCounter += 2;
}

static void Hang(void)
{
	programCounter--;
}

static void executeOpcode(void)
{
	unsigned char opcode = memRead(programCounter++);

	switch (opcode)
	{
	case 0x00:
	    Imm();
	    BRK();
	    break;
	case 0x01:
	    IndZeroX();
	    ORA();
	    break;
	case 0x02:
	    Hang();
	    break;
	case 0x03:
	    Unoff();
	    break;
	case 0x04:
	    Unoff2();
	    break;
	case 0x05:
	    Zero();
	    ORA();
	    break;
	case 0x06:
	    Zero();
	    ASL();
	    break;
	case 0x07:
	    Unoff();
	    break;
	case 0x08:
	    Imp();
	    PHP();
	    break;
	case 0x09:
	    Imm();
	    ORA();
	    break;
	case 0x0a:
	    Imp();
	    ASL_A();
	    break;
	case 0x0b:
	    Imm();
	    AND();
	    break;
	case 0x0c:
	    Unoff3();
	    break;
	case 0x0d:
	    Abs();
	    ORA();
	    break;
	case 0x0e:
	    Abs();
	    ASL();
	    break;
	case 0x0f:
	    Unoff();
	    break;
	case 0x10:
	    Rel();
	    BPL();
	    break;
	case 0x11:
	    IndZeroY();
	    ORA();
	    break;
	case 0x12:
	    Hang();
	    break;
	case 0x13:
	    Unoff();
	    break;
	case 0x14:
	    Unoff2();
	    break;
	case 0x15:
	    ZeroX();
	    ORA();
	    break;
	case 0x16:
	    ZeroX();
	    ASL();
	    break;
	case 0x17:
	    Unoff();
	    break;
	case 0x18:
	    Imp();
	    CLC();
	    break;
	case 0x19:
	    AbsY();
	    ORA();
	    break;
	case 0x1a:
	    Unoff1();
	    break;
	case 0x1b:
	    Unoff();
	    break;
	case 0x1c:
	    Unoff3();
	    break;
	case 0x1d:
	    AbsX();
	    ORA();
	    break;
	case 0x1e:
	    WAbsX();
	    ASL();
	    break;
	case 0x1f:
	    Unoff();
	    break;
	case 0x20:
	    JSR();
	    break;
	case 0x21:
	    IndZeroX();
	    AND();
	    break;
	case 0x22:
	    Hang();
	    break;
	case 0x23:
	    Unoff();
	    break;
	case 0x24:
	    Zero();
	    BIT();
	    break;
	case 0x25:
	    Zero();
	    AND();
	    break;
	case 0x26:
	    Zero();
	    ROL();
	    break;
	case 0x27:
	    Unoff();
	    break;
	case 0x28:
	    Imp();
	    PLP();
	    break;
	case 0x29:
	    Imm();
	    AND();
	    break;
	case 0x2a:
	    Imp();
	    ROL_A();
	    break;
	case 0x2b:
	    Imm();
	    AND();
	    break;
	case 0x2c:
	    Abs();
	    BIT();
	    break;
	case 0x2d:
	    Abs();
	    AND();
	    break;
	case 0x2e:
	    Abs();
	    ROL();
	    break;
	case 0x2f:
	    Unoff();
	    break;
	case 0x30:
	    Rel();
	    BMI();
	    break;
	case 0x31:
	    IndZeroY();
	    AND();
	    break;
	case 0x32:
	    Hang();
	    break;
	case 0x33:
	    Unoff();
	    break;
	case 0x34:
	    Unoff2();
	    break;
	case 0x35:
	    ZeroX();
	    AND();
	    break;
	case 0x36:
	    ZeroX();
	    ROL();
	    break;
	case 0x37:
	    Unoff();
	    break;
	case 0x38:
	    Imp();
	    SEC();
	    break;
	case 0x39:
	    AbsY();
	    AND();
	    break;
	case 0x3a:
	    Unoff1();
	    break;
	case 0x3b:
	    Unoff();
	    break;
	case 0x3c:
	    Unoff3();
	    break;
	case 0x3d:
	    AbsX();
	    AND();
	    break;
	case 0x3e:
	    WAbsX();
	    ROL();
	    break;
	case 0x3f:
	    Unoff();
	    break;
	case 0x40:
	    Imp();
	    RTI();
	    break;
	case 0x41:
	    IndZeroX();
	    EOR();
	    break;
	case 0x42:
	    Hang();
	    break;
	case 0x43:
	    Unoff();
	    break;
	case 0x44:
	    Unoff2();
	    break;
	case 0x45:
	    Zero();
	    EOR();
	    break;
	case 0x46:
	    Zero();
	    LSR();
	    break;
	case 0x47:
	    Unoff();
	    break;
	case 0x48:
	    Imp();
	    PHA();
	    break;
	case 0x49:
	    Imm();
	    EOR();
	    break;
	case 0x4a:
	    Imp();
	    LSR_A();
	    break;
	case 0x4b:
	    Unoff();
	    break;
	case 0x4c:
	    Abs();
	    JMP();
	    break;
	case 0x4d:
	    Abs();
	    EOR();
	    break;
	case 0x4e:
	    Abs();
	    LSR();
	    break;
	case 0x4f:
	    Unoff();
	    break;
	case 0x50:
	    Rel();
	    BVC();
	    break;
	case 0x51:
	    IndZeroY();
	    EOR();
	    break;
	case 0x52:
	    Hang();
	    break;
	case 0x53:
	    Unoff();
	    break;
	case 0x54:
	    Unoff2();
	    break;
	case 0x55:
	    ZeroX();
	    EOR();
	    break;
	case 0x56:
	    ZeroX();
	    LSR();
	    break;
	case 0x57:
	    Unoff();
	    break;
	case 0x58:
	    Imp();
	    CLI();
	    break;
	case 0x59:
	    AbsY();
	    EOR();
	    break;
	case 0x5a:
	    Unoff1();
	    break;
	case 0x5b:
	    Unoff();
	    break;
	case 0x5c:
	    Unoff3();
	    break;
	case 0x5d:
	    AbsX();
	    EOR();
	    break;
	case 0x5e:
	    WAbsX();
	    LSR();
	    break;
	case 0x5f:
	    Unoff();
	    break;
	case 0x60:
	    Imp();
	    RTS();
	    break;
	case 0x61:
	    IndZeroX();
	    ADC();
	    break;
	case 0x62:
	    Hang();
	    break;
	case 0x63:
	    Unoff();
	    break;
	case 0x64:
	    Unoff2();
	    break;
	case 0x65:
	    Zero();
	    ADC();
	    break;
	case 0x66:
	    Zero();
	    ROR();
	    break;
	case 0x67:
	    Unoff();
	    break;
	case 0x68:
	    Imp();
	    PLA();
	    break;
	case 0x69:
	    Imm();
	    ADC();
	    break;
	case 0x6a:
	    Imp();
	    ROR_A();
	    break;
	case 0x6b:
	    Unoff();
	    break;
	case 0x6c:
	    Ind();
	    JMP();
	    break;
	case 0x6d:
	    Abs();
	    ADC();
	    break;
	case 0x6e:
	    Abs();
	    ROR();
	    break;
	case 0x6f:
	    Unoff();
	    break;
	case 0x70:
	    Rel();
	    BVS();
	    break;
	case 0x71:
	    IndZeroY();
	    ADC();
	    break;
	case 0x72:
	    Hang();
	    break;
	case 0x73:
	    Unoff();
	    break;
	case 0x74:
	    Unoff2();
	    break;
	case 0x75:
	    ZeroX();
	    ADC();
	    break;
	case 0x76:
	    ZeroX();
	    ROR();
	    break;
	case 0x77:
	    Unoff();
	    break;
	case 0x78:
	    Imp();
	    SEI();
	    break;
	case 0x79:
	    AbsY();
	    ADC();
	    break;
	case 0x7a:
	    Unoff1();
	    break;
	case 0x7b:
	    Unoff();
	    break;
	case 0x7c:
	    Unoff3();
	    break;
	case 0x7d:
	    AbsX();
	    ADC();
	    break;
	case 0x7e:
	    WAbsX();
	    ROR();
	    break;
	case 0x7f:
	    Unoff();
	    break;
	case 0x80:
	    Unoff2();
	    break;
	case 0x81:
	    IndZeroX();
	    STA();
	    break;
	case 0x82:
	    Unoff2();
	    break;
	case 0x83:
	    Unoff();
	    break;
	case 0x84:
	    Zero();
	    STY();
	    break;
	case 0x85:
	    Zero();
	    STA();
	    break;
	case 0x86:
	    Zero();
	    STX();
	    break;
	case 0x87:
	    Unoff();
	    break;
	case 0x88:
	    Imp();
	    DEY();
	    break;
	case 0x89:
	    Unoff2();
	    break;
	case 0x8a:
	    Imp();
	    TXA();
	    break;
	case 0x8b:
	    Unoff();
	    break;
	case 0x8c:
	    Abs();
	    STY();
	    break;
	case 0x8d:
	    Abs();
	    STA();
	    break;
	case 0x8e:
	    Abs();
	    STX();
	    break;
	case 0x8f:
	    Unoff();
	    break;
	case 0x90:
	    Rel();
	    BCC();
	    break;
	case 0x91:
	    WIndZeroY();
	    STA();
	    break;
	case 0x92:
	    Hang();
	    break;
	case 0x93:
	    Unoff();
	    break;
	case 0x94:
	    ZeroX();
	    STY();
	    break;
	case 0x95:
	    ZeroX();
	    STA();
	    break;
	case 0x96:
	    ZeroY();
	    STX();
	    break;
	case 0x97:
	    Unoff();
	    break;
	case 0x98:
	    Imp();
	    TYA();
	    break;
	case 0x99:
	    WAbsY();
	    STA();
	    break;
	case 0x9a:
	    Imp();
	    TXS();
	    break;
	case 0x9b:
	    Unoff();
	    break;
	case 0x9c:
	    Unoff();
	    break;
	case 0x9d:
	    WAbsX();
	    STA();
	    break;
	case 0x9e:
	    Unoff();
	    break;
	case 0x9f:
	    Unoff();
	    break;
	case 0xa0:
	    Imm();
	    LDY();
	    break;
	case 0xa1:
	    IndZeroX();
	    LDA();
	    break;
	case 0xa2:
	    Imm();
	    LDX();
	    break;
	case 0xa3:
	    Unoff();
	    break;
	case 0xa4:
	    Zero();
	    LDY();
	    break;
	case 0xa5:
	    Zero();
	    LDA();
	    break;
	case 0xa6:
	    Zero();
	    LDX();
	    break;
	case 0xa7:
	    Unoff();
	    break;
	case 0xa8:
	    Imp();
	    TAY();
	    break;
	case 0xa9:
	    Imm();
	    LDA();
	    break;
	case 0xaa:
	    Imp();
	    TAX();
	    break;
	case 0xab:
	    Unoff();
	    break;
	case 0xac:
	    Abs();
	    LDY();
	    break;
	case 0xad:
	    Abs();
	    LDA();
	    break;
	case 0xae:
	    Abs();
	    LDX();
	    break;
	case 0xaf:
	    Unoff();
	    break;
	case 0xb0:
	    Rel();
	    BCS();
	    break;
	case 0xb1:
	    IndZeroY();
	    LDA();
	    break;
	case 0xb2:
	    Hang();
	    break;
	case 0xb3:
	    Unoff();
	    break;
	case 0xb4:
	    ZeroX();
	    LDY();
	    break;
	case 0xb5:
	    ZeroX();
	    LDA();
	    break;
	case 0xb6:
	    ZeroY();
	    LDX();
	    break;
	case 0xb7:
	    Unoff();
	    break;
	case 0xb8:
	    Imp();
	    CLV();
	    break;
	case 0xb9:
	    AbsY();
	    LDA();
	    break;
	case 0xba:
	    Imp();
	    TSX();
	    break;
	case 0xbb:
	    Unoff();
	    break;
	case 0xbc:
	    AbsX();
	    LDY();
	    break;
	case 0xbd:
	    AbsX();
	    LDA();
	    break;
	case 0xbe:
	    AbsY();
	    LDX();
	    break;
	case 0xbf:
	    Unoff();
	    break;
	case 0xc0:
	    Imm();
	    CPY();
	    break;
	case 0xc1:
	    IndZeroX();
	    CMP();
	    break;
	case 0xc2:
	    Unoff2();
	    break;
	case 0xc3:
	    Unoff();
	    break;
	case 0xc4:
	    Zero();
	    CPY();
	    break;
	case 0xc5:
	    Zero();
	    CMP();
	    break;
	case 0xc6:
	    Zero();
	    DEC();
	    break;
	case 0xc7:
	    Unoff();
	    break;
	case 0xc8:
	    Imp();
	    INY();
	    break;
	case 0xc9:
	    Imm();
	    CMP();
	    break;
	case 0xca:
	    Imp();
	    DEX();
	    break;
	case 0xcb:
	    Unoff();
	    break;
	case 0xcc:
	    Abs();
	    CPY();
	    break;
	case 0xcd:
	    Abs();
	    CMP();
	    break;
	case 0xce:
	    Abs();
	    DEC();
	    break;
	case 0xcf:
	    Unoff();
	    break;
	case 0xd0:
	    Rel();
	    BNE();
	    break;
	case 0xd1:
	    IndZeroY();
	    CMP();
	    break;
	case 0xd2:
	    Hang();
	    break;
	case 0xd3:
	    Unoff();
	    break;
	case 0xd4:
	    Unoff2();
	    break;
	case 0xd5:
	    ZeroX();
	    CMP();
	    break;
	case 0xd6:
	    ZeroX();
	    DEC();
	    break;
	case 0xd7:
	    Unoff();
	    break;
	case 0xd8:
	    Imp();
	    CLD();
	    break;
	case 0xd9:
	    AbsY();
	    CMP();
	    break;
	case 0xda:
	    Unoff1();
	    break;
	case 0xdb:
	    Unoff();
	    break;
	case 0xdc:
	    Unoff3();
	    break;
	case 0xdd:
	    AbsX();
	    CMP();
	    break;
	case 0xde:
	    WAbsX();
	    DEC();
	    break;
	case 0xdf:
	    Unoff();
	    break;
	case 0xe0:
	    Imm();
	    CPX();
	    break;
	case 0xe1:
	    IndZeroX();
	    SBC();
	    break;
	case 0xe2:
	    Unoff2();
	    break;
	case 0xe3:
	    Unoff();
	    break;
	case 0xe4:
	    Zero();
	    CPX();
	    break;
	case 0xe5:
	    Zero();
	    SBC();
	    break;
	case 0xe6:
	    Zero();
	    INC();
	    break;
	case 0xe7:
	    Unoff();
	    break;
	case 0xe8:
	    Imp();
	    INX();
	    break;
	case 0xe9:
	    Imm();
	    SBC();
	    break;
	case 0xea:
	    Imp();
	    NOP();
	    break;
	case 0xeb:
	    Imm();
	    SBC();
	    break;
	case 0xec:
	    Abs();
	    CPX();
	    break;
	case 0xed:
	    Abs();
	    SBC();
	    break;
	case 0xee:
	    Abs();
	    INC();
	    break;
	case 0xef:
	    Unoff();
	    break;
	case 0xf0:
	    Rel();
	    BEQ();
	    break;
	case 0xf1:
	    IndZeroY();
	    SBC();
	    break;
	case 0xf2:
	    Hang();
	    break;
	case 0xf3:
	    Unoff();
	    break;
	case 0xf4:
	    Unoff2();
	    break;
	case 0xf5:
	    ZeroX();
	    SBC();
	    break;
	case 0xf6:
	    ZeroX();
	    INC();
	    break;
	case 0xf7:
	    Unoff();
	    break;
	case 0xf8:
	    Imp();
	    SED();
	    break;
	case 0xf9:
	    AbsY();
	    SBC();
	    break;
	case 0xfa:
	    Unoff1();
	    break;
	case 0xfb:
	    Unoff();
	    break;
	case 0xfc:
	    Unoff3();
	    break;
	case 0xfd:
	    AbsX();
	    SBC();
	    break;
	case 0xfe:
	    WAbsX();
	    INC();
	    break;
	case 0xff:
	    Unoff();
	    break;
	}
}

static int runM6502(void *data)
{
	while (1)
	{
		synchronize();
		
		cycles = 0;

		while (cycles < sync_cycles)
		{
			if (!(statusRegister & I) && IRQ)
				handleIRQ();
			if (NMI)
				handleNMI();

			executeOpcode();
		}
	}

	return 0;
}

void startM6502(void)
{
	struct timeval t;

	if (!thread)
	{
		gettimeofday(&t, NULL);
		interval_start = (long long)(t.tv_usec / 1000 + 
					     t.tv_sec * 1000);
		pthread_create(&thread, NULL,(void *)&runM6502, NULL);	       
	}
}

void stopM6502(void)
{
	if (thread)
	{
		pthread_cancel(thread);
	}
}

void resetM6502(void)
{
	statusRegister |= I;
	stackPointer = 0xFF;
	programCounter = memReadAbsolute(0xFFFC);
}

/* freq      Processr clock speed in Hz 
 * interval  Interval of sync between real and ideal time in msec 
 */
void setSpeed(int freq, int interval)
{
	/* freq = cycles/1sec, cycles/1msec = freq/1000 */
	sync_cycles = freq / 1000 * interval;
	sync_interval = interval;
}

void setIRQ(int state)
{
	IRQ = state;
}

void setNMI(void)
{
	NMI = 1;
}
