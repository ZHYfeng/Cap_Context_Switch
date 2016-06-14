/*
 * Thread.cpp
 *
 *  Created on: 2015年1月5日
 *      Author: berserker
 */

#include "Thread.h"



using namespace::llvm;
using namespace::std;

namespace klee {



Thread::Thread(unsigned threadId, Thread* parentThread, KFunction* kf)
	: pc(kf->instructions),
	  prevPC(pc),
	  incomingBBIndex(0),
	  threadId(threadId),
	  parentThread(parentThread),
	  threadState(Thread::RUNNABLE),
	  stack(parentThread->stack) {
	for(unsigned i = 0; i < 5; i++) {
		vectorClock.push_back(0);
	}
	stack.realStack.reserve(10);
	stack.pushFrame(0, kf);
}

Thread::Thread(Thread& anotherThread)
	: pc(anotherThread.pc),
	  prevPC(anotherThread.prevPC),
	  incomingBBIndex(anotherThread.incomingBBIndex),
	  threadId(anotherThread.threadId),
	  parentThread(anotherThread.parentThread),
	  threadState(anotherThread.threadState),
	  stack(anotherThread.stack) {
	for(unsigned i = 0; i < 5; i++) {
		vectorClock.push_back(0);
	}
}

Thread::~Thread() {
	stack.~StackType();
}


} /* namespace klee */
