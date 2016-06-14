/*
 * CondScheduler.h
 *
 *  Created on: 2015年1月7日
 *      Author: berserker
 */

#ifndef LIB_CORE_CONDSCHEDULER_H_
#define LIB_CORE_CONDSCHEDULER_H_

#include "WaitParam.h"
#include "Prefix.h"
#include "klee/Internal/ADT/RNG.h"
#include <list>
#include <vector>
#include <iostream>

namespace klee {

class CondScheduler {
public:
	enum CondSchedulerType {
		FIFS,
		Preemptive,
		Random
	};
	CondScheduler();
	virtual ~CondScheduler();
	virtual WaitParam* selectNextItem() = 0;
	virtual void popAllItem(std::vector<WaitParam*>& allItem) = 0;
	virtual int itemNum() = 0;
	virtual bool isQueueEmpty() = 0;
	virtual void printName(std::ostream &os) = 0;
	virtual void addItem(WaitParam* param) = 0;
	virtual WaitParam* removeItem(WaitParam* param) = 0;
	virtual WaitParam* removeItem(unsigned threadId) = 0;
	virtual void printAllItem(std::ostream &os) = 0;
};

class FIFSCondScheduler : public CondScheduler {
private:
	std::list<WaitParam*> queue;
public:
	void printName(std::ostream &os) {
		os << "FIFS Condition Scheduler\n";
	}
	FIFSCondScheduler();
	virtual ~FIFSCondScheduler();
	WaitParam* selectNextItem();
	void popAllItem(std::vector<WaitParam*>& allItem);
	int itemNum();
	bool isQueueEmpty();
	void addItem(WaitParam* param);
	WaitParam* removeItem(WaitParam* param);
	WaitParam* removeItem(unsigned threadId);
	void printAllItem(std::ostream &os);
};

class PreemptiveCondScheduler : public CondScheduler {
private:
	std::list<WaitParam*> queue;
public:
	void printName(std::ostream &os) {
		os << "Preemptive Condition Scheduler\n";
	}
	PreemptiveCondScheduler();
	virtual ~PreemptiveCondScheduler();
	WaitParam* selectNextItem();
	void popAllItem(std::vector<WaitParam*>& allItem);
	int itemNum();
	bool isQueueEmpty();
	void addItem(WaitParam* param);
	WaitParam* removeItem(WaitParam* param);
	WaitParam* removeItem(unsigned threadId);
	void printAllItem(std::ostream &os);
};

class GuidedCondScheduler : public CondScheduler {
private:
	CondScheduler* baseScheduler;
	Prefix* prefix;

public:
	void printName(std::ostream &os) {
		os << "Guided Condition Scheduler\n";
	}
	GuidedCondScheduler(CondSchedulerType secondarySchedulerType, Prefix* prefix);
	virtual ~GuidedCondScheduler();
	WaitParam* selectNextItem();
	void popAllItem(std::vector<WaitParam*>& allItem);
	int itemNum();
	bool isQueueEmpty();
	void addItem(WaitParam* param);
	WaitParam* removeItem(WaitParam* param);
	WaitParam* removeItem(unsigned threadId);
	void printAllItem(std::ostream &os);
};

CondScheduler* getCondSchedulerByType(CondScheduler::CondSchedulerType type);

} /* namespace klee */

#endif /* LIB_CORE_CONDSCHEDULER_H_ */
