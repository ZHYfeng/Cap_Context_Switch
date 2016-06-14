/*
 * ThreadList.h
 *
 *  Created on: 2015年1月14日
 *      Author: berserker
 */

#ifndef THREADLIST_H_
#define THREADLIST_H_

#include <vector>
#include <map>
#include "Thread.h"

namespace klee {
class ThreadListIterator;

class ThreadList {
	friend class ThreadListIterator;

private:
	std::vector<Thread*> allThreads;
	std::vector<unsigned> index;
	int threadNum;

public:
	typedef ThreadListIterator iterator;
	ThreadList();
	virtual ~ThreadList();
	iterator begin();
	iterator end();
	iterator begin() const;
	iterator end() const;
	void addThread(Thread* thread);
	std::map<unsigned, Thread*> getAllUnfinishedThreads();
	Thread* findThreadById(unsigned threadId);
	int getThreadNum();
	Thread* getLastThread();
};

class ThreadListIterator {

public:
	const ThreadList* threadList;
	unsigned currentPos;

public:
	ThreadListIterator(ThreadList* threadList, unsigned pos = 0);
	ThreadListIterator(const ThreadList* threadList, unsigned pos = 0);
	ThreadListIterator& operator ++(int);
	Thread* operator*();
	bool operator==(ThreadListIterator& another);
	bool operator!=(ThreadListIterator& another);
};

} /* namespace klee */

#endif /* THREADLIST_H_ */
