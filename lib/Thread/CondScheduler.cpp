/*
 * CondScheduler.cpp
 *
 *  Created on: 2015年1月7日
 *      Author: berserker
 */

#include "CondScheduler.h"

#include "../Encode/Event.h"

#include <stddef.h>
#include <iterator>

using namespace::std;

namespace klee {

CondScheduler* getCondSchedulerByType(CondScheduler::CondSchedulerType type) {
	CondScheduler* scheduler;
	switch (type) {
	case CondScheduler::FIFS: {
		scheduler = new FIFSCondScheduler();
		break;
	}

	case CondScheduler::Preemptive: {
		scheduler = new PreemptiveCondScheduler();
		break;
	}

	case CondScheduler::Random: {
//		scheduler = new RandomCondScheduler();
		break;
	}
	default: {
		scheduler = new FIFSCondScheduler();
	}

	}
	return scheduler;
}

CondScheduler::CondScheduler() {
	// TODO Auto-generated constructor stub

}

CondScheduler::~CondScheduler() {
	// TODO Auto-generated destructor stub
}

FIFSCondScheduler::FIFSCondScheduler() {

}

FIFSCondScheduler::~FIFSCondScheduler() {

}

WaitParam* FIFSCondScheduler::selectNextItem() {
	WaitParam* item = queue.front();
	queue.pop_front();
	return item;
}

void FIFSCondScheduler::popAllItem(vector<WaitParam*>& allItem) {
	allItem.reserve(queue.size());
	for (list<WaitParam*>::iterator ii = queue.begin(), ie = queue.end(); ii != ie; ii++) {
		allItem.push_back(*ii);
	}
	queue.clear();
}

int FIFSCondScheduler::itemNum() {
	return queue.size();
}

bool FIFSCondScheduler::isQueueEmpty() {
	return queue.empty();
}

void FIFSCondScheduler::addItem(WaitParam* param) {
	queue.push_back(param);
}

WaitParam* FIFSCondScheduler::removeItem(WaitParam* param) {
	WaitParam* result = NULL;
	for (list<WaitParam*>::iterator ii = queue.begin(), ie = queue.end(); ii != ie; ii++) {
		if (*ii == param) {
			result = *ii;
			queue.erase(ii);
			break;
		}
	}
	return result;
}

WaitParam* FIFSCondScheduler::removeItem(unsigned threadId) {
	WaitParam* result = NULL;
	for (list<WaitParam*>::iterator ii = queue.begin(), ie = queue.end(); ii != ie; ii++) {
		if ((*ii)->threadId == threadId) {
			result = *ii;
			queue.erase(ii);
			break;
		}
	}
	return result;
}

void FIFSCondScheduler::printAllItem(ostream &os) {
	for (list<WaitParam*>::iterator ii = queue.begin(), ie = queue.end(); ii != ie; ii++) {
		os << (*ii)->threadId << " ";
	}
	os << endl;
}

PreemptiveCondScheduler::PreemptiveCondScheduler() {

}

PreemptiveCondScheduler::~PreemptiveCondScheduler() {

}

WaitParam* PreemptiveCondScheduler::selectNextItem() {
	WaitParam* item = queue.back();
	queue.pop_back();
	return item;
}

void PreemptiveCondScheduler::popAllItem(vector<WaitParam*>& allItem) {
	allItem.reserve(queue.size());
	for (list<WaitParam*>::iterator ii = queue.begin(), ie = queue.end(); ii != ie; ii++) {
		allItem.push_back(*ii);
	}
	queue.clear();
}

int PreemptiveCondScheduler::itemNum() {
	return queue.size();
}

bool PreemptiveCondScheduler::isQueueEmpty() {
	return queue.empty();
}

void PreemptiveCondScheduler::addItem(WaitParam* param) {
	queue.push_back(param);
}

WaitParam* PreemptiveCondScheduler::removeItem(WaitParam* param) {
	WaitParam* result = NULL;
	for (list<WaitParam*>::iterator ii = queue.begin(), ie = queue.end(); ii != ie; ii++) {
		if (*ii == param) {
			result = *ii;
			queue.erase(ii);
			break;
		}
	}
	return result;
}

WaitParam* PreemptiveCondScheduler::removeItem(unsigned threadId) {
	WaitParam* result = NULL;
	for (list<WaitParam*>::iterator ii = queue.begin(), ie = queue.end(); ii != ie; ii++) {
		if ((*ii)->threadId == threadId) {
			result = *ii;
			queue.erase(ii);
			break;
		}
	}
	return result;
}

void PreemptiveCondScheduler::printAllItem(ostream &os) {
	for (list<WaitParam*>::iterator ii = queue.begin(), ie = queue.end(); ii != ie; ii++) {
		os << (*ii)->threadId << " ";
	}
	os << endl;
}

GuidedCondScheduler::GuidedCondScheduler(CondSchedulerType secondarySchedulerType, Prefix* prefix)
	: prefix(prefix) {
	baseScheduler = getCondSchedulerByType(secondarySchedulerType);
}

GuidedCondScheduler::~GuidedCondScheduler() {
	delete baseScheduler;
}

WaitParam* GuidedCondScheduler::selectNextItem() {
	if (prefix->isFinished()) {
		return baseScheduler->selectNextItem();
	} else {
		unsigned lastThreadId = 0;
		unsigned threadId = 0;
		WaitParam* result = NULL;
		for (Prefix::EventIterator ei = prefix->current(), ee = prefix->end(); ei != ee; ei++) {
			threadId = (*ei)->threadId;
			if (threadId == lastThreadId) {
				continue;
			} else {
				lastThreadId = threadId;
				result = baseScheduler->removeItem(threadId);
				if (result) {
					break;
				}
			}
		}
		if (!result && !baseScheduler->isQueueEmpty()) {
			result = baseScheduler->selectNextItem();
		}
		return result;
	}
}

void GuidedCondScheduler::popAllItem(std::vector<WaitParam*>& allItem) {
	baseScheduler->popAllItem(allItem);
}

int GuidedCondScheduler::itemNum() {
	return baseScheduler->itemNum();
}

bool GuidedCondScheduler::isQueueEmpty() {
	return baseScheduler->isQueueEmpty();
}

void GuidedCondScheduler::addItem(WaitParam* param) {
	baseScheduler->addItem(param);
}

WaitParam* GuidedCondScheduler::removeItem(WaitParam* param) {
	return baseScheduler->removeItem(param);
}

WaitParam* GuidedCondScheduler::removeItem(unsigned threadId) {
	return baseScheduler->removeItem(threadId);
}

void GuidedCondScheduler::printAllItem(ostream &os) {
	baseScheduler->printAllItem(os);
}

} /* namespace klee */
