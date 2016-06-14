/*
 * Mutex.cpp
 *
 *  Created on: Jul 24, 2014
 *      Author: klee
 */

#include "Mutex.h"

using namespace::std;

namespace klee {

Mutex::Mutex(unsigned id, string name)
	: id(id),
	  name(name),
	  isLocked(false),
	  lockedThreadId(0) {

}

Mutex::~Mutex() {
	// TODO Auto-generated destructor stub
}

void Mutex::lock(unsigned threadId) {
	this->lockedThreadId = threadId;
	this->isLocked = true;
}

void Mutex::unlock() {
	this->lockedThreadId = 0;
	this->isLocked = false;
}

bool Mutex::isThreadOwnMutex(unsigned threadId) {
	if (threadId == lockedThreadId) {
		return true;
	} else {
		return false;
	}
}
//void Mutex::addToBlockedList(Thread* thread) {
//	blockedList.push_front(thread);
//}
//
//void Mutex::removeFromBlockedList(Thread* thread) {
//	blockedList.remove(thread);
//}

}
