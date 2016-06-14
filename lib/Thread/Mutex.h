/*
 * Mutex.h
 *
 *  Created on: Jul 24, 2014
 *      Author: klee
 */

#ifndef MUTEX_H_
#define MUTEX_H_

#include "MutexScheduler.h"
#include <string>

namespace klee {

class Mutex {

public:
	unsigned id;
	std::string name;

private:
	bool isLocked;
	unsigned lockedThreadId;
	//std::list<Thread*> blockedList;
	//unsigned lockedThread;
	//MutexScheduler* blockedList;

public:

	bool isMutexLocked() {
		return isLocked;
	}

	unsigned getLockedThread() {
		return lockedThreadId	;
	}

	void lock(unsigned thread);

	void unlock();

	bool isThreadOwnMutex(unsigned threadId);
//	void addToBlockedList(Thread* thread);
//
//	void removeFromBlockedList(Thread* thread);

	Mutex(unsigned id, std::string name);

	virtual ~Mutex();
};

}
#endif /* MUTEX_H_ */
