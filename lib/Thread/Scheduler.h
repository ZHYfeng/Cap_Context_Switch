/*
 * Scheduler.h
 *
 *  Created on: Jul 24, 2014
 *      Author: klee
 */

#ifndef SCHEDULER_H_
#define SCHEDULER_H_

#include <list>
#include <vector>
#include <iostream>
#include <sys/time.h>
#include "klee/Internal/ADT/RNG.h"

namespace klee {

extern RNG theRNG;

/**
 * 调度器适用于所有类型的调度，线程调度，锁调度，条件变量调度，使用调度器的拷贝构造函数时请注意，调度器的拷贝构造是深拷贝。
 */
template <class Item>
class Scheduler {

public:
	virtual Item selectNextItem() = 0;
	virtual void popAllItem(std::vector<Item>& allItem) = 0;
	virtual int itemNum() = 0;
	virtual bool isQueueEmpty() = 0;
	virtual void printName(std::ostream &os) = 0;
	virtual void addItem(Item& item) = 0;
	virtual void removeItem(Item& item) = 0;
	virtual void printAllItem(std::ostream &os) = 0;
	virtual ~Scheduler();
};

template <class Item>
class DefaultScheduler : public Scheduler<Item> {
private:
	std::list<Item> queue;
public:
	Item selectNextItem();
	void popAllItem(std::vector<Item>& allItem);
	int itemNum();
	bool isQueueEmpty();
	void addItem(Item& item);
	void removeItem(Item& item);
	void printName(std::ostream &os) {
		os << "DefaultScheduler\n";
	}
	void printAllItem(std::ostream &os);
	DefaultScheduler();
	DefaultScheduler(DefaultScheduler<Item>& scheduler);
	~DefaultScheduler();
};

template <class Item>
class RandomScheduler : public Scheduler<Item> {
private:
	std::vector<Item> itemList;
public:
	Item selectNextItem();
	void popAllItem(std::vector<Item>& allItem);
	int itemNum();
	bool isQueueEmpty();
	void addItem(Item& item);
	void removeItem(Item& item);
	void printName(std::ostream &os) {
		os << "RandomScheduler\n";
	}
	void printAllItem(std::ostream &os);
	RandomScheduler();
	RandomScheduler(RandomScheduler<Item>& scheduler);
	~RandomScheduler();
};

template <class Item, class Id>
class GuidedScheduler : public Scheduler<Item> {
private:
	std::vector<Id> idList;
public:
	Item selectNextItem();
	void popAllItem(std::vector<Item>& allItem);
	int itemNum();
	bool isQueueEmpty();
	void printName(std::ostream &os);
	void addItem(Item& item);
	void removeItem(Item& item);
	void printAllItem(std::ostream &os);
	GuidedScheduler();
	GuidedScheduler(std::vector<Id>& idList);
};

template <class Item>
Scheduler<Item>::~Scheduler() {

}

template <class Item>
Item DefaultScheduler<Item>::selectNextItem() {
	Item selected = queue.back();
	queue.pop_back();
	return selected;
}

template <class Item>
void DefaultScheduler<Item>::popAllItem(std::vector<Item>& allItem) {
	typename std::list<Item>::iterator qi, qe;
	typename std::vector<Item>::iterator ai;
	allItem.resize(itemNum());
	for (qi = queue.begin(), qe = queue.end(), ai = allItem.begin(); qi != qe; qi++, ai++) {
		*ai = *qi;
	}
	queue.erase(queue.begin(), queue.end());
}

template <class Item>
int DefaultScheduler<Item>::itemNum() {
	return queue.size();
}

template <class Item>
bool DefaultScheduler<Item>::isQueueEmpty() {
	return queue.empty();
}

template <class Item>
void DefaultScheduler<Item>::addItem(Item& item) {
	queue.push_back(item);
}

template <class Item>
void DefaultScheduler<Item>::removeItem(Item& item) {
	typename std::list<Item>::iterator qi, qe;
	for (qi = queue.begin(), qe = queue.end(); qi != qe; qi++) {
		if (*qi == item) {
			queue.erase(qi);
			break;
		}
	}
}

template <class Item>
void DefaultScheduler<Item>::printAllItem(std::ostream &os) {
	typename std::list<Item>::iterator qi, qe;
	for (qi = queue.begin(), qe = queue.end(); qi != qe; qi++) {
		os << (*qi) << " ";
	}
	os << std::endl;
}

template <class Item>
DefaultScheduler<Item>::DefaultScheduler() {

}

template <class Item>
DefaultScheduler<Item>::DefaultScheduler(DefaultScheduler<Item>& scheduler)
	: queue(scheduler.queue) {

}

template <class Item>
DefaultScheduler<Item>::~DefaultScheduler() {

}

template<class Item>
Item RandomScheduler<Item>::selectNextItem() {
	struct timeval tv;
	gettimeofday(&tv, 0);
	theRNG.seed(tv.tv_usec);
	unsigned index = theRNG.getInt32() % itemList.size();
	Item element = itemList[index];
	itemList.erase(itemList.begin() + index);
	return element;
}

template <class Item>
void RandomScheduler<Item>::popAllItem(std::vector<Item>& allItem) {
	typename std::vector<Item>::iterator ii, ie;
	typename std::vector<Item>::iterator ai;
	allItem.resize(itemNum());
	for (ii = itemList.begin(), ie = itemList.end(), ai = allItem.begin(); ii != ie; ii++, ai++) {
		*ai = *ii;
	}
	itemList.erase(itemList.begin(), itemList.end());
}

template <class Item>
int RandomScheduler<Item>::itemNum() {
	return itemList.size();
}

template<class Item>
bool RandomScheduler<Item>::isQueueEmpty() {
	return itemList.empty();
}

template<class Item>
void RandomScheduler<Item>::addItem(Item& item) {
	return itemList.push_back(item);
}

template<class Item>
void RandomScheduler<Item>::removeItem(Item& item) {
	typename std::vector<Item>::iterator qi, qe;
	for (qi = itemList.begin(), qe = itemList.end(); qi != qe; qi++) {
		if (*qi == item) {
			itemList.erase(qi);
			break;
		}
	}
}

template<class Item>
void RandomScheduler<Item>::printAllItem(std::ostream &os) {
	typename std::vector<Item>::iterator qi, qe;
	for (qi = itemList.begin(), qe = itemList.end(); qi != qe; qi++) {
		os << (*qi) << " ";
	}
	os << std::endl;
}

template <class Item>
RandomScheduler<Item>::RandomScheduler() {
	itemList.reserve(10);
}

template <class Item>
RandomScheduler<Item>::RandomScheduler(RandomScheduler<Item>& scheduler)
	: itemList(scheduler.itemList) {

}

template <class Item>
RandomScheduler<Item>::~RandomScheduler() {

}

}
#endif /* SCHEDULER_H_ */
