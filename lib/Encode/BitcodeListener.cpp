/*
 * BitcodeListener.cpp
 *
 *  Created on: May 16, 2014
 *      Author: ylc
 */

#include "BitcodeListener.h"

#include "../Thread/StackType.h"

namespace klee {

	BitcodeListener::BitcodeListener(RuntimeDataManager* rdManager) :
			kind(defaultKind), rdManager(rdManager) {
		stack[1] = new StackType(&addressSpace);
		stack[1]->realStack.reserve(10);
	}

	BitcodeListener::~BitcodeListener() {
		for (std::map<unsigned, StackType*>::iterator it = stack.begin(), ie = stack.end(); it != ie; it++) {
			if (it->second) {
				delete it->second;
			}
		}
	}
}
