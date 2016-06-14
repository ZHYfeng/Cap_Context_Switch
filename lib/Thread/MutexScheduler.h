/*
 * LockScheduler.h
 *
 *  Created on: 2015年1月7日
 *      Author: berserker
 */

#ifndef LIB_CORE_LOCKSCHEDULER_H_
#define LIB_CORE_LOCKSCHEDULER_H_

#include <list>
#include <vector>
#include <iostream>
#include "klee/Internal/ADT/RNG.h"

namespace klee {

class MutexScheduler {
public:
	enum MutexSchedulerType {
		FIFS,
		Preemptive,
		Random
	};
	MutexScheduler();
	virtual ~MutexScheduler();
	virtual unsigned selectNextItem() = 0;
	virtual void popAllItem(std::vector<unsigned>& allItem) = 0;
	virtual int itemNum() = 0;
	virtual bool isQueueEmpty() = 0;
	virtual void printName(std::ostream &os) = 0;
	virtual void addItem(unsigned threadId) = 0;
	virtual unsigned removeItem(unsigned threadId) = 0;
	virtual void printAllItem(std::ostream &os) = 0;
};

class FIFSMutexScheduler : public MutexScheduler {
private:
	std::list<unsigned> queue;
public:
	void printName(std::ostream &os) {
		os << "FIFS Mutex Scheduler\n";
	}
	FIFSMutexScheduler();
	virtual ~FIFSMutexScheduler();
	unsigned selectNextItem();
	void popAllItem(std::vector<unsigned>& allItem);
	int itemNum();
	bool isQueueEmpty();
	void addItem(unsigned threadId);
	unsigned removeItem(unsigned threadId);
	void printAllItem(std::ostream &os);
};

class PreemptiveMutexScheduler : public MutexScheduler {
private:
	std::list<unsigned> queue;
public:
	void printName(std::ostream &os) {
		os << "Preemptive Mutex Scheduler\n";
	}
	PreemptiveMutexScheduler();
	virtual ~PreemptiveMutexScheduler();
	unsigned selectNextItem();
	void popAllItem(std::vector<unsigned>& allItem);
	int itemNum();
	bool isQueueEmpty();
	void addItem(unsigned threadId);
	unsigned removeItem(unsigned threadId);
	void printAllItem(std::ostream &os);
};

//class GuidedMutexScheduler : public MutexScheduler {
//private:
//	MutexScheduler* baseScheduler;
//	std::vector<unsigned> threadList;
//	unsigned pos;
//public:
//	void printName(std::ostream &os) {
//		os << "Guided Lock Scheduler\n";
//	}
//	GuidedMutexScheduler(MutexSchedulerType secondarySchedulerType, std::vector<unsigned>& threadList);
//	virtual ~GuidedMutexScheduler();
//	unsigned selectNextItem();
//	void popAllItem(std::vector<unsigned>& allItem);
//	int itemNum();
//	bool isQueueEmpty();
//	void addItem(unsigned threadId);
//	unsigned removeItem(unsigned threadId);
//	void printAllItem(std::ostream &os);
//};

MutexScheduler* getMutexSchedulerByType(MutexScheduler::MutexSchedulerType type);

} /* namespace klee */

#endif /* LIB_CORE_LOCKSCHEDULER_H_ */
