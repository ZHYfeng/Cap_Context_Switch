/*
 * ThreadList.cpp
 *
 *  Created on: 2015年1月14日
 *      Author: berserker
 */

#include "ThreadList.h"
#include <iostream>

using namespace::std;

namespace klee {

ThreadList::ThreadList() : threadNum(0) {
	allThreads.resize(20, 0);
	index.reserve(20);
	//thread id从1开始，根据thread id分配数组元素
	//allThreads.push_back(0);
}

ThreadList::~ThreadList() {

}


ThreadList::iterator ThreadList::begin() {
	ThreadListIterator l(this);
	return l;
}

ThreadList::iterator ThreadList::end() {
	return ThreadListIterator(this, threadNum);
}

ThreadList::iterator ThreadList::begin() const {
	ThreadListIterator l(this);
	return l;
}

ThreadList::iterator ThreadList::end() const {
	return ThreadListIterator(this, threadNum);
}

void ThreadList::addThread(Thread* thread) {
	if (allThreads.size() <= thread->threadId) {
		allThreads.resize(thread->threadId * 2, 0);
	}
	assert(!allThreads[thread->threadId]);
	allThreads[thread->threadId] = thread;
	index.push_back(thread->threadId);
	threadNum++;
}

map<unsigned, Thread*> ThreadList::getAllUnfinishedThreads() {
	map<unsigned, Thread*> result;
	for (ThreadList::iterator ti = begin(), te = end(); ti != te; ti++) {
		Thread* thread = *ti;
		if (*ti && !thread->isTerminated()) {
			result.insert(make_pair(thread->threadId, thread));
		}
	}
	return result;
}

Thread* ThreadList::findThreadById(unsigned threadId) {
	assert(threadId < allThreads.size() && allThreads[threadId]);
	return allThreads[threadId];
}

int ThreadList::getThreadNum() {
	return threadNum;
}

Thread* ThreadList::getLastThread() {
	return allThreads[index.back()];
}


ThreadListIterator::ThreadListIterator(ThreadList* threadList, unsigned pos) :
	threadList(threadList),
	currentPos(pos) {
}

ThreadListIterator::ThreadListIterator(const ThreadList* threadList, unsigned pos) :
	threadList(threadList),
	currentPos(pos) {

}

ThreadListIterator& ThreadListIterator::operator ++(int) {
	currentPos++;
	return *this;
}

Thread* ThreadListIterator::operator*() {
	return threadList->allThreads[threadList->index[currentPos]];
}

bool ThreadListIterator::operator==(ThreadListIterator& another) {
	if (threadList == another.threadList && currentPos == another.currentPos) {
		return true;
	} else {
		return false;
	}
}

bool ThreadListIterator::operator!=(ThreadListIterator& another) {
	return !(*this == another);
}

} /* namespace klee */
