/*
 * StackFrame.cpp
 *
 *  Created on: Jun 14, 2016
 *      Author: zhy
 */

#include "StackFrame.h"

namespace klee {

	StackFrame::StackFrame(KInstIterator _caller, KFunction *_kf) :
			caller(_caller), kf(_kf), callPathNode(0), minDistToUncoveredOnReturn(0), varargs(0) {
		locals = new Cell[kf->numRegisters];
	}

	StackFrame::StackFrame(const StackFrame &s) :
			caller(s.caller), kf(s.kf), callPathNode(s.callPathNode), allocas(s.allocas), minDistToUncoveredOnReturn(
					s.minDistToUncoveredOnReturn), varargs(s.varargs) {
		locals = new Cell[s.kf->numRegisters];
		for (unsigned i = 0; i < s.kf->numRegisters; i++)
			locals[i] = s.locals[i];
	}

	StackFrame::~StackFrame() {
		delete[] locals;
	}

} /* namespace klee */
