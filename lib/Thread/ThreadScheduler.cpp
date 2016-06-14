/*
 * ThreadScheduler.cpp
 *
 *  Created on: 2015年1月7日
 *      Author: berserker
 */

#include "ThreadScheduler.h"

#include <llvm/IR/Instruction.h>
#include <cassert>
#include <string>

#include "../../include/klee/ExecutionState.h"
#include "../../include/klee/Internal/Module/InstructionInfoTable.h"
#include "../../include/klee/Internal/Module/KInstruction.h"

#define MAXINST 100

using namespace::std;

namespace klee {

ThreadScheduler* getThreadSchedulerByType(ThreadScheduler::ThreadSchedulerType type) {
	ThreadScheduler* scheduler = NULL;
	switch (type) {
	case ThreadScheduler::RR: {
		scheduler = new RRThreadScheduler();
		break;
	}
	case ThreadScheduler::FIFS: {
		scheduler = new FIFSThreadScheduler();
		break;
	}
	case ThreadScheduler::Preemptive: {
		scheduler = new PreemptiveThreadScheduler();
		break;
	}
	default: {
		assert("ThreadSchedulerType error");
	}
	}
	return scheduler;
}

ThreadScheduler::ThreadScheduler() {

}

ThreadScheduler::~ThreadScheduler() {

}

RRThreadScheduler::RRThreadScheduler() {
	count = 0;
}

//拷贝构造，没用
RRThreadScheduler::RRThreadScheduler(RRThreadScheduler& scheduler, map<unsigned, Thread*> &threadMap) {
	for (list<Thread*>::iterator ti = scheduler.queue.begin(), te = scheduler.queue.end(); ti != te; ti++) {
		queue.push_back(threadMap[(*ti)->threadId]);
	}
}

RRThreadScheduler::~RRThreadScheduler() {

}

//考虑使用迭代器而不是front();
Thread* RRThreadScheduler::selectCurrentItem() {
	return queue.front();
}

Thread* RRThreadScheduler::selectNextItem() {
	if (count > MAXINST) {
		reSchedule();
	}
	count++;
//	Thread* thread = queue.front();
	return queue.front();
}

void RRThreadScheduler::popAllItem(vector<Thread*>& allItem) {
	allItem.reserve(queue.size());
	for (list<Thread*>::iterator ti = queue.begin(), te = queue.end(); ti != te; ti++) {
		allItem.push_back(*ti);
	}
	queue.clear();
}

int RRThreadScheduler::itemNum() {
	return queue.size();
}

bool RRThreadScheduler::isSchedulerEmpty() {
	return queue.empty();
}

void RRThreadScheduler::addItem(Thread* item) {
	queue.push_back(item);
}

void RRThreadScheduler::removeItem(Thread* item) {
	for (list<Thread*>::iterator ti = queue.begin(), te = queue.end(); ti!= te; ti++) {
		if (*ti == item) {
			queue.erase(ti);
			break;
		}
	}
}

void RRThreadScheduler::printAllItem(ostream &os) {
	for (list<Thread*>::iterator ti = queue.begin(), te = queue.end(); ti!= te; ti++) {
		Thread* thread = *ti;
		os << thread->threadId << " state: " << thread->threadState << " current inst: " << thread->pc->inst->getOpcodeName() << " ";
		if (thread->threadState == Thread::TERMINATED) {
			KInstruction* ki = thread->pc;
			os << ki->info->file << " " << ki->info->line;
		}
		os << endl;
	}
}

void RRThreadScheduler::reSchedule() {
	Thread* thread = queue.front();
	queue.pop_front();
	queue.push_back(thread);
	count = 0;
}

void RRThreadScheduler::setCountZero() {
	count = 0;
}

FIFSThreadScheduler::FIFSThreadScheduler() {

}

FIFSThreadScheduler::FIFSThreadScheduler(FIFSThreadScheduler& scheduler, map<unsigned, Thread*> &threadMap) {
	for (list<Thread*>::iterator ti = scheduler.queue.begin(), te = scheduler.queue.end(); ti != te; ti++) {
		queue.push_back(threadMap[(*ti)->threadId]);
	}
}

FIFSThreadScheduler::~FIFSThreadScheduler() {

}

Thread* FIFSThreadScheduler::selectCurrentItem() {
	return selectNextItem();
}

Thread* FIFSThreadScheduler::selectNextItem() {
	return queue.front();
}

void FIFSThreadScheduler::popAllItem(vector<Thread*>& allItem) {
	allItem.reserve(queue.size());
	for (list<Thread*>::iterator ti = queue.begin(), te = queue.end(); ti != te; ti++) {
		allItem.push_back(*ti);
	}
	queue.clear();
}

int FIFSThreadScheduler::itemNum() {
	return queue.size();
}

bool FIFSThreadScheduler::isSchedulerEmpty() {
	return queue.empty();
}

void FIFSThreadScheduler::addItem(Thread* item) {
	queue.push_back(item);
}

void FIFSThreadScheduler::removeItem(Thread* item) {
	for (list<Thread*>::iterator ti = queue.begin(), te = queue.end(); ti!= te; ti++) {
		if (*ti == item) {
			queue.erase(ti);
			break;
		}
	}
}

void FIFSThreadScheduler::printAllItem(ostream &os) {
	for (list<Thread*>::iterator ti = queue.begin(), te = queue.end(); ti!= te; ti++) {
		Thread* thread = *ti;
		os << thread->threadId << " state: " << thread->threadState << " current inst: " << thread->pc->inst->getOpcodeName() << " ";
		if (thread->threadState == Thread::TERMINATED) {
			KInstruction* ki = thread->pc;
			os << ki->info->file << " " << ki->info->line;
		}
		os << endl;
	}
}

void FIFSThreadScheduler::reSchedule() {
	Thread* thread = queue.front();
	queue.pop_front();
	queue.push_back(thread);
}

PreemptiveThreadScheduler::PreemptiveThreadScheduler() {

}

PreemptiveThreadScheduler::PreemptiveThreadScheduler(PreemptiveThreadScheduler& scheduler, map<unsigned, Thread*> &threadMap) {
	for (list<Thread*>::iterator ti = scheduler.queue.begin(), te = scheduler.queue.end(); ti != te; ti++) {
		queue.push_back(threadMap[(*ti)->threadId]);
	}
}

PreemptiveThreadScheduler::~PreemptiveThreadScheduler() {

}

Thread* PreemptiveThreadScheduler::selectCurrentItem() {
	return selectNextItem();
}

Thread* PreemptiveThreadScheduler::selectNextItem() {
	return queue.back();
}

void PreemptiveThreadScheduler::popAllItem(std::vector<Thread*>& allItem) {
	allItem.reserve(queue.size());
	for (list<Thread*>::iterator ti = queue.begin(), te = queue.end(); ti != te; ti++) {
		allItem.push_back(*ti);
	}
	queue.clear();
}

int PreemptiveThreadScheduler::itemNum() {
	return queue.size();
}

bool PreemptiveThreadScheduler::isSchedulerEmpty() {
	return queue.empty();
}

void PreemptiveThreadScheduler::addItem(Thread* item) {
	queue.push_back(item);
}

void PreemptiveThreadScheduler::removeItem(Thread* item) {
	for (list<Thread*>::iterator ti = queue.begin(), te = queue.end(); ti != te; ti++) {
		if (*ti == item) {
			queue.erase(ti);
			break;
		}
	}
}

void PreemptiveThreadScheduler::printAllItem(std::ostream &os) {
	for (list<Thread*>::iterator ti = queue.begin(), te = queue.end(); ti!= te; ti++) {
		Thread* thread = *ti;
		os << thread->threadId << " state: " << thread->threadState << " current inst: " << thread->pc->inst->getOpcodeName() << " ";
		if (thread->threadState == Thread::TERMINATED) {
			KInstruction* ki = thread->pc;
			os << ki->info->file << " " << ki->info->line;
		}
		os << endl;
	}
}

void PreemptiveThreadScheduler::reSchedule() {
	Thread* thread = queue.back();
	queue.pop_back();
	list<Thread*>::iterator ti = queue.end();
	ti--;
	queue.insert(ti, thread);
}

GuidedThreadScheduler::GuidedThreadScheduler(ExecutionState* state, ThreadSchedulerType schedulerType, Prefix* prefix)
	: prefix(prefix),
	  state(state) {
	subScheduler = getThreadSchedulerByType(schedulerType);
}

GuidedThreadScheduler::~GuidedThreadScheduler() {
	delete subScheduler;
}

Thread* GuidedThreadScheduler::selectCurrentItem() {
	return selectNextItem();
}

Thread* GuidedThreadScheduler::selectNextItem() {
	Thread* thread = NULL;
	if (!prefix->isFinished()) {
		unsigned threadId = prefix->getCurrentEventThreadId();
		thread = state->findThreadById(threadId);
	} else {
		thread = subScheduler->selectNextItem();
	}
	return thread;
}

void GuidedThreadScheduler::popAllItem(vector<Thread*>& allItem) {
	subScheduler->popAllItem(allItem);
}

int GuidedThreadScheduler::itemNum() {
	return subScheduler->itemNum();
}

bool GuidedThreadScheduler::isSchedulerEmpty() {
	return subScheduler->isSchedulerEmpty();
}

void GuidedThreadScheduler::addItem(Thread* item) {
	subScheduler->addItem(item);
}

void GuidedThreadScheduler::removeItem(Thread* item) {
	subScheduler->removeItem(item);
}

void GuidedThreadScheduler::printAllItem(std::ostream &os) {
	subScheduler->printAllItem(os);
}

void GuidedThreadScheduler::reSchedule() {
	subScheduler->reSchedule();
}

} /* namespace klee */
