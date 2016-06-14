/*
 * WaitParam.cpp
 *
 *  Created on: Jul 24, 2014
 *      Author: ylc
 */

#include "WaitParam.h"

using namespace::std;

namespace klee {

WaitParam::WaitParam() {
	// TODO Auto-generated constructor stub

}

WaitParam::WaitParam(string mutexName, unsigned threadId) {
	this->mutexName = mutexName;
	this->threadId = threadId;
}

WaitParam::~WaitParam() {
	// TODO Auto-generated destructor stub
}

}
