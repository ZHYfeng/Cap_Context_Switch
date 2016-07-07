/*
 * BitcodeListener.cpp
 *
 *  Created on: May 16, 2014
 *      Author: ylc
 */

#include "BitcodeListener.h"

namespace klee {

	BitcodeListener::BitcodeListener(RuntimeDataManager* rdManager) :
			kind(defaultKind), rdManager(rdManager) {

	}

	BitcodeListener::~BitcodeListener() {

	}
}
