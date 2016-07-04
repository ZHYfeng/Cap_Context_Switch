/*
 * Barrierinfo.cpp
 *
 *  Created on: 2015年11月13日
 *      Author: zhy
 */

#include "BarrierInfo.h"

namespace klee{

BarrierInfo::BarrierInfo() {
	this->count = 0x7fffffff;
	this->current = 0;
	this->releasedCount = 0;
}

bool BarrierInfo::addWaitItem() {
	this->current++;
	if (this->current == this->count) {
		return true;
	} else {
		return false;
	}
}

void BarrierInfo::addReleaseItem() {
	this->current = 0;
	this->releasedCount++;
}

BarrierInfo::~BarrierInfo() {

}

}
