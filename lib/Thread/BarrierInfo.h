/*
 * BarrierInfo.h
 *
 *  Created on: 2015年11月13日
 *      Author: zhy
 */

#ifndef LIB_CORE_BARRIERINFO_H_
#define LIB_CORE_BARRIERINFO_H_


namespace klee {

class BarrierInfo {
public:

	unsigned count;
	unsigned current;
	unsigned releasedCount;
	BarrierInfo();
	~BarrierInfo();
	bool addWaitItem();
	void addReleaseItem();

};

}

#endif /* LIB_CORE_BARRIERINFO_H_ */
