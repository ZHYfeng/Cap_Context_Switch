/*
 * Thread.cpp
 *
 *  Created on: 2015年1月5日
 *      Author: berserker
 */

#include "Thread.h"

namespace klee {

	Thread::Thread(unsigned threadId, Thread* parentThread, KFunction* kf, AddressSpace *addressSpace) :
			pc(kf->instructions), prevPC(pc), incomingBBIndex(0), threadId(threadId), parentThread(parentThread), threadState(
					Thread::RUNNABLE), addressSpace(addressSpace) {
		for (unsigned i = 0; i < 5; i++) {
			vectorClock.push_back(0);
		}
		stack = new StackType(addressSpace);
		stack->realStack.reserve(10);
		stack->pushFrame(0, kf);
	}

	Thread::Thread(Thread& anotherThread, AddressSpace *addressSpace) :
			pc(anotherThread.pc), prevPC(anotherThread.prevPC), incomingBBIndex(anotherThread.incomingBBIndex), threadId(
					anotherThread.threadId), parentThread(anotherThread.parentThread), threadState(anotherThread.threadState), addressSpace(addressSpace) {
		stack = new StackType(addressSpace, anotherThread.stack);
		for (unsigned i = 0; i < 5; i++) {
			vectorClock.push_back(0);
		}
	}

	Thread::~Thread() {
		delete stack;
	}

	void Thread::dumpStack(llvm::raw_ostream &out) const {
		stack->dumpStack(out, prevPC);
	}

} /* namespace klee */
