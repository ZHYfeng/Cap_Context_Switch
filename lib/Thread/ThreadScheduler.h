/*
 * ThreadScheduler.h
 *
 *  Created on: 2015年1月7日
 *      Author: berserker
 */

#ifndef LIB_CORE_THREADSCHEDULER_H_
#define LIB_CORE_THREADSCHEDULER_H_

#include <list>
#include <vector>
#include <map>
#include <iostream>
#include <sys/time.h>

#include "Event.h"
#include "klee/Internal/ADT/RNG.h"
#include "Thread.h"
#include "Prefix.h"

namespace klee {

class ExecutionState;

class ThreadScheduler {
public:
	ThreadScheduler();
	virtual ~ThreadScheduler();
	virtual Thread* selectCurrentItem() = 0;
	virtual Thread* selectNextItem() = 0;
	virtual void popAllItem(std::vector<Thread*>& allItem) = 0;
	virtual int itemNum() = 0;
	virtual bool isSchedulerEmpty() = 0;
	virtual void printName(std::ostream &os) = 0;
	virtual void addItem(Thread* item) = 0;
	virtual void removeItem(Thread* item) = 0;
	virtual void printAllItem(std::ostream &os) = 0;
	virtual void reSchedule() = 0;

	enum ThreadSchedulerType {
		RR,
		FIFS,
		Preemptive
	};
};

/**
 * RR Scheduler
 */
class RRThreadScheduler : public ThreadScheduler {
private:
	std::list<Thread*> queue;
	unsigned int count;
public:
	RRThreadScheduler();
	RRThreadScheduler(RRThreadScheduler& scheduler, std::map<unsigned, Thread*> &threadMap);
	~RRThreadScheduler();
	void printName(std::ostream &os) {
		os << "RR Thread Scheduler\n";
	}

	Thread* selectCurrentItem();
	Thread* selectNextItem();
	void popAllItem(std::vector<Thread*>& allItem);
	int itemNum();
	bool isSchedulerEmpty();
	void addItem(Thread* item);
	void removeItem(Thread* item);
	void printAllItem(std::ostream &os);
	void reSchedule();
	void setCountZero();

};

/**
 * FIFS Scheduler, simply queues threads in the order that they arrive in the ready queue
 */
class FIFSThreadScheduler : public ThreadScheduler {
private:
	std::list<Thread*> queue;
public:
	FIFSThreadScheduler();
	FIFSThreadScheduler(FIFSThreadScheduler& scheduler, std::map<unsigned, Thread*> &threadMap);
	~FIFSThreadScheduler();
	void printName(std::ostream &os) {
		os << "FIFS Thread Scheduler\n";
	}

	Thread* selectCurrentItem();
	Thread* selectNextItem();
	void popAllItem(std::vector<Thread*>& allItem);
	int itemNum();
	bool isSchedulerEmpty();
	void addItem(Thread* item);
	void removeItem(Thread* item);
	void printAllItem(std::ostream &os);
	void reSchedule();

};

class PreemptiveThreadScheduler : public ThreadScheduler {
private:
	std::list<Thread*> queue;

public:
	PreemptiveThreadScheduler();
	PreemptiveThreadScheduler(PreemptiveThreadScheduler& scheduler, std::map<unsigned, Thread*> &threadMap);
	~PreemptiveThreadScheduler();
	void printName(std::ostream &os) {
		os << "Preemptive Thread Scheduler\n";
	}

	Thread* selectCurrentItem();
	Thread* selectNextItem();
	void popAllItem(std::vector<Thread*>& allItem);
	int itemNum();
	bool isSchedulerEmpty();
	void addItem(Thread* item);
	void removeItem(Thread* item);
	void printAllItem(std::ostream &os);
	void reSchedule();

};

class GuidedThreadScheduler : public ThreadScheduler {
private:
	Prefix* prefix;
	ThreadScheduler* subScheduler;
	ExecutionState* state;

public:
	GuidedThreadScheduler(ExecutionState* state, ThreadSchedulerType schedulerType, Prefix* prefix);
	//GuidedThreadScheduler(GuidedThreadScheduler& scheduler, std::map<unsigned, Thread*> &threadMap);//拷贝构造，暂时不实现
	~GuidedThreadScheduler();
	void printName(std::ostream &os) {
		os << "Guided Thread Scheduler\n";
	}

	Thread* selectCurrentItem();
	Thread* selectNextItem();
	void popAllItem(std::vector<Thread*>& allItem);
	int itemNum();
	bool isSchedulerEmpty();
	void addItem(Thread* item);
	void removeItem(Thread* item);
	void printAllItem(std::ostream &os);
	void reSchedule();
};

ThreadScheduler* getThreadSchedulerByType(ThreadScheduler::ThreadSchedulerType type);

} /* namespace klee */

#endif /* LIB_CORE_THREADSCHEDULER_H_ */
